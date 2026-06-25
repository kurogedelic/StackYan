#include "SimApiServer.h"
#include "SimWebUi.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace stackyan::sim {
namespace {


cJSON* errorJson(const char* code, const char* message) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON* err = cJSON_AddObjectToObject(root, "error");
    cJSON_AddStringToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message);
    return root;
}

uint64_t nowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string pathAfter(const std::string& path, const char* prefix) {
    const std::string p(prefix);
    if (path.rfind(p, 0) != 0) return {};
    return path.substr(p.size());
}

}  // namespace

bool SimApiServer::begin(uint16_t port) {
    int serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        std::perror("socket");
        return false;
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (::bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(serverFd);
        return false;
    }

    if (::listen(serverFd, 16) < 0) {
        std::perror("listen");
        ::close(serverFd);
        return false;
    }

    std::cout << "[sim] HTTP server: http://localhost:" << port << std::endl;
    std::thread([this, serverFd]() { serveLoop(serverFd); }).detach();
    return true;
}

void SimApiServer::serveLoop(int serverFd) {
    while (true) {
        int client = ::accept(serverFd, nullptr, nullptr);
        if (client >= 0) std::thread([this, client]() { handleClient(client); }).detach();
    }
}

void SimApiServer::handleClient(int clientFd) {
    std::string req;
    char buf[4096];
    ssize_t n = 0;
    while ((n = ::recv(clientFd, buf, sizeof(buf), 0)) > 0) {
        req.append(buf, buf + n);
        const size_t headerEnd = req.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            size_t contentLength = 0;
            const size_t cl = req.find("Content-Length:");
            if (cl != std::string::npos) {
                contentLength = std::stoul(req.substr(cl + 15));
            }
            if (req.size() >= headerEnd + 4 + contentLength) break;
        }
    }

    std::istringstream stream(req);
    std::string method;
    std::string path;
    std::string version;
    stream >> method >> path >> version;

    const size_t headerEnd = req.find("\r\n\r\n");
    const std::string body = headerEnd == std::string::npos ? "" : req.substr(headerEnd + 4);

    if (method == "GET" && path == "/") {
        sendResponse(clientFd, 200, "text/html", kSimIndexHtml);
    } else {
        handleJsonEndpoint(clientFd, method, path, body);
    }
    ::close(clientFd);
}

void SimApiServer::sendResponse(int clientFd, int status, const char* contentType, const char* body) {
    const char* text = status == 200 ? "OK" : (status == 404 ? "Not Found" : "Bad Request");
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << text << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << std::strlen(body) << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    const std::string response = out.str();
    ::send(clientFd, response.data(), response.size(), 0);
}

void SimApiServer::sendJson(int clientFd, int status, cJSON* root) {
    char* json = cJSON_PrintUnformatted(root);
    sendResponse(clientFd, status, "application/json", json ? json : "{}");
    if (json) cJSON_free(json);
    cJSON_Delete(root);
}

void SimApiServer::handleJsonEndpoint(int clientFd, const std::string& method, const std::string& path, const std::string& body) {
    if (method == "GET" && path == "/api/status") {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "name", "StackYan mac_simulator");
        cJSON_AddStringToObject(root, "hostname", "localhost");
        cJSON_AddStringToObject(root, "version", "0.1.0-sim");
        cJSON_AddNumberToObject(root, "uptime_ms", nowMs());
        cJSON_AddNumberToObject(root, "tool_count", registry_.count());
        cJSON* storage = cJSON_AddObjectToObject(root, "storage");
        cJSON_AddBoolToObject(storage, "mounted", hardware_.storage().isMounted());
        cJSON_AddStringToObject(storage, "root", hardware_.storage().root());
        cJSON_AddNumberToObject(storage, "free_bytes", hardware_.storage().freeSpace());
        sendJson(clientFd, 200, root);
    } else if (method == "GET" && path == "/api/capabilities") {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "role", "physical_tool_server_simulator");
        cJSON_AddStringToObject(root, "tool_discovery", "/api/tools");
        cJSON_AddStringToObject(root, "tool_invoke", "/api/invoke");
        cJSON_AddStringToObject(root, "events", "/api/events");
        cJSON_AddBoolToObject(root, "face_tools", true);
        cJSON_AddBoolToObject(root, "workflow_tools", true);
        cJSON_AddBoolToObject(root, "avatar_renderer_attached", false);
        sendJson(clientFd, 200, root);
    } else if (method == "GET" && path == "/api/events") {
        sendJson(clientFd, 200, events_.toJson());
    } else if (method == "GET" && path == "/api/sim/state") {
        sendJson(clientFd, 200, hardware_.stateJson());
    } else if (method == "GET" && path == "/api/tools") {
        sendJson(clientFd, 200, registry_.toJson());
    } else if (method == "GET" && path.rfind("/api/tools/", 0) == 0) {
        const std::string name = pathAfter(path, "/api/tools/");
        const ITool* tool = registry_.find(name);
        if (!tool) sendJson(clientFd, 404, errorJson("NOT_FOUND", "tool not found"));
        else sendJson(clientFd, 200, registry_.toolToJson(*tool));
    } else if (method == "POST" && path == "/api/invoke") {
        cJSON* root = cJSON_Parse(body.c_str());
        if (!root) return sendJson(clientFd, 400, errorJson("BAD_JSON", "request body must be JSON"));
        const cJSON* tool = cJSON_GetObjectItemCaseSensitive(root, "tool");
        const cJSON* argsJson = cJSON_GetObjectItemCaseSensitive(root, "args");
        if (!cJSON_IsString(tool)) {
            cJSON_Delete(root);
            return sendJson(clientFd, 400, errorJson("INVALID_ARGUMENT", "tool is required"));
        }
        cJSON* args = argsJson ? cJSON_Duplicate(argsJson, true) : cJSON_CreateObject();
        const std::string name = tool->valuestring;
        cJSON_Delete(root);
        invokeByName(clientFd, name.c_str(), args);
    } else if (method == "POST" && path.rfind("/api/tools/", 0) == 0) {
        const std::string name = pathAfter(path, "/api/tools/");
        cJSON* args = body.empty() ? cJSON_CreateObject() : cJSON_Parse(body.c_str());
        if (!args) return sendJson(clientFd, 400, errorJson("BAD_JSON", "request body must be JSON"));
        invokeByName(clientFd, name.c_str(), args);
    } else {
        sendJson(clientFd, 404, errorJson("NOT_FOUND", "endpoint not found"));
    }
}

void SimApiServer::invokeByName(int clientFd, const char* name, cJSON* args) {
    ITool* tool = registry_.find(name ? name : "");
    if (!tool) {
        cJSON_Delete(args);
        return sendJson(clientFd, 404, errorJson("NOT_FOUND", "tool not found"));
    }

    const auto start = std::chrono::steady_clock::now();
    ToolResult result = tool->invoke(args);
    cJSON_Delete(args);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    std::cout << "[sim] tool " << tool->name() << " ok=" << (result.ok ? "true" : "false")
              << " elapsed_ms=" << elapsed << std::endl;

    cJSON* eventPayload = cJSON_CreateObject();
    cJSON_AddStringToObject(eventPayload, "tool", tool->name());
    cJSON_AddBoolToObject(eventPayload, "ok", result.ok);
    cJSON_AddNumberToObject(eventPayload, "elapsed_ms", static_cast<double>(elapsed));
    events_.publish("tool.invoked", "ToolRegistry", eventPayload);
    cJSON_Delete(eventPayload);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", result.ok);
    cJSON_AddStringToObject(root, "tool", tool->name());
    cJSON_AddNumberToObject(root, "elapsed_ms", static_cast<double>(elapsed));
    if (result.ok) {
        cJSON_AddItemToObject(root, "result", cJSON_Duplicate(result.result, true));
        sendJson(clientFd, 200, root);
    } else {
        cJSON* err = cJSON_AddObjectToObject(root, "error");
        cJSON_AddStringToObject(err, "code", result.errorCode.c_str());
        cJSON_AddStringToObject(err, "message", result.errorMessage.c_str());
        sendJson(clientFd, 400, root);
    }
}

}  // namespace stackyan::sim
