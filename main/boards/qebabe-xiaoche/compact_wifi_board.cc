#include "compact_wifi_board.h"
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
#include <wifi_manager.h>

// Forward declaration for Application function
extern "C" void HandleMotorActionForApplication(int direction, int speed, int duration_ms, int priority);
extern "C" void HandleMotorActionForEmotion(const char* emotion);



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

// Global pointer to motor controller for Application callbacks
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
        // No longer using MotorController - all motor control goes through Application

        // Add motor control tools to MCP server
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool("self.motor.move_forward",
            "Move the robot forward with specified speed and duration.\n"
            "Args:\n"
            "  `speed_percent`: Motor speed (0-100), default 100\n"
            "  `duration_ms`: Movement duration in milliseconds, default 5000\n"
            "Return:\n"
            "  Success message with parameters",
            PropertyList({
                Property("speed_percent", kPropertyTypeInteger, 100, 0, 100),
                Property("duration_ms", kPropertyTypeInteger, 5000, 100, 10000)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int speed = properties["speed_percent"].value<int>();
                int duration = properties["duration_ms"].value<int>();
                MotorMoveForward(duration, speed);
                return std::string("Moved forward at ") + std::to_string(speed) + "% speed for " + std::to_string(duration) + "ms";
            });

        mcp_server.AddTool("self.motor.move_backward",
            "Move the robot backward with specified speed and duration.\n"
            "Args:\n"
            "  `speed_percent`: Motor speed (0-100), default 100\n"
            "  `duration_ms`: Movement duration in milliseconds, default 5000\n"
            "Return:\n"
            "  Success message with parameters",
            PropertyList({
                Property("speed_percent", kPropertyTypeInteger, 100, 0, 100),
                Property("duration_ms", kPropertyTypeInteger, 5000, 100, 10000)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int speed = properties["speed_percent"].value<int>();
                int duration = properties["duration_ms"].value<int>();
                MotorMoveBackward(duration, speed);
                return std::string("Moved backward at ") + std::to_string(speed) + "% speed for " + std::to_string(duration) + "ms";
            });

        mcp_server.AddTool("self.motor.spin_around",
            "Spin the robot around in a full circle with specified speed.\n"
            "Args:\n"
            "  `speed_percent`: Motor speed (0-100), default 100\n"
            "Return:\n"
            "  Success message",
            PropertyList({
                Property("speed_percent", kPropertyTypeInteger, 100, 0, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int speed = properties["speed_percent"].value<int>();
                MotorSpinAround(speed);
                return std::string("Spin around completed at ") + std::to_string(speed) + "% speed";
            });

        mcp_server.AddTool("self.motor.turn_left",
            "Turn the robot left with specified speed and duration.\n"
            "Args:\n"
            "  `speed_percent`: Motor speed (0-100), default 100\n"
            "  `duration_ms`: Turn duration in milliseconds, default 600 (approx 90 degrees)\n"
            "Return:\n"
            "  Success message with parameters",
            PropertyList({
                Property("speed_percent", kPropertyTypeInteger, 100, 0, 100),
                Property("duration_ms", kPropertyTypeInteger, 600, 100, 5000)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int speed = properties["speed_percent"].value<int>();
                int duration = properties["duration_ms"].value<int>();
                MotorTurnLeftDuration(duration, speed);
                return std::string("Turned left at ") + std::to_string(speed) + "% speed for " + std::to_string(duration) + "ms";
            });

        mcp_server.AddTool("self.motor.turn_right",
            "Turn the robot right with specified speed and duration.\n"
            "Args:\n"
            "  `speed_percent`: Motor speed (0-100), default 100\n"
            "  `duration_ms`: Turn duration in milliseconds, default 600 (approx 90 degrees)\n"
            "Return:\n"
            "  Success message with parameters",
            PropertyList({
                Property("speed_percent", kPropertyTypeInteger, 100, 0, 100),
                Property("duration_ms", kPropertyTypeInteger, 600, 100, 5000)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int speed = properties["speed_percent"].value<int>();
                int duration = properties["duration_ms"].value<int>();
                MotorTurnRightDuration(duration, speed);
                return std::string("Turned right at ") + std::to_string(speed) + "% speed for " + std::to_string(duration) + "ms";
            });

        mcp_server.AddTool("self.motor.quick_forward",
            "Quick forward movement for 0.5 seconds with specified speed.\n"
            "Args:\n"
            "  `speed_percent`: Motor speed (0-100), default 100\n"
            "Return:\n"
            "  Success message",
            PropertyList({
                Property("speed_percent", kPropertyTypeInteger, 100, 0, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int speed = properties["speed_percent"].value<int>();
                MotorQuickForward(speed);
                return std::string("Quick forward movement completed at ") + std::to_string(speed) + "% speed";
            });

        mcp_server.AddTool("self.motor.quick_backward",
            "Quick backward movement for 0.5 seconds with specified speed.\n"
            "Args:\n"
            "  `speed_percent`: Motor speed (0-100), default 100\n"
            "Return:\n"
            "  Success message",
            PropertyList({
                Property("speed_percent", kPropertyTypeInteger, 100, 0, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int speed = properties["speed_percent"].value<int>();
                MotorQuickBackward(speed);
                return std::string("Quick backward movement completed at ") + std::to_string(speed) + "% speed";
            });

        mcp_server.AddTool("self.motor.wiggle",
            "Make the robot perform a quick wiggle movement (turn right briefly).\n"
            "Args:\n"
            "  `speed_percent`: Motor speed (0-100), default 100\n"
            "Return:\n"
            "  Success message",
            PropertyList({
                Property("speed_percent", kPropertyTypeInteger, 100, 0, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int speed = properties["speed_percent"].value<int>();
                MotorWiggle(speed);
                return std::string("Wiggle movement completed at ") + std::to_string(speed) + "% speed";
            });

        mcp_server.AddTool("self.motor.dance",
            "Make the robot perform a quick dance movement (move forward briefly).\n"
            "Args:\n"
            "  `speed_percent`: Motor speed (0-100), default 100\n"
            "Return:\n"
            "  Success message",
            PropertyList({
                Property("speed_percent", kPropertyTypeInteger, 100, 0, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int speed = properties["speed_percent"].value<int>();
                MotorDance(speed);
                return std::string("Dance movement completed at ") + std::to_string(speed) + "% speed";
            });

        mcp_server.AddTool("self.motor.stop",
            "Stop all motor movement immediately",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                HandleMotorActionForApplication(0, 0, 0, 2); // Stop, high priority
                return std::string("Motor stopped");
            });

        // Animation actions
        mcp_server.AddTool("self.motor.wake_up",
            "Play wake up animation - excited movement to greet the user",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                HandleMotorActionForEmotion("wake");
                return std::string("Wake up animation played");
            });

        mcp_server.AddTool("self.motor.happy",
            "Play happy animation - playful movements to show joy",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                HandleMotorActionForEmotion("happy");
                return std::string("Happy animation played");
            });

        mcp_server.AddTool("self.motor.sad",
            "Play sad animation - slow backward movements to show sadness",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                HandleMotorActionForEmotion("sad");
                return std::string("Sad animation played");
            });

        mcp_server.AddTool("self.motor.thinking",
            "Play thinking animation - small left-right movements",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                HandleMotorActionForEmotion("thinking");
                return std::string("Thinking animation played");
            });

        mcp_server.AddTool("self.motor.listening",
            "Play listening animation - gentle swaying movements",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                HandleMotorActionForEmotion("listening");
                return std::string("Listening animation played");
            });

        mcp_server.AddTool("self.motor.speaking",
            "Play speaking animation - forward thrusts",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                HandleMotorActionForEmotion("speaking");
                return std::string("Speaking animation played");
            });

        mcp_server.AddTool("self.motor.excited",
            "Play excited animation - fast movements in multiple directions",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                HandleMotorActionForEmotion("excited");
                return std::string("Excited animation played");
            });

        mcp_server.AddTool("self.motor.loving",
            "Play loving animation - gentle forward movements",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                HandleMotorActionForEmotion("loving");
                return std::string("Loving animation played");
            });

        mcp_server.AddTool("self.motor.angry",
            "Play angry animation - strong backward and forward movements",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                HandleMotorActionForEmotion("angry");
                return std::string("Angry animation played");
            });

        mcp_server.AddTool("self.motor.surprised",
            "Play surprised animation - quick backward then forward movement",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                HandleMotorActionForEmotion("surprised");
                return std::string("Surprised animation played");
            });

        mcp_server.AddTool("self.motor.confused",
            "Play confused animation - hesitant left-right movements",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                HandleMotorActionForEmotion("confused");
                return std::string("Confused animation played");
            });

        mcp_server.AddTool("self.network.get_ip",
            "获取当前WiFi IP地址信息，用于语音播报或状态查询",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                auto& wifi = WifiManager::GetInstance();
                std::string ip = wifi.GetIpAddress();
                if (ip.empty()) {
                    return std::string("当前未连接到WiFi网络，无法获取IP地址");
                }
                return std::string("当前IP地址是") + ip;
            });
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    // Unified motor control through Application (single PWM system)
    void MotorMoveForward(int duration_ms = 5000, uint8_t speed_percent = 100) {
        HandleMotorActionForApplication(4, speed_percent, duration_ms, 2); // 4 = forward, high priority
    }

    void MotorMoveBackward(int duration_ms = 5000, uint8_t speed_percent = 100) {
        HandleMotorActionForApplication(2, speed_percent, duration_ms, 2); // 2 = backward, high priority
    }

    void MotorTurnLeft(int duration_ms = 300, uint8_t speed_percent = 100) {
        HandleMotorActionForApplication(3, speed_percent, duration_ms, 2); // 3 = left, high priority
    }

    void MotorTurnRight(int duration_ms = 300, uint8_t speed_percent = 100) {
        HandleMotorActionForApplication(1, speed_percent, duration_ms, 2); // 1 = right, high priority
    }

    // Alias for compatibility
    void MotorTurnLeftDuration(int duration_ms = 600, uint8_t speed_percent = 100) {
        MotorTurnLeft(duration_ms, speed_percent);
    }

    void MotorTurnRightDuration(int duration_ms = 600, uint8_t speed_percent = 100) {
        MotorTurnRight(duration_ms, speed_percent);
    }

    void MotorStop() {
        HandleMotorActionForApplication(0, 0, 0, 2); // 0 = stop, high priority
    }

    void MotorSpinAround(uint8_t speed_percent = 100) {
        HandleMotorActionForApplication(3, speed_percent, 2000, 2); // Spin = turn left longer, high priority
    }

    void MotorQuickForward(uint8_t speed_percent = 100) {
        HandleMotorActionForApplication(4, speed_percent, 500, 2); // Quick forward, high priority
    }

    void MotorQuickBackward(uint8_t speed_percent = 100) {
        HandleMotorActionForApplication(2, speed_percent, 500, 2); // Quick backward, high priority
    }

    void MotorWiggle(uint8_t speed_percent = 100) {
        // Simple wiggle - just a quick left-right movement for MCP calls
        // Since MCP calls are synchronous, we can't use delays
        HandleMotorActionForApplication(1, speed_percent, 300, 2); // right turn, high priority
    }

public:
    void MotorDance(uint8_t speed_percent = 100) {
        ESP_LOGI(TAG, "电机跳舞: 执行完整的舞蹈序列 (速度: %d%%)", speed_percent);

        // 舞蹈序列：前进 -> 左转 -> 右转 -> 后退 -> 前进 -> 左转 -> 右转 -> 结束
        // 使用高优先级确保舞蹈动作不被其他动作打断

        // 第一步：快速前进
        HandleMotorActionForApplication(4, speed_percent, 300, 2); // forward
        vTaskDelay(pdMS_TO_TICKS(350));

        // 第二步：左转
        HandleMotorActionForApplication(3, speed_percent, 250, 2); // left
        vTaskDelay(pdMS_TO_TICKS(300));

        // 第三步：右转
        HandleMotorActionForApplication(1, speed_percent, 250, 2); // right
        vTaskDelay(pdMS_TO_TICKS(300));

        // 第四步：后退
        HandleMotorActionForApplication(2, speed_percent, 300, 2); // backward
        vTaskDelay(pdMS_TO_TICKS(350));

        // 第五步：前进
        HandleMotorActionForApplication(4, speed_percent, 200, 2); // forward
        vTaskDelay(pdMS_TO_TICKS(250));

        // 第六步：左转
        HandleMotorActionForApplication(3, speed_percent, 200, 2); // left
        vTaskDelay(pdMS_TO_TICKS(250));

        // 第七步：右转
        HandleMotorActionForApplication(1, speed_percent, 200, 2); // right
        vTaskDelay(pdMS_TO_TICKS(250));

        // 第八步：最终前进结束舞蹈
        HandleMotorActionForApplication(4, speed_percent, 400, 2); // forward
    }

public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
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

    void OnExcited() {
        ESP_LOGI(TAG, "电机情感: 兴奋被触发 - 执行快速动作");
        PerformMotorAction(1, 150); // FORWARD for 150ms - 快速前进
        vTaskDelay(pdMS_TO_TICKS(50));
        PerformMotorAction(3, 150); // LEFT for 150ms - 快速左转
        vTaskDelay(pdMS_TO_TICKS(50));
        PerformMotorAction(4, 150); // RIGHT for 150ms - 快速右转
    }

    void OnLoving() {
        ESP_LOGI(TAG, "电机情感: 爱慕被触发 - 执行温柔动作");
        PerformMotorAction(1, 300); // FORWARD for 300ms - 温柔前进
        vTaskDelay(pdMS_TO_TICKS(200));
        PerformMotorAction(3, 200); // LEFT for 200ms - 轻柔左转
    }

    void OnAngry() {
        ESP_LOGI(TAG, "电机情感: 生气被触发 - 执行强烈动作");
        PerformMotorAction(2, 200); // BACKWARD for 200ms - 后退表示生气
        vTaskDelay(pdMS_TO_TICKS(100));
        PerformMotorAction(1, 200); // FORWARD for 200ms - 前冲表示生气
    }

    void OnSurprised() {
        ESP_LOGI(TAG, "电机情感: 惊讶被触发 - 执行突然动作");
        PerformMotorAction(2, 100); // BACKWARD for 100ms - 快速后退
        vTaskDelay(pdMS_TO_TICKS(150));
        PerformMotorAction(1, 200); // FORWARD for 200ms - 前进表示惊讶
    }

    void OnConfused() {
        ESP_LOGI(TAG, "电机情感: 困惑被触发 - 执行犹豫动作");
        PerformMotorAction(3, 100); // LEFT for 100ms - 犹豫左转
        vTaskDelay(pdMS_TO_TICKS(200));
        PerformMotorAction(4, 100); // RIGHT for 100ms - 犹豫右转
        vTaskDelay(pdMS_TO_TICKS(200));
        PerformMotorAction(3, 100); // LEFT for 100ms - 再次犹豫
    }

    void OnIdle() {
        if ((esp_random() % 100) < 50) { // 5% chance for random movement
            ESP_LOGI(TAG, "电机空闲: 随机动作被触发 (5%概率)");
            // Simple random movement using Application's motor control
            int random_action = (esp_random() % 4) + 1; // 1-4 for different directions
            HandleMotorActionForApplication(random_action, 60, 500, 0); // 60% speed, 500ms duration, low priority
        }
    }

    // 电机动作执行函数实现 - 使用Application的统一PWM系统
    void PerformMotorAction(int action, int duration_ms) {
        uint8_t speed_percent = 80; // 使用80%的速度进行情感表达动作

        // Emotion actions use medium priority (1) - can be interrupted by MCP commands (2) but not by lower priority
        switch (action) {
            case 1: // FORWARD
                HandleMotorActionForApplication(4, speed_percent, duration_ms, 1);
                break;
            case 2: // BACKWARD
                HandleMotorActionForApplication(2, speed_percent, duration_ms, 1);
                break;
            case 3: // LEFT
                HandleMotorActionForApplication(3, speed_percent, duration_ms, 1);
                break;
            case 4: // RIGHT
                HandleMotorActionForApplication(1, speed_percent, duration_ms, 1);
                break;
            default:
                break;
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
        } else if (emotion_str == "excited") {
            board->OnExcited();
        } else if (emotion_str == "sad" || emotion_str == "unhappy") {
            board->OnSad();
        } else if (emotion_str == "thinking") {
            board->OnThinking();
        } else if (emotion_str == "confused") {
            board->OnConfused();
        } else if (emotion_str == "listening" || emotion_str == "curious") {
            board->OnListening();
        } else if (emotion_str == "speaking" || emotion_str == "talking") {
            board->OnSpeaking();
        } else if (emotion_str == "wake" || emotion_str == "wakeup") {
            board->OnWakeUp();
        } else if (emotion_str == "loving") {
            board->OnLoving();
        } else if (emotion_str == "angry") {
            board->OnAngry();
        } else if (emotion_str == "surprised") {
            board->OnSurprised();
        } else {
            ESP_LOGW(TAG, "Unknown emotion: %s", emotion_str.c_str());
        }
    }
}

// Global function for idle motor movements
extern "C" void HandleMotorIdleAction(void) {
    auto board = static_cast<CompactWifiBoard*>(&Board::GetInstance());
    board->OnIdle();
}

// Global function for dance motor action
extern "C" void HandleMotorActionForDance(uint8_t speed_percent) {
    auto board = static_cast<CompactWifiBoard*>(&Board::GetInstance());
    board->MotorDance(speed_percent);
}

DECLARE_BOARD(CompactWifiBoard);
