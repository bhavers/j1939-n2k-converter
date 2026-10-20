#include "NMEA2000_esp32c5.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "bus_status.h"

static const char *TAG = "N2K_C5";

tNMEA2000_esp32c5::tNMEA2000_esp32c5(gpio_num_t txPin, gpio_num_t rxPin)
    : m_txPin(txPin), m_rxPin(rxPin), m_node(nullptr), m_rxQueue(nullptr) {
    // All slots start free: bits 0..(N2K_TX_POOL-1) set
    atomic_store(&m_txFree, (1u << N2K_TX_POOL) - 1u);
    memset(m_txPool, 0, sizeof(m_txPool));
}

tNMEA2000_esp32c5::~tNMEA2000_esp32c5() {
    if (m_node) {
        twai_node_disable(m_node);
        twai_node_delete(m_node);
        m_node = nullptr;
    }
    if (m_rxQueue) {
        vQueueDelete(m_rxQueue);
        m_rxQueue = nullptr;
    }
}

// --- TX pool helpers -------------------------------------------------------

int tNMEA2000_esp32c5::txSlotAlloc() {
    uint_fast32_t free = atomic_load(&m_txFree);
    while (free) {
        int idx = __builtin_ctz(free);                   // lowest free bit
        uint_fast32_t desired = free & ~(1u << idx);
        if (atomic_compare_exchange_weak(&m_txFree, &free, desired)) {
            return idx;
        }
        // free was updated by CAS; retry with new value
    }
    return -1;  // pool exhausted
}

void IRAM_ATTR tNMEA2000_esp32c5::txSlotFree(const twai_frame_t *frame) {
    for (int i = 0; i < N2K_TX_POOL; i++) {
        if (&m_txPool[i].frame == frame) {
            uint_fast32_t prev = atomic_fetch_or(&m_txFree, 1u << i);
            (void)prev;
            return;
        }
    }
}

// --- TX-done callback (ISR) ------------------------------------------------

bool IRAM_ATTR tNMEA2000_esp32c5::txDoneCallback(twai_node_handle_t /*handle*/,
                                                   const twai_tx_done_event_data_t *edata,
                                                   void *user_ctx) {
    tNMEA2000_esp32c5 *self = static_cast<tNMEA2000_esp32c5*>(user_ctx);
    if (self && edata) self->txSlotFree(edata->done_tx_frame);
    return false;
}

// --- RX callback (ISR) -----------------------------------------------------

bool IRAM_ATTR tNMEA2000_esp32c5::rxCallback(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx) {
    tNMEA2000_esp32c5 *self = static_cast<tNMEA2000_esp32c5*>(user_ctx);
    if (!self || !self->m_rxQueue) return false;

    uint8_t buffer[64];
    twai_frame_t rx_frame = {};
    rx_frame.buffer = buffer;
    rx_frame.buffer_len = sizeof(buffer);

    uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
    BaseType_t high_task_wakeup = pdFALSE;
    // Limit frames per ISR call: the N2K bus (Raymarine MFD) can deliver many
    // frames in a burst; an unbounded loop keeps the ISR running long enough
    // to starve other interrupts and trigger the INT WDT on the unicore ESP32-C5.
    for (int _rxlim = 0; _rxlim < 16 && twai_node_receive_from_isr(handle, &rx_frame) == ESP_OK; _rxlim++) {
        if (!rx_frame.header.ide) continue;  // NMEA 2000 uses 29-bit ids only
        BusStatus::n2kBus.noteFrame(nowMs);
        N2kCANFrame frame;
        frame.id = rx_frame.header.id;
        frame.len = rx_frame.header.dlc > 8 ? 8 : rx_frame.header.dlc;
        memcpy(frame.data, rx_frame.buffer, frame.len);

        xQueueSendFromISR(self->m_rxQueue, &frame, &high_task_wakeup);
    }
    // Do NOT yield to the n2k_gw task from ISR: the Raymarine MFD sends
    // continuous bursts and unconditional yielding starves the IDLE task,
    // causing Task WDT → INT WDT crashes.  The task polls the queue every 5 ms,
    // which is fast enough for NMEA 2000.
    (void)high_task_wakeup;
    return false;
}

bool tNMEA2000_esp32c5::CANOpen() {
    if (!m_rxQueue) {
        m_rxQueue = xQueueCreate(128, sizeof(N2kCANFrame));
    }

    twai_onchip_node_config_t cfg = {};
    cfg.io_cfg.tx = m_txPin;
    cfg.io_cfg.rx = m_rxPin;
    cfg.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    cfg.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    cfg.bit_timing.bitrate = 250000;
    cfg.tx_queue_depth = 20;

    esp_err_t err = twai_new_node_onchip(&cfg, &m_node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_new_node_onchip failed: %s", esp_err_to_name(err));
        return false;
    }

    twai_event_callbacks_t cbs = {};
    cbs.on_rx_done  = rxCallback;
    cbs.on_tx_done  = txDoneCallback;
    err = twai_node_register_event_callbacks(m_node, &cbs, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_node_register_event_callbacks failed: %s", esp_err_to_name(err));
        return false;
    }

    err = twai_node_enable(m_node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_node_enable failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "NMEA2000 TWAI node enabled on TX=%d, RX=%d (250 kbps)", m_txPin, m_rxPin);
    return true;
}

bool tNMEA2000_esp32c5::CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf, bool wait_sent) {
    if (!m_node) return false;

    twai_node_status_t status = {};
    if (twai_node_get_info(m_node, &status, NULL) == ESP_OK && status.state == TWAI_ERROR_BUS_OFF) {
        static int64_t last_recover_us = 0;
        int64_t now_us = esp_timer_get_time();
        if (now_us - last_recover_us > 3000000) { // rate limit recovery to once per 3 s
            last_recover_us = now_us;
            twai_node_recover(m_node);
        }
        return false;
    }

    // The IDF TWAI tx-queue stores only a *pointer* to twai_frame_t — it does
    // NOT copy the struct.  The frame must remain valid until the TX-done ISR
    // fires and our txDoneCallback releases the slot.
    int slot = txSlotAlloc();
    if (slot < 0) return false;   // pool exhausted (shouldn't happen in practice)

    TxSlot &s = m_txPool[slot];
    if (len > 8) len = 8;
    memcpy(s.buf, buf, len);

    s.frame = {};
    s.frame.header.id  = id;
    s.frame.header.ide = true;  // 29-bit extended frame
    s.frame.header.dlc = len;
    s.frame.buffer     = s.buf;
    s.frame.buffer_len = len;

    esp_err_t err = twai_node_transmit(m_node, &s.frame, 0);
    if (err != ESP_OK) {
        // transmit rejected — free the slot immediately
        atomic_fetch_or(&m_txFree, 1u << slot);
        return false;
    }
    return true;
}

bool tNMEA2000_esp32c5::CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) {
    if (!m_rxQueue) return false;

    N2kCANFrame frame;
    if (xQueueReceive(m_rxQueue, &frame, 0) == pdTRUE) {
        id = frame.id;
        len = frame.len;
        memcpy(buf, frame.data, len);
        return true;
    }
    return false;
}
