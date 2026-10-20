#pragma once

// Pure decoder for the J1939 DM1 message (PGN 65226, active diagnostic trouble codes).
// No ESP-IDF dependencies, unit-tested on the host (host_test/test_j1939_dm1.cpp).
//
// Frame layout: byte 0 = lamp status, byte 1 = lamp flash, then 4-byte DTCs:
//   byte a   : SPN bits 0-7
//   byte a+1 : SPN bits 8-15
//   byte a+2 : bits 5-7 = SPN bits 16-18, bits 0-4 = FMI
//   byte a+3 : bit 7 = conversion method, bits 0-6 = occurrence count
// A single 8-byte frame carries one DTC. Several active DTCs are sent with the
// transport protocol (BAM), which this decoder does not reassemble.

#include <stdint.h>

namespace J1939Dm1 {

constexpr uint32_t SPN_OIL_PRESSURE = 100;
constexpr uint32_t SPN_COOLANT_TEMP = 110;
constexpr uint32_t SPN_BATTERY_V    = 168;

struct Alarms {
    bool lowOilPressure;
    bool overTemp;
    bool lowVoltage;
};

// Any active DTC for the SPN counts as an alarm, whatever the FMI, so a failed
// sensor is also shown (the operator should look at it either way).
inline Alarms decode(const uint8_t *data, uint8_t len) {
    Alarms a{false, false, false};
    if (!data) return a;
    for (uint8_t i = 2; i + 3 < len; i += 4) {
        uint32_t spn = (uint32_t)data[i] | ((uint32_t)data[i + 1] << 8) |
                       ((uint32_t)((data[i + 2] >> 5) & 0x07) << 16);
        uint8_t fmi = data[i + 2] & 0x1F;
        if (spn == 0 && fmi == 0) continue;           // "no fault" placeholder
        if (spn == 0x7FFFF) continue;                  // not available (0xFF fill)
        if (spn == SPN_OIL_PRESSURE) a.lowOilPressure = true;
        else if (spn == SPN_COOLANT_TEMP) a.overTemp = true;
        else if (spn == SPN_BATTERY_V) a.lowVoltage = true;
    }
    return a;
}

} // namespace J1939Dm1
