#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "motor_controller.h"



#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <utility>
#include <string>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"

// Forward declaration
class CompactWifiBoard;

// Global pointer to motor controller for Application callbacks
static CompactWifiBoard* g_motor_board = nullptr;

class CompactWifiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    MotorController* motor_controller_ = nullptr;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        boot_button_.OnClick([]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                // Get the current board instance and call EnterWifiConfigMode
                auto board = static_cast<WifiBoard*>(&Board::GetInstance());
                board->EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        touch_button_.OnPressDown([]() {
            Application::GetInstance().StartListening();
        });
        touch_button_.OnPressUp([]() {
            Application::GetInstance().StopListening();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    // 物联网初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
        motor_controller_ = new MotorController(MOTOR_LF_GPIO, MOTOR_LB_GPIO, MOTOR_RF_GPIO, MOTOR_RB_GPIO);

        // Add motor control tools to MCP server
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool("self.motor.move_forward",
            "Move the robot forward. If no duration is specified, moves for 5 seconds. You can specify a custom duration in milliseconds.",
            PropertyList({
                Property("duration_ms", kPropertyTypeInteger, 5000, 100, 10000)  // Default 5000ms, range 100ms to 10s
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int duration = properties["duration_ms"].value<int>();
                MotorMoveForward(duration);
                return std::string("Moved forward for ") + std::to_string(duration) + "ms";
            });

        mcp_server.AddTool("self.motor.move_backward",
            "Move the robot backward. If no duration is specified, moves for 5 seconds. You can specify a custom duration in milliseconds.",
            PropertyList({
                Property("duration_ms", kPropertyTypeInteger, 5000, 100, 10000)  // Default 5000ms, range 100ms to 10s
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int duration = properties["duration_ms"].value<int>();
                MotorMoveBackward(duration);
                return std::string("Moved backward for ") + std::to_string(duration) + "ms";
            });

        mcp_server.AddTool("self.motor.spin_around",
            "Spin the robot around in a full circle",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                MotorSpinAround();
                return std::string("Spin around completed");
            });

        mcp_server.AddTool("self.motor.turn_left",
            "Turn the robot left. If no duration is specified, turns for 0.6 seconds (approximately 90 degrees). You can specify a custom duration in milliseconds.",
            PropertyList({
                Property("duration_ms", kPropertyTypeInteger, 600, 100, 5000)  // Default 600ms, range 100ms to 5s
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int duration = properties["duration_ms"].value<int>();
                MotorTurnLeftDuration(duration);
                return std::string("Turned left for ") + std::to_string(duration) + "ms";
            });

        mcp_server.AddTool("self.motor.turn_right",
            "Turn the robot right. If no duration is specified, turns for 0.6 seconds (approximately 90 degrees). You can specify a custom duration in milliseconds.",
            PropertyList({
                Property("duration_ms", kPropertyTypeInteger, 600, 100, 5000)  // Default 600ms, range 100ms to 5s
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int duration = properties["duration_ms"].value<int>();
                MotorTurnRightDuration(duration);
                return std::string("Turned right for ") + std::to_string(duration) + "ms";
            });

        mcp_server.AddTool("self.motor.quick_forward",
            "Quick forward movement for 5 seconds",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                MotorQuickForward();
                return std::string("Quick forward movement completed");
            });

        mcp_server.AddTool("self.motor.quick_backward",
            "Quick backward movement for 5 seconds",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                MotorQuickBackward();
                return std::string("Quick backward movement completed");
            });

        mcp_server.AddTool("self.motor.wiggle",
            "Make the robot wiggle left and right",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                MotorWiggle();
                return std::string("Wiggle completed");
            });

        mcp_server.AddTool("self.motor.dance",
            "Make the robot perform a dance routine",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                MotorDance();
                return std::string("Dance completed");
            });

        mcp_server.AddTool("self.motor.stop",
            "Stop all motor movement immediately",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                if (motor_controller_) {
                    motor_controller_->Stop();
                }
                return std::string("Motor stopped");
            });
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    // Direct motor control methods
    void MotorForward(int duration_ms = 500) {
        if (motor_controller_) {
            motor_controller_->ExecuteAction(MOTOR_FORWARD, duration_ms, 0, 1);
        }
    }

    void MotorBackward(int duration_ms = 500) {
        if (motor_controller_) {
            motor_controller_->ExecuteAction(MOTOR_BACKWARD, duration_ms, 0, 1);
        }
    }

    void MotorTurnLeft(int duration_ms = 300) {
        if (motor_controller_) {
            motor_controller_->ExecuteAction(MOTOR_FULL_LEFT, duration_ms, 0, 1);
        }
    }

    void MotorTurnRight(int duration_ms = 300) {
        if (motor_controller_) {
            motor_controller_->ExecuteAction(MOTOR_FULL_RIGHT, duration_ms, 0, 1);
        }
    }

    // Specific movement actions as requested
    void MotorMoveForward(int duration_ms = 5000) {
        if (motor_controller_) {
            motor_controller_->MoveForward(duration_ms);
        }
    }

    void MotorMoveBackward(int duration_ms = 5000) {
        if (motor_controller_) {
            motor_controller_->MoveBackward(duration_ms);
        }
    }

    void MotorSpinAround() {
        if (motor_controller_) {
            motor_controller_->SpinAround();
        }
    }

    void MotorTurnLeftDuration(int duration_ms = 600) {
        if (motor_controller_) {
            motor_controller_->TurnLeftDuration(duration_ms);
        }
    }

    void MotorTurnRightDuration(int duration_ms = 600) {
        if (motor_controller_) {
            motor_controller_->TurnRightDuration(duration_ms);
        }
    }

    // Additional useful movements
    void MotorQuickForward() {
        if (motor_controller_) {
            motor_controller_->QuickForward();
        }
    }

    void MotorQuickBackward() {
        if (motor_controller_) {
            motor_controller_->QuickBackward();
        }
    }

    void MotorWiggle() {
        if (motor_controller_) {
            motor_controller_->Wiggle();
        }
    }

    void MotorDance() {
        if (motor_controller_) {
            motor_controller_->Dance();
        }
    }

public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        g_motor_board = this;
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeTools();
    }
    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    // Motor control interface for different emotions and actions
    void OnWakeUp() {
        ESP_LOGI(TAG, "电机情感: 唤醒被触发 - 执行兴奋动作");
        PerformMotorAction(1, 300); // FORWARD for 300ms - 兴奋的前进动作
    }

    void OnHappy() {
        ESP_LOGI(TAG, "电机情感: 开心被触发 - 执行欢快动作");
        PerformMotorAction(1, 200); // FORWARD for 200ms - 简单的开心动作
        vTaskDelay(pdMS_TO_TICKS(100));
        PerformMotorAction(3, 200); // LEFT for 200ms - 左转表示开心
    }

    void OnSad() {
        ESP_LOGI(TAG, "电机情感: 悲伤被触发 - 执行缓慢动作");
        PerformMotorAction(2, 400); // BACKWARD for 400ms - 缓慢后退表示悲伤
    }

    void OnThinking() {
        ESP_LOGI(TAG, "电机情感: 思考被触发 - 执行轻微动作");
        PerformMotorAction(3, 150); // LEFT for 150ms - 轻微左转表示思考
        vTaskDelay(pdMS_TO_TICKS(200));
        PerformMotorAction(4, 150); // RIGHT for 150ms - 右转表示思考
    }

    void OnListening() {
        ESP_LOGI(TAG, "电机情感: 倾听被触发 - 执行轻柔动作");
        PerformMotorAction(3, 100); // LEFT for 100ms - 轻柔左转
        vTaskDelay(pdMS_TO_TICKS(150));
        PerformMotorAction(4, 100); // RIGHT for 100ms - 右转表示倾听
    }

    void OnSpeaking() {
        ESP_LOGI(TAG, "电机情感: 说话被触发 - 执行前进动作");
        PerformMotorAction(1, 250); // FORWARD for 250ms - 前进表示说话
    }

    // 简单的电机动作执行函数
    void PerformMotorAction(int action, int duration_ms) {
        static bool gpio_initialized = false;

        // 初始化GPIO（只执行一次）
        if (!gpio_initialized) {
            gpio_config_t io_conf = {};
            io_conf.intr_type = GPIO_INTR_DISABLE;
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pin_bit_mask = (1ULL << MOTOR_LF_GPIO) | (1ULL << MOTOR_LB_GPIO) | (1ULL << MOTOR_RF_GPIO) | (1ULL << MOTOR_RB_GPIO);
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

            if (gpio_config(&io_conf) == ESP_OK) {
                gpio_initialized = true;
            }
        }

        if (!gpio_initialized) return;

        // 执行电机动作，使用config.h中定义的宏
        switch (action) {
            case 1: // FORWARD
                gpio_set_level(MOTOR_LF_GPIO, 1);  // LF
                gpio_set_level(MOTOR_LB_GPIO, 0); // LB
                gpio_set_level(MOTOR_RF_GPIO, 1); // RF
                gpio_set_level(MOTOR_RB_GPIO, 0);  // RB
                vTaskDelay(pdMS_TO_TICKS(duration_ms));
                gpio_set_level(MOTOR_LF_GPIO, 0);
                gpio_set_level(MOTOR_LB_GPIO, 0);
                gpio_set_level(MOTOR_RF_GPIO, 0);
                gpio_set_level(MOTOR_RB_GPIO, 0);
                break;
            case 2: // BACKWARD
                gpio_set_level(MOTOR_LF_GPIO, 0);  // LF
                gpio_set_level(MOTOR_LB_GPIO, 1); // LB
                gpio_set_level(MOTOR_RF_GPIO, 0); // RF
                gpio_set_level(MOTOR_RB_GPIO, 1);  // RB
                vTaskDelay(pdMS_TO_TICKS(duration_ms));
                gpio_set_level(MOTOR_LF_GPIO, 0);
                gpio_set_level(MOTOR_LB_GPIO, 0);
                gpio_set_level(MOTOR_RF_GPIO, 0);
                gpio_set_level(MOTOR_RB_GPIO, 0);
                break;
            case 3: // LEFT
                gpio_set_level(MOTOR_LF_GPIO, 0);  // LF
                gpio_set_level(MOTOR_LB_GPIO, 1); // LB
                gpio_set_level(MOTOR_RF_GPIO, 1); // RF
                gpio_set_level(MOTOR_RB_GPIO, 0);  // RB
                vTaskDelay(pdMS_TO_TICKS(duration_ms));
                gpio_set_level(MOTOR_LF_GPIO, 0);
                gpio_set_level(MOTOR_LB_GPIO, 0);
                gpio_set_level(MOTOR_RF_GPIO, 0);
                gpio_set_level(MOTOR_RB_GPIO, 0);
                break;
            case 4: // RIGHT
                gpio_set_level(MOTOR_LF_GPIO, 1);  // LF
                gpio_set_level(MOTOR_LB_GPIO, 0); // LB
                gpio_set_level(MOTOR_RF_GPIO, 0); // RF
                gpio_set_level(MOTOR_RB_GPIO, 1);  // RB
                vTaskDelay(pdMS_TO_TICKS(duration_ms));
                gpio_set_level(MOTOR_LF_GPIO, 0);
                gpio_set_level(MOTOR_LB_GPIO, 0);
                gpio_set_level(MOTOR_RF_GPIO, 0);
                gpio_set_level(MOTOR_RB_GPIO, 0);
                break;
        }
    }

    void OnIdle() {
        if (motor_controller_ && (esp_random() % 100) < 50) { // 5% chance for random movement
            ESP_LOGI(TAG, "电机空闲: 随机动作被触发 (5%概率)");
            motor_controller_->RandomMovement();
        }
    }
};

// Public wrapper functions for motor control
extern "C" void HandleMotorActionForEmotion(const char* emotion) {
    auto board = static_cast<CompactWifiBoard*>(&Board::GetInstance());
    if (emotion) {
        std::string emotion_str(emotion);
        if (emotion_str == "happy" || emotion_str == "joy") {
            board->OnHappy();
        } else if (emotion_str == "sad" || emotion_str == "unhappy") {
            board->OnSad();
        } else if (emotion_str == "thinking" || emotion_str == "confused") {
            board->OnThinking();
        } else if (emotion_str == "listening" || emotion_str == "curious") {
            board->OnListening();
        } else if (emotion_str == "speaking" || emotion_str == "talking") {
            board->OnSpeaking();
        } else if (emotion_str == "wake" || emotion_str == "wakeup" || emotion_str == "excited") {
            board->OnWakeUp();
        }
    }
}

// Global function for idle motor movements
extern "C" void HandleMotorIdleAction(void) {
    auto board = static_cast<CompactWifiBoard*>(&Board::GetInstance());
    board->OnIdle();
}

DECLARE_BOARD(CompactWifiBoard);
