#include "ToolRegistry.h"

#include <algorithm>

namespace stackyan {

bool ToolRegistry::registerTool(std::unique_ptr<ITool> tool) {
    if (!tool || !tool->name() || std::string(tool->name()).empty()) return false;
    if (find(tool->name()) != nullptr) return false;
    tools_.push_back(std::move(tool));
    return true;
}

ITool* ToolRegistry::find(const std::string& name) {
    for (auto& tool : tools_) {
        if (name == tool->name()) return tool.get();
    }
    return nullptr;
}

const ITool* ToolRegistry::find(const std::string& name) const {
    for (const auto& tool : tools_) {
        if (name == tool->name()) return tool.get();
    }
    return nullptr;
}

std::vector<const ITool*> ToolRegistry::all() const {
    std::vector<const ITool*> out;
    out.reserve(tools_.size());
    for (const auto& tool : tools_) out.push_back(tool.get());
    std::sort(out.begin(), out.end(), [](const ITool* a, const ITool* b) {
        return std::string(a->name()) < std::string(b->name());
    });
    return out;
}

size_t ToolRegistry::count() const {
    return tools_.size();
}

cJSON* ToolRegistry::toolToJson(const ITool& tool) const {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name", tool.name());
    cJSON_AddStringToObject(obj, "title", tool.title());
    cJSON_AddStringToObject(obj, "description", tool.description());
    cJSON_AddBoolToObject(obj, "dangerous", tool.dangerous());
    cJSON_AddNumberToObject(obj, "timeout_ms", tool.timeoutMs());

    cJSON* schema = cJSON_Parse(tool.parametersSchema());
    if (!schema) schema = cJSON_CreateObject();
    cJSON_AddItemToObject(obj, "parameters", schema);
    return obj;
}

cJSON* ToolRegistry::toJson() const {
    cJSON* root = cJSON_CreateObject();
    cJSON* tools = cJSON_AddArrayToObject(root, "tools");
    for (const ITool* tool : all()) {
        cJSON_AddItemToArray(tools, toolToJson(*tool));
    }
    return root;
}

}  // namespace stackyan
