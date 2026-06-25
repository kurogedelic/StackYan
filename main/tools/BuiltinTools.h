#pragma once

#include "IHardware.h"
#include "ToolRegistry.h"

namespace stackyan {

void registerBuiltinTools(ToolRegistry& registry, IHardware& hardware);

}  // namespace stackyan
