#include "StorageLayout.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"

namespace stackyan {
namespace {
constexpr const char* kTag = "stackyan_storage";
}

bool ensureStorageLayout(stackchu::IStorage& storage) {
    if (!storage.isMounted()) {
        ESP_LOGW(kTag, "storage is not mounted; StackYan layout skipped");
        return false;
    }

    bool ok = true;
    ok = storage.mkdir("/stackyan") && ok;
    ok = storage.mkdir("/stackyan/tools") && ok;
    ok = storage.mkdir("/stackyan/workflows") && ok;
    ok = storage.mkdir("/stackyan/logs") && ok;
    ok = storage.mkdir("/stackyan/memory") && ok;

    if (!storage.exists("/stackyan/config.json")) {
        const char* initial =
            "{\n"
            "  \"device_name\": \"stackyan\",\n"
            "  \"version\": 1,\n"
            "  \"network\": {\n"
            "    \"hostname\": \"stackyan\"\n"
            "  },\n"
            "  \"avatar\": {\n"
            "    \"expression\": \"Neutral\",\n"
            "    \"vowel\": \"Off\",\n"
            "    \"palette\": \"default\"\n"
            "  },\n"
            "  \"storage\": {\n"
            "    \"backend\": \"internal_flash_fatfs\"\n"
            "  }\n"
            "}\n";
        ok = storage.writeText("/stackyan/config.json", initial) && ok;
    }

    ESP_LOGI(kTag, "storage layout %s", ok ? "ready" : "incomplete");
    return ok;
}

void appendToolLog(stackchu::IStorage& storage, const char* toolName, bool ok, int elapsedMs) {
    if (!storage.isMounted()) return;
    char line[192];
    std::snprintf(line, sizeof(line), "tool=%s ok=%s elapsed_ms=%d\n",
                  toolName ? toolName : "(unknown)", ok ? "true" : "false", elapsedMs);
    stackchu::FileHandle h = storage.open("/stackyan/logs/tool.log", stackchu::OpenMode::Append);
    if (!h.valid()) return;
    storage.write(h, line, std::strlen(line));
    storage.close(h);
}

}  // namespace stackyan
