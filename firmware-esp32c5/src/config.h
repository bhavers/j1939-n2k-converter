#pragma once

// =============================================================================
//  Wi-Fi & Remote Diagnostics Configuration
//  The gateway runs its own access point (AP_SSID / AP_PASSWORD, in the
//  git-ignored wifi_secrets.h).  Create that file by copying
//  wifi_secrets.example.h and filling in your values.
// =============================================================================

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing src/wifi_secrets.h - copy src/wifi_secrets.example.h to src/wifi_secrets.h and set your access point name and password."
#endif

#define TELNET_PORT     23
#define AP_CHANNEL      6

// Board CAN GPIO Configuration (from SmuqDev ESP32-C5 official PINOUT)
#define N2K_TWAI_TX_PIN   GPIO_NUM_4
#define N2K_TWAI_RX_PIN   GPIO_NUM_5

#define J1939_TWAI_TX_PIN GPIO_NUM_23
#define J1939_TWAI_RX_PIN GPIO_NUM_24

// Set to 0 to skip Wi-Fi entirely (USB-only mode, serial monitor only).
#define WIFI_ENABLED 1

// 1 = while no J1939 traffic arrives on CAN 2, send a simulated engine on NMEA 2000
// (800-2200 RPM, 75 C, 13.8 V, 1234.5 h) so the device can be checked on the MFD
// without an engine. Real J1939 data replaces it as soon as frames arrive.
// Keep 0 on the boat, otherwise a stopped engine shows fake values.
#define N2K_DUMMY_DATA 0


// 1 = log every payload change of the J1939 PGNs that are not decoded as
// "[J1939-RAW] t=<ms> SA <sa> PGN <pgn>: <hex>" on the TCP log (port 23) and USB serial,
// for later analysis (capture: nc 192.168.4.1 23 | tee log.txt). Limited to one line per
// PGN and source address per 100 ms and 40 lines per second in total.
// 0 = off (default): only the 5 s overview is printed.
#define J1939_RAW_LOG 0
