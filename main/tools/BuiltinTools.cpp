#include "BuiltinTools.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Types.h"
#include "hal/IMotionSensor.h"
#include "hal/IPower.h"
#include "hal/IRgb.h"
#include "hal/IServo.h"
#include "hal/IStorage.h"

namespace stackyan {
namespace {

int jsonInt(const cJSON* obj, const char* key, int fallback) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valueint : fallback;
}

const char* jsonString(const cJSON* obj, const char* key, const char* fallback = nullptr) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : fallback;
}

int clamp255(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

stackchu::ServoAxis axisFromString(const char* axis) {
    if (axis && std::strcmp(axis, "vertical") == 0) return stackchu::ServoAxis::Vertical;
    return stackchu::ServoAxis::Horizontal;
}

const char* chargeName(stackchu::ChargeState state) {
    switch (state) {
        case stackchu::ChargeState::Discharging: return "discharging";
        case stackchu::ChargeState::Charging: return "charging";
        case stackchu::ChargeState::Standby: return "standby";
        case stackchu::ChargeState::Full: return "full";
        default: return "unknown";
    }
}

class RgbSetTool final : public ITool {
public:
    explicit RgbSetTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "rgb.set"; }
    const char* title() const override { return "Set RGB"; }
    const char* description() const override { return "Set all RGB LEDs to one color."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["r","g","b"],"properties":{"r":{"type":"integer","minimum":0,"maximum":255},"g":{"type":"integer","minimum":0,"maximum":255},"b":{"type":"integer","minimum":0,"maximum":255},"brightness":{"type":"integer","minimum":0,"maximum":255,"default":64}}})";
    }
    ToolResult invoke(const cJSON* args) override {
        if (!cJSON_IsObject(args)) return ToolResult::error("INVALID_ARGUMENT", "args must be an object");
        const int r = clamp255(jsonInt(args, "r", -1));
        const int g = clamp255(jsonInt(args, "g", -1));
        const int b = clamp255(jsonInt(args, "b", -1));
        const int brightness = clamp255(jsonInt(args, "brightness", 64));
        if (jsonInt(args, "r", -1) < 0 || jsonInt(args, "g", -1) < 0 || jsonInt(args, "b", -1) < 0) {
            return ToolResult::error("INVALID_ARGUMENT", "r, g, and b are required");
        }
        auto& rgb = hardware_.rgb();
        rgb.setBrightness(brightness / 255.0f);
        rgb.setAll(stackchu::Color{static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)});
        rgb.show();
        cJSON* out = cJSON_CreateObject();
        cJSON_AddBoolToObject(out, "applied", true);
        cJSON_AddNumberToObject(out, "r", r);
        cJSON_AddNumberToObject(out, "g", g);
        cJSON_AddNumberToObject(out, "b", b);
        cJSON_AddNumberToObject(out, "brightness", brightness);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class RgbClearTool final : public ITool {
public:
    explicit RgbClearTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "rgb.clear"; }
    const char* title() const override { return "Clear RGB"; }
    const char* description() const override { return "Turn off all RGB LEDs."; }
    const char* parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    ToolResult invoke(const cJSON*) override {
        hardware_.rgb().clear();
        cJSON* out = cJSON_CreateObject();
        cJSON_AddBoolToObject(out, "cleared", true);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class ServoMoveTool final : public ITool {
public:
    explicit ServoMoveTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "servo.move"; }
    const char* title() const override { return "Move Servo"; }
    const char* description() const override { return "Move a StackChan servo axis. Vertical axis is clamped by HAL safety limits."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["axis","degrees"],"properties":{"axis":{"type":"string","enum":["horizontal","vertical"],"default":"horizontal"},"degrees":{"type":"number"},"speed":{"type":"integer","minimum":0,"maximum":3000,"default":1000}}})";
    }
    bool dangerous() const override { return true; }
    uint32_t timeoutMs() const override { return 2000; }
    ToolResult invoke(const cJSON* args) override {
        if (!cJSON_IsObject(args)) return ToolResult::error("INVALID_ARGUMENT", "args must be an object");
        const char* axisName = jsonString(args, "axis", "horizontal");
        const cJSON* degJson = cJSON_GetObjectItemCaseSensitive(args, "degrees");
        if (!cJSON_IsNumber(degJson)) return ToolResult::error("INVALID_ARGUMENT", "degrees is required");
        const uint16_t speed = static_cast<uint16_t>(jsonInt(args, "speed", 1000));
        const auto axis = axisFromString(axisName);
        hardware_.servo().moveTo(axis, static_cast<float>(degJson->valuedouble), speed);
        cJSON* out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "axis", axisName);
        cJSON_AddNumberToObject(out, "degrees", degJson->valuedouble);
        cJSON_AddNumberToObject(out, "speed", speed);
        cJSON_AddBoolToObject(out, "commanded", true);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class ServoHomeTool final : public ITool {
public:
    explicit ServoHomeTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "servo.home"; }
    const char* title() const override { return "Home Servo"; }
    const char* description() const override { return "Move servos to a conservative home pose."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","properties":{"speed":{"type":"integer","minimum":0,"maximum":3000,"default":1000}}})";
    }
    bool dangerous() const override { return true; }
    uint32_t timeoutMs() const override { return 2000; }
    ToolResult invoke(const cJSON* args) override {
        const uint16_t speed = static_cast<uint16_t>(jsonInt(args, "speed", 1000));
        hardware_.servo().moveTo(stackchu::ServoAxis::Horizontal, 0.0f, speed);
        hardware_.servo().moveTo(stackchu::ServoAxis::Vertical, 45.0f, speed);
        cJSON* out = cJSON_CreateObject();
        cJSON_AddBoolToObject(out, "commanded", true);
        cJSON_AddNumberToObject(out, "horizontal", 0);
        cJSON_AddNumberToObject(out, "vertical", 45);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class MotionReadTool final : public ITool {
public:
    explicit MotionReadTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "motion.read"; }
    const char* title() const override { return "Read Motion"; }
    const char* description() const override { return "Read accelerometer, gyroscope, and temperature sample."; }
    const char* parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    ToolResult invoke(const cJSON*) override {
        const auto sample = hardware_.motion().read();
        cJSON* out = cJSON_CreateObject();
        cJSON_AddBoolToObject(out, "valid", sample.valid);
        cJSON_AddNumberToObject(out, "ax", sample.ax);
        cJSON_AddNumberToObject(out, "ay", sample.ay);
        cJSON_AddNumberToObject(out, "az", sample.az);
        cJSON_AddNumberToObject(out, "gx", sample.gx);
        cJSON_AddNumberToObject(out, "gy", sample.gy);
        cJSON_AddNumberToObject(out, "gz", sample.gz);
        cJSON_AddNumberToObject(out, "temperature_c", sample.temperatureC);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class PowerStatusTool final : public ITool {
public:
    explicit PowerStatusTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "power.status"; }
    const char* title() const override { return "Power Status"; }
    const char* description() const override { return "Read battery and power state."; }
    const char* parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    ToolResult invoke(const cJSON*) override {
        const auto state = hardware_.power().read();
        cJSON* out = cJSON_CreateObject();
        cJSON_AddNumberToObject(out, "level", state.level);
        cJSON_AddStringToObject(out, "charge", chargeName(state.charge));
        cJSON_AddNumberToObject(out, "battery_voltage", state.batteryVoltage);
        cJSON_AddNumberToObject(out, "battery_current_ma", state.batteryCurrent);
        cJSON_AddNumberToObject(out, "temperature_c", state.temperatureC);
        cJSON_AddBoolToObject(out, "usb_connected", state.usbConnected);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class StorageReadTool final : public ITool {
public:
    explicit StorageReadTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "storage.read"; }
    const char* title() const override { return "Read Storage"; }
    const char* description() const override { return "Read a text file from StackYan storage."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["path"],"properties":{"path":{"type":"string","description":"Storage path, for example /stackyan/config.json"}}})";
    }
    ToolResult invoke(const cJSON* args) override {
        const char* path = jsonString(args, "path");
        if (!path || !path[0]) return ToolResult::error("INVALID_ARGUMENT", "path is required");
        std::vector<char> text;
        if (!hardware_.storage().readText(path, text)) return ToolResult::error("READ_FAILED", "could not read path");
        cJSON* out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "path", path);
        cJSON_AddStringToObject(out, "content", text.data());
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class StorageWriteTool final : public ITool {
public:
    explicit StorageWriteTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "storage.write"; }
    const char* title() const override { return "Write Storage"; }
    const char* description() const override { return "Write text content to StackYan storage."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["path","content"],"properties":{"path":{"type":"string","description":"Storage path, for example /stackyan/config.json"},"content":{"type":"string"}}})";
    }
    bool dangerous() const override { return true; }
    ToolResult invoke(const cJSON* args) override {
        const char* path = jsonString(args, "path");
        const char* content = jsonString(args, "content");
        if (!path || !path[0]) return ToolResult::error("INVALID_ARGUMENT", "path is required");
        if (!content) return ToolResult::error("INVALID_ARGUMENT", "content is required");
        if (!hardware_.storage().writeText(path, content)) return ToolResult::error("WRITE_FAILED", "could not write path");
        cJSON* out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "path", path);
        cJSON_AddNumberToObject(out, "bytes", std::strlen(content));
        cJSON_AddBoolToObject(out, "written", true);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

}  // namespace

void registerBuiltinTools(ToolRegistry& registry, IHardware& hardware) {
    registry.registerTool(std::make_unique<RgbSetTool>(hardware));
    registry.registerTool(std::make_unique<RgbClearTool>(hardware));
    registry.registerTool(std::make_unique<ServoMoveTool>(hardware));
    registry.registerTool(std::make_unique<ServoHomeTool>(hardware));
    registry.registerTool(std::make_unique<MotionReadTool>(hardware));
    registry.registerTool(std::make_unique<PowerStatusTool>(hardware));
    registry.registerTool(std::make_unique<StorageReadTool>(hardware));
    registry.registerTool(std::make_unique<StorageWriteTool>(hardware));
}

}  // namespace stackyan
