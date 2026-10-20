#pragma once

// Latest GPS position and UTC time seen on the NMEA 2000 bus (from any GNSS
// device, e.g. the chartplotter): PGN 129029, 129025 and 126992.  Written by the
// N2K task, read by the web server, so everything is behind a mutex.
// A value counts as current for GPS_MAX_AGE_MS after it was last received.

#include <stdint.h>
#include <stdio.h>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

namespace GpsState {

static const uint32_t GPS_MAX_AGE_MS = 10000;

struct Snapshot {
    bool     havePos = false;
    double   lat = 0.0, lon = 0.0;       // degrees, + = N / E
    bool     haveTime = false;
    uint16_t days = 0;                   // days since 1970-01-01 (UTC)
    double   secs = 0.0;                 // seconds since midnight (UTC)
};

static SemaphoreHandle_t s_mutex = nullptr;
static Snapshot s_snap;
static uint32_t s_posMs = 0, s_timeMs = 0;

inline void init() {
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
}

static inline uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

inline void updatePosition(double lat, double lon) {
    if (!s_mutex || !std::isfinite(lat) || !std::isfinite(lon) || std::fabs(lat) > 90.0 || std::fabs(lon) > 180.0) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    s_snap.havePos = true;
    s_snap.lat = lat;
    s_snap.lon = lon;
    s_posMs = nowMs();
    xSemaphoreGive(s_mutex);
}

inline void updateTime(uint16_t days, double secs) {
    if (!s_mutex || !std::isfinite(secs) || secs < 0.0 || secs >= 86400.0) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    s_snap.haveTime = true;
    s_snap.days = days;
    s_snap.secs = secs;
    s_timeMs = nowMs();
    xSemaphoreGive(s_mutex);
}

// Position and time as last received; havePos/haveTime are false when nothing
// arrived in the last GPS_MAX_AGE_MS.  The time is advanced by the age of the
// message so the clock keeps running between the (1 Hz) updates.
inline Snapshot snapshot() {
    Snapshot out;
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return out;
    uint32_t now = nowMs();
    out = s_snap;
    if (out.havePos && now - s_posMs > GPS_MAX_AGE_MS) out.havePos = false;
    if (out.haveTime) {
        uint32_t age = now - s_timeMs;
        if (age > GPS_MAX_AGE_MS) {
            out.haveTime = false;
        } else {
            out.secs += age / 1000.0;
            if (out.secs >= 86400.0) { out.secs -= 86400.0; out.days++; }
        }
    }
    xSemaphoreGive(s_mutex);
    return out;
}

// "YYYY-MM-DD HH:MM:SS" (UTC) from days since 1970-01-01 and seconds of day.
inline void formatTime(uint16_t days, double secs, char *out, size_t cap) {
    // Civil-from-days (Howard Hinnant): proleptic Gregorian calendar.
    int32_t z = (int32_t)days + 719468;
    int32_t era = z / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t y = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t d = doy - (153 * mp + 2) / 5 + 1;
    uint32_t m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    uint32_t s = (uint32_t)secs;
    snprintf(out, cap, "%04d-%02u-%02u %02u:%02u:%02u", (int)y, (unsigned)m, (unsigned)d,
             (unsigned)(s / 3600), (unsigned)(s % 3600 / 60), (unsigned)(s % 60));
}

} // namespace GpsState
