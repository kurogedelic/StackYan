#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ITool.h"

namespace stackyan {

class ToolRegistry {
public:
    bool registerTool(std::unique_ptr<ITool> tool);
    ITool* find(const std::string& name);
    const ITool* find(const std::string& name) const;
    std::vector<const ITool*> all() const;
    size_t count() const;

    cJSON* toJson() const;
    cJSON* toolToJson(const ITool& tool) const;

private:
    std::vector<std::unique_ptr<ITool>> tools_;
};

}  // namespace stackyan
