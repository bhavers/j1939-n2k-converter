#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "config.h"
#include "wifi_manager.h"
#include "ota_manager.h"
#include "fuel_service.h"
#include "gps_state.h"
#include "j1939_receiver.h"
#include "n2k_gateway.h"
#include "fuel_web.h"
#include "web_ui.h"

extern "C" void app_main(void) {
    OTAManager::mark_app_valid();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    FuelService::init();
    GpsState::init();

    // Wi-Fi must be initialised before the CAN tasks start: esp_wifi_init()
    // registers high-priority interrupt handlers and runs ROM routines that
    // temporarily disable interrupts.  Without it the N2K task loop does not
    // yield long enough and triggers an Interrupt WDT crash.
    // When WIFI_ENABLED=0 we still call esp_wifi_init() for the side-effects
    // but skip the radio, AP, STA and HTTP server entirely.
#if WIFI_ENABLED
    if (NetworkManager::init_ap()) {
        RemoteLogger::init();
        if (OTAManager::init_server()) {
            FuelWeb::register_handlers(OTAManager::s_httpd);
            WebUI::register_handlers(OTAManager::s_httpd);
        }
    }
#else
    {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_LOGI("MAIN", "Wi-Fi disabled (WIFI_ENABLED=0) — radio not started");
    }
#endif

    // Initialise the N2K TWAI node (CANOpen) first, but do NOT start the
    // ParseMessages() task yet.  Then start the J1939 node.  Only after both
    // TWAI nodes are fully up (their PERIPH_RCC_ATOMIC critical sections in
    // twai_node_enable() are done) do we start the N2K task — preventing the
    // shared periph spinlock from being contested between the task loop and the
    // J1939 ISR on this unicore ESP32-C5.
    if (!N2KGateway::init()) ESP_LOGE("MAIN", "NMEA 2000 gateway did not start");
    if (!J1939Receiver::init()) ESP_LOGE("MAIN", "J1939 receiver did not start");
    if (!N2KGateway::startTask()) ESP_LOGE("MAIN", "NMEA 2000 task did not start");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
