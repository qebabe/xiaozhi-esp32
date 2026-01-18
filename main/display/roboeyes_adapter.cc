#include "roboeyes_adapter.h"
#include <esp_log.h>
#include <cstring>
#include <esp_timer.h>
#include <esp_system.h>
#include <stdlib.h>
#include <stdint.h>
#include <unordered_map>
#include <vector>
// Provide Arduino-like 'byte' type used by RoboEyes header
using byte = uint8_t;
// forward-declare Arduino-like helpers used inside the header
extern "C" unsigned long millis();
long random(long v);
// Include vendored RoboEyes header (vendored path)
#include "third_party/roboeyes/src/FluxGarage_RoboEyes.h"
#include <algorithm>

// Provide Arduino-like millis() and random() used by RoboEyes header
extern "C" unsigned long millis() {
    return static_cast<unsigned long>(esp_timer_get_time() / 1000ULL);
}

long random(long v) {
    if (v <= 0) return 0;
    return (long)(rand() % (uint32_t)v);
}

// Adafruit-like shim used by RoboEyes (file-scoped so type is visible)
class AdafruitShim {
public:
    AdafruitShim(uint8_t* bitbuf, int w, int h, RoboEyesAdapter* parent, const char* tag) : bitbuf_(bitbuf), w_(w), h_(h), parent_(parent), tag_(tag) {
        stride_ = (w_ + 7) / 8;
    }
    void clearDisplay() {
        if (bitbuf_) {
            memset(bitbuf_, 0x00, stride_ * h_);
            if (parent_ && parent_->verbose_logging()) ESP_LOGI(tag_, "AdafruitShim: clearDisplay called");
            if (parent_) parent_->SetDrewThisFrame(true);
        }
    }
    void display() {
        // no-op; adapter will invalidate LVGL canvas after RoboEyes update
    }
    void fillRoundRect(int x, int y, int w, int h, int r, uint8_t color) {
        bool on = color ? true : false;
        if (!bitbuf_) return;
        int x1 = x;
        int y1 = y;
        int x2 = x + w - 1;
        int y2 = y + h - 1;
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 >= w_) x2 = w_ - 1;
        if (y2 >= h_) y2 = h_ - 1;
        if (r > w/2) r = w/2;
        if (r > h/2) r = h/2;
        if (parent_ && parent_->verbose_logging()) ESP_LOGI(tag_, "AdafruitShim: fillRoundRect x=%d y=%d w=%d h=%d r=%d color=%d", x, y, w, h, r, color);
        if (parent_) parent_->SetDrewThisFrame(true);
        for (int yy = y1; yy <= y2; ++yy) {
            uint8_t* row = bitbuf_ + yy * stride_;
            for (int xx = x1; xx <= x2; ++xx) {
                bool should_draw = true;

                // Check if pixel is in a corner that should be rounded
                if (xx < x1 + r && yy < y1 + r) {
                    // Top-left corner
                    int dx = (x1 + r) - xx;
                    int dy = (y1 + r) - yy;
                    if (dx*dx + dy*dy > r*r) should_draw = false;
                } else if (xx > x2 - r && yy < y1 + r) {
                    // Top-right corner
                    int dx = xx - (x2 - r);
                    int dy = (y1 + r) - yy;
                    if (dx*dx + dy*dy > r*r) should_draw = false;
                } else if (xx < x1 + r && yy > y2 - r) {
                    // Bottom-left corner
                    int dx = (x1 + r) - xx;
                    int dy = yy - (y2 - r);
                    if (dx*dx + dy*dy > r*r) should_draw = false;
                } else if (xx > x2 - r && yy > y2 - r) {
                    // Bottom-right corner
                    int dx = xx - (x2 - r);
                    int dy = yy - (y2 - r);
                    if (dx*dx + dy*dy > r*r) should_draw = false;
                }
                // Non-corner areas are always drawn

                if (should_draw) {
                    int byte_idx = xx / 8;
                    int bit_idx = 7 - (xx % 8);
                    if (on) row[byte_idx] |= (1 << bit_idx);
                    else row[byte_idx] &= ~(1 << bit_idx);
                }
            }
        }
        // Dump a small region of the affected row for debugging to confirm bits were set
        {
            int sample_row = y1;
            if (sample_row < 0) sample_row = 0;
            if (sample_row >= h_) sample_row = h_ - 1;
            uint8_t* srow = bitbuf_ + sample_row * stride_;
            int byte_start = x1 / 8;
            int max_print = std::min(8, stride_ - byte_start);
            if (max_print > 0 && parent_ && parent_->verbose_logging()) {
                char dbg[128];
                int p = 0;
                p += snprintf(dbg + p, sizeof(dbg) - p, "AdafruitShim: rowdump y=%d:", sample_row);
                for (int i = 0; i < max_print; ++i) p += snprintf(dbg + p, sizeof(dbg) - p, " %02X", srow[byte_start + i]);
                ESP_LOGI(tag_, "%s", dbg);
            }
        }
    }
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) {
        int minx = std::min({x0, x1, x2});
        int maxx = std::max({x0, x1, x2});
        int miny = std::min({y0, y1, y2});
        int maxy = std::max({y0, y1, y2});
        if (minx < 0) minx = 0;
        if (miny < 0) miny = 0;
        if (maxx >= w_) maxx = w_ - 1;
        if (maxy >= h_) maxy = h_ - 1;
        bool on = color ? true : false;
        auto edge = [](int x0,int y0,int x1,int y1,int x,int y)->bool{
            return (x - x0) * (y1 - y0) - (y - y0) * (x1 - x0) >= 0;
        };
        if (parent_ && parent_->verbose_logging()) ESP_LOGI(tag_, "AdafruitShim: fillTriangle x0=%d y0=%d x1=%d y1=%d x2=%d y2=%d color=%d", x0,y0,x1,y1,x2,y2,color);
        if (parent_) parent_->SetDrewThisFrame(true);
        for (int yy = miny; yy <= maxy; ++yy) {
            uint8_t* row = bitbuf_ + yy * stride_;
            for (int xx = minx; xx <= maxx; ++xx) {
                bool b0 = edge(x0,y0,x1,y1,xx,yy);
                bool b1 = edge(x1,y1,x2,y2,xx,yy);
                bool b2 = edge(x2,y2,x0,y0,xx,yy);
                if ((b0 && b1 && b2) || (!b0 && !b1 && !b2)) {
                    int byte_idx = xx / 8;
                    int bit_idx = 7 - (xx % 8);
                    if (on) row[byte_idx] |= (1 << bit_idx);
                    else row[byte_idx] &= ~(1 << bit_idx);
                }
            }
        }
    }
private:
    uint8_t* bitbuf_;
    int w_;
    int h_;
    int stride_ = 0;
    RoboEyesAdapter* parent_;
    const char* tag_;
};

static const char* TAG = "RoboEyesAdapter";

// Static map to associate timers with RoboEyesAdapter instances
static std::unordered_map<lv_timer_t*, RoboEyesAdapter*> g_timer_map;

void RoboEyesAdapter::RoboEyesTimerCallback(lv_timer_t* timer) {
    auto it = g_timer_map.find(timer);
    if (it != g_timer_map.end()) {
        it->second->Update();
    }
}

RoboEyesAdapter::RoboEyesAdapter() {}

RoboEyesAdapter::~RoboEyesAdapter() {
    if (canvas_ != nullptr) {
        lv_obj_del(canvas_);
        canvas_ = nullptr;
    }
    if (canvas_buffer_ != nullptr) {
        delete[] canvas_buffer_;
        canvas_buffer_ = nullptr;
    }
    if (bit_buffer_ != nullptr) {
        delete[] bit_buffer_;
        bit_buffer_ = nullptr;
    }
    if (indexed_buffer_ != nullptr) {
        delete[] indexed_buffer_;
        indexed_buffer_ = nullptr;
    }
    // stop any panel timer
    if (panel_timer_ != nullptr) {
        esp_timer_stop(panel_timer_);
        esp_timer_delete(panel_timer_);
        panel_timer_ = nullptr;
    }
    // delete RoboEyes instance and shim if created
    if (eyes_obj_) {
        // eyes_obj_ is a RoboEyes<AdafruitShim>*
        auto eyes_ptr = static_cast<RoboEyes<AdafruitShim>*>(eyes_obj_);
        delete eyes_ptr;
        eyes_obj_ = nullptr;
    }
    if (shim_obj_) {
        auto shim_ptr = static_cast<AdafruitShim*>(shim_obj_);
        delete shim_ptr;
        shim_obj_ = nullptr;
    }
}

bool RoboEyesAdapter::Begin(lv_obj_t* parent, int width, int height, int max_fps,
                           esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel) {
    if (initialized_) return true;
    if (width <= 0 || height <= 0 || parent == nullptr) return false;

    // store optional panel handles for direct flush
    panel_io_ = panel_io;
    panel_ = panel;

    width_ = width;
    height_ = height;
    max_fps_ = max_fps > 0 ? max_fps : 15;

    // allocate internal 1-bit packed buffer (stride bytes per row)
    int stride = (width_ + 7) / 8;
    bit_buffer_ = new uint8_t[stride * height_];
    if (bit_buffer_ == nullptr) {
        ESP_LOGE(TAG, "bit buffer alloc failed");
        return false;
    }
    // clear bit buffer
    memset(bit_buffer_, 0x00, stride * height_);

    // allocate an RGB canvas buffer to blit converted pixels into for LVGL (fallback)
    canvas_buffer_ = new lv_color_t[width_ * height_];
    if (canvas_buffer_ == nullptr) {
        ESP_LOGE(TAG, "canvas buffer alloc failed");
        delete[] bit_buffer_;
        bit_buffer_ = nullptr;
        return false;
    }
    // clear canvas buffer (black)
    for (int i = 0; i < width_ * height_; ++i) canvas_buffer_[i] = lv_color_black();

    canvas_ = lv_canvas_create(parent);
    if (canvas_ == nullptr) {
        ESP_LOGE(TAG, "lv_canvas_create failed");
        delete[] canvas_buffer_;
        canvas_buffer_ = nullptr;
        delete[] bit_buffer_;
        bit_buffer_ = nullptr;
        return false;
    }
    lv_obj_set_size(canvas_, width_, height_);
    // Indexed 1-bit canvas is optional; disable by default to avoid LVGL compatibility issues.
    use_indexed_canvas_ = false;
    // Fallback: RGB canvas
    if (!use_indexed_canvas_) {
        lv_canvas_set_buffer(canvas_, canvas_buffer_, width_, height_, LV_COLOR_FORMAT_RGB565);
        lv_canvas_fill_bg(canvas_, lv_color_black(), LV_OPA_COVER);
    }

    initialized_ = true;
    if (verbose_logging_) ESP_LOGI(TAG, "RoboEyesAdapter initialized %dx%d fps=%d", width_, height_, max_fps_);
    // Initialize RoboEyes with Adafruit-like shim
    // instantiate RoboEyes using the shim (store opaque pointers in header fields)
    AdafruitShim* shim = new AdafruitShim(bit_buffer_, width_, height_, this, TAG);
    shim_obj_ = static_cast<void*>(shim);
    // Set global colors expected by RoboEyes header
    BGCOLOR = 0;
    MAINCOLOR = 1;
    // create RoboEyes instance (template) and store opaque pointer
    auto eyes_ptr = new RoboEyes<AdafruitShim>(*shim);
    eyes_obj_ = static_cast<void*>(eyes_ptr);
    // RoboEyes uses millis()/internal timing; set framerate
    eyes_ptr->setFramerate(max_fps_);
    // Enable idle/autoblink modes so RoboEyes animates continually
    eyes_ptr->setIdleMode(true, 1, 3);     // idle repositioning every ~1s ± variation
    eyes_ptr->setAutoblinker(true, 3, 4);  // autoblink every ~3s ± variation

    // Start LVGL timer for periodic updates
    if (!StartTimer(max_fps_)) {
        ESP_LOGE(TAG, "Failed to start LVGL timer for RoboEyes updates");
        delete eyes_ptr;
        eyes_obj_ = nullptr;
        delete shim;
        shim_obj_ = nullptr;
        return false;
    }

    if (verbose_logging_) ESP_LOGI(TAG, "RoboEyes initialized with timer at %d fps", max_fps_);
    return true;
}

void RoboEyesAdapter::SetMood(const char* mood) {
    mood_ = mood ? mood : "neutral";
}

void RoboEyesAdapter::SetEmotion(const char* emotion) {
    if (!initialized_ || eyes_obj_ == nullptr) return;

    if (emotion == nullptr) emotion = "neutral";

    auto eyes_ptr = static_cast<RoboEyes<AdafruitShim>*>(eyes_obj_);

    // Reset all special modes first
    eyes_ptr->setIdleMode(false);
    eyes_ptr->setCuriosity(false);
    eyes_ptr->setSweat(false);

    // Exact emoji and emotion mapping based on user's list
    // Handle English verb forms that might come from LLM emotion field
    if (strcmp(emotion, "smile") == 0) {
        // smile -> happy
        eyes_ptr->setMood(HAPPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to HAPPY (smile)");
    } else if (strcmp(emotion, "laugh") == 0) {
        // laugh -> laughing
        eyes_ptr->anim_laugh();
        eyes_ptr->setMood(HAPPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to LAUGHING (laugh)");
    } else if (strcmp(emotion, "cry") == 0) {
        // cry -> crying
        eyes_ptr->setMood(TIRED);
        eyes_ptr->setSweat(true);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to CRYING (cry)");
    } else if (strcmp(emotion, "wink") == 0) {
        // wink -> winking
        eyes_ptr->anim_laugh();
        eyes_ptr->setMood(HAPPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to WINKING (wink)");
    } else if (strcmp(emotion, "😶") == 0 || strcmp(emotion, "neutral") == 0) {
        // 1. 😶 - neutral
        eyes_ptr->setMood(DEFAULT);
        eyes_ptr->setIdleMode(true, 2, 4);  // Moderate idle movement
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to NEUTRAL (😶)");
    } else if (strcmp(emotion, "🙂") == 0 || strcmp(emotion, "happy") == 0) {
        // 2. 🙂 - happy
        eyes_ptr->setMood(HAPPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to HAPPY (🙂)");
    } else if (strcmp(emotion, "😆") == 0 || strcmp(emotion, "laughing") == 0) {
        // 3. 😆 - laughing
        eyes_ptr->anim_laugh();
        eyes_ptr->setMood(HAPPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to LAUGHING (😆)");
    } else if (strcmp(emotion, "😂") == 0 || strcmp(emotion, "funny") == 0) {
        // 4. 😂 - funny
        eyes_ptr->anim_laugh();
        eyes_ptr->setMood(HAPPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to FUNNY (😂)");
    } else if (strcmp(emotion, "😔") == 0 || strcmp(emotion, "sad") == 0) {
        // 5. 😔 - sad
        eyes_ptr->setMood(TIRED);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to SAD (😔)");
    } else if (strcmp(emotion, "😠") == 0 || strcmp(emotion, "angry") == 0) {
        // 6. 😠 - angry
        eyes_ptr->setMood(ANGRY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to ANGRY (😠)");
    } else if (strcmp(emotion, "😭") == 0 || strcmp(emotion, "crying") == 0) {
        // 7. 😭 - crying
        eyes_ptr->setMood(TIRED);
        eyes_ptr->setSweat(true);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to CRYING (😭)");
    } else if (strcmp(emotion, "😍") == 0 || strcmp(emotion, "loving") == 0) {
        // 8. 😍 - loving
        eyes_ptr->setMood(LOVING);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to LOVING (😍)");
    } else if (strcmp(emotion, "😳") == 0 || strcmp(emotion, "embarrassed") == 0) {
        // 9. 😳 - embarrassed
        eyes_ptr->setIdleMode(true, 2, 2);  // Frequent idle movement for embarrassment
        eyes_ptr->setMood(DEFAULT);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to EMBARRASSED (😳)");
    } else if (strcmp(emotion, "😲") == 0 || strcmp(emotion, "surprised") == 0) {
        // 10. 😲 - surprised
        eyes_ptr->setMood(SURPRISED);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to SURPRISED (😲)");
    } else if (strcmp(emotion, "😱") == 0 || strcmp(emotion, "shocked") == 0) {
        // 11. 😱 - shocked
        eyes_ptr->anim_confused();
        eyes_ptr->setMood(DEFAULT);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to SHOCKED (😱)");
    } else if (strcmp(emotion, "🤔") == 0 || strcmp(emotion, "thinking") == 0) {
        // 12. 🤔 - thinking
        eyes_ptr->setIdleMode(true, 2, 2);  // Looking around while thinking
        eyes_ptr->setMood(DEFAULT);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to THINKING (🤔)");
    } else if (strcmp(emotion, "😉") == 0 || strcmp(emotion, "winking") == 0) {
        // 13. 😉 - winking
        eyes_ptr->anim_laugh();
        eyes_ptr->setMood(HAPPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to WINKING (😉)");
    } else if (strcmp(emotion, "😎") == 0 || strcmp(emotion, "cool") == 0) {
        // 14. 😎 - cool
        eyes_ptr->setMood(HAPPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to COOL (😎)");
    } else if (strcmp(emotion, "😌") == 0 || strcmp(emotion, "relaxed") == 0) {
        // 15. 😌 - relaxed
        eyes_ptr->setMood(HAPPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to RELAXED (😌)");
    } else if (strcmp(emotion, "🤤") == 0 || strcmp(emotion, "delicious") == 0) {
        // 16. 🤤 - delicious
        eyes_ptr->setMood(HAPPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to DELICIOUS (🤤)");
    } else if (strcmp(emotion, "😘") == 0 || strcmp(emotion, "kissy") == 0) {
        // 17. 😘 - kissy
        eyes_ptr->setMood(LOVING);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to KISSY (😘)");
    } else if (strcmp(emotion, "😏") == 0 || strcmp(emotion, "confident") == 0) {
        // 18. 😏 - confident
        eyes_ptr->setMood(HAPPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to CONFIDENT (😏)");
    } else if (strcmp(emotion, "😴") == 0 || strcmp(emotion, "sleepy") == 0) {
        // 19. 😴 - sleepy
        eyes_ptr->setMood(SLEEPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to SLEEPY (😴)");
    } else if (strcmp(emotion, "😜") == 0 || strcmp(emotion, "silly") == 0) {
        // 20. 😜 - silly
        eyes_ptr->anim_laugh();
        eyes_ptr->setMood(HAPPY);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to SILLY (😜)");
    } else if (strcmp(emotion, "🙄") == 0 || strcmp(emotion, "confused") == 0) {
        // 21. 🙄 - confused
        eyes_ptr->setIdleMode(true, 2, 2);  // Looking around in confusion
        eyes_ptr->setMood(DEFAULT);
        if (verbose_logging_) ESP_LOGI(TAG, "Set eyes to CONFUSED (🙄)");
    } else {
        // Default to neutral for unknown emotions
        eyes_ptr->setMood(DEFAULT);
        eyes_ptr->setIdleMode(true, 2, 4);  // Moderate idle movement
        if (verbose_logging_) ESP_LOGI(TAG, "Unknown emotion '%s', set to DEFAULT", emotion);
    }
}


void RoboEyesAdapter::DrawFrame() {
    if (!initialized_ || bit_buffer_ == nullptr || canvas_buffer_ == nullptr) return;

    // Reset drawing flag for this frame
    drew_this_frame_ = false;

    // If vendored RoboEyes is available, call its update() to render into the bit buffer
    if (eyes_obj_) {
        if (verbose_logging_) ESP_LOGI(TAG, "Drawing with RoboEyes");
        auto eyes_ptr = static_cast<RoboEyes<AdafruitShim>*>(eyes_obj_);
        eyes_ptr->update();
        // convert 1-bit buffer -> canvas buffer (either indexed 1-bit or RGB565)
        int stride = (width_ + 7) / 8;
        if (use_indexed_canvas_ && indexed_buffer_ != nullptr) {
            // palette occupies first 4 bytes (u16 x 2), copy bit data directly after palette
            int pal_bytes = sizeof(uint16_t) * 2;
            memcpy(indexed_buffer_ + pal_bytes, bit_buffer_, stride * height_);
        } else {
            for (int y = 0; y < height_; ++y) {
                uint8_t* row = bit_buffer_ + y * stride;
                for (int x = 0; x < width_; ++x) {
                    int byte_idx = x / 8;
                    int bit_idx = 7 - (x % 8);
                    bool on = (row[byte_idx] >> bit_idx) & 0x1;
                    canvas_buffer_[y * width_ + x] = on ? lv_color_white() : lv_color_black();
                }
            }
        }
        // quick sanity: count white pixels (handle indexed vs RGB canvas)
        int white_count = 0;
        if (use_indexed_canvas_ && indexed_buffer_ != nullptr) {
            int stride_local = (width_ + 7) / 8;
            for (int y = 0; y < height_; ++y) {
                uint8_t* row = indexed_buffer_ + sizeof(uint16_t)*2 + y * stride_local;
                for (int b = 0; b < stride_local; ++b) {
                    uint8_t v = row[b];
                    // count bits set
                    white_count += __builtin_popcount((unsigned)v);
                }
            }
        } else {
            for (int i = 0; i < width_ * height_; ++i) {
                if (lv_color_to_u16(canvas_buffer_[i]) != 0x0000) ++white_count;
            }
        }
        if (verbose_logging_) ESP_LOGI(TAG, "Converted canvas white pixels: %d", white_count);
        // If panel handle provided, flush raw 1-bit buffer directly to panel and skip LVGL canvas
        if (panel_ != nullptr) {
            if (verbose_logging_) ESP_LOGI(TAG, "Flushing frame directly to panel");
            FlushToPanel();
            return;
        }
        // Periodic debug dump (once per second-ish) to help diagnose bit ordering issues
        frame_counter_++;
        if ((frame_counter_ % (max_fps_ > 0 ? max_fps_ : 30)) == 0) {
            // dump first 16 bytes of bit buffer and first 16 pixels of canvas buffer
            int dump_bytes = (stride * height_) >= 16 ? 16 : (stride * height_);
            char buf[256];
            int pos = 0;
            if (verbose_logging_) {
                pos += snprintf(buf + pos, sizeof(buf) - pos, "bitbuf:");
                for (int i = 0; i < dump_bytes; ++i) pos += snprintf(buf + pos, sizeof(buf) - pos, " %02X", bit_buffer_[i]);
                ESP_LOGI(TAG, "%s", buf);
            }
            pos = 0;
            if (use_indexed_canvas_ && indexed_buffer_ != nullptr) {
                int pal_bytes = sizeof(uint16_t) * 2;
                int dump_bytes2 = dump_bytes;
                if (verbose_logging_) {
                    pos += snprintf(buf + pos, sizeof(buf) - pos, "indexed:");
                    for (int i = 0; i < dump_bytes2; ++i) pos += snprintf(buf + pos, sizeof(buf) - pos, " %02X", indexed_buffer_[pal_bytes + i]);
                    ESP_LOGI(TAG, "%s", buf);
                }
            } else {
                if (verbose_logging_) {
                    pos += snprintf(buf + pos, sizeof(buf) - pos, "canvas:");
                    int dump_pixels = (width_ * height_) >= 16 ? 16 : (width_ * height_);
                    for (int i = 0; i < dump_pixels; ++i) {
                        uint16_t v = lv_color_to_u16(canvas_buffer_[i]);
                        pos += snprintf(buf + pos, sizeof(buf) - pos, " %04X", v);
                    }
                    ESP_LOGI(TAG, "%s", buf);
                }
            }
        }
        lv_obj_invalidate(canvas_);

        // If RoboEyes didn't call any drawing functions this frame, draw a test pattern
        if (!drew_this_frame_) {
            ESP_LOGW(TAG, "RoboEyes didn't draw anything this frame, drawing test pattern");
            DrawTestPattern();
        }

        return;
    }
    if (verbose_logging_) ESP_LOGI(TAG, "Drawing demo eyes (RoboEyes not available)");
    // fallback simple demo rendering
    // clear
    // clear bit buffer and canvas buffer
    int stride = (width_ + 7) / 8;
    memset(bit_buffer_, 0x00, stride * height_);
    for (int i = 0; i < width_ * height_; ++i) canvas_buffer_[i] = lv_color_black();

    int eye_w = width_ / 4;
    int eye_h = height_ / 2;
    int spacing = width_ / 8;
    int left_x = width_ / 2 - spacing/2 - eye_w;
    int right_x = width_ / 2 + spacing/2;
    int y = (height_ - eye_h) / 2;

    // blink simulation: every 60 frames blink for a few frames
    int blink_period = 60;
    int blink_frame = frame_counter_ % blink_period;
    bool closing = (blink_frame >= 58);
    int eye_h_draw = eye_h;
    if (closing) {
        eye_h_draw = eye_h / 10; // nearly closed
        y = (height_ - eye_h_draw) / 2;
    }

    // left and right eyes -> draw into bit buffer using helper that sets bits
    // we'll use fill_rect_bits which writes bits into bit_buffer_
    auto fill_rect_bits = [&](int bx1,int by1,int bx2,int by2,bool on){
        if (bx1 < 0) bx1 = 0;
        if (by1 < 0) by1 = 0;
        if (bx2 >= width_) bx2 = width_ - 1;
        if (by2 >= height_) by2 = height_ - 1;
        int stride_local = (width_ + 7) / 8;
        for (int yy = by1; yy <= by2; ++yy) {
            uint8_t* row = bit_buffer_ + yy * stride_local;
            for (int xx = bx1; xx <= bx2; ++xx) {
                int byte_idx = xx / 8;
                int bit_idx = 7 - (xx % 8);
                if (on) row[byte_idx] |= (1 << bit_idx);
                else row[byte_idx] &= ~(1 << bit_idx);
            }
        }
    };
    fill_rect_bits(left_x, y, left_x + eye_w - 1, y + eye_h_draw - 1, true);
    fill_rect_bits(right_x, y, right_x + eye_w - 1, y + eye_h_draw - 1, true);

    // pupils (small black rect)
    int psize = (eye_w/6) > 1 ? (eye_w/6) : 1;
    int lp_x = left_x + eye_w/2 - psize/2;
    int rp_x = right_x + eye_w/2 - psize/2;
    int p_y = y + eye_h_draw/2 - psize/2;
    fill_rect_bits(lp_x, p_y, lp_x + psize -1, p_y + psize -1, false);
    fill_rect_bits(rp_x, p_y, rp_x + psize -1, p_y + psize -1, false);

    frame_counter_++;
}

void RoboEyesAdapter::DrawTestPattern() {
    if (!bit_buffer_ || !canvas_buffer_) return;

    int stride = (width_ + 7) / 8;

    // Clear bit buffer
    memset(bit_buffer_, 0, stride * height_);

    // Draw a simple test pattern: alternating checkerboard
    for (int y = 0; y < height_; ++y) {
        uint8_t* row = bit_buffer_ + y * stride;
        for (int x = 0; x < width_; ++x) {
            bool on = ((x / 8) + (y / 8)) % 2 == 0;
            int byte_idx = x / 8;
            int bit_idx = 7 - (x % 8);
            if (on) row[byte_idx] |= (1 << bit_idx);
            else row[byte_idx] &= ~(1 << bit_idx);
        }
    }

    // Convert to canvas buffer
    for (int y = 0; y < height_; ++y) {
        uint8_t* row = bit_buffer_ + y * stride;
        for (int x = 0; x < width_; ++x) {
            int byte_idx = x / 8;
            int bit_idx = 7 - (x % 8);
            bool on = (row[byte_idx] >> bit_idx) & 0x1;
            canvas_buffer_[y * width_ + x] = on ? lv_color_white() : lv_color_black();
        }
    }

    if (verbose_logging_) ESP_LOGI(TAG, "Drew test checkerboard pattern");
}

void RoboEyesAdapter::Update() {
    if (!initialized_) return;
    DrawFrame();
    // After DrawFrame we ensure canvas buffer is up-to-date (demo path already updated canvas_buffer_)
    // For the RoboEyes path, DrawFrame handles conversion and invalidation.
}

bool RoboEyesAdapter::StartTimer(int fps) {
    if (update_timer_ != nullptr) {
        ESP_LOGW(TAG, "Timer already running");
        return true;
    }

    int interval_ms = 1000 / fps;
    if (interval_ms < 1) interval_ms = 1;

    // If we were given a panel handle (direct writes), use esp_timer instead
    if (panel_ != nullptr) {
        if (panel_task_ != nullptr) {
            ESP_LOGW(TAG, "Panel task already running");
            return true;
        }
        panel_task_running_ = true;
        // create a dedicated FreeRTOS task to drive updates in a safe context
        BaseType_t rc = xTaskCreatePinnedToCore(
            [](void* arg) {
                RoboEyesAdapter* self = static_cast<RoboEyesAdapter*>(arg);
                int delay_ms = 1000 / (self->max_fps_ > 0 ? self->max_fps_ : 15);
                if (delay_ms < 1) delay_ms = 1;
                while (self->panel_task_running_) {
                    self->Update();
                    vTaskDelay(pdMS_TO_TICKS(delay_ms));
                }
                vTaskDelete(nullptr);
            },
            "roboeyes_panel_task",
            4096,
            this,
            tskIDLE_PRIORITY + 1,
            &panel_task_,
#if CONFIG_SOC_CPU_CORES_NUM > 1
            0
#else
            tskNO_AFFINITY
#endif
        );
        if (rc != pdPASS) {
            ESP_LOGE(TAG, "Failed to create panel task: %d", rc);
            panel_task_ = nullptr;
            panel_task_running_ = false;
            return false;
        }
        if (verbose_logging_) ESP_LOGI(TAG, "Started panel task for updates at ~%dms interval", interval_ms);
        return true;
    } else {
        update_timer_ = lv_timer_create(RoboEyesTimerCallback, interval_ms, this);
        if (update_timer_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create LVGL timer");
            return false;
        }
        g_timer_map[update_timer_] = this;
        if (verbose_logging_) ESP_LOGI(TAG, "Started LVGL timer with %dms interval", interval_ms);
        return true;
    }
}

void RoboEyesAdapter::StopTimer() {
    if (update_timer_ != nullptr) {
        lv_timer_del(update_timer_);
        g_timer_map.erase(update_timer_);
        update_timer_ = nullptr;
        ESP_LOGI(TAG, "Stopped LVGL timer");
    }
    if (panel_timer_ != nullptr) {
        esp_timer_stop(panel_timer_);
        esp_timer_delete(panel_timer_);
        panel_timer_ = nullptr;
        ESP_LOGI(TAG, "Stopped panel esp_timer");
    }
    if (panel_task_ != nullptr) {
        panel_task_running_ = false;
        // wait a short moment for task to exit
        vTaskDelay(pdMS_TO_TICKS(20));
        panel_task_ = nullptr;
        ESP_LOGI(TAG, "Stopped panel task");
    }
}

void RoboEyesAdapter::FlushToPanel() {
    if (!panel_ || !bit_buffer_) return;
    int stride = (width_ + 7) / 8;
    int pages = (height_ + 7) / 8;
    size_t len = static_cast<size_t>(pages) * static_cast<size_t>(width_);
    std::vector<uint8_t> raw;
    raw.resize(len);

    // For each page (vertical 8-pixel band), for each column, pack bits LSB = top row in page
    for (int p = 0; p < pages; ++p) {
        for (int x = 0; x < width_; ++x) {
            uint8_t b = 0;
            for (int k = 0; k < 8; ++k) {
                int y = p * 8 + k;
                if (y >= height_) continue;
                int byte_idx = (y * stride) + (x / 8);
                int bit_idx = 7 - (x % 8); // match how AdafruitShim sets bits
                bool on = (bit_buffer_[byte_idx] >> bit_idx) & 0x1;
                if (on) b |= (1 << k);
            }
            raw[p * width_ + x] = b;
        }
    }

    // Write per-page to avoid driver/panel issues when sending a full frame at once.
    for (int p = 0; p < pages; ++p) {
        const uint8_t* page_ptr = raw.data() + (p * width_);
        int y_start = p * 8;
        int y_end = y_start + 8;
        if (y_end > height_) y_end = height_;
        esp_err_t err = esp_lcd_panel_draw_bitmap(panel_, 0, y_start, width_, y_end, page_ptr);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "FlushToPanel draw_bitmap page %d failed: %d", p, err);
        } else {
            ESP_LOGD(TAG, "FlushToPanel wrote page %d (%d-%d)", p, y_start, y_end - 1);
        }
    }
}
