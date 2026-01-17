#pragma once

#include <lvgl.h>
#include <cstdint>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class RoboEyesAdapter {
public:
    RoboEyesAdapter();
    ~RoboEyesAdapter();

    // Initialize with LVGL parent and target size
    bool Begin(lv_obj_t* parent, int width, int height, int max_fps = 15,
               esp_lcd_panel_io_handle_t panel_io = nullptr, esp_lcd_panel_handle_t panel = nullptr);

    // Set mood string (e.g., "happy","sad")
    void SetMood(const char* mood);

    // Update internal animation state (called in LVGL-safe context)
    void Update();

    // Return whether initialized
    bool IsInitialized() const { return initialized_; }

    // Get canvas object
    lv_obj_t* GetCanvas() const { return canvas_; }

private:
    void DrawFrame();
    static void RoboEyesTimerCallback(lv_timer_t* timer);
    bool StartTimer(int fps);
    void StopTimer();

    lv_obj_t* canvas_ = nullptr;
    // LVGL canvas buffer (RGB565) used for display update
    lv_color_t* canvas_buffer_ = nullptr;
    // Internal 1-bit buffer for RoboEyes drawing (packed bits, stride = (width+7)/8)
    uint8_t* bit_buffer_ = nullptr;
    // Optional LVGL indexed 1-bit buffer (palette + bit data) to avoid RGB conversion
    uint8_t* indexed_buffer_ = nullptr;
    bool use_indexed_canvas_ = true;
    int width_ = 0;
    int height_ = 0;
    int max_fps_ = 15;

    // simple state
    const char* mood_ = "neutral";
    bool initialized_ = false;
    uint32_t frame_counter_ = 0;
    // opaque pointers for vendored RoboEyes objects
    void* eyes_obj_ = nullptr;
    void* shim_obj_ = nullptr;

    // LVGL timer for periodic updates
    lv_timer_t* update_timer_ = nullptr;
    // When flushing directly to panel we use esp_timer to drive updates
    esp_timer_handle_t panel_timer_ = nullptr;
    // Alternative: a dedicated FreeRTOS task for panel updates (used instead of esp_timer)
    TaskHandle_t panel_task_ = nullptr;
    bool panel_task_running_ = false;

private:
    // Debug tracking for drawing operations
    bool drew_this_frame_ = false;

public:
    void SetDrewThisFrame(bool drew) { drew_this_frame_ = drew; }

    void DrawTestPattern();
    // If panel handles are provided, adapter can flush frames directly to the panel (SSD1306)
    void FlushToPanel();

    // Optional panel handles for direct writes
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    // Control verbose logging (default off)
    bool verbose_logging_ = false;
    void SetVerboseLogging(bool v) { verbose_logging_ = v; }
    bool verbose_logging() const { return verbose_logging_; }

    // Set emotion/mood for the animated eyes
    void SetEmotion(const char* emotion);
};

