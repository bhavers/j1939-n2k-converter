# Deutsch DT 6-Pin Harness — Build Specification

Quick links: [specification](esp32c5/volvo-d130-esp32c5-dual-can-spec-plan.md) · [implementation plan](esp32c5/implementation-plan.md) · [README](../README.md)

This document describes the harness that sits between the Volvo Penta MDI and the VDO tachometer and carries the CAN signals to the SmuqDev ESP32-C5 gateway board.

---

## 1. Purpose & Design

The MDI control box on the Volvo Penta D1-30B is connected to the VDO tachometer via a 6-pin Deutsch DT harness. The new harness is inserted in that connection. It passes all six lines straight through, and a six-wire extension cable is joined to the six pins with butt splices:

```
MDI box  ──  [end A: DT receptacle, female]
                          │
               ┌──────────┴──────────┐
               │                     │
    [end B: DT plug, male]    [extension cable, 6 wires]
    → to VDO tachometer        → to ESP32-C5 terminal CAN 1 (3 wires used)
```

All six signals reach the tachometer unmodified. Only GND, CAN_H and CAN_L of the extension cable are connected to the board (pins 4, 5 and 2, see section 4). The tachometer and MDI box see no change in electrical loading.

---

## 2. Connector Types

The Deutsch DT Series connectors used throughout this harness are **DT Series** (not DTM or DTP). All contacts are **size 16**.

| End | Shell | Contacts | Purpose |
| :--- | :--- | :--- | :--- |
| **A — Incoming (from MDI)** | DT06-6S (receptacle, female shell) | 6× size-16 socket contacts | Receives the male DT plug from the MDI cable |
| **B — Passthrough (to tachometer)** | DT04-6P (plug, male shell) | 6× size-16 pin contacts | Plugs into the female receptacle on the tachometer cable |
| **C — Extension cable (to ESP32-C5)** | No connector — bare wire ends | — | Screw terminals on the ESP32-C5 board (three of the six wires) |

> **Orientation**: the connector on the tachometer is a **DT receptacle** (female socket contacts, recessed), so the passthrough end needs a **DT plug** (male pin contacts) — end **B** above.

### Wedgelocks

Each shell requires its wedgelock to be inserted after the contacts are crimped in, locking the contacts in place:

| Shell | Wedgelock part |
| :--- | :--- |
| DT06-6S (end A) | W6S |
| DT04-6P (end B) | W6P |

---

## 3. Bill of Materials

| Qty | Part | Description |
| :--- | :--- | :--- |
| 1 | **DT06-6S** | 6-pin DT receptacle shell (end A, female) |
| 1 | **W6S** | Wedgelock for DT06-6S |
| 1 | **DT04-6P** | 6-pin DT plug shell (end B, male) |
| 1 | **W6P** | Wedgelock for DT04-6P |
| 6 | **0462-201-16141** (or equivalent) | Size-16 socket contacts for end A — match to wire gauge (16–18 AWG / 1–1.5 mm²) |
| 6 | **0460-202-16141** (or equivalent) | Size-16 pin contacts for end B — match to wire gauge |
| 6 | Blue butt splices | One per pin, joining the passthrough wire to the extension wire |
| — | Extension cable, 6 wires, about 0.5 m or as needed | One wire per pin. Use a twisted pair for CAN_H / CAN_L |
| — | Heat-shrink or marine-grade tape | Strain relief and weather sealing |

> Use **marine-rated tinned copper** wire throughout. 18 AWG (0.75 mm²) is sufficient for the CAN signal lines. Match the gauge of the existing Volvo harness for the passthrough wires.

---

## 4. Pin Assignment

> **Measured on the engine, not assumed.** The pin numbers first assumed for this harness (pin 1 = +12 V, pin 2 = GND, pin 3 = CAN_H, pin 4 = CAN_L) were wrong. The table below is what was measured, and then confirmed by receiving J1939 frames. The numbers are the ones on the Deutsch connector.

The extension cable (to the board) has six wires, one per pin, each joined to the matching pin of the passthrough with a blue butt splice. Only three of them go to the board.

| Pin | Original wire colour | Colour on the extension cable | Signal | Connected to the board |
| :--- | :--- | :--- | :--- | :--- |
| **1** | red | blue | not identified (joined to pin 2, see below) | no |
| **2** | yellow/white | brown | **CAN_L** | terminal CAN 1: **CAN_L** |
| **3** | black | red | not identified (joined to pin 5, see below) | no |
| **4** | black | green | **GND** (engine ground) | terminal CAN 1: **GND** (GND_ISO) |
| **5** | yellow/grey | black | **CAN_H** | terminal CAN 1: **CAN_H** |
| **6** | red (12 V) | yellow | **+12 V switched** (13.4 V against pin 4) | **no, never connect it** |

How this was found (wires not yet on the board):

- Yellow to green: 13.4 V. Every other wire: about 2.5 V against green. Yellow to the other four wires read 10.4 V.
  - **Explained.** The CAN lines idle at about 2.5 V against ground, so yellow to a CAN wire is the supply minus about 2.5 V: 13.4 V − 2.5 V ≈ 10.9 V. The remaining 0.5 V is most likely the supply voltage differing between the two readings (the settings page later showed 13.0 to 13.1 V). With 13.0 V the sum gives 10.5 V. This is arithmetic on the readings, not a new measurement, and it is not a fault.
- Brown and blue beep against each other, and black and red beep against each other. Between the two groups: 118 to 120 ohm, i.e. one 120 ohm termination. Two (about 60 ohm) were expected.
  - **Explained as far as it matters.** Two 120 ohm terminators in parallel give 60 ohm, so the reading shows that only one termination was in the measured circuit. Whether the cable was plugged into both the MDI and the tachometer was not recorded, so the most likely reason is that the second terminator was not connected during the measurement. That has not been re-measured, so the cause is not proven.
  - **Why it does not matter now.** The bus works: 17,474 frames in 20 s and 0 receive errors with both termination DIP switches on the board OFF. The gateway adds no termination of its own. Leave the DIP switches OFF unless the engine bus shows errors.
- A multimeter cannot tell CAN_H from CAN_L. Black on CAN_H and brown on CAN_L works: 17,474 frames in 20 s, 0 receive errors, and the page shows Engine Data J1939 (CAN1) as connected. The swapped order was not tried.

---

## 5. Wiring Diagram

```
 MDI plug ──► [male end] ═══ pins 1-6 straight through ═══ [female end] ──► tachometer
                              │ │ │ │ │ │   (a blue butt splice on every pin)
                              └─┴─┴─┴─┴─┴── extension cable, 6 wires ──► board

   pin 5  black   CAN_H   ──► terminal CAN 1: CAN_H
   pin 2  brown   CAN_L   ──► terminal CAN 1: CAN_L
   pin 4  green   GND     ──► terminal CAN 1: GND (GND_ISO)
   pin 1  blue    not identified  ──► not connected
   pin 3  red     not identified  ──► not connected
   pin 6  yellow  +12 V           ──► not connected, tape the end
```

---

## 6. Assembly Steps

1. **Cut wire lengths**: 6 passthrough wires (end A to end B, about 15 cm each) and a six-wire extension cable, length to suit the installation.
2. **Crimp the contacts**: size-16 socket contacts on the end A wires, size-16 pin contacts on the end B wires.
3. **Insert contacts into the shells**: push each contact into its pin position until it clicks. Verify with a gentle tug.
4. **Insert wedgelocks**: W6S into end A, W6P into end B. The wedgelock clicks flush when fully seated.
5. **Join the extension cable**: one blue butt splice per pin, joining each passthrough wire to the extension wire of the same pin (colours in section 4).
6. **Twist the CAN pair**: keep the CAN_H and CAN_L wires twisted (about 2 twists per cm) for the full run to the board.
7. **Connect at the board**: GND, CAN_H and CAN_L to the terminal CAN 1 screw terminals. Insulate the other three wires; the +12 V wire (pin 6) must never be connected.
8. **Strain relief**: heat-shrink at the splices, and cable ties so the connectors carry no load.

---

## 7. Verification Before Power-On

| Check | Expected |
| :--- | :--- |
| Continuity: every pin 1 to 6, one end to the other end | Connected |
| Continuity: pin 5 to black, pin 2 to brown, pin 4 to green (extension) | Connected |
| No short between black (CAN_H) and brown (CAN_L) | No continuity, or about 120 ohm when plugged into the harness |
| No short between yellow (+12 V) and any other wire | No continuity |
| No short between any pin and the shell | No continuity |

With ignition ON (engine not started):

| Measurement | Result measured |
| :--- | :--- |
| Yellow against green | 13.4 V |
| The four other wires against green | about 2.5 V |
| CAN_H against CAN_L | too small to tell apart with a multimeter; find the order by trying (section 4) |
