/**
 * @file config.h
 * @brief Bread Compact WiFi LCD板子的硬件配置头文件
 * @details 支持多种LCD屏幕配置的通用紧凑型WiFi开发板
 */

#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

/* ==================== 音频配置 ==================== */
/** 音频输入采样率 (16kHz，语音识别优化) */
#define AUDIO_INPUT_SAMPLE_RATE  16000
/** 音频输出采样率 (24kHz，语音合成优化) */
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

/* 音频I2S模式选择：取消注释使用单工模式，注释掉使用双工模式 */
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX
/* 单工模式：麦克风和扬声器使用独立的I2S接口 */

#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4    /**< 麦克风WS (Word Select) 引脚 */
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5    /**< 麦克风SCK (Serial Clock) 引脚 */
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6    /**< 麦克风数据输入引脚 */
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7    /**< 扬声器数据输出引脚 */
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15   /**< 扬声器BCLK (Bit Clock) 引脚 */
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16   /**< 扬声器LRCK (Left Right Clock) 引脚 */

#else
/* 双工模式：麦克风和扬声器共享同一组I2S引脚 */

#define AUDIO_I2S_GPIO_WS GPIO_NUM_4    /**< I2S WS引脚 (Word Select) */
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5  /**< I2S BCLK引脚 (Bit Clock) */
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6  /**< I2S数据输入引脚 */
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7  /**< I2S数据输出引脚 */

#endif

/* ==================== 按键和LED配置 ==================== */
/** 板载LED引脚 */
#define BUILTIN_LED_GPIO        GPIO_NUM_48
/** 开机/聊天切换按键引脚 */
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
/** 触摸按键引脚 (未连接) */
#define TOUCH_BUTTON_GPIO       GPIO_NUM_NC
/** 音量增加按键引脚 (未连接) */
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
/** 音量减少按键引脚 (未连接) */
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC


/* ==================== 显示屏SPI配置 ==================== */
/** 背光控制引脚 */
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_42
/** SPI MOSI引脚 (显示数据) */
#define DISPLAY_MOSI_PIN      GPIO_NUM_47
/** SPI CLK引脚 (时钟信号) */
#define DISPLAY_CLK_PIN       GPIO_NUM_21
/** 显示屏数据/命令选择引脚 */
#define DISPLAY_DC_PIN        GPIO_NUM_40
/** 显示屏复位引脚 */
#define DISPLAY_RST_PIN       GPIO_NUM_45
/** 显示屏片选引脚 */
#define DISPLAY_CS_PIN        GPIO_NUM_41

/* ==================== 显示屏型号配置 ==================== */
/* ST7789 240x320 IPS屏幕配置 */
#ifdef CONFIG_LCD_ST7789_240X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240          /**< 屏幕宽度 (像素) */
#define DISPLAY_HEIGHT  320          /**< 屏幕高度 (像素) */
#define DISPLAY_MIRROR_X false       /**< 水平镜像 */
#define DISPLAY_MIRROR_Y false       /**< 垂直镜像 */
#define DISPLAY_SWAP_XY false        /**< 交换X/Y坐标 */
#define DISPLAY_INVERT_COLOR    true /**< 反转颜色 */
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB  /**< RGB顺序 */
#define DISPLAY_OFFSET_X  0          /**< X轴偏移 */
#define DISPLAY_OFFSET_Y  0          /**< Y轴偏移 */
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false  /**< 背光输出反转 */
#define DISPLAY_SPI_MODE 0           /**< SPI模式 */
#endif

/* ST7789 240x320 非IPS屏幕配置 */
#ifdef CONFIG_LCD_ST7789_240X320_NO_IPS
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* ST7789 170x320 屏幕配置 (带偏移补偿) */
#ifdef CONFIG_LCD_ST7789_170X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   170
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  35
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* ST7789 172x320 屏幕配置 (带偏移补偿) */
#ifdef CONFIG_LCD_ST7789_172X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   172
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  34
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* ST7789 240x280 屏幕配置 (带Y轴偏移) */
#ifdef CONFIG_LCD_ST7789_240X280
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  280
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  20
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* ST7789 240x240 圆形屏幕配置 */
#ifdef CONFIG_LCD_ST7789_240X240
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* ST7789 240x240 7PIN接口圆形屏幕配置 (SPI模式3) */
#ifdef CONFIG_LCD_ST7789_240X240_7PIN
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 3
#endif

/* ST7789 240x135 条形屏幕配置 (旋转90度) */
#ifdef CONFIG_LCD_ST7789_240X135
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  135
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY true
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  40
#define DISPLAY_OFFSET_Y  53
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* ST7735 128x160 小尺寸屏幕配置 */
#ifdef CONFIG_LCD_ST7735_128X160
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  160
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* ST7735 128x128 正方形屏幕配置 (BGR顺序) */
#ifdef CONFIG_LCD_ST7735_128X128
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  128
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR  false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  32
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* ST7796 320x480 高分辨率IPS屏幕配置 (BGR顺序) */
#ifdef CONFIG_LCD_ST7796_320X480
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7796_320X480_NO_IPS
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* ILI9341 240x320 IPS屏幕配置 (BGR顺序) */
#ifdef CONFIG_LCD_ILI9341_240X320
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* ILI9341 240x320 非IPS屏幕配置 (BGR顺序) */
#ifdef CONFIG_LCD_ILI9341_240X320_NO_IPS
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* GC9A01 240x240 圆形屏幕配置 (BGR顺序) */
#ifdef CONFIG_LCD_GC9A01_240X240
#define LCD_TYPE_GC9A01_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* 自定义LCD配置 (默认参数) */
#ifdef CONFIG_LCD_CUSTOM
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

/* ==================== MCP物联网测试配置 ==================== */
/** 灯控制GPIO引脚 (用于MCP物联网功能测试) */
#define LAMP_GPIO GPIO_NUM_18

#endif // _BOARD_CONFIG_H_
