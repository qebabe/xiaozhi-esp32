#include "animated_emotion.h"
#include <esp_log.h>
#include <cstring>
#include <cmath>
#include <unordered_map>

// lv_timer API may not be exposed as a standalone header in some LVGL setups.
// Declare the minimal timer API we need here to avoid depending on lv_timer.h.
extern "C" {
typedef struct _lv_timer_t lv_timer_t;
typedef void (*lv_timer_cb_t)(lv_timer_t*);
lv_timer_t* lv_timer_create(lv_timer_cb_t cb, uint32_t period, void* user_data);
void lv_timer_del(lv_timer_t* timer);
void* lv_timer_get_user_data(lv_timer_t* timer);
void lv_timer_set_user_data(lv_timer_t* timer, void* user_data);
}

#define TAG "AnimatedEmotion"

// 全局映射：timer -> AnimatedEmotion*
static std::unordered_map<lv_timer_t*, AnimatedEmotion*> g_timer_map;

// LVGL 定时器回调：在 LVGL 线程上下文中调用 AnimatedEmotion::Update()
static void AnimatedEmotionTimerCallback(lv_timer_t* timer) {
    if (timer == nullptr) return;
    auto it = g_timer_map.find(timer);
    if (it == g_timer_map.end()) return;
    AnimatedEmotion* self = it->second;
    if (self != nullptr) {
        self->Update();
    }
}

AnimatedEmotion::AnimatedEmotion(int canvas_width, int canvas_height)
    : canvas_width_(canvas_width), canvas_height_(canvas_height) {

    // 初始化基础眼睛形状
    base_shape_.width = 20;
    base_shape_.height = 12;
    base_shape_.border_radius = 10;
    base_shape_.pupil_size = 4;
    base_shape_.space_between = 2;

    // 初始化动画参数
    anim_params_.blink_interval_ms = 3000;
    anim_params_.blink_duration_ms = 150;
    anim_params_.max_frame_rate = 30.0f;
}

AnimatedEmotion::~AnimatedEmotion() {
    if (canvas_buffer_ != nullptr) {
        delete[] canvas_buffer_;
        canvas_buffer_ = nullptr;
    }
    if (update_timer_ != nullptr) {
        lv_timer_del(update_timer_);
        update_timer_ = nullptr;
    }
}

bool AnimatedEmotion::Initialize(lv_obj_t* parent) {
    if (is_initialized_) {
        return true;
    }

    // 创建画布缓冲区
    canvas_buffer_ = new lv_color_t[canvas_width_ * canvas_height_];
    if (canvas_buffer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer");
        return false;
    }

    // 创建LVGL画布
    canvas_ = lv_canvas_create(parent);
    if (canvas_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create canvas");
        delete[] canvas_buffer_;
        canvas_buffer_ = nullptr;
        return false;
    }

    // 配置画布
    lv_canvas_set_buffer(canvas_, canvas_buffer_, canvas_width_, canvas_height_, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(canvas_, canvas_width_, canvas_height_);

    // 清除画布为黑色背景
    lv_canvas_fill_bg(canvas_, lv_color_black(), LV_OPA_COVER);

    // 初始化时间戳
    last_blink_time_ = std::chrono::steady_clock::now();

    is_initialized_ = true;
    ESP_LOGI(TAG, "Animated emotion initialized successfully");
    // NOTE: Creating LVGL timers at Initialize() has caused instability on some boards
    // (observed assertion in esp_timer/timer_task). To restore system stability we
    // avoid auto-creating the LVGL timer here. Call StartTimer() explicitly if
    // you want to enable LVGL timer-driven updates at runtime.
    update_timer_ = nullptr;
    return true;
}

void AnimatedEmotion::SetEmotion(EmotionType emotion) {
    if (!is_initialized_) {
        return;
    }

    if (current_emotion_ != emotion) {
        target_emotion_ = emotion;
        expression_progress_ = 0.0f;
        animation_state_ = AnimationState::EXPRESSING;
    }
}

void AnimatedEmotion::SetDirection(EyeDirection direction) {
    if (!is_initialized_) {
        return;
    }

    if (current_direction_ != direction) {
        target_direction_ = direction;
        position_progress_ = 0.0f;
        animation_state_ = AnimationState::MOVING;
    }
}

void AnimatedEmotion::SetBlinkInterval(int interval_ms) {
    blink_interval_ms_ = interval_ms;
}

void AnimatedEmotion::Update() {
    if (!is_initialized_ || is_paused_) {
        return;
    }

    // 简单的帧率控制，避免更新太频繁
    static std::chrono::steady_clock::time_point last_update;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count();

    if (elapsed < (1000.0f / anim_params_.max_frame_rate)) {
        return; // 还没到更新时间
    }
    last_update = now;

    // 清除画布
    lv_canvas_fill_bg(canvas_, lv_color_black(), LV_OPA_COVER);

    // 更新各种动画状态
    UpdateBlink();
    UpdatePosition();
    UpdateExpression();

    // 获取当前状态的眼睛形状
    EyeShape current_shape = GetEmotionShape(current_emotion_);

    // 计算左右眼的中心位置
    int left_eye_center_x = canvas_width_ / 2 - current_shape.space_between / 2 - current_shape.width / 2;
    int right_eye_center_x = canvas_width_ / 2 + current_shape.space_between / 2 + current_shape.width / 2;
    int eye_center_y = canvas_height_ / 2;

    // 计算瞳孔位置偏移
    lv_point_t pupil_offset = GetDirectionOffset(current_direction_, 3);

    // 绘制左眼
    DrawEye(left_eye_center_x, eye_center_y, current_shape, 1.0f - blink_progress_);
    DrawPupil(left_eye_center_x, eye_center_y, left_eye_center_x + pupil_offset.x, eye_center_y + pupil_offset.y, current_shape.pupil_size);

    // 绘制右眼
    DrawEye(right_eye_center_x, eye_center_y, current_shape, 1.0f - blink_progress_);
    DrawPupil(right_eye_center_x, eye_center_y, right_eye_center_x + pupil_offset.x, eye_center_y + pupil_offset.y, current_shape.pupil_size);

    // 如果在眨眼中，绘制眼皮
    if (blink_progress_ > 0.0f) {
        DrawEyelid(left_eye_center_x, eye_center_y, current_shape, 1.0f - blink_progress_);
        DrawEyelid(right_eye_center_x, eye_center_y, current_shape, 1.0f - blink_progress_);
    }

    // 刷新画布显示
    lv_obj_invalidate(canvas_);
}

void AnimatedEmotion::PauseAnimation(bool pause) {
    is_paused_ = pause;
    if (!pause) {
        // 恢复时重置时间戳
        last_blink_time_ = std::chrono::steady_clock::now();
    }
}

void AnimatedEmotion::StartTimer(int fps) {
    if (!is_initialized_) return;
    if (update_timer_ != nullptr) return;
    int period_ms = 33;
    if (fps > 0) {
        period_ms = static_cast<int>(1000 / fps);
    } else if (anim_params_.max_frame_rate > 0.0f) {
        period_ms = static_cast<int>(1000.0f / anim_params_.max_frame_rate);
    }
    update_timer_ = lv_timer_create(AnimatedEmotionTimerCallback, static_cast<uint32_t>(period_ms), this);
    if (update_timer_ == nullptr) {
        ESP_LOGW(TAG, "StartTimer: failed to create LVGL timer");
    }
}

void AnimatedEmotion::StopTimer() {
    if (update_timer_ != nullptr) {
        g_timer_map.erase(update_timer_);
        lv_timer_del(update_timer_);
        update_timer_ = nullptr;
    }
}

void AnimatedEmotion::DrawEye(int center_x, int center_y, const EyeShape& shape, float openness) {
    if (!is_initialized_ || openness <= 0.0f) {
        return;
    }

    // 计算睁眼高度
    int eye_height = static_cast<int>(shape.height * openness);
    if (eye_height < 2) eye_height = 2;

    // 初始化layer进行绘制
    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);

    // 绘制眼睛外框（椭圆）
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_white();
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.radius = shape.border_radius;
    rect_dsc.border_width = 1;
    rect_dsc.border_color = lv_color_make(200, 200, 200);

    lv_area_t eye_area = {
        .x1 = static_cast<lv_coord_t>(center_x - shape.width / 2),
        .y1 = static_cast<lv_coord_t>(center_y - eye_height / 2),
        .x2 = static_cast<lv_coord_t>(center_x + shape.width / 2),
        .y2 = static_cast<lv_coord_t>(center_y + eye_height / 2)
    };

    lv_draw_rect(&layer, &rect_dsc, &eye_area);

    // 完成绘制
    lv_canvas_finish_layer(canvas_, &layer);
}

void AnimatedEmotion::DrawPupil(int eye_center_x, int eye_center_y, int pupil_x, int pupil_y, int size) {
    if (!is_initialized_) {
        return;
    }

    // 初始化layer进行绘制
    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);

    // 绘制瞳孔（使用填充的圆形近似）
    lv_draw_rect_dsc_t pupil_dsc;
    lv_draw_rect_dsc_init(&pupil_dsc);
    pupil_dsc.bg_color = lv_color_black();
    pupil_dsc.bg_opa = LV_OPA_COVER;
    pupil_dsc.radius = size;  // 使用圆角矩形近似圆形

    lv_area_t pupil_area = {
        .x1 = static_cast<lv_coord_t>(pupil_x - size),
        .y1 = static_cast<lv_coord_t>(pupil_y - size),
        .x2 = static_cast<lv_coord_t>(pupil_x + size),
        .y2 = static_cast<lv_coord_t>(pupil_y + size)
    };

    lv_draw_rect(&layer, &pupil_dsc, &pupil_area);

    // 绘制高光
    lv_draw_rect_dsc_t highlight_dsc;
    lv_draw_rect_dsc_init(&highlight_dsc);
    highlight_dsc.bg_color = lv_color_white();
    highlight_dsc.bg_opa = LV_OPA_COVER;
    highlight_dsc.radius = size / 3;

    int highlight_size = size / 3;
    lv_area_t highlight_area = {
        .x1 = static_cast<lv_coord_t>(pupil_x - highlight_size + size/4),
        .y1 = static_cast<lv_coord_t>(pupil_y - highlight_size + size/4),
        .x2 = static_cast<lv_coord_t>(pupil_x + highlight_size + size/4),
        .y2 = static_cast<lv_coord_t>(pupil_y + highlight_size + size/4)
    };

    lv_draw_rect(&layer, &highlight_dsc, &highlight_area);

    // 完成绘制
    lv_canvas_finish_layer(canvas_, &layer);
}

void AnimatedEmotion::DrawEyelid(int center_x, int center_y, const EyeShape& shape, float openness) {
    if (!is_initialized_ || openness >= 1.0f) {
        return;
    }

    // 初始化layer进行绘制
    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);

    // 计算眼皮位置
    int eyelid_y = center_y - static_cast<int>(shape.height * openness / 2);

    // 绘制上眼皮
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();
    rect_dsc.bg_opa = LV_OPA_COVER;

    lv_area_t eyelid_area = {
        .x1 = static_cast<lv_coord_t>(center_x - shape.width / 2),
        .y1 = static_cast<lv_coord_t>(center_y - shape.height / 2),
        .x2 = static_cast<lv_coord_t>(center_x + shape.width / 2),
        .y2 = static_cast<lv_coord_t>(eyelid_y)
    };

    lv_draw_rect(&layer, &rect_dsc, &eyelid_area);

    // 完成绘制
    lv_canvas_finish_layer(canvas_, &layer);
}

void AnimatedEmotion::UpdateBlink() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_blink_time_).count();

    if (animation_state_ == AnimationState::BLINKING) {
        // 正在眨眼中
        blink_progress_ += 1.0f / (anim_params_.blink_duration_ms / (1000.0f / anim_params_.max_frame_rate));

        if (blink_progress_ >= 1.0f) {
            // 眨眼完成
            blink_progress_ = 0.0f;
            animation_state_ = AnimationState::IDLE;
            last_blink_time_ = now;
        }
    } else if (elapsed >= blink_interval_ms_) {
        // 开始眨眼
        animation_state_ = AnimationState::BLINKING;
        blink_progress_ = 0.0f;
    }
}

void AnimatedEmotion::UpdatePosition() {
    if (animation_state_ == AnimationState::MOVING) {
        position_progress_ += 1.0f / (500.0f / (1000.0f / anim_params_.max_frame_rate)); // 500ms过渡时间

        if (position_progress_ >= 1.0f) {
            current_direction_ = target_direction_;
            position_progress_ = 0.0f;
            animation_state_ = AnimationState::IDLE;
        }
    }
}

void AnimatedEmotion::UpdateExpression() {
    if (animation_state_ == AnimationState::EXPRESSING) {
        expression_progress_ += 1.0f / (800.0f / (1000.0f / anim_params_.max_frame_rate)); // 800ms过渡时间

        if (expression_progress_ >= 1.0f) {
            current_emotion_ = target_emotion_;
            expression_progress_ = 0.0f;
            animation_state_ = AnimationState::IDLE;
        }
    }
}

lv_point_t AnimatedEmotion::GetDirectionOffset(EyeDirection direction, int max_offset) {
    lv_point_t offset = {0, 0};

    switch (direction) {
        case EyeDirection::CENTER:
            offset.x = 0;
            offset.y = 0;
            break;
        case EyeDirection::LEFT:
            offset.x = -max_offset;
            offset.y = 0;
            break;
        case EyeDirection::RIGHT:
            offset.x = max_offset;
            offset.y = 0;
            break;
        case EyeDirection::UP:
            offset.x = 0;
            offset.y = -max_offset;
            break;
        case EyeDirection::DOWN:
            offset.x = 0;
            offset.y = max_offset;
            break;
        case EyeDirection::UP_LEFT:
            offset.x = -max_offset;
            offset.y = -max_offset;
            break;
        case EyeDirection::UP_RIGHT:
            offset.x = max_offset;
            offset.y = -max_offset;
            break;
        case EyeDirection::DOWN_LEFT:
            offset.x = -max_offset;
            offset.y = max_offset;
            break;
        case EyeDirection::DOWN_RIGHT:
            offset.x = max_offset;
            offset.y = max_offset;
            break;
    }

    // 在过渡期间进行插值
    if (animation_state_ == AnimationState::MOVING) {
        lv_point_t target_offset = GetDirectionOffset(target_direction_, max_offset);
        lv_point_t current_offset = GetDirectionOffset(current_direction_, max_offset);

        offset.x = current_offset.x + static_cast<int>((target_offset.x - current_offset.x) * position_progress_);
        offset.y = current_offset.y + static_cast<int>((target_offset.y - current_offset.y) * position_progress_);
    }

    return offset;
}

EyeShape AnimatedEmotion::GetEmotionShape(EmotionType emotion) {
    EyeShape shape = base_shape_;

    // 根据表情调整形状
    switch (emotion) {
        case EmotionType::HAPPY:
            shape.height = static_cast<int>(base_shape_.height * 0.8f);  // 开心时眼睛稍微细长
            shape.border_radius = static_cast<int>(base_shape_.border_radius * 1.2f);
            break;
        case EmotionType::SAD:
            shape.height = static_cast<int>(base_shape_.height * 1.3f);  // 悲伤时眼睛变圆
            break;
        case EmotionType::ANGRY:
            shape.height = static_cast<int>(base_shape_.height * 0.7f);  // 生气时眼睛细长
            shape.border_radius = static_cast<int>(base_shape_.border_radius * 0.8f);
            break;
        case EmotionType::SURPRISED:
            shape.width = static_cast<int>(base_shape_.width * 1.2f);   // 惊讶时眼睛变大
            shape.height = static_cast<int>(base_shape_.height * 1.2f);
            break;
        case EmotionType::SLEEPY:
            shape.height = static_cast<int>(base_shape_.height * 0.5f);  // 困倦时眼睛变小
            break;
        default:
            break;
    }

    // 在表情过渡期间进行插值
    if (animation_state_ == AnimationState::EXPRESSING) {
        EyeShape target_shape = GetEmotionShape(target_emotion_);

        shape.width = base_shape_.width + static_cast<int>((target_shape.width - base_shape_.width) * expression_progress_);
        shape.height = base_shape_.height + static_cast<int>((target_shape.height - base_shape_.height) * expression_progress_);
        shape.border_radius = base_shape_.border_radius + static_cast<int>((target_shape.border_radius - base_shape_.border_radius) * expression_progress_);
    }

    return shape;
}