
/**
 * @file config.h
 * @brief 郑辰1.54TFT WiFi板子的硬件配置头文件
 * @details 定义了ESP32-S3引脚分配、音频参数和显示屏配置
 */

#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

/* ==================== 音频配置 ==================== */
/** 音频输入采样率 (16kHz，语音识别优化) */
#define AUDIO_INPUT_SAMPLE_RATE  16000
/** 音频输出采样率 (24kHz，语音合成优化) */
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

/* 麦克风I2S引脚配置 */
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4    /**< 麦克风WS (Word Select) 引脚 */
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5    /**< 麦克风SCK (Serial Clock) 引脚 */
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6    /**< 麦克风数据输入引脚 */

/* 扬声器I2S引脚配置 */
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7    /**< 扬声器数据输出引脚 */
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15   /**< 扬声器BCLK (Bit Clock) 引脚 */
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16   /**< 扬声器LRCK (Left Right Clock) 引脚 */

/* ==================== 按键配置 ==================== */
/** 开机/聊天切换按键引脚 */
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
/** 音量增加按键引脚 */
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_10
/** 音量减少按键引脚 */
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

/* ==================== 显示屏SPI配置 ==================== */
/** SPI MOSI引脚 (显示数据) */
#define DISPLAY_SDA GPIO_NUM_41
/** SPI CLK引脚 (时钟信号) */
#define DISPLAY_SCL GPIO_NUM_42
/** 显示屏复位引脚 */
#define DISPLAY_RES GPIO_NUM_45
/** 显示屏数据/命令选择引脚 */
#define DISPLAY_DC GPIO_NUM_40
/** 显示屏片选引脚 */
#define DISPLAY_CS GPIO_NUM_21

/* ==================== 显示屏参数配置 ==================== */
/** 显示屏宽度 (像素) */
#define DISPLAY_WIDTH   240
/** 显示屏高度 (像素) */
#define DISPLAY_HEIGHT  240
/** 是否交换X/Y坐标 */
#define DISPLAY_SWAP_XY  false
/** 是否水平镜像 */
#define DISPLAY_MIRROR_X false
/** 是否垂直镜像 */
#define DISPLAY_MIRROR_Y false
/** 背光输出是否反转 */
#define BACKLIGHT_INVERT false
/** X轴偏移量 */
#define DISPLAY_OFFSET_X  0
/** Y轴偏移量 */
#define DISPLAY_OFFSET_Y  0

/* ==================== 背光控制配置 ==================== */
/** 背光控制引脚 */
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_20
/** 背光输出是否反转 */
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#endif // _BOARD_CONFIG_H_
