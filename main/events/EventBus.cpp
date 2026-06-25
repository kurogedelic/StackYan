#include "EventBus.h"

#ifdef STACKYAN_SIMULATOR
#include <chrono>
#else
#include "esp_timer.h"
#endif

namespace stackyan {
namespace {
constexpr size_t kMaxEvents = 32;
}

void EventBus::publish(const char* type, const char* source, cJSON* payload) {
    EventRecord event;
#ifdef STACKYAN_SIMULATOR
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    event.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
#else
    event.timestampMs = esp_timer_get_time() / 1000;
#endif
    event.type = type ? type : "event";
    event.source = source ? source : "stackyan";

    char* json = payload ? cJSON_PrintUnformatted(payload) : nullptr;
    event.payloadJson = json ? json : "{}";
    if (json) cJSON_free(json);

    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.size() >= kMaxEvents) events_.erase(events_.begin());
    events_.push_back(std::move(event));
}

cJSON* EventBus::toJson() const {
    cJSON* root = cJSON_CreateObject();
    cJSON* events = cJSON_AddArrayToObject(root, "events");

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& event : events_) {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "timestamp_ms", event.timestampMs);
        cJSON_AddStringToObject(obj, "type", event.type.c_str());
        cJSON_AddStringToObject(obj, "source", event.source.c_str());
        cJSON* payload = cJSON_Parse(event.payloadJson.c_str());
        if (!payload) payload = cJSON_CreateObject();
        cJSON_AddItemToObject(obj, "payload", payload);
        cJSON_AddItemToArray(events, obj);
    }
    return root;
}

}  // namespace stackyan
