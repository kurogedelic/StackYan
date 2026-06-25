#pragma once

#include "hal/IStorage.h"

namespace stackyan {

bool ensureStorageLayout(stackchu::IStorage& storage);
void appendToolLog(stackchu::IStorage& storage, const char* toolName, bool ok, int elapsedMs);

}  // namespace stackyan
