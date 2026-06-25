#pragma once

#include "EventBus.h"
#include "Hal.h"
#include "ToolRegistry.h"

#include "esp_http_server.h"

namespace stackyan {

class ApiServer {
public:
    ApiServer(stackchu::Hal& hal, ToolRegistry& registry, EventBus& events);

    bool begin();

private:
    static esp_err_t handleIndex(httpd_req_t* req);
    static esp_err_t handleStatus(httpd_req_t* req);
    static esp_err_t handleCapabilities(httpd_req_t* req);
    static esp_err_t handleEvents(httpd_req_t* req);
    static esp_err_t handleInvoke(httpd_req_t* req);
    static esp_err_t handleTools(httpd_req_t* req);
    static esp_err_t handleToolGet(httpd_req_t* req);
    static esp_err_t handleToolPost(httpd_req_t* req);

    esp_err_t sendStatus(httpd_req_t* req);
    esp_err_t sendCapabilities(httpd_req_t* req);
    esp_err_t sendEvents(httpd_req_t* req);
    esp_err_t sendTools(httpd_req_t* req);
    esp_err_t sendTool(httpd_req_t* req, const char* name);
    esp_err_t invokeTool(httpd_req_t* req, const char* name);
    esp_err_t invokeRpc(httpd_req_t* req);
    esp_err_t invokeByName(httpd_req_t* req, const char* name, cJSON* args);

    stackchu::Hal& hal_;
    ToolRegistry& registry_;
    EventBus& events_;
    httpd_handle_t server_ = nullptr;
};

}  // namespace stackyan
