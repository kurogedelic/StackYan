#pragma once

#include <cstdint>
#include <string>

#include "cJSON.h"

namespace stackyan {

struct ToolResult {
    bool ok = false;
    cJSON* result = nullptr;
    std::string errorCode;
    std::string errorMessage;

    ToolResult() = default;
    ToolResult(const ToolResult&) = delete;
    ToolResult& operator=(const ToolResult&) = delete;

    ToolResult(ToolResult&& other) noexcept {
        ok = other.ok;
        result = other.result;
        errorCode = std::move(other.errorCode);
        errorMessage = std::move(other.errorMessage);
        other.result = nullptr;
    }

    ToolResult& operator=(ToolResult&& other) noexcept {
        if (this != &other) {
            if (result) cJSON_Delete(result);
            ok = other.ok;
            result = other.result;
            errorCode = std::move(other.errorCode);
            errorMessage = std::move(other.errorMessage);
            other.result = nullptr;
        }
        return *this;
    }

    ~ToolResult() {
        if (result) cJSON_Delete(result);
    }

    static ToolResult success(cJSON* value) {
        ToolResult r;
        r.ok = true;
        r.result = value ? value : cJSON_CreateObject();
        return r;
    }

    static ToolResult error(const char* code, const char* message) {
        ToolResult r;
        r.ok = false;
        r.errorCode = code ? code : "ERROR";
        r.errorMessage = message ? message : "Tool invocation failed";
        return r;
    }
};

class ITool {
public:
    virtual ~ITool() = default;
    virtual const char* name() const = 0;
    virtual const char* title() const = 0;
    virtual const char* description() const = 0;
    virtual const char* parametersSchema() const = 0;
    virtual bool dangerous() const { return false; }
    virtual uint32_t timeoutMs() const { return 1000; }
    virtual ToolResult invoke(const cJSON* args) = 0;
};

}  // namespace stackyan
