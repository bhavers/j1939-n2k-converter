#pragma once

// NMEA 2000 side of the gateway (CAN 1): claims an address on the boat network
// and sends PGN 127488 (engine rapid, 10 Hz), PGN 127489 (engine dynamic, 1 Hz)
// and PGN 127497 (engine trip, 1 Hz) for engine instance 0.
//
// Data source:
//   - J1939 frames are arriving on CAN 2 -> real engine values (EngineTelemetry).
//   - No J1939 traffic and N2K_DUMMY_DATA is 1 (config.h) -> a simulated engine,
//     so the device and its data can be checked on the MFD without an engine.
//   - No J1939 traffic and N2K_DUMMY_DATA is 0 -> all fields are "not available".

#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "N2kMessages.h"
#include "NMEA2000_esp32c5.h"
#include "config.h"
#include "engine_state.h"
#include "fuel_model.h"
#include "fuel_service.h"
#include "bus_status.h"
#include "remote_logger.h"
#include "n2k_device_list.h"
#include "gps_state.h"

#ifndef N2K_DUMMY_DATA
#define N2K_DUMMY_DATA 0
#endif

namespace N2KGateway {

static const char *TAG = "N2K_GW";
static const unsigned char ENGINE_INSTANCE = 0;
static const double RAD_S_PER_RPM = 0.104719755;

static tNMEA2000_esp32c5 *s_n2k = nullptr;
static uint32_t s_sent = 0;
static uint32_t s_notAccepted = 0;

struct Data {
    double rpm = N2kDoubleNA;        // RPM (PGN 127488 wants RPM, not rad/s)
    double coolantK = N2kDoubleNA;
    double engineHoursS = N2kDoubleNA;
    double altV = N2kDoubleNA;
    double fuelLph = N2kDoubleNA;
    double tripFuelL = N2kDoubleNA;
    double avgLph = N2kDoubleNA;
    bool lowOilPressure = false;
    bool overTemp = false;
    bool lowVoltage = false;
    bool dummy = false;
};

static Data collectReal() {
    EngineState s = EngineTelemetry::getInstance().getState();
    FuelService::Snapshot fs = FuelService::snapshot();
    Data d;
    d.rpm = N2kIsNA(s.rpm) ? N2kDoubleNA : s.rpm / RAD_S_PER_RPM;
    d.coolantK = s.coolantTemp;
    d.engineHoursS = s.engineHours;
    d.altV = s.alternatorV;
    d.fuelLph = N2kIsNA(s.rpm) ? N2kDoubleNA : fs.rateLph;
    d.tripFuelL = fs.trip.fuelL;
    d.avgLph = fs.avgLph < 0.0 ? N2kDoubleNA : fs.avgLph;
    d.lowOilPressure = s.lowOilPressure;
    d.overTemp = s.overTemp;
    d.lowVoltage = s.lowVoltage;
    return d;
}

#if N2K_DUMMY_DATA
// Simulated engine: speed swings between 800 and 2200 RPM over 60 s.
static Data collectDummy(int64_t nowUs) {
    static const FuelModel::Curve curve = FuelModel::defaultCurve();
    double t = (double)nowUs / 1e6;
    Data d;
    d.rpm = 1500.0 + 700.0 * sin(2.0 * M_PI * t / 60.0);
    d.coolantK = 273.15 + 75.0;
    d.engineHoursS = 1234.5 * 3600.0;
    d.altV = 13.8;
    d.fuelLph = FuelModel::rateAt(curve, d.rpm);
    d.tripFuelL = 12.3;
    d.avgLph = 4.5;
    d.dummy = true;
    return d;
}
#endif

static Data collect(int64_t nowUs) {
#if N2K_DUMMY_DATA
    if (!BusStatus::j1939Bus.active((uint32_t)(nowUs / 1000))) return collectDummy(nowUs);
#endif
    return collectReal();
}

static void send(const tN2kMsg &msg) {
    if (s_n2k->SendMsg(msg)) s_sent++; else s_notAccepted++;
}

static void sendRapid(const Data &d) {
    tN2kMsg msg;
    SetN2kEngineParamRapid(msg, ENGINE_INSTANCE, d.rpm);
    send(msg);
}

static void sendDynamic(const Data &d) {
    tN2kEngineDiscreteStatus1 s1;
    tN2kEngineDiscreteStatus2 s2;
    s1.Status = 0;
    s2.Status = 0;
    s1.Bits.LowOilPressure = d.lowOilPressure;
    s1.Bits.OverTemperature = d.overTemp;
    s1.Bits.LowSystemVoltage = d.lowVoltage;
    tN2kMsg msg;
    SetN2kEngineDynamicParam(msg, ENGINE_INSTANCE, N2kDoubleNA, N2kDoubleNA, d.coolantK, d.altV,
                             d.fuelLph, d.engineHoursS, N2kDoubleNA, N2kDoubleNA,
                             N2kInt8NA, N2kInt8NA, s1, s2);
    send(msg);
}

static void sendTrip(const Data &d) {
    tN2kMsg msg;
    SetN2kEngineTripParameters(msg, ENGINE_INSTANCE, d.tripFuelL, d.avgLph);
    send(msg);
}

// Called by the library from ParseMessages() for every message on the bus.
// Keeps the latest GPS position and UTC time of any GNSS device (PGN 129029,
// 129025, 126992) for the web page.
static void onBusMessage(const tN2kMsg &msg) {
    unsigned char sid;
    double lat, lon;
    switch (msg.PGN) {
    case 129029: {
        uint16_t days, refId;
        double secs, alt, hdop, pdop, geoid, age;
        tN2kGNSStype gnssType, refType;
        tN2kGNSSmethod method;
        unsigned char nSat, nRef;
        if (ParseN2kPGN129029(msg, sid, days, secs, lat, lon, alt, gnssType, method, nSat,
                              hdop, pdop, geoid, nRef, refType, refId, age)) {
            if (!N2kIsNA(lat) && !N2kIsNA(lon)) GpsState::updatePosition(lat, lon);
            if (days != N2kUInt16NA && !N2kIsNA(secs)) GpsState::updateTime(days, secs);
        }
        break;
    }
    case 129025:
        if (ParseN2kPGN129025(msg, lat, lon) && !N2kIsNA(lat) && !N2kIsNA(lon)) {
            GpsState::updatePosition(lat, lon);
        }
        break;
    case 126992: {
        uint16_t days;
        double secs;
        tN2kTimeSource src;
        if (ParseN2kPGN126992(msg, sid, days, secs, src) &&
            days != N2kUInt16NA && !N2kIsNA(secs)) {
            GpsState::updateTime(days, secs);
        }
        break;
    }
    default:
        break;
    }
}

// The NMEA2000 library is not thread safe: everything that touches it runs here.
static void gatewayTask(void *) {
    // Wait for both TWAI nodes to be fully stable before starting ParseMessages().
    // The ESP32-C5 TWAI clock mux is shared between nodes; starting the second
    // node's ISR while the first is already running causes a PERIPH_RCC_ATOMIC
    // critical section long enough to trigger the interrupt WDT on a unicore chip.
    vTaskDelay(pdMS_TO_TICKS(500));

    int64_t next100 = 0, next1000 = 0, nextLog = 0;
    while (true) {
        s_n2k->ParseMessages();
        N2kDeviceTracker::poll();

        int64_t now = esp_timer_get_time();
        if (now >= next100) {
            next100 = now + 100000;
            sendRapid(collect(now));
        }
        if (now >= next1000) {
            next1000 = now + 1000000;
            Data d = collect(now);
            sendDynamic(d);
            sendTrip(d);
        }
        if (now >= nextLog) {
            nextLog = now + 5000000;  // every 5 s
            uint32_t nowMs = (uint32_t)(now / 1000);
            bool busActive = BusStatus::n2kBus.active(nowMs);

            // Count known devices on the bus
            tN2kDeviceList *devList = N2kDeviceTracker::s_list;
            uint8_t devCount = devList ? devList->Count() : 0;

            Data d = collect(now);
            ESP_LOGI(TAG, "src=%u sent=%u notAccepted=%u busActive=%s devices=%u %s rpm=%.0f",
                     (unsigned)s_n2k->GetN2kSource(), (unsigned)s_sent, (unsigned)s_notAccepted,
                     busActive ? "YES" : "NO", (unsigned)devCount,
                     d.dummy ? "DUMMY" : "real", N2kIsNA(d.rpm) ? -1.0 : d.rpm);

            // List every known device
            if (devList) {
                for (int src = 0; src < 254; src++) {
                    const tNMEA2000::tDevice *dev = devList->FindDeviceBySource((uint8_t)src);
                    if (!dev) continue;
                    const char *model = dev->GetModelID();
                    ESP_LOGI(TAG, "  device src=%d mfr=%u class=%u func=%u name=\"%s\"",
                             src, (unsigned)dev->GetManufacturerCode(),
                             (unsigned)dev->GetDeviceClass(),
                             (unsigned)dev->GetDeviceFunction(),
                             (model && model[0]) ? model : "(pending)");
                }
            }
        }
        // FreeRTOS tick rate is 100 Hz (10 ms/tick).  pdMS_TO_TICKS(N) rounds
        // down, so values < 10 ms become 0 ticks (= taskYIELD, never blocks).
        // Use 2 explicit ticks (20 ms) to guarantee a real block that lets
        // the IDLE task run and reset the Task WDT.
        vTaskDelay(2);
    }
}

inline bool init() {
    s_n2k = new tNMEA2000_esp32c5(N2K_TWAI_TX_PIN, N2K_TWAI_RX_PIN);

    s_n2k->SetProductInformation("00000001", 100, "J1939-N2K Gateway", "1.0.0", "1.0.0");
    s_n2k->SetDeviceInformation(1, 140, 50, 2046);  // unique no., Engine Gateway, Propulsion, manufacturer
    s_n2k->SetMode(tNMEA2000::N2km_NodeOnly, 22);
    s_n2k->EnableForward(false);
    s_n2k->SetMsgHandler(onBusMessage);

    static const unsigned long transmitMessages[] = {127488L, 127489L, 127497L, 0};
    s_n2k->ExtendTransmitMessages(transmitMessages);

    // Open() is a state machine that is finished by ParseMessages() in the task.
    // The task itself is started later via startTask(), after the J1939 TWAI node
    // is fully up. Both TWAI nodes call PERIPH_RCC_ATOMIC() during enable; on the
    // unicore ESP32-C5 the shared periph spinlock causes an interrupt WDT crash if
    // the N2K task's ParseMessages() loop holds the lock while the J1939 ISR fires.
    N2kDeviceTracker::init(s_n2k);
    s_n2k->Open();
    return true;
}

inline bool startTask() {
    if (xTaskCreate(gatewayTask, "n2k_gw", 8192, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Could not create NMEA 2000 task");
        return false;
    }
#if N2K_DUMMY_DATA
    ESP_LOGW(TAG, "NMEA 2000 started. DUMMY engine data is sent while no J1939 traffic arrives");
#else
    ESP_LOGI(TAG, "NMEA 2000 started");
#endif
    return true;
}

} // namespace N2KGateway
