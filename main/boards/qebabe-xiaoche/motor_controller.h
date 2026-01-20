#ifndef _MOTOR_CONTROLLER_H_
#define _MOTOR_CONTROLLER_H_

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_random.h>

#define MOTOR_LOG_TAG "MotorController"

// Motor control actions
#define MOTOR_STOP       0
#define MOTOR_BACKWARD   1
#define MOTOR_FORWARD    2
#define MOTOR_FULL_LEFT  3
#define MOTOR_FULL_RIGHT 4
#define MOTOR_BACK_LEFT  5
#define MOTOR_BACK_RIGHT 6
#define MOTOR_FORWARD_LEFT  7
#define MOTOR_FORWARD_RIGHT 8

class MotorController {
private:
    gpio_num_t lf_pin_;  // Left Forward
    gpio_num_t lb_pin_;  // Left Backward
    gpio_num_t rf_pin_;  // Right Forward
    gpio_num_t rb_pin_;  // Right Backward

    // PWM configuration
    bool pwm_initialized_;
    ledc_timer_config_t pwm_timer_config_;
    ledc_channel_config_t pwm_channel_configs_[4]; // LF, LB, RF, RB

    void SetMotorPins(bool lf, bool lb, bool rf, bool rb) {
        gpio_set_level(lf_pin_, lf);
        gpio_set_level(lb_pin_, lb);
        gpio_set_level(rf_pin_, rf);
        gpio_set_level(rb_pin_, rb);
    }

    // Initialize PWM for motor control
    bool InitPWM() {
        if (pwm_initialized_) return true;

        ESP_LOGI(MOTOR_LOG_TAG, "Initializing PWM timer...");

        // Configure PWM timer
        pwm_timer_config_.speed_mode = LEDC_LOW_SPEED_MODE;
        pwm_timer_config_.duty_resolution = LEDC_TIMER_10_BIT; // 10-bit resolution (0-1023)
        pwm_timer_config_.timer_num = LEDC_TIMER_1;
        pwm_timer_config_.freq_hz = 5000; // 5kHz PWM frequency
        pwm_timer_config_.clk_cfg = LEDC_AUTO_CLK;

        esp_err_t timer_ret = ledc_timer_config(&pwm_timer_config_);
        if (timer_ret != ESP_OK) {
            ESP_LOGE(MOTOR_LOG_TAG, "Failed to configure PWM timer: %s", esp_err_to_name(timer_ret));
            return false;
        }

        ESP_LOGI(MOTOR_LOG_TAG, "PWM timer configured successfully");

        // Configure PWM channels for each motor pin (use channels 0-3 to match application.cc)
        ledc_channel_t channels[4] = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3};
        gpio_num_t pins[4] = {lf_pin_, lb_pin_, rf_pin_, rb_pin_};

        for (int i = 0; i < 4; i++) {
            pwm_channel_configs_[i].gpio_num = pins[i];
            pwm_channel_configs_[i].speed_mode = LEDC_LOW_SPEED_MODE;
            pwm_channel_configs_[i].channel = channels[i];
            pwm_channel_configs_[i].timer_sel = LEDC_TIMER_1;
            pwm_channel_configs_[i].duty = 0;
            pwm_channel_configs_[i].hpoint = 0;

            if (ledc_channel_config(&pwm_channel_configs_[i]) != ESP_OK) {
                ESP_LOGE(MOTOR_LOG_TAG, "Failed to configure PWM channel %d", i);
                return false;
            }
        }

        pwm_initialized_ = true;
        ESP_LOGI(MOTOR_LOG_TAG, "PWM initialized successfully (freq=5kHz, 10-bit resolution)");
        return true;
    }

    // Set PWM duty cycle for a motor pin (0-100 speed)
    void SetMotorPWMDuty(ledc_channel_t channel, uint32_t speed_percent) {
        if (!pwm_initialized_) {
            ESP_LOGW(MOTOR_LOG_TAG, "PWM not initialized, falling back to GPIO control");
            return;
        }

        // 10-bit PWM resolution: max duty = 2^10 - 1 = 1023
        uint32_t max_duty = 1023;
        uint32_t duty = (speed_percent * max_duty) / 100;

        ESP_LOGI(MOTOR_LOG_TAG, "PWM Channel %d: speed_percent=%d%%, duty=%d/%d",
                 channel, speed_percent, duty, max_duty);

        ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
    }

public:
    MotorController(gpio_num_t lf_pin, gpio_num_t lb_pin, gpio_num_t rf_pin, gpio_num_t rb_pin)
        : lf_pin_(lf_pin), lb_pin_(lb_pin), rf_pin_(rf_pin), rb_pin_(rb_pin), pwm_initialized_(false) {

        // Don't initialize PWM automatically - let Application handle it
        // Initialize all motors to LOW (stop) using GPIO for now
        Stop();
    }

    // Public method to initialize PWM (called by Application after LEDC is ready)
    bool InitializePWM() {
        return InitPWM();
    }

    // Basic motor control functions (legacy GPIO mode)
    void Stop() {
        if (!pwm_initialized_) {
            // Try to initialize PWM if not already done
            InitPWM();
        }

        if (pwm_initialized_) {
            // Stop PWM
            SetMotorPWMDuty(LEDC_CHANNEL_0, 0); // LF
            SetMotorPWMDuty(LEDC_CHANNEL_1, 0); // LB
            SetMotorPWMDuty(LEDC_CHANNEL_2, 0); // RF
            SetMotorPWMDuty(LEDC_CHANNEL_3, 0); // RB
        } else {
            // Fallback to GPIO
            SetMotorPins(0, 0, 0, 0);
        }
    }

    void Forward(uint8_t speed_percent = 100) {
        if (!pwm_initialized_) {
            InitPWM();
        }

        if (pwm_initialized_) {
            SetMotorPWMDuty(LEDC_CHANNEL_0, speed_percent); // LF
            SetMotorPWMDuty(LEDC_CHANNEL_1, 0);             // LB
            SetMotorPWMDuty(LEDC_CHANNEL_2, speed_percent); // RF
            SetMotorPWMDuty(LEDC_CHANNEL_3, 0);             // RB
        } else {
            SetMotorPins(1, 0, 1, 0);
        }
    }

    void Backward(uint8_t speed_percent = 100) {
        if (!pwm_initialized_) {
            InitPWM();
        }

        if (pwm_initialized_) {
            SetMotorPWMDuty(LEDC_CHANNEL_0, 0);             // LF
            SetMotorPWMDuty(LEDC_CHANNEL_1, speed_percent); // LB
            SetMotorPWMDuty(LEDC_CHANNEL_2, 0);             // RF
            SetMotorPWMDuty(LEDC_CHANNEL_3, speed_percent); // RB
        } else {
            SetMotorPins(0, 1, 0, 1);
        }
    }

    void TurnLeft(uint8_t speed_percent = 100) {
        if (!pwm_initialized_) {
            InitPWM();
        }

        if (pwm_initialized_) {
            SetMotorPWMDuty(LEDC_CHANNEL_0, 0);             // LF
            SetMotorPWMDuty(LEDC_CHANNEL_1, speed_percent); // LB
            SetMotorPWMDuty(LEDC_CHANNEL_2, speed_percent); // RF
            SetMotorPWMDuty(LEDC_CHANNEL_3, 0);             // RB
        } else {
            SetMotorPins(0, 1, 1, 0);
        }
    }

    void TurnRight(uint8_t speed_percent = 100) {
        if (!pwm_initialized_) {
            InitPWM();
        }

        if (pwm_initialized_) {
            SetMotorPWMDuty(LEDC_CHANNEL_0, speed_percent); // LF
            SetMotorPWMDuty(LEDC_CHANNEL_1, 0);             // LB
            SetMotorPWMDuty(LEDC_CHANNEL_2, 0);             // RF
            SetMotorPWMDuty(LEDC_CHANNEL_3, speed_percent); // RB
        } else {
            SetMotorPins(1, 0, 0, 1);
        }
    }

    void ForwardLeft(uint8_t speed_percent = 100) {
        if (pwm_initialized_) {
            SetMotorPWMDuty(LEDC_CHANNEL_0, speed_percent); // LF
            SetMotorPWMDuty(LEDC_CHANNEL_1, 0);             // LB
            SetMotorPWMDuty(LEDC_CHANNEL_2, 0);             // RF
            SetMotorPWMDuty(LEDC_CHANNEL_3, 0);             // RB
        } else {
            SetMotorPins(1, 0, 0, 0);
        }
    }

    void ForwardRight(uint8_t speed_percent = 100) {
        if (pwm_initialized_) {
            SetMotorPWMDuty(LEDC_CHANNEL_0, 0);             // LF
            SetMotorPWMDuty(LEDC_CHANNEL_1, 0);             // LB
            SetMotorPWMDuty(LEDC_CHANNEL_2, speed_percent); // RF
            SetMotorPWMDuty(LEDC_CHANNEL_3, 0);             // RB
        } else {
            SetMotorPins(0, 0, 1, 0);
        }
    }

    void BackwardLeft(uint8_t speed_percent = 100) {
        if (pwm_initialized_) {
            SetMotorPWMDuty(LEDC_CHANNEL_0, 0);             // LF
            SetMotorPWMDuty(LEDC_CHANNEL_1, speed_percent); // LB
            SetMotorPWMDuty(LEDC_CHANNEL_2, 0);             // RF
            SetMotorPWMDuty(LEDC_CHANNEL_3, 0);             // RB
        } else {
            SetMotorPins(0, 1, 0, 0);
        }
    }

    void BackwardRight(uint8_t speed_percent = 100) {
        if (pwm_initialized_) {
            SetMotorPWMDuty(LEDC_CHANNEL_0, 0);             // LF
            SetMotorPWMDuty(LEDC_CHANNEL_1, 0);             // LB
            SetMotorPWMDuty(LEDC_CHANNEL_2, 0);             // RF
            SetMotorPWMDuty(LEDC_CHANNEL_3, speed_percent); // RB
        } else {
            SetMotorPins(0, 0, 0, 1);
        }
    }

    // Execute motor action with timing and speed control
    void ExecuteAction(int action, int on_time_ms, int off_time_ms, int repeat_count, uint8_t speed_percent = 100) {
        const char* action_name = "UNKNOWN";
        switch (action) {
            case MOTOR_STOP: action_name = "STOP"; break;
            case MOTOR_FORWARD: action_name = "FORWARD"; break;
            case MOTOR_BACKWARD: action_name = "BACKWARD"; break;
            case MOTOR_FULL_LEFT: action_name = "FULL_LEFT"; break;
            case MOTOR_FULL_RIGHT: action_name = "FULL_RIGHT"; break;
            case MOTOR_FORWARD_LEFT: action_name = "FORWARD_LEFT"; break;
            case MOTOR_FORWARD_RIGHT: action_name = "FORWARD_RIGHT"; break;
            case MOTOR_BACK_LEFT: action_name = "BACK_LEFT"; break;
            case MOTOR_BACK_RIGHT: action_name = "BACK_RIGHT"; break;
        }

        ESP_LOGI(MOTOR_LOG_TAG, "电机动作: %s, 速度:%d%%, 运行:%d毫秒, 停止:%d毫秒, 重复:%d次",
                 action_name, speed_percent, on_time_ms, off_time_ms, repeat_count);

        for (int i = 0; i < repeat_count; i++) {
            // Execute action with speed control
            switch (action) {
                case MOTOR_STOP:
                    Stop();
                    break;
                case MOTOR_FORWARD:
                    Forward(speed_percent);
                    break;
                case MOTOR_BACKWARD:
                    Backward(speed_percent);
                    break;
                case MOTOR_FULL_LEFT:
                    TurnLeft(speed_percent);
                    break;
                case MOTOR_FULL_RIGHT:
                    TurnRight(speed_percent);
                    break;
                case MOTOR_FORWARD_LEFT:
                    ForwardLeft(speed_percent);
                    break;
                case MOTOR_FORWARD_RIGHT:
                    ForwardRight(speed_percent);
                    break;
                case MOTOR_BACK_LEFT:
                    BackwardLeft(speed_percent);
                    break;
                case MOTOR_BACK_RIGHT:
                    BackwardRight(speed_percent);
                    break;
                default:
                    Stop();
                    break;
            }

            // Wait for on_time_ms
            if (on_time_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(on_time_ms));
            }

            // Stop motors during off_time_ms
            if (off_time_ms > 0 && i < repeat_count - 1) {
                Stop();
                vTaskDelay(pdMS_TO_TICKS(off_time_ms));
            }
        }

        // Ensure motors are stopped at the end
        Stop();
    }

    // Wake up animation - excited movement
    void WakeUpAnimation() {
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 唤醒 - 兴奋的动作");
        // Quick forward-backward movement
        ExecuteAction(MOTOR_FORWARD, 100, 50, 2, 100);
        ExecuteAction(MOTOR_BACKWARD, 100, 50, 2, 100);
        // Spin around
        ExecuteAction(MOTOR_FULL_LEFT, 200, 100, 3, 100);
        // Final stop
        Stop();
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 唤醒完成");
    }

    // Happy animation - playful movements
    void HappyAnimation() {
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 开心 - 欢快的舞蹈动作");
        // Dance-like movements
        ExecuteAction(MOTOR_FORWARD_LEFT, 150, 100, 2, 100);
        ExecuteAction(MOTOR_FORWARD_RIGHT, 150, 100, 2, 100);
        ExecuteAction(MOTOR_BACK_LEFT, 150, 100, 2, 100);
        ExecuteAction(MOTOR_BACK_RIGHT, 150, 100, 2, 100);
        Stop();
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 开心完成");
    }

    // Sad animation - slow movements
    void SadAnimation() {
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 悲伤 - 缓慢的后退动作");
        // Slow backward movement
        ExecuteAction(MOTOR_BACKWARD, 300, 200, 3, 50); // 使用较低的速度表示悲伤
        Stop();
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 悲伤完成");
    }

    // Thinking animation - small movements
    void ThinkingAnimation() {
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 思考 - 轻微的左右摆动");
        // Small left-right movements
        ExecuteAction(MOTOR_FORWARD_LEFT, 100, 150, 2, 60); // 使用较低的速度表示思考
        ExecuteAction(MOTOR_FORWARD_RIGHT, 100, 150, 2, 60);
        Stop();
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 思考完成");
    }

    // Listening animation - gentle swaying
    void ListeningAnimation() {
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 倾听 - 温柔的左右摇摆");
        // Gentle side-to-side movement
        ExecuteAction(MOTOR_FULL_LEFT, 200, 300, 2, 40); // 使用很低的速度表示倾听
        ExecuteAction(MOTOR_FULL_RIGHT, 200, 300, 2, 40);
        Stop();
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 倾听完成");
    }

    // Speaking animation - forward movement
    void SpeakingAnimation() {
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 说话 - 前进冲刺");
        // Forward thrust
        ExecuteAction(MOTOR_FORWARD, 150, 100, 3, 80); // 使用中等速度表示说话
        Stop();
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 说话完成");
    }

    // Random movement for idle state
    void RandomMovement() {
        int random_action = esp_random() % 9; // 0-8
        int random_on_time = 50 + (esp_random() % 100); // 50-150ms
        int random_off_time = 100 + (esp_random() % 200); // 100-300ms

        const char* action_names[] = {
            "STOP", "BACKWARD", "FORWARD", "FULL_LEFT", "FULL_RIGHT",
            "BACK_LEFT", "BACK_RIGHT", "FORWARD_LEFT", "FORWARD_RIGHT"
        };

        ESP_LOGI(MOTOR_LOG_TAG, "随机动作: %s (运行:%d毫秒, 停止:%d毫秒)",
                 action_names[random_action], random_on_time, random_off_time);

        ExecuteAction(random_action, random_on_time, random_off_time, 1, 60); // 随机动作使用中等速度
    }

    // Specific movement actions as requested
    void MoveForward(int duration_ms = 5000, uint8_t speed_percent = 100) {
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 前进 - 向前走%d毫秒, 速度%d%%", duration_ms, speed_percent);
        ExecuteAction(MOTOR_FORWARD, duration_ms, 0, 1, speed_percent);
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 前进完成");
    }

    void MoveBackward(int duration_ms = 5000, uint8_t speed_percent = 100) {
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 后退 - 向后走%d毫秒, 速度%d%%", duration_ms, speed_percent);
        ExecuteAction(MOTOR_BACKWARD, duration_ms, 0, 1, speed_percent);
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 后退完成");
    }

    void SpinAround(uint8_t speed_percent = 100) {
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 转圈 - 旋转一圈, 速度%d%%", speed_percent);
        // Assuming full rotation takes about 3 seconds at current speed
        ExecuteAction(MOTOR_FULL_LEFT, 2500, 0, 1, speed_percent);
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 转圈完成");
    }

    void TurnLeftDuration(int duration_ms = 600, uint8_t speed_percent = 100) {
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 左转 - 向左转%d毫秒, 速度%d%%", duration_ms, speed_percent);
        ExecuteAction(MOTOR_FULL_LEFT, duration_ms, 0, 1, speed_percent);
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 左转完成");
    }

    void TurnRightDuration(int duration_ms = 600, uint8_t speed_percent = 100) {
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 右转 - 向右转%d毫秒, 速度%d%%", duration_ms, speed_percent);
        ExecuteAction(MOTOR_FULL_RIGHT, duration_ms, 0, 1, speed_percent);
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 右转完成");
    }

    // Additional useful movements
    void QuickForward(uint8_t speed_percent = 100) {
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 快速前进 - 向前冲刺5秒, 速度%d%%", speed_percent);
        ExecuteAction(MOTOR_FORWARD, 5000, 0, 1, speed_percent);
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 快速前进完成");
    }

    void QuickBackward(uint8_t speed_percent = 100) {
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 快速后退 - 向后退5秒, 速度%d%%", speed_percent);
        ExecuteAction(MOTOR_BACKWARD, 5000, 0, 1, speed_percent);
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 快速后退完成");
    }

    void Wiggle(uint8_t speed_percent = 100) {
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 摆动 - 左右快速摆动, 速度%d%%", speed_percent);
        ExecuteAction(MOTOR_FULL_LEFT, 300, 200, 3, speed_percent);
        ExecuteAction(MOTOR_FULL_RIGHT, 300, 200, 3, speed_percent);
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 摆动完成");
    }

    void Dance(uint8_t speed_percent = 100) {
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 跳舞 - 欢快舞蹈, 速度%d%%", speed_percent);
        ExecuteAction(MOTOR_FORWARD_LEFT, 500, 300, 2, speed_percent);
        ExecuteAction(MOTOR_FORWARD_RIGHT, 500, 300, 2, speed_percent);
        ExecuteAction(MOTOR_BACK_LEFT, 500, 300, 2, speed_percent);
        ExecuteAction(MOTOR_BACK_RIGHT, 500, 300, 2, speed_percent);
        ESP_LOGI(MOTOR_LOG_TAG, "动作: 跳舞完成");
    }
};

#endif // _MOTOR_CONTROLLER_H_