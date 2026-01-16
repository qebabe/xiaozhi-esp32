/**
 * @file stepper_motor_controller.h
 * @brief ULN2003步进电机控制器
 * @details 支持通过ULN2003驱动芯片控制28BYJ-48等步进电机，提供MCP协议接口
 */

#ifndef __STEPPER_MOTOR_CONTROLLER_H__
#define __STEPPER_MOTOR_CONTROLLER_H__

#include "mcp_server.h"
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <vector>
#include <atomic>
#include <string>

class StepperMotorController {
private:
    /** 步进电机控制引脚 */
    gpio_num_t in1_pin_;
    gpio_num_t in2_pin_;
    gpio_num_t in3_pin_;
    gpio_num_t in4_pin_;

    /** 电机状态 */
    std::atomic<bool> is_running_;
    std::atomic<int> current_step_;
    std::atomic<int> target_steps_;
    std::atomic<int> step_delay_ms_;  // 旧字段：步进间隔(ms)，控制转速（用于初始化和兼容）
    std::atomic<bool> invert_direction_{true}; // 是否反转方向（用于调整与物理接线方向不一致的情况）
    // 当前使用 4-step 全步序列，因此每转序列步数应为 2048（半步序列 4096，4-step 为 2048）
    std::atomic<int> steps_per_rev_{2048}; // 每转序列步数（用户可调整）
    static constexpr int kMinDelayMs = 15;
    static constexpr int kMaxDelayMs = 60000;
    // 平滑加速控制字段
    std::atomic<int> current_delay_ms_{50};   // 当前实际延时（ms）
    std::atomic<int> target_delay_ms_{50};    // 目标延时（ms），SetSpeed 设置此值
    std::atomic<int> accel_step_ms_{2};       // 每次循环调整的延时步长（ms）

    /** FreeRTOS任务句柄 */
    TaskHandle_t motor_task_handle_;

    /** 步进序列长度（可切换为4步全步或8步半步） */
    static constexpr int kSequenceLen = 4;
    /** 28BYJ-48 步进电机的 4 步双线圈全步序列（更稳定，扭矩较好） */
    const uint8_t step_sequence_[kSequenceLen][4] = {
        {1, 1, 0, 0},
        {0, 1, 1, 0},
        {0, 0, 1, 1},
        {1, 0, 0, 1}
    };

    /**
     * @brief 设置单个步进的GPIO状态
     * @param step_index 步进序列索引 (0-7)
     */
    void SetStep(int step_index) {
        int idx = step_index % kSequenceLen;
        gpio_set_level(in1_pin_, step_sequence_[idx][0]);
        gpio_set_level(in2_pin_, step_sequence_[idx][1]);
        gpio_set_level(in3_pin_, step_sequence_[idx][2]);
        gpio_set_level(in4_pin_, step_sequence_[idx][3]);
    }

    /**
     * @brief 电机控制任务
     * @details 在单独的任务中运行电机控制逻辑
     */
    static void MotorTask(void* parameter) {
        StepperMotorController* controller = static_cast<StepperMotorController*>(parameter);

        while (true) {
            // 平滑调整 current_delay_ms_ 朝 target_delay_ms_ 变化
            int cur_delay = controller->current_delay_ms_.load();
            int tgt_delay = controller->target_delay_ms_.load();
            int accel = controller->accel_step_ms_.load();
            if (cur_delay < tgt_delay) {
                int next = cur_delay + accel;
                if (next > tgt_delay) next = tgt_delay;
                controller->current_delay_ms_ = next;
            } else if (cur_delay > tgt_delay) {
                int next = cur_delay - accel;
                if (next < tgt_delay) next = tgt_delay;
                controller->current_delay_ms_ = next;
            }
            if (controller->is_running_.load()) {
                int current_step = controller->current_step_.load();
                int target_steps = controller->target_steps_.load();

                if (target_steps > 0) {
                    // 正转
                    controller->SetStep(current_step % kSequenceLen);
                    if (!controller->invert_direction_.load()) {
                        controller->current_step_ = (current_step + 1) % kSequenceLen;
                    } else {
                        controller->current_step_ = (current_step + kSequenceLen - 1) % kSequenceLen;
                    }
                    controller->target_steps_--;

                    if (controller->target_steps_.load() <= 0) {
                        controller->Stop();
                    }
                } else if (target_steps < 0) {
                    // 反转
                    controller->SetStep(current_step % kSequenceLen);
                    if (!controller->invert_direction_.load()) {
                        controller->current_step_ = (current_step + kSequenceLen - 1) % kSequenceLen;  // 反向计数
                    } else {
                        controller->current_step_ = (current_step + 1) % kSequenceLen;
                    }
                    controller->target_steps_++;

                    if (controller->target_steps_.load() >= 0) {
                        controller->Stop();
                    }
                }
            } else {
                // 停止时断开所有线圈
                gpio_set_level(controller->in1_pin_, 0);
                gpio_set_level(controller->in2_pin_, 0);
                gpio_set_level(controller->in3_pin_, 0);
                gpio_set_level(controller->in4_pin_, 0);
            }

            vTaskDelay(pdMS_TO_TICKS(controller->current_delay_ms_.load()));
        }
    }

public:
    /**
     * @brief 构造函数
     * @param in1_pin IN1引脚 (ULN2003输入1)
     * @param in2_pin IN2引脚 (ULN2003输入2)
     * @param in3_pin IN3引脚 (ULN2003输入3)
     * @param in4_pin IN4引脚 (ULN2003输入4)
     */
    StepperMotorController(gpio_num_t in1_pin, gpio_num_t in2_pin, gpio_num_t in3_pin, gpio_num_t in4_pin)
        : in1_pin_(in1_pin), in2_pin_(in2_pin), in3_pin_(in3_pin), in4_pin_(in4_pin),
          is_running_(false), current_step_(0), target_steps_(0), step_delay_ms_(50) {

        // 配置GPIO引脚
        gpio_config_t config = {
            .pin_bit_mask = (1ULL << in1_pin_) | (1ULL << in2_pin_) | (1ULL << in3_pin_) | (1ULL << in4_pin_),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&config));

        // 初始化为低电平
        gpio_set_level(in1_pin_, 0);
        gpio_set_level(in2_pin_, 0);
        gpio_set_level(in3_pin_, 0);
        gpio_set_level(in4_pin_, 0);

        // 初始化延时控制值（使用 load/store 避免 atomic=atomic 被删除的问题）
        current_delay_ms_.store(step_delay_ms_.load());
        target_delay_ms_.store(step_delay_ms_.load());

        // 创建电机控制任务
        xTaskCreate(MotorTask, "stepper_motor", 2048, this, 5, &motor_task_handle_);

        // 注册MCP工具
        RegisterMcpTools();
    }

    /**
     * @brief 析构函数
     */
    ~StepperMotorController() {
        Stop();
        if (motor_task_handle_) {
            vTaskDelete(motor_task_handle_);
        }
    }

    /**
     * @brief 启动电机连续转动
     * @param direction 转动方向 (true=正转, false=反转)
     */
    void Start(bool direction = true) {
        is_running_ = true;
        target_steps_ = direction ? INT_MAX : INT_MIN;  // 连续转动
    }

    /**
     * @brief 按指定步数转动
     * @param steps 步数 (正数=正转, 负数=反转)
     */
    void Step(int steps) {
        target_steps_ = steps;
        is_running_ = true;
    }

    /**
     * @brief 停止电机
     */
    void Stop() {
        is_running_ = false;
        target_steps_ = 0;
    }

    /**
     * @brief 设置转速
     * @param rpm 转速 (每分钟转数)
     * @note 28BYJ-48步进电机每转需要2048步
     */
    void SetSpeed(float rpm) {
        if (rpm <= 0) return;

        // 根据当前配置的每圈步数计算每个序列步的延时（单位 ms）
        int sprev = steps_per_rev_.load();
        if (sprev <= 0) sprev = 2048;
        int delay_ms = (int)(60000.0f / (rpm * sprev));

        // 限制最小/最大延迟，并设置目标延时（用于平滑加速）
        if (delay_ms < kMinDelayMs) delay_ms = kMinDelayMs;
        if (delay_ms > kMaxDelayMs) delay_ms = kMaxDelayMs;

        target_delay_ms_ = delay_ms;
        // 保留旧字段以兼容查询
        step_delay_ms_ = delay_ms;
    }

    /** 设置每转序列步数（用于校准脉冲总数） */
    void SetStepsPerRevolution(int steps) {
        if (steps > 0) steps_per_rev_ = steps;
    }

    /**
     * @brief 获取电机状态
     * @return true=正在运行, false=停止
     */
    bool IsRunning() const {
        return is_running_.load();
    }

    /**
     * @brief 获取剩余步数
     * @return 剩余步数
     */
    int GetRemainingSteps() const {
        return target_steps_.load();
    }

    /** 设置方向反转开关（true = 反转） */
    void SetDirectionInverted(bool inverted) {
        invert_direction_ = inverted;
    }

private:
    /**
     * @brief 注册MCP协议工具
     */
    void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();

        // 获取电机状态
        mcp_server.AddTool("self.stepper.get_state", "Get the current state of the stepper motor",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                int sprev = steps_per_rev_.load();
                int delayms = current_delay_ms_.load();
                int targetms = target_delay_ms_.load();
                return std::string("{\"running\": ") + (is_running_.load() ? "true" : "false") +
                       ", \"remaining_steps\": " + std::to_string(target_steps_.load()) +
                       ", \"steps_per_rev\": " + std::to_string(sprev) +
                       ", \"current_delay_ms\": " + std::to_string(delayms) +
                       ", \"target_delay_ms\": " + std::to_string(targetms) + "}";
            });

        // 启动连续转动
        PropertyList start_params;
        start_params.AddProperty(Property("direction", kPropertyTypeString));
        mcp_server.AddTool("self.stepper.start", "Start continuous rotation of the stepper motor",
            start_params,
            [this](const PropertyList& properties) -> ReturnValue {
                try {
                    const Property& direction_prop = properties["direction"];
                    std::string direction_str = direction_prop.value<std::string>();
                    bool direction = (direction_str == "clockwise");
                    Start(direction);
                    return std::string("Motor started");
                } catch (const std::exception& e) {
                    return std::string("Error: Missing or invalid direction parameter");
                }
            });

        // 按步数转动
        PropertyList step_params;
        step_params.AddProperty(Property("steps", kPropertyTypeInteger));
        mcp_server.AddTool("self.stepper.step", "Rotate stepper motor by specified number of steps",
            step_params,
            [this](const PropertyList& properties) -> ReturnValue {
                try {
                    const Property& steps_prop = properties["steps"];
                    int steps = steps_prop.value<int>();
                    Step(steps);
                    return std::string("Step command sent");
                } catch (const std::exception& e) {
                    return std::string("Error: Missing or invalid steps parameter");
                }
            });

        // 停止电机
        mcp_server.AddTool("self.stepper.stop", "Stop the stepper motor",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                Stop();
                return std::string("Motor stopped");
            });

        // 设置转速
        PropertyList speed_params;
        speed_params.AddProperty(Property("rpm", kPropertyTypeInteger, 15, 1, 30));  // 默认15 RPM, 范围1-30
        mcp_server.AddTool("self.stepper.set_speed", "Set the rotation speed of the stepper motor",
            speed_params,
            [this](const PropertyList& properties) -> ReturnValue {
                try {
                    const Property& rpm_prop = properties["rpm"];
                    int rpm = rpm_prop.value<int>();
                    if (rpm <= 0) return std::string("Error: RPM must be positive");

                    // 校验基于当前 steps_per_rev 的最小延时要求，避免 delay < kMinDelayMs 导致失步
                    int sprev = steps_per_rev_.load();
                    if (sprev <= 0) sprev = 2048;
                    double rpm_max = (double)60000.0 / (kMinDelayMs * sprev);
                    if (rpm > rpm_max) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "Error: RPM too high for current steps_per_rev=%d, max allowed %.2f", sprev, rpm_max);
                        return std::string(buf);
                    }

                    SetSpeed(static_cast<float>(rpm));
                    return std::string("Speed set to " + std::to_string(rpm) + " RPM");
                } catch (const std::exception& e) {
                    return std::string("Error: Missing or invalid rpm parameter");
                }
            });
        // 设置方向反转
        PropertyList dir_params;
        dir_params.AddProperty(Property("inverted", kPropertyTypeBoolean, false));
        mcp_server.AddTool("self.stepper.set_direction_inverted", "Set direction inverted (true/false)",
            dir_params,
            [this](const PropertyList& properties) -> ReturnValue {
                try {
                    const Property& p = properties["inverted"];
                    bool inv = p.value<bool>();
                    SetDirectionInverted(inv);
                    return std::string(std::string("Direction inverted set to ") + (inv ? "true" : "false"));
                } catch (const std::exception& e) {
                    return std::string("Error: Missing or invalid inverted parameter");
                }
            });
        // 设置每转总步数（用于校准，一般 2048/4096 等）
        PropertyList sprev_params;
        sprev_params.AddProperty(Property("steps", kPropertyTypeInteger, 2048));
        mcp_server.AddTool("self.stepper.set_steps_per_rev", "Set steps per revolution (sequence steps)",
            sprev_params,
            [this](const PropertyList& properties) -> ReturnValue {
                try {
                    const Property& p = properties["steps"];
                    int s = p.value<int>();
                    SetStepsPerRevolution(s);
                    return std::string("Steps per revolution set to " + std::to_string(s));
                } catch (const std::exception& e) {
                    return std::string("Error: Missing or invalid steps parameter");
                }
            });
    }
};

#endif // __STEPPER_MOTOR_CONTROLLER_H__