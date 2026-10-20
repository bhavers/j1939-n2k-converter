# Implementation Plan & Field Test Log: Volvo Penta D1 series to NMEA 2000 Gateway

Quick links: [specification](volvo-d130-esp32c5-dual-can-spec-plan.md) · [README](../../README.md) · [Deutsch harness](../deutsch-dt-y-splitter.md) · [hardware comparison](../hardware-comparison.md) · [fuel table source](../fuel-consumption-source.md) · [captures](../captures/README.md)

Plan and test results for the gateway described in the [specification](volvo-d130-esp32c5-dual-can-spec-plan.md). Working notes for AI assistants are in [`AGENTS.md`](../../AGENTS.md).

Current status: **End to end verified on a real D1-30B with the engine stopped.** Next: run the engine (Sub-Task 8).

---

## 1. Sub-Tasks

| # | Sub-Task | Status |
| :-: | :--- | :--- |
| 1 | Dev environment and initial firmware | done |
| 2 | Initial flash and wireless verification (bench) | done |
| 3 | Deutsch DT harness assembly | done |
| 4 | N2K validation on a bench backbone | done |
| 5 | J1939 listener (harness connected, ignition on) | done with the engine stopped |
| 6 | Bridge to N2K (engine connected, ignition on) | open: items 4 and 7 |
| 6B | Fuel rate estimate and trip parameters | open: item 6 (calibration) |
| 7 | Permanent installation | pending |
| 8 | Full engine validation (engine running) | pending |
| 9 | Publish to GitHub | done |

### Done: Sub-Tasks 1–5

| Sub-Task | What was done | Evidence |
| :--- | :--- | :--- |
| 1 | PlatformIO (`platformio 6.2.0`, `espressif32 7.1.3`) project with the `NMEA2000` library and an OTA partition table. Firmware: Wi-Fi access point, TCP log, OTA, engine state with watchdog, J1939 receiver, N2K driver for the C5. | Clean RISC-V build. |
| 2 | Flashed over USB (serial port must be free). Power jumper out and both termination DIP switches OFF (checked by the owner). TCP log works without USB. OTA over Wi-Fi works and the new image is marked valid. | Many OTA updates. The rollback of a bad image was **not** deliberately tested. |
| 3 | Harness built as a six-wire extension cable joined pin by pin with butt splices ([build guide](../deutsch-dt-y-splitter.md)). Pins 5 / 2 / 4 (CAN_H / CAN_L / GND) go to the board; pin 6 (+12 V) is never connected. | Pin assignment confirmed by receiving J1939 frames. No separate short test between adjacent pins was recorded. The 118 Ω and 10.4 V readings are explained in the build guide, section 4. |
| 4 | N2K drop cable on terminal CAN 2, board powered from the backbone. "J1939-N2K Gateway" appears on the MFD (source address 22). Live PGNs from the MFD received (GPS position and time are shown on the settings page). Simulated PGN 127488 / 127489 / 127497 arrived on the MFD (`N2K_DUMMY_DATA 1`). | Checked by the owner. |
| 5 | TWAI listen-only at 250 kbps; parser for PGN 61444, 65262, 65253, 65271 and 65226 (DM1 decoder host-tested). Harness inserted, ignition on. | See the field test log. VDO tachometer and MDI keep working (checked by the owner). RPM was 0 and DM1 was never seen. |

### Sub-Task 6: Bridge Implementation (engine connected, ignition on)

- **Intent**: decoded J1939 data flows through to the N2K transmitters and reaches the MFD.
- **Done** (engine stopped): engine state wired into PGN 127488, 127489 and 127497 ([`n2k_gateway.h`](../../firmware-esp32c5/src/n2k_gateway.h)); engine hours, RPM = 0, coolant temperature and battery voltage appear on the MFD; the watchdog clears all fields 3 s after ignition off; VDO tachometer and MDI stay normal.
- **Open**:
  - [ ] Item 4: check whether a `LowOilPressure` alarm flag appears in PGN 127489 on key-on. No DM1 message has been seen, so this may not happen.
  - [ ] Item 7: verify zero CAN error counters on both CAN 1 and CAN 2.

### Sub-Task 6B: Fuel Rate Estimate & Trip Parameters

- **Intent**: the D1-30 does not send fuel consumption on J1939, so the gateway estimates it from engine speed. Behaviour, defaults and limits are in the [specification, section 4.6](volvo-d130-esp32c5-dual-can-spec-plan.md#46-fuel-estimate-and-trip).
- **Done**:
  - Model and trip maths with automatic new trip, no ESP dependencies ([`fuel_model.h`](../../firmware-esp32c5/src/fuel_model.h), host-tested).
  - Flash storage and 1 Hz integrator ([`fuel_service.h`](../../firmware-esp32c5/src/fuel_service.h)).
  - JSON API and settings page ([`fuel_web.h`](../../firmware-esp32c5/src/fuel_web.h), [`web_ui.h`](../../firmware-esp32c5/src/web_ui.h)). Checked on the bench: reads, writes, 11 invalid inputs rejected, 403 without the `X-Gateway` header, table and trip survive a reboot.
  - Live view: coolant temperature, oil-pressure and temperature lamps ([`j1939_dm1.h`](../../firmware-esp32c5/src/j1939_dm1.h)), real bus status in the page header ([`bus_status.h`](../../firmware-esp32c5/src/bus_status.h)). The J1939 *Connected* state was confirmed on the real engine bus.
  - Estimated fuel rate in PGN 127489 and trip values in PGN 127497, checked on the bench with simulated data.
- **Open**: [ ] item 6: with the engine running, run at several fixed RPM values, compare estimated and refuelled litres, edit the table, and note the calibration in this document.
- **Not yet verified**: the live chain (real RPM → fuel rate → trip, and engine off → start → new trip) has only been exercised through the unit tests and simulated data.

### Sub-Task 7: Permanent Installation

- [ ] Select an IP65 enclosure that fits behind the instrument panel, with airflow for the AP63203 regulator.
- [ ] Mount the board; route the N2K drop cable and the engine harness through cable glands.
- [ ] Label all connections.
- [ ] Verify the board powers from the N2K backbone in the enclosure and the log is reachable over Wi-Fi.
- [ ] Confirm "J1939-N2K Gateway" still appears on the MFD and all PGNs transmit.

### Sub-Task 8: Full Engine Validation (engine running)

- [ ] Start the engine; verify the `LowOilPressure` alarm clears in PGN 127489 within seconds (if the engine reports it at all).
- [ ] Live RPM in PGN 127488 tracks the VDO tachometer needle and LCD.
- [ ] Coolant temperature rises to the normal range (about 80–88 °C) and tracks the VDO LCD.
- [ ] Zero CAN error frames on both CAN 1 and CAN 2 under cranking, idle and load.
- [ ] Rev to various speeds; confirm RPM resolution and update rate (10 Hz) look smooth on the MFD.
- [ ] Record the undecoded PGNs with the engine running (`J1939_RAW_LOG 1`; steps and checklist in [`design/captures/README.md`](../captures/README.md)). Look at PGN 65276 (possibly fuel level) and 65376.
- [ ] Check the VDO tachometer and the MDI with the engine running.

### Sub-Task 9: Publish to GitHub

Published at <https://github.com/bhavers/j1939-n2k-converter> (public, Apache 2.0).

- [x] `git init` with [`.gitignore`](../../.gitignore) in place.
- [x] `git status --untracked-files=all` does not list `wifi_secrets.h`; `git ls-files` finds 0 matches.
- [x] `git grep -n -i -E "password|ssid"` shows only the placeholders in `wifi_secrets.example.h`, the README and the code comments.
- [x] First commit made, repository created and pushed.

---

## 2. Field Test Log

### Engine: J1939 side (engine stopped, ignition on)

Tested on a D1-30B through the Deutsch DT 6-pin harness (pin assignment in [`deutsch-dt-y-splitter.md`](../deutsch-dt-y-splitter.md) section 4):

- 17,474 frames in 20 s, 0 receive errors, 13 different PGNs, from source addresses 0 and 242.
- Engine hours (PGN 65253): 913.6 h, matches the tachometer exactly.
- Coolant temperature (PGN 65262): 15 °C. Engine speed: 0 (engine stopped).
- Battery voltage (PGN 65271): about 13.0 V. The voltage sits in bytes 7–8 (keyswitch battery), not in bytes 2–3 as first assumed; the decoder reads the first valid voltage field.
- Not seen: PGN 65226 (DM1) and PGN 65263 (oil pressure). It is not known whether this engine sends oil pressure on J1939.
- Unknown PGNs, not decoded: 65417, 65420, 65352, 65373, 65283, 65256, 65376, 65276. A one-minute raw recording (ignition on, engine off) is in [`design/captures/`](../captures/README.md); 65417 and 65420 change (counters), the others are constant and mostly `FF`.

### Engine to MFD (engine stopped, ignition on)

Engine hours, RPM = 0, coolant temperature and battery voltage from the real engine appear on the MFD through the gateway (confirmed by the owner). After ignition off the values disappear from the MFD (engine-off watchdog). The VDO tachometer and the MDI keep working normally. The alarm flags were not checked.

**Still to test:** the engine running (engine speed, coolant temperature rising, voltage with the alternator charging), engine speed under load, CAN error counters on both buses, and the VDO tachometer and MDI with the engine running.

### Bench: CAN bring-up findings

Bench set-up: a 5-way T adapter with terminators at both ends, a Raymarine eS75 MFD, the board, and a 12 V backbone power cable. Terminal CAN 1 = GPIO 23/24 (J1939), terminal CAN 2 = GPIO 4/5 (N2K); this matches [`config.h`](../../firmware-esp32c5/src/config.h).

1. **The bus needs power.** Without the backbone power cable the eS75 sends nothing and the RX pins see 0 edges.
2. **A listen-only node on a quiet bus makes the other device stop sending**: the eS75 sent only about 11 frames and stopped; in normal mode, i.e. with ACKs, it sends about 300 frames/s. That is an explanation, not a separate measurement. The gateway's N2K node transmits, so it is in normal mode anyway; the J1939 node stays listen-only, which is fine because the engine bus is busy by itself.
3. **A wrong bitrate gives only errors.** A node at 1 Mbit/s on a 250 kbit/s bus produces stuff/form errors, ends error-passive with `REC=511` and receives nothing, although the RX pin shows clean 250 kbit/s pulses.
4. **The `GND_ISO` pin must be connected.** The transceiver side is isolated, so it needs a reference to the bus ground. The yellow LED (VCC_ISO) of the channel lights when the isolated side is powered.
5. **What the eS75 sends** (all from source address 0): GNSS position (129025, 129029), COG/SOG (129026), satellites (129540), navigation data (129283–129285), time (126992, 129033), datum (129044), DOPs (129539), plus Raymarine proprietary PGNs (65311, 130848, 130918).
6. **DIP switches (the 4 apart from the termination ones):** the maker's firmware reads them through a PCF8574 on I²C. `firmware-esp32c5/` does not read them; their position has no effect here.
7. **Main firmware on the bench:** with CAN 2 on the powered N2K bus, the TWAI node starts on TX=4/RX=5, `busActive=YES`, the eS75 shows up as device `src=0 mfr=1851 (Raymarine)`, the gateway claims source address 22 and the counter `sent` grows steadily. `sent` counts messages accepted by the library, not frames seen on the bus. Right after boot there is a burst of 16 `tx queue full` errors from the TWAI driver (address claim / product info burst against a queue of 20); it does not repeat and nothing was lost on the MFD. This is a reading of the cause, not measured.
