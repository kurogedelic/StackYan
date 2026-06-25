#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "cJSON.h"

namespace stackyan {

struct EventRecord {
    uint64_t timestampMs = 0;
    std::string type;
    std::string source;
    std::string payloadJson;
};

class EventBus {
public:
    void publish(const char* type, const char* source, cJSON* payload = nullptr);
    cJSON* toJson() const;

private:
    mutable std::mutex mutex_;
    std::vector<EventRecord> events_;
};

}  // namespace stackyan
