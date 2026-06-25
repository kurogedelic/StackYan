#include "ApiServer.h"
#include "AvatarService.h"
#include "BuiltinTools.h"
#include "EventBus.h"
#include "EspHardware.h"
#include "Hal.h"
#include "NetworkManager.h"
#include "StorageLayout.h"
#include "ToolRegistry.h"

#include "esp_log.h"
#include "nvs_flash.h"

namespace {
constexpr const char* kTag = "stackyan";
}

extern "C" void app_main() {
    ESP_LOGI(kTag, "StackYan booting...");

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    static stackchu::Hal hal;
    const bool halAnyOk = hal.begin();
    ESP_LOGI(kTag, "HAL begin any_ok=%s", halAnyOk ? "true" : "false");

    stackyan::ensureStorageLayout(hal.storage());

    static stackyan::EventBus events;
    events.publish("system.boot", "StackYan");

    static stackyan::ToolRegistry registry;
    static stackyan::EspHardware hardware(hal);
    static stackyan::AvatarService avatar;
    stackyan::registerBuiltinTools(registry, hardware, avatar, events);
    ESP_LOGI(kTag, "registered %u tools", static_cast<unsigned>(registry.count()));

    static stackyan::NetworkManager network;
    network.begin();
    events.publish("wifi.ap.started", "NetworkManager");

    static stackyan::ApiServer api(hal, registry, events);
    api.begin();

    ESP_LOGI(kTag, "StackYan ready: http://stackyan.local/");
}
