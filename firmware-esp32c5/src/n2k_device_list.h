#pragma once

// Keeps track of all devices on the NMEA 2000 bus using the library's
// tN2kDeviceList helper.  Call init() once with the NMEA2000 instance,
// then call poll() from the gateway task each time ParseMessages() has run.
// Whenever a new device appears (or its product info is resolved) a line is
// written to RemoteLogger and ESP_LOG so it shows up on the USB serial monitor.

#include "N2kDeviceList.h"
#include "NMEA2000.h"
#include "esp_log.h"
#include "remote_logger.h"

namespace N2kDeviceTracker {

static const char *TAG = "N2K_DEV";
static tN2kDeviceList *s_list = nullptr;

inline void init(tNMEA2000 *n2k) {
    s_list = new tN2kDeviceList(n2k);
}

// Call after every ParseMessages(). Logs each device when the list changes.
inline void poll() {
    if (!s_list || !s_list->ReadResetIsListUpdated()) return;

    uint8_t count = s_list->Count();
    RemoteLogger::logf("[N2K_DEV] Bus device list updated (%u device(s)):\r\n", (unsigned)count);
    ESP_LOGI(TAG, "Bus device list updated (%u device(s)):", (unsigned)count);

    for (int src = 0; src < 254; src++) {
        const tNMEA2000::tDevice *dev = s_list->FindDeviceBySource((uint8_t)src);
        if (!dev) continue;

        const char *modelID   = dev->GetModelID();
        const char *swCode    = dev->GetSwCode();
        const char *serial    = dev->GetModelSerialCode();
        uint16_t    mfr       = dev->GetManufacturerCode();
        uint8_t     devClass  = dev->GetDeviceClass();
        uint8_t     devFunc   = dev->GetDeviceFunction();

        // modelID may be nullptr if product info has not yet arrived
        if (modelID && modelID[0] != '\0') {
            RemoteLogger::logf("  [%3d] mfr=%u class=%u func=%u  \"%s\"  sw=%s  serial=%s\r\n",
                               src, (unsigned)mfr, (unsigned)devClass, (unsigned)devFunc,
                               modelID, swCode ? swCode : "-", serial ? serial : "-");
            ESP_LOGI(TAG, "  [%3d] mfr=%u class=%u func=%u  \"%s\"  sw=%s  serial=%s",
                     src, (unsigned)mfr, (unsigned)devClass, (unsigned)devFunc,
                     modelID, swCode ? swCode : "-", serial ? serial : "-");
        } else {
            RemoteLogger::logf("  [%3d] mfr=%u class=%u func=%u  (product info pending)\r\n",
                               src, (unsigned)mfr, (unsigned)devClass, (unsigned)devFunc);
            ESP_LOGI(TAG, "  [%3d] mfr=%u class=%u func=%u  (product info pending)",
                     src, (unsigned)mfr, (unsigned)devClass, (unsigned)devFunc);
        }
    }
}

} // namespace N2kDeviceTracker
