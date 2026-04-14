# RLC Development Progress

## Overview

ESP32-S3 Wireless Rocket Launch Controller — dual-unit system (Base + Remote)
communicating via encrypted ESP-NOW. Developed against **FSD v1.14**.

| Phase | Name | Status |
|-------|------|--------|
| 0 | Hardware Validation | COMPLETE |
| 1 | Foundation and Communication | COMPLETE |
| 2 | Input/Output and Debouncing | NOT STARTED |
| 3 | State Machines and Command Processing | NOT STARTED |
| 4 | Display | NOT STARTED |
| 5 | Hardening and Final Testing | NOT STARTED |

---

## Phase 0 — Hardware Validation

**Status:** COMPLETE

All hardware peripherals validated with standalone test firmware before Phase 1.

- Base unit hardware test: `rlc-hw-test-base/` — PASS
- Remote unit hardware test: `rlc-hw-test-remote/` — PASS
- Continuity circuit limitation documented (pin_config.h Phase 0 notes)

**Key commits:**
- `4ad14bb` Phase 0: Document continuity circuit limitation in pin_config.h

---

## Phase 1 — Foundation and Communication

**Status:** COMPLETE

Both units boot, establish link, exchange heartbeats, detect link loss, and
recover. RGB LED shows status. Boot self-tests verify firmware integrity.

### Code Review

Full code review performed against FSD v1.14 — results in `Phase1_Code_Review.md`.

6 must-fix items and 10 recommended fixes identified and resolved.

### Fixes Applied

| # | Fix | Files |
|---|-----|-------|
| 1 | CRC32-C (Castagnoli) software implementation + header in CRC input | `rlc_message.c/h` |
| 2 | ESP-NOW recv callback ISR→task API fix + 5-consecutive-send-failure tracking | `rlc_espnow.c/h` |
| 3 | RGB LED overlay mutex, task priority 5→1, 8-pixel base support | `rlc_rgb_led.c/h` |
| 4 | Link manager: retry count, seq overflow guard, app-state hook, send failure handling, TWDT | `rlc_link.c/h` |
| 5 | Battery warning threshold (`min_operate_mv`) | `rlc_battery.c/h` |
| 6 | Watchdog helper (`rlc_watchdog_add_task`) | `rlc_watchdog.c/h` |
| 7 | Boot self-tests (7 suites: struct offsets, CRC32-C, message serialisation, seq validation, debounce, version, integrity CRC) | `rlc_selftest.c/h` |
| 8 | Kconfig serial debug logging option | `Kconfig.projbuild` |
| 9 | Boot sequence updates (self-test calls, pixel count) | `rlc_base_main.c`, `rlc_remote_main.c` |

### Test Results — Phase 1

| Test | Result | Notes |
|------|--------|-------|
| Boot self-tests (both units) | PASS | 7/7 suites, 25+ checks |
| CRC32-C test vector `0xE3069283` | PASS | Correct Castagnoli table (256 entries) |
| Struct offset verification | PASS | 25 offsets verified |
| Base unit boot | PASS | Relays safe, siren, LED (8 pixels), battery ADC |
| Remote unit boot | PASS | LED (1 pixel), battery ADC, display init |
| ESP-NOW init + encrypted peer registration | PASS | PMK/LMK, real MACs |
| Link establishment (LINK_REQUEST/ACK) | PASS | Links within ~3 seconds |
| Session token agreement | PASS | Token matches on both units |
| Heartbeat stability (45s continuous) | PASS | 0 missed pings |
| RSSI tracking | PASS | -41 to -57 dBm range, 3-frame moving average |
| Battery readings | PASS | Base ~12V, Remote ~3.3V |
| Version mismatch rejection | PASS | Remote v1.0.1 → "FW MISMATCH", state 5, red triple-flash LED, stuck until reboot |
| Build verification (both targets) | PASS | Zero warnings, zero errors |

### Test Results — Pending Manual Verification

| Test | How | Expected |
|------|-----|----------|
| Link loss (separate units) | Move one unit to another room | Yellow fast blink within ~1.5s, "LINK LOST" log |
| Link recovery (reunite units) | Bring unit back in range | Auto-recovers, green LED, "link recovery" log |
| Ping failure flash | Briefly block signal (< 1.5s) | Orange LED flash (255,100,0) per missed ping |
| Re-link (reset one unit) | Reset one unit while linked | New LINK_REQUEST/ACK, new session token |

**Key commits:**
- `ed62aff` Phase 1: Foundation and Communication — link manager + ESP-NOW rx decoupling
- `40ab607` Phase 1 code review fixes — all 6 must-fix + 10 recommended items
- `6192948` Set real hardware MAC addresses in rlc_config.h
- `4c0f682` Add build_base.sh and build_remote.sh helper scripts

---

## Phase 2 — Input/Output and Debouncing

**Status:** NOT STARTED

**Scope:**
- Shift-register debounce engine
- Battery voltage ADC (base GPIO 1, remote GPIO 1)
- Base: 8 channel SPDT relays, arm switch sense, arm relay, siren
- Base: 8-channel continuity monitoring (ADC, 64-sample oversampling, 4-band classification)
- Remote: Rotary encoder driver (interrupt-driven, channel 1–8)
- Remote: Fire button driver (debounced, fresh-press)
- Remote: Arm switch monitoring (debounced)

**Test criteria:** Base correctly classifies continuity on all 8 channels.
Arm switch state and battery voltage read correctly. Remote encoder selects
channels, buttons debounce correctly. Status updates arrive with correct
continuity bands.

---

## Phase 3 — State Machines and Command Processing

**Status:** NOT STARTED

**Scope:**
- Base state machine (BOOT → IDLE → ARMED → PRE_FIRE → FIRING → POST_FIRE)
- Remote state machine (BOOT → LINKING → IDLE → ARMED → FIRING)
- Command handler (ARM, DISARM, FIRE, CEASE_FIRE) with guard conditions
- ACK/NACK responses with reason codes
- Siren control (pulsing ARMED, continuous PRE_FIRE/FIRING)
- Fire pulse via hardware timer
- Command retry logic and timeout handling
- Dead-man switch logic
- All safety interlocks

**Test criteria:** Complete fire sequence works end-to-end. All disarm triggers
work. NACK reasons displayed correctly. Channel change while armed triggers
disarm.

---

## Phase 4 — Display

**Status:** NOT STARTED

**Scope:**
- ILI9488 SPI display driver (480×320, landscape)
- Screen layout manager (Splash, Main Status, Armed, Firing, Link Lost, Error)
- Partial refresh (dirty-rectangle) for dynamic elements
- RSSI bar, battery bars, continuity grid, channel selector
- NACK reason display, version mismatch screen

**Test criteria:** All screens render. Updates at ≥ 5 Hz. State transitions
trigger full redraws.

---

## Phase 5 — Hardening and Final Testing

**Status:** NOT STARTED

**Scope:**
- Complete test suite (FSD §15)
- Watchdog stress testing
- Range testing (10 m, 50 m, 100 m, 200 m)
- Power consumption measurement
- Edge case testing (rapid toggling, button mashing, power cycling)
- Documentation: build instructions, flash procedure, wiring diagram

**Test criteria:** All §15 tests pass. System operates reliably at 100 m LOS.

---

## Hardware Reference

| Item | Value |
|------|-------|
| Base MAC | `94:A9:90:31:18:38` |
| Remote MAC | `44:1B:F6:81:F1:70` |
| Base serial | `/dev/ttyACM0` (verify with `udevadm` each session) |
| Remote serial | `/dev/ttyACM1` (verify with `udevadm` each session) |
| ESP-IDF version | v5.4.1 |
| Target | ESP32-S3 (xtensa) |
| Flash size | 16 MB |
| PSRAM | 8 MB OCT |

## Build Commands

```
./build_base.sh flash          # Build + flash base to /dev/ttyACM0
./build_remote.sh flash        # Build + flash remote to /dev/ttyACM1
./build_base.sh flash -p PORT  # Custom port
```
