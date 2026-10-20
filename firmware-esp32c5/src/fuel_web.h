#pragma once

// Web UI + JSON API for the fuel table and trip data (plan Sub-Task 6B).
//   GET  /fuel                    editor page
//   GET  /api/fuel                live values, table and trip as JSON
//   POST /api/fuel/curve          body "rpm:lph,rpm:lph,..."   replaces the table
//   POST /api/fuel/curve/reset    restores the default table
//   POST /api/fuel/trip/reset     zeroes the trip now
//   POST /api/fuel/newtrip        body "<minutes>" (0-1440, 0 = never)  engine-off time for an automatic new trip
// Every write needs the header "X-Gateway: 1". A browser will not add a custom
// header on a cross-site request without a CORS preflight, which this server
// never answers, so another web page cannot change values through your browser.
// There is no login: use it on the boat's own network only.

#include <stdio.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "fuel_service.h"
#include "engine_state.h"
#include "bus_status.h"
#include "gps_state.h"
#include "esp_timer.h"

namespace FuelWeb {

static const char *TAG = "FUEL_WEB";
static const size_t MAX_BODY = 512;

static esp_err_t api_get(httpd_req_t *req) {
    FuelService::Snapshot s = FuelService::snapshot();
    EngineState es = EngineTelemetry::getInstance().getState();
    // Values are N2kDoubleNA (-1e9) when the engine is not reporting.
    bool haveCoolant = es.coolantTemp > 0.0;
    bool haveData = es.rpm > N2kDoubleNA || haveCoolant;
    uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
    char buf[1500];
    int n = snprintf(buf, sizeof(buf),
        "{\"running\":%s,\"rpm\":%.0f,\"rate_lph\":%.2f,\"trip_fuel_l\":%.3f,\"trip_run_s\":%.0f,"
        "\"new_trip_min\":%u,\"engine_off_min\":%.1f,\"engine_data\":%s,"
        "\"j1939_active\":%s,\"n2k_active\":%s,"
        "\"oil_alarm\":%s,\"temp_alarm\":%s,\"coolant_c\":",
        s.running ? "true" : "false", s.rpm, s.rateLph, s.trip.fuelL, s.trip.runSeconds,
        (unsigned)s.newTripMin, s.offSeconds / 60.0, haveData ? "true" : "false",
        BusStatus::j1939Bus.active(nowMs) ? "true" : "false",
        BusStatus::n2kBus.active(nowMs) ? "true" : "false",
        es.lowOilPressure ? "true" : "false", es.overTemp ? "true" : "false");
    if (haveCoolant) n += snprintf(buf + n, sizeof(buf) - n, "%.1f", es.coolantTemp - 273.15);
    else n += snprintf(buf + n, sizeof(buf) - n, "null");
    if (es.engineHours > 0.0) n += snprintf(buf + n, sizeof(buf) - n, ",\"hours\":%.2f", es.engineHours / 3600.0);
    else n += snprintf(buf + n, sizeof(buf) - n, ",\"hours\":null");
    if (es.alternatorV > 0.0) n += snprintf(buf + n, sizeof(buf) - n, ",\"batt_v\":%.2f", es.alternatorV);
    else n += snprintf(buf + n, sizeof(buf) - n, ",\"batt_v\":null");
    n += snprintf(buf + n, sizeof(buf) - n, ",\"avg_lph\":");
    if (s.avgLph < 0.0) n += snprintf(buf + n, sizeof(buf) - n, "null");
    else n += snprintf(buf + n, sizeof(buf) - n, "%.2f", s.avgLph);
    GpsState::Snapshot gps = GpsState::snapshot();
    if (gps.havePos) n += snprintf(buf + n, sizeof(buf) - n, ",\"gps_lat\":%.6f,\"gps_lon\":%.6f", gps.lat, gps.lon);
    else n += snprintf(buf + n, sizeof(buf) - n, ",\"gps_lat\":null,\"gps_lon\":null");
    if (gps.haveTime) {
        char t[24];
        GpsState::formatTime(gps.days, gps.secs, t, sizeof(t));
        n += snprintf(buf + n, sizeof(buf) - n, ",\"gps_time\":\"%s\"", t);
    } else {
        n += snprintf(buf + n, sizeof(buf) - n, ",\"gps_time\":null");
    }
    n += snprintf(buf + n, sizeof(buf) - n, ",\"curve\":[");
    for (uint32_t i = 0; i < s.curve.count && n < (int)sizeof(buf) - 40; i++) {
        n += snprintf(buf + n, sizeof(buf) - n, "%s[%.0f,%.2f]", i ? "," : "",
                      s.curve.pts[i].rpm, s.curve.pts[i].lph);
    }
    n += snprintf(buf + n, sizeof(buf) - n, "]}");
    if (n <= 0 || n >= (int)sizeof(buf)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Response error");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

// Checks the custom header and reads the body into `body` (NUL terminated).
// On failure it has already sent the error response.
static bool read_write_request(httpd_req_t *req, char *body, size_t cap, bool wantBody) {
    if (httpd_req_get_hdr_value_len(req, "X-Gateway") == 0) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Missing X-Gateway header");
        return false;
    }
    size_t len = req->content_len;
    if (!wantBody) {
        body[0] = '\0';
        return true;
    }
    if (len == 0 || len >= cap) {
        httpd_resp_send_err(req, len == 0 ? HTTPD_400_BAD_REQUEST : HTTPD_413_CONTENT_TOO_LARGE,
                            len == 0 ? "Empty body" : "Body too large");
        return false;
    }
    size_t got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, body + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Receive failed");
            return false;
        }
        got += (size_t)r;
    }
    body[got] = '\0';
    if (strlen(body) != got) {  // embedded NUL
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return false;
    }
    return true;
}

static esp_err_t reply(httpd_req_t *req, bool ok, const char *okText, const char *failText) {
    if (ok) return httpd_resp_sendstr(req, okText);
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, failText);
}

static esp_err_t curve_post(httpd_req_t *req) {
    char body[MAX_BODY];
    if (!read_write_request(req, body, sizeof(body), true)) return ESP_FAIL;
    FuelModel::Curve c;
    if (!FuelModel::parseCurve(body, c)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
            "Invalid table: use 2-12 rows, rpm ascending (1-6000), 0-50 L/h");
    }
    return reply(req, FuelService::setCurve(c), "OK", "Could not store table");
}

static esp_err_t curve_reset_post(httpd_req_t *req) {
    char body[8];
    if (!read_write_request(req, body, sizeof(body), false)) return ESP_FAIL;
    return reply(req, FuelService::resetCurveToDefault(), "OK", "Could not store table");
}

static esp_err_t trip_reset_post(httpd_req_t *req) {
    char body[8];
    if (!read_write_request(req, body, sizeof(body), false)) return ESP_FAIL;
    return reply(req, FuelService::resetTrip(), "OK", "Could not store trip");
}

static esp_err_t newtrip_post(httpd_req_t *req) {
    char body[16];
    if (!read_write_request(req, body, sizeof(body), true)) return ESP_FAIL;
    uint32_t minutes;
    if (!FuelModel::parseNewTripMin(body, minutes)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
            "Invalid value: whole minutes 0-1440 (0 = never start a new trip automatically)");
    }
    return reply(req, FuelService::setNewTripMin(minutes), "OK", "Could not store setting");
}

// Registers all fuel URIs on an already started HTTP server.
inline bool register_handlers(httpd_handle_t server) {
    if (!server) return false;
    const httpd_uri_t uris[] = {
        {.uri = "/api/fuel",              .method = HTTP_GET,  .handler = api_get,          .user_ctx = NULL},
        {.uri = "/api/fuel/curve",        .method = HTTP_POST, .handler = curve_post,       .user_ctx = NULL},
        {.uri = "/api/fuel/curve/reset",  .method = HTTP_POST, .handler = curve_reset_post, .user_ctx = NULL},
        {.uri = "/api/fuel/trip/reset",   .method = HTTP_POST, .handler = trip_reset_post,  .user_ctx = NULL},
        {.uri = "/api/fuel/newtrip",      .method = HTTP_POST, .handler = newtrip_post,     .user_ctx = NULL},
    };
    for (const httpd_uri_t &u : uris) {
        if (httpd_register_uri_handler(server, &u) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s", u.uri);
            return false;
        }
    }
    return true;
}

} // namespace FuelWeb
