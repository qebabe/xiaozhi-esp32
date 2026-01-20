#include "web_server.h"
#include <esp_log.h>
#include <cstring>
#include <cJSON.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>

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

    httpd_uri_t debug_uri = {
        .uri       = "/api/debug/motor_test",
        .method    = HTTP_POST,
        .handler   = debug_motor_test_handler,
        .user_ctx  = this
    };
    httpd_register_uri_handler(server_handle_, &debug_uri);


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
            <button class="control-btn" onclick="debugForward()">▶️ 测试前进</button>
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

        // Debug: 远程触发一次前进测试（通过 /api/debug/motor_test）
        async function debugForward() {
            try {
                const res = await fetch('/api/debug/motor_test', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ direction: 4, speed: 90, duration: 1000 })
                });
                if (!res.ok) throw new Error('Network response not ok');
                console.log('Debug forward sent');
            } catch (err) {
                console.error('Debug forward failed', err);
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