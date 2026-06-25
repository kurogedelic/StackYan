#pragma once

#include "IHardware.h"
#include "ToolRegistry.h"

namespace stackyan {

class AvatarService;
class EventBus;

void registerBuiltinTools(ToolRegistry& registry, IHardware& hardware, AvatarService& avatar, EventBus& events);

}  // namespace stackyan
