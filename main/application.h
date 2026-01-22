#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <string>
#include <mutex>
#include <deque>
#include <memory>

#include "protocol.h"
#include "ota.h"
#include "audio_service.h"
#include "device_state.h"
#include "device_state_machine.h"
#include <atomic>
#include "web_server/web_server.h"

// Main event bits
#define MAIN_EVENT_SCHEDULE             (1 << 0)
#define MAIN_EVENT_SEND_AUDIO           (1 << 1)
#define MAIN_EVENT_WAKE_WORD_DETECTED   (1 << 2)
#define MAIN_EVENT_VAD_CHANGE           (1 << 3)
#define MAIN_EVENT_ERROR                (1 << 4)
#define MAIN_EVENT_ACTIVATION_DONE      (1 << 5)
#define MAIN_EVENT_CLOCK_TICK           (1 << 6)
#define MAIN_EVENT_NETWORK_CONNECTED    (1 << 7)
#define MAIN_EVENT_NETWORK_DISCONNECTED (1 << 8)
#define MAIN_EVENT_TOGGLE_CHAT          (1 << 9)
#define MAIN_EVENT_START_LISTENING      (1 << 10)
#define MAIN_EVENT_STOP_LISTENING       (1 << 11)
#define MAIN_EVENT_STATE_CHANGED        (1 << 12)


enum AecMode {
    kAecOff,
    kAecOnDeviceSide,
    kAecOnServerSide,
};

enum DisplayMode {
    kDisplayModeDefault,    // 默认模式：显示文字和表情
    kDisplayModeEyeOnly,    // 眼睛模式：只显示动画眼睛
};

class Application {
public:
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    // Delete copy constructor and assignment operator
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /**
     * Initialize the application
     * This sets up display, audio, network callbacks, etc.
     * Network connection starts asynchronously.
     */
    void Initialize();

    /**
     * Run the main event loop
     * This function runs in the main task and never returns.
     * It handles all events including network, state changes, and user interactions.
     */
    void Run();

    DeviceState GetDeviceState() const { return state_machine_.GetState(); }
    bool IsVoiceDetected() const { return audio_service_.IsVoiceDetected(); }

    // 显示模式相关方法
    DisplayMode GetDisplayMode() const { return display_mode_; }
    void SetDisplayMode(DisplayMode mode);
    void ToggleDisplayMode();
    
    /**
     * Request state transition
     * Returns true if transition was successful
     */
    bool SetDeviceState(DeviceState state);

    /**
     * Schedule a callback to be executed in the main task
     */
    void Schedule(std::function<void()>&& callback);

    /**
     * Alert with status, message, emotion and optional sound
     */
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");
    void DismissAlert();

    void AbortSpeaking(AbortReason reason);

    /**
     * Toggle chat state (event-based, thread-safe)
     * Sends MAIN_EVENT_TOGGLE_CHAT to be handled in Run()
     */
    void ToggleChatState();

    /**
     * Start listening (event-based, thread-safe)
     * Sends MAIN_EVENT_START_LISTENING to be handled in Run()
     */
    void StartListening();

    /**
     * Stop listening (event-based, thread-safe)
     * Sends MAIN_EVENT_STOP_LISTENING to be handled in Run()
     */
    void StopListening();

    void Reboot();
    void WakeWordInvoke(const std::string& wake_word);
    bool UpgradeFirmware(const std::string& url, const std::string& version = "");
    bool CanEnterSleepMode();
    void SendMcpMessage(const std::string& payload);
    void SetAecMode(AecMode mode);
    AecMode GetAecMode() const { return aec_mode_; }
    void PlaySound(const std::string_view& sound);
    AudioService& GetAudioService() { return audio_service_; }

    // Motor action configuration
    struct MotorActionConfig {
        int forward_duration_ms = 5000;
        int backward_duration_ms = 5000;
        int left_turn_duration_ms = 600;
        int right_turn_duration_ms = 600;
        int spin_duration_ms = 2500;
        int wiggle_duration_ms = 600;
        int dance_duration_ms = 1500;
        int quick_forward_duration_ms = 5000;
        int quick_backward_duration_ms = 5000;
        int default_speed_percent = 100;
    };

    void LoadMotorActionConfig();
    void SaveMotorActionConfig();
    const MotorActionConfig& GetMotorActionConfig() const { return motor_action_config_; }
    void SetMotorActionConfig(const MotorActionConfig& config);

    // Motor control
    void MotorControlTask();
    void TriggerMotorControl();
    void TriggerMotorEmotion(int emotion_type);
    void HandleWebMotorControl(int direction, int speed);
    void HandleMotorActionWithDuration(int direction, int speed, int duration_ms, int priority = 1);
    void QueueMotorAction(int direction, int speed, int duration_ms, const std::string& description);
    void ExecuteMotorActionQueue();

    /**
     * Reset protocol resources (thread-safe)
     * Can be called from any task to release resources allocated after network connected
     * This includes closing audio channel, resetting protocol and ota objects
     */
    void ResetProtocol();

private:
    Application();
    ~Application();

    std::mutex mutex_;
    std::deque<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    DeviceStateMachine state_machine_;
    ListeningMode listening_mode_ = kListeningModeAutoStop;
    AecMode aec_mode_ = kAecOff;
    DisplayMode display_mode_ = kDisplayModeDefault;
    std::string last_error_message_;
    AudioService audio_service_;
    std::unique_ptr<Ota> ota_;

    bool has_server_time_ = false;
    bool aborted_ = false;
    bool assets_version_checked_ = false;
    bool play_popup_on_listening_ = false;  // Flag to play popup sound after state changes to listening
    int clock_ticks_ = 0;
    TaskHandle_t activation_task_handle_ = nullptr;

    // Motor control task

    // Web server for remote control
    std::unique_ptr<WebServer> web_server_;

    // Motor action configuration
    MotorActionConfig motor_action_config_;

    // Real-time motor control support
    std::atomic<bool> realtime_control_active_{false};
    std::atomic<int> current_motor_priority_{0}; // Current motor action priority
    // Initialize motor gpio on demand in a thread-safe way
    std::mutex motor_gpio_init_mutex_;
    bool motor_gpio_initialized_member_ = false;

    // API for realtime control (bypass queue)
    void SetRealtimeMotorCommand(int direction, int speed);
    void StopRealtimeMotorControl();
    // Timestamp (ms) of last realtime command, used for watchdog to auto-stop
    std::atomic<int64_t> last_realtime_command_ms_{0};
    // PWM (LEDC) support
    bool motor_pwm_initialized_member_ = false;
    int pwm_freq_hz_ = 20000;
    int pwm_resolution_bits_ = 10;
    void InitMotorPwm();
    // Ramp (ms) for PWM fade
    int pwm_ramp_ms_ = 50;


    // Event handlers
    void HandleStateChangedEvent();
    void HandleToggleChatEvent();
    void HandleStartListeningEvent();
    void HandleStopListeningEvent();
    void HandleNetworkConnectedEvent();
    void HandleNetworkDisconnectedEvent();
    void HandleActivationDoneEvent();
    void HandleWakeWordDetectedEvent();

    // Activation task (runs in background)
    void ActivationTask();

    // Helper methods
    void CheckAssetsVersion();
    void CheckNewVersion();
    void InitializeProtocol();
    void ShowActivationCode(const std::string& code, const std::string& message);
    void SetListeningMode(ListeningMode mode);
    
    // State change handler called by state machine
    void OnStateChanged(DeviceState old_state, DeviceState new_state);
};


class TaskPriorityReset {
public:
    TaskPriorityReset(BaseType_t priority) {
        original_priority_ = uxTaskPriorityGet(NULL);
        vTaskPrioritySet(NULL, priority);
    }
    ~TaskPriorityReset() {
        vTaskPrioritySet(NULL, original_priority_);
    }

private:
    BaseType_t original_priority_;
};

#endif // _APPLICATION_H_
