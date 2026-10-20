#pragma once

// Fuel-rate estimation service: holds the editable RPM -> L/h table and the trip
// totals, persists both in NVS, and integrates fuel use once per second.
// Maths lives in fuel_model.h (host-tested). See plan Sub-Task 6B.

#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "engine_state.h"
#include "fuel_model.h"
#include "remote_logger.h"

namespace FuelService {

static const char *TAG = "FUEL";
static const char *NVS_NS = "fuel";
static const char *KEY_CURVE = "curve";
static const char *KEY_TRIP = "trip";
static const char *KEY_NEWTRIP = "newtrip_min";
static const int64_t SAVE_INTERVAL_US = 60LL * 1000000LL;
static const double RAD_S_PER_RPM = 0.104719755;

struct Snapshot {
    double rpm;                  // 0 when the engine is not reporting
    double rateLph;              // estimated fuel rate
    bool running;                // rpm >= FuelModel::RUNNING_RPM
    FuelModel::Trip trip;
    double avgLph;               // FuelModel::NA when not enough data
    FuelModel::Curve curve;
    uint32_t newTripMin;         // engine-off minutes after which the next start is a new trip (0 = never)
    double offSeconds;           // engine-off time so far (RAM only, restarts at 0 on power-up)
};

static SemaphoreHandle_t s_mutex = nullptr;
static FuelModel::Curve s_curve;
static FuelModel::Trip s_trip = {0.0, 0.0};
static FuelModel::AutoTrip s_auto = {FuelModel::DEFAULT_NEW_TRIP_MIN, 0.0};
static double s_rpm = 0.0;
static double s_rate = 0.0;
static bool s_dirty = false;
static int64_t s_lastSaveUs = 0;

static bool nvsLoad(const char *key, void *dst, size_t size) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = size;
    esp_err_t err = nvs_get_blob(h, key, dst, &len);
    nvs_close(h);
    return err == ESP_OK && len == size;
}

static bool nvsSave(const char *key, const void *src, size_t size) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_blob(h, key, src, size);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS save of '%s' failed: %s", key, esp_err_to_name(err));
        return false;
    }
    return true;
}

// Callers must hold s_mutex.
static void saveTripLocked() {
    if (nvsSave(KEY_TRIP, &s_trip, sizeof(s_trip))) {
        s_dirty = false;
        s_lastSaveUs = esp_timer_get_time();
    }
}

static void integratorTask(void *) {
    int64_t last = esp_timer_get_time();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t now = esp_timer_get_time();
        double dt = (double)(now - last) / 1e6;
        last = now;

        EngineState es = EngineTelemetry::getInstance().getState();
        double rpm = (es.rpm > 0.0) ? es.rpm / RAD_S_PER_RPM : 0.0;

        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            s_rpm = rpm;
            s_rate = FuelModel::rateAt(s_curve, rpm);
            FuelModel::StepResult r = FuelModel::step(s_trip, s_auto, rpm, s_rate, dt);
            if (r.tripChanged) s_dirty = true;
            if (r.newTrip) {
                RemoteLogger::logf("[FUEL] Engine was off for the set time: new trip started\r\n");
                saveTripLocked();  // do not lose the reset if power drops right now
            }
            if (s_dirty && (now - s_lastSaveUs) >= SAVE_INTERVAL_US) saveTripLocked();
            xSemaphoreGive(s_mutex);
        }
    }
}

inline bool init() {
    if (s_mutex) return true;
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return false;
    EngineTelemetry::getInstance().init();  // mutex must exist before getState()

    FuelModel::Curve c;
    if (nvsLoad(KEY_CURVE, &c, sizeof(c)) && FuelModel::validateCurve(c)) {
        s_curve = c;
        ESP_LOGI(TAG, "Loaded fuel table from flash (%u points)", (unsigned)c.count);
    } else {
        s_curve = FuelModel::defaultCurve();
        ESP_LOGI(TAG, "Using default fuel table (%u points)", (unsigned)s_curve.count);
    }

    FuelModel::Trip t;
    if (nvsLoad(KEY_TRIP, &t, sizeof(t)) && FuelModel::validateTrip(t)) {
        s_trip = t;
        ESP_LOGI(TAG, "Loaded trip from flash: %.2f L, %.0f s", t.fuelL, t.runSeconds);
    } else {
        s_trip = {0.0, 0.0};
    }

    uint32_t minutes;
    if (nvsLoad(KEY_NEWTRIP, &minutes, sizeof(minutes)) &&
        FuelModel::validateAutoTrip(FuelModel::AutoTrip{minutes, 0.0})) {
        s_auto.newTripMin = minutes;
    }
    // The off-time counter starts at 0 on every power-up: time while the board
    // is unpowered cannot be measured.
    s_auto.offSeconds = 0.0;
    ESP_LOGI(TAG, "New trip after %u min engine off", (unsigned)s_auto.newTripMin);

    s_lastSaveUs = esp_timer_get_time();
    return xTaskCreate(integratorTask, "fuel_task", 4096, NULL, 4, NULL) == pdPASS;
}

inline Snapshot snapshot() {
    Snapshot s{};
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s.rpm = s_rpm;
        s.rateLph = s_rate;
        s.running = s_rpm >= FuelModel::RUNNING_RPM;
        s.trip = s_trip;
        s.avgLph = FuelModel::avgLph(s_trip);
        s.curve = s_curve;
        s.newTripMin = s_auto.newTripMin;
        s.offSeconds = s_auto.offSeconds;
        xSemaphoreGive(s_mutex);
    }
    return s;
}

// The setters validate again so no caller can store an invalid table or trip.
inline bool setCurve(const FuelModel::Curve &c) {
    if (!s_mutex || !FuelModel::validateCurve(c)) return false;
    bool ok = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        ok = nvsSave(KEY_CURVE, &c, sizeof(c));
        if (ok) s_curve = c;  // RAM only changes once flash has the new table
        xSemaphoreGive(s_mutex);
    }
    if (ok) RemoteLogger::logf("[FUEL] Fuel table updated (%u points)\r\n", (unsigned)c.count);
    return ok;
}

inline bool resetCurveToDefault() {
    return setCurve(FuelModel::defaultCurve());
}

inline bool setTrip(const FuelModel::Trip &t) {
    if (!s_mutex || !FuelModel::validateTrip(t)) return false;
    bool ok = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        ok = nvsSave(KEY_TRIP, &t, sizeof(t));
        if (ok) {
            s_trip = t;
            s_dirty = false;
            s_lastSaveUs = esp_timer_get_time();
        }
        xSemaphoreGive(s_mutex);
    }
    if (ok) RemoteLogger::logf("[FUEL] Trip set to %.2f L, %.0f s\r\n", t.fuelL, t.runSeconds);
    return ok;
}

inline bool setNewTripMin(uint32_t minutes) {
    if (!s_mutex || !FuelModel::validateAutoTrip(FuelModel::AutoTrip{minutes, 0.0})) return false;
    bool ok = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        ok = nvsSave(KEY_NEWTRIP, &minutes, sizeof(minutes));
        if (ok) s_auto.newTripMin = minutes;
        xSemaphoreGive(s_mutex);
    }
    if (ok) RemoteLogger::logf("[FUEL] New trip after %u min engine off\r\n", (unsigned)minutes);
    return ok;
}

inline bool resetTrip() {
    return setTrip(FuelModel::Trip{0.0, 0.0});
}

} // namespace FuelService
