#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"
#include "web_server/web_server.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>
#include <tuple>

#define TAG "Application"
// 包含板级引脚配置（由选定的 board 提供的 config.h）
// 使用 __has_include 避免在静态分析时因文件不可用导致错误
#if defined(__has_include)
#  if __has_include("config.h")
#    include "config.h"
#  endif
#else
/* __has_include not available; rely on build system to provide config.h */
#endif

// 如果没有提供 BOARD 层的 MOTOR_* 定义，提供默认值以便本文件在不同环境下也能编译
#ifndef MOTOR_LF_GPIO
#define MOTOR_LF_GPIO GPIO_NUM_12
#define MOTOR_LB_GPIO GPIO_NUM_13
#define MOTOR_RF_GPIO GPIO_NUM_14
#define MOTOR_RB_GPIO GPIO_NUM_21
#endif

// Test comment to trigger reanalysis
// 说明（中文）：
// 该文件实现设备的主应用逻辑，包括：
// - 设备初始化（显示、音频、网络、OTA 等）
// - 主事件循环（处理定时、网络、音频、状态变化等）
// - 协议初始化与消息处理（MQTT/WebSocket）
// - 将服务器下发的情绪（emotion）映射为电机动作并调度执行
// 注：电机动作通过消息队列发送到 `MotorControlTask` 在单独任务中执行，避免阻塞主循环。

// Motor control functions - only available on qebabe-xiaoche board
// These are declared as weak externs and will be resolved at link time
extern "C" void HandleMotorActionForEmotion(const char* emotion) __attribute__((weak));
extern "C" void (*HandleMotorActionForEmotionPtr)(const char* emotion) __attribute__((weak));
extern "C" void (*HandleMotorIdleActionPtr)(void) __attribute__((weak));

// Motor action queue for sequential execution
#include <queue>
#include <mutex>
struct MotorAction {
    int direction;
    int speed;
    int duration_ms;
    std::string description;
};

static std::queue<MotorAction> motor_action_queue_;
static std::mutex motor_queue_mutex_;
static volatile bool motor_executor_running_ = false;

// Motor control task (deprecated - now using global functions)

// Motor action flags for state-based actions


Application::Application() {
    event_group_ = xEventGroupCreate();

    // 构造函数说明（中文）：
    // 创建事件组、定时器等基础资源。实际的电机队列和任务在 Initialize() 中创建，
    // 以保证硬件和系统资源已完成初始化。

    // 初始化Web服务器
    web_server_ = std::make_unique<WebServer>();

    // 加载电机动作配置
    LoadMotorActionConfig();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
            .callback = [](void* arg) -> void {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);

    // Motor control queue and task will be created in Initialize() to ensure proper initialization
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }

    // Removed old motor control task and queue cleanup - now using unified PWM system

    vEventGroupDelete(event_group_);
}

bool Application::SetDeviceState(DeviceState state) {
    return state_machine_.TransitionTo(state);
}

void Application::Initialize() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    // Setup the display
    auto display = board.GetDisplay();

    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    // Default to eye-only display mode on startup (enable animated emotion)
    display_mode_ = kDisplayModeEyeOnly;
    SetDisplayMode(kDisplayModeEyeOnly);
    ESP_LOGI("Application", "Animated emotion mode enabled by default (Eye Only)");

    // Initialize 说明（中文）：
    // 该函数负责完成设备的整体初始化：
    // 1. 初始化显示和音频服务
    // 2. 注册音频回调（唤醒词、VAD 等）
    // 3. 启动时钟定时器以更新状态栏
    // 4. 初始化 MCP 服务工具（调试/远程控制）
    // 5. 设置网络事件回调并异步启动网络

    // Setup the audio service
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Add state change listeners
    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    // Start the clock timer to update the status bar
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // Add MCP common tools (only once during initialization)
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    // Set network event callback for UI updates and network state handling
    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        auto display = Board::GetInstance().GetDisplay();
        
        switch (event) {
            case NetworkEvent::Scanning:
                display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::Connecting: {
                if (data.empty()) {
                    // Cellular network - registering without carrier info yet
                    display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                } else {
                    // WiFi or cellular with carrier info
                    std::string msg = Lang::Strings::CONNECT_TO;
                    msg += data;
                    msg += "...";
                    display->ShowNotification(msg.c_str(), 30000);
                }
                break;
            }
            case NetworkEvent::Connected: {
                std::string msg = Lang::Strings::CONNECTED_TO;
                msg += data;
                display->ShowNotification(msg.c_str(), 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
                break;
            }
            case NetworkEvent::Disconnected:
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::WifiConfigModeEnter:
                // WiFi config mode enter is handled by WifiBoard internally
                break;
            case NetworkEvent::WifiConfigModeExit:
                // WiFi config mode exit is handled by WifiBoard internally
                break;
            // Cellular modem specific events
            case NetworkEvent::ModemDetecting:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorNoSim:
                Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_PIN);
                break;
            case NetworkEvent::ModemErrorRegDenied:
                Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_REG);
                break;
            case NetworkEvent::ModemErrorInitFailed:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                display->SetChatMessage("system", Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorTimeout:
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                break;
        }
    });

    // Start network asynchronously
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
}

// 显示模式相关方法实现
void Application::SetDisplayMode(DisplayMode mode) {
    if (display_mode_ != mode) {
        display_mode_ = mode;
        ESP_LOGI("Application", "Display mode changed to: %s",
                 mode == kDisplayModeDefault ? "Default" : "Eye Only");

        // 根据显示模式更新显示器状态
        auto display = Board::GetInstance().GetDisplay();
        if (mode == kDisplayModeEyeOnly) {
            // 眼睛模式：启用动画表情，隐藏文字
            display->SetAnimatedEmotionMode(true);
            display->SetStatus("");  // 清空状态文字
            display->SetChatMessage("system", "");  // 清空聊天消息
        } else {
            // 默认模式：根据当前状态显示相应内容
            display->SetAnimatedEmotionMode(false);
            // 重新设置当前状态的显示内容
            HandleStateChangedEvent();
        }
    }
}

void Application::ToggleDisplayMode() {
    DisplayMode new_mode = (display_mode_ == kDisplayModeDefault) ?
                           kDisplayModeEyeOnly : kDisplayModeDefault;
    SetDisplayMode(new_mode);
}

void Application::Run() {
    const EventBits_t ALL_EVENTS = 
        MAIN_EVENT_SCHEDULE |
        MAIN_EVENT_SEND_AUDIO |
        MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE |
        MAIN_EVENT_CLOCK_TICK |
        MAIN_EVENT_ERROR |
        MAIN_EVENT_NETWORK_CONNECTED |
        MAIN_EVENT_NETWORK_DISCONNECTED |
        MAIN_EVENT_TOGGLE_CHAT |
        MAIN_EVENT_START_LISTENING |
        MAIN_EVENT_STOP_LISTENING |
        MAIN_EVENT_ACTIVATION_DONE |
        MAIN_EVENT_STATE_CHANGED;

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }

        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }

        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }

        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }

        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }

        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();

            // Update animated emotion if enabled
            display->UpdateAnimatedEmotion();

            // Handle motor idle actions (only on boards that support it)
            // Use separate motor control task to avoid stability issues
            // Trigger motor control every 30 seconds to reduce frequency
            // Removed old motor control task trigger - now using unified PWM system

            // Motor feedback is now handled in HandleStateChangedEvent() to avoid duplication
            // Print debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::HandleNetworkConnectedEvent() {
    ESP_LOGI(TAG, "Network connected");
    auto state = GetDeviceState();

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        // Network is ready, start activation
        SetDeviceState(kDeviceStateActivating);
        if (activation_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Activation task already running");
            return;
        }

        xTaskCreate([](void* arg) {
            Application* app = static_cast<Application*>(arg);
            app->ActivationTask();
            app->activation_task_handle_ = nullptr;
            vTaskDelete(NULL);
        }, "activation", 4096 * 2, this, 2, &activation_task_handle_);
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleNetworkDisconnectedEvent() {
    // Close current conversation when network disconnected
    auto state = GetDeviceState();
    if (state == kDeviceStateConnecting || state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        protocol_->CloseAudioChannel();
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent() {
    ESP_LOGI(TAG, "Activation done");

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota_->HasServerTime();

    auto display = Board::GetInstance().GetDisplay();
    std::string message = std::string(Lang::Strings::VERSION) + ota_->GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");

    // Play the success sound to indicate the device is ready
    audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);

    // Release OTA object after activation is complete
    ota_.reset();
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    // Start web server for remote control
    web_server_->SetMotorControlCallback([this](int direction, int speed) {
        HandleWebMotorControl(direction, speed);
    });

    // Set emotion callback for web interface
    web_server_->SetEmotionCallback([](const char* emotion) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetEmotion(emotion);
    });

    // Set motor action config callbacks for web interface
    web_server_->SetMotorActionConfigCallback(
        [this]() -> WebServer::MotorActionConfig {
            const auto& config = GetMotorActionConfig();
            WebServer::MotorActionConfig ws_config;
            ws_config.forward_duration_ms = config.forward_duration_ms;
            ws_config.backward_duration_ms = config.backward_duration_ms;
            ws_config.left_turn_duration_ms = config.left_turn_duration_ms;
            ws_config.right_turn_duration_ms = config.right_turn_duration_ms;
            ws_config.spin_duration_ms = config.spin_duration_ms;
            ws_config.wiggle_duration_ms = config.wiggle_duration_ms;
            ws_config.dance_duration_ms = config.dance_duration_ms;
            ws_config.quick_forward_duration_ms = config.quick_forward_duration_ms;
            ws_config.quick_backward_duration_ms = config.quick_backward_duration_ms;
            ws_config.default_speed_percent = config.default_speed_percent;
            return ws_config;
        },
        [this](const WebServer::MotorActionConfig& ws_config) {
            MotorActionConfig config;
            config.forward_duration_ms = ws_config.forward_duration_ms;
            config.backward_duration_ms = ws_config.backward_duration_ms;
            config.left_turn_duration_ms = ws_config.left_turn_duration_ms;
            config.right_turn_duration_ms = ws_config.right_turn_duration_ms;
            config.spin_duration_ms = ws_config.spin_duration_ms;
            config.wiggle_duration_ms = ws_config.wiggle_duration_ms;
            config.dance_duration_ms = ws_config.dance_duration_ms;
            config.quick_forward_duration_ms = ws_config.quick_forward_duration_ms;
            config.quick_backward_duration_ms = ws_config.quick_backward_duration_ms;
            config.default_speed_percent = ws_config.default_speed_percent;
            SetMotorActionConfig(config);
        }
    );

    if (web_server_->Start(80)) {
        ESP_LOGI(TAG, "Web server started successfully on port 80");
        display->ShowNotification("Web控制已启用", 2000);

        // Initialize MotorController PWM after web server is ready
        // (LEDC should be fully initialized by now)
        // For now, we'll rely on the fact that CompactWifiBoard is the only board type
        // and PWM will be initialized when motor control is first used
    } else {
        ESP_LOGE(TAG, "Failed to start web server");
        display->ShowNotification("Web控制启动失败", 2000);
    }
}

void Application::ActivationTask() {
    // Create OTA object for activation process
    ota_ = std::make_unique<Ota>();

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version
    CheckNewVersion();

    // Initialize the protocol
    InitializeProtocol();

    // Signal completion to main loop
    xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
}

void Application::CheckAssetsVersion() {
    // Only allow CheckAssetsVersion to be called once
    if (assets_version_checked_) {
        return;
    }
    assets_version_checked_ = true;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [display](int progress, size_t speed) -> void {
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            SetDeviceState(kDeviceStateActivating);
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // Initial retry delay in seconds

    auto& board = Board::GetInstance();
    while (true) {
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        esp_err_t err = ota_->CheckVersion();
        if (err != ESP_OK) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char error_message[128];
            snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err, ota_->GetCheckVersionUrl().c_str());
            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, error_message);
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (GetDeviceState() == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // Double the retry delay
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // Reset retry delay

        if (ota_->HasNewVersion()) {
            if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation
        }

        // No new version, mark the current version as valid
        ota_->MarkCurrentVersionValid();
        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_->HasActivationCode()) {
            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_->Activate();
            if (err == ESP_OK) {
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (GetDeviceState() == kDeviceStateIdle) {
                break;
            }
        }
    }

    // Removed old motor control queue and task - now using unified PWM system
}

void Application::InitializeProtocol() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto codec = board.GetAudioCodec();

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    if (ota_->HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_->HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (GetDeviceState() == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    
    // OnIncomingJson 回调（中文说明）：
    // 该回调处理来自服务器的 JSON 消息，消息类型包括：
    // - tts: 文本到语音控制（开始/停止/句子开始）
    // - stt: 识别结果（显示用户文本）
    // - llm: 语言模型输出，包含 emotion 字段，用于驱动电机动作和显示表情
    // - mcp: MCP 协议消息
    // - system: 系统命令（例如 reboot）
    // - alert: 弹出通知（包含 status/message/emotion）
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // 将收到的原始 JSON 打印到串口，便于调试
        char* json_str = cJSON_PrintUnformatted(root);
        if (json_str != nullptr) {
            ESP_LOGI(TAG, "Received JSON message: %s", json_str);
            cJSON_free(json_str);
        }

        // 开始解析 JSON 字段 type 并分发处理逻辑
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    SetDeviceState(kDeviceStateSpeaking);
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (GetDeviceState() == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                Schedule([display, message = std::string(text->valuestring)]() {
                    // 在动画表情模式下不显示聊天消息
                    if (!display->IsAnimatedEmotionMode()) {
                        display->SetChatMessage("assistant", message.c_str());
                    }
                });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                std::string message = std::string(text->valuestring);
                ESP_LOGI(TAG, ">> %s", message.c_str());

                // 检查是否是显示模式切换命令
                bool is_display_mode_command = false;
                if (message.find("切换模式") != std::string::npos ||
                    message.find("切换显示") != std::string::npos ||
                    message.find("眼睛模式") != std::string::npos ||
                    message.find("默认模式") != std::string::npos ||
                    message.find("文字模式") != std::string::npos ||
                    message.find("change mode") != std::string::npos ||
                    message.find("eye mode") != std::string::npos ||
                    message.find("text mode") != std::string::npos) {
                    is_display_mode_command = true;
                    ESP_LOGI(TAG, "Detected display mode toggle command");
                    Schedule([this]() {
                        ToggleDisplayMode();
                    });
                }

                Schedule([this, display, message, is_command = is_display_mode_command]() {
                    // 在眼睛模式或命令模式下不显示聊天消息
                    if (display_mode_ != kDisplayModeEyeOnly && !is_command) {
                        display->SetChatMessage("user", message.c_str());
                    }
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                std::string emotion_str = std::string(emotion->valuestring);

                // 更新显示表情
                Schedule([display, emotion_str]() {
                    display->SetEmotion(emotion_str.c_str());
                });

                // 根据常用 emoji / 情绪字符串映射到电机命令
                // 命令说明（约定）：
                // 0: 不触发
                // 1: 短促向前（温柔 / 开心）
                // 2: 短促向后（悲伤 / 哭）
                // 3: 左右快摆（顽皮 / 笑）
                // 4: 轻点/点头（喜欢 / 自信 / 酷）
                // 5: 轻微倾斜/停顿（困惑 / 尴尬 / 思考）
                // 6: 突然/强烈动作（惊讶 / 震惊 / 生气）
                int motor_cmd = 0;

                // 支持情绪文本和 emoji 字符两种情况 - 与眼睛表情完全对应
                if (emotion_str == "smile") {
                    // smile -> happy
                    motor_cmd = 1;
                } else if (emotion_str == "laugh") {
                    // laugh -> laughing
                    motor_cmd = 3;
                } else if (emotion_str == "cry") {
                    // cry -> crying
                    motor_cmd = 2;
                } else if (emotion_str == "wink") {
                    // wink -> winking
                    motor_cmd = 3;
                } else if (emotion_str == "😶" || emotion_str == "neutral") {
                    // 1. 😶 - neutral
                    motor_cmd = 0;
                } else if (emotion_str == "🙂" || emotion_str == "happy") {
                    // 2. 🙂 - happy
                    motor_cmd = 1;
                } else if (emotion_str == "😆" || emotion_str == "laughing") {
                    // 3. 😆 - laughing
                    motor_cmd = 3;
                } else if (emotion_str == "😂" || emotion_str == "funny") {
                    // 4. 😂 - funny
                    motor_cmd = 3;
                } else if (emotion_str == "😔" || emotion_str == "sad") {
                    // 5. 😔 - sad
                    motor_cmd = 2;
                } else if (emotion_str == "😠" || emotion_str == "angry") {
                    // 6. 😠 - angry
                    motor_cmd = 6;
                } else if (emotion_str == "😭" || emotion_str == "crying") {
                    // 7. 😭 - crying
                    motor_cmd = 2;
                } else if (emotion_str == "😍" || emotion_str == "loving") {
                    // 8. 😍 - loving
                    motor_cmd = 4;
                } else if (emotion_str == "😳" || emotion_str == "embarrassed") {
                    // 9. 😳 - embarrassed
                    motor_cmd = 5;
                } else if (emotion_str == "😲" || emotion_str == "surprised") {
                    // 10. 😲 - surprised
                    motor_cmd = 6;
                } else if (emotion_str == "😱" || emotion_str == "shocked") {
                    // 11. 😱 - shocked
                    motor_cmd = 6;
                } else if (emotion_str == "🤔" || emotion_str == "thinking") {
                    // 12. 🤔 - thinking
                    motor_cmd = 5;
                } else if (emotion_str == "😉" || emotion_str == "winking") {
                    // 13. 😉 - winking
                    motor_cmd = 3;
                } else if (emotion_str == "😎" || emotion_str == "cool") {
                    // 14. 😎 - cool
                    motor_cmd = 1;
                } else if (emotion_str == "😌" || emotion_str == "relaxed") {
                    // 15. 😌 - relaxed
                    motor_cmd = 1;
                } else if (emotion_str == "🤤" || emotion_str == "delicious") {
                    // 16. 🤤 - delicious
                    motor_cmd = 1;
                } else if (emotion_str == "😘" || emotion_str == "kissy") {
                    // 17. 😘 - kissy
                    motor_cmd = 4;
                } else if (emotion_str == "😏" || emotion_str == "confident") {
                    // 18. 😏 - confident
                    motor_cmd = 1;
                } else if (emotion_str == "😴" || emotion_str == "sleepy") {
                    // 19. 😴 - sleepy
                    motor_cmd = 0;
                } else if (emotion_str == "😜" || emotion_str == "silly") {
                    // 20. 😜 - silly
                    motor_cmd = 3;
                } else if (emotion_str == "🙄" || emotion_str == "confused") {
                    // 21. 🙄 - confused
                    motor_cmd = 5;
                } else {
                    // 如果 emotion 字段是原生 emoji 字符（例如 "😊"）但未覆盖上面分支，可在这里做更多指定
                    motor_cmd = 0;
                }

                if (motor_cmd != 0) {
                    Schedule([this, motor_cmd]() {
                        TriggerMotorEmotion(motor_cmd);
                    });
                }
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    // 在动画表情模式下不显示聊天消息
                    if (!display->IsAnimatedEmotionMode()) {
                        display->SetChatMessage("system", payload_str.c_str());
                    }
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    
    protocol_->Start();
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();

    // 在动画表情模式下不显示状态文字和聊天消息，只显示表情
    if (display->IsAnimatedEmotionMode()) {
        display->SetStatus("");
        display->SetEmotion(emotion);
        // 不显示聊天消息
    } else {
        display->SetStatus(status);
        display->SetEmotion(emotion);
        display->SetChatMessage("system", message);
    }

    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }

    // Handle motor actions for emotion (only on boards that support it)
    if (emotion && strlen(emotion) > 0 && HandleMotorActionForEmotion) {
        HandleMotorActionForEmotion(emotion);
    }
}

void Application::DismissAlert() {
    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT);
}

void Application::StartListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING);
}

void Application::StopListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING);
}

void Application::HandleToggleChatEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                return;
            }
        }

        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening) {
        protocol_->CloseAudioChannel();
    }
}

void Application::HandleStartListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                return;
            }
        }

        SetListeningMode(kListeningModeManualStop);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    } else if (state == kDeviceStateListening) {
        if (protocol_) {
            protocol_->SendStopListening();
        }
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleWakeWordDetectedEvent() {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        // Set flag to play popup sound after state changes to listening
        // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
        play_popup_on_listening_ = true;
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#endif
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (state == kDeviceStateActivating) {
        // Restart the activation check if the wake word is detected during activation
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleStateChangedEvent() {
    static DeviceState last_state_for_motor = kDeviceStateUnknown;
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;

    // Handle motor feedback for state changes (immediate execution)
    if (new_state != last_state_for_motor) {
        switch (new_state) {
            case kDeviceStateListening:
                ESP_LOGI("Application", "状态变化事件: 唤醒 - 加入平衡电机反馈队列");
                // Queue wake balance feedback actions
                this->QueueMotorAction(4, 40, 200, "Wake forward");
                this->QueueMotorAction(2, 40, 200, "Wake backward");
                break;
            case kDeviceStateSpeaking:
                ESP_LOGI("Application", "状态变化事件: 开始说话 - 加入电机反馈队列");
                this->QueueMotorAction(4, 50, 250, "Start speaking forward");
                break;
            default:
                // No motor feedback for other state changes
                break;
        }

        // Handle transition FROM speaking
        if (last_state_for_motor == kDeviceStateSpeaking && new_state != kDeviceStateSpeaking) {
            ESP_LOGI("Application", "状态变化事件: 说话结束 - 加入电机反馈队列");
            this->QueueMotorAction(2, 45, 220, "End speaking backward");
        }

        last_state_for_motor = new_state;
    }

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    
    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            if (display_mode_ == kDisplayModeEyeOnly) {
                // 眼睛模式：只显示动画眼睛，不显示任何文字
                display->SetAnimatedEmotionMode(true);
                display->SetStatus("");
                display->SetChatMessage("system", "");
                display->SetEmotion("neutral");
            } else {
                // 默认模式：显示静态表情
                display->SetAnimatedEmotionMode(false);
                display->SetStatus(Lang::Strings::STANDBY);
                display->SetChatMessage("system", "");
                display->SetEmotion("neutral");
            }
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            if (display_mode_ == kDisplayModeEyeOnly) {
                display->SetStatus("");
                display->SetChatMessage("system", "");
                display->SetEmotion("neutral");
            } else {
                display->SetStatus(Lang::Strings::CONNECTING);
                display->SetEmotion("neutral");
                display->SetChatMessage("system", "");
            }
            break;
        case kDeviceStateListening:
            if (display_mode_ == kDisplayModeEyeOnly) {
                // 眼睛模式：显示专用的聆听表情
                display->SetStatus("");
                display->SetChatMessage("system", "");
                display->SetEmotion("listening");
            } else {
                // 默认模式：显示状态文字
                display->SetStatus(Lang::Strings::LISTENING);
                display->SetChatMessage("system", "");
                display->SetEmotion("neutral");
            }

            // Make sure the audio processor is running
            if (!audio_service_.IsAudioProcessorRunning()) {
                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            }

            // Play popup sound after ResetDecoder (in EnableVoiceProcessing) has been called
            if (play_popup_on_listening_) {
                play_popup_on_listening_ = false;
                audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            }
            break;
        case kDeviceStateSpeaking:
            if (display_mode_ == kDisplayModeEyeOnly) {
                // 眼睛模式：只显示动画眼睛
                display->SetStatus("");
                display->SetChatMessage("system", "");
                display->SetEmotion("happy");
            } else {
                // 默认模式：显示状态文字
                display->SetStatus(Lang::Strings::SPEAKING);
                display->SetChatMessage("system", "");
                display->SetEmotion("neutral");
            }
            // 清空聊天消息，在对话过程中不显示文字
            display->SetChatMessage("system", "");

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        case kDeviceStateWifiConfiguring:
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Schedule(std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(const std::string& url, const std::string& version) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    std::string upgrade_url = url;
    std::string version_info = version.empty() ? "(Manual upgrade)" : version;

    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    // 在动画表情模式下不显示聊天消息
    if (!display->IsAnimatedEmotionMode()) {
        display->SetChatMessage("system", message.c_str());
    }

    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = Ota::Upgrade(upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER); // Restore power save level
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        // Set flag to play popup sound after state changes to listening
        // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
        play_popup_on_listening_ = true;
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#endif
    } else if (state == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (state == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (GetDeviceState() != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    // Always schedule to run in main task for thread safety
    Schedule([this, payload = std::move(payload)]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

void Application::ResetProtocol() {
    Schedule([this]() {
        // Close audio channel if opened
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        // Reset protocol
        protocol_.reset();
    });
}


void Application::TriggerMotorEmotion(int emotion_type) {
    // Use the queue system for all motor actions to ensure they all execute
    switch (emotion_type) {
        case 1: // 非常短暂的向前（唤醒反馈）
            QueueMotorAction(4, motor_action_config_.default_speed_percent,
                           200, "Emotion forward twitch");
            break;
        case 2: // 非常短暂的向后（说话反馈）
            QueueMotorAction(2, motor_action_config_.default_speed_percent,
                           150, "Emotion backward twitch");
            break;
        case 3: // 左右快摆（顽皮 / 笑 / 眨眼）
            // Quick left-right wiggle
            QueueMotorAction(3, motor_action_config_.default_speed_percent,
                           motor_action_config_.wiggle_duration_ms / 6, "Emotion left wiggle");
            QueueMotorAction(1, motor_action_config_.default_speed_percent,
                           motor_action_config_.wiggle_duration_ms / 6, "Emotion right wiggle");
            break;
        case 4: // 轻点/点头（喜欢 / 自信）
            QueueMotorAction(4, motor_action_config_.default_speed_percent,
                           motor_action_config_.forward_duration_ms / 6, "Emotion nod forward");
            break;
        case 5: // 轻微倾斜/停顿（困惑 / 尴尬 / 思考）
            QueueMotorAction(3, motor_action_config_.default_speed_percent / 2,
                           motor_action_config_.left_turn_duration_ms / 4, "Emotion confused left");
            QueueMotorAction(1, motor_action_config_.default_speed_percent / 2,
                           motor_action_config_.right_turn_duration_ms / 4, "Emotion confused right");
            break;
        case 6: // 突然/强烈动作（惊讶 / 震惊 / 生气）
            QueueMotorAction(2, motor_action_config_.default_speed_percent,
                           motor_action_config_.backward_duration_ms / 6, "Emotion shocked back");
            QueueMotorAction(4, motor_action_config_.default_speed_percent,
                           motor_action_config_.forward_duration_ms / 6, "Emotion shocked forward");
            break;
        case 7: // 唤醒反馈：前进后退平衡（最高优先级）
            ESP_LOGI("Application", "唤醒电机反馈：前进后退平衡");
            QueueMotorAction(4, 40, 200, "Wake balance forward");
            QueueMotorAction(2, 40, 200, "Wake balance backward");
            break;
        case 8: // 开始说话反馈：向前（最高优先级）
            ESP_LOGI("Application", "开始说话电机反馈");
            QueueMotorAction(4, 50, 250, "Start speaking forward");
            break;
        case 9: // 结束说话反馈：向后（最高优先级）
            ESP_LOGI("Application", "结束说话电机反馈");
            QueueMotorAction(2, 45, 220, "End speaking backward");
            break;
        default:
            ESP_LOGW("Application", "未知的情感动作类型: %d", emotion_type);
            break;
    }
}

// Global function for motor control with duration (used by CompactWifiBoard)
// Priority levels: 0=low (emotion), 1=medium (speech), 2=high (MCP commands)
extern "C" void HandleMotorActionForApplication(int direction, int speed, int duration_ms, int priority) {
    // Get Application instance and handle motor action
    Application& app = Application::GetInstance();
    app.HandleMotorActionWithDuration(direction, speed, duration_ms, priority);
}

void Application::QueueMotorAction(int direction, int speed, int duration_ms, const std::string& description) {
    ESP_LOGI(TAG, "Queue motor action: %s (direction=%d, speed=%d, duration=%dms)",
             description.c_str(), direction, speed, duration_ms);

    // Add action to queue
    {
        std::lock_guard<std::mutex> lock(motor_queue_mutex_);
        motor_action_queue_.push({direction, speed, duration_ms, description});
    }

    // Start executor if not running
    if (!motor_executor_running_) {
        motor_executor_running_ = true;
        xTaskCreate([](void* param) {
            Application* app = static_cast<Application*>(param);
            app->ExecuteMotorActionQueue();
        }, "motor_executor", 4096, this, tskIDLE_PRIORITY + 1, NULL);
    }
}

void Application::ExecuteMotorActionQueue() {
    ESP_LOGI(TAG, "Motor action queue executor started");

    while (true) {
        MotorAction action;
        bool has_action = false;

        // Get next action from queue
        {
            std::lock_guard<std::mutex> lock(motor_queue_mutex_);
            if (!motor_action_queue_.empty()) {
                action = motor_action_queue_.front();
                motor_action_queue_.pop();
                has_action = true;
            }
        }

        if (has_action) {
            ESP_LOGI(TAG, "Executing queued motor action: %s", action.description.c_str());

            // Execute the action synchronously
            SetRealtimeMotorCommand(action.direction, action.speed);
            if (action.duration_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(action.duration_ms));
            }
            StopRealtimeMotorControl();

            // Small delay between actions
            vTaskDelay(pdMS_TO_TICKS(50));
        } else {
            // No more actions, check again after a short delay
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void Application::HandleMotorActionWithDuration(int direction, int speed, int duration_ms, int priority) {
    ESP_LOGI(TAG, "Motor action with duration: direction=%d, speed=%d, duration=%dms, priority=%d", direction, speed, duration_ms, priority);

    // Check if there's already a motor action in progress
    if (realtime_control_active_.load()) {
        int current_priority = current_motor_priority_.load();
        if (priority >= current_priority) {
            ESP_LOGW(TAG, "Higher/equal priority motor action (new:%d >= current:%d), canceling previous action", priority, current_priority);
            StopRealtimeMotorControl();
            // Give a small delay for the previous action to stop
            vTaskDelay(pdMS_TO_TICKS(50));
        } else {
            ESP_LOGW(TAG, "Lower priority motor action (new:%d < current:%d), queuing instead", priority, current_priority);
            // Queue the action to ensure it gets executed eventually
            std::string desc = "Queued action (dir=" + std::to_string(direction) +
                             ", speed=" + std::to_string(speed) +
                             ", duration=" + std::to_string(duration_ms) + "ms, pri=" + std::to_string(priority) + ")";
            QueueMotorAction(direction, speed, duration_ms, desc);
            return;
        }
    }

    // Set the current priority
    current_motor_priority_.store(priority);

    // Start the motor action
    SetRealtimeMotorCommand(direction, speed);

    // Schedule auto-stop after duration
    if (duration_ms > 0) {
        // Use a task to handle the delay and stop
        auto stop_task = [](void* params) -> void {
            auto* timing_params = static_cast<std::tuple<int, int, Application*>*>(params);
            auto [duration, priority, app] = *timing_params;
            delete timing_params;

            vTaskDelay(pdMS_TO_TICKS(duration));
            // Stop the motor control after the specified duration
            // This is a simple approach: just stop whatever is running
            app->StopRealtimeMotorControl();
            vTaskDelete(NULL);
        };

        auto* params = new std::tuple<int, int, Application*>(duration_ms, priority, this);
        xTaskCreate(stop_task, "motor_stop", 2048, params, tskIDLE_PRIORITY + 1, NULL);
    }
}

void Application::HandleWebMotorControl(int direction, int speed) {
    ESP_LOGI(TAG, "Web motor control: direction=%d, speed=%d", direction, speed);

    // 将网页控制转换为电机命令
    // direction: 0=停止, 1=右, 2=下(后退), 3=左, 4=上(前进)
    // speed: 0-100

    static int last_direction = 0;
    static int last_speed = 0;

    // 避免重复发送相同的命令
    if (direction == last_direction && speed == last_speed) {
        return;
    }

    last_direction = direction;
    last_speed = speed;

    // 如果速度为0或方向为0，停止实时控制并停止电机
    if (speed == 0 || direction == 0) {
        StopRealtimeMotorControl();
        return;
    }

    // 根据方向和速度计算电机控制命令
    // 我们使用现有的电机控制命令格式，但添加速度控制
    // 直接实时控制电机（绕过队列）
    SetRealtimeMotorCommand(direction, speed);
}

void Application::SetRealtimeMotorCommand(int direction, int speed) {
    ESP_LOGI(TAG, "SetRealtimeMotorCommand: direction=%d speed=%d", direction, speed);

    // 标记实时控制开启
    realtime_control_active_.store(true);

    // 更新时间戳（ms）
    last_realtime_command_ms_.store(esp_timer_get_time() / 1000);

    // 线程安全地初始化GPIO（仅第一次）
    {
        std::lock_guard<std::mutex> lock(motor_gpio_init_mutex_);
        if (!motor_gpio_initialized_member_) {
            gpio_config_t io_conf = {};
            io_conf.intr_type = GPIO_INTR_DISABLE;
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pin_bit_mask = (1ULL << MOTOR_LF_GPIO) | (1ULL << MOTOR_LB_GPIO) | (1ULL << MOTOR_RF_GPIO) | (1ULL << MOTOR_RB_GPIO);
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            if (gpio_config(&io_conf) == ESP_OK) {
                motor_gpio_initialized_member_ = true;
                ESP_LOGI(TAG, "实时控制: 电机GPIO初始化成功");
            } else {
                ESP_LOGE(TAG, "实时控制: 电机GPIO初始化失败");
                return;
            }
        }
    }

    // 初始化并使用 PWM（LEDC）控制占空比以实现速度控制
    if (!motor_pwm_initialized_member_) {
        InitMotorPwm();
    }

    if (motor_pwm_initialized_member_) {
        uint32_t max_duty = (1 << pwm_resolution_bits_) - 1;
        uint32_t duty = (speed * max_duty) / 100;

        // 使用 ledc fade 实现平滑过渡（模式 A：把目标通道设置为 PWM，其他通道为 0）
        // 先把所有通道设为 0（平滑）
        ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0, pwm_ramp_ms_);
        ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
        ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0, pwm_ramp_ms_);
        ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, LEDC_FADE_NO_WAIT);
        ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, 0, pwm_ramp_ms_);
        ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, LEDC_FADE_NO_WAIT);
        ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, 0, pwm_ramp_ms_);
        ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, LEDC_FADE_NO_WAIT);

        // 然后根据方向，把对应的通道设为目标占空比（平滑）
        switch (direction) {
            case 1: // 右: LF (ch0) + RB (ch3)
                ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty, pwm_ramp_ms_);
                ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
                ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, duty, pwm_ramp_ms_);
                ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, LEDC_FADE_NO_WAIT);
                break;
            case 2: // 后退: LB (ch1) + RB (ch3)
                ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty, pwm_ramp_ms_);
                ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, LEDC_FADE_NO_WAIT);
                ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, duty, pwm_ramp_ms_);
                ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, LEDC_FADE_NO_WAIT);
                break;
            case 3: // 左: LB (ch1) + RF (ch2)
                ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty, pwm_ramp_ms_);
                ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, LEDC_FADE_NO_WAIT);
                ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, duty, pwm_ramp_ms_);
                ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, LEDC_FADE_NO_WAIT);
                break;
            case 4: // 前进: LF (ch0) + RF (ch2)
                ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty, pwm_ramp_ms_);
                ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
                ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, duty, pwm_ramp_ms_);
                ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, LEDC_FADE_NO_WAIT);
                break;
            default:
                break;
        }
    } else {
        // 回退到 GPIO 控制（如果 PWM 未初始化）
        if (motor_gpio_initialized_member_) {
            gpio_set_level(MOTOR_LF_GPIO, 0);
            gpio_set_level(MOTOR_LB_GPIO, 0);
            gpio_set_level(MOTOR_RF_GPIO, 0);
            gpio_set_level(MOTOR_RB_GPIO, 0);

            switch (direction) {
                case 1:
                    gpio_set_level(MOTOR_LF_GPIO, 1);
                    gpio_set_level(MOTOR_RB_GPIO, 1);
                    break;
                case 2:
                    gpio_set_level(MOTOR_LB_GPIO, 1);
                    gpio_set_level(MOTOR_RB_GPIO, 1);
                    break;
                case 3:
                    gpio_set_level(MOTOR_LB_GPIO, 1);
                    gpio_set_level(MOTOR_RF_GPIO, 1);
                    break;
                case 4:
                    gpio_set_level(MOTOR_LF_GPIO, 1);
                    gpio_set_level(MOTOR_RF_GPIO, 1);
                    break;
                default:
                    break;
            }
        }
    }
}

void Application::StopRealtimeMotorControl() {
    ESP_LOGI(TAG, "StopRealtimeMotorControl");
    realtime_control_active_.store(false);
    current_motor_priority_.store(0); // Reset priority
    if (motor_pwm_initialized_member_) {
        // 平滑降到 0
        ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0, pwm_ramp_ms_);
        ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
        ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0, pwm_ramp_ms_);
        ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, LEDC_FADE_NO_WAIT);
        ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, 0, pwm_ramp_ms_);
        ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, LEDC_FADE_NO_WAIT);
        ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, 0, pwm_ramp_ms_);
        ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, LEDC_FADE_NO_WAIT);
    } else if (motor_gpio_initialized_member_) {
        gpio_set_level(MOTOR_LF_GPIO, 0);
        gpio_set_level(MOTOR_LB_GPIO, 0);
        gpio_set_level(MOTOR_RF_GPIO, 0);
        gpio_set_level(MOTOR_RB_GPIO, 0);
    }
    // Removed queue cleanup - now using unified PWM system
    // 重置时间戳
    last_realtime_command_ms_.store(0);
}

void Application::InitMotorPwm() {
    if (motor_pwm_initialized_member_) return;

    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.duty_resolution = (ledc_timer_bit_t)pwm_resolution_bits_;
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer.freq_hz = (uint32_t)pwm_freq_hz_;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&ledc_timer) != ESP_OK) {
        ESP_LOGE(TAG, "InitMotorPwm: ledc_timer_config failed");
        return;
    }

    // Channel configs: ch0 -> MOTOR_LF_GPIO, ch1 -> MOTOR_LB_GPIO, ch2 -> MOTOR_RF_GPIO, ch3 -> MOTOR_RB_GPIO
    // Configure each channel; zero-init struct to avoid uninitialized fields
    ledc_channel_config_t ch = {};
    ch.gpio_num = MOTOR_LF_GPIO;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = LEDC_CHANNEL_0;
    ch.timer_sel = LEDC_TIMER_0;
    ch.duty = 0;
    ch.hpoint = 0;
    if (ledc_channel_config(&ch) != ESP_OK) {
        ESP_LOGE(TAG, "InitMotorPwm: ledc_channel_config ch0 failed");
    }

    ch = {};
    ch.gpio_num = MOTOR_LB_GPIO;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = LEDC_CHANNEL_1;
    ch.timer_sel = LEDC_TIMER_0;
    ch.duty = 0;
    ch.hpoint = 0;
    if (ledc_channel_config(&ch) != ESP_OK) {
        ESP_LOGE(TAG, "InitMotorPwm: ledc_channel_config ch1 failed");
    }

    ch = {};
    ch.gpio_num = MOTOR_RF_GPIO;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = LEDC_CHANNEL_2;
    ch.timer_sel = LEDC_TIMER_0;
    ch.duty = 0;
    ch.hpoint = 0;
    if (ledc_channel_config(&ch) != ESP_OK) {
        ESP_LOGE(TAG, "InitMotorPwm: ledc_channel_config ch2 failed");
    }

    ch = {};
    ch.gpio_num = MOTOR_RB_GPIO;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = LEDC_CHANNEL_3;
    ch.timer_sel = LEDC_TIMER_0;
    ch.duty = 0;
    ch.hpoint = 0;
    if (ledc_channel_config(&ch) != ESP_OK) {
        ESP_LOGE(TAG, "InitMotorPwm: ledc_channel_config ch3 failed");
    }
    // Install fade service for smooth transitions
    if (ledc_fade_func_install(0) != ESP_OK) {
        ESP_LOGW(TAG, "InitMotorPwm: ledc_fade_func_install failed or already installed");
    }

    motor_pwm_initialized_member_ = true;
    ESP_LOGI(TAG, "InitMotorPwm: initialized (freq=%dHz, bits=%d, ramp=%dms)", pwm_freq_hz_, pwm_resolution_bits_, pwm_ramp_ms_);
}

// TriggerMotorEmotion 说明（中文）：
// 该函数将情感命令放入 motor_control_queue_，由 MotorControlTask 异步执行。
// 通过队列方式可以保证所有电机动作在同一任务中串行执行，避免并发冲突和阻塞主线程。

void Application::LoadMotorActionConfig() {
    Settings settings("motor_config", true);

    motor_action_config_.forward_duration_ms = settings.GetInt("forward_ms", 5000);
    motor_action_config_.backward_duration_ms = settings.GetInt("backward_ms", 5000);
    motor_action_config_.left_turn_duration_ms = settings.GetInt("left_turn_ms", 600);
    motor_action_config_.right_turn_duration_ms = settings.GetInt("right_turn_ms", 600);
    motor_action_config_.spin_duration_ms = settings.GetInt("spin_ms", 2500);
    motor_action_config_.wiggle_duration_ms = settings.GetInt("wiggle_ms", 600);
    motor_action_config_.dance_duration_ms = settings.GetInt("dance_ms", 1500);
    motor_action_config_.quick_forward_duration_ms = settings.GetInt("quick_fwd_ms", 5000);
    motor_action_config_.quick_backward_duration_ms = settings.GetInt("quick_bwd_ms", 5000);
    motor_action_config_.default_speed_percent = settings.GetInt("def_speed_pct", 100);

    ESP_LOGI(TAG, "加载电机动作配置:");
    ESP_LOGI(TAG, "  前进时间: %d ms", motor_action_config_.forward_duration_ms);
    ESP_LOGI(TAG, "  后退时间: %d ms", motor_action_config_.backward_duration_ms);
    ESP_LOGI(TAG, "  左转时间: %d ms", motor_action_config_.left_turn_duration_ms);
    ESP_LOGI(TAG, "  右转时间: %d ms", motor_action_config_.right_turn_duration_ms);
    ESP_LOGI(TAG, "  转圈时间: %d ms", motor_action_config_.spin_duration_ms);
    ESP_LOGI(TAG, "  摆动时间: %d ms", motor_action_config_.wiggle_duration_ms);
    ESP_LOGI(TAG, "  跳舞时间: %d ms", motor_action_config_.dance_duration_ms);
    ESP_LOGI(TAG, "  快速前进时间: %d ms", motor_action_config_.quick_forward_duration_ms);
    ESP_LOGI(TAG, "  快速后退时间: %d ms", motor_action_config_.quick_backward_duration_ms);
    ESP_LOGI(TAG, "  默认速度: %d%%", motor_action_config_.default_speed_percent);
}

void Application::SaveMotorActionConfig() {
    Settings settings("motor_config", true);

    settings.SetInt("forward_ms", motor_action_config_.forward_duration_ms);
    settings.SetInt("backward_ms", motor_action_config_.backward_duration_ms);
    settings.SetInt("left_turn_ms", motor_action_config_.left_turn_duration_ms);
    settings.SetInt("right_turn_ms", motor_action_config_.right_turn_duration_ms);
    settings.SetInt("spin_ms", motor_action_config_.spin_duration_ms);
    settings.SetInt("wiggle_ms", motor_action_config_.wiggle_duration_ms);
    settings.SetInt("dance_ms", motor_action_config_.dance_duration_ms);
    settings.SetInt("quick_fwd_ms", motor_action_config_.quick_forward_duration_ms);
    settings.SetInt("quick_bwd_ms", motor_action_config_.quick_backward_duration_ms);
    settings.SetInt("def_speed_pct", motor_action_config_.default_speed_percent);

    ESP_LOGI(TAG, "保存电机动作配置:");
    ESP_LOGI(TAG, "  前进时间: %d ms", motor_action_config_.forward_duration_ms);
    ESP_LOGI(TAG, "  后退时间: %d ms", motor_action_config_.backward_duration_ms);
    ESP_LOGI(TAG, "  左转时间: %d ms", motor_action_config_.left_turn_duration_ms);
    ESP_LOGI(TAG, "  右转时间: %d ms", motor_action_config_.right_turn_duration_ms);
    ESP_LOGI(TAG, "  转圈时间: %d ms", motor_action_config_.spin_duration_ms);
    ESP_LOGI(TAG, "  摆动时间: %d ms", motor_action_config_.wiggle_duration_ms);
    ESP_LOGI(TAG, "  跳舞时间: %d ms", motor_action_config_.dance_duration_ms);
    ESP_LOGI(TAG, "  快速前进时间: %d ms", motor_action_config_.quick_forward_duration_ms);
    ESP_LOGI(TAG, "  快速后退时间: %d ms", motor_action_config_.quick_backward_duration_ms);
    ESP_LOGI(TAG, "  默认速度: %d%%", motor_action_config_.default_speed_percent);
}

void Application::SetMotorActionConfig(const MotorActionConfig& config) {
    motor_action_config_ = config;
    SaveMotorActionConfig();
}
