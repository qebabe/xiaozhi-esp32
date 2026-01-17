#ifndef ANIMATED_EMOTION_H
#define ANIMATED_EMOTION_H

#include <lvgl.h>
#include <chrono>

// 眼睛形状参数
struct EyeShape {
    int width = 24;        // 眼睛宽度
    int height = 16;       // 眼睛高度
    int border_radius = 12; // 边框圆角
    int pupil_size = 6;    // 瞳孔大小
    int space_between = 2; // 双眼间距
};

// 动画参数
struct AnimationParams {
    int blink_interval_ms = 3000;  // 眨眼间隔
    int blink_duration_ms = 150;   // 眨眼持续时间
    float max_frame_rate = 30.0f;  // 最大帧率
};

// 表情类型枚举
enum class EmotionType {
    NEUTRAL,     // 中性
    HAPPY,       // 开心
    SAD,         // 悲伤
    ANGRY,       // 生气
    SURPRISED,   // 惊讶
    THINKING,    // 思考
    SLEEPY,      // 困倦
    WINKING,     // 眨眼
    CONFUSED     // 困惑
};

// 动画状态
enum class AnimationState {
    IDLE,        // 空闲
    BLINKING,    // 眨眼中
    MOVING,      // 移动中
    EXPRESSING   // 表情变化中
};

// 位置方向
enum class EyeDirection {
    CENTER,      // 中央
    LEFT,        // 左
    RIGHT,       // 右
    UP,          // 上
    DOWN,        // 下
    UP_LEFT,     // 左上
    UP_RIGHT,    // 右上
    DOWN_LEFT,   // 左下
    DOWN_RIGHT   // 右下
};

class AnimatedEmotion {
public:
    AnimatedEmotion(int canvas_width = 32, int canvas_height = 32);
    ~AnimatedEmotion();

    // 初始化画布
    bool Initialize(lv_obj_t* parent);

    // 设置表情类型
    void SetEmotion(EmotionType emotion);

    // 设置眼睛方向
    void SetDirection(EyeDirection direction);

    // 设置眨眼间隔
    void SetBlinkInterval(int interval_ms);

    // 更新动画（需要在主循环中调用）
    void Update();

    // 暂停/恢复动画
    void PauseAnimation(bool pause);

    // 获取当前表情类型
    EmotionType GetCurrentEmotion() const { return current_emotion_; }

    // 获取画布对象
    lv_obj_t* GetCanvas() const { return canvas_; }

private:
    // 绘制函数
    void DrawEye(int center_x, int center_y, const EyeShape& shape, float openness = 1.0f);
    void DrawPupil(int center_x, int center_y, int pupil_x, int pupil_y, int size);
    void DrawEyelid(int center_x, int center_y, const EyeShape& shape, float openness);

    // 动画函数
    void UpdateBlink();
    void UpdatePosition();
    void UpdateExpression();

    // 工具函数
    lv_point_t GetDirectionOffset(EyeDirection direction, int max_offset = 3);
    EyeShape GetEmotionShape(EmotionType emotion);

    // 画布相关
    lv_obj_t* canvas_ = nullptr;
    lv_color_t* canvas_buffer_ = nullptr;
    int canvas_width_;
    int canvas_height_;

    // 当前状态
    EmotionType current_emotion_ = EmotionType::NEUTRAL;
    AnimationState animation_state_ = AnimationState::IDLE;
    EyeDirection current_direction_ = EyeDirection::CENTER;

    // 动画参数
    AnimationParams anim_params_;

    // 眨眼相关
    bool is_blinking_ = false;
    float blink_progress_ = 0.0f;  // 0.0 = 睁眼, 1.0 = 闭眼
    std::chrono::steady_clock::time_point last_blink_time_;
    int blink_interval_ms_ = 3000;

    // 位置相关
    EyeDirection target_direction_ = EyeDirection::CENTER;
    float position_progress_ = 0.0f;  // 位置变化进度 0.0-1.0

    // 表情变化相关
    EmotionType target_emotion_ = EmotionType::NEUTRAL;
    float expression_progress_ = 0.0f;  // 表情变化进度 0.0-1.0

    // 控制标志
    bool is_paused_ = false;
    bool is_initialized_ = false;

    // 基础形状
    EyeShape base_shape_;
    
    // LVGL 定时器（当使用定时器驱动时，外部不需要调用 Update()）
    lv_timer_t* update_timer_ = nullptr;
    
public:
    // 启动/停止定时器（通常在 Initialize() 内部处理）
    void StartTimer(int fps);
    void StopTimer();

    // 如果返回 true，表示内部使用 LVGL 定时器驱动动画，外部无需再调用 Update()
    bool IsTimerDriven() const { return update_timer_ != nullptr; }
};

#endif // ANIMATED_EMOTION_H