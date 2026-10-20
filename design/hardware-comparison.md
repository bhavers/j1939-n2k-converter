# Hardware Platform Comparison: Volvo Penta D1 J1939 → NMEA 2000 Gateway

The platforms that were considered for the gateway, and why the SmuqDev ESP32-C5 was chosen. The gateway is for the **Volvo Penta D1 series** (tested on a **D1-30B**).

## Requirements

| Requirement | Detail |
| :--- | :--- |
| **CAN interfaces** | Two: one J1939 listen-only (250 kbps, 29-bit), one NMEA 2000 active (250 kbps) |
| **Galvanic isolation** | Preferred on both CAN ports |
| **Bus termination** | 120Ω switchable per port (the engine bus is already terminated by the MDI and the tachometer) |
| **Power supply** | Wide input (8–30 V), from the N2K backbone |
| **Connectivity** | Wi-Fi desirable (firmware update, web page) |
| **Assembly** | Screw terminals preferred; minimal soldering |
| **Software** | PlatformIO, NMEA2000 library by T. Lappalainen |

## Options

| | Hat Labs SH-ESP32 + MCP2518FD | SK Pang Teensy 4.0 triple CAN | SK Pang ESP32-S3 CAN FD | **SmuqDev ESP32-C5** |
| :--- | :--- | :--- | :--- | :--- |
| **Result** | Tried, stopped | Not selected | Not selected | **Selected** |
| **CAN** | 1× TWAI + 1× MCP2518FD (SPI) | 3× native | 1× TWAI + 1× MCP2518FD (SPI) | 2× native TWAI |
| **Transceiver on board** | No, missing on the J1939 side | Yes | Yes | Yes |
| **Galvanic isolation** | N2K side only | None | None | Both ports |
| **120 Ω termination** | Jumper | Jumper | Jumper | DIP switch per channel |
| **Wi-Fi** | Yes | No | Yes | Yes (Wi-Fi 6) |
| **Price** | about €60 + transceiver | £57 | £65 | $59 |
| **Single board** | No, several modules | Yes | Yes | Yes |

- **Hat Labs SH-ESP32 + DomoticX MCP2518FD.** The N2K side worked (address claim, live backbone data, simulated engine data on the MFD) and the SPI link to the MCP2518FD passed a loopback test. The blocker: the MCP2518FD breakout has only the CAN controller, no transceiver, so a separate isolated transceiver module would have to be added on top of the SPI wiring. Stopped for that reason.
- **SK Pang Teensy 4.0.** Solderless and three native CAN controllers, but no Wi-Fi (no wireless update, no web page) and no isolation.
- **SK Pang ESP32-S3 CAN FD.** A clean single board with a proven library stack, but no isolation on either port, which risks ground loops on an installation with one common ground rail.
- **SmuqDev ESP32-C5 (selected).** Both ports are isolated (TI ISO1044BDR with a separate DC-DC converter per channel, plus common-mode chokes and TVS diodes), termination is a DIP switch per channel, and both ports are native TWAI controllers, so there is no SPI controller. The vendor is a small maker (Tindie, first batch). The price is the firmware work: the standard `NMEA2000_esp32` driver includes Xtensa-only headers and does not compile on the RISC-V C5, so the project has its own driver class (`tNMEA2000_esp32c5`) on the ESP-IDF TWAI API, and the J1939 receiver uses the TWAI v2 multi-controller API directly.

![The SmuqDev ESP32-C5 board with its main parts marked](../images/CANFD-board-annotated.jpg)

The full specification is in [`volvo-d130-esp32c5-dual-can-spec-plan.md`](esp32c5/volvo-d130-esp32c5-dual-can-spec-plan.md). The Hat Labs and Teensy designs are not part of this repository.
