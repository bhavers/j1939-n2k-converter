#pragma once

// Pure fuel-estimation maths (no ESP-IDF dependencies, unit-tested on the host).
// See design/esp32c5/implementation-plan.md, Sub-Task 6B.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>

namespace FuelModel {

constexpr uint32_t MAX_POINTS     = 12;
constexpr uint32_t MIN_POINTS     = 2;
constexpr float    MAX_RPM        = 6000.0f;
constexpr float    MAX_LPH        = 50.0f;
constexpr double   RUNNING_RPM    = 300.0;        // below this the engine counts as stopped
constexpr double   MAX_TRIP_L     = 60000.0;      // PGN 127497 TripFuelUsed is 16 bit, 1 L/bit
constexpr double   MAX_TRIP_RUN_S = 360000000.0;  // 100 000 h
constexpr double   MIN_AVG_RUN_S  = 60.0;         // average is "not available" before this
constexpr uint32_t DEFAULT_NEW_TRIP_MIN = 60;     // engine off this long -> next start is a new trip
constexpr uint32_t MAX_NEW_TRIP_MIN     = 1440;   // 24 h; 0 disables the automatic new trip
constexpr double   MAX_OFF_S      = 7.0 * 86400.0; // off-time counter stops growing here
constexpr double   NA             = -1.0;

struct CurvePoint {
    float rpm;
    float lph;
};

struct Curve {
    uint32_t   count;
    CurvePoint pts[MAX_POINTS];
};

struct Trip {
    double fuelL;
    double runSeconds;
};

// Automatic new-trip settings and state. Only newTripMin is stored in NVS;
// offSeconds is RAM-only because time cannot be counted while unpowered.
struct AutoTrip {
    uint32_t newTripMin;   // 0 = never start a new trip automatically
    double   offSeconds;   // engine-off time since the trip last ran (only counted while powered)
};

inline AutoTrip defaultAutoTrip() { return AutoTrip{DEFAULT_NEW_TRIP_MIN, 0.0}; }

// Default curve: D1-30 values from the myHanse forum thread
// https://www.myhanse.com/average-fuel-consumption_topic11698.html
// Source details: design/fuel-consumption-source.md
inline Curve defaultCurve() {
    static const CurvePoint d[] = {
        {1400.0f, 1.0f}, {1600.0f, 0.9f}, {1800.0f, 1.5f}, {2000.0f, 2.0f},
        {2200.0f, 2.5f}, {2400.0f, 2.8f}, {2800.0f, 4.5f}, {3200.0f, 6.6f},
    };
    Curve c{};
    c.count = sizeof(d) / sizeof(d[0]);
    for (uint32_t i = 0; i < c.count; i++) c.pts[i] = d[i];
    return c;
}

// Points must be strictly ascending in rpm, finite, and within sane limits.
inline bool validateCurve(const Curve &c) {
    if (c.count < MIN_POINTS || c.count > MAX_POINTS) return false;
    float prev = 0.0f;
    for (uint32_t i = 0; i < c.count; i++) {
        const CurvePoint &p = c.pts[i];
        if (!std::isfinite(p.rpm) || !std::isfinite(p.lph)) return false;
        if (p.rpm <= prev || p.rpm > MAX_RPM) return false;
        if (p.lph < 0.0f || p.lph > MAX_LPH) return false;
        prev = p.rpm;
    }
    return true;
}

inline bool validateAutoTrip(const AutoTrip &a) {
    return a.newTripMin <= MAX_NEW_TRIP_MIN && std::isfinite(a.offSeconds) &&
           a.offSeconds >= 0.0 && a.offSeconds <= MAX_OFF_S;
}

inline bool validateTrip(const Trip &t) {
    return std::isfinite(t.fuelL) && std::isfinite(t.runSeconds) &&
           t.fuelL >= 0.0 && t.fuelL <= MAX_TRIP_L &&
           t.runSeconds >= 0.0 && t.runSeconds <= MAX_TRIP_RUN_S;
}

// Linear interpolation. Below the first point the rate scales linearly from
// 0 L/h at 0 rpm; above the last point it stays at the last value.
inline double rateAt(const Curve &c, double rpm) {
    if (!(rpm > 0.0) || c.count < 1) return 0.0;
    const CurvePoint *p = c.pts;
    if (rpm <= p[0].rpm) return p[0].lph * rpm / p[0].rpm;
    if (rpm >= p[c.count - 1].rpm) return p[c.count - 1].lph;
    for (uint32_t i = 1; i < c.count; i++) {
        if (rpm <= p[i].rpm) {
            double t = (rpm - p[i - 1].rpm) / (double)(p[i].rpm - p[i - 1].rpm);
            return p[i - 1].lph + t * (p[i].lph - p[i - 1].lph);
        }
    }
    return p[c.count - 1].lph;
}

// Adds dtS seconds of running to the trip. Returns true if the trip changed.
inline bool advance(Trip &t, double rpm, double rateLph, double dtS) {
    if (!(rpm >= RUNNING_RPM) || !(dtS > 0.0)) return false;
    t.fuelL      += rateLph * dtS / 3600.0;
    t.runSeconds += dtS;
    if (t.fuelL > MAX_TRIP_L) t.fuelL = MAX_TRIP_L;
    if (t.runSeconds > MAX_TRIP_RUN_S) t.runSeconds = MAX_TRIP_RUN_S;
    return true;
}

struct StepResult {
    bool tripChanged;  // trip totals changed (running, or reset by a new trip)
    bool newTrip;      // an automatic new trip was started
};

// One time step of the trip state machine.
// - Engine running: the off-time counter is cleared and fuel/run time accumulate.
//   If the engine had been off for at least newTripMin minutes, the old trip is
//   discarded first. Until then the old trip stays visible while moored.
// - Engine off: the off-time counter grows (capped at MAX_OFF_S).
inline StepResult step(Trip &t, AutoTrip &a, double rpm, double rateLph, double dtS) {
    StepResult r{false, false};
    if (!(dtS > 0.0)) return r;
    if (rpm >= RUNNING_RPM) {
        if (a.newTripMin > 0 && a.offSeconds >= a.newTripMin * 60.0 &&
            (t.fuelL > 0.0 || t.runSeconds > 0.0)) {
            t = Trip{0.0, 0.0};
            r.newTrip = true;
        }
        a.offSeconds = 0.0;
        r.tripChanged = advance(t, rpm, rateLph, dtS) || r.newTrip;
    } else {
        a.offSeconds += dtS;
        if (a.offSeconds > MAX_OFF_S) a.offSeconds = MAX_OFF_S;
    }
    return r;
}

// Parses the new-trip delay in whole minutes (0..MAX_NEW_TRIP_MIN).
inline bool parseNewTripMin(const char *text, uint32_t &out) {
    if (!text || !*text) return false;
    char *end = nullptr;
    long v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < 0 || v > (long)MAX_NEW_TRIP_MIN) return false;
    out = (uint32_t)v;
    return true;
}

// Average rate over the trip, or NA when there is not enough data yet.
inline double avgLph(const Trip &t) {
    if (t.runSeconds < MIN_AVG_RUN_S) return NA;
    return t.fuelL / (t.runSeconds / 3600.0);
}

// Parses "rpm:lph,rpm:lph,..." into a validated curve. Leaves `out` untouched on failure.
inline bool parseCurve(const char *text, Curve &out) {
    if (!text) return false;
    Curve c{};
    const char *p = text;
    while (*p) {
        if (c.count >= MAX_POINTS) return false;
        char *end = nullptr;
        float rpm = strtof(p, &end);
        if (end == p || *end != ':') return false;
        p = end + 1;
        float lph = strtof(p, &end);
        if (end == p) return false;
        c.pts[c.count].rpm = rpm;
        c.pts[c.count].lph = lph;
        c.count++;
        p = end;
        if (*p == ',') {
            p++;
            if (!*p) return false;  // trailing comma
        } else if (*p) {
            return false;
        }
    }
    if (!validateCurve(c)) return false;
    out = c;
    return true;
}

} // namespace FuelModel
