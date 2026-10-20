#pragma once

#include <stdint.h>
#include "j1939_dm1.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "N2kMessages.h"

struct EngineState {
    double   rpm             = N2kDoubleNA;  // rad/s (converted from RPM)
    double   coolantTemp     = N2kDoubleNA;  // Kelvin (converted from °C)
    double   engineHours     = N2kDoubleNA;  // seconds (converted from hours)
    double   alternatorV     = N2kDoubleNA;  // Volts
    bool     lowOilPressure  = false;
    bool     overTemp        = false;
    bool     lowVoltage      = false;
    uint32_t lastFrameMs     = 0;
    uint32_t totalFrames     = 0;
};

class EngineTelemetry {
public:
    static EngineTelemetry& getInstance() {
        static EngineTelemetry instance;
        return instance;
    }

    void init() {
        if (!m_mutex) {
            m_mutex = xSemaphoreCreateMutex();
        }
    }

    void updateEEC1(float rpm) {
        if (xSemaphoreTake(m_mutex, portMAX_DELAY) == pdTRUE) {
            m_state.rpm = rpm * 0.104719755; // RPM to rad/s
            m_state.lastFrameMs = (uint32_t)(esp_timer_get_time() / 1000);
            m_state.totalFrames++;
            xSemaphoreGive(m_mutex);
        }
    }

    void updateET1(float tempC) {
        if (xSemaphoreTake(m_mutex, portMAX_DELAY) == pdTRUE) {
            m_state.coolantTemp = tempC + 273.15; // °C to Kelvin
            m_state.lastFrameMs = (uint32_t)(esp_timer_get_time() / 1000);
            m_state.totalFrames++;
            xSemaphoreGive(m_mutex);
        }
    }

    void updateHours(float hours) {
        if (xSemaphoreTake(m_mutex, portMAX_DELAY) == pdTRUE) {
            m_state.engineHours = hours * 3600.0; // hours to seconds
            m_state.lastFrameMs = (uint32_t)(esp_timer_get_time() / 1000);
            m_state.totalFrames++;
            xSemaphoreGive(m_mutex);
        }
    }

    void updateVEP1(float volts) {
        if (xSemaphoreTake(m_mutex, portMAX_DELAY) == pdTRUE) {
            m_state.alternatorV = volts;
            m_state.lastFrameMs = (uint32_t)(esp_timer_get_time() / 1000);
            m_state.totalFrames++;
            xSemaphoreGive(m_mutex);
        }
    }

    // DM1 is sent about once a second; the alarm flags follow the latest frame,
    // so they clear again when the fault is gone.
    void updateDM1(const uint8_t *data, uint8_t len) {
        J1939Dm1::Alarms a = J1939Dm1::decode(data, len);
        if (xSemaphoreTake(m_mutex, portMAX_DELAY) == pdTRUE) {
            m_state.lowOilPressure = a.lowOilPressure;
            m_state.overTemp = a.overTemp;
            m_state.lowVoltage = a.lowVoltage;
            m_state.lastFrameMs = (uint32_t)(esp_timer_get_time() / 1000);
            m_state.totalFrames++;
            xSemaphoreGive(m_mutex);
        }
    }

    EngineState getState() {
        EngineState copy;
        if (xSemaphoreTake(m_mutex, portMAX_DELAY) == pdTRUE) {
            uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
            if (m_state.lastFrameMs > 0 && (nowMs - m_state.lastFrameMs > 3000)) {
                // Watchdog expired: engine turned off
                m_state.rpm = N2kDoubleNA;
                m_state.coolantTemp = N2kDoubleNA;
                m_state.engineHours = N2kDoubleNA;
                m_state.alternatorV = N2kDoubleNA;
                m_state.lowOilPressure = false;
                m_state.overTemp = false;
                m_state.lowVoltage = false;
            }
            copy = m_state;
            xSemaphoreGive(m_mutex);
        }
        return copy;
    }

private:
    EngineTelemetry() : m_mutex(nullptr) {}
    SemaphoreHandle_t m_mutex;
    EngineState m_state;
};
