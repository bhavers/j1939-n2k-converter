#pragma once

#include <NMEA2000.h>
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdatomic.h>

struct N2kCANFrame {
    uint32_t id;
    uint8_t  len;
    uint8_t  data[8];
};

// Number of TX frame slots.  Must exceed CANOpen tx_queue_depth (20) so we
// never run out of slots while the hardware queue is full.
static constexpr int N2K_TX_POOL = 24;

class tNMEA2000_esp32c5 : public tNMEA2000 {
public:
    tNMEA2000_esp32c5(gpio_num_t txPin, gpio_num_t rxPin);
    virtual ~tNMEA2000_esp32c5();
    twai_node_handle_t getNode() const { return m_node; }

protected:
    bool CANOpen() override;
    bool CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf, bool wait_sent = true) override;
    bool CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) override;

private:
    gpio_num_t         m_txPin;
    gpio_num_t         m_rxPin;
    twai_node_handle_t m_node;
    QueueHandle_t      m_rxQueue;

    // Each outgoing CAN frame must stay alive until the TX-done ISR fires.
    // The IDF TWAI queue stores only a pointer to twai_frame_t, not a copy,
    // so we keep a small pool of persistent frame + data-buffer pairs.
    struct TxSlot {
        twai_frame_t frame;
        uint8_t      buf[8];
    };
    TxSlot          m_txPool[N2K_TX_POOL];
    atomic_uint_fast32_t m_txFree; // bitmask: bit N = slot N is free

    // Allocate a free slot (returns index 0..N2K_TX_POOL-1, or -1 if full)
    int  txSlotAlloc();
    // Release a slot by frame pointer (called from TX-done ISR)
    void txSlotFree(const twai_frame_t *frame);

    static bool IRAM_ATTR rxCallback(twai_node_handle_t handle,
                                     const twai_rx_done_event_data_t *edata,
                                     void *user_ctx);
    static bool IRAM_ATTR txDoneCallback(twai_node_handle_t handle,
                                         const twai_tx_done_event_data_t *edata,
                                         void *user_ctx);
};
