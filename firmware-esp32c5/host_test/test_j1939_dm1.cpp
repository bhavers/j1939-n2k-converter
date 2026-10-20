// Host unit test for j1939_dm1.h
// Run:  c++ -std=c++17 -Wall -Wextra -Werror -I firmware-esp32c5/src \
//          firmware-esp32c5/host_test/test_j1939_dm1.cpp -o /tmp/test_j1939_dm1 && /tmp/test_j1939_dm1

#include <stdio.h>
#include "j1939_dm1.h"

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_fail++;                                                        \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                   \
        }                                                                    \
    } while (0)

using namespace J1939Dm1;

int main() {
    // No active faults, typical: lamps off, DTC all zero.
    const uint8_t none[8] = {0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF};
    Alarms a = decode(none, 8);
    CHECK(!a.lowOilPressure && !a.overTemp && !a.lowVoltage);

    // Some ECUs fill the whole DTC with 0xFF when there is no fault.
    const uint8_t ff[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    a = decode(ff, 8);
    CHECK(!a.lowOilPressure && !a.overTemp && !a.lowVoltage);

    // SPN 100 (oil pressure), FMI 1.
    const uint8_t oil[8] = {0x04, 0xFF, 100, 0x00, 0x01, 0x01, 0xFF, 0xFF};
    a = decode(oil, 8);
    CHECK(a.lowOilPressure && !a.overTemp && !a.lowVoltage);

    // SPN 110 (coolant temp), FMI 0.
    const uint8_t temp[8] = {0x04, 0xFF, 110, 0x00, 0x00, 0x01, 0xFF, 0xFF};
    a = decode(temp, 8);
    CHECK(!a.lowOilPressure && a.overTemp && !a.lowVoltage);

    // SPN 168 (battery), FMI 18.
    const uint8_t volt[8] = {0x04, 0xFF, 168, 0x00, 18, 0x01, 0xFF, 0xFF};
    a = decode(volt, 8);
    CHECK(!a.lowOilPressure && !a.overTemp && a.lowVoltage);

    // An unrelated SPN (190 = engine speed) sets nothing.
    const uint8_t other[8] = {0x04, 0xFF, 190, 0x00, 0x02, 0x01, 0xFF, 0xFF};
    a = decode(other, 8);
    CHECK(!a.lowOilPressure && !a.overTemp && !a.lowVoltage);

    // SPN bits 16-18 are decoded: SPN 0x10064 is not SPN 100.
    const uint8_t hi[8] = {0x04, 0xFF, 100, 0x00, (uint8_t)((1 << 5) | 1), 0x01, 0xFF, 0xFF};
    a = decode(hi, 8);
    CHECK(!a.lowOilPressure);

    // Two DTCs in one longer buffer (as after reassembly): oil + temp.
    const uint8_t two[10] = {0x04, 0xFF, 100, 0x00, 0x01, 0x01, 110, 0x00, 0x00, 0x01};
    a = decode(two, 10);
    CHECK(a.lowOilPressure && a.overTemp && !a.lowVoltage);

    // Short / invalid frames never read out of bounds and never alarm.
    a = decode(oil, 0);
    CHECK(!a.lowOilPressure);
    a = decode(oil, 5);                               // DTC cut off after 3 bytes
    CHECK(!a.lowOilPressure);
    a = decode(oil, 6);                               // exactly one complete DTC
    CHECK(a.lowOilPressure);
    a = decode(nullptr, 8);
    CHECK(!a.lowOilPressure && !a.overTemp && !a.lowVoltage);

    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
