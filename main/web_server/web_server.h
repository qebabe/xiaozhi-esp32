#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_http_server.h>
#include <string>
#include <functional>
#include <vector>

class WebServer {
public:
    WebServer();
    ~WebServer();

    bool Start(int port = 80);
    void Stop();

    // 注册电机控制回调函数 - 使用 std::function 支持 lambda
    void SetMotorControlCallback(std::function<void(int direction, int speed)> callback);
    // 由外部调用以触发电机控制（封装私有回调）
    void InvokeMotorControl(int direction, int speed);
    // Debug handler 注册（/api/debug/motor_test）
    static esp_err_t debug_motor_test_handler(httpd_req_t *req);

private:
    httpd_handle_t server_handle_;
    std::function<void(int direction, int speed)> motor_control_callback_;

    // HTTP请求处理函数
    static esp_err_t index_get_handler(httpd_req_t *req);
    static esp_err_t control_post_handler(httpd_req_t *req);
    static esp_err_t api_control_handler(httpd_req_t *req);

    // CORS处理
    static esp_err_t cors_handler(httpd_req_t *req);

    // 获取HTML页面内容
    static const char* get_html_page();

    // 解析控制命令
    void parse_simple_control_command(const char* data, int& direction, int& speed);
    void parse_json_control_command(const char* data, int& direction, int& speed);
};

#endif // WEB_SERVER_H