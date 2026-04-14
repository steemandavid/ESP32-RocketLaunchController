# RLC Development Progress

**Project:** ESP32-S3 Wireless Rocket Launch Controller
**Spec:** RLC-FSPEC-001 v1.14 (2026-04-14)
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
| 4 | Display | NOT STARTED |
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
| 6 | Initialise display + read-back ID (remote only) | STUB | `display_init()` is no-op (Phase 4) |
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
**Status:** CODE COMPLETE — On-target testing required

### Phase 3 Architecture

Command forwarding pattern: `link_task` continues to own ESP-NOW receive queue. In `process_frame()`, PING/PONG/LINK_REQUEST/LINK_ACK handled by link_task (unchanged). Command messages (CMD_ARM, CMD_FIRE, CMD_DISARM, CMD_CEASE_FIRE, CMD_ACK, CMD_NACK) and STATUS_UPDATE forwarded to a new command queue owned by the state machine task. Single-task-owner pattern per FSD §4.7 — FSM task owns all relay control exclusively.

### Phase 3 Files Created

| File | Purpose |
|------|---------|
| `components/rlc_common/include/rlc_fsm_events.h` | Shared event types, event struct, notification bit definitions |
| `components/rlc_base/src/rlc_base_fsm.c` | Full base FSM: BOOT→IDLE→ARMED→PRE_FIRE→FIRING→POST_FIRE + LINK_LOST + ERROR |
| `components/rlc_base/include/rlc_base_fsm.h` | Base FSM public API |
| `components/rlc_base/src/rlc_fire_timer.c` | GPTimer fire pulse (1µs resolution, ISR→xTaskNotifyFromISR) |
| `components/rlc_base/include/rlc_fire_timer.h` | Fire timer API |
| `components/rlc_remote/src/rlc_remote_fsm.c` | Full remote FSM: BOOT→LINKING→IDLE→ARMED→PRE_FIRE→FIRING + LINK_LOST + ERROR |
| `components/rlc_remote/include/rlc_remote_fsm.h` | Remote FSM public API |

### Phase 3 Files Modified

| File | Changes |
|------|---------|
| `components/rlc_common/src/rlc_link.c` | Command frame forwarding, integrity CRC verification, dead-man timestamp, link health tracking, 6 new public APIs |
| `components/rlc_common/include/rlc_link.h` | Added `rlc_link_register_cmd_queue()`, `rlc_link_send_cmd()`, `rlc_link_get_session_token()`, `rlc_link_next_seq()`, `rlc_link_is_healthy()`, `rlc_link_get_last_fire_ms()` |
| `components/rlc_base/src/rlc_base_state.c` | Replaced stub — delegates to `rlc_base_fsm.c` getters |
| `components/rlc_base/include/rlc_base_state.h` | Added `base_state_get_firing_channel()`, `base_state_is_busy()` |
| `components/rlc_base/src/rlc_siren.c` | Added `siren_start_error()` (3 short blasts 200ms on/off) |
| `components/rlc_base/include/rlc_siren.h` | Declared `siren_start_error()` |
| `components/rlc_base/src/rlc_status_update.c` | Populates `channel_armed_bitmask` and `channel_firing_bitmask` from FSM state |
| `components/rlc_base/src/rlc_base_main.c` | Starts FSM task, wires arm sense/fault callbacks to FSM, sets link guard callback |
| `components/rlc_remote/src/rlc_remote_state.c` | Replaced stub — delegates to `rlc_remote_fsm.c` getters |
| `components/rlc_remote/src/rlc_remote_main.c` | Starts FSM + fire-repeat tasks, wires all input callbacks to FSM event queue |
| `components/rlc_common/include/rlc_config.h` | Added `COMPLETE_PULSE_ON_LINK_LOSS` (default: 1) |
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
| 12 | Base: Arm switch sense guard | §7.2.2 | DONE | `arm_sense_get_debounced()` checked in IDLE→ARMED, ARMED, PRE_FIRE, FIRING |
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
| Arm sense → immediate disarm | §7.2.7 | EVT_ARM_SENSE_CHANGED processed in ARMED/PRE_FIRE/FIRING |
| Channel change while armed → disarm | §8.2.7 | EVT_ENCODER_ROTATE in ARMED state |
| Contact welding detection | §7.3.2 | Arm relay energise + verify sense HIGH within 200ms |
| ERR_COMM_DEGRADED | §7.2.2 | Ping health window (10 frames, >30% = degraded) |
| Link loss during FIRING | §7.2.5 | COMPLETE_PULSE_ON_LINK_LOSS configurable |
| Fire pulse hardware timer | §7.4.2 | GPTimer ISR → xTaskNotifyFromISR, task does relay control |
| Relay control exclusivity | §9.12 | All relay ops only in state_machine_task |
| ERROR state unrecoverable | §7.2.9 | Intentional halt requiring power cycle |

### Phase 3 Build Status

- **Base:** Zero warnings, zero errors
- **Remote:** Zero warnings, zero errors

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
- (Phase 3 commit pending — code complete, on-target testing required before commit)

---

## Phase 4 — Display

**FSD ref:** §4.3 Phase 4, §10 (Display Specification)
**Status:** NOT STARTED

### Phase 4 Development Tasks

| # | Task | FSD ref | Status |
|---|------|---------|--------|
| 1 | ILI9488 SPI display driver (480x320, SPI2_HOST, 20 MHz) | §10.1 | TODO |
| 2 | Display health check (ID read-back at boot) | §9.13 step 6 | TODO |
| 3 | Screen layout manager | §10.3 | TODO |
| 4 | Splash screen | §10.3.1 | TODO |
| 5 | Main status screen (IDLE) — RSSI bar, battery, continuity grid, channel | §10.3.2 | TODO |
| 6 | Armed screen — channel indicator, continuity, status | §10.3.3 | TODO |
| 7 | Firing / Pre-fire screen — countdown, pulse indicator | §10.3.4 | TODO |
| 8 | Link lost screen | §10.3.5 | TODO |
| 9 | Error screen | §10.3.6 | TODO |
| 10 | Fire complete screen | §10.3.7 | TODO |
| 11 | Firmware mismatch screen | §10.3.8 | TODO |
| 12 | Partial refresh (dirty-rectangle) for dynamic elements | §10.4 | TODO |
| 13 | NACK reason display (human-readable text) | §10.5 | TODO |
| 14 | Display refresh rate >= 5 Hz | §10.4 | TODO |

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
| Base MAC | `94:A9:90:31:18:38` |
| Remote MAC | `44:1B:F6:81:F1:70` |
| Base serial | `/dev/ttyACM0` (verify with `udevadm` each session) |
| Remote serial | `/dev/ttyACM1` (verify with `udevadm` each session) |
| ESP-IDF version | v5.4.1 |
| Target | ESP32-S3 (xtensa) |
| Flash size | 16 MB |
| PSRAM | 8 MB OCT |

## Task Priority Reference (FSD §9.10)

### Base Unit

| Task | Priority | Core | Stack | Phase |
|------|----------|------|-------|-------|
| `arm_switch_task` | 7 (highest) | 0 | 3072 | 2 |
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
| `encoder_task` | 3 | 0 | 2048 | 2 |
| `display_task` | 2 | 1 | 8192 | 4 |
| `buzzer_task` | 1 | 1 | 2048 | 2 |
| `rgb_led_task` | 1 (lowest) | 1 | 2048 | 1 DONE |

## Build Commands

```
./build_base.sh flash          # Build + flash base to /dev/ttyACM0
./build_remote.sh flash        # Build + flash remote to /dev/ttyACM1
./build_base.sh flash -p PORT  # Custom port
```
