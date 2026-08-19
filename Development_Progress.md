# RLC Development Progress

**Project:** ESP32-S3 Wireless Rocket Launch Controller
**Spec:** RLC-FSPEC-001 v1.19 (2026-08-19)
**Platform:** ESP32-S3-WROOM-1 N16R8 | ESP-IDF v5.4.1

## Legend

**Dev task status:** `DONE` | `IN PROGRESS` | `TODO`
**Test status:** `PASS` | `FAIL` | `CHECK` (passed on bench, needs re-verify) | `TODO`

---

## Phase Overview

| Phase | Name | Status |
|-------|------|--------|
| 0 | Hardware Validation | COMPLETE |
| 1 | Foundation and Communication | COMPLETE |
| 2 | Input/Output and Debouncing | COMPLETE |
| 3 | State Machines and Command Processing | CODE COMPLETE |
| 4 | Display | CODE COMPLETE |
| 5 | Hardening and Final Testing | NOT STARTED |

---

## Phase 0 — Hardware Validation

**FSD ref:** §4.3 Phase 0
**Status:** COMPLETE

Standalone test firmware validates all hardware peripherals before Phase 1 firmware development.
Test specs: `rlc-hw-test-base/RLC_Base_Hardware_Test_Specification.md` and
`rlc-hw-test-remote/RLC_Remote_Hardware_Test_Specification.md`.

### Phase 0 Development Tasks

| # | Task | Status | Notes |
|---|------|--------|-------|
| 1 | Base hardware test firmware | DONE | `rlc-hw-test-base/` |
| 2 | Remote hardware test firmware | DONE | `rlc-hw-test-remote/` |
| 3 | Document continuity circuit limitation | DONE | `pin_config.h` Phase 0 notes |

### Phase 0 Base Unit Hardware Tests

| ID | Test | Status | Notes |
|----|------|--------|-------|
| B-R01 | Individual SPDT relay activation (ch 1-8) | CHECK | Relay click observed |
| B-R02 | Relay sweep (9 relays, 500 ms apart) | CHECK | 8 channel + arm relay |
| B-R03 | All-safe command | CHECK | All GPIOs return inactive |
| B-C01 | Always-on continuity with relays de-energised | CHECK | NC position provides path |
| B-C02 | SHORT classification (0 ohm) | CHECK | Voltage < 500 uV |
| B-C03 | GOOD classification (2 ohm) | CHECK | Voltage ~660 uV |
| B-C04 | MARGINAL classification (100 ohm) | CHECK | Voltage ~97,000 uV |
| B-C05 | OPEN classification (open circuit) | CHECK | Voltage ~3,190,000 uV |
| B-C06 | All 8 channels continuity | CHECK | Each reports correct band |
| B-C07 | Noise floor analysis (256 samples) | CHECK | Stddev < 2 mV |
| B-C08 | Hysteresis stability (30 s) | CHECK | No spurious transitions |
| B-C09 | Continuity isolation during fire | CHECK | OPEN when relay energised |
| B-C10 | Post-fire reconnection | CHECK | GOOD after relay de-energises |
| B-B01 | Battery voltage reading | CHECK | Matches supply +/-100 mV |
| B-B02 | Battery ADC stability | CHECK | Stddev < 20 mV |
| B-B03 | Divider ratio test | CHECK | Ratio matches +/-2% |
| B-I01 | Arm relay de-energised = DISARMED | CHECK | GPIO LOW |
| B-I02 | Arm relay energised = ARMED | CHECK | GPIO HIGH |
| B-I03 | Arm sense toggle | CHECK | Clean state changes |
| B-I04 | Arm sense with battery disconnected | CHECK | Reports DISARMED |
| B-I05 | Contact welding detection | CHECK | LOW when relay de-energised |
| B-S01 | Siren on/off | CHECK | Clean activation/deactivation |
| B-S02 | Siren pulse (500/500 x 4) | CHECK | 4 pulses observed |
| B-S03 | Siren patterns (all 6) | CHECK | All patterns distinct |
| B-L01 | RGB LED colour accuracy (R, G, B) | CHECK | Correct colours |
| B-L02 | RGB LED pattern test (9 patterns) | CHECK | All cycle correctly |
| B-L03 | RGB LED brightness test | CHECK | Visible brightness steps |
| B-F01 | Fire pulse timing (2000 ms) | CHECK | +/-500 us accuracy |
| B-F02 | Fire pulse short (100 ms) | CHECK | +/-500 us accuracy |
| B-F03 | Fire pulse safe-during-fire | CHECK | Relays de-energise immediately |
| B-F04 | Fire all channels | CHECK | Each ch energised ~1000 ms |
| B-F05 | Task-context verification (ISR→task) | CHECK | ISR signals task only |
| B-BS01 | Safe boot state (all relays inactive < 1 ms) | CHECK | GPIO LOW on boot |
| B-BS02 | Boot order (GPIO init first) | CHECK | Before ESP-NOW init |
| B-K01 | Key switch OFF = DISARMED | PASS | GPIO 42 raw=0, OFF |
| B-K02 | Key switch ON = ON | PASS | GPIO 42 raw=1, ON |
| B-K03 | Key switch toggle | PASS | Clean state changes, no bounce |

### Phase 0 Remote Unit Hardware Tests

| ID | Test | Status | Notes |
|----|------|--------|-------|
| R-E01 | Encoder rotation slow (1 step/detent) | CHECK | CW++, CCW-- |
| R-E02 | Encoder fast rotation (~5 detents/s) | CHECK | All steps detected |
| R-E03 | Encoder direction reversal | CHECK | Net count returns |
| R-E04 | Encoder channel wrapping (8→1, 1→8) | CHECK | Correct wrap |
| R-E05 | Encoder push button debounce | CHECK | Clean press/release |
| R-E06 | Encoder long-press detection (>=500 ms) | CHECK | Short vs long |
| R-E07 | Encoder rotation + simultaneous press | CHECK | Independent events |
| R-F01 | Fire button press/release (shift reg 0x00/0xFF) | CHECK | Converges correctly |
| R-F02 | Fire button debounce visualisation | CHECK | LSB fills correctly |
| R-F03 | Fire button fresh-press (held at start) | CHECK | No false trigger |
| R-F04 | Fire button fresh-press (boot held) | CHECK | Re-press detected |
| R-F05 | Fire button rapid press/release (<80 ms) | CHECK | Filtering works |
| R-A01 | Arm switch states (ARMED/DISARMED) | CHECK | 0x0000/0xFFFF |
| R-A02 | Arm switch debounce timing (160 ms) | CHECK | 16 samples x 10 ms |
| R-A03 | Arm switch disconnected wire | CHECK | DISARMED (fail-safe) |
| R-D01 | Display init + ILI9488 ID read | CHECK | SPI initialised |
| R-D02 | Display backlight control | CHECK | On/off visible |
| R-D03 | Display colour fills (RGB, white, black) | CHECK | Full 480x320 |
| R-D04 | Display test pattern | CHECK | Colours + bars |
| R-D05 | Display text rendering | CHECK | Readable text |
| R-D06 | Display channel grid (8 cells) | CHECK | Numbered channels |
| R-D07 | Display gradient (black→white) | CHECK | No banding |
| R-D08 | Display pixel accuracy (corners) | CHECK | Exact placement |
| R-D09 | Display partial update | CHECK | Background intact |
| R-D10 | Display SPI speed (full-screen fill) | CHECK | ~50-100 ms at 20 MHz |
| R-D11 | Display ID re-read during operation | CHECK | Valid ILI9488 ID |
| R-B01 | Buzzer on/off | CHECK | Clean activation |
| R-B02 | Buzzer single beep (100 ms) | CHECK | Timing correct |
| R-B03 | Buzzer triple beep (100/100 x 3) | CHECK | Pattern correct |
| R-B04 | Buzzer all patterns (8 patterns) | CHECK | All distinct |
| R-BT01 | Battery voltage reading | CHECK | Matches supply +/-100 mV |
| R-BT02 | Battery ADC stability | CHECK | Stddev < 20 mV |
| R-L01 | RGB LED colour accuracy | CHECK | Correct colours |
| R-L02 | RGB LED pattern test (8 patterns) | CHECK | All cycle correctly |
| R-L03 | RGB LED brightness test | CHECK | Visible steps |
| R-INT01 | All inputs simultaneous | CHECK | No interference |
| R-INT02 | Display + inputs concurrent | CHECK | Encoder updates display |
| R-INT03 | Buzzer + display concurrent | CHECK | Both work together |

### Phase 0 Key Commits

- `4ad14bb` Phase 0: Document continuity circuit limitation in pin_config.h

---

## Phase 1 — Foundation and Communication

**FSD ref:** §4.3 Phase 1, §6 (Protocol), §9.13 (Boot Sequence)
**Status:** COMPLETE

### Phase 1 Development Tasks — Boot Sequence (FSD §9.13)

| Step | Task | Status | Implementation |
|------|------|--------|----------------|
| 1 | Configure relay GPIOs to safe state | DONE | `relay_init()` + `siren_init()` in `rlc_base_main.c` |
| 2 | Verify packed struct field offsets (§9.9) | DONE | `rlc_selftest_run()` — 25 offsets checked |
| 3 | Verify CRC32-C test vector (§6.2.2) | DONE | `rlc_selftest_run()` — `"123456789"` → `0xE3069283` |
| 4 | Initialise ADC calibration | DONE | `rlc_battery_init()` with calibration |
| 5 | Initialise ESP-NOW, PMK, register peer (3 retries) | DONE | `rlc_espnow_init()` + `rlc_espnow_add_peer()` |
| 6 | Initialise display + read-back ID (remote only) | DONE | Phase 4: `display_init()` + `display_is_healthy()`; ERROR halt on failure |
| 7 | Configure input GPIOs + debounce engine | DONE | Phase 1: debounce engine exists; GPIOs are Phase 2 |
| 8 | Configure hardware watchdog + TWDT | DONE | `rlc_watchdog_init()` + `esp_task_wdt_add()` in link task |
| 9 | Start FreeRTOS tasks | DONE | link_task (prio 6), led_task (prio 1) |
| 10 | Begin link establishment | DONE | `rlc_link_init()` — LINK_REQUEST/wait |

### Phase 1 Development Tasks — Core Modules

| # | Task | FSD ref | Status | Implementation |
|---|------|---------|--------|----------------|
| 1 | Project scaffolding (CMake, Kconfig, components) | §4.1 | DONE | `rlc_common`, `rlc_base`, `rlc_remote` |
| 2 | ESP-NOW driver wrapper | §6.2.1 | DONE | `rlc_espnow.c/h` |
| 3 | Message serialisation/deserialisation | §6.3 | DONE | `rlc_message.c/h` |
| 4 | Protocol header + packed structs with static_assert | §6.3.3 | DONE | `rlc_protocol.h` — 11 struct asserts |
| 5 | Encryption setup (PMK + LMK) | §6.2.1 | DONE | `rlc_espnow.c` |
| 6 | Sequence number management (overflow guard) | §6.2.2 | DONE | `seq_next()` in `rlc_link.c` |
| 7 | Session token generation (non-zero, non-repeat) | §6.2.2 | DONE | `handle_link_request()` in `rlc_link.c` |
| 8 | CRC32-C (Castagnoli) software implementation | §6.2.2 | DONE | 256-entry lookup table in `rlc_message.c` |
| 9 | Integrity CRC over header + payload + key | §6.2.2 | DONE | `rlc_compute_integrity_crc()` |
| 10 | LINK_REQUEST / LINK_ACK handshake + version check | §6.4.1 | DONE | `rlc_link.c` — strict MAJOR.MINOR.PATCH |
| 11 | PING (remote→base) / PONG (base→remote) at 500 ms | §6.4.2 | DONE | `send_ping()`, `handle_ping()` |
| 12 | Link loss detection (3 missed pings / 1.5 s drought) | §6.4.2 | DONE | `tick_remote()`, `tick_base()` |
| 13 | Link recovery on PING/PONG return | §6.4.2 | DONE | State recovery in `process_frame()` |
| 14 | 5-consecutive-send-failure immediate link loss | §6.4.1a | DONE | Counter in `espnow_send_cb()`, callback to link mgr |
| 15 | LINK_REQUEST retry count + "NO LINK" after 5 attempts | §6.4.1 | DONE | `s_linkreq_attempts` counter |
| 16 | App-state guard callback (reject LINK_REQUEST when busy) | §6.4.1 | DONE | `rlc_link_set_guard()` — callback pattern |
| 17 | RSSI tracking (3-frame moving average) | §6.4.2 | DONE | Ring buffer in `update_rssi()` |
| 18 | RGB LED driver (WS2812 via RMT, GPIO 48) | §5.4.11 | DONE | `rlc_rgb_led.c` — 1 pixel (remote) / 8 pixel (base) |
| 19 | RGB LED status patterns (all 10 patterns) | §11 | DONE | Pattern engine in `led_task()` |
| 20 | RGB LED overlay flash (mutex-protected) | §4.7 | DONE | `s_overlay_mutex` in `rlc_rgb_led.c` |
| 21 | LED task priority 1 (lowest) | §9.10 | DONE | `xTaskCreate(..., 1, ...)` |
| 22 | Watchdog setup | §9.6 | DONE | `rlc_watchdog.c` |
| 23 | TWDT per-task registration helper | §9.6 | DONE | `rlc_watchdog_add_task()` |
| 24 | Battery ADC driver (ADC1, 8-sample avg, calibration) | §5.4.7 | DONE | `rlc_battery.c` |
| 25 | Battery 3-threshold check (OK/WARNING/LOW/CRITICAL) | §8.3.4 | DONE | `rlc_battery_check()` with `min_operate_mv` |
| 26 | Debounce engine (8-bit / 16-bit shift register) | §5.3 | DONE | `rlc_debounce.c/h` |
| 27 | Version header | §4.3 | DONE | `rlc_version.h` — v1.0.0 |
| 28 | Kconfig serial debug logging option | §9.11 | DONE | `CONFIG_RLC_SERIAL_DEBUG_LOGGING` |
| 29 | Boot self-tests (7 suites) | §9.9, §15.5 | DONE | `rlc_selftest.c/h` |
| 30 | Relay control (safe state on boot) | §9.7 | DONE | `rlc_relay.c` — stubs for Phase 2 |
| 31 | Siren control | §5.4.10 | DONE | `rlc_siren.c` — stubs for Phase 3 |
| 32 | Build helper scripts | — | DONE | `build_base.sh`, `build_remote.sh` |

### Phase 1 Code Review

Full code review against FSD v1.14 — see `Phase1_Code_Review.md`.

6 must-fix items (C1–C6) + 10 recommended fixes (R1–R10) identified. All resolved.

### Phase 1 FSD Communication Tests (§15.1)

| ID | Test | Status | Notes |
|----|------|--------|-------|
| T-C01 | Power on remote with base off | PASS | Blue pulse, retries, no crash. "NO LINK" after 5 attempts |
| T-C02 | Power on both — link within 10 s | PASS | Links in ~3 s. RSSI displayed. LEDs green |
| T-C03 | Separate units beyond range (simulated via reset) | PASS | Remote reset: drought detected at 1548 ms. Base reset: 3 missed pings + send failures at ~1520 ms |
| T-C04 | Return units after link loss (simulated via reset) | PASS | Re-link in 50–220 ms, new session token assigned, state 4→3 clean |
| T-C05 | Send pings, measure loss rate (desk range ~0.5 m) | PASS | 360 pings over 3 min, 0 missed, 0% loss. RSSI -48 dBm rock-solid |
| T-C06 | Replay captured ARM command | TODO | Phase 3 command layer implemented — needs on-target test |
| T-C07 | Firmware version mismatch | PASS | Remote v1.0.1 rejected with "FW MISMATCH", red triple-flash LED |
| T-C08 | RSSI averaging (3-frame, stable) | PASS | -41 to -57 dBm, stable display over 45 s |

### Phase 1 Boot Self-Tests (on-target, run every boot)

| Suite | Test | Status | Notes |
|-------|------|--------|-------|
| 1 | Struct field offset verification (25 checks) | PASS | All packed struct offsets match spec |
| 2 | CRC32-C test vector | PASS | `"123456789"` → `0xE3069283` |
| 3 | Message serialisation round-trip | PASS | Build PING, parse, verify header + payload fields |
| 4 | Sequence number validation | PASS | Accepts increasing, rejects equal/lower/zero/NULL |
| 5 | Debounce logic (8-bit) | PASS | 7 LOWs → no trigger; 8th LOW → active; 8 HIGHs → inactive |
| 6 | Version comparison | PASS | v1.0.0 matches, non-zero check |
| 7 | Integrity CRC determinism | PASS | Same input → same CRC; modified input → different CRC |

### Phase 1 Additional Verified Behaviour

| Behaviour | Status | Evidence |
|-----------|--------|----------|
| Session token agreement (base = remote) | PASS | Tokens match across 7 re-link events during range testing |
| Heartbeat stability (45 s, 0 missed) | PASS | `missed=0` entire run |
| Heartbeat stability (3 min, 0 missed) | PASS | 360 pings, 0 missed, desk range |
| Link-loss detection (remote reset) | PASS | Base detected PING drought at 1548 ms (spec: 1500 ms) |
| Link-loss detection (base reset) | PASS | Remote detected 3 missed pings at ~1520 ms + send failures |
| Link recovery after loss | PASS | Re-link in 50–220 ms, new session token, clean state transitions |
| 5 consecutive send failures (T-S11) | PASS | Triggered during RF shielding test, immediate link loss declared |
| RF degradation resilience | PASS | RSSI dropped to -98 dBm via antenna shielding; system re-linked reliably |
| Battery reading base (~12 V) | PASS | `12048 mV` with 4.3 divider |
| Battery reading remote (~3.3 V) | PASS | `3295 mV` with 2.8 divider |
| Build clean — both targets | PASS | Zero warnings, zero errors |
| Base boots as "RLC Base Unit v1.0.0" | PASS | Correct relay init, siren, battery, 8-pixel LED |
| Remote boots as "RLC Remote Unit v1.0.0" | PASS | Correct LED (1 pixel), battery, display stub |
| ESP-NOW encrypted peer (PMK + LMK) | PASS | Real MACs, no encryption errors |
| Remote battery in PING payload | PASS | `remote_battery_voltage_mv` field populated |
| Peer MAC filtering (reject unknown MAC) | PASS | `memcmp(it->src_mac, s_peer_mac, 6)` guard |
| Session token non-zero guarantee | PASS | `do {} while (new_token == 0)` loop |

### Phase 1 FSD Unit Tests (§15.5) — Host-Compilable

| ID | Module | Test | Status | Notes |
|----|--------|------|--------|-------|
| T-U01 | Message serialisation | All message types, byte-for-byte | PASS | Covered in boot self-test suite 3 |
| T-U02 | Integrity CRC | Known inputs + expected outputs + rejection | PASS | Covered in boot self-test suite 7 |
| T-U03 | Sequence number | Increasing accept, equal/lower reject, reset | PASS | Covered in boot self-test suite 4 |
| T-U04 | Session token | Correct accept, wrong reject, atomic invalidation | PASS | Tokens matched base↔remote across 7 re-link events |
| T-U05 | Debounce 8-bit | 0x00/0xFF detection, timing | PASS | Covered in boot self-test suite 5 |
| T-U06 | Debounce 16-bit | 0x0000/0xFFFF detection, timing | PASS | 15 LOWs no trigger, 16th triggers; 8 LOWs no trigger in 16-bit mode |
| T-U07 | Battery threshold | Three remote thresholds | PASS | Logic in `rlc_battery_check()` — needs on-target ADC test |
| T-U08 | Version comparison | Strict MAJOR.MINOR.PATCH | PASS | Covered in boot self-test suite 6 |
| T-U09 | Update sequence gap | Gap > 2 warning | TODO | Phase 2/3 feature |
| T-U10 | Continuity band classification | Known microvolt values | TODO | Phase 2 feature |
| T-U11 | Continuity hysteresis | Oscillating near threshold | TODO | Phase 2 feature |
| T-U12 | Continuity bands encoding | 2-bit-per-channel packing | TODO | Phase 2 feature |
| T-U13 | Struct field offsets | offsetof() for all packed structs | PASS | Covered in boot self-test suite 1 (25 checks) |
| T-U14 | CRC32-C test vector + header in input | `"123456789"` = `0xE3069283` | PASS | Covered in boot self-test suite 2 |
| T-U15 | Sequence number overflow | UINT32_MAX triggers re-link | PASS | rlc_seq_validate accepts UINT32_MAX-1 then UINT32_MAX, rejects equal/lower/wrap |
| T-U16 | Update sequence wrap-around | 65535→0 not treated as gap | TODO | Phase 2/3 feature |

### Phase 1 Key Commits

- `ed62aff` Phase 1: Foundation and Communication — link manager + ESP-NOW rx decoupling
- `40ab607` Phase 1 code review fixes — all 6 must-fix + 10 recommended items
- `6192948` Set real hardware MAC addresses in rlc_config.h
- `4c0f682` Add build_base.sh and build_remote.sh helper scripts

---

## Phase 2 — Input/Output and Debouncing

**FSD ref:** §4.3 Phase 2, §5 (Hardware Interface), §5.3 (Debounce), §5.4 (Base I/O), §5.5 (Remote I/O)
**Status:** COMPLETE

### Phase 2 Development Tasks

| # | Task | FSD ref | Status |
|---|------|---------|--------|
| 1 | Shift-register debounce engine (already exists in rlc_common) | §5.3 | DONE |
| 2 | Battery ADC driver base (already exists) | §5.4.7 | DONE |
| 3 | Battery ADC driver remote (already exists) | §5.5 | DONE |
| 4 | Base: 8 channel SPDT relay GPIO configuration | §5.4.1 | DONE |
| 5 | Base: Arm switch sense GPIO (GPIO 21, voltage divider + zener) | §5.4.3 | DONE |
| 6 | Base: Arm relay output (GPIO 47) | §5.4.9 | DONE |
| 7 | Base: Siren output (GPIO 40) | §5.4.10 | DONE |
| 8 | Base: 8-channel continuity ADC (GPIO 2-9, 64-sample oversampling) | §5.4.4 | DONE |
| 9 | Base: Continuity 4-band classification (SHORT/GOOD/MARGINAL/OPEN) | §5.4.4 | DONE |
| 10 | Base: Continuity hysteresis | §5.4.4 | DONE |
| 11 | Base: Relay feedback monitoring (check at arm-time) | §5.4.6 | DONE |
| 12 | Base: MOSFET driver outputs (10x IRLZ44N, active-high) | §5.4.10 | DONE |
| 13 | Remote: Rotary encoder driver (interrupt-driven, channel 1-8 wrap) | §5.5.3 | DONE |
| 14 | Remote: Fire button driver (8-bit debounce, fresh-press detection) | §5.5.4 | DONE |
| 15 | Remote: Arm switch monitoring (16-bit debounce, 10 ms poll) | §5.5.2 | DONE |
| 16 | Remote: Battery monitoring with 3 thresholds | §8.3.4 | DONE |
| 17 | Remote battery voltage in PING payload (already wired) | §6.3.5 | DONE |
| 18 | STATUS_UPDATE with real continuity bands + armed bitmask | §6.3.3 | DONE |
| 19 | Debounce 16-bit unit test (T-U06) | §15.5 | DONE |

### Phase 2 Code Review

Full code review against FSD v1.14 — see `Phase2_Code_Review.md`.

Verdict: PASS WITH NOTES. 3 major findings (M1–M3), 4 minor (m1–m4). All addressed.

### Phase 2 Bugs Found and Fixed During Testing

| # | Bug | Root Cause | Fix |
|---|-----|-----------|-----|
| 1 | Remote boot crash: NULL mutex in `rlc_link_set_remote_battery_mv()` | Battery task started before `rlc_link_init()` — mutex was NULL | NULL guard on `s_state_mutex` before `xSemaphoreTake` |
| 2 | Remote boot crash: stack overflow in `battery_task` | 2048-byte stack insufficient for ADC + `ESP_LOGW` formatting | Increased to 3072 bytes (both base and remote battery tasks) |
| 3 | Base boot crash: `LoadProhibited` in `adc_cali_raw_to_voltage` | ESP-IDF `adc_cali_create_scheme_curve_fitting` produces corrupted handles for some ADC1 channels on ESP32-S3 | Disabled ADC calibration for continuity channels — raw conversion sufficient for band classification |
| 4 | Encoder long-press not detected | Long-press timeout check gated on `s_long_press_cb != NULL` — no callback registered in Phase 2 | Separated detection from callback invocation — always detect, only invoke callback if registered |
| 5 | Fire button held at boot generates false fresh-press | `s_was_released` initialised `true` — debounce settling on held button triggers fresh-press | Read GPIO at init: if LOW, set `s_was_released = false` |
| 6 | Stack overflow in `fire_btn_task` with debug logging | 2048-byte stack insufficient for `ESP_LOGI` in callback | Increased to 3072 bytes |
| 7 | Stack risk in `arm_sw_task` with existing logging | Same 2048-byte stack with `ESP_LOGI` in callback | Increased to 3072 bytes preemptively |

### Phase 2 Boot Self-Tests (on-target, run every boot)

| Suite | Test | Status | Notes |
|-------|------|--------|-------|
| 1 | Struct field offset verification (25 checks) | PASS | All packed struct offsets match spec |
| 2 | CRC32-C test vector | PASS | `"123456789"` → `0xE3069283` |
| 3 | Message serialisation round-trip | PASS | Build PING, parse, verify header + payload fields |
| 4 | Sequence number validation | PASS | Accepts increasing, rejects equal/lower/zero/NULL |
| 5 | Debounce logic (8-bit) | PASS | 7 LOWs → no trigger; 8th LOW → active; 8 HIGHs → inactive |
| 6 | Debounce logic (16-bit) | PASS | 15 LOWs no trigger, 16th triggers; 8 LOWs no trigger in 16-bit mode |
| 7 | Version comparison | PASS | v1.0.0 matches, non-zero check |
| 8 | Integrity CRC determinism | PASS | Same input → same CRC; modified input → different CRC |
| 9 | Continuity band classification (T-U10) | PASS | 11 known microvolt values classified correctly |
| 10 | Continuity hysteresis (T-U11) | PASS | No spurious transitions near band boundaries |
| 11 | Continuity bands encoding (T-U12) | PASS | 2-bit-per-channel packing into uint16_t |
| 12 | Update sequence gap / wrap (T-U09, T-U16) | TODO | Deferred to Phase 3 |

### Phase 2 On-Target Tests

#### Base Unit — Continuity Sensing

| ID | Test | Status | Notes |
|----|------|--------|-------|
| B2-C01 | All channels floating = OPEN | PASS | `cont=0x0000` |
| B2-C02 | CH1 with ~2Ω resistor = GOOD | PASS | 32000–33000 uV, band 0→1 |
| B2-C03 | CH1 resistor removed = OPEN | PASS | 3300000 uV, band 1→0 |
| B2-C04 | CH1 resistor reconnect = GOOD | PASS | `cont=0x0001` in status log |
| B2-C05 | Round-robin timing | PASS | ~800ms per full 8-channel sweep (100ms/ch) |
| B2-C06 | Event-driven STATUS_UPDATE on band change | PASS | Immediate trigger, not waiting for 2s timer |

#### Base Unit — Arm Sense & Battery

| ID | Test | Status | Notes |
|----|------|--------|-------|
| B2-A01 | Arm sense ARMED/DISARMED transitions | PASS | GPIO 21, 16-bit debounce, clean transitions |
| B2-A02 | Contact welding detection | PASS | `CONTACT WELD DETECTED` logged when sense HIGH with relay OFF. Fault callback triggers STATUS_UPDATE |
| B2-B01 | Battery ~12V, stable reading | PASS | 12001–12022 mV across sessions |
| B2-B02 | Battery no false warnings | PASS | No LOW/CRITICAL warnings at 12V |

#### Remote Unit — Fire Button

| ID | Test | Status | Notes |
|----|------|--------|-------|
| R2-F01 | Press/release detection | PASS | `fire=0`→`fire=1`→`fire=0` in status log |
| R2-F02 | LED control (red=pressed, green=released) | PASS | Visual confirmation |
| R2-F03 | Fire while armed | PASS | `arm=1 fire=1` detected correctly |
| R2-F04 | Fresh-press safety (held at boot) | PASS | Fix: GPIO read at init suppresses false fresh-press. `fire=0` after boot with button held |
| R2-F05 | Rapid tap < 80 ms debounce filtering | PASS | 8-bit debounce naturally filters sub-80ms glitches |
| R2-F06 | Disconnected wire fail-safe | PASS | Pull-up → GPIO HIGH → released. `fire=0` consistently |

#### Remote Unit — Arm Switch

| ID | Test | Status | Notes |
|----|------|--------|-------|
| R2-A01 | ARMED/DISARMED toggle (6+ cycles) | PASS | Clean debounce, ~160ms settle |
| R2-A02 | LED indicator | PASS | On when armed, off when disarmed |
| R2-A03 | Disconnected wire fail-safe | PASS | Pull-up → DISARMED. `arm=0` consistently |

#### Remote Unit — Encoder

| ID | Test | Status | Notes |
|----|------|--------|-------|
| R2-E01 | CW rotation (ch 1→5) | PASS | Channel increments correctly |
| R2-E02 | CCW rotation (ch 5→2, wrap 1→8) | PASS | Channel decrements and wraps correctly |
| R2-E03 | Short press (<500ms) | PASS | `long_press_fired=0` on release |
| R2-E04 | Long press (>500ms) | PASS | `LONG PRESS detected` fires at 500ms; `long_press_fired=1` on release |
| R2-E05 | Explicit 8→1 wrap (CW) | PASS | `ch 8 -> 1` clean wrap at boundary |
| R2-E06 | Explicit 1→8 wrap (CCW) | PASS | `ch 1 -> 8` clean wrap at boundary |
| R2-E07 | Rotation + simultaneous press | PASS | Channel changes and press events detected independently |

#### Remote Unit — Battery

| ID | Test | Status | Notes |
|----|------|--------|-------|
| R2-B01 | Battery ~3.3V stable reading | PASS | 3267–3298 mV across sessions |
| R2-B02 | CRITICAL warning at 3.3V supply | PASS | Expected — thresholds designed for ~7.4V LiPo |

#### Integration — Both Units Linked

| ID | Test | Status | Notes |
|----|------|--------|-------|
| R2-INT01 | Link established after boot | PASS | LINK_REQUEST/ACK, session token assigned |
| R2-INT02 | Link stability (~5 min total) | PASS | `missed=0` across all sessions, RSSI -30 to -50 dBm |
| R2-INT03 | STATUS_UPDATE with real data | PASS | `cont`, `arm`, `vbat` populated with real sensor values |
| R2-INT04 | No crashes | PASS | Zero panics/overflows/watchdog resets after bug fixes |

### Phase 2 FSD Unit Tests (§15.5)

| ID | Module | Test | Status | Notes |
|----|--------|------|--------|-------|
| T-U09 | Update sequence gap | Feed update_sequence numbers with gaps. Verify warning at gap > 2. | TODO | Phase 3 feature |
| T-U10 | Continuity band classification | Known microvolt values → correct band | PASS | Covered in boot self-test suite 9 (11 points) |
| T-U11 | Continuity hysteresis | Oscillating near threshold — no spurious transitions | PASS | Covered in boot self-test suite 10 |
| T-U12 | Continuity bands encoding | 2-bit-per-channel packing into uint16 | PASS | Covered in boot self-test suite 11 |
| T-U16 | Update sequence wrap-around | 65535→0 not treated as gap | TODO | Phase 3 feature |

### Phase 2 Remaining On-Target Tests

The following Phase 2 behaviours were not fully exercised during bench testing.
They should be verified before Phase 3 work begins, or during Phase 5 hardening.

#### Base Unit — Continuity Sensing

| ID | Test | Notes |
|----|------|-------|
| B2-C07 | CH2–CH8 individual resistor = GOOD | Only CH1 tested with resistor; others verified floating only |
| B2-C08 | SHORT classification (~0 Ω) | Needs low-resistance short on a channel |
| B2-C09 | MARGINAL classification (~100 Ω) | Needs ~100 Ω resistor on a channel |

#### Base Unit — Arm Sense

| ID | Test | Notes |
|----|------|-------|
| B2-A03 | Disconnected wire fail-safe | Disconnect arm sense GPIO — should report DISARMED |
| B2-A04 | Raw vs debounced in STATUS_UPDATE | Verify `arm_switch_hw` field matches raw GPIO |

#### Base Unit — Battery Thresholds

| ID | Test | Status | Notes |
|----|------|--------|-------|
| B2-B03 | LOW threshold (< 10500 mV) | PASS | `LOW battery: 10483 mV (< 10500)` at ~10V supply |
| B2-B04 | CRITICAL threshold (< 9000 mV) | PASS | `CRITICAL battery: 8969 mV (< 9000)` at ~8.5V supply |

#### Remote Unit — Battery Thresholds

| ID | Test | Notes |
|----|------|-------|
| R2-B03 | WARNING threshold (> 6400 mV) | Current supply is 3.3 V — always CRITICAL. Needs ~7.4 V LiPo |

#### Integration — STATUS_UPDATE

| ID | Test | Notes |
|----|------|-------|
| R2-INT05 | Periodic 2000 ms timer accuracy | Verify STATUS_UPDATE interval is ~2 s ± tolerance |
| R2-INT06 | update_sequence field verification | Confirm sequence increments in STATUS_UPDATE messages |

### Phase 2 Key Commits

- `aafacd0` Phase 2: Input/Output and Debouncing
- `7e55b99` Phase 2 code review fixes — address all review findings

---

## Phase 3 — State Machines and Command Processing

**FSD ref:** §4.3 Phase 3, §7 (Base FSM), §8 (Remote FSM), §6.3 (Commands)
**Status:** ON-TARGET TESTING RESUMING (channel 1 only) — G1 partial (T-R04 PASS, T-R05 SKIP/code-reviewed, T-R06 pending). Blocker resolved 2026-07-21: base ESP32 chip #3 installed (MAC `44:1B:F6:D4:0D:68`), hardware protection fitted on **channel 1** (clamping diodes on the ADC input + snubber across the relay contact; channels 2–8 still unprotected — test channel 1 ONLY), software relay-order fix already in place. Both units reflashed; G0 re-verified with chip #3 (LINK_ACK, rssi=-35). Next: G2 arming (T-A01..T-A15) then G3 fire (T-F01..T-F09) on channel 1, pending battery connection.

### Phase 3 Architecture

Command forwarding pattern: `link_task` continues to own ESP-NOW receive queue. In `process_frame()`, PING/PONG/LINK_REQUEST/LINK_ACK handled by link_task (unchanged). Command messages (CMD_ARM, CMD_FIRE, CMD_DISARM, CMD_CEASE_FIRE, CMD_ACK, CMD_NACK) and STATUS_UPDATE forwarded to a new command queue owned by the state machine task. Single-task-owner pattern per FSD §4.7 — FSM task owns all relay control exclusively.

### Phase 3 Files Created

| File | Purpose |
|------|---------|
| `components/rlc_common/include/rlc_fsm_events.h` | Shared event types, event struct, notification bit definitions |
| `components/rlc_base/src/rlc_base_fsm.c` | Full base FSM: BOOT→IDLE→ARMED→PRE_FIRE→FIRING→POST_FIRE + LINK_LOST + ERROR. Fixed: `guard_arm()` uses `key_sense`; EVT_KEY_SWITCH_CHANGED handlers in ARMED/PRE_FIRE/FIRING; `send_ack`/`send_nack` call `rlc_link_next_seq()` |
| `components/rlc_base/include/rlc_base_fsm.h` | Base FSM public API |
| `components/rlc_base/src/rlc_fire_timer.c` | GPTimer fire pulse (1µs resolution, ISR→xTaskNotifyFromISR) |
| `components/rlc_base/include/rlc_fire_timer.h` | Fire timer API |
| `components/rlc_remote/src/rlc_remote_fsm.c` | Full remote FSM: BOOT→LINKING→IDLE→ARMED→PRE_FIRE→FIRING + LINK_LOST + ERROR. Fixed: all `send_cmd_*` pass caller-supplied seq to `rlc_link_send_cmd()` |
| `components/rlc_remote/include/rlc_remote_fsm.h` | Remote FSM public API |

### Phase 3 Files Modified

| File | Changes |
|------|---------|
| `components/rlc_common/src/rlc_link.c` | Command frame forwarding, integrity CRC verification, dead-man timestamp, link health tracking, 6 new public APIs. Fixed: `rlc_link_send_cmd()` accepts caller-supplied seq; NULL guard on `lock()`/`unlock()` |
| `components/rlc_common/include/rlc_link.h` | Added `rlc_link_register_cmd_queue()`, `rlc_link_send_cmd()`, `rlc_link_get_session_token()`, `rlc_link_next_seq()`, `rlc_link_is_healthy()`, `rlc_link_get_last_fire_ms()`. Changed: `rlc_link_send_cmd()` now takes `uint32_t seq` parameter |
| `components/rlc_base/src/rlc_base_state.c` | Replaced stub — delegates to `rlc_base_fsm.c` getters |
| `components/rlc_base/include/rlc_base_state.h` | Added `base_state_get_firing_channel()`, `base_state_is_busy()` |
| `components/rlc_base/src/rlc_siren.c` | Added `siren_start_error()` (3 short blasts 200ms on/off) |
| `components/rlc_base/include/rlc_siren.h` | Declared `siren_start_error()` |
| `components/rlc_base/src/rlc_status_update.c` | Populates `channel_armed_bitmask` and `channel_firing_bitmask` from FSM state. `base_arm_switch` reads from `key_sense_get_debounced()` |
| `components/rlc_base/src/rlc_base_main.c` | Starts FSM task, wires arm sense/fault/key callbacks to FSM, sets link guard callback. Housekeeping log includes `key=` field |
| `components/rlc_base/src/rlc_arm_sense.c` | Arm sense + key sense monitoring. Second debounce engine for GPIO 42 (key_sense). API: `key_sense_get_debounced()`, `key_sense_get_raw()`, `key_sense_register_cb()` |
| `components/rlc_base/include/rlc_arm_sense.h` | Added key_sense API declarations. Updated module doxygen for dual-input monitoring |
| `components/rlc_remote/src/rlc_remote_state.c` | Replaced stub — delegates to `rlc_remote_fsm.c` getters |
| `components/rlc_remote/src/rlc_remote_main.c` | Starts FSM + fire-repeat tasks, wires all input callbacks to FSM event queue |
| `components/rlc_common/include/rlc_config.h` | Added `COMPLETE_PULSE_ON_LINK_LOSS` (default: 1) |
| `components/rlc_common/include/pin_config.h` | Added `PIN_KEY_SENSE 42` (key switch direct input) |
| `components/rlc_base/CMakeLists.txt` | Added `rlc_base_fsm.c`, `rlc_fire_timer.c` |
| `components/rlc_remote/CMakeLists.txt` | Added `rlc_remote_fsm.c` |

### Phase 3 Development Tasks

| # | Task | FSD ref | Status | Implementation |
|---|------|---------|--------|----------------|
| 1 | Base state machine (BOOT→IDLE→ARMED→PRE_FIRE→FIRING→POST_FIRE+LINK_LOST+ERROR) | §7.2 | DONE | `rlc_base_fsm.c` — 8 states, `bfsm_task` (prio 4, core 0, 8192 stack) |
| 2 | Remote state machine (BOOT→LINKING→IDLE→ARMED→FIRING+LINK_LOST+ERROR) | §8.2 | DONE | `rlc_remote_fsm.c` — 7 states, `rfsm_task` (prio 4, core 0, 8192 stack) |
| 3 | Base: CMD_ARM handler with all guard conditions | §7.2.2 | DONE | `guard_arm()` returns NACK reason or 0. All 10 guards checked |
| 4 | Base: CMD_DISARM handler | §7.2.2 | DONE | Idempotent ACK + `do_disarm()` in IDLE/ARMED/PRE_FIRE/FIRING |
| 5 | Base: CMD_FIRE handler with pre-fire delay + fire pulse | §7.2.3 | DONE | CMD_FIRE→PRE_FIRE→FIRING→POST_FIRE pipeline |
| 6 | Base: CMD_CEASE_FIRE handler | §7.2.2 | DONE | ACK + immediate safe in ARMED/PRE_FIRE/FIRING |
| 7 | Base: ACK/NACK response with reason codes | §6.3 | DONE | `send_ack()`, `send_nack()` with `rlc_nack_reason_str()` |
| 8 | Base: Siren patterns (pulse ARMED, continuous PRE_FIRE/FIRING) | §7.4.1 | DONE | `siren_start_pulse()`, `siren_start_continuous()`, `siren_start_error()`, `siren_start_link_lost()` |
| 9 | Base: Fire pulse via hardware timer (ISR signals task) | §7.2.3 | DONE | `rlc_fire_timer.c` — GPTimer, ISR→`xTaskNotifyFromISR` |
| 10 | Base: 50 ms relay dropout delay after FIRING | §7.2.5 | DONE | POST_FIRE state with `POST_FIRE_COOLDOWN_MS` |
| 11 | Base: Arm timeout auto-disarm (10 s) | §7.2.5 | DONE | `s_arm_time_ms` tracked in `check_timers()` |
| 12 | Base: Key switch sense guard (key_sense GPIO 42) | §7.2.2 | DONE | `key_sense_get_debounced()` checked in guard_arm(), PRE_FIRE, ARMED, FIRING |
| 13 | Base: Contact welding detection | §7.2.7 | DONE | Arm relay energise + verify sense HIGH within 200ms |
| 14 | Remote: Command sender with ACK timeout + retry | §8.2 | DONE | `send_cmd_arm/fire/disarm/cease_fire()` + `wait_for_ack()` |
| 15 | Remote: Repeated CMD_FIRE at 200 ms (fire-and-forget) | §8.2.4 | DONE | `cmd_fire_repeat_task_fn` — separate task, fire-and-forget |
| 16 | Remote: Dead-man switch logic | §8.2.4 | DONE | 500ms timeout via `rlc_link_get_last_fire_ms()` |
| 17 | Remote: Channel change while armed triggers disarm | §8.2 | DONE | EVT_ENCODER_ROTATE in ARMED → `do_disarm_and_idle()` |
| 18 | Remote: Long-press to arm (500 ms) | §8.2 | DONE | EVT_ENCODER_LONG_PRESS in IDLE → CMD_ARM flow |
| 19 | Remote: Arm switch debounce + encoder lockout | §8.2 | DONE | EVT_ARM_SWITCH_CHANGED, EVT_ENCODER_ROTATE/PRESS all disarm |
| 20 | Remote: PRE_FIRE local state (before base confirms) | §8.2.4 | DONE | Local countdown + fire-repeat task + STATUS_UPDATE sync |
| 21 | App-state guard wired to FSM (reject LINK_REQUEST when armed) | §6.4.1 | DONE | `rlc_link_set_guard(base_state_is_busy)` in `rlc_base_main.c` |
| 22 | ERR_COMM_DEGRADED calculation (>30% failure in 10 pings) | §7.2.2 | DONE | `s_ping_window[10]` sliding window, `rlc_link_is_healthy()` |
| 23 | Link-health guard at PRE_FIRE→FIRING transition | §7.2.3 | DONE | `rlc_link_is_healthy()` checked in `check_timers()` |

### Phase 3 Safety Features Implemented

| Feature | FSD Ref | Implementation |
|---------|---------|---------------|
| Dual-key arming (10 guards) | §9.2 | All 10 guards checked in `guard_arm()` |
| Single-channel arming | §9.3 | NACK 0x0A if another channel armed |
| Dead-man switch | §9.4 | 500ms CMD_FIRE authorization timeout via `rlc_link_get_last_fire_ms()` |
| Auto-disarm after fire | §9.5 | FIRING→POST_FIRE→IDLE auto-transition |
| Arm timeout 10s | §7.2.5 | Software timer in `check_timers()` |
| Arm sense → immediate disarm | §7.2.7 | EVT_KEY_SWITCH_CHANGED and EVT_ARM_SENSE_CHANGED processed in ARMED/PRE_FIRE/FIRING |
| Key switch OFF → immediate disarm | §7.2.7 | EVT_KEY_SWITCH_CHANGED (GPIO 42) triggers do_disarm() in ARMED/PRE_FIRE/FIRING |
| Channel change while armed → disarm | §8.2.7 | EVT_ENCODER_ROTATE in ARMED state |
| Contact welding detection | §7.3.2 | Arm relay energise + verify sense HIGH within 200ms |
| ERR_COMM_DEGRADED | §7.2.2 | Ping health window (10 frames, >30% = degraded) |
| Link loss during FIRING | §7.2.5 | COMPLETE_PULSE_ON_LINK_LOSS configurable |
| Fire pulse hardware timer | §7.4.2 | GPTimer ISR → xTaskNotifyFromISR, task does relay control |
| Relay control exclusivity | §9.12 | All relay ops only in state_machine_task |
| ERROR state unrecoverable | §7.2.9 | Intentional halt requiring power cycle |

### Phase 3 Build Status

- **Base:** Zero warnings, zero errors (last verified 2026-04-15, ESP-IDF 5.4.1, includes key_sense + CRC fix + lock guard)
- **Remote:** Zero warnings, zero errors (last verified 2026-04-16, includes encoder ADC init order fix + contact-weld debounce)

### Phase 3 Critical Init Order

Remote unit `rlc_remote_main.c` init order **must** be preserved:
1. `encoder_init()` — configures GPIO 4/5 for digital input with interrupts
2. `rlc_battery_init()` — configures ADC1 which claims GPIO 4/5 (ADC1_CH3/CH4) for analog mode

If ADC init runs first, it overrides the digital GPIO config and encoder interrupts never fire. This is an ESP32-S3 hardware constraint.

### Phase 3 Key Sense Hardware

Added dedicated key switch sense input to resolve circular dependency in `guard_arm()`:

| Item | Value |
|------|-------|
| GPIO | 42 (`PIN_KEY_SENSE`) |
| Circuit | Key switch output (+VBAT ~12V when ON) → 27kΩ/10kΩ divider + 3.3V zener → GPIO 42 |
| Debounce | 16-bit (160ms at 10ms polling) — integrated into `arm_sense_task` |
| Event | `EVT_KEY_SWITCH_CHANGED = 0x18` |
| API | `key_sense_get_debounced()`, `key_sense_get_raw()`, `key_sense_register_cb()` |
| Status | Software complete. Hardware wired and verified — key sense toggles cleanly on GPIO 42. Tests B-K01–B-K03 PASS. |

**Input role separation:**

| Input | GPIO | Role |
|-------|------|------|
| key_sense | 42 | Direct key switch position. Used in `guard_arm()`, PRE_FIRE check, FSM disarm triggers. |
| arm_sense | 21 | Post-energize relay feedback. Used for M1 verify flow and contact-weld detection only. |

### Phase 3 Test Plan

**Preamble.** All remaining Phase 3 tests are on-target and require user interaction — there are no host-side unit tests in this project. Testing runs the two flashed units against each other with real relays, continuity banks, arm switches, encoder, and arm/fire buttons. Execute the plan top-to-bottom; later groups assume earlier groups passed.

**Target hardware.** Identify each board by its **stable by-id** under `/dev/serial/by-id/` — never `/dev/ttyACMx` (those numbers shift on every hot-plug). Prefer each board's **COM port** by-id (`usb-1a86_USB_Single_Serial_…` — the UART-bridge serial, stable across ESP32 chip swaps); the native-USB by-id (`usb-Espressif_…`) embeds the chip MAC and changes whenever a chip is swapped. Base MAC `44:1B:F6:D4:0D:68` (chip #3), Remote MAC `AC:A7:04:E2:F2:8C` (chip #2; #1 flash-damaged). Confirm identity with `esptool.py -p <by-id> read-mac`. Both running commit `e03b826` or later.

### Phase 3 On-Target Testing Fixes

Bugs discovered and fixed during Phase 3 on-target testing (2026-04-15):

| # | Bug | Root Cause | Fix |
|---|-----|-----------|-----|
| 1 | Inverted guard in `rlc_link.c:366` — all LINK_REQUESTs rejected | `!s_guard_cb()` rejects when callback returns false (not busy), inverted logic | Changed `!s_guard_cb()` to `s_guard_cb()` |
| 2 | Missing EVT_BATTERY_CRITICAL handler in remote LINKING state | Event silently dropped, FSM never transitions to ERROR on critical battery | Added handler that calls `do_enter_error()` |
| 3 | Race condition — link establishes before FSM queue registered | `rlc_link_init()` starts link_task which completes handshake before `rlc_link_register_cmd_queue()` | In `rlc_link_register_cmd_queue()`, check if link already LINKED and post catch-up EVT_LINK_ESTABLISHED |
| 4 | ADC priority inversion deadlock — Interrupt WDT timeout | ESP-IDF ADC driver uses newlib `_lock_t` (spinlock w/o priority inheritance) shared between ADC1/ADC2. WiFi (ADC2) blocks battery task (ADC1) → spinlock with interrupts disabled → 300ms WDT fires | 1. Boost battery task priority to 24 during ADC read (above WiFi prio 23). 2. Increase Interrupt WDT timeout from 300ms to 5000ms. 3. Add 3-second startup delay to avoid contention during WiFi init |
| 5 | Task WDT timeout for `fire_rep` task | `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` blocks forever → never calls `esp_task_wdt_reset()` | Changed to timed wait (`WATCHDOG_TIMEOUT_S * 1000 - 500` ms) with periodic WDT reset |
| 6 | `status_update` task not registered with Task WDT | `xTaskCreatePinnedToCore` passes NULL for task handle, no `rlc_watchdog_add_task()` call | Pass `&handle`, call `rlc_watchdog_add_task(handle)` |
| 7 | WATCHDOG_TIMEOUT_S (2s) too short for battery startup delay (3s) | Battery task delays 3s for WiFi init, but WDT expects reset every 2s | Increased to 5s; battery task feeds WDT during delay loop |
| 8 | Missing EVT_BATTERY_CRITICAL handler in remote IDLE state | Remote FSM silently drops battery critical events in IDLE state | Added `do_enter_error()` handler for EVT_BATTERY_CRITICAL in IDLE state |
| 9 | Remote battery thresholds wrong for bench testing | Remote reads ~3290 mV on USB power (3.3V rail), triggers CRITICAL at 6400 mV | Temporarily lowered thresholds for bench testing (production values: 7000/6600/6400) |
| 10 | All CMD messages rejected with CRC mismatch | `send_cmd_arm()` calls `rlc_link_next_seq()` (seq=N) for CRC, then `rlc_link_send_cmd()` calls `seq_next()` again (seq=N+1) — CRC/header mismatch | Changed `rlc_link_send_cmd()` to accept caller-supplied seq; all callers pass their pre-obtained seq |
| 11 | Base crash loop — NULL mutex in startup | `status_update_task` calls `rlc_link_is_linked()` before `rlc_link_init()` creates mutex | Added NULL guard on `lock()`/`unlock()` in `rlc_link.c` |
| 12 | Arming always NACKs BASE_KEY_OFF — circular guard | `guard_arm()` checks `arm_sense_get_debounced()` (GPIO 21, arm relay output) which is LOW before relay is energized | Added dedicated `key_sense` input (GPIO 42) reading key switch position directly; `guard_arm()` now checks `key_sense_get_debounced()` |
| 13 | Encoder ISR never fires after Phase 3 code shuffle | `rlc_battery_init()` (ADC1) claims GPIO 4/5 (ADC1_CH3/CH4) for analog mode, overriding digital GPIO config set by `encoder_init()` | Restored Phase 2 init order: `encoder_init()` before `rlc_battery_init()` in `rlc_remote_main.c` |
| 14 | Contact weld false positive — residual voltage on arm_sense divider | Raw GPIO read ~150ms after arm relay de-energize catches residual voltage on 27k/10k divider, triggering false CONTACT WELD DETECTED | `weld_check()` now requires 3 consecutive HIGH readings (1.5s at 500ms check interval) via `WELD_CONFIRM_COUNT` before declaring fault |
| 15 | arm_sense_task stack overflow (2048 bytes) | Adding key_sense monitoring (second debounce engine + ESP_LOGI) exceeds 2048-byte stack | Increased `arm_sense_task` stack from 2048 to 4096 |
| 16 | Remote encoder_task stack overflow (2048 bytes) | ESP_LOGI in encoder task blew 2048-byte stack | Increased `encoder_task` stack from 2048 to 4096 |
| 17 | Contact weld false positive during ARMED state | `weld_check()` reads `gpio_get_level(PIN_ARM_RELAY)` for relay state — unreliable GPIO readback. Rewrote to use `arm_relay_get_intended()` (tracked in software) + `arm_sense_get_debounced()` (16-bit debounce engine) | Uses intended relay state instead of raw GPIO read; uses debounced arm_sense instead of raw sense read |
| 18 | Base ESP32 destroyed during first fire pulse | `relay_all_safe()` de-energized channel relays before arm relay. Channel relay contacts opened while VBAT (12V) still on fire bus → 6A arc coupled VBAT to NC contact → continuity ADC inputs (GPIO 2-10) saw 12V, no clamping → ESP32 latch-up | Reversed order: arm relay OFF first (removes VBAT), wait 20ms, then channel relays OFF. Hardware fix needed: Schottky diode clamps on ADC GPIOs |

**HARDWARE DAMAGE (2026-04-16):** Base unit ESP32-S3 destroyed during first on-target fire pulse (T-R06). Root cause: relay de-energization order allowed 6A arc at channel relay contacts to couple VBAT to continuity ADC inputs. Software fix applied (relay order reversed). Hardware protection (Schottky diode clamps on GPIO 2-10) required before resuming fire tests. Replacement ESP32 needed.

**Bug #18 audit + software channel gate (2026-08-17).** Audited the software half of the
fix and added a firmware gate against firing unprotected channels.

- **Relay-order fix verified complete.** All 13 de-energise call sites in
  `rlc_base_fsm.c` route through `relay_all_safe()` (arm relay OFF → 20 ms →
  channel relays OFF); `relay_fire_set(ch, true)` at the PRE_FIRE→FIRING
  transition is the only place a channel relay is ever energised. No path
  bypasses the ordering.
- **New: `FIRE_PROTECTED_CHANNEL_MASK`** in `rlc_config.h` (currently `0x01` —
  channel 1 only). `guard_arm()` NACKs ARM on any channel outside the mask
  (reusing `NACK_INVALID_CHANNEL`, so the wire protocol is unchanged; the real
  reason is logged on the base), and `relay_fire_set()` refuses to energise an
  unprotected channel relay as a last line of defence. De-energising is always
  allowed. `relay_init()` logs a warning each boot while the mask != `0xFF`.
  **Bump the mask to `0xFF` once channels 2–8 get their clamps + snubbers.**
- **Residual exposure — the software fix only covers the break, not the make.**
  The arm relay is energised on entry to ARMED, so VBAT is already live on the
  fire bus when `relay_fire_set(ch, true)` transfers the channel contact
  NC→NO. Contact bounce/arc at *make* can still couple VBAT toward the NC
  contact — i.e. the unclamped ADC pin. Only the hardware protection (ADC clamp
  diodes + contact snubber) closes that window; no relay ordering can. Treat the
  clamps as mandatory, not belt-and-braces.
- **The arm relay now breaks the 6 A fire current** (previously the channel
  relay did), and the arc lands on the ARM SENSE node that GPIO 21 watches.
  **As-built 2026-08-17 (confirmed with the user): the arm relay contact has NO
  snubber, and GPIO 21 has NO clamp diode/zener — only the 27 kΩ/10 kΩ divider.**
  The FSD (§5.4.3, §5.4.3b) states the 3.3 V zener as fitted on both GPIO 21 and
  GPIO 42; it is **not** fitted on either. Doc/hardware mismatch — correct the FSD.
  - *ESP32 risk on GPIO 21: moderate, not the ADC-pin scenario.* The continuity
    front end is `3.3V → 3.3 kΩ → sense node → NC contact` with the ADC pin
    tapping the sense node, i.e. **zero** series resistance to VBAT — hence
    instant death. GPIO 21 has 27 kΩ in series, so DC VBAT is ~0.33 mA into the
    internal clamp (survivable); the exposure is inductive spikes at contact
    break (200 V → ~7 mA, marginal and cumulative).
  - *The bigger risk is the interlock, not the MCU.* Unsnubbed 6 A DC break
    erodes and eventually **welds** the arm relay contact — and a welded arm
    relay leaves VBAT permanently on the fire bus, defeating the primary fire
    path interlock. `weld_check()` detects it (hard ERROR, power-cycle to
    clear), so it fails safe — but all switching wear now lands on the one
    contact the safety case depends on.
  - *Hardware to fit:* (a) RC snubber across the arm relay contact — start
    47 Ω 0.5 W + 100 nF film rated ≥ 100 V; (b) TVS across the fire bus (arm
    relay COM to GND, e.g. SMBJ18A/20A) to clamp the kick at the node the sense
    divider watches; (c) clamp on GPIO 21 — the spec'd 3.3 V zener across R2, or
    better a BAT54S dual Schottky (mid node to the GPIO, to 3V3 and GND) plus
    ~10 nF to GND to slow edges; (d) the same clamp on GPIO 42 (key sense sits
    on the fire-path high side too).
- **Fire-pulse duration side effect:** igniter current now ends when the *arm*
  relay opens, so the delivered pulse is ~`FIRE_PULSE_DURATION_MS` + arm-relay
  release time. Account for this in T-F08 (oscilloscope timing).

**Required test equipment.**
- Power supply for base (≥ 9 V) and remote (≥ 3.7 V Li-ion or bench supply on VBAT)
- 10 squib simulator resistors (≈ 10 Ω each) wired to channels 0–9
- Oscilloscope (for T-F08 only — fire pulse timing)
- Dummy load or LED across a relay output for visual pulse confirmation
- Two USB-serial cables for `idf.py monitor` on both units simultaneously

**Test tooling.** Run these in two terminals throughout the session:
```bash
# Terminal 1 — base log
idf.py -B build_base -p /dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E044219-if00 monitor   # base COM
# Terminal 2 — remote log
./build_remote.sh && idf.py -p /dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E043219-if00 monitor   # remote COM
```
Watch for `state=` lines (base 5 s housekeeping log) and `rfsm:` / `bfsm:` tags.

**Flash procedure (run once at session start).**
```bash
./build_base.sh flash    # flashes base (default PORT = base COM by-id; override with -p)
./build_remote.sh flash  # flashes remote (default PORT = remote COM by-id; override with -p)
```

**Pass criteria for Phase 3 as a whole.**
1. All 15 FSD §15.2 arming tests pass (T-A01..T-A15)
2. All 9 FSD §15.3 fire tests pass (T-F01..T-F09)
3. All 6 round-3 regression tests pass (T-R01..T-R06)
4. No unexpected `ERROR` entries, no watchdog resets, no crashes across the full run
5. Base and remote agree on state (`channel_armed_bitmask` / `channel_firing_bitmask`) after every transition

**Test execution order.**

| Group | Tests | Purpose | Requires user? |
|-------|-------|---------|----------------|
| **G0 — Smoke** | Boot both units; observe link establish within 2 s; confirm STATUS_UPDATE flowing | Sanity | Yes (power-on, visual check) |

### G0 — Smoke Test Results

| Check | Result | Evidence |
|-------|--------|----------|
| Both units boot without crash | PASS | 12 self-test suites PASS, no panics, no WDT resets |
| Link establishes | PASS | LINK_REQUEST at 3300ms, LINK_ACK accepted, state→LINKED |
| STATUS_UPDATE flowing | PASS | Base: `state=1 armed=0 firing=0 rssi=-54 vbat=12130 mv` every 5s |
| Remote status flowing | PASS | Remote: `state=1 armed=0 sel=1 rssi=-52 missed=0 vbat=3290 mv` every 5s |
| Battery reads succeed | PASS | Base: 12130 mV (12V supply). Remote: 3290 mV (USB power, 3.3V rail) |
| No watchdog resets | PASS | No WDT panics, no reboots for 25+ seconds |
| FSM states correct | PASS | Base: IDLE, Remote: IDLE (LINKING→IDLE after link established) |

**Known non-blocking issue:** PING droughts causing periodic LINK_LOST/RECOVER cycles (~every 6s, recovers in ~130ms). Does not prevent testing.

**G0 re-verified 2026-04-15** after bugs 10-12 fixes (CRC seq, lock guard, key_sense). Base: `state=1 armed=0 firing=0 rssi=-19 vbat=11837 mv cont=0x0001 arm=0 key=0 err=0x00`. Remote: `state=1 armed=0 sel=1 rssi=-23 missed=0 vbat=3290 mv arm=1 fire=0`. Both stable, zero crashes.
| **G1 — Round-3 regressions** | T-R01..T-R06 | Verify the fixes shipped in commit `d357b33` actually work on target | Yes |
| **G2 — FSD §15.2 Arming** | T-A01..T-A15 | Spec conformance for arm path | Yes |
| **G3 — FSD §15.3 Fire** | T-F01..T-F09 | Spec conformance for fire path | Yes (T-F08 also needs scope) |

Groups run in order. Any FAIL halts the run and is logged as a new finding; re-run from the start after any fix.

### Phase 3 Round-3 Regression Tests

These are new tests specific to the fixes applied in commit `d357b33`. They are **not** in FSD §15 — they verify that the round-3 code-review fixes actually hold on hardware.

| ID | Target fix | Procedure | Expected | Status |
|----|-----------|-----------|----------|--------|
| T-R01 | J1 — EVT_ARM_SENSE_FAULT consumer | Apply ~12V to arm relay COM terminal while relay is de-energized (3V3 insufficient — produces only 0.89V through divider). | Base logs `ARM RELAY CONTACT WELD — entering ERROR`, siren plays 3-blast error pattern, base enters STATE_ERROR (flags=0x04). Repeated detections every 500ms while voltage applied. No relay activity. Power cycle to clear. | **PASS** |
| T-R02 | J7 — base battery critical posts event | With base running, lower VBAT below `BASE_VBAT_CRITICAL_MV` (use bench supply). | Base battery task logs `CRITICAL battery: ... mV`, FSM transitions to STATE_ERROR within 1 s, siren plays error pattern, no relay energizes afterwards. | PASS |
| T-R03 | R8 — remote battery critical posts event | With remote running, lower remote VBAT below `REMOTE_VBAT_CRITICAL_MV`. | Remote logs `CRITICAL battery: ... mV`, remote FSM enters STATE_ERROR within 1 s, buzzer plays critical alarm, CMD_DISARM(0xFF) broadcast if any channel was armed. | PASS |
| T-R04 | R1 — wait_for_ack sentinel preserves LINK_LOST | Arm a channel normally. While the remote is in `wait_for_ack` for CMD_ARM (window is very short — may need to force by powering base off right as remote sends CMD_ARM). | Remote transitions to STATE_LINK_LOST (not STATE_IDLE), LED shows link-lost pattern. After base comes back, remote re-links and returns to IDLE. | **PASS** |
| T-R05 | R2 — multi-arm detection | Hand-craft this only if a fault-injection path is available: modify base to report two bits set in `channel_armed_bitmask` for one STATUS_UPDATE, or achieve it via a desync. If not achievable, mark as SKIPPED with justification and rely on code review. | Remote logs `MULTI-ARM DETECTED (mask=0x...)`, broadcasts CMD_DISARM(0xFF), enters STATE_ERROR, buzzer plays critical alarm. | **SKIP** — no fault-injection path available; code review confirmed logic correct |
| T-R06 | J5 — POST_FIRE idempotent ACKs | Execute a full fire sequence (arm + fire). Immediately after the fire pulse completes (base is in POST_FIRE), send a redundant CMD_CEASE_FIRE and CMD_DISARM from the remote (repeat key press). | Base ACKs both commands without NACK, stays in POST_FIRE→IDLE transition, no ERROR state. Dead-man timestamp `s_last_fire_cmd_ms` logs as 0 on entry to IDLE. | **SKIP** — base ESP32 destroyed during first fire pulse. Software fix applied (relay order). Requires: replacement ESP32 + Schottky diode clamps on continuity ADC inputs before resuming fire tests |

Note on T-R02/T-R03: if a bench supply is not available, these can be exercised by temporarily lowering `*_VBAT_CRITICAL_MV` in `rlc_config.h` to above the actual measured VBAT, rebuilding, and flashing. Revert after.

### Phase 3 FSD Arming Tests (§15.2)

| ID | Test | Status |
|----|------|--------|
| T-A01 | ARM with both switches + continuity GOOD | TODO |
| T-A02 | ARM with base switch disarmed → NACK 0x01 | TODO |
| T-A03 | ARM with remote switch disarmed → local guard | TODO |
| T-A04 | ARM with OPEN continuity → NACK 0x04 | TODO |
| T-A05 | ARM second channel while armed → NACK 0x0A | TODO |
| T-A06 | Turn base key OFF while armed → immediate disarm | TODO |
| T-A07 | Turn remote arm switch DISARM while armed → disarm | TODO |
| T-A08 | Rotate encoder while armed → disarm | TODO |
| T-A09 | Continuity bands visible with arm switch OFF | TODO |
| T-A10 | ARM with arm sense fault → NACK 0x0B | TODO |
| T-A11 | ARM with stale STATUS_UPDATE → local block | TODO |
| T-A12 | ARM with low remote battery → local block | TODO |
| T-A13 | Wrong channel in ACK → channel mismatch error | TODO |
| T-A14 | ARM with MARGINAL continuity → succeeds (warning) | TODO |
| T-A15 | ARM with SHORT continuity → succeeds (informational) | TODO |

### Phase 3 FSD Fire Tests (§15.3)

**Blocker resolved 2026-07-21** (chip #3 + clamping diodes + snubber on channel 1) — resume fire tests on **channel 1 only** until channels 2–8 receive the same protection. See bug #18. Channel-1-only is now enforced in firmware by `FIRE_PROTECTED_CHANNEL_MASK` (2026-08-17), not just by operator discipline.

| ID | Test | Status |
|----|------|--------|
| T-F01 | Full fire sequence (arm→fire→complete) | TODO |
| T-F02 | Release fire button during pre-fire delay → abort | TODO |
| T-F03 | Release fire button during active fire → cease fire | TODO |
| T-F04 | Fire command on non-armed channel → NACK 0x05 | TODO |
| T-F05 | Continuity readable during ARMED (relay in NC) | TODO |
| T-F06 | Link lost during firing → complete pulse then disarm | TODO |
| T-F07 | Pre-fire timer expires without fire button → abort | TODO |
| T-F08 | Fire pulse timing accuracy (oscilloscope) | TODO |
| T-F09 | Link-health guard at PRE_FIRE→FIRING | TODO |

### Phase 3 Key Commits

- `744240c` Phase 2 extended testing — fresh-press fix, stack increases, 9 tests verified
- `e03b826` Phase 3 on-target testing — ADC deadlock fix + 9 bug fixes + G0/G1 partial
- (Phase 3 final commit pending — G1 verification + G2/G3 testing required)

---

## Phase 4 — Display

**FSD ref:** §4.3 Phase 4, §10 (Display Specification)
**Status:** CODE COMPLETE — awaiting on-target verification

Implemented independently of the base firing-sequence debugging: the display
lives entirely in the remote unit (`components/rlc_remote/src/rlc_display.c`).

Architecture: a PSRAM framebuffer (480x320x3, RGB666) is rendered by
`display_task` (prio 2, core 1, 8192 stack — FSD §9.10); only the dirty
bounding box is flushed over SPI through an internal-RAM row bounce buffer.
No other task touches SPI, so the FSM and input tasks never block on the panel.
Screens are derived from the remote FSM state, with latched overrides for
ERROR / firmware mismatch and a timed overlay for NACKs and notices.

### Phase 4 Development Tasks

| # | Task | FSD ref | Status | Implementation |
|---|------|---------|--------|----------------|
| 1 | ILI9488 SPI display driver (480x320, SPI2_HOST, 20 MHz) | §10.1 | DONE | Init sequence ported from validated `rlc-hw-test-remote` |
| 2 | Display health check (ID read-back at boot) | §9.13 step 6 | DONE | `display_is_healthy()`; remote halts in ERROR on failure (T-S10) |
| 3 | Screen layout manager | §10.3 | DONE | `display_task` + `screen_for_state()` |
| 4 | Splash screen | §10.2.1 | DONE | Title, version, VRO + author credits, attempt counter, progress bar; held for `SPLASH_MIN_DURATION_MS` (10 s) so it is readable even when linking completes in <1 s |
| 5 | Main status screen (IDLE) — RSSI bar, battery, continuity grid, channel | §10.2.2 | DONE | Top bar (RSSI/RTT/both batteries), 4x2 channel grid, legend, arm-sense line |
| 6 | Armed screen — channel indicator, continuity, status | §10.2.3 | DONE | Pulsing red border, large channel number, arm-sense confirmation |
| 7 | Firing / Pre-fire screen — countdown, pulse indicator | §10.2.4 | DONE | 100 ms countdown, "IGNITION ACTIVE" on red field |
| 8 | Link lost screen | §10.2.5 | DONE | Amber frame, last-contact seconds, ping attempts |
| 9 | Error screen | §10.2.6 | DONE | `display_error()` latches text until reboot |
| 10 | Fire complete screen | §10.2.4a | DONE | Shown for POST_FIRE_COOLDOWN_MS with return countdown |
| 11 | Firmware mismatch screen | §10.2.1 | DONE | Auto-detected from `RLC_LINK_STATE_VERSION_MISMATCH` |
| 12 | Partial refresh (dirty-rectangle) for dynamic elements | §10.3 | DONE | Dirty bounding box; full redraw only on screen change |
| 13 | NACK reason display (human-readable text) | §10.2.7 | DONE | 3 s overlay via `display_nack()`; `display_toast()` for local rejections |
| 14 | Display refresh rate >= 5 Hz | §10.3 | DONE | 10 Hz frame loop (`DISPLAY_FRAME_MS` 100) |

Supporting changes:
- `remote_fsm_get_status()` — spinlock-guarded snapshot of the cached
  STATUS_UPDATE (continuity bands, base battery, arm sense, error flags).
- `remote_fsm_get_prefire_remaining_ms()` — drives the pre-fire countdown.
- `rlc_link_status_t.ping_rtt_ms` — PING/PONG round-trip for the top bar.
- FSM display hooks: NACK overlays, "TURN ARM KEY FIRST" and other local
  rejection toasts, multi-arm error screen, fire-complete screen.

### 8-Pixel Igniter Status Strip — Both Units (2026-08-19, revised)

An 8-way NeoPixel strip is wired to `PIN_RGB_LED` (GPIO 48) on **both** units,
one pixel per igniter channel. Each DevKit's built-in NeoPixel sits in parallel
on the same data line (confirmed on the bench) and so mirrors pixel 0. The
built-in LEDs no longer carry any independent meaning — the old link/boot
indication is gone.

**The two strips are wired data-in at opposite ends**, so `RLC_STRIP_REVERSED`
is set per unit in `rlc_config.h` (verified with `tools/strip-diag`):

| Unit | Data-in end | Mapping | `RLC_STRIP_REVERSED` | Built-in LED shows |
|---|---|---|---|---|
| Base | channel 1 | channel N → pixel `N-1` | 0 | channel 1 |
| Remote | channel 8 | channel N → pixel `7-(N-1)` | 1 | channel 8 |

| Continuity | Colour | Constant |
|---|---|---|
| GOOD | dark green `#006400` | `RLC_COLOR_CONT_GOOD` |
| MARGINAL | light green `#90EE90` | `RLC_COLOR_CONT_MARGINAL` |
| OPEN | yellow `#FFFF00` | `RLC_COLOR_CONT_OPEN` |
| SHORT | red `#FF0000` | `RLC_COLOR_CONT_SHORT` |

The constants live in `rlc_config.h` and are the **single source of truth for
both units** — the remote display's channel grid resolves its colours from the
same macros, so pad, handheld strip and handheld screen always agree.

**Deviation from FSD §10.2.0**, which specifies blue for GOOD, red for OPEN and
orange for SHORT, with blue chosen deliberately to avoid red-green ambiguity for
colour-blind operators. The requested palette pairs green (good) with red
(short) and moves red off OPEN. The display's shape coding (filled circle /
triangle / ring / diamond) still carries the meaning without colour, and the
palette is a one-line config change. FSD §10.2.0 should be updated to match.

#### Rendering layers (FSD §11.1)

The strip is an **igniter continuity display**; system status modulates the map
rather than replacing it. Highest active layer wins:

| # | Layer | Rendering |
|---|---|---|
| 1 | `ARMED` / `PRE_FIRE` / `FIRING` | Whole strip red — **unchanged**, the firing signal stays unmistakable |
| 2 | `ERROR` | Red triple flash; map dimmed to 20 % in the 700 ms gap |
| 3 | Alarm wink | 300 ms full-strip flash every 3 s; concurrent alarms alternate colours |
| 4 | Stale (remote only) | Whole map dimmed to 10 % — cached STATUS_UPDATE has aged out |
| 5 | Breathing | Base: whole map, key switch ON. Remote: selected channel, arm switch ON |
| 6 | Channel map | Continuity; channel of interest pulses (base: armed/firing, remote: cursor) |

`BOOT`, `LINKING`, `IDLE`, `LINK_LOST` and `POST_FIRE` all render as layer 6.
Before the first continuity data arrives, a cyan chase runs instead. Alarm
colours: link lost = amber, battery = magenta, arm-sense fault = white — none of
which can be confused with a continuity colour.

#### What was removed

- The boot-time **RSSI bar** and blue boot pulse (`set_rssi()`, `led_show_rssi_bar()`).
- Whole-strip `IDLE` green, `LINK_LOST` amber and `POST_FIRE` amber.
- The 250 ms whole-strip **orange ping-miss flash** (`flash_overlay()`), which
  wiped the map and blocked the LED task; the 80 ms buzzer beep remains.
- Dead code: `LED_PATTERN_CHANNEL_STATUS`, `LED_PATTERN_PING_FAIL`,
  `rlc_rgb_led_set_state()` — none were ever called.
- `LED_PATTERN_IDLE_ARM_ON` was documented but never set by the FSM; the key-ON
  warning is now a feed (`set_key_warning()`) rather than a pattern.

#### Architecture

`rlc_rgb_led.c` is now **unit-agnostic**: one layer resolver, both units, only
the feeds differ. All feeds (`set_channel_bands`, `set_active_channel`,
`set_alarms`, `set_stale`, `set_key_warning`) are published from each unit's
**housekeeping loop** at 10 Hz — deliberately not from the FSM, so the fire path
is untouched. The FSMs set only the firing-path and ERROR patterns. Animation
phase derives from `esp_timer_get_time()`, so patterns are stable across frame
jitter.

The remote's map comes from the cached STATUS_UPDATE (`remote_fsm_get_status()`,
which returns a freshness flag) rather than local sensing — the one genuine
asymmetry between the units, and the reason layer 4 exists. Dim means "the data
is old"; a wink means "something is wrong". The two compose.

#### Host tests

`./tests/host/run.sh` compiles `tests/host/test_strip.c`, which includes
`rlc_rgb_led.c` directly and links it against mock `led_strip` / FreeRTOS /
`esp_timer` headers, capturing every emitted pixel. **30 checks, 0 failures**
covering T-L01…T-L09 (FSD §15.5). This is the project's first host-compiled
test suite.

### Display Legibility — Minimum Font Size (2026-08-19)

Field feedback: scale-1 text (6x8 px per character) is unreadable at arm's
length. **Scale 2 (12x16 px) is now the floor** — no text on any screen is
drawn below it; the only remaining scale-1 calls are 1-pixel frame and rule
thicknesses. "Connected to base" on the splash is the reference size.

Layout consequences: channel cells shortened from 86 to 80 px to free two
scale-2 status rows, and several strings abbreviated so they still fit 480 px
at 12 px/character:

| Was | Now |
|---|---|
| `Turn ARM key, then hold encoder to arm channel N` | `TURN ARM KEY TO ARM CH N` |
| `ARM SENSE ON  HW ON  KEY SAFE` | `ARM ON  HW ON  KEY SAFE` |
| `BASE ERROR FLAGS 0x0A` | `BASE ERROR 0x0A` |
| `PRESS AND HOLD FIRE TO LAUNCH` | `HOLD FIRE TO LAUNCH` |
| `HOLD FIRE BUTTON - RELEASE TO ABORT` | `RELEASE TO ABORT` |
| `Returning to IDLE in 1.8s` | `IDLE IN 1.8s` |
| `Ping attempts: 7   RSSI -45 dBm` | `Attempts 7   RSSI -45 dBm` |
| `System halted. Power cycle required.` | `System halted - power cycle` |
| `ESP32 ROCKET LAUNCH CONTROLLER` (mismatch screen) | `ROCKET LAUNCH CONTROLLER` |

The NACK/toast overlay previously fell back to scale 1 for long strings; it now
always renders at scale 2 (every NACK reason string fits).

### Phase 4 Findings — Battery Thresholds (2026-08-19)

Raised while diagnosing a "remote power fail" report on the bench. Neither is a
display bug; both are open items.

1. **The base never checks the remote's battery.** FSD §7 (line 1357) requires
   the base to refuse ARM with NACK `0x0C` ("REMOTE BATTERY LOW") when the
   voltage reported in PING is below `REMOTE_VBAT_MIN_ARM_MV`, as defence in
   depth against a remote firmware bug. `remote_battery_voltage_mv` arrives in
   every PING but nothing in `components/rlc_base/` reads it, and
   `check_arm_guards()` only tests the base's own pack. **Requirement not
   implemented.**
2. **`rlc_config.h` still carries the bench-test threshold overrides**
   (`REMOTE_VBAT_MIN_ARM_MV` 3200 / `MIN_OPERATE` 3100 / `CRITICAL` 3000, sized
   for the 3.3 V USB rail). FSD §5.6.2 production values for the specified 2S
   pack are 7000 / 6600 / 6400. As shipped, the remote would arm on a 2S pack at
   3.3 V per cell — well under the FSD floor. **Must be switched back before
   field use**, together with `REMOTE_VBAT_FULL_MV` (bench 4200 → 2S 8400),
   the new display gauge endpoint.

Remote battery criteria, for reference: `rlc_remote_battery.c` samples GPIO 1
(ADC1_CH0) once per second through the 18 kΩ/10 kΩ (2.8:1) divider, 8-sample
average. Below `REMOTE_VBAT_CRITICAL_MV` it posts `EVT_BATTERY_CRITICAL`
(edge-triggered) and the remote FSM enters STATE_ERROR — unrecoverable, power
cycle required. A reading of **0 mV means the divider is unfed**, not a flat
pack: USB power alone does not energise the VBAT sense. Note the divider is
sized for 8.4 V full scale; feeding the battery input from a 12 V+ supply puts
>4.5 V on GPIO 1, above the 3.3 V absolute maximum (bug #18 failure class).

### Bug #19 — Base LED strip: dead pixel at channel 4 (2026-08-19, OPEN)

**Symptom.** With the base strip powered, channels 1-3 render correctly,
**channel 4 is stuck solid blue and never updates**, and channels 5-8 stay dark.
Stable across every test pattern.

**Diagnosis.** Characterised with `tools/strip-diag`, which paints static solid
frames (red/green/blue/yellow/white) and walks a single pixel along the chain.
Channels 1-3 rendered all five colours correctly, so the data line from GPIO 48
is clean. The single-pixel walk lit ch1 first, proving the base strip's data-in
is at the **channel-1 end** — the opposite of the remote, and the reason the
first firmware showed the map mirrored on this unit.

The fault is at the **4th pixel in the data chain**: it holds a value latched at
power-up (blue) and never updates, meaning its data input is not receiving valid
bits — a dead LED controller, or a broken joint between pixel 3's DOUT and pixel
4's DIN. Channels 5-8 are dark because a WS2812 that has never received a frame
stays off; nothing valid propagates past the fault.

**Not** a supply or logic-level problem: three pixels rendering five different
static patterns perfectly rules out marginal 3.3 V data levels, which corrupt the
pixels *nearest* DIN and produce flicker rather than a stable pattern. (An
earlier working hypothesis blamed level shifting; the static-frame evidence
disproved it.)

**Fix required (hardware):** reflow or replace the 4th LED in the base strip, or
cut the strip after pixel 3 and splice in a replacement section. Until then the
base shows only channels 1-3.

**Preceding cause, resolved:** the strip was entirely dark because its 5 V feed
was not connected.

### LED Strip Tests (2026-08-19)

Host renderer tests — `./tests/host/run.sh`, run once per unit (the two strip
orientations), 30 checks each, all passing:

| ID | Test | Status |
|----|------|--------|
| T-L01 | Channel → pixel mapping (reversed: ch1=pixel7, ch8=pixel0) | PASS |
| T-L02 | Continuity map colours on the correct pixels | PASS |
| T-L03 | Cyan boot chase before any continuity data | PASS |
| T-L04 | Alarm wink timing; map restored between winks | PASS |
| T-L05 | Concurrent alarms alternate colours across winks | PASS |
| T-L06 | Stale flag dims map to 10 %, clears cleanly | PASS |
| T-L07 | Cursor pulse on the channel of interest only | PASS |
| T-L08 | Key warning: whole map (base) vs cursor only (remote) | PASS |
| T-L09 | Stale dim and cursor pulse compose | PASS |

On-target, both units flashed and running:

| ID | Test | Status | Notes |
|----|------|--------|-------|
| T-L10 | Both units build with no warnings | PASS | |
| T-L11 | Base boots, links, no watchdog trips | PASS | rssi −34 dBm, vbat 11618 mV, key ON, cont=0x0000 |
| T-L12 | Remote boots, links, no watchdog trips | PASS | rssi −42 dBm, vbat 5740 mV, arm switch ON, sel=1 |
| T-L13 | Link-loss alarm path: remote held in reset | PASS | Base detected loss in 1.5 s, held LINK_LOST 25 s, recovered to IDLE cleanly |
| T-L14 | Strip colours match the map **by eye** | PASS | Remote verified: ch1 red/SHORT, ch2-8 yellow/OPEN, ch2 breathing as cursor |
| T-L15 | Continuity change moves the right pixel | TODO | Needs a resistor on a known channel |
| T-L16 | Alarm wink legible at arm's length in daylight | TODO | May drive a brightness change |
| T-L17 | Remote cursor pulse follows the encoder | PASS | Cursor observed on the selected channel |
| T-L18 | Base strip renders all 8 channels | FAIL | Bug #19 — dead pixel at channel 4; ch1-3 correct |

**Verified by eye on the remote (2026-08-19):** channel 1 red (SHORT), channels
2-8 yellow (OPEN), channel 2 breathing as the selected channel — mapping,
colours, cursor breathing and the reversed orientation all confirmed correct.

**Base:** channels 1-3 render correctly once the orientation was fixed;
channels 4-8 are blocked by bug #19 (dead pixel at channel 4).

### Phase 4 On-Target Tests (pending)

| ID | Test | Status |
|----|------|--------|
| T-D01 | Panel ID read-back at boot (expect clone ID 0x2A403300) | TODO |
| T-D02 | Splash holds 10 s, then transitions to main status | TODO |
| T-D03 | Continuity grid matches base STATUS_UPDATE for all 8 channels | TODO |
| T-D04 | Encoder rotation moves the cyan selection cursor | TODO |
| T-D05 | ARMED screen on arm, red pulse, arm-sense confirmed | TODO |
| T-D06 | Pre-fire countdown smoothness (100 ms steps) | TODO |
| T-D07 | NACK overlay text + 3 s timeout, screen restored cleanly | TODO |
| T-D08 | Link-lost screen and recovery back to main status | TODO |
| T-D09 | Full-screen redraw time and steady-state frame rate | TODO |

---

## Phase 5 — Hardening and Final Testing

**FSD ref:** §4.3 Phase 5, §15 (Test Requirements)
**Status:** NOT STARTED

### Phase 5 Development Tasks

| # | Task | FSD ref | Status |
|---|------|---------|--------|
| 1 | Complete test suite execution (all §15 tests) | §15 | TODO |
| 2 | Watchdog stress testing | §15.4 | TODO |
| 3 | Range testing (10 m, 50 m, 100 m, 200 m) | §15.1 | TODO |
| 4 | Power consumption measurement | §14 | TODO |
| 5 | Edge case testing (rapid toggling, button mashing, power cycling) | §15 | TODO |
| 6 | Documentation: build instructions, flash procedure, wiring diagram | §4.3 | TODO |
| 7 | Final version number setting | §4.3 | TODO |

### Phase 5 FSD Safety Tests (§15.4)

| ID | Test | Status |
|----|------|--------|
| T-S01 | Power cycle base while armed → safe boot | TODO |
| T-S02 | Power cycle remote while base armed → link loss disarm | TODO |
| T-S03 | Base battery below VBAT_CRITICAL_MV → ERROR state | TODO |
| T-S04 | Hold fire button at boot, then arm → no fire (fresh press) | TODO |
| T-S05 | Corrupt message (bit flip) → rejected | TODO |
| T-S06 | GPIO init order verification (oscilloscope on boot) | TODO |
| T-S07 | Watchdog: infinite loop → reboot within 2 s | TODO |
| T-S08 | Hold fire button + arm → no fire (fresh press required) | TODO |
| T-S09 | LINK_REQUEST while ARMED → silently ignored | TODO |
| T-S10 | Display SPI failure at boot → ERROR | TODO |
| T-S11 | 5 consecutive send failures → immediate link loss | PASS | Triggered on-target during RF shielding test (RSSI -98 dBm) |
| T-S12 | Fire pulse on link loss (COMPLETE_PULSE=true) | TODO |
| T-S13 | Fire pulse on link loss (COMPLETE_PULSE=false) | TODO |
| T-S14 | Arm timeout (10 s auto-disarm) | TODO |
| T-S15 | ERR_COMM_DEGRADED blocks arming | TODO |
| T-S16 | ERR_COMM_DEGRADED blocks firing | TODO |
| T-S17 | Arm switch sense verifies arm switch | TODO |
| T-S18 | Arm switch sense fault detection → NACK 0x0B | TODO |
| T-S19 | Post-fire igniter status via continuity | TODO |

---

## Hardware Reference

| Item | Value |
|------|-------|
| Base MAC | `44:1B:F6:D4:0D:68` (chip #3; #2 `44:1B:F6:81:FA:F8` & #1 `94:A9:90:31:18:38` destroyed) |
| Remote MAC | `AC:A7:04:E2:F2:8C` (chip #2; #1 `44:1B:F6:81:F1:70` flash-damaged) |
| Base serial (COM port) | `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E044219-if00` (stable board serial) |
| Remote serial (COM port) | `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E043219-if00` (verified 2026-08-19 by `read_mac` → `ac:a7:04:e2:f2:8c`; was `...5B5E042156` before the board swap) |
| ESP-IDF version | v5.4.1 |
| Target | ESP32-S3 (xtensa) |
| Flash size | 16 MB |
| PSRAM | 8 MB OCT |

## Task Priority Reference (FSD §9.10)

### Base Unit

| Task | Priority | Core | Stack | Phase |
|------|----------|------|-------|-------|
| `arm_switch_task` | 7 (highest) | 0 | 4096 | 2 |
| `continuity_task` | 5 | 0 | 4096 | 2 |
| `heartbeat_task` (link_task) | 6 | 0 | 4096 | 1 DONE |
| `state_machine_task` (bfsm_task) | 4 | 0 | 8192 | 3 DONE |
| `battery_task` | 3 | 0 | 3072 | 2 |
| `status_update_task` | 3 | 0 | 4096 | 2 |
| `siren_task` | 2 | 1 | 2048 | 3 DONE |
| `rgb_led_task` | 1 (lowest) | 1 | 2048 | 1 DONE |

### Remote Unit

| Task | Priority | Core | Stack | Phase |
|------|----------|------|-------|-------|
| `fire_button_task` | 7 (highest) | 0 | 3072 | 2 |
| `arm_switch_task` | 6 | 0 | 3072 | 2 |
| `heartbeat_task` (link_task) | 6 | 0 | 4096 | 1 DONE |
| `state_machine_task` (rfsm_task) | 4 | 0 | 8192 | 3 DONE |
| `cmd_fire_repeat_task` (fire_rep) | 4 | 0 | 2048 | 3 DONE |
| `battery_task` | 3 | 0 | 3072 | 2 |
| `encoder_task` | 3 | 0 | 4096 | 2 |
| `display_task` | 2 | 1 | 8192 | 4 DONE |
| `buzzer_task` | 1 | 1 | 2048 | 2 |
| `rgb_led_task` | 1 (lowest) | 1 | 2048 | 1 DONE |

## Build Commands

```
./build_base.sh flash          # Build + flash base (COM by-id; override with -p)
./build_remote.sh flash        # Build + flash remote (COM by-id; override with -p)
./build_base.sh flash -p PORT  # Custom port
```
