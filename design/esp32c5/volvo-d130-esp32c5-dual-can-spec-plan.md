# Specification & Architecture Plan: Volvo Penta D1 series to NMEA 2000 Gateway (SmuqDev ESP32-C5 Dual Isolated CAN FD)

> Implementation specification for the J1939 → NMEA 2000 gateway. Plan, status and test log: [`implementation-plan.md`](implementation-plan.md). Other platforms that were evaluated: [`hardware-comparison.md`](../hardware-comparison.md).

---

## 1. Top-Level Overview & Design Intent

This specification defines the hardware architecture, galvanic isolation, wiring, J1939-to-NMEA 2000 protocol translation, and firmware implementation for an engine telemetry and alarm gateway for the **Volvo Penta D1 series** (tested specifically on a **D1-30B**).

The gateway intercepts the SAE J1939 data broadcast by the Volvo Penta MDI box via an inline Deutsch DT 6-pin harness, decodes the engine telemetry and alarm states, and broadcasts standard NMEA 2000 PGNs onto the N2K backbone.

CAN numbering in this document follows the terminals on the board and the web page: **CAN 1 = J1939 (engine)**, **CAN 2 = NMEA 2000**.

### 1.1 Core Hardware

* **Board**: [SmuqDev ESP32-C5 Dual Isolated CAN FD — Gateway & Logger](https://www.tindie.com/products/smuqdev/esp32-c5-dual-can-fd-wi-fi-6-gateway-logger/) (Tindie, ~$59). Why this board: [`hardware-comparison.md`](../hardware-comparison.md).
* **MCU**: Espressif ESP32-C5, RISC-V single-core @ 240 MHz, 16 MB Flash, 8 MB PSRAM
* **Terminal CAN 1**: Volvo Penta MDI J1939 (250 kbps, listen-only), GPIO 23/24
* **Terminal CAN 2**: NMEA 2000 backbone (250 kbps, active mode, full address claiming), GPIO 4/5
* **Transceivers**: TI ISO1044BDR — galvanically isolated, per channel, with dedicated DC-DC converters
* **Termination**: On-board 120Ω per channel, switchable via DIP switch (both **OFF** for this installation)
* **Extras**: MicroSD card slot + RTC (on the board, not used by the firmware)

### 1.2 N2K Device Identity

Product name `"J1939-N2K Gateway"`, NMEA 2000 Device Class **50** (Propulsion), Device Function **140** (Engine Gateway), Unique Number `00000001`, Manufacturer Code `2046`. The node claims source address 22 and sends engine instance 0.

---

## 2. Electrical Architecture & Galvanic Isolation

Both CAN ports are galvanically isolated at the board level using TI ISO1044BDR transceivers with an independent DC-DC converter per channel. This eliminates ground loops between the engine battery domain and the house/N2K domain without additional components. Cable resistance in a shared ground rail can create voltage differences of 0.5–2 V during starter-motor cranking; the isolation makes this a non-issue for this build.

```
 Volvo Penta MDI ── Deutsch DT harness ── VDO tachometer
                          │ CAN_H, CAN_L, GND
                  ┌───────┴────────┐
                  │ CAN 1 (J1939)  │   isolated side 1
                  │                │
                  │  ESP32-C5 MCU  │ ◄── VIN (N2K 12 V) via AP63203 buck, 3.3 V
                  │                │
                  │ CAN 2 (N2K)    │   isolated side 2
                  └───────┬────────┘
                          │ CAN_H, CAN_L, GND_ISO
 NMEA 2000 backbone ──────┘ (Micro-C drop, 12 V + data)
```

### 2.1 Power Supply

The board is powered from a **single VIN screw terminal**, fed by the NMEA 2000 drop cable (NET-S +12 V / NET-C GND). The Deutsch DT pin 6 (+12 V switched) is **not** connected to the board.

Per the board schematic ([`can_schematic.png`](can_schematic.png)), the on-board regulator (AP63203) converts VIN to 3.3 V for the MCU domain. Each CAN channel has a **B0305S-1WR3** isolated DC-DC converter (3.3 V in → 5 V out) that generates the bus-side supply (`VCC_ISO` / `GND_ISO`). The **ISO1044BDR** separates the MCU side (`VCC1`/`GND1`) from the bus side (`VCC2`/`GND2`). The bus lines also have a common-mode choke (ACM2012L-121) and a TVS diode (CDSOT23-T24CAN) for EMI and ESD protection.

```
VIN (N2K 12V) ──→ AP63203 ──→ 3.3V MCU domain (VCC1)
                             ├──→ B0305S-1WR3 DC-DC #1 ──→ VCC_ISO ch1 ──→ CAN 1 bus side (J1939)
                             └──→ B0305S-1WR3 DC-DC #2 ──→ VCC_ISO ch2 ──→ CAN 2 bus side (N2K)
```

**Confirmed by the board maker:**

- The AP63203 is rated for **up to 32 V DC** (absolute max 35 V). A typical N2K supply of 12–14 V is well within range.
- Powering from the NMEA 2000 backbone is recommended: the N2K supply is clean and filtered, and protects the board from inductive kickbacks and alternator load dumps that raw starter-battery circuits can produce.
- Do **not** wire VIN directly to the engine starter battery circuit without an external automotive surge protector.
- At 12–14 V the regulator runs cool; higher input voltages produce more heat, so ensure airflow in the enclosure.

### 2.2 Ground Connection (Engine Side)

Deutsch pin 4 (engine ground) connects **only to the isolated side of the CAN 1 transceiver** (`GND_ISO`). There is no electrical path between engine ground and the ESP32 logic domain or the N2K network ground. No external resistor or isolation component is required.

### 2.3 USB Power Jumper & Status LEDs

**LEDs at power-up:** a green LED near VBUS/USB shows 5 V to the MCU domain. One yellow LED per CAN channel (next to the isolation barrier) shows that the `VCC_ISO` supply of that channel is active.

The **power jumper next to the USB port** controls whether USB power reaches the board:

| Jumper state | Effect |
| :--- | :--- |
| **Installed** (factory default) | USB 5 V rail connected to board power. The board can be powered from USB alone. |
| **Removed** | USB 5 V rail disconnected. USB data lines stay connected, so flashing and serial monitoring still work. |

- **Powered from the N2K backbone (VIN terminal): leave the jumper out.** This is the normal installation and the maker's recommendation. It prevents a 5 V/12 V power conflict when a laptop is connected via USB-C. (A protective diode on the USB rail means leaving it in would not cause damage.)
- **Powered from USB-C only (bench work, J1939 test): put the jumper in.** With the jumper out, USB-C does not power the board. Take it out again before connecting the board to the N2K backbone.
- **Disconnect any mains-powered laptop charger** before connecting USB while the board is live on the N2K backbone, to avoid introducing a mains ground reference.
- **OTA firmware updates** over Wi-Fi are the preferred update path after the first flash (see the [README](../../README.md#build-and-flash)).

---

## 3. Physical Wiring & Pinout Specification

### 3.1 Volvo Penta Multilink Deutsch DT 6-Pin Harness

The inline harness passes all 6 lines between the MDI and the VDO tachometer, with a 3-wire tap to the board's terminal CAN 1. The tachometer sees no change in electrical loading. Only these three pins go to the board; all six pins, wire colours, bill of materials and assembly steps are in [`deutsch-dt-y-splitter.md`](../deutsch-dt-y-splitter.md) (the pin numbers were **measured on the engine**; the ones first assumed were wrong).

| Deutsch pin | Signal | Board connection |
| :--- | :--- | :--- |
| **2** | J1939 CAN Low (250 kbps) | terminal CAN 1: **CAN_L** |
| **4** | Engine ground (battery −) | terminal CAN 1: **GND** (`GND_ISO`) |
| **5** | J1939 CAN High (250 kbps) | terminal CAN 1: **CAN_H** |

Pin 6 carries +12 V switched and must **never** be connected to the board. Pins 1 and 3 are not identified and are not connected.

### 3.2 NMEA 2000 Micro-C Drop Cable to Board Terminals

A standard NMEA 2000 drop cable connects to the board's terminal CAN 2 and the power input:

| N2K Micro-C colour | Signal | Board connection | Notes |
| :--- | :--- | :--- | :--- |
| **Red** | NET-S (+12 V) | **Power In: VIN** | Powers the board (typically 12–14 V) |
| **Black** | NET-C (0 V) | **Power In: GND** | N2K power return |
| **White** | NET-H (CAN High) | **terminal CAN 2: CAN_H** | |
| **Blue** | NET-L (CAN Low) | **terminal CAN 2: CAN_L** | |
| **Bare / shield** | Drain | **terminal CAN 2: SHIELD / GND** | RF noise suppression |

### 3.3 Termination Resistor Configuration

Both on-board 120Ω terminators are **disabled (DIP switch OFF)**:

- **CAN 1 (J1939)**: the Volvo MDI box and VDO tachometer already terminate the engine bus. A third terminator would drop the impedance to about 40Ω. The bus works with the switch OFF (a measured 118 Ω instead of about 60 Ω is explained in [`deutsch-dt-y-splitter.md`](../deutsch-dt-y-splitter.md), section 4).
- **CAN 2 (N2K)**: the backbone is terminated at its two ends by standard terminators. A board on a stub drop must not add termination.

---

## 4. Protocol & Data Mapping Specification

### 4.1 Bus Parameters

- **Engine CAN (terminal CAN 1)**: SAE J1939, **250 kbps**, 29-bit identifiers, **listen-only** — the gateway sends no ACK or error frames onto the MDI bus.
- **NMEA 2000 CAN (terminal CAN 2)**: NMEA 2000 / ISO 11783, **250 kbps**, 29-bit identifiers, classic CAN in normal mode (it must ACK), full dynamic ISO address claiming.

### 4.2 J1939 to NMEA 2000 Translation Matrix

| Telemetry Parameter | Source J1939 PGN | J1939 SPN & Byte Layout | Target N2K PGN | N2K Field & Scaling | Update Rate |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Engine Speed (RPM)** | `61444 (0xF004)` — EEC1 | SPN 190 (Bytes 4–5): `0.125 RPM/bit`, 0 offset | `127488` (Rapid Update) | `EngineSpeed` (0.25 Hz/bit → rad/s) | 10 Hz (100 ms) |
| **Coolant Temperature** | `65262 (0xFEEE)` — ET1 | SPN 110 (Byte 1): `1 °C/bit`, −40 °C offset | `127489` (Dynamic) | `EngineCoolantTemperature` (0.01 K/bit) | 1 Hz |
| **Engine Total Hours** | `65253 (0xFEE5)` — HOURS | SPN 247 (Bytes 1–4): `0.05 hr/bit`, 0 offset | `127489` (Dynamic) | `EngineHours` (1 s/bit) | 1 Hz |
| **Battery / Alternator Potential** | `65271 (0xFEF7)` — VEP1 | `0.05 V/bit`, 0 offset; the first valid field of bytes 3–4 (SPN 167), 5–6 (SPN 168), 7–8 (SPN 158). On the D1-30B the voltage is in bytes 7–8 | `127489` (Dynamic) | `AlternatorPotential` (0.01 V/bit) | 1 Hz |
| **Low Oil Pressure Alarm** | `65226 (0xFECA)` — DM1 | SPN 100 / active DTC | `127489` (Dynamic) | `DiscreteStatus1.LowOilPressure` | Event / 1 Hz |
| **Over-Temperature Alarm** | `65226 (0xFECA)` — DM1 | SPN 110 / active DTC | `127489` (Dynamic) | `DiscreteStatus1.OverTemperature` | Event / 1 Hz |
| **Low Voltage / Charge Alarm** | `65226 (0xFECA)` — DM1 | SPN 168 / active DTC | `127489` (Dynamic) | `DiscreteStatus1.LowVoltage / ChargeIndicator` | Event / 1 Hz |
| **Fuel Rate (estimated)** | *none — the D1-30 does not report fuel on J1939* | Derived from RPM (SPN 190) via the editable fuel table, see section 4.6 | `127489` (Dynamic) | `FuelRate` (L/h, 0.1 L/h/bit) | 1 Hz |
| **Trip Fuel Used / Average Rate (estimated)** | *none — integrated on the gateway* | Σ(fuel rate × Δt); average = trip fuel ÷ trip run time | `127497` (Trip Parameters, Engine) | `TripFuelUsed` (L), `FuelRateAverage` (L/h) | 1 Hz |

### 4.3 Engine Telemetry State

A single mutex-protected struct, `EngineState` in [`engine_state.h`](../../firmware-esp32c5/src/engine_state.h), holds the decoded values in N2K units (rad/s, K, s, V) and three alarm flags. The J1939 receive task writes it; the N2K transmit timers read it.

**Engine-stop watchdog:** if no J1939 frame arrived for more than 3000 ms, all values become `N2kDoubleNA` and the alarm flags are cleared, so chartplotters do not keep a frozen reading after engine shutdown.

### 4.4 Undecoded PGNs

Eight PGNs on the engine bus are not decoded (65417, 65420, 65352, 65373, 65283, 65256, 65376, 65276). Recordings and analysis: [`design/captures/README.md`](../captures/README.md).

### 4.5 DM1 Limit

Only a single-frame DM1 message is decoded; several simultaneous faults (transport protocol) are not reassembled.

### 4.6 Fuel Estimate and Trip

The gateway estimates the fuel rate from engine speed using an editable RPM → L/h table (2–12 points, stored in flash, editable on the settings page), and integrates it into trip totals. The result is an **estimate, not a measurement**: expect 10–20 % error from load changes (current, wind, fouling). Label it "estimated" in any documentation.

- **Default table**: D1-30 owner's figures from [myHanse](https://www.myhanse.com/average-fuel-consumption_topic11698.html) (details in [`fuel-consumption-source.md`](../fuel-consumption-source.md)): 1400 rpm 1.0 · 1600 0.9 · 1800 1.5 · 2000 2.0 · 2200 2.5 · 2400 2.8 · 2800 4.5 · 3200 6.6 L/h. Other owners on the same thread report about 2 L/h at 2100–2200 rpm, so expect the table to read slightly high around cruise speed. Calibrate on your own installation (fill the tank, run a fixed rpm, compare).
- **Interpolation**: below the first point the rate scales linearly from 0 L/h at 0 rpm; above the last point it stays at the last value; in between it interpolates linearly.
- **Trip maths**: trip fuel and trip run time only advance while RPM ≥ 300. Average rate = trip fuel ÷ trip run time (N/A during the first 36 s of a trip).
- **Storage**: the trip is saved to flash every 60 s while changing, and immediately when edited on the page. Worst case after power loss: 1 minute of trip data.
- **Automatic new trip**: the gateway counts how long the engine has been off (RPM < 300). When that reaches the editable "new trip after" time (default **60 min**, 1–1440, 0 = never automatic) and the engine is started again, the trip is zeroed. Until then the old trip stays visible, so the MFD still shows the last trip while moored. Starting within the set time continues the same trip. **Limit**: off time is only counted while the gateway has power; after a power-up it starts at 0. If power is switched off for the night, the trip continues until the engine has been off for the full time with the gateway powered, or until "Reset now" is pressed.
- **Web API**: `/api/fuel`, `/api/fuel/curve`, `/api/fuel/trip/reset`, `/api/fuel/newtrip`, with server-side input validation. Writes need the `X-Gateway` header. The page has no login and is reachable only through the password-protected access point.

---

## 5. Firmware Architecture (PlatformIO / ESP-IDF)

### 5.1 Libraries & Software Stack

* **Build system**: PlatformIO (`platform = espressif32`, `framework = espidf`).
* **NMEA 2000 stack**: [`NMEA2000`](https://github.com/ttlappalainen/NMEA2000) by Timo Lappalainen, with the project's own CAN driver class `tNMEA2000_esp32c5` ([`NMEA2000_esp32c5.h`](../../firmware-esp32c5/src/NMEA2000_esp32c5.h)) on the ESP-IDF TWAI v2 API.
* **J1939 driver**: direct ESP-IDF **TWAI v2 multi-controller API** (`esp_twai_onchip.h`). The ESP32-C5 has two native TWAI controllers; the J1939 one (CAN 1) is driven independently of the NMEA2000 library.

> **Why not `NMEA2000_esp32`**: the standard `ttlappalainen/NMEA2000_esp32` library cannot be used on the ESP32-C5. It includes `soc/dport_reg.h` and `rom/gpio.h`, which exist only on the original Xtensa ESP32 and are absent on all RISC-V variants (C3, C5, C6, H2); compiling it fails with `No such file or directory`.

### 5.2 Operating Loop & Data Handling

1. **CAN 2 (N2K)** — [`n2k_gateway.h`](../../firmware-esp32c5/src/n2k_gateway.h):
   - ISO 11783 address claiming on startup.
   - Non-blocking `ParseMessages()` every main loop iteration.
   - Periodic transmit timers: PGN 127488 every **100 ms**, PGN 127489 and PGN 127497 every **1000 ms**.

2. **CAN 1 (J1939)** — [`j1939_receiver.h`](../../firmware-esp32c5/src/j1939_receiver.h):
   - Node created with `twai_new_node_onchip()` at 250 kbps, listen-only.
   - The receive interrupt only copies frames into a queue; a FreeRTOS task dequeues and decodes them (the engine state uses a mutex, which an interrupt must not take).
   - The parser decodes PGN 61444, 65262, 65253, 65271 and 65226 into the engine state.

3. **Engine-stop watchdog**: see section 4.3.
