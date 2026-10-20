# Working notes for AI assistants

Read this first when starting a new session on this repository.

## Project

J1939 → NMEA 2000 gateway for a Volvo Penta D1-30B on an ESP32-C5 board. See [`README.md`](README.md) for what it does and [`design/esp32c5/implementation-plan.md`](design/esp32c5/implementation-plan.md) for the status and open items.

- Stack: ESP32-C5, PlatformIO + ESP-IDF. Firmware in `firmware-esp32c5/src/`.
- CAN 1 = J1939 (engine, listen-only). CAN 2 = NMEA 2000.
- The gateway makes its own Wi-Fi access point at `192.168.4.1`. The password is in the git-ignored `wifi_secrets.h`. Never commit or print it.

## Commands

- Build: `pio run -d firmware-esp32c5`
- Host tests: `firmware-esp32c5/host_test/run.sh` (each test prints `N checks, 0 failed`)
- Flash over Wi-Fi: see "Build and flash" in the README.

## Rules

- Only claim what was measured. Mark the rest as unconfirmed.
- Answer the owner in Dutch. Keep the documentation in English.
- No personal names or boat name in the documentation.
- Security rules apply: no secrets in git.
