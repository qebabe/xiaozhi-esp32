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
        if (motor_controller_) {
            motor_controller_->WakeUpAnimation();
        }
    }

    void OnHappy() {
        if (motor_controller_) {
            motor_controller_->HappyAnimation();
        }
    }

    void OnSad() {
        if (motor_controller_) {
            motor_controller_->SadAnimation();
        }
    }

    void OnThinking() {
        if (motor_controller_) {
            motor_controller_->ThinkingAnimation();
        }
    }

    void OnListening() {
        if (motor_controller_) {
            motor_controller_->ListeningAnimation();
        }
    }

    void OnSpeaking() {
        if (motor_controller_) {
            motor_controller_->SpeakingAnimation();
        }
    }

    void OnIdle() {
        if (motor_controller_ && (esp_random() % 100) < 5) { // 5% chance for random movement
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
