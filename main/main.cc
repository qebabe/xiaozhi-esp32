#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "system_info.h"

// Motor control pins (defined in config.h, but we need them early)
// Try to include board `config.h` if available so board-level overrides take effect.
#if defined(__has_include)
#  if __has_include("config.h")
#    include "config.h"
#  endif
#endif

// Provide guarded defaults if the board did not define MOTOR_* pins
#ifndef MOTOR_LF_GPIO
#define MOTOR_LF_GPIO GPIO_NUM_12   // Left Forward
#define MOTOR_LB_GPIO GPIO_NUM_13  // Left Backward
#define MOTOR_RF_GPIO GPIO_NUM_14  // Right Forward
#define MOTOR_RB_GPIO GPIO_NUM_21  // Right Backward (was GPIO_NUM_3)
#endif

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize motor control GPIOs to LOW as early as possible to prevent motors from running on boot
    // ESP32 GPIO_NUM_3 has special strapping behavior, so we need to be extra careful
    gpio_config_t motor_io_conf = {};
    motor_io_conf.intr_type = GPIO_INTR_DISABLE;
    motor_io_conf.mode = GPIO_MODE_OUTPUT;
    motor_io_conf.pin_bit_mask = (1ULL << MOTOR_LF_GPIO) | (1ULL << MOTOR_LB_GPIO) | (1ULL << MOTOR_RF_GPIO) | (1ULL << MOTOR_RB_GPIO);
    motor_io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    motor_io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&motor_io_conf);

    // Force all motor pins to LOW by setting them multiple times
    gpio_set_level(MOTOR_LF_GPIO, 0);
    gpio_set_level(MOTOR_LB_GPIO, 0);
    gpio_set_level(MOTOR_RF_GPIO, 0);
    gpio_set_level(MOTOR_RB_GPIO, 0);

    // Double-check by reading back the levels
    int level_3 = gpio_get_level(MOTOR_RB_GPIO);
    int level_8 = gpio_get_level(MOTOR_LF_GPIO);
    int level_19 = gpio_get_level(MOTOR_LB_GPIO);
    int level_20 = gpio_get_level(MOTOR_RF_GPIO);

    ESP_LOGI(TAG, "电机GPIO初始化完成 - GPIO3:%d, GPIO8:%d, GPIO19:%d, GPIO20:%d", level_3, level_8, level_19, level_20);

    ESP_LOGI(TAG, "电机GPIO已预初始化为低电平");

    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
}
