#include "BuiltinTools.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "AvatarService.h"
#include "EventBus.h"
#include "Types.h"
#include "hal/IMotionSensor.h"
#include "hal/IPower.h"
#include "hal/IRgb.h"
#include "hal/IServo.h"
#include "hal/IStorage.h"

#ifdef STACKYAN_SIMULATOR
#include <chrono>
#include <thread>
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

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

void sleepMs(uint32_t ms) {
#ifdef STACKYAN_SIMULATOR
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#else
    vTaskDelay(pdMS_TO_TICKS(ms));
#endif
}

stackchu::ServoAxis axisFromString(const char* axis) {
    if (axis && std::strcmp(axis, "vertical") == 0) return stackchu::ServoAxis::Vertical;
    return stackchu::ServoAxis::Horizontal;
}

const char* axisName(stackchu::ServoAxis axis) {
    return axis == stackchu::ServoAxis::Vertical ? "vertical" : "horizontal";
}

cJSON* servoStateToJson(stackchu::ServoAxis axis, const stackchu::ServoState& state) {
    cJSON* out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "axis", axisName(axis));
    cJSON_AddBoolToObject(out, "valid", state.valid);
    cJSON_AddNumberToObject(out, "position_deg", state.positionDeg);
    cJSON_AddNumberToObject(out, "speed", state.speed);
    cJSON_AddNumberToObject(out, "load", state.load);
    cJSON_AddNumberToObject(out, "temperature_c", state.temperatureC);
    cJSON_AddNumberToObject(out, "voltage", state.voltage);
    return out;
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

cJSON* readConfigJson(stackchu::IStorage& storage) {
    std::vector<char> text;
    if (!storage.readText("/stackyan/config.json", text)) return nullptr;
    return cJSON_Parse(text.data());
}

bool writeJsonFile(stackchu::IStorage& storage, const char* path, const cJSON* json) {
    if (!json) return false;
    char* text = cJSON_Print(json);
    if (!text) return false;
    const bool ok = storage.writeText(path, text);
    cJSON_free(text);
    return ok;
}

std::string workflowPath(const char* name) {
    std::string n = name ? name : "";
    while (!n.empty() && n[0] == '/') n.erase(n.begin());
    if (n.empty()) return {};
    if (n.find("..") != std::string::npos) return {};
    if (n.rfind("stackyan/workflows/", 0) == 0) n = n.substr(std::strlen("stackyan/workflows/"));
    if (n.rfind("workflows/", 0) == 0) n = n.substr(std::strlen("workflows/"));
    if (n.size() < 5 || n.substr(n.size() - 5) != ".json") n += ".json";
    return std::string("/stackyan/workflows/") + n;
}

cJSON* readJsonFile(stackchu::IStorage& storage, const char* path) {
    std::vector<char> text;
    if (!storage.readText(path, text)) return nullptr;
    return cJSON_Parse(text.data());
}

void mergeObjectShallow(cJSON* target, const cJSON* patch) {
    if (!cJSON_IsObject(target) || !cJSON_IsObject(patch)) return;
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, patch) {
        if (!item || !item->string) continue;
        cJSON* copy = cJSON_Duplicate(item, true);
        if (!copy) continue;
        if (cJSON_GetObjectItemCaseSensitive(target, item->string)) {
            cJSON_ReplaceItemInObjectCaseSensitive(target, item->string, copy);
        } else {
            cJSON_AddItemToObject(target, item->string, copy);
        }
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

class ServoReadStateTool final : public ITool {
public:
    explicit ServoReadStateTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "servo.readState"; }
    const char* title() const override { return "Read Servo State"; }
    const char* description() const override { return "Read feedback state for one servo axis."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","properties":{"axis":{"type":"string","enum":["horizontal","vertical"],"default":"horizontal"}}})";
    }
    ToolResult invoke(const cJSON* args) override {
        const char* requested = jsonString(args, "axis", "horizontal");
        const auto axis = axisFromString(requested);
        return ToolResult::success(servoStateToJson(axis, hardware_.servo().readState(axis)));
    }
private:
    IHardware& hardware_;
};

class ServoDisableTorqueTool final : public ITool {
public:
    explicit ServoDisableTorqueTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "servo.disableTorque"; }
    const char* title() const override { return "Disable Servo Torque"; }
    const char* description() const override { return "Disable torque for one servo axis."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","properties":{"axis":{"type":"string","enum":["horizontal","vertical"],"default":"horizontal"}}})";
    }
    bool dangerous() const override { return true; }
    ToolResult invoke(const cJSON* args) override {
        const char* requested = jsonString(args, "axis", "horizontal");
        const auto axis = axisFromString(requested);
        hardware_.servo().disableTorque(axis);
        cJSON* out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "axis", axisName(axis));
        cJSON_AddBoolToObject(out, "torque_disabled", true);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class ServoStopTool final : public ITool {
public:
    explicit ServoStopTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "servo.stop"; }
    const char* title() const override { return "Stop Servos"; }
    const char* description() const override { return "Stop servo motion conservatively by disabling torque on both axes."; }
    const char* parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    bool dangerous() const override { return true; }
    ToolResult invoke(const cJSON*) override {
        hardware_.servo().disableTorque(stackchu::ServoAxis::Horizontal);
        hardware_.servo().disableTorque(stackchu::ServoAxis::Vertical);
        cJSON* out = cJSON_CreateObject();
        cJSON_AddBoolToObject(out, "horizontal_torque_disabled", true);
        cJSON_AddBoolToObject(out, "vertical_torque_disabled", true);
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

class StorageInfoTool final : public ITool {
public:
    explicit StorageInfoTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "storage.info"; }
    const char* title() const override { return "Storage Info"; }
    const char* description() const override { return "Read storage mount status, root path, and free space."; }
    const char* parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    ToolResult invoke(const cJSON*) override {
        auto& storage = hardware_.storage();
        cJSON* out = cJSON_CreateObject();
        cJSON_AddBoolToObject(out, "mounted", storage.isMounted());
        cJSON_AddStringToObject(out, "root", storage.root());
        cJSON_AddNumberToObject(out, "free_bytes", storage.freeSpace());
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class StorageExistsTool final : public ITool {
public:
    explicit StorageExistsTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "storage.exists"; }
    const char* title() const override { return "Storage Exists"; }
    const char* description() const override { return "Check whether a storage path exists."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["path"],"properties":{"path":{"type":"string","default":"/stackyan/config.json"}}})";
    }
    ToolResult invoke(const cJSON* args) override {
        const char* path = jsonString(args, "path");
        if (!path || !path[0]) return ToolResult::error("INVALID_ARGUMENT", "path is required");
        cJSON* out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "path", path);
        cJSON_AddBoolToObject(out, "exists", hardware_.storage().exists(path));
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class StorageListTool final : public ITool {
public:
    explicit StorageListTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "storage.list"; }
    const char* title() const override { return "List Storage"; }
    const char* description() const override { return "List one storage directory."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","properties":{"path":{"type":"string","default":"/stackyan"}}})";
    }
    ToolResult invoke(const cJSON* args) override {
        const char* path = jsonString(args, "path", "/stackyan");
        std::vector<stackchu::DirEntry> entries;
        if (!hardware_.storage().listDir(path, entries)) {
            return ToolResult::error("LIST_FAILED", "could not list directory");
        }
        cJSON* out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "path", path);
        cJSON* arr = cJSON_AddArrayToObject(out, "entries");
        for (const auto& entry : entries) {
            cJSON* item = cJSON_CreateObject();
            const char* name = entry.name.empty() ? "" : entry.name.data();
            cJSON_AddStringToObject(item, "name", name);
            cJSON_AddBoolToObject(item, "is_dir", entry.isDir);
            cJSON_AddNumberToObject(item, "size", entry.size);
            cJSON_AddItemToArray(arr, item);
        }
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class StorageMkdirTool final : public ITool {
public:
    explicit StorageMkdirTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "storage.mkdir"; }
    const char* title() const override { return "Make Storage Directory"; }
    const char* description() const override { return "Create a storage directory. Existing directories are treated as success."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["path"],"properties":{"path":{"type":"string","default":"/stackyan/tools"}}})";
    }
    bool dangerous() const override { return true; }
    ToolResult invoke(const cJSON* args) override {
        const char* path = jsonString(args, "path");
        if (!path || !path[0]) return ToolResult::error("INVALID_ARGUMENT", "path is required");
        if (!hardware_.storage().mkdir(path)) return ToolResult::error("MKDIR_FAILED", "could not create directory");
        cJSON* out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "path", path);
        cJSON_AddBoolToObject(out, "created_or_exists", true);
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

class StorageRemoveTool final : public ITool {
public:
    explicit StorageRemoveTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "storage.remove"; }
    const char* title() const override { return "Remove Storage File"; }
    const char* description() const override { return "Remove one storage file. Directories are not removed by this tool."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["path"],"properties":{"path":{"type":"string","default":"/stackyan/logs/tool.log"}}})";
    }
    bool dangerous() const override { return true; }
    ToolResult invoke(const cJSON* args) override {
        const char* path = jsonString(args, "path");
        if (!path || !path[0]) return ToolResult::error("INVALID_ARGUMENT", "path is required");
        if (!hardware_.storage().remove(path)) return ToolResult::error("REMOVE_FAILED", "could not remove path");
        cJSON* out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "path", path);
        cJSON_AddBoolToObject(out, "removed", true);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class ConfigReadTool final : public ITool {
public:
    explicit ConfigReadTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "config.read"; }
    const char* title() const override { return "Read Config"; }
    const char* description() const override { return "Read /stackyan/config.json as validated JSON."; }
    const char* parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    ToolResult invoke(const cJSON*) override {
        cJSON* config = readConfigJson(hardware_.storage());
        if (!config) return ToolResult::error("READ_FAILED", "could not read valid /stackyan/config.json");
        cJSON* out = cJSON_CreateObject();
        cJSON_AddItemToObject(out, "config", config);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class ConfigWriteTool final : public ITool {
public:
    explicit ConfigWriteTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "config.write"; }
    const char* title() const override { return "Write Config"; }
    const char* description() const override { return "Replace /stackyan/config.json with a JSON object."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["config"],"properties":{"config":{"type":"object","description":"Complete StackYan config object"}}})";
    }
    bool dangerous() const override { return true; }
    ToolResult invoke(const cJSON* args) override {
        const cJSON* config = cJSON_GetObjectItemCaseSensitive(args, "config");
        if (!cJSON_IsObject(config)) return ToolResult::error("INVALID_ARGUMENT", "config object is required");
        cJSON* copy = cJSON_Duplicate(config, true);
        if (!writeJsonFile(hardware_.storage(), "/stackyan/config.json", copy)) {
            cJSON_Delete(copy);
            return ToolResult::error("WRITE_FAILED", "could not write /stackyan/config.json");
        }
        cJSON* out = cJSON_CreateObject();
        cJSON_AddBoolToObject(out, "written", true);
        cJSON_AddItemToObject(out, "config", copy);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class ConfigPatchTool final : public ITool {
public:
    explicit ConfigPatchTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "config.patch"; }
    const char* title() const override { return "Patch Config"; }
    const char* description() const override { return "Shallow-merge a JSON object into /stackyan/config.json."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["patch"],"properties":{"patch":{"type":"object","description":"Top-level JSON object to shallow-merge into the config"}}})";
    }
    bool dangerous() const override { return true; }
    ToolResult invoke(const cJSON* args) override {
        const cJSON* patch = cJSON_GetObjectItemCaseSensitive(args, "patch");
        if (!cJSON_IsObject(patch)) return ToolResult::error("INVALID_ARGUMENT", "patch object is required");
        cJSON* config = readConfigJson(hardware_.storage());
        if (!config) return ToolResult::error("READ_FAILED", "could not read valid /stackyan/config.json");
        mergeObjectShallow(config, patch);
        if (!writeJsonFile(hardware_.storage(), "/stackyan/config.json", config)) {
            cJSON_Delete(config);
            return ToolResult::error("WRITE_FAILED", "could not write /stackyan/config.json");
        }
        cJSON* out = cJSON_CreateObject();
        cJSON_AddBoolToObject(out, "patched", true);
        cJSON_AddItemToObject(out, "config", config);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class ConfigApplyTool final : public ITool {
public:
    ConfigApplyTool(IHardware& hardware, AvatarService& avatar) : hardware_(hardware), avatar_(avatar) {}
    const char* name() const override { return "config.apply"; }
    const char* title() const override { return "Apply Config"; }
    const char* description() const override { return "Apply supported /stackyan/config.json sections to runtime services."; }
    const char* parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    ToolResult invoke(const cJSON*) override {
        cJSON* config = readConfigJson(hardware_.storage());
        if (!config) return ToolResult::error("READ_FAILED", "could not read valid /stackyan/config.json");

        cJSON* out = cJSON_CreateObject();
        cJSON* applied = cJSON_AddObjectToObject(out, "applied");
        const cJSON* avatar = cJSON_GetObjectItemCaseSensitive(config, "avatar");
        if (cJSON_IsObject(avatar)) {
            const char* expression = jsonString(avatar, "expression");
            const char* vowel = jsonString(avatar, "vowel");
            const char* palette = jsonString(avatar, "palette");
            if (AvatarService::isValidExpression(expression)) {
                avatar_.setExpression(expression);
                cJSON_AddStringToObject(applied, "face.expression", expression);
            }
            if (AvatarService::isValidVowel(vowel)) {
                avatar_.setVowel(vowel);
                cJSON_AddStringToObject(applied, "face.vowel", vowel);
            }
            if (AvatarService::isValidPalette(palette)) {
                avatar_.setPalette(palette);
                cJSON_AddStringToObject(applied, "face.palette", palette);
            }
        }
        cJSON_AddItemToObject(out, "face", avatar_.stateJson());
        cJSON_Delete(config);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
    AvatarService& avatar_;
};

class WorkflowListTool final : public ITool {
public:
    explicit WorkflowListTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "workflow.list"; }
    const char* title() const override { return "List Workflows"; }
    const char* description() const override { return "List JSON workflows stored under /stackyan/workflows."; }
    const char* parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    ToolResult invoke(const cJSON*) override {
        std::vector<stackchu::DirEntry> entries;
        if (!hardware_.storage().listDir("/stackyan/workflows", entries)) {
            return ToolResult::error("LIST_FAILED", "could not list /stackyan/workflows");
        }
        cJSON* out = cJSON_CreateObject();
        cJSON* arr = cJSON_AddArrayToObject(out, "workflows");
        for (const auto& entry : entries) {
            if (entry.isDir) continue;
            const char* name = entry.name.empty() ? "" : entry.name.data();
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", name);
            cJSON_AddNumberToObject(item, "size", entry.size);
            cJSON_AddItemToArray(arr, item);
        }
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class WorkflowReadTool final : public ITool {
public:
    explicit WorkflowReadTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "workflow.read"; }
    const char* title() const override { return "Read Workflow"; }
    const char* description() const override { return "Read a stored JSON workflow by name."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["name"],"properties":{"name":{"type":"string","default":"demo"}}})";
    }
    ToolResult invoke(const cJSON* args) override {
        const std::string path = workflowPath(jsonString(args, "name"));
        if (path.empty()) return ToolResult::error("INVALID_ARGUMENT", "valid workflow name is required");
        cJSON* workflow = readJsonFile(hardware_.storage(), path.c_str());
        if (!workflow) return ToolResult::error("READ_FAILED", "could not read valid workflow JSON");
        cJSON* out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "path", path.c_str());
        cJSON_AddItemToObject(out, "workflow", workflow);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class WorkflowWriteTool final : public ITool {
public:
    explicit WorkflowWriteTool(IHardware& hardware) : hardware_(hardware) {}
    const char* name() const override { return "workflow.write"; }
    const char* title() const override { return "Write Workflow"; }
    const char* description() const override { return "Write a JSON workflow under /stackyan/workflows."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["name","workflow"],"properties":{"name":{"type":"string","default":"demo"},"workflow":{"type":"object","description":"Workflow object with a steps array"}}})";
    }
    bool dangerous() const override { return true; }
    ToolResult invoke(const cJSON* args) override {
        const std::string path = workflowPath(jsonString(args, "name"));
        if (path.empty()) return ToolResult::error("INVALID_ARGUMENT", "valid workflow name is required");
        const cJSON* workflow = cJSON_GetObjectItemCaseSensitive(args, "workflow");
        if (!cJSON_IsObject(workflow)) return ToolResult::error("INVALID_ARGUMENT", "workflow object is required");
        const cJSON* steps = cJSON_GetObjectItemCaseSensitive(workflow, "steps");
        if (!cJSON_IsArray(steps)) return ToolResult::error("INVALID_ARGUMENT", "workflow.steps array is required");
        if (!writeJsonFile(hardware_.storage(), path.c_str(), workflow)) {
            return ToolResult::error("WRITE_FAILED", "could not write workflow");
        }
        cJSON* out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "path", path.c_str());
        cJSON_AddBoolToObject(out, "written", true);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
};

class WorkflowWaitTool final : public ITool {
public:
    const char* name() const override { return "workflow.wait"; }
    const char* title() const override { return "Workflow Wait"; }
    const char* description() const override { return "Wait for a short duration inside a workflow."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","properties":{"ms":{"type":"integer","minimum":0,"maximum":10000,"default":250}}})";
    }
    uint32_t timeoutMs() const override { return 11000; }
    ToolResult invoke(const cJSON* args) override {
        int ms = jsonInt(args, "ms", 250);
        if (ms < 0) ms = 0;
        if (ms > 10000) ms = 10000;
        sleepMs(static_cast<uint32_t>(ms));
        cJSON* out = cJSON_CreateObject();
        cJSON_AddNumberToObject(out, "waited_ms", ms);
        return ToolResult::success(out);
    }
};

class WorkflowValidateTool final : public ITool {
public:
    WorkflowValidateTool(IHardware& hardware, ToolRegistry& registry) : hardware_(hardware), registry_(registry) {}
    const char* name() const override { return "workflow.validate"; }
    const char* title() const override { return "Validate Workflow"; }
    const char* description() const override { return "Validate workflow structure and report missing or dangerous tools without running it."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","properties":{"name":{"type":"string","description":"Stored workflow name under /stackyan/workflows"},"workflow":{"type":"object","description":"Inline workflow object"}}})";
    }
    ToolResult invoke(const cJSON* args) override {
        cJSON* ownedWorkflow = nullptr;
        const cJSON* workflow = cJSON_GetObjectItemCaseSensitive(args, "workflow");
        const char* requestedName = jsonString(args, "name");
        std::string path;
        if (!cJSON_IsObject(workflow)) {
            path = workflowPath(requestedName);
            if (path.empty()) return ToolResult::error("INVALID_ARGUMENT", "name or workflow object is required");
            ownedWorkflow = readJsonFile(hardware_.storage(), path.c_str());
            workflow = ownedWorkflow;
        }
        if (!cJSON_IsObject(workflow)) {
            if (ownedWorkflow) cJSON_Delete(ownedWorkflow);
            return ToolResult::error("READ_FAILED", "could not read valid workflow JSON");
        }

        cJSON* out = cJSON_CreateObject();
        if (!path.empty()) cJSON_AddStringToObject(out, "path", path.c_str());
        cJSON* issues = cJSON_AddArrayToObject(out, "issues");
        cJSON* stepsOut = cJSON_AddArrayToObject(out, "steps");
        bool valid = true;
        bool hasDangerous = false;
        const cJSON* steps = cJSON_GetObjectItemCaseSensitive(workflow, "steps");
        if (!cJSON_IsArray(steps)) {
            valid = false;
            cJSON_AddItemToArray(issues, cJSON_CreateString("workflow.steps array is required"));
        } else {
            int index = 0;
            const cJSON* step = nullptr;
            cJSON_ArrayForEach(step, steps) {
                cJSON* stepOut = cJSON_CreateObject();
                cJSON_AddNumberToObject(stepOut, "index", index);
                const char* toolName = jsonString(step, "tool");
                cJSON_AddStringToObject(stepOut, "tool", toolName ? toolName : "");
                if (!cJSON_IsObject(step) || !toolName || !toolName[0]) {
                    valid = false;
                    cJSON_AddStringToObject(stepOut, "issue", "step.tool is required");
                } else if (std::strcmp(toolName, "workflow.run") == 0) {
                    valid = false;
                    cJSON_AddStringToObject(stepOut, "issue", "recursive workflow.run is not allowed");
                } else {
                    const ITool* tool = registry_.find(toolName);
                    if (!tool) {
                        valid = false;
                        cJSON_AddStringToObject(stepOut, "issue", "tool not found");
                    } else {
                        cJSON_AddBoolToObject(stepOut, "dangerous", tool->dangerous());
                        if (tool->dangerous()) hasDangerous = true;
                    }
                }
                cJSON_AddItemToArray(stepsOut, stepOut);
                ++index;
            }
        }
        cJSON_AddBoolToObject(out, "valid", valid);
        cJSON_AddBoolToObject(out, "has_dangerous_tools", hasDangerous);
        if (ownedWorkflow) cJSON_Delete(ownedWorkflow);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
    ToolRegistry& registry_;
};

class WorkflowRunTool final : public ITool {
public:
    WorkflowRunTool(IHardware& hardware, ToolRegistry& registry, EventBus& events) : hardware_(hardware), registry_(registry), events_(events) {}
    const char* name() const override { return "workflow.run"; }
    const char* title() const override { return "Run Workflow"; }
    const char* description() const override { return "Run a simple sequential JSON workflow through ToolRegistry."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","properties":{"name":{"type":"string","description":"Stored workflow name under /stackyan/workflows"},"workflow":{"type":"object","description":"Inline workflow object"},"stop_on_error":{"type":"boolean","default":true},"allow_dangerous":{"type":"boolean","default":false}}})";
    }
    bool dangerous() const override { return true; }
    uint32_t timeoutMs() const override { return 10000; }
    ToolResult invoke(const cJSON* args) override {
        cJSON* ownedWorkflow = nullptr;
        const cJSON* workflow = cJSON_GetObjectItemCaseSensitive(args, "workflow");
        const char* requestedName = jsonString(args, "name");
        std::string path;
        if (!cJSON_IsObject(workflow)) {
            path = workflowPath(requestedName);
            if (path.empty()) return ToolResult::error("INVALID_ARGUMENT", "name or workflow object is required");
            ownedWorkflow = readJsonFile(hardware_.storage(), path.c_str());
            workflow = ownedWorkflow;
        }
        if (!cJSON_IsObject(workflow)) {
            if (ownedWorkflow) cJSON_Delete(ownedWorkflow);
            return ToolResult::error("READ_FAILED", "could not read valid workflow JSON");
        }

        const cJSON* steps = cJSON_GetObjectItemCaseSensitive(workflow, "steps");
        if (!cJSON_IsArray(steps)) {
            if (ownedWorkflow) cJSON_Delete(ownedWorkflow);
            return ToolResult::error("INVALID_ARGUMENT", "workflow.steps array is required");
        }

        const bool stopOnError = cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(args, "stop_on_error")) ? false : true;
        const bool allowDangerous = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(args, "allow_dangerous"));
        cJSON* out = cJSON_CreateObject();
        if (!path.empty()) cJSON_AddStringToObject(out, "path", path.c_str());
        cJSON_AddBoolToObject(out, "stop_on_error", stopOnError);
        cJSON_AddBoolToObject(out, "allow_dangerous", allowDangerous);
        cJSON* results = cJSON_AddArrayToObject(out, "steps");
        cJSON* startPayload = cJSON_CreateObject();
        if (!path.empty()) cJSON_AddStringToObject(startPayload, "path", path.c_str());
        events_.publish("workflow.started", "workflow.run", startPayload);
        cJSON_Delete(startPayload);
        bool allOk = true;
        int index = 0;
        const cJSON* step = nullptr;
        cJSON_ArrayForEach(step, steps) {
            cJSON* stepOut = cJSON_CreateObject();
            cJSON_AddNumberToObject(stepOut, "index", index);
            const char* toolName = jsonString(step, "tool");
            cJSON_AddStringToObject(stepOut, "tool", toolName ? toolName : "");
            if (!cJSON_IsObject(step) || !toolName || !toolName[0]) {
                allOk = false;
                cJSON_AddBoolToObject(stepOut, "ok", false);
                cJSON_AddStringToObject(stepOut, "error", "step.tool is required");
                cJSON_AddItemToArray(results, stepOut);
                if (stopOnError) break;
                ++index;
                continue;
            }
            if (std::strcmp(toolName, "workflow.run") == 0) {
                allOk = false;
                cJSON_AddBoolToObject(stepOut, "ok", false);
                cJSON_AddStringToObject(stepOut, "error", "recursive workflow.run is not allowed");
                cJSON_AddItemToArray(results, stepOut);
                if (stopOnError) break;
                ++index;
                continue;
            }
            ITool* tool = registry_.find(toolName);
            if (!tool) {
                allOk = false;
                cJSON_AddBoolToObject(stepOut, "ok", false);
                cJSON_AddStringToObject(stepOut, "error", "tool not found");
                cJSON_AddItemToArray(results, stepOut);
                if (stopOnError) break;
                ++index;
                continue;
            }
            if (tool->dangerous() && !allowDangerous) {
                allOk = false;
                cJSON_AddBoolToObject(stepOut, "ok", false);
                cJSON_AddBoolToObject(stepOut, "dangerous", true);
                cJSON_AddStringToObject(stepOut, "error", "dangerous tool requires allow_dangerous=true");
                cJSON* eventPayload = cJSON_CreateObject();
                cJSON_AddNumberToObject(eventPayload, "index", index);
                cJSON_AddStringToObject(eventPayload, "tool", toolName);
                cJSON_AddBoolToObject(eventPayload, "ok", false);
                cJSON_AddStringToObject(eventPayload, "blocked", "dangerous");
                events_.publish("workflow.step", "workflow.run", eventPayload);
                cJSON_Delete(eventPayload);
                cJSON_AddItemToArray(results, stepOut);
                if (stopOnError) break;
                ++index;
                continue;
            }
            const cJSON* stepArgs = cJSON_GetObjectItemCaseSensitive(step, "args");
            cJSON* invokeArgs = stepArgs ? cJSON_Duplicate(stepArgs, true) : cJSON_CreateObject();
            ToolResult result = tool->invoke(invokeArgs);
            cJSON_Delete(invokeArgs);
            cJSON_AddBoolToObject(stepOut, "ok", result.ok);
            cJSON* eventPayload = cJSON_CreateObject();
            cJSON_AddNumberToObject(eventPayload, "index", index);
            cJSON_AddStringToObject(eventPayload, "tool", toolName);
            cJSON_AddBoolToObject(eventPayload, "ok", result.ok);
            events_.publish("workflow.step", "workflow.run", eventPayload);
            cJSON_Delete(eventPayload);
            if (result.ok) {
                cJSON_AddItemToObject(stepOut, "result", cJSON_Duplicate(result.result, true));
            } else {
                allOk = false;
                cJSON_AddStringToObject(stepOut, "error_code", result.errorCode.c_str());
                cJSON_AddStringToObject(stepOut, "error_message", result.errorMessage.c_str());
            }
            cJSON_AddItemToArray(results, stepOut);
            if (!result.ok && stopOnError) break;
            ++index;
        }
        cJSON_AddBoolToObject(out, "ok", allOk);
        cJSON* finishPayload = cJSON_CreateObject();
        if (!path.empty()) cJSON_AddStringToObject(finishPayload, "path", path.c_str());
        cJSON_AddBoolToObject(finishPayload, "ok", allOk);
        cJSON_AddNumberToObject(finishPayload, "step_count", cJSON_GetArraySize(results));
        events_.publish("workflow.finished", "workflow.run", finishPayload);
        cJSON_Delete(finishPayload);
        if (ownedWorkflow) cJSON_Delete(ownedWorkflow);
        return ToolResult::success(out);
    }
private:
    IHardware& hardware_;
    ToolRegistry& registry_;
    EventBus& events_;
};

class FaceGetStateTool final : public ITool {
public:
    explicit FaceGetStateTool(AvatarService& avatar) : avatar_(avatar) {}
    const char* name() const override { return "face.getState"; }
    const char* title() const override { return "Get Face State"; }
    const char* description() const override { return "Read the current face/avatar state. Rendering is attached later by the device avatar layer."; }
    const char* parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    ToolResult invoke(const cJSON*) override {
        return ToolResult::success(avatar_.stateJson());
    }
private:
    AvatarService& avatar_;
};

class FaceSetExpressionTool final : public ITool {
public:
    explicit FaceSetExpressionTool(AvatarService& avatar) : avatar_(avatar) {}
    const char* name() const override { return "face.setExpression"; }
    const char* title() const override { return "Set Face Expression"; }
    const char* description() const override { return "Set the face expression state used by the avatar renderer."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["expression"],"properties":{"expression":{"type":"string","enum":["Neutral","Happy","Joy","Angry","Sad","Curious","Surprised","Shy","Thinking","Wink","Talking","Love","Panic","Proud","Sigh","Mischief","Cold","Sleepy"],"default":"Neutral"}}})";
    }
    ToolResult invoke(const cJSON* args) override {
        const char* expression = jsonString(args, "expression");
        if (!AvatarService::isValidExpression(expression)) {
            return ToolResult::error("INVALID_ARGUMENT", "expression is required and must be a supported expression");
        }
        avatar_.setExpression(expression);
        cJSON* out = avatar_.stateJson();
        cJSON_AddBoolToObject(out, "applied", true);
        return ToolResult::success(out);
    }
private:
    AvatarService& avatar_;
};

class FaceSetVowelTool final : public ITool {
public:
    explicit FaceSetVowelTool(AvatarService& avatar) : avatar_(avatar) {}
    const char* name() const override { return "face.setVowel"; }
    const char* title() const override { return "Set Face Vowel"; }
    const char* description() const override { return "Set the current mouth vowel state for avatar lip-sync tests."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["vowel"],"properties":{"vowel":{"type":"string","enum":["Off","Closed","A","I","U","E","O"],"default":"Off"}}})";
    }
    ToolResult invoke(const cJSON* args) override {
        const char* vowel = jsonString(args, "vowel");
        if (!AvatarService::isValidVowel(vowel)) {
            return ToolResult::error("INVALID_ARGUMENT", "vowel is required and must be a supported vowel");
        }
        avatar_.setVowel(vowel);
        cJSON* out = avatar_.stateJson();
        cJSON_AddBoolToObject(out, "applied", true);
        return ToolResult::success(out);
    }
private:
    AvatarService& avatar_;
};

class FaceSetCaptionTool final : public ITool {
public:
    explicit FaceSetCaptionTool(AvatarService& avatar) : avatar_(avatar) {}
    const char* name() const override { return "face.setCaption"; }
    const char* title() const override { return "Set Face Caption"; }
    const char* description() const override { return "Set up to two caption lines for the avatar renderer."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","properties":{"line1":{"type":"string","default":"StackYan"},"line2":{"type":"string","default":"Tool Server"}}})";
    }
    ToolResult invoke(const cJSON* args) override {
        avatar_.setCaption(jsonString(args, "line1", ""), jsonString(args, "line2", ""));
        cJSON* out = avatar_.stateJson();
        cJSON_AddBoolToObject(out, "applied", true);
        return ToolResult::success(out);
    }
private:
    AvatarService& avatar_;
};

class FaceClearCaptionTool final : public ITool {
public:
    explicit FaceClearCaptionTool(AvatarService& avatar) : avatar_(avatar) {}
    const char* name() const override { return "face.clearCaption"; }
    const char* title() const override { return "Clear Face Caption"; }
    const char* description() const override { return "Clear avatar caption text."; }
    const char* parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    ToolResult invoke(const cJSON*) override {
        avatar_.clearCaption();
        cJSON* out = avatar_.stateJson();
        cJSON_AddBoolToObject(out, "applied", true);
        return ToolResult::success(out);
    }
private:
    AvatarService& avatar_;
};

class FaceSetPaletteTool final : public ITool {
public:
    explicit FaceSetPaletteTool(AvatarService& avatar) : avatar_(avatar) {}
    const char* name() const override { return "face.setPalette"; }
    const char* title() const override { return "Set Face Palette"; }
    const char* description() const override { return "Set the face palette hint for the avatar renderer."; }
    const char* parametersSchema() const override {
        return R"({"type":"object","required":["palette"],"properties":{"palette":{"type":"string","enum":["default","warm","cool","mono","night"],"default":"default"}}})";
    }
    ToolResult invoke(const cJSON* args) override {
        const char* palette = jsonString(args, "palette");
        if (!AvatarService::isValidPalette(palette)) {
            return ToolResult::error("INVALID_ARGUMENT", "palette is required and must be a supported palette");
        }
        avatar_.setPalette(palette);
        cJSON* out = avatar_.stateJson();
        cJSON_AddBoolToObject(out, "applied", true);
        return ToolResult::success(out);
    }
private:
    AvatarService& avatar_;
};

class FaceResetTool final : public ITool {
public:
    explicit FaceResetTool(AvatarService& avatar) : avatar_(avatar) {}
    const char* name() const override { return "face.reset"; }
    const char* title() const override { return "Reset Face"; }
    const char* description() const override { return "Reset face state to the neutral default."; }
    const char* parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    ToolResult invoke(const cJSON*) override {
        avatar_.reset();
        cJSON* out = avatar_.stateJson();
        cJSON_AddBoolToObject(out, "applied", true);
        return ToolResult::success(out);
    }
private:
    AvatarService& avatar_;
};

}  // namespace

void registerBuiltinTools(ToolRegistry& registry, IHardware& hardware, AvatarService& avatar, EventBus& events) {
    registry.registerTool(std::make_unique<RgbSetTool>(hardware));
    registry.registerTool(std::make_unique<RgbClearTool>(hardware));
    registry.registerTool(std::make_unique<ServoMoveTool>(hardware));
    registry.registerTool(std::make_unique<ServoHomeTool>(hardware));
    registry.registerTool(std::make_unique<ServoReadStateTool>(hardware));
    registry.registerTool(std::make_unique<ServoDisableTorqueTool>(hardware));
    registry.registerTool(std::make_unique<ServoStopTool>(hardware));
    registry.registerTool(std::make_unique<MotionReadTool>(hardware));
    registry.registerTool(std::make_unique<PowerStatusTool>(hardware));
    registry.registerTool(std::make_unique<StorageInfoTool>(hardware));
    registry.registerTool(std::make_unique<StorageExistsTool>(hardware));
    registry.registerTool(std::make_unique<StorageListTool>(hardware));
    registry.registerTool(std::make_unique<StorageMkdirTool>(hardware));
    registry.registerTool(std::make_unique<StorageReadTool>(hardware));
    registry.registerTool(std::make_unique<StorageWriteTool>(hardware));
    registry.registerTool(std::make_unique<StorageRemoveTool>(hardware));
    registry.registerTool(std::make_unique<ConfigReadTool>(hardware));
    registry.registerTool(std::make_unique<ConfigWriteTool>(hardware));
    registry.registerTool(std::make_unique<ConfigPatchTool>(hardware));
    registry.registerTool(std::make_unique<ConfigApplyTool>(hardware, avatar));
    registry.registerTool(std::make_unique<WorkflowListTool>(hardware));
    registry.registerTool(std::make_unique<WorkflowReadTool>(hardware));
    registry.registerTool(std::make_unique<WorkflowWriteTool>(hardware));
    registry.registerTool(std::make_unique<WorkflowWaitTool>());
    registry.registerTool(std::make_unique<WorkflowValidateTool>(hardware, registry));
    registry.registerTool(std::make_unique<WorkflowRunTool>(hardware, registry, events));
    registry.registerTool(std::make_unique<FaceGetStateTool>(avatar));
    registry.registerTool(std::make_unique<FaceSetExpressionTool>(avatar));
    registry.registerTool(std::make_unique<FaceSetVowelTool>(avatar));
    registry.registerTool(std::make_unique<FaceSetCaptionTool>(avatar));
    registry.registerTool(std::make_unique<FaceClearCaptionTool>(avatar));
    registry.registerTool(std::make_unique<FaceSetPaletteTool>(avatar));
    registry.registerTool(std::make_unique<FaceResetTool>(avatar));
}

}  // namespace stackyan
