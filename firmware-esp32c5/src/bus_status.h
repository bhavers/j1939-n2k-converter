#pragma once

// "Is there traffic on this CAN bus?" Each bus receive interrupt calls noteFrame();
// the web API asks active(). No ESP-IDF dependencies, unit-tested on the host
// (host_test/test_bus_status.cpp).

#include <stdint.h>
#include <atomic>

namespace BusStatus {

constexpr uint32_t TIMEOUT_MS = 3000;  // no frame for this long -> "no data"

class Activity {
public:
    // Single writer (the bus receive interrupt). Time 0 is reserved for "never".
    void noteFrame(uint32_t nowMs) {
        last_.store(nowMs ? nowMs : 1, std::memory_order_relaxed);
    }

    // `nowMs` may be a few ms older than the last frame (the interrupt can fire
    // between reading the clock and calling this), so a negative age counts as fresh.
    bool active(uint32_t nowMs, uint32_t timeoutMs = TIMEOUT_MS) const {
        uint32_t last = last_.load(std::memory_order_relaxed);
        if (last == 0) return false;
        int32_t age = (int32_t)(nowMs - last);
        return age <= (int32_t)timeoutMs;
    }

private:
    std::atomic<uint32_t> last_{0};
};

inline Activity j1939Bus;  // engine side (CAN 2)
inline Activity n2kBus;    // boat network side (CAN 1)

} // namespace BusStatus
