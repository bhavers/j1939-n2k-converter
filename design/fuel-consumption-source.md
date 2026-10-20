# Source of the default fuel consumption table

Quick links: [specification](esp32c5/volvo-d130-esp32c5-dual-can-spec-plan.md) · [implementation plan](esp32c5/implementation-plan.md) · [README](../README.md)

The Volvo Penta D1-30 does not report fuel use on J1939, so the gateway estimates it from engine speed with an editable RPM → litres/hour table (see [specification, section 4.6](esp32c5/volvo-d130-esp32c5-dual-can-spec-plan.md#46-fuel-estimate-and-trip)). This file records where the default values come from.

## Source

- **Page**: [Average fuel consumption — myHanse, Hanse Yachts Owners Forum](https://www.myhanse.com/average-fuel-consumption_topic11698.html) (Hints & Tips, Hanse 385/388)
- **Post**: by a forum member, 5 January 2020, 05:19
- **Retrieved**: 13 October 2026 (page read and the values below compared with the firmware)
- **Nature of the data**: the poster describes the figures as the "official" Volvo data for the D1-30 ("actual l/h"). The thread gives no further reference (no Volvo document or test conditions), so the origin of the numbers is not verified.

## Values used (`actual l/h`)

| RPM | L/h |
| ---: | ---: |
| 1400 | 1.0 |
| 1600 | 0.9 |
| 1800 | 1.5 |
| 2000 | 2.0 |
| 2200 | 2.5 |
| 2400 | 2.8 |
| 2800 | 4.5 |
| 3200 | 6.6 |

The same post also lists a calculated column from a formula (L/h = max hp × (rpm ÷ max rpm)³ × 2.2 ÷ 10). That column is **not** used; the gateway uses the "actual" column only.

The poster notes that the D1-30 has a minimum in its consumption curve at 1600 rpm, a roughly linear increase up to 2500 rpm, and a steeper increase above that.

## Other figures in the same thread (not used)

| Poster | Engine | Figure |
| :--- | :--- | :--- |
| Another owner (11 Dec 2019) | Hanse 385 | 1.7–1.9 L/h, usually at 2000 rpm |
| Another owner (4 Jan 2020) | Hanse 385 | about 1.8 L/h at 1500 rpm, up to 6 L/h at 2500 rpm (max 3000 rpm) |

These are lower than the table around 2000–2200 rpm, so expect the default table to read slightly high around cruise speed.

## Where it is used

- Default table: `defaultCurve()` in [`fuel_model.h`](../firmware-esp32c5/src/fuel_model.h)
- Description: [README](../README.md#fuel-estimate) and [specification, section 4.6](esp32c5/volvo-d130-esp32c5-dual-can-spec-plan.md#46-fuel-estimate-and-trip)

## Calibration

The figures come from another owner's installation. Calibrate them on your own: fill the tank, run a fixed rpm for a known time, refill, compare, and edit the table on the gateway's settings page.
