#pragma once

#include <cstdint>
#include <string>

#include "cJSON.h"
#include "EventBus.h"
#include "IHardware.h"
#include "ToolRegistry.h"

namespace stackyan::sim {

class SimApiServer {
public:
    SimApiServer(stackyan::IHardware& hardware, stackyan::ToolRegistry& registry, stackyan::EventBus& events)
        : hardware_(hardware), registry_(registry), events_(events) {}

    bool begin(uint16_t port = 8080);

private:
    void serveLoop(int serverFd);
    void handleClient(int clientFd);
    void sendResponse(int clientFd, int status, const char* contentType, const char* body);
    void sendJson(int clientFd, int status, cJSON* root);
    void handleJsonEndpoint(int clientFd, const std::string& method, const std::string& path, const std::string& body);
    void invokeByName(int clientFd, const char* name, cJSON* args);

    stackyan::IHardware& hardware_;
    stackyan::ToolRegistry& registry_;
    stackyan::EventBus& events_;
};

}  // namespace stackyan::sim
