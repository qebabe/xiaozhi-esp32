#include "web_server.h"
#include <esp_log.h>
#include <cstring>
#include <cJSON.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>

// Forward declarations for motor control functions
extern "C" void HandleMotorActionForApplication(int direction, int speed, int duration_ms, int priority);
extern "C" void HandleMotorActionForEmotion(const char* emotion);
extern "C" void HandleMotorActionForDance(uint8_t speed_percent);

static const char *TAG = "WebServer";

WebServer::WebServer()
    : server_handle_(nullptr) {
}

WebServer::~WebServer() {
    Stop();
}

bool WebServer::Start(int port) {
    ESP_LOGI(TAG, "Starting web server on port %d", port);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 10;
    // 增加超时设置以更好地处理频繁请求
    config.recv_wait_timeout = 5;  // 接收超时5秒
    config.send_wait_timeout = 5;  // 发送超时5秒
    config.max_resp_headers = 8;   // 减少响应头数量

    if (httpd_start(&server_handle_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return false;
    }

    // 注册URI处理器
    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_get_handler,
        .user_ctx  = this
    };
    httpd_register_uri_handler(server_handle_, &index_uri);

    httpd_uri_t control_uri = {
        .uri       = "/control",
        .method    = HTTP_POST,
        .handler   = control_post_handler,
        .user_ctx  = this
    };
    httpd_register_uri_handler(server_handle_, &control_uri);

    httpd_uri_t api_control_uri = {
        .uri       = "/api/control",
        .method    = HTTP_POST,
        .handler   = api_control_handler,
        .user_ctx  = this
    };
    httpd_register_uri_handler(server_handle_, &api_control_uri);

    httpd_uri_t api_motor_action_uri = {
        .uri       = "/api/motor/action",
        .method    = HTTP_POST,
        .handler   = api_motor_action_handler,
        .user_ctx  = this
    };
    httpd_register_uri_handler(server_handle_, &api_motor_action_uri);

    httpd_uri_t debug_uri = {
        .uri       = "/api/debug/motor_test",
        .method    = HTTP_POST,
        .handler   = debug_motor_test_handler,
        .user_ctx  = this
    };
    httpd_register_uri_handler(server_handle_, &debug_uri);

    // 注册配置页面处理器
    httpd_uri_t config_uri = {
        .uri       = "/config",
        .method    = HTTP_GET,
        .handler   = config_get_handler,
        .user_ctx  = this
    };
    httpd_register_uri_handler(server_handle_, &config_uri);

    httpd_uri_t config_post_uri = {
        .uri       = "/config",
        .method    = HTTP_POST,
        .handler   = config_post_handler,
        .user_ctx  = this
    };
    httpd_register_uri_handler(server_handle_, &config_post_uri);

    httpd_uri_t api_config_get_uri = {
        .uri       = "/api/config",
        .method    = HTTP_GET,
        .handler   = api_config_get_handler,
        .user_ctx  = this
    };
    httpd_register_uri_handler(server_handle_, &api_config_get_uri);

    httpd_uri_t api_config_post_uri = {
        .uri       = "/api/config",
        .method    = HTTP_POST,
        .handler   = api_config_post_handler,
        .user_ctx  = this
    };
    httpd_register_uri_handler(server_handle_, &api_config_post_uri);

    ESP_LOGI(TAG, "Web server started successfully");
    return true;
}

void WebServer::Stop() {
    if (server_handle_) {
        httpd_stop(server_handle_);
        server_handle_ = nullptr;
        ESP_LOGI(TAG, "Web server stopped");
    }
}

void WebServer::SetMotorControlCallback(std::function<void(int direction, int speed)> callback) {
    motor_control_callback_ = callback;
}

void WebServer::InvokeMotorControl(int direction, int speed) {
    if (motor_control_callback_) {
        motor_control_callback_(direction, speed);
    }
}

void WebServer::SetMotorActionConfigCallback(std::function<MotorActionConfig()> get_callback,
                                           std::function<void(const MotorActionConfig&)> set_callback) {
    get_motor_config_callback_ = get_callback;
    set_motor_config_callback_ = set_callback;
}

void WebServer::SetEmotionCallback(std::function<void(const char* emotion)> callback) {
    emotion_callback_ = callback;
}

void WebServer::SetEmotion(const char* emotion) {
    if (emotion_callback_) {
        emotion_callback_(emotion);
    }
}

esp_err_t WebServer::index_get_handler(httpd_req_t *req) {
    WebServer* server = (WebServer*)req->user_ctx;

    // 设置CORS头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, server->get_html_page(), HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

esp_err_t WebServer::control_post_handler(httpd_req_t *req) {
    WebServer* server = (WebServer*)req->user_ctx;

    // 设置CORS头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    char content[100];
    int ret = httpd_req_recv(req, content, sizeof(content));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }

    int direction, speed;
    server->parse_simple_control_command(content, direction, speed);

    // 调用电机控制回调（通过公共封装）
    server->InvokeMotorControl(direction, speed);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

esp_err_t WebServer::api_control_handler(httpd_req_t *req) {
    WebServer* server = (WebServer*)req->user_ctx;

    // 设置CORS头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    char content[200];
    int ret = httpd_req_recv(req, content, sizeof(content));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }

    // 解析JSON数据
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    int direction = cJSON_GetObjectItem(root, "direction")->valueint;
    int speed = cJSON_GetObjectItem(root, "speed")->valueint;

    cJSON_Delete(root);

    // 调用电机控制回调（通过公共封装）
    server->InvokeMotorControl(direction, speed);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// Simple struct passed to the stop task
struct debug_stop_param_t {
    WebServer* server;
    int duration_ms;
};

static void debug_stop_task(void* arg) {
    debug_stop_param_t* p = static_cast<debug_stop_param_t*>(arg);
    if (!p) vTaskDelete(NULL);
    vTaskDelay(pdMS_TO_TICKS(p->duration_ms));
    if (p->server) {
        p->server->InvokeMotorControl(0, 0); // stop via public wrapper
    }
    free(p);
    vTaskDelete(NULL);
}

esp_err_t WebServer::debug_motor_test_handler(httpd_req_t *req) {
    WebServer* server = (WebServer*)req->user_ctx;

    // 设置CORS头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(content);
    int direction = 4;
    int speed = 80;
    int duration = 1000;
    if (root) {
        cJSON* jdir = cJSON_GetObjectItem(root, "direction");
        cJSON* jspd = cJSON_GetObjectItem(root, "speed");
        cJSON* jdur = cJSON_GetObjectItem(root, "duration");
        if (cJSON_IsNumber(jdir)) direction = jdir->valueint;
        if (cJSON_IsNumber(jspd)) speed = jspd->valueint;
        if (cJSON_IsNumber(jdur)) duration = jdur->valueint;
        cJSON_Delete(root);
    }

    if (server) {
        server->InvokeMotorControl(direction, speed);
        // spawn a short-lived task to stop after duration ms
        debug_stop_param_t* p = (debug_stop_param_t*)malloc(sizeof(debug_stop_param_t));
        if (p) {
            p->server = server;
            p->duration_ms = duration;
            BaseType_t rc = xTaskCreate(debug_stop_task, "dbg_stop", 2048, p, tskIDLE_PRIORITY + 1, NULL);
            if (rc != pdPASS) {
                free(p);
            }
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t WebServer::api_motor_action_handler(httpd_req_t *req) {
    WebServer* server = (WebServer*)req->user_ctx;

    // 设置CORS头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON* action_json = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action_json)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid action parameter");
        return ESP_FAIL;
    }

    std::string action = action_json->valuestring;

    // Get motor configuration for durations
    MotorActionConfig config;
    if (server->get_motor_config_callback_) {
        config = server->get_motor_config_callback_();
    }

    // Call motor action functions directly (simulating MCP tool calls) with configured durations
    try {
        ESP_LOGI(TAG, "网页动作请求: %s", action.c_str());

        if (action == "move_forward") {
            ESP_LOGI(TAG, "执行前进动作 - 速度:%d%%, 持续时间:%d ms", config.default_speed_percent, config.forward_duration_ms);
            HandleMotorActionForApplication(4, config.default_speed_percent, config.forward_duration_ms, 1);
        } else if (action == "move_backward") {
            ESP_LOGI(TAG, "执行后退动作 - 速度:%d%%, 持续时间:%d ms", config.default_speed_percent, config.backward_duration_ms);
            HandleMotorActionForApplication(2, config.default_speed_percent, config.backward_duration_ms, 1);
        } else if (action == "spin_around") {
            ESP_LOGI(TAG, "执行转圈动作 - 速度:%d%%, 持续时间:%d ms", config.default_speed_percent, config.spin_duration_ms);
            HandleMotorActionForApplication(3, config.default_speed_percent, config.spin_duration_ms, 1);
        } else if (action == "turn_left") {
            ESP_LOGI(TAG, "执行左转动作 - 速度:%d%%, 持续时间:%d ms", config.default_speed_percent, config.left_turn_duration_ms);
            HandleMotorActionForApplication(3, config.default_speed_percent, config.left_turn_duration_ms, 1);
        } else if (action == "turn_right") {
            ESP_LOGI(TAG, "执行右转动作 - 速度:%d%%, 持续时间:%d ms", config.default_speed_percent, config.right_turn_duration_ms);
            HandleMotorActionForApplication(1, config.default_speed_percent, config.right_turn_duration_ms, 1);
        } else if (action == "quick_forward") {
            ESP_LOGI(TAG, "执行快速前进动作 - 速度:%d%%, 持续时间:%d ms", config.default_speed_percent, config.quick_forward_duration_ms);
            HandleMotorActionForApplication(4, config.default_speed_percent, config.quick_forward_duration_ms, 1);
        } else if (action == "quick_backward") {
            ESP_LOGI(TAG, "执行快速后退动作 - 速度:%d%%, 持续时间:%d ms", config.default_speed_percent, config.quick_backward_duration_ms);
            HandleMotorActionForApplication(2, config.default_speed_percent, config.quick_backward_duration_ms, 1);
        } else if (action == "wiggle") {
            ESP_LOGI(TAG, "执行摆动动作 (情感:困惑)");
            server->SetEmotion("confused");
            HandleMotorActionForEmotion("confused");
        } else if (action == "dance") {
            ESP_LOGI(TAG, "执行跳舞动作 - 速度:%d%%", config.default_speed_percent);
            server->SetEmotion("excited");
            HandleMotorActionForDance(config.default_speed_percent);
        } else if (action == "stop") {
            ESP_LOGI(TAG, "执行停止动作");
            HandleMotorActionForApplication(0, 0, 0, 2); // stop, high priority
        } else if (action == "wake_up") {
            ESP_LOGI(TAG, "执行唤醒情感动作");
            server->SetEmotion("sleepy");  // 原版特殊动画：困倦效果
            HandleMotorActionForEmotion("wake");
        } else if (action == "happy") {
            ESP_LOGI(TAG, "执行开心情感动作");
            server->SetEmotion("laughing");  // 原版特殊动画：大笑动画
            HandleMotorActionForEmotion("happy");
        } else if (action == "sad") {
            ESP_LOGI(TAG, "执行悲伤情感动作");
            server->SetEmotion("crying");  // 原版特殊动画：哭泣动画
            HandleMotorActionForEmotion("sad");
        } else if (action == "thinking") {
            ESP_LOGI(TAG, "执行思考情感动作");
            server->SetEmotion("thinking");  // 原版特殊动画：思考动画
            HandleMotorActionForEmotion("thinking");
        } else if (action == "listening") {
            ESP_LOGI(TAG, "执行倾听情感动作");
            server->SetEmotion("wink");  // 原版特殊动画：眨眼动画
            HandleMotorActionForEmotion("listening");
        } else if (action == "speaking") {
            ESP_LOGI(TAG, "执行说话情感动作");
            server->SetEmotion("funny");  // 原版特殊动画：搞笑动画
            HandleMotorActionForEmotion("speaking");
        } else if (action == "excited") {
            ESP_LOGI(TAG, "执行兴奋情感动作");
            server->SetEmotion("shocked");  // 原版特殊动画：震惊动画
            HandleMotorActionForEmotion("excited");
        } else if (action == "loving") {
            ESP_LOGI(TAG, "执行爱慕情感动作");
            server->SetEmotion("kissy");  // 原版特殊动画：亲吻动画
            HandleMotorActionForEmotion("loving");
        } else if (action == "angry") {
            ESP_LOGI(TAG, "执行生气情感动作");
            server->SetEmotion("angry");  // 基础表情（生气）
            HandleMotorActionForEmotion("angry");
        } else if (action == "surprised") {
            ESP_LOGI(TAG, "执行惊讶情感动作");
            server->SetEmotion("surprised");  // 基础表情（惊讶）
            HandleMotorActionForEmotion("surprised");
        } else if (action == "confused") {
            ESP_LOGI(TAG, "执行困惑情感动作");
            server->SetEmotion("confused");  // 原版特殊动画：困惑动画
            HandleMotorActionForEmotion("confused");
        } else {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown action");
            return ESP_FAIL;
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Failed to execute motor action %s: %s", action.c_str(), e.what());
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to execute action");
        return ESP_FAIL;
    }

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void WebServer::parse_simple_control_command(const char* data, int& direction, int& speed) {
    // 简单解析格式：direction=X,speed=Y
    char* str = strdup(data);
    char* token = strtok(str, ",");

    direction = 0;
    speed = 0;

    while (token != NULL) {
        if (strstr(token, "direction=")) {
            direction = atoi(token + 10);
        } else if (strstr(token, "speed=")) {
            speed = atoi(token + 6);
        }
        token = strtok(NULL, ",");
    }

    free(str);
}

const char* WebServer::get_html_page() {
    static const char html_page[] = R"html(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>小智小车遥控器</title>
    <style>
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            margin: 0;
            padding: 20px;
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
        }

        .container {
            background: rgba(255, 255, 255, 0.95);
            border-radius: 20px;
            padding: 30px;
            box-shadow: 0 20px 40px rgba(0,0,0,0.1);
            text-align: center;
            max-width: 400px;
            width: 100%;
        }

        h1 {
            color: #333;
            margin-bottom: 10px;
            font-size: 2.2em;
        }

        .subtitle {
            color: #666;
            margin-bottom: 30px;
            font-size: 1.1em;
        }

        .joystick-container {
            position: relative;
            width: 400px;
            height: 400px;
            margin: 0 auto 30px;
            border-radius: 50%;
            background: #f0f0f0;
            border: 3px solid #ddd;
            touch-action: none;
        }

        .joystick {
            position: absolute;
            width: 112px;
            height: 112px;
            background: linear-gradient(135deg, #4CAF50, #45a049);
            border-radius: 50%;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            box-shadow: 0 4px 8px rgba(0,0,0,0.2);
            transition: all 0.1s ease;
            cursor: pointer;
        }

        .joystick.active {
            background: linear-gradient(135deg, #2196F3, #1976D2);
            transform: translate(-50%, -50%) scale(0.95);
        }

        .direction-indicator {
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            font-size: 18px;
            font-weight: bold;
            color: #333;
            pointer-events: none;
            transition: opacity 0.3s ease;
        }

        .direction-indicator.active {
            opacity: 1;
        }

        .status {
            margin-top: 20px;
            padding: 10px;
            border-radius: 10px;
            background: #f8f9fa;
            border: 1px solid #e9ecef;
        }

        .status.connected {
            background: #d4edda;
            border-color: #c3e6cb;
            color: #155724;
        }

        .status.disconnected {
            background: #f8d7da;
            border-color: #f5c6cb;
            color: #721c24;
        }

        .controls {
            margin-top: 20px;
        }

        .control-btn {
            background: linear-gradient(135deg, #FF6B6B, #EE5A24);
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 25px;
            font-size: 16px;
            cursor: pointer;
            margin: 5px;
            transition: all 0.3s ease;
            box-shadow: 0 4px 15px rgba(255, 107, 107, 0.3);
        }

        .control-btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(255, 107, 107, 0.4);
        }

        .control-btn:active {
            transform: translateY(0);
        }

        .stop-btn {
            background: linear-gradient(135deg, #DC3545, #C82333);
        }

        .stop-btn:hover {
            box-shadow: 0 6px 20px rgba(220, 53, 69, 0.4);
        }

        /* 动作控制区域样式 */
        .actions-section {
            margin-top: 30px;
            padding: 20px;
            background: rgba(255, 255, 255, 0.9);
            border-radius: 15px;
            border: 1px solid #e9ecef;
        }

        .action-buttons {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 20px;
        }

        .action-group {
            background: #f8f9fa;
            padding: 15px;
            border-radius: 10px;
            border: 1px solid #dee2e6;
        }

        .action-group h4 {
            margin: 0 0 15px 0;
            color: #495057;
            font-size: 1.1em;
            text-align: center;
            border-bottom: 2px solid #e9ecef;
            padding-bottom: 8px;
        }

        .action-btn {
            background: linear-gradient(135deg, #28a745, #20c997);
            color: white;
            border: none;
            padding: 10px 15px;
            border-radius: 8px;
            font-size: 14px;
            cursor: pointer;
            margin: 5px;
            transition: all 0.3s ease;
            box-shadow: 0 2px 8px rgba(40, 167, 69, 0.2);
            min-width: 100px;
        }

        .action-btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 12px rgba(40, 167, 69, 0.3);
        }

        .action-btn:active {
            transform: translateY(0);
        }

        @media (max-width: 480px) {
            .container {
                padding: 20px;
                margin: 10px;
            }

            .joystick-container {
                width: 320px;
                height: 320px;
            }

            .joystick {
                width: 96px;
                height: 96px;
            }

            h1 {
                font-size: 1.8em;
            }

            .action-buttons {
                grid-template-columns: 1fr;
                gap: 15px;
            }

            .action-btn {
                font-size: 13px;
                padding: 8px 12px;
                min-width: 80px;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🚗 小智小车遥控器</h1>
        <div class="subtitle">触摸或拖拽摇杆控制小车移动</div>

        <div class="joystick-container" id="joystick-container">
            <div class="joystick" id="joystick"></div>
            <div class="direction-indicator" id="direction-indicator">⏹️</div>
        </div>

        <div class="status connected" id="status">
            <strong>状态:</strong> <span id="status-text">已连接</span>
        </div>

        <div class="controls">
            <button class="control-btn stop-btn" onclick="stopCar()">🛑 停止</button>
            <a href="/config" class="control-btn" style="background: linear-gradient(135deg, #FF9800, #F57C00);">⚙️ 配置</a>
        </div>

        <div class="actions-section">
            <h3 style="color: #333; margin: 20px 0 15px 0; text-align: center;">🎭 动作控制</h3>

            <div class="action-buttons">
                <!-- 基本移动动作 -->
                <div class="action-group">
                    <h4>🚗 基本移动</h4>
                    <button class="action-btn" onclick="executeAction('move_forward')">⬆️ 前进</button>
                    <button class="action-btn" onclick="executeAction('move_backward')">⬇️ 后退</button>
                    <button class="action-btn" onclick="executeAction('turn_left')">⬅️ 左转</button>
                    <button class="action-btn" onclick="executeAction('turn_right')">➡️ 右转</button>
                    <button class="action-btn" onclick="executeAction('spin_around')">🔄 转圈</button>
                </div>

                <!-- 情感动作 -->
                <div class="action-group">
                    <h4>😊 情感表达</h4>
                    <button class="action-btn" onclick="executeAction('wake_up')">🌅 唤醒</button>
                    <button class="action-btn" onclick="executeAction('happy')">😄 开心</button>
                    <button class="action-btn" onclick="executeAction('sad')">😢 悲伤</button>
                    <button class="action-btn" onclick="executeAction('thinking')">🤔 思考</button>
                    <button class="action-btn" onclick="executeAction('listening')">👂 倾听</button>
                    <button class="action-btn" onclick="executeAction('speaking')">💬 说话</button>
                    <button class="action-btn" onclick="executeAction('wiggle')">🌊 摆动</button>
                    <button class="action-btn" onclick="executeAction('dance')">💃 跳舞</button>
                </div>

                <!-- 高级情感 -->
                <div class="action-group">
                    <h4>🎭 高级情感</h4>
                    <button class="action-btn" onclick="executeAction('excited')">🤩 兴奋</button>
                    <button class="action-btn" onclick="executeAction('loving')">😍 爱慕</button>
                    <button class="action-btn" onclick="executeAction('angry')">😠 生气</button>
                    <button class="action-btn" onclick="executeAction('surprised')">😲 惊讶</button>
                    <button class="action-btn" onclick="executeAction('confused')">😕 困惑</button>
                </div>
            </div>
        </div>
    </div>

    <script>
        let joystick = document.getElementById('joystick');
        let joystickContainer = document.getElementById('joystick-container');
        let directionIndicator = document.getElementById('direction-indicator');
        let statusText = document.getElementById('status-text');

        let isDragging = false;
        let centerX = 0;
        let centerY = 0;
        let currentDirection = 0;
        let currentSpeed = 0;
        let isRequestPending = false; // 防止并发请求


        // 初始化摇杆中心位置
        function initJoystick() {
            const rect = joystickContainer.getBoundingClientRect();
            centerX = rect.left + rect.width / 2;
            centerY = rect.top + rect.height / 2;
        }

        // 更新摇杆位置
        function updateJoystickPosition(x, y) {
            const rect = joystickContainer.getBoundingClientRect();
            const containerCenterX = rect.left + rect.width / 2;
            const containerCenterY = rect.top + rect.height / 2;

            // 计算相对于容器的位置
            let relativeX = x - containerCenterX;
            let relativeY = y - containerCenterY;

            // 限制在圆形范围内
            const maxRadius = rect.width / 2 - 56;
            const distance = Math.sqrt(relativeX * relativeX + relativeY * relativeY);

            if (distance > maxRadius) {
                relativeX = (relativeX / distance) * maxRadius;
                relativeY = (relativeY / distance) * maxRadius;
            }

            // 更新摇杆位置
            joystick.style.left = `calc(50% + ${relativeX}px)`;
            joystick.style.top = `calc(50% + ${relativeY}px)`;

            // 计算方向和速度
            const normalizedX = relativeX / maxRadius;
            const normalizedY = relativeY / maxRadius;

            // 计算方向角度 (0-360度)
            let angle = Math.atan2(normalizedY, normalizedX) * (180 / Math.PI);
            if (angle < 0) angle += 360;

            // 计算速度 (0-100)
            const speed = Math.min(distance / maxRadius, 1) * 100;

            // 转换方向为整数值
            let direction = 0; // 停止
            if (speed > 5) { // 最小阈值 (降低阈值以响应点击)
                if (angle >= 315 || angle < 45) {
                    direction = 1; // 右
                } else if (angle >= 45 && angle < 135) {
                    direction = 2; // 下
                } else if (angle >= 135 && angle < 225) {
                    direction = 3; // 左
                } else if (angle >= 225 && angle < 315) {
                    direction = 4; // 上
                }
            }

            return { direction, speed: Math.round(speed) };
        }

        // 更新方向指示器
        function updateDirectionIndicator(direction, speed) {
            let icon = '⏹️';
            let text = '停止';

            if (speed > 5) {
                switch(direction) {
                    case 1: icon = '➡️'; text = '右转'; break;
                    case 2: icon = '⬇️'; text = '后退'; break;
                    case 3: icon = '⬅️'; text = '左转'; break;
                    case 4: icon = '⬆️'; text = '前进'; break;
                }
            }

            directionIndicator.textContent = icon;
            directionIndicator.classList.toggle('active', speed > 5);
        }

        // 发送控制命令
        // 发送控制命令
        async function sendControl(direction, speed) {
            // 停止命令(0, 0)优先处理，不受并发限制
            if (direction === 0 && speed === 0) {
                currentDirection = 0;
                currentSpeed = 0;
                try {
                    const response = await fetch('/api/control', {
                        method: 'POST',
                        headers: {
                            'Content-Type': 'application/json',
                        },
                        body: JSON.stringify({
                            direction: 0,
                            speed: 0
                        })
                    });
                    if (!response.ok) {
                        throw new Error('Network response was not ok');
                    }
                    statusText.textContent = '已连接';
                    document.getElementById('status').className = 'status connected';
                } catch (error) {
                    console.error('Failed to send stop control:', error);
                    statusText.textContent = '连接错误';
                    document.getElementById('status').className = 'status error';
                }
                return;
            }

            if (direction === currentDirection && speed === currentSpeed) {
                return; // 避免重复发送相同命令
            }

            // 如果有请求正在进行中，跳过
            if (isRequestPending) {
                return;
            }

            currentDirection = direction;
            currentSpeed = speed;
            isRequestPending = true;

            try {
                const response = await fetch('/api/control', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        direction: direction,
                        speed: speed
                    })
                });

                if (!response.ok) {
                    throw new Error('Network response was not ok');
                }

                statusText.textContent = '已连接';
                document.getElementById('status').className = 'status connected';
            } catch (error) {
                console.error('Failed to send control:', error);
                statusText.textContent = '连接错误';
                document.getElementById('status').className = 'status error';
            } finally {
                isRequestPending = false;
            }
        }

        // 停止小车
        function stopCar() {
            // 重置状态
            isDragging = false;
            currentDirection = 0;
            currentSpeed = 0;

            // 重置UI
            joystick.style.left = '50%';
            joystick.style.top = '50%';
            joystick.classList.remove('active');
            updateDirectionIndicator(0, 0);

            // 发送停止命令
            sendControl(0, 0);
        }


        // 执行电机动作
        async function executeAction(action) {
            try {
                const response = await fetch('/api/motor/action', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        action: action
                    })
                });

                if (!response.ok) {
                    throw new Error('Network response was not ok');
                }

                const result = await response.json();
                console.log('Action executed:', action, result);

                // 更新状态显示
                statusText.textContent = '动作执行成功';
                document.getElementById('status').className = 'status connected';

            } catch (error) {
                console.error('Failed to execute action:', action, error);
                statusText.textContent = '动作执行失败';
                document.getElementById('status').className = 'status disconnected';
            }
        }

        // 鼠标事件
        joystickContainer.addEventListener('mousedown', (e) => {
            isDragging = true;
            joystick.classList.add('active');
            initJoystick();
            const { direction, speed } = updateJoystickPosition(e.clientX, e.clientY);
            updateDirectionIndicator(direction, speed);
            sendControl(direction, speed);
        });

        document.addEventListener('mousemove', (e) => {
            if (isDragging) {
                const { direction, speed } = updateJoystickPosition(e.clientX, e.clientY);
                updateDirectionIndicator(direction, speed);
                sendControl(direction, speed);
            }
        });

        document.addEventListener('mouseup', () => {
            if (isDragging) {
                stopCar();
            }
        });

        // 触摸事件
        joystickContainer.addEventListener('touchstart', (e) => {
            e.preventDefault();
            isDragging = true;
            joystick.classList.add('active');
            initJoystick();
            const touch = e.touches[0];
            const { direction, speed } = updateJoystickPosition(touch.clientX, touch.clientY);
            updateDirectionIndicator(direction, speed);
            sendControl(direction, speed);
        });

        joystickContainer.addEventListener('touchmove', (e) => {
            e.preventDefault();
            if (isDragging) {
                const touch = e.touches[0];
                const { direction, speed } = updateJoystickPosition(touch.clientX, touch.clientY);
                updateDirectionIndicator(direction, speed);
                sendControl(direction, speed);
            }
        });

        joystickContainer.addEventListener('touchend', (e) => {
            e.preventDefault();
            if (isDragging) {
                stopCar();
            }
        });

        // 全局触摸结束事件，确保在任何地方松手都能停止
        document.addEventListener('touchend', (e) => {
            if (isDragging && e.target !== joystick && e.target !== joystickContainer) {
                stopCar();
            }
        });

        // 定期发送控制命令（当摇杆被拖拽时）
        setInterval(() => {
            if (isDragging) {
                sendControl(currentDirection, currentSpeed);
            }
        }, 200); // 每200ms发送一次，减少服务器压力

        // 初始化
        initJoystick();
        window.addEventListener('resize', initJoystick);
    </script>
</body>
</html>
)html";

    return html_page;
}


// 解析JSON控制命令
void WebServer::parse_json_control_command(const char* data, int& direction, int& speed) {
    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        direction = 0;
        speed = 0;
        return;
    }

    cJSON *j_direction = cJSON_GetObjectItem(json, "direction");
    cJSON *j_speed = cJSON_GetObjectItem(json, "speed");

    if (cJSON_IsNumber(j_direction) && cJSON_IsNumber(j_speed)) {
        direction = j_direction->valueint;
        speed = j_speed->valueint;
    } else {
        direction = 0;
        speed = 0;
    }

    cJSON_Delete(json);
}

// 配置页面处理器
esp_err_t WebServer::config_get_handler(httpd_req_t *req) {
    WebServer* server = (WebServer*)req->user_ctx;

    // 设置CORS头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, server->get_config_html_page(), HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// 配置页面POST处理器
esp_err_t WebServer::config_post_handler(httpd_req_t *req) {
    WebServer* server = (WebServer*)req->user_ctx;

    // 设置CORS头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }

    // 解析表单数据
    MotorActionConfig config;
    server->parse_config_form_data(content, config);

    // 保存配置
    if (server->set_motor_config_callback_) {
        server->set_motor_config_callback_(config);
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, R"html(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>配置已保存</title>
    <style>
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            margin: 0;
            padding: 20px;
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
        }

        .container {
            background: rgba(255, 255, 255, 0.95);
            border-radius: 20px;
            padding: 40px;
            box-shadow: 0 20px 40px rgba(0,0,0,0.1);
            text-align: center;
            max-width: 500px;
            width: 100%;
        }

        .success-icon {
            font-size: 4em;
            margin-bottom: 20px;
        }

        h1 {
            color: #333;
            margin-bottom: 10px;
            font-size: 2.2em;
        }

        .message {
            color: #666;
            margin-bottom: 30px;
            font-size: 1.1em;
        }

        .buttons {
            display: flex;
            gap: 15px;
            justify-content: center;
            flex-wrap: wrap;
        }

        .btn {
            background: linear-gradient(135deg, #4CAF50, #45a049);
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 25px;
            font-size: 16px;
            cursor: pointer;
            text-decoration: none;
            display: inline-block;
            transition: all 0.3s ease;
            box-shadow: 0 4px 15px rgba(76, 175, 80, 0.3);
        }

        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(76, 175, 80, 0.4);
        }

        .btn.secondary {
            background: linear-gradient(135deg, #2196F3, #1976D2);
        }

        .btn.secondary:hover {
            box-shadow: 0 6px 20px rgba(33, 150, 243, 0.4);
        }

        @media (max-width: 480px) {
            .container {
                padding: 30px 20px;
            }

            .buttons {
                flex-direction: column;
                align-items: center;
            }

            .btn {
                width: 200px;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="success-icon">✅</div>
        <h1>配置已保存！</h1>
        <div class="message">您的电机动作配置已成功保存到设备中。</div>

        <div class="buttons">
            <a href="/config" class="btn secondary">⚙️ 返回配置页面</a>
            <a href="/" class="btn">🏠 返回遥控器</a>
        </div>
    </div>
</body>
</html>)html", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Configuration callback not set");
    }

    return ESP_OK;
}

// API配置GET处理器
esp_err_t WebServer::api_config_get_handler(httpd_req_t *req) {
    WebServer* server = (WebServer*)req->user_ctx;

    // 设置CORS头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    if (server->get_motor_config_callback_) {
        MotorActionConfig config = server->get_motor_config_callback_();

        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "forward_ms", config.forward_duration_ms);
        cJSON_AddNumberToObject(root, "backward_ms", config.backward_duration_ms);
        cJSON_AddNumberToObject(root, "left_turn_ms", config.left_turn_duration_ms);
        cJSON_AddNumberToObject(root, "right_turn_ms", config.right_turn_duration_ms);
        cJSON_AddNumberToObject(root, "spin_ms", config.spin_duration_ms);
        cJSON_AddNumberToObject(root, "quick_fwd_ms", config.quick_forward_duration_ms);
        cJSON_AddNumberToObject(root, "quick_bwd_ms", config.quick_backward_duration_ms);
        cJSON_AddNumberToObject(root, "def_speed_pct", config.default_speed_percent);

        char *json_str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (json_str) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
            cJSON_free(json_str);
        } else {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to generate JSON");
        }
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Configuration callback not set");
    }

    return ESP_OK;
}

// API配置POST处理器
esp_err_t WebServer::api_config_post_handler(httpd_req_t *req) {
    WebServer* server = (WebServer*)req->user_ctx;

    // 设置CORS头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }

    // 解析JSON数据
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    MotorActionConfig config;
    config.forward_duration_ms = cJSON_GetObjectItem(root, "forward_ms")->valueint;
    config.backward_duration_ms = cJSON_GetObjectItem(root, "backward_ms")->valueint;
    config.left_turn_duration_ms = cJSON_GetObjectItem(root, "left_turn_ms")->valueint;
    config.right_turn_duration_ms = cJSON_GetObjectItem(root, "right_turn_ms")->valueint;
    config.spin_duration_ms = cJSON_GetObjectItem(root, "spin_ms")->valueint;
    config.quick_forward_duration_ms = cJSON_GetObjectItem(root, "quick_fwd_ms")->valueint;
    config.quick_backward_duration_ms = cJSON_GetObjectItem(root, "quick_bwd_ms")->valueint;
    config.default_speed_percent = cJSON_GetObjectItem(root, "def_speed_pct")->valueint;

    cJSON_Delete(root);

    // 保存配置
    if (server->set_motor_config_callback_) {
        server->set_motor_config_callback_(config);

        // 在串口中输出保存的配置信息
        ESP_LOGI(TAG, "网页配置已保存:");
        ESP_LOGI(TAG, "  前进时间: %d ms", config.forward_duration_ms);
        ESP_LOGI(TAG, "  后退时间: %d ms", config.backward_duration_ms);
        ESP_LOGI(TAG, "  左转时间: %d ms", config.left_turn_duration_ms);
        ESP_LOGI(TAG, "  右转时间: %d ms", config.right_turn_duration_ms);
        ESP_LOGI(TAG, "  转圈时间: %d ms", config.spin_duration_ms);
        ESP_LOGI(TAG, "  快速前进时间: %d ms", config.quick_forward_duration_ms);
        ESP_LOGI(TAG, "  快速后退时间: %d ms", config.quick_backward_duration_ms);
        ESP_LOGI(TAG, "  默认速度: %d%%", config.default_speed_percent);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"success\"}", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Configuration callback not set");
    }

    return ESP_OK;
}

// 解析配置表单数据
void WebServer::parse_config_form_data(const char* data, MotorActionConfig& config) {
    char* str = strdup(data);
    char* token = strtok(str, "&");

    while (token != NULL) {
        char* eq_pos = strchr(token, '=');
        if (eq_pos) {
            *eq_pos = '\0';
            char* key = token;
            char* value = eq_pos + 1;

            if (strcmp(key, "forward_ms") == 0) {
                config.forward_duration_ms = atoi(value);
            } else if (strcmp(key, "backward_ms") == 0) {
                config.backward_duration_ms = atoi(value);
            } else if (strcmp(key, "left_turn_ms") == 0) {
                config.left_turn_duration_ms = atoi(value);
            } else if (strcmp(key, "right_turn_ms") == 0) {
                config.right_turn_duration_ms = atoi(value);
            } else if (strcmp(key, "spin_ms") == 0) {
                config.spin_duration_ms = atoi(value);
            } else if (strcmp(key, "quick_fwd_ms") == 0) {
                config.quick_forward_duration_ms = atoi(value);
            } else if (strcmp(key, "quick_bwd_ms") == 0) {
                config.quick_backward_duration_ms = atoi(value);
            } else if (strcmp(key, "def_speed_pct") == 0) {
                config.default_speed_percent = atoi(value);
            }
        }
        token = strtok(NULL, "&");
    }

    free(str);
}

// 获取配置页面HTML
const char* WebServer::get_config_html_page() {
    static const char config_html_page[] = R"html(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>电机动作配置</title>
    <style>
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            margin: 0;
            padding: 20px;
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
        }

        .container {
            background: rgba(255, 255, 255, 0.95);
            border-radius: 20px;
            padding: 30px;
            box-shadow: 0 20px 40px rgba(0,0,0,0.1);
            max-width: 600px;
            width: 100%;
        }

        h1 {
            color: #333;
            text-align: center;
            margin-bottom: 30px;
        }

        .form-group {
            margin-bottom: 20px;
        }

        label {
            display: block;
            margin-bottom: 5px;
            font-weight: bold;
            color: #555;
        }

        input[type="number"] {
            width: 100%;
            padding: 10px;
            border: 2px solid #ddd;
            border-radius: 8px;
            font-size: 16px;
            transition: border-color 0.3s ease;
        }

        input[type="number"]:focus {
            outline: none;
            border-color: #4CAF50;
        }

        .unit {
            color: #666;
            font-size: 14px;
            margin-left: 5px;
        }

        .description {
            color: #777;
            font-size: 14px;
            margin-top: 3px;
            font-weight: normal;
        }

        .buttons {
            text-align: center;
            margin-top: 30px;
        }

        .btn {
            background: linear-gradient(135deg, #4CAF50, #45a049);
            color: white;
            border: none;
            padding: 12px 30px;
            border-radius: 25px;
            font-size: 16px;
            cursor: pointer;
            margin: 0 10px;
            text-decoration: none;
            display: inline-block;
            transition: all 0.3s ease;
        }

        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(76, 175, 80, 0.4);
        }

        .btn.secondary {
            background: linear-gradient(135deg, #2196F3, #1976D2);
        }

        .btn.secondary:hover {
            box-shadow: 0 6px 20px rgba(33, 150, 243, 0.4);
        }

        .grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
        }

        @media (max-width: 480px) {
            .grid {
                grid-template-columns: 1fr;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>⚙️ 电机动作配置</h1>
        <form method="POST" action="/config">
            <div class="grid">
                <div class="form-group">
                    <label for="forward_ms">前进时间</label>
                    <input type="number" id="forward_ms" name="forward_ms" min="100" max="30000" step="100" required>
                    <span class="unit">毫秒</span>
                    <div class="description">默认前进动作的持续时间</div>
                </div>

                <div class="form-group">
                    <label for="backward_ms">后退时间</label>
                    <input type="number" id="backward_ms" name="backward_ms" min="100" max="30000" step="100" required>
                    <span class="unit">毫秒</span>
                    <div class="description">默认后退动作的持续时间</div>
                </div>

                <div class="form-group">
                    <label for="left_turn_ms">左转时间</label>
                    <input type="number" id="left_turn_ms" name="left_turn_ms" min="100" max="10000" step="50" required>
                    <span class="unit">毫秒</span>
                    <div class="description">左转动作的持续时间</div>
                </div>

                <div class="form-group">
                    <label for="right_turn_ms">右转时间</label>
                    <input type="number" id="right_turn_ms" name="right_turn_ms" min="100" max="10000" step="50" required>
                    <span class="unit">毫秒</span>
                    <div class="description">右转动作的持续时间</div>
                </div>

                <div class="form-group">
                    <label for="spin_ms">转圈时间</label>
                    <input type="number" id="spin_ms" name="spin_ms" min="500" max="10000" step="100" required>
                    <span class="unit">毫秒</span>
                    <div class="description">转圈动作的持续时间</div>
                </div>


                <div class="form-group">
                    <label for="def_speed_pct">默认速度</label>
                    <input type="number" id="def_speed_pct" name="def_speed_pct" min="10" max="100" step="5" required>
                    <span class="unit">%</span>
                    <div class="description">电机动作的默认速度百分比</div>
                </div>
            </div>

            <div class="buttons">
                <button type="submit" class="btn">💾 保存配置</button>
                <a href="/" class="btn secondary">🏠 返回遥控器</a>
            </div>
        </form>
    </div>

    <script>
        // 页面加载时获取当前配置
        window.onload = function() {
            fetch('/api/config')
                .then(response => response.json())
                .then(config => {
                    document.getElementById('forward_ms').value = config.forward_ms;
                    document.getElementById('backward_ms').value = config.backward_ms;
                    document.getElementById('left_turn_ms').value = config.left_turn_ms;
                    document.getElementById('right_turn_ms').value = config.right_turn_ms;
                    document.getElementById('spin_ms').value = config.spin_ms;
                    document.getElementById('def_speed_pct').value = config.def_speed_pct;
                })
                .catch(error => console.error('Failed to load config:', error));
        };

        // 处理表单提交
        document.getElementById('config-form').addEventListener('submit', function(e) {
            e.preventDefault(); // 阻止默认表单提交

            const formData = new FormData(this);
            const data = Object.fromEntries(formData.entries());

            fetch('/api/config', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(data)
            })
            .then(response => response.json())
            .then(result => {
                if (result.status === 'success') {
                    // 显示成功消息
                    alert('配置保存成功！');
                    // 自动跳转回遥控器界面
                    window.location.href = '/';
                } else {
                    alert('配置保存失败，请重试');
                }
            })
            .catch(error => {
                console.error('Failed to save config:', error);
                alert('配置保存失败，请检查网络连接');
            });
        });
    </script>
</body>
</html>
)html";

    return config_html_page;
}