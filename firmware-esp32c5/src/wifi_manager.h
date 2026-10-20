#pragma once

// Wi-Fi strategy: softAP-only — the gateway creates its own network (AP_SSID
// from wifi_secrets.h, WPA2/WPA3) and does not join any other network.  Connect
// the Mac to that network; the gateway is then at 192.168.4.1.  The HTTP server
// and TCP log listen on the AP interface.

#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "config.h"

namespace NetworkManager {

static const char *TAG = "WIFI_MGR";

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* e = (wifi_event_ap_staconnected_t*)event_data;
        ESP_LOGI(TAG, "AP: client connected, MAC %02x:%02x:%02x:%02x:%02x:%02x",
                 e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5]);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "AP: client disconnected");
    }
}

// Starts the softAP. Returns true when the AP is up.
inline bool init_ap() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &event_handler, NULL, NULL));

    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid,     AP_SSID,     sizeof(ap_config.ap.ssid) - 1);
    strncpy((char*)ap_config.ap.password, AP_PASSWORD, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.ssid_len       = strlen(AP_SSID);
    ap_config.ap.channel        = AP_CHANNEL;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode       = WIFI_AUTH_WPA2_WPA3_PSK;
    ap_config.ap.pmf_cfg.capable  = true;
    ap_config.ap.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP \"%s\" started on channel %d, gateway at 192.168.4.1", AP_SSID, AP_CHANNEL);
    return true;
}

} // namespace NetworkManager
