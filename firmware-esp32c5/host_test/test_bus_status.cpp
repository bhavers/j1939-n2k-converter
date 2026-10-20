// Host unit test for bus_status.h
// Run:  c++ -std=c++17 -Wall -Wextra -Werror -I firmware-esp32c5/src \
//          firmware-esp32c5/host_test/test_bus_status.cpp -o /tmp/test_bus_status && /tmp/test_bus_status

#include <stdio.h>
#include "bus_status.h"

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

using namespace BusStatus;

int main() {
    Activity a;
    CHECK(!a.active(0));
    CHECK(!a.active(100000));                 // never saw a frame

    a.noteFrame(10000);
    CHECK(a.active(10000));
    CHECK(a.active(10000 + TIMEOUT_MS));      // exactly at the limit is still fresh
    CHECK(!a.active(10000 + TIMEOUT_MS + 1)); // one ms later: no data

    a.noteFrame(20000);                       // traffic resumes
    CHECK(a.active(20500));

    CHECK(a.active(19990));                   // clock read slightly before the frame stamp

    // Time 0 is reserved for "never", but a frame at t=0 must still count.
    Activity z;
    z.noteFrame(0);
    CHECK(z.active(5));
    CHECK(!z.active(TIMEOUT_MS + 10));

    // 32-bit millisecond clock wraps after ~49 days.
    Activity w;
    w.noteFrame(0xFFFFFF00u);
    CHECK(w.active(0x00000010u));             // 272 ms later, across the wrap
    CHECK(!w.active(0x00001000u));            // ~4 s later

    // Custom timeout.
    Activity c;
    c.noteFrame(1000);
    CHECK(c.active(1900, 1000));
    CHECK(!c.active(2100, 1000));

    // The two global buses are independent.
    j1939Bus.noteFrame(5000);
    CHECK(j1939Bus.active(5100));
    CHECK(!n2kBus.active(5100));

    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
