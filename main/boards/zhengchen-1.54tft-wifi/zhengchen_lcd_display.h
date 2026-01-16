/**
 * @file zhengchen_lcd_display.h
 * @brief 郑辰LCD显示类头文件
 * @details 扩展基础LCD显示类，添加高温警告弹窗功能
 */

#ifndef ZHENGCHEN_LCD_DISPLAY_H
#define ZHENGCHEN_LCD_DISPLAY_H

#include "display/lcd_display.h"
#include "lvgl_theme.h"
#include <esp_lvgl_port.h>

/**
 * @class ZHENGCHEN_LcdDisplay
 * @brief 郑辰LCD显示类
 * @details 继承自SpiLcdDisplay，添加高温警告弹窗功能
 */
class ZHENGCHEN_LcdDisplay : public SpiLcdDisplay {
protected:
    /** 高温警告弹窗对象 */
    lv_obj_t* high_temp_popup_ = nullptr;
    /** 高温警告标签对象 */
    lv_obj_t* high_temp_label_ = nullptr;

public:
    /** 继承基础类的构造函数 */
    using SpiLcdDisplay::SpiLcdDisplay;

    /**
     * @brief 设置高温警告弹窗
     * @details 创建红色警告弹窗和标签，初始状态为隐藏
     */
    void SetupHighTempWarningPopup() {
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        auto text_font = lvgl_theme->text_font()->font();
        // 创建高温警告弹窗
        high_temp_popup_ = lv_obj_create(lv_screen_active());  // 使用当前屏幕
        lv_obj_set_scrollbar_mode(high_temp_popup_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_size(high_temp_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
        lv_obj_align(high_temp_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(high_temp_popup_, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_radius(high_temp_popup_, 10, 0);
        
        // 创建警告标签
        high_temp_label_ = lv_label_create(high_temp_popup_);
        lv_label_set_text(high_temp_label_, "警告：温度过高");
        lv_obj_set_style_text_color(high_temp_label_, lv_color_white(), 0);
        lv_obj_center(high_temp_label_);
        
        // 默认隐藏
        lv_obj_add_flag(high_temp_popup_, LV_OBJ_FLAG_HIDDEN);
    }

    /**
     * @brief 更新高温警告状态
     * @param chip_temp 当前芯片温度 (°C)
     * @param threshold 温度阈值 (°C)，默认为75°C
     */
    void UpdateHighTempWarning(float chip_temp, float threshold = 75.0f) {
        if (high_temp_popup_ == nullptr) {
            ESP_LOGW("ZHENGCHEN_LcdDisplay", "High temp popup not initialized!");
            return;
        }

        if (chip_temp >= threshold) {
            ShowHighTempWarning();
        } else {
            HideHighTempWarning();
        }
    }

    /**
     * @brief 显示高温警告弹窗
     */
    void ShowHighTempWarning() {
        if (high_temp_popup_ && lv_obj_has_flag(high_temp_popup_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_remove_flag(high_temp_popup_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /**
     * @brief 隐藏高温警告弹窗
     */
    void HideHighTempWarning() {
        if (high_temp_popup_ && !lv_obj_has_flag(high_temp_popup_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(high_temp_popup_, LV_OBJ_FLAG_HIDDEN);
        }
    }
};



#endif // ZHENGCHEN_LCD_DISPLAY_H