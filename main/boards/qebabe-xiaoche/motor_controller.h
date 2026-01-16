#ifndef _MOTOR_CONTROLLER_H_
#define _MOTOR_CONTROLLER_H_

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_random.h>

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
        // Quick forward-backward movement
        ExecuteAction(MOTOR_FORWARD, 100, 50, 2);
        ExecuteAction(MOTOR_BACKWARD, 100, 50, 2);
        // Spin around
        ExecuteAction(MOTOR_FULL_LEFT, 200, 100, 3);
        // Final stop
        Stop();
    }

    // Happy animation - playful movements
    void HappyAnimation() {
        // Dance-like movements
        ExecuteAction(MOTOR_FORWARD_LEFT, 150, 100, 2);
        ExecuteAction(MOTOR_FORWARD_RIGHT, 150, 100, 2);
        ExecuteAction(MOTOR_BACK_LEFT, 150, 100, 2);
        ExecuteAction(MOTOR_BACK_RIGHT, 150, 100, 2);
        Stop();
    }

    // Sad animation - slow movements
    void SadAnimation() {
        // Slow backward movement
        ExecuteAction(MOTOR_BACKWARD, 300, 200, 3);
        Stop();
    }

    // Thinking animation - small movements
    void ThinkingAnimation() {
        // Small left-right movements
        ExecuteAction(MOTOR_FORWARD_LEFT, 100, 150, 2);
        ExecuteAction(MOTOR_FORWARD_RIGHT, 100, 150, 2);
        Stop();
    }

    // Listening animation - gentle swaying
    void ListeningAnimation() {
        // Gentle side-to-side movement
        ExecuteAction(MOTOR_FULL_LEFT, 200, 300, 2);
        ExecuteAction(MOTOR_FULL_RIGHT, 200, 300, 2);
        Stop();
    }

    // Speaking animation - forward movement
    void SpeakingAnimation() {
        // Forward thrust
        ExecuteAction(MOTOR_FORWARD, 150, 100, 3);
        Stop();
    }

    // Random movement for idle state
    void RandomMovement() {
        int random_action = esp_random() % 9; // 0-8
        int random_on_time = 50 + (esp_random() % 100); // 50-150ms
        int random_off_time = 100 + (esp_random() % 200); // 100-300ms
        ExecuteAction(random_action, random_on_time, random_off_time, 1);
    }
};

#endif // _MOTOR_CONTROLLER_H_