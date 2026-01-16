#ifndef _MOTOR_CONTROLLER_H_
#define _MOTOR_CONTROLLER_H_

#include <driver/gpio.h>
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

    void SetMotorPins(bool lf, bool lb, bool rf, bool rb) {
        gpio_set_level(lf_pin_, lf);
        gpio_set_level(lb_pin_, lb);
        gpio_set_level(rf_pin_, rf);
        gpio_set_level(rb_pin_, rb);
    }

public:
    MotorController(gpio_num_t lf_pin, gpio_num_t lb_pin, gpio_num_t rf_pin, gpio_num_t rb_pin)
        : lf_pin_(lf_pin), lb_pin_(lb_pin), rf_pin_(rf_pin), rb_pin_(rb_pin) {

        // Configure GPIO pins
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << lf_pin) | (1ULL << lb_pin) | (1ULL << rf_pin) | (1ULL << rb_pin);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);

        // Initialize all pins to LOW (stop)
        Stop();
    }

    // Basic motor control functions
    void Stop() {
        SetMotorPins(0, 0, 0, 0);
    }

    void Forward() {
        SetMotorPins(1, 0, 1, 0);
    }

    void Backward() {
        SetMotorPins(0, 1, 0, 1);
    }

    void TurnLeft() {
        SetMotorPins(0, 1, 1, 0);
    }

    void TurnRight() {
        SetMotorPins(1, 0, 0, 1);
    }

    void ForwardLeft() {
        SetMotorPins(1, 0, 0, 0);
    }

    void ForwardRight() {
        SetMotorPins(0, 0, 1, 0);
    }

    void BackwardLeft() {
        SetMotorPins(0, 1, 0, 0);
    }

    void BackwardRight() {
        SetMotorPins(0, 0, 0, 1);
    }

    // Execute motor action with timing
    void ExecuteAction(int action, int on_time_ms, int off_time_ms, int repeat_count) {
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

        ESP_LOGI(MOTOR_LOG_TAG, "电机动作: %s, 运行:%d毫秒, 停止:%d毫秒, 重复:%d次",
                 action_name, on_time_ms, off_time_ms, repeat_count);

        for (int i = 0; i < repeat_count; i++) {
            // Execute action
            switch (action) {
                case MOTOR_STOP:
                    Stop();
                    break;
                case MOTOR_FORWARD:
                    Forward();
                    break;
                case MOTOR_BACKWARD:
                    Backward();
                    break;
                case MOTOR_FULL_LEFT:
                    TurnLeft();
                    break;
                case MOTOR_FULL_RIGHT:
                    TurnRight();
                    break;
                case MOTOR_FORWARD_LEFT:
                    ForwardLeft();
                    break;
                case MOTOR_FORWARD_RIGHT:
                    ForwardRight();
                    break;
                case MOTOR_BACK_LEFT:
                    BackwardLeft();
                    break;
                case MOTOR_BACK_RIGHT:
                    BackwardRight();
                    break;
                default:
                    Stop();
                    break;
            }

            if (on_time_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(on_time_ms));
            }

            // Stop between actions
            Stop();

            if (off_time_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(off_time_ms));
            }
        }
    }

    // Wake up animation - excited movement
    void WakeUpAnimation() {
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 唤醒 - 兴奋的动作");
        // Quick forward-backward movement
        ExecuteAction(MOTOR_FORWARD, 100, 50, 2);
        ExecuteAction(MOTOR_BACKWARD, 100, 50, 2);
        // Spin around
        ExecuteAction(MOTOR_FULL_LEFT, 200, 100, 3);
        // Final stop
        Stop();
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 唤醒完成");
    }

    // Happy animation - playful movements
    void HappyAnimation() {
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 开心 - 欢快的舞蹈动作");
        // Dance-like movements
        ExecuteAction(MOTOR_FORWARD_LEFT, 150, 100, 2);
        ExecuteAction(MOTOR_FORWARD_RIGHT, 150, 100, 2);
        ExecuteAction(MOTOR_BACK_LEFT, 150, 100, 2);
        ExecuteAction(MOTOR_BACK_RIGHT, 150, 100, 2);
        Stop();
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 开心完成");
    }

    // Sad animation - slow movements
    void SadAnimation() {
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 悲伤 - 缓慢的后退动作");
        // Slow backward movement
        ExecuteAction(MOTOR_BACKWARD, 300, 200, 3);
        Stop();
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 悲伤完成");
    }

    // Thinking animation - small movements
    void ThinkingAnimation() {
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 思考 - 轻微的左右摆动");
        // Small left-right movements
        ExecuteAction(MOTOR_FORWARD_LEFT, 100, 150, 2);
        ExecuteAction(MOTOR_FORWARD_RIGHT, 100, 150, 2);
        Stop();
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 思考完成");
    }

    // Listening animation - gentle swaying
    void ListeningAnimation() {
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 倾听 - 温柔的左右摇摆");
        // Gentle side-to-side movement
        ExecuteAction(MOTOR_FULL_LEFT, 200, 300, 2);
        ExecuteAction(MOTOR_FULL_RIGHT, 200, 300, 2);
        Stop();
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 倾听完成");
    }

    // Speaking animation - forward movement
    void SpeakingAnimation() {
        ESP_LOGI(MOTOR_LOG_TAG, "动画: 说话 - 前进冲刺");
        // Forward thrust
        ExecuteAction(MOTOR_FORWARD, 150, 100, 3);
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

        ExecuteAction(random_action, random_on_time, random_off_time, 1);
    }
};

#endif // _MOTOR_CONTROLLER_H_