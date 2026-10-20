#pragma once

#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "config.h"
#include "engine_state.h"
#include "bus_status.h"
#include "remote_logger.h"

namespace J1939Receiver {

static const char *TAG = "J1939_RX";
static twai_node_handle_t s_j1939_node = nullptr;
static QueueHandle_t s_rxQueue = nullptr;

// The receive interrupt must not take mutexes (EngineTelemetry does), so it only
// copies the frame into a queue; rxTask() decodes it.
struct RxFrame {
    uint32_t id;
    uint8_t  len;
    uint8_t  data[8];
};

inline uint32_t extractPGN(uint32_t canId) {
    uint8_t dp = (canId >> 24) & 0x01;
    uint8_t pf = (canId >> 16) & 0xFF;
    uint8_t ps = (canId >> 8) & 0xFF;
    return (pf >= 0xF0) ? ((uint32_t)dp << 17 | (uint32_t)pf << 8 | ps)
                        : ((uint32_t)dp << 17 | (uint32_t)pf << 8);
}

static void parseFrame(uint32_t canId, const uint8_t *data, uint8_t len) {
    uint32_t pgn = extractPGN(canId);

    switch (pgn) {
        case 61444: { // EEC1 (RPM)
            if (len >= 5) {
                uint16_t rawRpm = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
                float rpm = rawRpm * 0.125f;
                EngineTelemetry::getInstance().updateEEC1(rpm);
            }
            break;
        }
        case 65262: { // ET1 (Coolant Temp)
            if (len >= 1) {
                float tempC = (float)data[0] - 40.0f;
                EngineTelemetry::getInstance().updateET1(tempC);
            }
            break;
        }
        case 65253: { // Engine Hours
            if (len >= 4) {
                uint32_t rawHours = (uint32_t)data[0] |
                                    ((uint32_t)data[1] << 8) |
                                    ((uint32_t)data[2] << 16) |
                                    ((uint32_t)data[3] << 24);
                float hours = rawHours * 0.05f;
                EngineTelemetry::getInstance().updateHours(hours);
            }
            break;
        }
        case 65271: { // VEP1 (Battery Potential)
            // 0.05 V/bit, three voltages: bytes 3-4 charging system (SPN 167),
            // bytes 5-6 battery (SPN 168), bytes 7-8 keyswitch battery (SPN 158).
            // 0xFB00 and above means "not available"; the first valid one is used.
            // (Engine off: only the keyswitch voltage is valid.)
            for (uint8_t o = 2; o + 1 < len; o += 2) {
                uint16_t raw = (uint16_t)data[o] | ((uint16_t)data[o + 1] << 8);
                if (raw < 0xFB00) {
                    EngineTelemetry::getInstance().updateVEP1(raw * 0.05f);
                    break;
                }
            }
            break;
        }
        case 65226: { // DM1 (Diagnostics)
            EngineTelemetry::getInstance().updateDM1(data, len);
            break;
        }
        default:
            break;
    }
}

// Overview of what arrives on the engine bus: one row per PGN + source address
// with a frame count and the last payload. Printed every 5 s on the TCP log
// (port 23) and USB serial, so unknown or unexpected messages can be seen.
// Only rxTask() touches it.
struct Seen {
    uint32_t pgn;
    uint8_t  sa;
    uint32_t count;
    uint8_t  len;
    uint8_t  data[8];
    uint8_t  loggedLen;     // payload last written by logRawChange(); 0xFF = never
    uint8_t  logged[8];
    uint32_t lastLogMs;
};
static const int MAX_SEEN = 32;
static Seen s_seen[MAX_SEEN];
static int s_nSeen = 0;
static uint32_t s_totalFrames = 0;
static uint32_t s_overflow = 0;   // frames of PGNs that did not fit in the table

// Every payload change of a PGN that is NOT decoded is logged as one line with a
// timestamp ("[J1939-RAW] t=<ms since boot> SA <sa> PGN <pgn>: <hex>"), so the
// messages can be analysed later (capture with: nc 192.168.4.1 23 | tee log.txt).
// Limits keep the TCP log from stalling the receive task: at most one line per
// PGN+SA every RAW_MIN_INTERVAL_MS and RAW_MAX_LINES_PER_S lines in total.
// Only compiled in with J1939_RAW_LOG 1 (config.h).
static uint32_t s_rawSkipped = 0;   // changes not logged because of the total limit

#if J1939_RAW_LOG
static const uint32_t RAW_MIN_INTERVAL_MS = 100;
static const uint32_t RAW_MAX_LINES_PER_S = 40;
static uint32_t s_rawWindowStartMs = 0;
static uint32_t s_rawWindowLines = 0;

static bool isDecodedPgn(uint32_t pgn) {
    return pgn == 61444 || pgn == 65262 || pgn == 65253 || pgn == 65271 || pgn == 65226;
}
#endif

static void logRawChange(Seen &s, uint32_t nowMs) {
#if !J1939_RAW_LOG
    (void)s; (void)nowMs;
    return;
#else
    if (isDecodedPgn(s.pgn)) return;
    if (s.loggedLen == s.len && memcmp(s.logged, s.data, s.len) == 0) return;
    if (s.loggedLen != 0xFF && (uint32_t)(nowMs - s.lastLogMs) < RAW_MIN_INTERVAL_MS) return;
    if ((uint32_t)(nowMs - s_rawWindowStartMs) >= 1000) {
        s_rawWindowStartMs = nowMs;
        s_rawWindowLines = 0;
    }
    if (s_rawWindowLines >= RAW_MAX_LINES_PER_S) { s_rawSkipped++; return; }
    s_rawWindowLines++;
    char hex[3 * 8 + 1] = {0};
    for (int b = 0; b < s.len; b++) snprintf(hex + b * 3, 4, "%02X ", s.data[b]);
    RemoteLogger::logf("[J1939-RAW] t=%u SA %u PGN %u: %s\r\n",
                       (unsigned)nowMs, (unsigned)s.sa, (unsigned)s.pgn, hex);
    s.loggedLen = s.len;
    memcpy(s.logged, s.data, s.len);
    s.lastLogMs = nowMs;
#endif
}

static void noteSeen(uint32_t canId, const uint8_t *data, uint8_t len) {
    s_totalFrames++;
    uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t pgn = extractPGN(canId);
    uint8_t sa = canId & 0xFF;
    for (int i = 0; i < s_nSeen; i++) {
        if (s_seen[i].pgn == pgn && s_seen[i].sa == sa) {
            s_seen[i].count++;
            s_seen[i].len = len;
            memcpy(s_seen[i].data, data, len);
            logRawChange(s_seen[i], nowMs);
            return;
        }
    }
    if (s_nSeen >= MAX_SEEN) { s_overflow++; return; }
    Seen &s = s_seen[s_nSeen++];
    s.pgn = pgn;
    s.sa = sa;
    s.count = 1;
    s.len = len;
    memcpy(s.data, data, len);
    s.loggedLen = 0xFF;
    s.lastLogMs = 0;
    logRawChange(s, nowMs);
}

static void printSeen() {
    twai_node_status_t st = {};
    if (twai_node_get_info(s_j1939_node, &st, NULL) == ESP_OK) {
        RemoteLogger::logf("[J1939] frames=%u pgns=%d overflow=%u rawSkipped=%u rxErr=%u txErr=%u\r\n",
                           (unsigned)s_totalFrames, s_nSeen, (unsigned)s_overflow,
                           (unsigned)s_rawSkipped,
                           (unsigned)st.rx_error_count, (unsigned)st.tx_error_count);
    }
    for (int i = 0; i < s_nSeen; i++) {
        const Seen &s = s_seen[i];
        char hex[3 * 8 + 1] = {0};
        for (int b = 0; b < s.len; b++) snprintf(hex + b * 3, 4, "%02X ", s.data[b]);
        RemoteLogger::logf("[J1939] PGN %6u SA %3u n=%-6u last: %s\r\n",
                           (unsigned)s.pgn, (unsigned)s.sa, (unsigned)s.count, hex);
    }
}

static void rxTask(void *) {
    RxFrame f;
    int64_t nextPrint = esp_timer_get_time() + 5000000;
    while (true) {
        if (xQueueReceive(s_rxQueue, &f, pdMS_TO_TICKS(1000)) == pdTRUE) {
            noteSeen(f.id, f.data, f.len);
            parseFrame(f.id, f.data, f.len);
        }
        if (esp_timer_get_time() >= nextPrint) {
            nextPrint += 5000000;
            printSeen();
        }
    }
}

static bool IRAM_ATTR on_j1939_rx(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx) {
    uint8_t buffer[64];
    twai_frame_t rx_frame = {};
    rx_frame.buffer = buffer;
    rx_frame.buffer_len = sizeof(buffer);

    uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
    BaseType_t wake = pdFALSE;
    // Limit frames per ISR call to avoid hogging the CPU on a busy bus.
    for (int _i = 0; _i < 8 && twai_node_receive_from_isr(handle, &rx_frame) == ESP_OK; _i++) {
        if (!rx_frame.header.ide) continue;  // J1939 uses 29-bit ids only
        BusStatus::j1939Bus.noteFrame(nowMs);
        RxFrame f;
        f.id = rx_frame.header.id;
        f.len = rx_frame.header.dlc > 8 ? 8 : rx_frame.header.dlc;
        memcpy(f.data, rx_frame.buffer, f.len);
        xQueueSendFromISR(s_rxQueue, &f, &wake);  // queue full: frame dropped, next one follows in <1 s
    }
    return wake == pdTRUE;
}

inline bool init() {
    EngineTelemetry::getInstance().init();  // mutex must exist before the first frame
    s_rxQueue = xQueueCreate(64, sizeof(RxFrame));
    if (!s_rxQueue || xTaskCreate(rxTask, "j1939_rx", 4096, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Could not create J1939 receive queue/task");
        return false;
    }

    twai_onchip_node_config_t cfg = {};
    cfg.io_cfg.tx = (gpio_num_t)J1939_TWAI_TX_PIN;
    cfg.io_cfg.rx = (gpio_num_t)J1939_TWAI_RX_PIN;
    cfg.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    cfg.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    cfg.bit_timing.bitrate = 250000;
    cfg.flags.enable_listen_only = true;

    esp_err_t err = twai_new_node_onchip(&cfg, &s_j1939_node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_new_node_onchip failed for J1939: %s", esp_err_to_name(err));
        return false;
    }

    twai_event_callbacks_t cbs = {};
    cbs.on_rx_done = on_j1939_rx;
    err = twai_node_register_event_callbacks(s_j1939_node, &cbs, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_node_register_event_callbacks failed: %s", esp_err_to_name(err));
        return false;
    }

    err = twai_node_enable(s_j1939_node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_node_enable failed for J1939: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "J1939 TWAI node started in LISTEN-ONLY mode (250 kbps) on TX=%d, RX=%d",
             J1939_TWAI_TX_PIN, J1939_TWAI_RX_PIN);
    return true;
}

} // namespace J1939Receiver
