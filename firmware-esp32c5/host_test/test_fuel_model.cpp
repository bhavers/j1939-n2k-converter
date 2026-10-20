// Host unit test for fuel_model.h
// Run:  c++ -std=c++17 -Wall -Wextra -Werror -I firmware-esp32c5/src \
//          firmware-esp32c5/host_test/test_fuel_model.cpp -o /tmp/test_fuel_model && /tmp/test_fuel_model

#include <stdio.h>
#include <initializer_list>
#include <limits>
#include "fuel_model.h"

using namespace FuelModel;

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

#define NEAR(a, b) CHECK(std::fabs((a) - (b)) < 1e-4)

static Curve mk(std::initializer_list<CurvePoint> pts) {
    Curve c{};
    for (auto &p : pts) c.pts[c.count++] = p;
    return c;
}

int main() {
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    const float fNaN = std::numeric_limits<float>::quiet_NaN();

    // --- default curve ---
    Curve d = defaultCurve();
    CHECK(validateCurve(d));
    CHECK(d.count == 8);

    // --- rateAt ---
    NEAR(rateAt(d, 2000.0), 2.0);                 // exact point
    NEAR(rateAt(d, 3200.0), 6.6);                 // last point
    NEAR(rateAt(d, 5000.0), 6.6);                 // above last: clamp
    NEAR(rateAt(d, 2100.0), 2.25);                // midpoint 2.0..2.5
    NEAR(rateAt(d, 1500.0), 0.95);                // midpoint 1.0..0.9 (curve dips)
    NEAR(rateAt(d, 700.0), 0.5);                  // half of first point rpm -> half of 1.0
    NEAR(rateAt(d, 0.0), 0.0);
    NEAR(rateAt(d, -5.0), 0.0);
    NEAR(rateAt(d, NaN), 0.0);

    // --- validateCurve rejects ---
    CHECK(!validateCurve(Curve{}));                                        // 0 points
    CHECK(!validateCurve(mk({{1000, 1}})));                                // 1 point
    CHECK(!validateCurve(mk({{2000, 1}, {1000, 2}})));                     // descending
    CHECK(!validateCurve(mk({{1000, 1}, {1000, 2}})));                     // duplicate rpm
    CHECK(!validateCurve(mk({{0, 1}, {1000, 2}})));                        // rpm 0
    CHECK(!validateCurve(mk({{1000, -1}, {2000, 2}})));                    // negative rate
    CHECK(!validateCurve(mk({{1000, 1}, {2000, MAX_LPH + 1}})));           // rate too high
    CHECK(!validateCurve(mk({{1000, 1}, {MAX_RPM + 1, 2}})));              // rpm too high
    CHECK(!validateCurve(mk({{1000, 1}, {2000, fNaN}})));                  // NaN
    CHECK(!validateCurve(mk({{1000, 1}, {std::numeric_limits<float>::infinity(), 2}})));
    CHECK(validateCurve(mk({{1000, 0}, {2000, 0}})));                      // zero rate is allowed
    Curve tooMany{};
    tooMany.count = MAX_POINTS + 1;
    for (uint32_t i = 0; i < MAX_POINTS; i++) tooMany.pts[i] = {100.0f * (i + 1), 1.0f};
    CHECK(!validateCurve(tooMany));
    Curve full{};
    full.count = MAX_POINTS;
    for (uint32_t i = 0; i < MAX_POINTS; i++) full.pts[i] = {100.0f * (i + 1), 1.0f};
    CHECK(validateCurve(full));                                            // exactly 12 is fine

    // --- parseCurve ---
    Curve out = defaultCurve();
    CHECK(parseCurve("1000:1.0,2000:3.5", out));
    CHECK(out.count == 2);
    NEAR(out.pts[1].lph, 3.5);

    Curve keep = defaultCurve();
    CHECK(!parseCurve("", keep));                    // empty -> too few points
    CHECK(!parseCurve("1000:1.0", keep));            // one point
    CHECK(!parseCurve("1000:1.0,", keep));           // trailing comma
    CHECK(!parseCurve("1000:1.0,,2000:2.0", keep));  // empty entry
    CHECK(!parseCurve("1000;1.0,2000:2.0", keep));   // wrong separator
    CHECK(!parseCurve("1000:1.0,2000:abc", keep));   // not a number
    CHECK(!parseCurve("1000:1.0,2000:2.0x", keep));  // trailing garbage
    CHECK(!parseCurve("1000:1.0,500:2.0", keep));    // descending
    CHECK(!parseCurve("1000:nan,2000:2.0", keep));   // nan
    CHECK(!parseCurve("1000:1.0,2000:inf", keep));   // inf
    CHECK(!parseCurve("1000:1.0,2000:1e99", keep));  // out of range
    CHECK(!parseCurve("1:1,2:1,3:1,4:1,5:1,6:1,7:1,8:1,9:1,10:1,11:1,12:1,13:1", keep));  // 13 points
    CHECK(!parseCurve(nullptr, keep));
    CHECK(keep.count == 8);                          // untouched after every failure

    // --- advance / trip ---
    Trip t{0.0, 0.0};
    CHECK(!advance(t, 0.0, 0.0, 1.0));               // stopped
    CHECK(!advance(t, 299.0, 1.0, 1.0));             // below running threshold
    CHECK(!advance(t, 2000.0, 2.0, 0.0));            // zero dt
    CHECK(t.fuelL == 0.0 && t.runSeconds == 0.0);
    for (int i = 0; i < 3600; i++) advance(t, 2000.0, rateAt(d, 2000.0), 1.0);
    NEAR(t.fuelL, 2.0);                              // 1 h at 2.0 L/h
    NEAR(t.runSeconds, 3600.0);
    NEAR(avgLph(t), 2.0);

    Trip young{0.5, 30.0};
    NEAR(avgLph(young), NA);                         // less than a minute: not available

    // --- trip clamp ---
    Trip big{MAX_TRIP_L - 0.001, 10.0};
    advance(big, 3000.0, 50.0, 3600.0);
    NEAR(big.fuelL, MAX_TRIP_L);

    // --- validateTrip ---
    CHECK(validateTrip(Trip{0.0, 0.0}));
    CHECK(validateTrip(Trip{MAX_TRIP_L, MAX_TRIP_RUN_S}));
    CHECK(!validateTrip(Trip{NaN, 0.0}));
    CHECK(!validateTrip(Trip{0.0, MAX_TRIP_RUN_S + 1}));

    // --- automatic new trip (state machine) ---
    {
        AutoTrip a = defaultAutoTrip();
        CHECK(a.newTripMin == 60);
        CHECK(validateAutoTrip(a));
        Trip s{0.0, 0.0};

        // running accumulates and keeps the off-counter at 0
        StepResult r = step(s, a, 2000.0, 2.0, 1.0);
        CHECK(r.tripChanged && !r.newTrip);
        NEAR(s.runSeconds, 1.0);
        NEAR(a.offSeconds, 0.0);

        // engine off: counter grows, trip stays visible and unchanged
        for (int i = 0; i < 59 * 60; i++) step(s, a, 0.0, 0.0, 1.0);
        NEAR(a.offSeconds, 59 * 60.0);
        NEAR(s.runSeconds, 1.0);

        // restart after 59 min: same trip continues
        r = step(s, a, 2000.0, 2.0, 1.0);
        CHECK(!r.newTrip);
        NEAR(s.runSeconds, 2.0);
        NEAR(a.offSeconds, 0.0);

        // off for exactly 60 min: next start begins a new trip
        for (int i = 0; i < 60 * 60; i++) step(s, a, 0.0, 0.0, 1.0);
        NEAR(s.runSeconds, 2.0);                     // old trip still shown while stopped
        r = step(s, a, 2000.0, 3600.0, 1.0);         // 1 L in this second
        CHECK(r.newTrip && r.tripChanged);
        NEAR(s.runSeconds, 1.0);
        NEAR(s.fuelL, 1.0);
        NEAR(a.offSeconds, 0.0);

        // below running threshold counts as off
        step(s, a, 299.0, 1.0, 10.0);
        NEAR(a.offSeconds, 10.0);

        // newTripMin = 0 disables the automatic new trip
        AutoTrip never{0, 0.0};
        Trip keepTrip{5.0, 100.0};
        for (int i = 0; i < 200; i++) step(keepTrip, never, 0.0, 0.0, 3600.0);
        CHECK(never.offSeconds == MAX_OFF_S);        // capped
        r = step(keepTrip, never, 2000.0, 2.0, 1.0);
        CHECK(!r.newTrip);
        NEAR(keepTrip.runSeconds, 101.0);

        // an empty trip is not "restarted" (no spurious newTrip)
        AutoTrip longOff{60, 7200.0};
        Trip empty{0.0, 0.0};
        r = step(empty, longOff, 2000.0, 2.0, 1.0);
        CHECK(!r.newTrip && r.tripChanged);

        // zero dt is ignored
        AutoTrip z = defaultAutoTrip();
        Trip zt{1.0, 1.0};
        r = step(zt, z, 0.0, 0.0, 0.0);
        CHECK(!r.tripChanged && z.offSeconds == 0.0);
    }

    // --- validateAutoTrip / parseNewTripMin ---
    CHECK(validateAutoTrip(AutoTrip{0, 0.0}));
    CHECK(validateAutoTrip(AutoTrip{MAX_NEW_TRIP_MIN, MAX_OFF_S}));
    CHECK(!validateAutoTrip(AutoTrip{MAX_NEW_TRIP_MIN + 1, 0.0}));
    CHECK(!validateAutoTrip(AutoTrip{60, -1.0}));
    CHECK(!validateAutoTrip(AutoTrip{60, NaN}));
    CHECK(!validateAutoTrip(AutoTrip{60, MAX_OFF_S + 1}));
    uint32_t nm = 7;
    CHECK(parseNewTripMin("45", nm) && nm == 45);
    CHECK(parseNewTripMin("0", nm) && nm == 0);
    CHECK(parseNewTripMin("1440", nm) && nm == 1440);
    nm = 7;
    CHECK(!parseNewTripMin("", nm));
    CHECK(!parseNewTripMin("abc", nm));
    CHECK(!parseNewTripMin("12x", nm));
    CHECK(!parseNewTripMin("-1", nm));
    CHECK(!parseNewTripMin("1441", nm));
    CHECK(!parseNewTripMin("99999999999999999999", nm));
    CHECK(!parseNewTripMin(nullptr, nm));
    CHECK(nm == 7);                                  // untouched after every failure

    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
