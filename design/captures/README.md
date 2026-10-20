# J1939 captures

Raw logs of the engine bus, kept for later analysis of the PGNs the gateway does not decode.

## How to record

1. In [`firmware-esp32c5/src/config.h`](../../firmware-esp32c5/src/config.h) set `#define J1939_RAW_LOG 1`, build, and flash (over Wi-Fi: see [Build and flash](../../README.md#build-and-flash) in the README).
2. Join the gateway's Wi-Fi network and record:

   ```bash
   nc 192.168.4.1 23 | tee j1939-log.txt
   ```

3. Set `J1939_RAW_LOG` back to 0 and flash again when done. With the flag at 0 (the default) only the overview line every 5 s is printed.

Each line looks like `[J1939-RAW] t=<ms since boot> SA <source address> PGN <pgn>: <8 bytes in hex>`. A line is written when the payload of a PGN that is not decoded changes, at most once per 100 ms per PGN and source address and 40 lines per second in total. The `[J1939] frames=… rawSkipped=…` line shows how many changes were dropped by that limit.

## Recordings

### `j1939-raw-ignition-on-engine-off.txt`

- **Conditions:** ignition on, engine not running (confirmed by the owner). About 60 s on a D1-30B, 117,956 frames, 0 receive errors, `rawSkipped=0`.
- **Only changing PGNs appear.** PGNs whose payload never changed during the minute (65352, 65373, 65283, 65256, 65276, 65376) are not in the file as lines after the first, because only changes are logged. Their last payloads are in the `[J1939] PGN …` overview lines at the end of the file.

What the recording shows (observations, not confirmed meanings):

| PGN | Source address | Observation |
| :--- | :---: | :--- |
| 65417 | 242 | Byte 2 counts up (`65 66 67 …`) about every 102 ms. Byte 3 is always `41`. |
| 65417 | 0 | Byte 1 takes three values (`A0`, `08`, `05`); one message per second per value. Byte 2 changes in steps of 8. |
| 65420 | 0 | `B0 xx 01 00 01 44 55 66` about every 410 ms. Byte 2 counts up; the rest is constant. |
| 65352, 65373, 65283, 65256, 65276 | 0 | Constant, mostly `FF` (not available). 65276 is all `FF`. |
| 65376 | 0 | Constant `FF FF 00 80 2C 01 04 7D`. |

## To do: more recordings (next season)

With the engine running, record again and compare with this file. Write down the time (in seconds since the gateway booted, or by the clock on the computer) at which each state starts, and what the tachometer and dashboard show:

- [ ] Engine idling, at least 1 minute.
- [ ] Engine at about 1500, 2000 and 2500 rpm, 1 minute each.
- [ ] Engine stopped again, and ignition off.
- [ ] Note the fuel gauge reading; PGN 65276 may be the dashboard display message (fuel level), but it was all `FF` with the engine stopped.
- [ ] Check which bytes of PGN 65417, 65420 and 65376 change with engine speed, temperature or load.
- [ ] Look for PGN 65226 (DM1) and 65263 (oil pressure) on the bus while the engine runs.
- [ ] Find a source for the Volvo Penta proprietary PGNs (with a URL) and compare. Unverified lead, no URL: PGN 65417 and 65373 may be named as Volvo Penta proprietary in Yacht Devices documentation, and 65352 may be listed in a 2008 ComAp manual.
