#include "NetworkManager.h"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"

namespace stackyan {
namespace {
constexpr const char* kTag = "stackyan_net";
}

bool NetworkManager::begin() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {};
    std::strncpy(reinterpret_cast<char*>(ap_config.ap.ssid), apSsid(), sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = std::strlen(apSsid());
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // v1 is intentionally unauthenticated and intended only for trusted LAN/dev use.
    ESP_LOGW(kTag, "starting open setup AP '%s'; use only on trusted local networks", apSsid());

    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        mdns_hostname_set(hostname());
        mdns_instance_name_set("StackYan Tool Server");
        mdns_service_add("StackYan HTTP", "_http", "_tcp", 80, nullptr, 0);
        ESP_LOGI(kTag, "mDNS hostname: %s.local", hostname());
    } else {
        ESP_LOGW(kTag, "mDNS init failed: %s", esp_err_to_name(err));
    }
    return true;
}

}  // namespace stackyan
