/**
 * @file power_manager.h
 * @brief 郑辰1.54TFT WiFi板子的电源管理系统
 * @details 实现电池电量监测、充电状态检测、温度监控和智能省电功能
 */

#pragma once
#include <vector>
#include <functional>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <driver/temperature_sensor.h>
#include "application.h"
#include "zhengchen_lcd_display.h"

/**
 * @class PowerManager
 * @brief 电源管理类
 * @details 负责电池电量监测、充电状态检测、温度监控和省电策略管理
 */
class PowerManager {
private:
    // ============ 事件回调函数 ============
    /** 定时器句柄 */
    esp_timer_handle_t timer_handle_;
    /** 充电状态改变回调函数 */
    std::function<void(bool)> on_charging_status_changed_;
    /** 低电量状态改变回调函数 */
    std::function<void(bool)> on_low_battery_status_changed_;
    /** 温度改变回调函数 */
    std::function<void(float)> on_temperature_changed_;

    // ============ 硬件状态变量 ============
    /** 充电检测引脚 */
    gpio_num_t charging_pin_ = GPIO_NUM_NC;
    /** ADC采样值队列 */
    std::vector<uint16_t> adc_values_;
    /** 当前电池电量百分比 (0-100) */
    uint32_t battery_level_ = 0;
    /** 当前是否正在充电 */
    bool is_charging_ = false;
    /** 当前是否处于低电量状态 */
    bool is_low_battery_ = false;
    /** 当前温度值 (°C) */
    float current_temperature_ = 0.0f;
    /** 定时器tick计数器 */
    int ticks_ = 0;

    // ============ 配置常量 ============
    /** 电池ADC采样间隔 (秒) */
    const int kBatteryAdcInterval = 60;
    /** ADC数据采样数量 */
    const int kBatteryAdcDataCount = 3;
    /** 低电量阈值 (%) */
    const int kLowBatteryLevel = 20;
    /** 温度读取间隔 (秒) */
    const int kTemperatureReadInterval = 10;

    // ============ 硬件句柄 ============
    /** ADC单次采样单元句柄 */
    adc_oneshot_unit_handle_t adc_handle_;
    /** 温度传感器句柄 */
    temperature_sensor_handle_t temp_sensor_ = NULL;  

    /**
     * @brief 检查电池状态
     * @details 定期检查充电状态、读取ADC数据并计算电池电量，读取温度数据
     */
    void CheckBatteryStatus() {
        // Get charging status
        bool new_charging_status = gpio_get_level(charging_pin_) == 1;
        if (new_charging_status != is_charging_) {
            is_charging_ = new_charging_status;
            if (on_charging_status_changed_) {
                on_charging_status_changed_(is_charging_);
            }
            ReadBatteryAdcData();
            return;
        }

        // 如果电池电量数据不足，则读取电池电量数据
        if (adc_values_.size() < kBatteryAdcDataCount) {
            ReadBatteryAdcData();
            return;
        }

        // 如果电池电量数据充足，则每 kBatteryAdcInterval 个 tick 读取一次电池电量数据
        ticks_++;
        if (ticks_ % kBatteryAdcInterval == 0) {
            ReadBatteryAdcData();
        }

        // 新增：周期性读取温度
        if (ticks_ % kTemperatureReadInterval == 0) {
            ReadTemperature();
        }
    }

    /**
     * @brief 读取电池ADC数据
     * @details 读取ADC通道7的值，计算平均值并转换为电池电量百分比
     */
    void ReadBatteryAdcData() {
        // 读取 ADC 值
        int adc_value;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle_, ADC_CHANNEL_7, &adc_value));
       
        
        // 将 ADC 值添加到队列中
        adc_values_.push_back(adc_value);
        if (adc_values_.size() > kBatteryAdcDataCount) {
            adc_values_.erase(adc_values_.begin());
        }
        uint32_t average_adc = 0;
        for (auto value : adc_values_) {
            average_adc += (value + 80);
        }
        average_adc /= adc_values_.size();

       
        // 定义电池电量区间
        const struct {
            uint16_t adc;
            uint8_t level;
        } levels[] = {
            {2030, 0},
            {2134, 20},
            {2252, 40},
            {2370, 60},
            {2488, 80},
            {2606, 100}
        };
        // 低于最低值时
        if (average_adc < levels[0].adc) {
            battery_level_ = 0;
        }
        // 高于最高值时
        else if (average_adc >= levels[5].adc) {
            battery_level_ = 100;
        } else {
            // 线性插值计算中间值
            for (int i = 0; i < 5; i++) {
                if (average_adc >= levels[i].adc && average_adc < levels[i+1].adc) {
                    float ratio = static_cast<float>(average_adc - levels[i].adc) / (levels[i+1].adc - levels[i].adc);
                    battery_level_ = levels[i].level + ratio * (levels[i+1].level - levels[i].level);
                    break;
                }
            }
        }
        // 检查是否达到低电量阈值
        if (adc_values_.size() >= kBatteryAdcDataCount) {
            bool new_low_battery_status = battery_level_ <= kLowBatteryLevel;
            if (new_low_battery_status != is_low_battery_) {
                is_low_battery_ = new_low_battery_status;
                if (on_low_battery_status_changed_) {
                    on_low_battery_status_changed_(is_low_battery_);
                }
            }
        }

        ESP_LOGI("PowerManager", "ADC value: %d average: %ld level: %ld", adc_value, average_adc, battery_level_);
    }

    /**
     * @brief 读取温度数据
     * @details 从ESP32内置温度传感器读取温度值，变化超过3.5°C时触发回调
     */
    void ReadTemperature() {
        float temperature = 0.0f;
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor_, &temperature));
        
        if (abs(temperature - current_temperature_) >= 3.5f) {  // 温度变化超过3.5°C才触发回调
            current_temperature_ = temperature;
            if (on_temperature_changed_) {
                on_temperature_changed_(current_temperature_);
            }
            ESP_LOGI("PowerManager", "Temperature updated: %.1f°C", current_temperature_);
        }      
    }


public:
    /**
     * @brief 构造函数
     * @param pin 充电检测引脚
     */
    PowerManager(gpio_num_t pin) : charging_pin_(pin) {
        
        // 初始化充电引脚
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << charging_pin_);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; 
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;     
        gpio_config(&io_conf);

        // 创建电池电量检查定时器
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                PowerManager* self = static_cast<PowerManager*>(arg);
                self->CheckBatteryStatus();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "battery_check_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle_, 1000000));

        // 初始化 ADC
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle_));
        
        adc_oneshot_chan_cfg_t chan_config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, ADC_CHANNEL_7, &chan_config));

        // 初始化温度传感器
        temperature_sensor_config_t temp_config = {
            .range_min = 10,
            .range_max = 80,
            .clk_src = TEMPERATURE_SENSOR_CLK_SRC_DEFAULT
        };
        ESP_ERROR_CHECK(temperature_sensor_install(&temp_config, &temp_sensor_));
        ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor_));
        ESP_LOGI("PowerManager", "Temperature sensor initialized (new driver)");
    }

    /**
     * @brief 析构函数
     * @details 停止定时器并释放ADC和温度传感器资源
     */
    ~PowerManager() {
        if (timer_handle_) {
            esp_timer_stop(timer_handle_);
            esp_timer_delete(timer_handle_);
        }
        if (adc_handle_) {
            adc_oneshot_del_unit(adc_handle_);
        }
        
        if (temp_sensor_) {
            temperature_sensor_disable(temp_sensor_);
            temperature_sensor_uninstall(temp_sensor_);
        }
  
    }

    /**
     * @brief 获取充电状态
     * @return true表示正在充电，false表示未充电
     */
    bool IsCharging() {
        // 如果电量已经满了，则不再显示充电中
        if (battery_level_ == 100) {
            return false;
        }
        return is_charging_;
    }

    /**
     * @brief 获取放电状态
     * @return true表示正在放电，false表示未放电
     */
    bool IsDischarging() {
        // 没有区分充电和放电，所以直接返回相反状态
        return !is_charging_;
    }

    /**
     * @brief 获取电池电量
     * @return 电池电量百分比 (0-100)
     */
    uint8_t GetBatteryLevel() {
        return battery_level_;
    }

    /**
     * @brief 获取当前温度
     * @return 温度值 (°C)
     */
    float GetTemperature() const { return current_temperature_; }

    /**
     * @brief 设置温度改变回调函数
     * @param callback 温度改变时的回调函数
     */
    void OnTemperatureChanged(std::function<void(float)> callback) {
        on_temperature_changed_ = callback;
    }

    /**
     * @brief 设置低电量状态改变回调函数
     * @param callback 低电量状态改变时的回调函数
     */
    void OnLowBatteryStatusChanged(std::function<void(bool)> callback) {
        on_low_battery_status_changed_ = callback;
    }

    /**
     * @brief 设置充电状态改变回调函数
     * @param callback 充电状态改变时的回调函数
     */
    void OnChargingStatusChanged(std::function<void(bool)> callback) {
        on_charging_status_changed_ = callback;
    }
};
