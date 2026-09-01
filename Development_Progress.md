# RLC Development Progress

**Project:** ESP32-S3 Wireless Rocket Launch Controller
**Spec:** RLC-FSPEC-001 v1.44 (2026-08-27)
**Firmware:** 1.1.9
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
| 3 | State Machines and Command Processing | COMPLETE — dev + FSD tests; residual test items (T-F04/T-F05) tracked under Phase 5 task 1 |
| 4 | Display | COMPLETE — dev + FSD tests; residual test items (T-L15/T-L16) tracked under Phase 5 task 1 |
| 5 | Hardening and Final Testing | **COMPLETE (release fw 1.2.0, 2026-08-28)** — safety tests §15.4 mostly PASS, review fixes applied and verified **on target** (MAJ-01, CRIT-01 both halves, bug #29 regression suite T-A16/T-A17/T-A18 all PASS); final docs sweep done, final-build audit clean (zero injection/harness symbols in both stock ELFs), both units on stock 1.2.0 and linked (fw **1.2.3** since 2026-09-01 — 1.2.1 added the remote fault-injection splash banner; 1.2.2 removed the main-screen continuity legend and enlarged the status band; 1.2.3 added the base's one-chirp boot-complete siren test; both flashed together, link verified). **Deferred past release** (tracked here, not blocking live fire): T-S10b, T-S12/S13, T-S18 (physical access), T-C06 replay tool, range 10–100 m, power consumption, remote FSM host harness, CI runner |

---

## Open Bugs

Blocking items, newest first. Each has a detailed entry further down this
document; search for its heading. Numbering is continuous with the earlier
on-target defect log in the Phase 3 section.

| # | Title | Class | Status | Blocks |
|---|-------|-------|--------|--------|
| 31 | Fire GPTimer never stopped on successful pulse completion — the second launch of a power cycle panicked with the igniter energised | Firmware | **RESOLVED 2026-08-27 (fw 1.1.9)** — `fire_timer_stop()` on the completion path, an unconditional stop at the top of `fire_timer_start()`, and a checked `gptimer_start()` return that makes the fire path safe and latches ERROR instead of `abort()`. Regression-tested by `tests/host/test_base_fsm.c` T-FSM05 (two full fire cycles per power-on). | Was: **NO-GO for a second live launch per power cycle.** Now clear. |
| 30 | Continuity-loss disarm is edge-triggered only — a band change to OPEN during the 200 ms arm-verify window is dropped and never re-delivered | Firmware | **RESOLVED 2026-08-26 (fw 1.1.8)** — two fixes: a continuity re-check at arm-verify completion (aborts with `NACK_NO_CONTINUITY`), and a periodic **level** check in `check_timers()` for ARMED/PRE_FIRE which also covers an event dropped by a full FSM queue (an entry check alone does not). | Nothing. **Verification is partial:** T-F02 confirmed the entry check does not false-positive (a normal arm completes through the verify path it sits on) and the backstop does not fire spuriously in IDLE. Neither has been *positively* triggered — that needs a disconnection inside a 200 ms window or a full FSM queue, both of which want an injection to reach reliably. |
| 29 | Base stays ARMED when the armed channel's igniter loses continuity | Firmware + spec | **RESOLVED 2026-08-26 (fw 1.1.2)** — continuity OPEN on the armed channel now disarms from ARMED or PRE_FIRE. Was a *specification* defect as much as a code one: v1.8 removed the disarm on a rationale ("sensing disabled — stale data") that the v1.10 SPDT redesign made obsolete, while the Phase 3 test criteria kept listing it as required. | Nothing now. **Regression retest 2026-08-28 COMPLETE: T-A16 PASS (10 ms), T-A17 PASS after the fw 1.1.34 toast fix** (the retest itself found a display-layer defect, now fixed), **T-A18 PASS** (68 Ω on ch2 pulled while ch1 armed; base ran its full 10 s ARM TIMEOUT). Cleared for live fire. |
| 28 | Base ARM RELAY LED lights when the key is turned to **SAFE**, while the relay itself stays de-energised | Hardware | **RESOLVED 2026-08-26** — indicator wiring corrected on the base. The ARM RELAY LED now lights only when the arm relay is actually energised. A second, related indicator fault was found and fixed in the same session: the arm-key red and green LEDs lit *simultaneously* with the key in SAFE; they now read red = ARMED, green = SAFE. | Nothing. **Fire testing is unblocked** — this was the last hardware gate. |
| 27 | Base siren not connected — GPIO 40 drives nothing, IRLZ44N driver not fitted | Hardware | **RESOLVED (hardware) 2026-08-26** — IRLZ44N driver fitted on GPIO 40 with its 150 Ω gate series resistor, 10 kΩ gate pull-down, and a 1N5819 flyback diode across the siren (cathode VBAT+, anode drain). | Nothing further in hardware. **Retests DONE 2026-08-26 — six checks, all PASS, review finding N2 closed by measurement:** silent at power-on; continuous across ARMED→PRE_FIRE; stops and stays stopped after all three disarm routes; LINK_LOST = 4 cycles of 500/500; ERROR = 3 blasts at 200 ms; and link recovery mid-pattern silences it immediately and permanently. (This row said "retest still owed" until 2026-08-27 — it was never updated when the tests ran, and the README and changelog were correct.) |
| 26 | Continuity misclassified — ~64 Ω return-path fault + ADC calibration disabled + 12 dB attenuation | Hardware + firmware | **RESOLVED** — return repaired, calibration on, 0 dB adopted, SHORT band merged away as unmeasurable | All continuity sensing. Operators are told a good igniter is a wiring fault, and OPEN (the only band that blocks arming) cannot be trusted. |
| 25 | No hardware undervoltage cut-off on either battery — firmware thresholds only, and ERROR does not disconnect the load | Hardware | OPEN | Pack protection. A unit left switched on, or one whose firmware has halted, will discharge a LiPo past the point of permanent damage and into the unsafe-to-recharge region. |
| 24 | Base 3.3 V rail runs high — killed chip #3 at 3.68 V, and **measured ~3.72 V again on 2026-08-21 with chip #4 fitted** | Hardware | **RECURRING — chip #4 at risk now.** High readings now attributed to a grounding measurement artefact (DevKit reads 3.35 V against its own GND); rail clamp spec'd as a TL431 shunt at ~3.57 V, **not** the 3V6 zener originally recommended | Nothing functionally. The base has no rail clamp and no secured ground path, so a repeat ground-lift kills chip #4 the same way. The eight 3V3-side continuity clamps are now a second route to an over-rail (bug #18). |
| 23 | Remote VBAT divider has no ADC headroom — a full 2S pack sits at 97 % of the ADC's usable ceiling | Hardware | OPEN | Accuracy only. Costs ~0.7 % at full charge; thresholds are unaffected as they sit at 71-78 % of range. |
| 22 | Remote GPIO 1 has no overvoltage clamp — the bug #21 zener was removed and not replaced | Hardware | OPEN | Protection only. The divider's series impedance is the sole limit on an overvoltage fault. |
| 21 | Remote VBAT sense was non-linear — 3.3 V zener leakage into a 6.4 kΩ divider | Hardware | **Zener removed 2026-08-19, sense verified and calibrated. PARTIAL — no replacement clamp fitted, so GPIO 1 is unprotected.** | Nothing functionally. Production thresholds restored. Remaining risk is overvoltage exposure on GPIO 1 until a BAT54-class clamp is fitted. |
| 20 | Shipped crypto keys are public — AES-128-CCM and the keyed CRC32 check are ineffective against anyone who has read the source | Security | OPEN — rotation deferred by decision | Field use where an adversary is in the threat model. No effect on bench work. |
| 19 | Base LED strip: data chain breaks after pixel 3 — pixels 4-8 dark | Hardware | **RESOLVED 2026-08-26** — strip replaced. Root cause was **pixel 3's output stage**: the LED rendered its own colour correctly but no longer passed data downstream. All 8 pixels now respond. | Nothing. T-L15 and T-L18 unblocked. |
| 18 | Base ESP32 destroyed by relay-arc coupling on the continuity ADC inputs | Hardware + firmware | Software fix DONE and audited. **As-built 2026-08-23: snubbers on ALL 8 channel relays + arm relay; 1N5819 clamps to GND and 3V3 on ALL 8 sense pins; 217 Ω sense-branch resistors on ALL 8 channels (thresholds recalibrated, verified on target).** Only the 3.3 V rail clamp (TL431 at ~3.57 V) is still missing, and with the 217 Ω fitted it is now belt-and-braces rather than the primary defence | Nothing further — `FIRE_PROTECTED_CHANNEL_MASK` widened to 0xFF on 2026-08-23. Only the TL431 rail clamp is still outstanding, now covering the multi-channel fault case rather than the single-channel one |

Non-blocking items tracked elsewhere: the unimplemented FSD §7 remote-battery
arming guard / NACK `0x0C` (in "Phase 4 Findings — Battery Thresholds"), and the
FSD §10.2.0 continuity palette deviation.

**Full-codebase review 2026-08-28** (`Code_Review_Phase5_20260828_0641.md`,
RLC-REVIEW-ALL-009, commit `a101077`, fw 1.1.29): verdict **MAYBE** —
conditional GO. Focus: the arm-fire sequence, error handling, and
toast/status screens. **Every ALL-008 finding was re-verified fixed**
(BF-01's three layers, CM-01, DS-01, TT-04's harness, CM-02/04/05, RM-05/06,
BF-02/03), and the fw 1.1.29 asymmetric debounce traced correct through
every layer. Nothing new extends a pulse or energizes a relay; the 1
Critical + 6 Majors are all in the operator-information layer:

- **CRIT-01.** `buzzer_set_background()`'s `BUZZER_OFF` nudge is an
  `xQueueOverwrite` on the depth-1 mailbox and atomically deletes any alarm
  queued in the same FSM tick — the link-lost and critical-error alarms are
  **completely silent when the transition originates in ARMED/PRE_FIRE/FIRING**,
  as are all FIRE-guard refusal beeps and the FIRING "PULSE CUT SHORT"
  triples. §7.2.9a's audible half fails on the highest-hazard paths.
- **MAJ-01.** Remote FIRING syncs only on base POST_FIRE/IDLE (whitelist;
  PRE_FIRE's is a blacklist) — a base entering ERROR mid-pulse (weld fault)
  leaves the remote showing IGNITION ACTIVE + firing tone indefinitely.
- **MAJ-02.** False FIRE COMPLETE: base aborts during PRE_FIRE + one lost
  STATUS_UPDATE ⇒ the local-elapsed backstop (which assumes local FIRING
  entry ⇒ base energized) shows the 10 s green screen for a never-fired
  channel.
- **MAJ-03.** The base NACKs stray CMD_FIRE repeats ~200 ms after leaving the
  firing path (WRONG_STATE / BASE_ERROR) but the remote discards
  `EVT_CMD_NACK` outside `wait_for_ack()` — abort detection waits for the
  2 s status cadence. Root amplifier of MAJ-01/02.
- **MAJ-04/05.** Display precedence: the 10 s splash hold outranks the
  ARMED/FIRING screens; FIRE COMPLETE outranks LINK_LOST during its hold.
- **MAJ-06.** Three refusal paths have a toast but no buzzer at all
  (arm-guard-1 key-off, ARM −4, "BASE ENDED SEQUENCE"), independent of
  CRIT-01.

Plus 12 Minor (double-CMD_ARM in the verify window; arm-verify timeout
doesn't latch `ERR_RELAY_FAULT` though §7.2.2 says to — FSD
self-contradiction; FIRE −2 and arm-retry key-off misattributed to the base;
"LINK OK" bar while degraded; boot display-fault has no buzzer; …) and
12 Info. **Gate: fix CRIT-01 + MAJ-01/02/04 before the next live-fire
session** (all small, localized); bench work can continue now.

**Full-codebase review 2026-08-27** (`Code_Review_AllPhases_20260827_0308.md`,
commit d04d07b): verdict **FAIL**, on one CRITICAL finding. **All findings —
Critical, Major and Minor — were fixed on 2026-08-27 in firmware 1.1.9 and
FSD v1.44.** Highlights:

- **BF-01 (CRITICAL, bug #31 below).** The fire GPTimer was never stopped on
  the *successful* pulse-completion path, so the second launch of any power
  cycle panicked with the igniter energised. Fixed, and now regression-tested
  by an automated two-cycle test.
- **DS-01 (MAJOR).** FSD §5.5.6's runtime display health check did not exist —
  a panel that died mid-session froze the last frame (possibly an ARMED screen
  reading "CONTINUITY CONNECTED") while the FSM kept accepting fire commands.
  Implemented: 5 s panel-ID re-read inside `display_task`, SPI return codes
  counted, and a display fault while armed disarms and latches ERROR.
- **CM-01 (MAJOR).** `rlc_link_send_status_update()` mutated link state from
  `status_update_task` with no lock, against `link_task`. Duplicate sequence
  numbers were reachable, which the peer rejects as replay.
- **TT-04 (MAJOR).** Neither safety FSM had a single automated test. There is
  now a host event-injection harness for the base FSM
  (`tests/host/test_base_fsm.c`, 111 checks), and `build_base.sh` /
  `build_remote.sh` run the whole host suite before every build and refuse to
  build on failure. Suite grew from 12 binaries / 265 checks to 16 / 418.
- **TT-01/TT-02.** Both broken bench tools repaired: `test_tr04.py`'s ports
  were stale *and crossed* (it would have halted the remote and talked to the
  base as if it were the remote); `vbat_fit.py` could not parse any real
  `vbat-cal` log.

The earlier review below is retained for history.

**Full-codebase review 2026-08-21** (`Code_Review_AllPhases_20260821_1430.md`,
commit cd4ddf0): verdict MAYBE. All Phase 1–3 review fixes verified present
except Phase-2 M2 (self-test still runs a copy of the continuity classifier).
Seven Major findings; four gate live-fire testing until fixed: (1) DISARM during
the arm-verify window does not abort the pending ARM (`rlc_base_fsm.c`), (2)
remote can command FIRE with the arm key off (retry loop + fire guards miss
`arm_switch_is_armed()`, `rlc_remote_fsm.c`), (3) continuity ADC read failure
classifies as CONNECTED — fails permissive, should fail to OPEN
(`rlc_continuity.c`), (4) `ERR_VBAT_CRITICAL` latched during FIRING is dropped on
abort exits then misfires as a spurious terminal ERROR at the next POST_FIRE
(`rlc_base_fsm.c`). A parallel documentation audit found the remote hw-test spec
flashing the base board's by-id and the base hw-test spec wiring the LED to
GPIO 47 (arm relay) — both need fixing before those docs are followed on the
bench. (Both hw-test-spec defects were finally fixed in the v1.43 doc sweep;
all four Major findings were fixed at the time.)

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
| 24 | Battery ADC driver (ADC1, median-of-33 burst + 8-deep average, calibration) | §5.4.7 | DONE | `rlc_battery.c` |
| 25 | Battery 3-threshold check (OK/WARNING/LOW/CRITICAL) | §8.3.4 | DONE | `rlc_battery_check()` with `min_operate_mv` |
| 26 | Debounce engine (8-bit / 16-bit shift register) | §5.3 | DONE | `rlc_debounce.c/h` |
| 27 | Version header | §4.3 | DONE | `rlc_version.h` — now v1.1.0 (bumped 2026-08-20 with the arm-sense field change) |
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
| B2-C07 | CH2–CH8 individual load = CONNECTED | **PASS 2026-08-21.** All eight channels loaded simultaneously (0.1 Ω, 14.9 Ω, 74.3 Ω, 2k16, 4k28, and three igniters) and every band verified — see "Final on-target verification". |
| ~~B2-C08~~ | ~~SHORT classification (~0 Ω)~~ | **RETIRED 2026-08-21** — the SHORT band was merged into CONNECTED as unmeasurable at 1 mA. A 0.1 Ω load now correctly reads CONNECTED. |
| B2-C09 | MARGINAL classification | **PASS 2026-08-21** with a 74.3 Ω load (raw 282, ~70 mV) against the 66 mV boundary. Note this sits only ~4 mV above the threshold — a load near this value is genuinely close to the line. |

#### Base Unit — Arm Sense

| ID | Test | Notes |
|----|------|-------|
| B2-A03 | Disconnected wire fail-safe | Disconnect arm sense GPIO — should report DISARMED |
| B2-A04 | Arm sense reaches the remote | **Rewritten 2026-08-20.** The old test verified `arm_switch_hw` matched the raw key GPIO; that field now carries the debounced **arm sense** (GPIO 21). Verify `base_arm_sense` follows the arm relay and that the remote shows BASE ARMED only while it is HIGH. |

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
**Status:** G2 ARMING SUITE COMPLETE 2026-08-26 — **16 PASS, 0 FAIL, 2 N/A** (T-A05 unreachable by design, T-A15 band merged away). T-A11 and T-A13 closed with the new `CONFIG_RLC_FAULT_INJECTION` harness. See `Test_Report_Phase3_G2.md`. Earlier status below is historical. ON-TARGET TESTING RESUMING (channel 1 only) — G1 partial (T-R04 PASS, T-R05 SKIP/code-reviewed, T-R06 pending). Blocker resolved 2026-07-21: base ESP32 chip #3 installed (MAC `44:1B:F6:D4:0D:68`), hardware protection fitted on **channel 1** (clamping diodes on the ADC input + snubber across the relay contact; channels 2–8 still unprotected — test channel 1 ONLY), software relay-order fix already in place. Both units reflashed; G0 re-verified with chip #3 (LINK_ACK, rssi=-35). Next: G2 arming (T-A01..T-A18) then G3 fire (T-F01..T-F09), now on any channel — see the 2026-08-26 entries.

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
| `components/rlc_base/src/rlc_status_update.c` | Populates `channel_armed_bitmask` and `channel_firing_bitmask` from FSM state, `base_key_switch` from `key_sense_get_debounced()` (GPIO 42) and `base_arm_sense` from `arm_sense_get_debounced()` (GPIO 21) |
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
| 8 | Base: Siren patterns (**continuous ARMED/PRE_FIRE/FIRING** since fw 1.1.2) | §7.4.1 | DONE | `siren_start_continuous()`, `siren_start_error()`, `siren_start_link_lost()`. `siren_start_pulse()` removed 2026-08-26 — see bug #29 entry |
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
- **New: `FIRE_PROTECTED_CHANNEL_MASK`** in `rlc_config.h` (`0x01`, channel 1
  only, when this was written — widened to `0xFF` on 2026-08-23). `guard_arm()` NACKs ARM on any channel outside the mask
  (reusing `NACK_INVALID_CHANNEL`, so the wire protocol is unchanged; the real
  reason is logged on the base), and `relay_fire_set()` refuses to energise an
  unprotected channel relay as a last line of defence. De-energising is always
  allowed. `relay_init()` logs a warning each boot while the mask != `0xFF`.
  **Done 2026-08-23** — clamps, snubbers and 217 Ω sense resistors are fitted on
  all eight channels and the mask is now `0xFF`.
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
idf.py -B build_base -p /dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E042156-if00 monitor   # base COM
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
1. All 18 FSD §15.2 arming tests pass (T-A01..T-A18 — T-A16/17/18 added 2026-08-26 with bug #29)
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
| **G2 — FSD §15.2 Arming** | T-A01..T-A18 | Spec conformance for arm path | Yes |
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
| T-A01 | ARM with both switches armed, continuity GOOD | **PASS** 2026-08-26 (siren continuous, M1 verify path 170 ms) |
| T-A02 | ARM with base switch disarmed → NACK 0x01 | **PASS** 2026-08-26 |
| T-A03 | ARM with remote switch disarmed → local block | **PASS** 2026-08-26 (no ARM traffic) |
| T-A04 | ARM channel with OPEN continuity → NACK 0x04 | **PASS** 2026-08-26 |
| T-A05 | ARM second channel while one armed → NACK 0x0A | **N/A** — unreachable via UI (T-A08 disarms on encoder rotate first). Guard stays as defence-in-depth; verify by host test. See FSD v1.37 |
| T-A06 | Base key OFF while armed → disarm | **PASS** 2026-08-26 (relay out at +10 ms, before the FSM — hardware leg) |
| T-A07 | Remote arm switch DISARM while armed | **PASS** 2026-08-26 (FSM first, relay +160 ms — software leg) |
| T-A08 | Rotate encoder while armed → disarm | **PASS** 2026-08-26 (channel advanced to CH2) |
| T-A09 | Continuity bands visible with arm switch OFF | **PASS** 2026-08-26 (band list corrected — no SHORT band) |
| T-A10 | ARM with arm relay sense fault → NACK 0x0B | **PASS** 2026-08-26 (200 ms verify timeout) |
| T-A11 | ARM with stale STATUS_UPDATE | **PASS** 2026-08-26 via `--inject` (`NO BASE STATUS DATA`, zero ARM frames) |
| T-A12 | ARM with low remote battery | **PASS** 2026-08-26 (remote at 6.8 V, no ARM traffic) |
| T-A13 | Verify channel in CMD_ACK | **PASS** 2026-08-26 via `--inject` (after the 1.1.5 fix added the missing toast) |
| T-A14 | ARM with MARGINAL continuity → succeeds (warning) | **PASS** 2026-08-26 (ch3, 269000 uV) |
| T-A15 | ARM with SHORT continuity → succeeds (informational) | **N/A** — SHORT band merged into CONNECTED 2026-08-21 (bug #26); not runnable, see FSD v1.37 |
| T-A16 | Disconnect the igniter on the armed channel while ARMED → disarm within ~1 s | **PASS** 2026-08-26 (920 ms detect, disarm +20 ms). **REGRESSION PASS 2026-08-28 (fw 1.1.33)** — pulling the halogen lead while ARMED: base disarmed **10 ms** after the band change; remote toast `CONTINUITY LOST - DISARMED` **110 ms** end-to-end with the `BEEP_CONTINUITY_LOST` pattern |
| T-A17 | Disconnect the igniter on the armed channel during the PRE_FIRE countdown → abort, no pulse | **PASS** 2026-08-26 (needed PRE_FIRE at 10 s; an igniter fired at 2 s). **REGRESSION PASS 2026-08-28 (fw 1.1.33/1.1.34)** — abort + no pulse in both race orderings; first retest found the raw-NACK toast defect (fixed in fw 1.1.34, retest PASS). NACK-wins latch path verified by inspection (remote FSM host harness does not exist yet) |
| T-A18 | Disconnect a **non-armed** channel's igniter while another is ARMED → base stays ARMED | **PASS** 2026-08-26 (3 transitions, stayed ARMED). **REGRESSION PASS 2026-08-28 (fw 1.1.35)** — 68 Ω resistor on ch2 (read MARGINAL, 267 mV), pulled ~1.5 s after arming ch1: ch2 MARG→OPEN, base stayed ARMED through the full `ARM TIMEOUT (10022 ms)`, remote no CONTINUITY LOST toast; at timeout the correct `BASE DISARMED` (RM-07 discrimination rightly silent for a non-armed channel) |
| T-A19 | ARM while base is in terminal ERROR → NACK 0x0E, remote names the fault | **PASS** 2026-08-26 via `--inject` key `e` (`BASE ERROR: VBAT CRITICAL`) |
| T-A20 | Every refusal reports audibly **and** on the display (§7.2.9a) | **PASS** 2026-08-26 (spot-checked: `BASE STATUS LOST`, `BASE DISARMED`, `BASE ERROR: <flag>`) |

### Phase 3 FSD Fire Tests (§15.3)

**Blocker resolved 2026-07-21** (chip #3 + clamping diodes + snubber on channel 1) — resume fire tests on **channel 1 only** until channels 2–8 receive the same protection. See bug #18. Channel-1-only was enforced in firmware by `FIRE_PROTECTED_CHANNEL_MASK` (2026-08-17), not just by operator discipline. **Superseded 2026-08-23:** protection complete on all eight channels, mask widened to 0xFF. Fire testing was then held by bug #28 until **2026-08-26**, when that bug was resolved as an indicator-wiring fault. **Fire testing is now unblocked on all eight channels** — channels 2-8 have still never been fired, so treat the first shot on each as a test.

| ID | Test | Status |
|----|------|--------|
| T-F01 | Full fire sequence (arm→fire→complete) | **PASS** 2026-08-27 — all 8 channels fired arm→fire→complete into a 12 V 50 W lamp, each arm sense-verified and each `Fire timer started` naming the selected channel. Nine pulses on one power cycle (ch 8 needed a second go after an early release at 470 ms), 0 reboots, 0 faults, uptime continuous 331584→582104 ms. That discharges two of the three acceptance criteria: relay energised for `FIRE_PULSE_DURATION_MS`, and auto-disarm returning it to NC. **Outstanding: the siren criterion** — "continuous from ARMED through PRE_FIRE and FIRING, with no gap at either transition". **T-F01 does not require an igniter**; its criteria are sequence mechanics, and the siren check is audible. **Siren criterion discharged 2026-08-27**: ARMED→PRE_FIRE was already measured gapless in the 2026-08-26 bench tests (which were pattern tests and never fired a channel), and the operator confirmed the siren unbroken through **PRE_FIRE→FIRING** on a ch 8 cycle — `4295064 ARMED->PRE_FIRE, 4300064 PRE_FIRE->FIRING, 4301134 FIRING->POST_FIRE`. Code agrees: `siren_start_continuous()` only ever calls `siren_drive(true)`, so re-asserting it is a GPIO no-op, and FIRING does not touch the siren at all. All three criteria now met |
| T-F02 | Release fire button during pre-fire delay | **PASS** 2026-08-26 (no pulse; also regression-tested the bug #30 fix and the v1.41 ring LED) |
| T-F03 | Release fire button during active fire → cease fire | **PASS** 2026-08-27 (fw 1.1.19, 12 V 50 W halogen on ch 1) — pulse cut at **540 ms** of 1000 ms, `FIRING -> IDLE (CEASE_FIRE)`, arm sense DISARMED 150 ms later. Note the exit is to **IDLE, not POST_FIRE**: the FSM distinguishes ceased from completed, so a cease-fire gets no cooldown and no fire-complete screen |
| T-F04 | Fire command on non-armed channel → NACK 0x05 | TODO |
| T-F05 | Continuity readable during ARMED (relay in NC) | TODO |
| T-F06 | Link lost during firing → complete pulse then disarm | TODO |
| T-F07 | Pre-fire timer expires without fire button → abort | TODO |
| T-F08 | Fire pulse timing accuracy (oscilloscope) | **PASS by log timing** 2026-08-27 — `Fire timer started` → `Fire timer stopped` = **1050 ms** against a 1000 ms `FIRE_PULSE_DURATION_MS`, consistent across all pulses measured. **Method stated deliberately:** the stop line is written *after* the callback runs, so this bounds the pulse at roughly 1000–1050 ms and cannot resolve better. No oscilloscope was used; the FSD asks for one, so this is corroboration at log resolution, not the scope-grade figure |
| T-F09 | Link-health guard at PRE_FIRE→FIRING | TODO |

### Phase 3 Key Commits

- `744240c` Phase 2 extended testing — fresh-press fix, stack increases, 9 tests verified
- `e03b826` Phase 3 on-target testing — ADC deadlock fix + 9 bug fixes + G0/G1 partial
- (Phase 3 final commit pending — G1 verification + G2/G3 testing required)

---

## Phase 4 — Display

**FSD ref:** §4.3 Phase 4, §10 (Display Specification)
**Status:** VERIFIED ON TARGET 2026-08-27 — **T-D01…T-D09 all pass.** T-D09
initially failed at 3.3 Hz against the §10.3 ≥5 Hz floor; the flush was made
incremental (pixel diffing against a shadow copy) and the frame loop given
fixed-period pacing, and it retested at 10.0 Hz with the pre-fire countdown
stepping at ~101 ms. See `Test_Report_Phase4_Display.md`. Version bump to 1.1.10
still outstanding.

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
| CONNECTED | dark green `#006400` | `RLC_COLOR_CONT_CONNECTED` |
| MARGINAL | light green `#90EE90` | `RLC_COLOR_CONT_MARGINAL` |
| OPEN | yellow `#FFFF00` | `RLC_COLOR_CONT_OPEN` |
| ~~SHORT~~ | ~~red `#FF0000`~~ | `RLC_COLOR_CONT_SHORT` — **deprecated 2026-08-21**, retained only so a value 3 from a pre-merge peer resolves to a colour |

The constants live in `rlc_config.h` and are the **single source of truth for
both units** — the remote display's channel grid resolves its colours from the
same macros, so pad, handheld strip and handheld screen always agree.

**Three bands since 2026-08-21** — SHORT was merged into CONNECTED, and GOOD
renamed CONNECTED, because the band was unmeasurable at the specified 1 mA test
current. See "Continuity Bands Reduced to Three" below.

**Deviation from FSD §10.2.0**, which specifies blue for GOOD, red for OPEN and
orange for SHORT, with blue chosen deliberately to avoid red-green ambiguity for
colour-blind operators. The requested palette pairs green (good) with red
(short) and moves red off OPEN. The display's shape coding (filled circle /
triangle / ring — the diamond retired with the SHORT band) still carries the
meaning without colour, and the
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
2. ~~**`rlc_config.h` still carries the bench-test threshold overrides**
   (`REMOTE_VBAT_MIN_ARM_MV` 3200 / `MIN_OPERATE` 3100 / `CRITICAL` 3000, sized
   for the 3.3 V USB rail).~~ **RESOLVED 2026-08-19.** The FSD §5.6.2 production
   values (7000 / 6600 / 6400, plus `REMOTE_VBAT_FULL_MV` 8400) were restored
   once the sense path was trustworthy — the divider had to be calibrated first,
   and doing so uncovered bug #21. Restoring them earlier would have been
   actively harmful: with the zener fitted, a fully charged pack read 5979 mV
   and would have locked the remote in STATE_ERROR at boot with no diagnostic
   pointing at the battery divider. See
   `docs/calibration/remote_vbat_2026-08-19.md`.

Remote battery criteria, for reference: `rlc_remote_battery.c` samples GPIO 1
(ADC1_CH0) once per second through the 18 kΩ/10 kΩ (2.8:1) divider, taking the
median of a 33-sample burst and then an 8-deep moving
average (see bug #23 on the divider's lack of ADC headroom).
Below `REMOTE_VBAT_CRITICAL_MV` it posts `EVT_BATTERY_CRITICAL`
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

**Superseded 2026-08-23** — the LED was replaced and the symptom did not change;
see "Bug #19 UPDATE" below. The remaining suspect is the data path into pixel 4
(copper or joints), not the LED.

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
| T-L18 | Base strip renders all 8 channels | **PASS** (2026-08-26) | Bug #19 resolved by replacing the strip — pixel 3's output stage was dead (it lit correctly but passed no data downstream). All 8 pixels respond |

**Verified by eye on the remote (2026-08-19):** channel 1 red (SHORT), channels
2-8 yellow (OPEN), channel 2 breathing as the selected channel — mapping,
colours, cursor breathing and the reversed orientation all confirmed correct.

**Base:** channels 1-3 render correctly once the orientation was fixed;
channels 4-8 are blocked by bug #19 (dead pixel at channel 4).

### System Status Band — firmware 1.1.12 to 1.1.17 (2026-08-27)

A coloured field across the bottom of **every** screen reporting the state of
the fire path, so it reads from across a launch site without the operator
parsing text. Requested as a full-screen border; built as a bottom band instead
because the channel grid fills the panel width exactly (`_Static_assert` in
`rlc_display.c`) and a border would have had to shrink the cells. The band
occupies the area that already held the status and instruction lines, so the
grid is untouched — and at ~34000 px it is over three times the area a 6 px
border would have been.

| Band | State | Verified |
|---|---|---|
| Green `SAFE` | base safe, remote arm switch off | on target |
| Yellow `BASE KEY ARMED` / `REMOTE ARMED` | one key turned | on target |
| Orange `READY TO ARM` | both keys — one long-press from a live relay | on target |
| Red `ARM RELAY LIVE` | arm relay engaged | on target |
| Flashing `RELAY WELDED` | contacts closed when they should not be | base `w` injection |
| Red `BASE FAULT` | base error_flags, or base_state == STATE_ERROR | base `e` injection |
| Red `REMOTE FAULT` | remote latched its own ERROR | remote `d` injection |
| Grey `STATUS UNKNOWN` | link down, stale, mismatch, or pre-first-report | on target |

Priority runs **WELD > RELAY LIVE > REMOTE FAULT > UNKNOWN > BASE FAULT > keys
> SAFE**. All seven non-nominal states verified on target.

**Grey, never green, when the state is not known** (§10.2.2's rule that unknown
is never displayed as SAFE). Green is a positive claim that the pad is safe to
approach.

**Faults are red** rather than something softer: a base that has faulted cannot
be trusted to have reported its *relay* state accurately either, so treating it
as possibly live is the honest reading, not an over-cautious one. The word
distinguishes it from `ARM RELAY LIVE`.

**One key and two keys are separate colours.** With one turned the hardware will
not act — the base refuses an ARM without its key, the remote will not send one
without its arm switch. With both turned a single long-press closes the arm
relay. That is the only transition in the sequence where the risk changes, and
collapsing it into a single amber hid it.

The band **logs one line per state transition**. It is a safety indicator whose
only check was previously to look at the panel, which is exactly how the
green-on-a-dead-link bug below survived; it is now verifiable from a capture.

#### Defects found and fixed along the way

- **Full-width background fills punched holes in every border.**
  `draw_text_centred_bg()` cleared `x=0, w=DW` unconditionally before writing,
  so every refresh of a live value notched the left and right edges of whatever
  frame the text sat inside — the LINK LOST amber border (reported by the
  operator) and the ARMED / FIRING / FIRE COMPLETE box outlines. The fill now
  takes explicit bounds and callers pass the interior of their enclosure.

- **Band showed green with the base switched off** (fw 1.1.15). Link loss is
  declared at 1500 ms but a STATUS_UPDATE is only stale after 4000 ms, so for
  2.5 s the band rendered the last state received before the power was cut —
  green, on a screen already announcing the link was gone. It now gates on link
  state as well as staleness: a dead link is itself proof the base state is
  unknown. Verified grey 10 ms after link loss.

- **False `RELAY WELDED` on every normal disarm** (fw 1.1.16). Measured at
  180 ms and 220 ms across two ordinary disarms: on ARMED → IDLE the base
  reports `base_state = IDLE` before `base_arm_sense` has fallen, which is
  exactly `rlc_base_arm_state()`'s weld condition. **Pre-existing** — the main
  screen's BASE field had been flashing `WELD!` the same way — but the band made
  it a full-width colour flash. An indicator that cries wolf twice a session
  teaches the operator to ignore it. A weld must now hold 500 ms before it is
  believed; during the window the state reports as ARMED, never anything safer,
  because the arm sense genuinely is high. The hysteresis is in the display, not
  in `rlc_base_arm_state()` — that function is pure, shared with the base, and
  compiled into the host tests (T-M01…T-M07).

- **Instruction line tested only the remote arm switch** (fw 1.1.14). With the
  remote armed and the base key still SAFE it read "HOLD ENCODER TO ARM CH n" —
  an instruction the base refuses. It now names the step actually outstanding
  for each of the four key combinations.

- **Two colours that were clearly separated in the source were identical on the
  panel** (fw 1.1.14). `C_WARN` (0xFFDC00, 87% green) against 0xFF6000 (38%)
  both read as orange. Pushed to 0xFFFF00 against 0xFF5000, and the distinction
  is now carried by wording as well as hue. Worth remembering: separation in
  the constants is not separation on this glass.

### Handshake Refusals Are No Longer Silent — firmware 1.1.17 (2026-08-27)

**Protocol change: new `MSG_LINK_REJECT` (0x03).** Both units must be flashed
together; the strict version check enforces it.

`handle_link_request()` refused a handshake with a bare `return` on two paths —
firmware mismatch, and the app-state guard when the base is armed or firing. The
remote cannot tell a refusal from a base that is switched off or out of range,
so it retried every 2 s forever behind a splash frozen at "Attempt 5 / 5" with
the progress bar at 100%: it reads as a hung boot, not a diagnosis. The base
knew exactly what was wrong and said so only on its own LED strip, at the pad.
Both paths contradicted the no-silent-refusals rule (§7.2.9a), applied to
commands in 1.1.6 but never to the handshake.

The base now answers with a reason code and its own version:

| Reason | Remote behaviour |
|---|---|
| `LINK_REJECT_VERSION_MISMATCH` | Terminal — latches `VERSION_MISMATCH`; the §10.2.1 mismatch screen finally renders, naming both versions |
| `LINK_REJECT_BUSY` | Not terminal — keeps retrying, but the splash says `Base busy - armed or firing` |

**The mismatch screen had been unreachable.** The remote has always had its own
check in `handle_link_ack()`, but it reads the version out of a LINK_ACK the
base never sent on a mismatch. The base-side check added in 5.7 to make
mismatches *clearer* is what pre-empted it. Verified on target: with the base at
1.1.90 and the remote at 1.1.17, the remote logged the mismatch 10 ms after its
first LINK_REQUEST and locked out — 0 further requests in 12 s, against 26+
blind retries before.

`LINK_REJECT_BUSY` deliberately does not latch: the base being armed is
transient, and locking the remote out until a power cycle would be worse than
the silence it replaces.

**Old remotes covered too (fw 1.1.18).** 1.1.17's LINK_REJECT only helped once
both units carried it: an older remote has no handler for message type 0x03 and
drops it at the dispatch switch's `default` case. Since a version mismatch means
one unit *is* on older firmware, that left the very case it was written for
uncovered. On a mismatch the base now also sends a **LINK_ACK carrying its
version, with the session token zeroed** — every version of `handle_link_ack()`
has checked the peer version before touching anything else, so an old remote
latches VERSION_MISMATCH from that. No `reset_session()`, no LINKED: the base's
lock-out is unchanged and no session is created. Both frames go out; whichever
lands first wins, and the dispatch guard on VERSION_MISMATCH drops the second.

Verified by building a **simulated pre-1.1.17 remote** — `MSG_LINK_REJECT` case
deleted from the dispatch, version forced to 1.1.91 — against a 1.1.18 base:

```
1907  LINK_REQUEST sent (attempt 1)
1917  FW MISMATCH: base 1.1.18 / remote 1.1.91     <- handle_link_ack(), not
1927  link state 1 -> 5  (VERSION_MISMATCH)           handle_link_reject()
```

The log line proves which path fired: that message belongs to
`handle_link_ack()`, so the detection came through the ACK, exactly as the
fallback intends.

### Remote Fault-Injection Harness (2026-08-27)

`CONFIG_RLC_REMOTE_FAULT_INJECTION` / `./build_remote.sh --inject`, mirroring
the base harness: `#warning`, boot banner, never written to `sdkconfig.remote`,
build dir wiped on a mode switch, and a build failure if the option did not
reach the built config. Keys `d` (display fault) and `b` (battery critical).

Added because the `REMOTE FAULT` band state is latched by only four conditions
— remote battery critical, display fault, multi-arm detected, boot failure —
none producible from the base harness or from the air. A wrong-channel ARM ACK
(base key `a`) does **not** latch it: the remote toasts "CHANNEL MISMATCH ERROR"
and reconciles by disarming, which is the better behaviour but left the fault
path untested.

The base harness also gained a `w` key (report `ERR_RELAY_FAULT`), since a weld
otherwise needs the arm sense jumpered high on a live base.

**Scope note:** this harness covers the REMOTE FAULT paths only. It does *not*
unblock T-F07 or T-F09, which need injections at specific FSM transitions that
have not been written.

### Phase 4 On-Target Tests (pending)

| ID | Test | Status |
|----|------|--------|
| T-D01 | Panel ID read-back at boot (expect clone ID 0x2A403300) | **PASS** 2026-08-27 — `ID 0x2A403300 (healthy)` |
| T-D02 | Splash holds 10 s, then transitions to main status | **PASS** 2026-08-27 — drawn 1827 ms, MAIN redraw 11767 ms = 9.94 s held |
| T-D03 | Continuity grid matches base STATUS_UPDATE for all 8 channels | **PASS** 2026-08-27 — bridged channel CONNECTED (●), other seven OPEN (○) |
| T-D04 | Encoder rotation moves the cyan selection cursor | **PASS** 2026-08-27 — one channel per detent, no overshoot (divider=4) |
| T-D05 | ARMED screen on arm, red pulse, arm-sense confirmed | **PASS** 2026-08-27 — border confirmed pulsing, not static |
| T-D06 | Pre-fire countdown smoothness (100 ms steps) | **PASS** 2026-08-27 — initially 301 ms steps (§10.3 deviation); **retested at ~101 ms after the Finding 1 fix, deviation closed** |
| T-D07 | NACK overlay text + 3 s timeout, screen restored cleanly | **PASS** 2026-08-27 — reason named in words, main screen restored with no residue |
| T-D08 | Link-lost screen and recovery back to main status | **PASS** 2026-08-27, and **re-run on fw 1.1.10** after the flush was replaced — `contact` advanced in exact 5 s steps 3637→33637 ms over a 40 s outage, `attempts` 1→16, `missed_pings` frozen at 3; recovery clean. Both transitions flushed 153600 px in 1 run, exercising the diff's all-changed path |
| T-D09 | Full-screen redraw time and steady-state frame rate | **FAIL then PASS** 2026-08-27 — first run 300 ms = **3.3 Hz** against the §10.3 ≥5 Hz floor. Fixed same day; retest **100.00 ms = 10.0 Hz**, render 33 ms avg, ~1200 px sent per frame against a 153600 px panel |

Full write-up: `Test_Report_Phase4_Display.md` (commit `c3b5745` + T-D09
profiling instrumentation). **9 PASS / 0 FAIL** after the Finding 1 fix.

### Display Refresh — Finding 1 Fixed (2026-08-27)

T-D09 failed at 3.3 Hz. The review-level diagnosis (one dirty bounding box
spanning the panel) was right but incomplete; two further causes turned up while
fixing it:

1. **`draw_field()` repaints every field every frame** regardless of whether its
   text changed. So the bounding box was not merely pessimistic — the pixels
   genuinely were all being rewritten, and a rect list alone would still have
   transmitted almost the whole panel.
2. **`vTaskDelay` came *after* the frame's work**, making the period
   `work + 100 ms`. Even an instantaneous flush could not have produced the
   100 ms period §10.3 requires of the countdown.

**Fix.** `flush()` now diffs the dirty box row by row against a shadow copy of
what the panel was last sent (second 460800-byte PSRAM buffer) and transmits
only the changed spans, coalescing consecutive changed rows into runs;
`xTaskDelayUntil` replaces `vTaskDelay` with a re-base on overrun.

Diffing was chosen over per-field invalidation deliberately: a missed
invalidation leaves a stale pixel, and this display shows ARMED. A pixel
comparison cannot get that wrong by construction, and it needed no changes to
any drawing code.

| Metric | Before | After |
|---|---|---|
| Steady frame period | 300 ms (3.3 Hz) | **100.00 ms (10.0 Hz)** |
| Period during PRE_FIRE | 301 ms | **101 ms** |
| Render + flush, steady | ~195 ms | **33 ms avg, 37 ms max** |
| Pixels sent per frame | ~153600 (whole panel) | **~1200 worst frame** |
| Full redraw | 232 ms | **89–188 ms** |

**A measurement lesson worth keeping:** the first profiling build sampled one
frame in twenty and reported `0 px` flushed for 30 s on a screen that was in
fact updating. Point-sampling could not distinguish a perfectly efficient
display from a frozen one. The profiling was changed to accumulate over the
whole window before the result was believed.

**Released as firmware 1.1.10**, then **1.1.11** once the profiling harness was
removed. Both units are on 1.1.11 stock and relinked — the base logs
`LINK_REQUEST from remote fw 1.1.11`, and a version mismatch would have refused
the link rather than ACKing it. 1.1.11 renders identically to 1.1.10: the
harness was passive and compiled out by default.

**Worst-case full-panel flush: 215 ms for 153600 px in one run.** This needed
separate instrumentation: after the fix a "full redraw" on a screen change only
sends what differs from the previous screen (MAIN 202 ms / 116124 px, SPLASH
105 ms / 36106 px), so none of those is a true all-pixels case. The only flush
where every pixel differs from the shadow is the boot panel clear in
`display_init()`, which runs before the frame loop and so was never logged.

**T-D08 re-run on 1.1.10 — PASS.** It was not treated as carrying over: it had
passed on the old whole-box flush, and LINK_LOST was the only §10.2 screen the
diffing code had never drawn. The re-run also turned out to be the most
informative of the session, because MAIN → LINK_LOST and back are the only
transitions in the test set where *every* pixel changes: both flushed 153600 px
in a single run, so the diff detected a complete change, coalesced it rather
than fragmenting it, and dropped no rows. That is the principal regression risk
of replacing the flush, closed by measurement rather than by argument. It also
yielded the true worst-case redraw of **250 ms**, higher than the 215 ms boot
clear (which only fills black and carries almost no render cost).

**T-D09 instrumentation — added in 1.1.10, removed in 1.1.11.** T-D09 asks for
two numbers that cannot be seen on the panel, and nothing measured them. A
`CONFIG_RLC_DISPLAY_PROFILE` Kconfig option plus `./build_remote.sh --profile`
were added, following the `--inject` pattern, and removed again once the
measurements were taken. **Recover from git history at 1.1.10 if the display
refresh ever needs re-measuring** — every timing figure in this section and in
`Test_Report_Phase4_Display.md` §6 was taken with that harness and cannot be
reproduced on a stock build.

**Four §10.2 screens have still never been rendered on the panel** — the
firmware-mismatch screen, the fire-complete screen, the error screen, and the
`IGNITION ACTIVE` state of the firing screen. Three fall out of tests already
planned (T-F01, T-S10b) at no extra cost; they should be observed there rather
than assumed working.

---

### Encoder Oversensitivity — Spec Never Implemented (2026-08-20)

**Report.** Channel selection overshoots, and felt worse since the NeoPixel
strip went in — suspected interference.

**Finding: the firmware implemented neither mechanism FSD §5.5.1 specifies.**

| §5.5.1 requires | Was implemented |
|---|---|
| Cycle-position quadrature decoder | Gray-code level comparison `if (A != B) CW else CCW` — the approach the section explicitly rejects for half-step encoders |
| `ENC_DIVIDER` raw pulses per step | **Did not exist anywhere in the codebase** |
| 2 ms lockout | 5 ms |

So every accepted edge became a channel change immediately. Worse, B was
configured `GPIO_INTR_NEGEDGE` but `gpio_isr_handler_add` was only called for
A — three of every four transitions were lost.

**Why that also explains the interference.** The decoder had no notion of a
legal transition: it sampled B at the instant of an edge on A. An electrical
glitch on A was therefore indistinguishable from a detent, and produced a
channel step whose direction depended on whatever B happened to read —
effectively random. The strip plausibly supplies the noise (8 pixels of 800 kHz
data plus current pulses sharing a ground with GPIO 4/5), but the reason it
*manifested* is that the decoder accepted anything.

**Fix.** Implemented as specified: cycle-position decoder rejecting any
transition that is not exactly one position around the cycle, `ENC_DIVIDER`
(**4**, raised from the spec's 3 at the user's request), reversal resetting the
accumulator, ISRs on both lines on both edges, lockout to the spec'd 2 ms.
Contact bounce is now rejected inherently — chattering one line toggles between
two adjacent states, so the accumulator oscillates about zero.

**Also reduced the strip's contribution.** Rather than slowing the frame rate
(which would degrade the ERROR flash and boot chase), `rlc_rgb_led.c` now keeps
a shadow of the last transmitted frame and **skips `led_strip_refresh()` when
nothing changed**. A steady map transmits nothing at all; a pulsing cursor
sends twice a second instead of twenty times. Same visuals, far less data-line
and current-pulse activity.

**Diagnostics.** `encoder_get_stats()` exposes ISR entries, legal transitions
and emitted steps; the remote's periodic log carries them as
`enc[isr= valid= step=]`. A much larger `isr` than `valid` means edges are
arriving that are not rotation.

**Measured on target after flashing:**

| | Idle ~15 s | After deliberate rotation |
|---|---|---|
| `isr` | 0 | 40 |
| `valid` | 0 | 35 |
| `step` | 0 | 8 |

Counters stay at zero when untouched — no phantom counts. `valid/step = 4.4`
against `ENC_DIVIDER` 4, and 5 of 40 edges were rejected as illegal — bounce
the old decoder would have turned into channel changes.

Covered by host tests T-Q01…T-Q06 (FSD §15.5), including bounce, reversal and
the guarantee that one detent moves exactly one channel.

---

### Encoder Rotation Sense Reversed (2026-08-20)

The v1.27 decoder moved the channel selection **opposite to the knob**. Which
way a KY-040 counts depends on how A and B are wired to the MCU, so this is a
board property, not a decoder property — added `ENC_REVERSED` (default 1, as
built) alongside `ENC_DIVIDER`, the same treatment `RLC_STRIP_REVERSED` gets.
Applied by negating the direction before the divider accumulator, so the
divider and reversal logic are unaffected. Host test T-Q07 pins the sense in
both directions, so a future rewire has to update the constant rather than
silently inverting the operator's controls.

**Caught while verifying: two strip tests had been failing since the
dirty-frame optimisation**, and truncated test output (`| head -16`) hid it —
they were committed red. The optimisation gave the driver hidden state (a
shadow of the last transmitted frame) that `test_strip.c`'s `reset()` did not
clear, so the driver correctly skipped writing pixels it believed were already
correct while the mock's buffer had been zeroed underneath it. A harness gap,
not a firmware defect: nothing in firmware clears the strip behind the driver's
back. `reset()` now clears the shadow, and the full suite is green — 217 checks
across 10 binaries.

---

### Arm Sense Reporting Corrected End to End (2026-08-20)

**Trigger.** A question about what "HW OFF" meant on the remote's status line.
Reviewing the chain turned up that the field meant nothing useful and that a
second field was making a false safety claim.

**What was wrong.**

1. `STATUS_UPDATE` carried the **key switch twice** — `base_arm_switch` =
   `key_sense_get_debounced()`, `arm_switch_hw` = `key_sense_get_raw()`. One bit
   of information in two fields; the raw copy had no operational use.
2. The protocol header documented both as *arm sense* ("raw arm sense GPIO (arm
   relay COM output)"), which is a different signal on a different pin. The
   display labels were written from those comments.
3. **The arm relay feedback (GPIO 21) was never transmitted at all.**
   `arm_sense_get_debounced()` appeared only in three FSM guards and the base's
   own log.
4. **The ARMED screen showed "SENSE CONFIRMED" derived from the key switch** —
   asserting arm-relay confirmation the remote had never received. The base's
   own interlock was sound (it checks arm sense before entering ARMED), but the
   display's claim was not derived from it and would have read CONFIRMED with a
   dead arm-sense circuit.
5. Stale status rendered identically to "key off" — absence of data displayed as
   a safety guarantee.

**Fix.** Fields renamed to `base_key_switch` (GPIO 42) and `base_arm_sense`
(GPIO 21), with the second now carrying the real debounced arm sense. **The
struct stays 14 bytes** — the redundant raw copy was simply repurposed into the
field its name already claimed — so `_Static_assert` and the offset self-test are
unchanged.

**Firmware bumped 1.0.0 → 1.1.0.** The field's *meaning* changed while its size
did not, so nothing structural would stop a mixed pair from misinterpreting it;
the strict version gate is the only thing that does, and only if the version
actually moves.

**Four-state BASE field** on the main status line, per FSD §10.2.2:

| Key | Arm sense | Shown | Colour |
|---|---|---|---|
| OFF | LOW | `SAFE` | green |
| ON | LOW | `READY` | amber |
| any | HIGH, in ARMED/PRE_FIRE/FIRING | `ARMED` | red |
| any | HIGH elsewhere, or `ERR_RELAY_FAULT` | `WELD!` | flashing |
| — | stale | `?` | grey |

`ARMED` and `WELD!` come from the arm sense, never the key. The reasoning: a
welded relay leaves the fire path live with the key OFF, so a key-driven display
would print SAFE over an energised igniter circuit. The `WELD!` check also tests
`base_state` rather than waiting on the base's weld confirm count, so it warns
earlier than `ERR_RELAY_FAULT` does.

Line renders as `SEL CH 1   BASE READY   REMOTE ARMED` — 36 of the 40 characters
available at the scale-2 font floor.

**Verified:** host tests T-M01…T-M07 (27 checks), including the welded-relay case
and a sweep proving the key switch alone can never produce ARMED or WELD! in any
base state. Both units rebuilt, flashed together and confirmed linked at v1.1.0.

---

### LINK LOST Screen Counters Fixed (2026-08-19)

**Symptom.** The link-lost screen's "Last contact: 1 s ago" never advanced.

**Root cause.** Both dynamic fields were derived from `missed_pings`. Its update
path in `rlc_link.c` sits behind `if (s_state != RLC_LINK_STATE_LINKED) return;`,
so the counter stops the instant the link is declared lost and freezes at
`HEARTBEAT_FAIL_THRESHOLD` (3). The display computed
`3 x HEARTBEAT_INTERVAL_MS / 1000` = **1 s, forever** — the arithmetic matches
the reported symptom exactly. The "Attempts" line was frozen at 3 for the same
reason, and was mislabelled: it was showing ping misses, not reconnect attempts.

**Fix.** Added `rlc_link_status_t.ms_since_contact`, computed from a new
`s_last_contact_ms` that records the wire-receive timestamp of every well-formed
frame from the peer — set in `process_frame()` after the MAC filter and parse,
so it covers all message types and both roles. The attempts line now uses
`linkreq_attempts`, which was already maintained and exported but unused.

**Trap hit while fixing it (worth remembering).** The first version took the
state mutex around the timestamp write. `link_task` **already holds that mutex**
across the whole `process_frame()` call, and it is a non-recursive FreeRTOS
mutex — so the link task deadlocked, the TWDT fired, and the remote went into a
reboot loop. Caught immediately on target by the watchdog (`rlc_link` listed as
the task that failed to check in, with both CPUs idle — a block, not a spin).
A comment at the write site now records this.

**Verified on target** across a 50 s induced outage (base held in reset):

| | During outage | On recovery |
|---|---|---|
| `missed_pings` (old source) | **frozen at 3 throughout** | 0 |
| `ms_since_contact` (new) | 2354 → 47354 ms, advancing | 153 ms |
| `linkreq_attempts` | 1 → 23, advancing | 0 |

The remote's periodic status log now carries `contact=` and `attempts=` too,
which is how the above was measured and is useful diagnostics in its own right.

---

### Battery ADC Sampling Hardened (2026-08-19)

`rlc_battery.c` took a **single** raw ADC read per call and fed an 8-deep mean.
Divider calibration showed why that is not good enough: a noisy bench supply
produced 600-1500 counts of sample spread with individual samples clipping at
ADC full scale, and **a clipped sample can only bias a mean upward** — making a
flat pack read as healthy, the one direction a battery guard must never fail in.

Each reading is now the **median of a 33-sample burst** (1 ms spacing), feeding
the existing 8-deep moving average. Odd count so the median is a real sample;
the spacing spreads the burst over ~33 ms so samples decorrelate from supply
ripple. Bursts where more than a quarter of samples clip log a warning — the
median has already discarded them, but persistent clipping means supply noise
or an input over range and should not pass silently.

Cost is immaterial: sampling runs at 1 Hz in dedicated tasks that feed the 5 s
task watchdog.

**Measured effect (host test T-B03).** In a burst with 9 of 33 samples clipped,
the mean reads **571 counts high** — roughly **+2 V** through the base's 4.3148
divider ratio — while the median is exact.

**On target, 30 s per unit, after reflashing:**

| Unit | vbat spread | Clipping warnings |
|---|---|---|
| Base | 43 mV (0.35 %) | 0 |
| Remote (on the noisy bench supply) | **20 mV (0.24 %)** | 0 |

Covered by host tests T-B01…T-B07 (FSD §15.5), including the clipped-burst case
contrasted directly against the mean, dropout rejection, and retention of the
last good reading on total ADC failure.

**Not changed:** the 8-deep moving average is still a mean. With each input
already a robust median that is sufficient, and making it a median too would
slow the response to a genuine voltage collapse — a behavioural change in a
safety path that was not warranted by the evidence.

---

### `FIRE_PROTECTED_CHANNEL_MASK` Widened to 0xFF — All 8 Channels (2026-08-23)

`0x01` → `0xFF` by explicit operator decision, closing the gate that has
restricted fire testing to channel 1 since 2026-07-21. The protection BOM the
gate was waiting on is now complete on every channel:

| Protection | Coverage |
|---|---|
| RC snubber across the relay contact | all 8 channels + arm relay |
| 2x 1N5819 clamp (GND and +3V3) on the sense pin | all 8 channels |
| 217 Ω sense-branch series resistor | all 8 channels |

The 217 Ω is what makes the difference: it converts the 3V3-side clamp from an
unlimited arc injector into a ~41 mA source and holds the pin at ~3.55 V during
a fault, inside the ESP32-S3's 3.6 V absolute maximum. Before it was fitted, the
clamps alone put the pin at 3.9-4.2 V and dumped the arc straight into the rail.

`relay_init()` no longer logs the `bug #18 gate ACTIVE` warning — its absence at
boot is the confirmation that the mask is 0xFF. Both units rebuilt and reflashed;
base verified running with all eight channels classified correctly and the link
up (`rssi=-38`, `txfail=0`).

**Still outstanding:** the TL431 rail clamp (bug #24). With the 217 Ω fitted and
the base's own rail load exceeding 41 mA, a single-channel fault no longer lifts
the rail; the clamp now covers the multi-channel case.

**Fire testing on channels 2-8 has never been done.** The gate is open, but no
channel other than 1 has been fired on this hardware. Treat the first shot on
each channel as a test, not a routine firing — and see bug #28, which blocks
fire testing entirely for now.

---

### Bug #31 — Fire GPTimer left running after a completed pulse (2026-08-27, **RESOLVED same day, fw 1.1.9**)

Found by the full-codebase review (`Code_Review_AllPhases_20260827_0308.md`,
finding BF-01). Rated CRITICAL — it is the only finding in that review that
changed the hazard analysis.

**The defect.** In ESP-IDF, an expired one-shot GPTimer alarm auto-disables the
*alarm*. It does not stop the *timer*: the driver stays in `GPTIMER_FSM_RUN`.
Every exit path from FIRING called `fire_timer_stop()` — CEASE_FIRE, DISARM,
key off, arm sense lost, the 4.5 max-duration backstop, `do_enter_error()` —
**except the successful one**, `EVT_FIRE_PULSE_DONE`.

So after one normal launch the timer was still running. On the next arm/fire
cycle of the same power cycle, `fire_timer_start()` called
`gptimer_set_raw_count()` and `gptimer_start()` on a running timer, inside
`ESP_ERROR_CHECK`. Against the ESP-IDF v5.4.1 this project builds with,
`gptimer_start()` CAS-expects `GPTIMER_FSM_ENABLE` and returns
`ESP_ERR_INVALID_STATE` → `abort()` → panic-print and reboot.

**Why it is critical, not merely a crash.** The panic happens *after*
`relay_fire_set(ch, true)`. The arm relay and the channel relay are both
energised at that moment, and nothing in a panic path de-energises them: the
igniter carries full current for the whole panic-print plus reboot interval —
well over 100 ms, where an e-match fires in single-digit milliseconds. The base
then reboots mid-FIRING, and the remote sees a link drop rather than FIRE
COMPLETE, so the operator gets no indication of what happened.

**Toolchain sensitivity.** A second IDF checkout on this machine (v5.5.2)
returns `ESP_OK` ("already started, do nothing") instead. Under a future 5.5.x
upgrade the same defect would stop panicking and become a quieter timing
hazard, because `gptimer_set_raw_count()` on a counting timer is documented as
unsynchronised with the counting clock. The fix is required either way.

**Why it was never seen.** No test has ever completed a fire pulse and re-armed
on the same power cycle. T-F02, the only G3 fire test run so far, aborts before
the pulse.

**Fix (three layers, all in fw 1.1.9):**

1. `fire_timer_stop()` on the `EVT_FIRE_PULSE_DONE` path, so the timer is in a
   known state between pulses.
2. An unconditional `gptimer_stop()` at the top of `fire_timer_start()`, so
   correctness does not depend on which exit path ran last.
3. `fire_timer_start()` returns `esp_err_t` instead of using `ESP_ERROR_CHECK`.
   The FSM checks it: on failure it clears the firing channel, calls
   `do_enter_error(ERR_INTERNAL)` — which runs `relay_all_safe()` — and latches
   ERROR. **Nothing on the fire path may `abort()`.**

**Regression test:** `tests/host/test_base_fsm.c` T-FSM05 runs two complete
arm→fire→pulse-done→cooldown cycles against the production FSM and asserts the
timer is stopped after each, plus a fault-injected start failure that must end
in ERROR with the igniter de-energised. It runs on every build.

### Bug #30 — Continuity-loss disarm has no level-triggered backstop (2026-08-26, **RESOLVED same day, fw 1.1.8**)

Found by the post-session code review, not by testing — G2 passed 18/20 without
touching this.

**Three facts combine into a hole in bug #29's own fix:**

1. The M1 non-blocking arm verify leaves the FSM in **`STATE_IDLE`** with
   `s_arm_verify_pending` set and **`s_armed_channel` still 0**, for up to
   `ARM_SENSE_VERIFY_TIMEOUT_MS` (200 ms).
2. `STATE_IDLE` has **no `EVT_CONTINUITY_CHANGED` branch** — the event is
   silently discarded there.
3. `continuity_task` posts **only on band change**. Once a channel reads OPEN,
   no further event is emitted for it.

So an igniter that goes OPEN inside the verify window has its event dropped in
IDLE, the verify completes, and the base enters ARMED with the band *already*
OPEN — at which point nothing will ever post again. **The base sits ARMED on an
open igniter until the 10 s arm timeout, silently.** Guard 2 does not help; it
passed legitimately before the disconnection.

The same hole exists if the FSM queue is full when `on_io_change()` posts: the
10 ms blocking send fails, the event is lost, and the band has already changed
so it is never regenerated.

**Probability is low** — the disconnection must land in a 200 ms window *and*
the round-robin must sample that channel within it. **The consequence is the
full bug #29 hazard**, which is why it is MAJOR rather than minor.

**Fix:** make it level-triggered on entry to ARMED. On both ARM completion
paths, re-read the current band instead of waiting for a change:

```c
if (continuity_get_channel(ch) == CONT_OPEN) { /* refuse / disarm */ }
```

That closes the verify-window race *and* the dropped-event case, and is robust
to any future missed edge. The event path stays the fast detector; the level
check is the backstop.

**The general lesson is worth keeping: an edge-triggered safety monitor needs a
level-triggered backstop.** Bug #29 was fixed with an event, and the event is
the only thing holding the property. Any lost or mistimed edge silently removes
the protection, and nothing reports it.

**RESOLVED 2026-08-26, firmware 1.1.8 — two fixes, covering different halves.**

1. **At arm-verify completion**, re-read the band before completing the ARM and
   abort with `NACK_NO_CONTINUITY` if it has gone OPEN. Refuses rather than
   arming and relying on an edge that has already been consumed.
2. **A periodic level check in `check_timers()`** (~50 ms) for ARMED and
   PRE_FIRE. Re-reading the level cannot miss an edge: if the armed channel is
   OPEN it is OPEN on every tick, so this converges within one tick of any
   missed event.

The second is not redundant. The review's first draft claimed an entry check
alone closed the dropped-event case; it does not — a queue-full drop while
*already* ARMED happens after entry, and nothing re-examines it. **Both checks
were needed.** Scoped exactly as `armed_channel_went_open()`: ARMED and
PRE_FIRE only.

**Verification is partial and should be completed.** T-F02 confirmed the entry
check does not false-positive (a normal arm completes through the verify path
the check sits on) and that the backstop does not fire spuriously in IDLE.
Neither has been *positively* triggered. Doing so needs a disconnection inside
a 200 ms window, or a deliberately full FSM queue — both want an injection to
reach reliably. A `--inject` key that drops the next `EVT_CONTINUITY_CHANGED`
would exercise the backstop directly and is the natural next addition to the
harness.

---

### Fault-Injection Harness — `CONFIG_RLC_FAULT_INJECTION` (2026-08-26)

Two arming tests could never be run: **T-A11** (ARM with a stale
STATUS_UPDATE) and **T-A13** (wrong channel in CMD_ACK). Neither is inducible
from outside the firmware.

- **T-A11.** Link loss trips at 3 missed pings (1.5 s), long before the
  staleness timeout, so jamming or shielding the radio produces LINK_LOST —
  never "linked but stale". The only way to reach that state is for the base to
  keep answering PINGs while withholding STATUS_UPDATE.
- **T-A13.** Nothing in normal operation emits a malformed ACK.

Both injections are therefore **base-side**, and both live in
`components/rlc_base/src/rlc_faultinject.c`, compiled only when
`CONFIG_RLC_FAULT_INJECTION` is set (default **off**, base only).

Build with `./build_base.sh flash --inject`. Console keys on UART0:

| Key | Effect |
|---|---|
| `s` | toggle STATUS_UPDATE suppression (heartbeats untouched) — T-A11 |
| `a` | arm a **one-shot** wrong-channel ARM ACK — T-A13 |
| `e` | force the base into ERROR while STATUS_UPDATE keeps reporting IDLE — T-A19 |
| `?` | print current injection state |

**Four independent guards against a test build reaching the field**, because a
firmware that deliberately lies to the remote must not be mistakable for a real
one: a `#warning` on every compile, a boot banner plus an `ESP_LOGE`, a flash-time
warning from the build script, and `--inject` never touching `sdkconfig.base`
(the option is appended to the working copy only). The script also **cleans the
build directory when switching in or out of injection mode**, and **fails the
build** if `CONFIG_RLC_FAULT_INJECTION` did not actually reach the built config.

**Design notes worth keeping:**

- **The one-shot channel corruption is deliberate.** A sticky version would
  also corrupt the DISARM ACK the remote sends in response, and that two-fault
  interaction is not what T-A13 tests.
- **The corrupted channel is kept inside 1..NUM_CHANNELS.** An out-of-range
  value might be rejected by a bounds check before the mismatch check ever
  runs, testing the wrong thing.
- **The suppression hook sits inside `send_update()`**, after the
  `rlc_link_is_linked()` check. Heartbeats live in the link task and are
  untouched, so the link stays healthy while the remote's cache ages out. The
  post-handshake status push (`rlc_link_set_status_request_cb`) routes through
  `status_update_trigger()` → the same `send_update()`, so it is suppressed too
  — there is no second path that would silently defeat the injection.

**Two harness bugs found while bringing it up, both worth recording:**

1. **`idf.py set-target` regenerates `sdkconfig` from the defaults**, discarding
   anything appended before it. The first version appended
   `CONFIG_RLC_FAULT_INJECTION=y` too early, so it produced a *normal* build
   wearing an injection build's log messages — and T-A11 duly "failed" because
   nothing was being suppressed. The option is now appended **after**
   `set-target`, and the script verifies it landed in `build_base/config/sdkconfig.h`
   before flashing.
2. **Stack overflow in `fi_console`.** 3072 bytes was not enough for ESP-IDF's
   stdio; the first `printf` from the task overflowed and rebooted the base,
   silently clearing every injection flag. The boot banner survived only
   because it runs on `app_main`'s much larger stack, which made the harness
   look functional. Raised to 8192 and `print_state()` moved to `ESP_LOG`.

**Both bugs presented as a T-A11 firmware failure.** The base was rebooting
(`rst:0xc` between the injection command and the arm attempt), so suppression
was off by the time the operator armed, and the arm correctly succeeded. Worth
remembering: **when an injection test fails, check the injection before the
firmware.**

---

### `tools/armgate-test` — Arm-Relay AND-Gate Verifier (2026-08-26)

Standalone bring-up firmware added at `tools/armgate-test/` that proves the
FSD §5.4.4 hardware AND gate **at the ARM SENSE node**, not from the indicator
LEDs. Written because bug #28 and its sibling were both *indicator* wiring
faults: after reworking that wiring, the LEDs are precisely what cannot serve
as the instrument. Run it after any rework of the arm relay, key switch or
indicator wiring.

| Step | Key | GPIO 47 | Expect |
|---|---|---|---|
| 0 | SAFE→ARM→SAFE | — | KEY SENSE (GPIO 42) tracks the key |
| 1 | SAFE | driven | relay out, ARM SENSE = 0 |
| 2 | ARM | low | relay out, ARM SENSE = 0 |
| 3 | ARM | driven | relay in, ARM SENSE = 1, coil LED lit |
| 4 | ARM | low again | relay releases, ARM SENSE back to 0 |
| 5 | ARM→SAFE | driven | key alone breaks the coil, ARM SENSE = 0 |

Steps 1–3 are the three rows that matter. The other three cover failures those
rows cannot see on their own: **step 0** validates the instrument (KEY SENSE is
what every later step uses to know the key position, and a stuck input would
turn the run into a silent no-op reporting PASS); **step 4** catches a relay
that pulls in and never releases; **step 5** reaches the key-SAFE case from an
energised relay with the software leg still asserted, proving the key-switch leg
— the one that must hold with the ESP32 crashed or unpowered — from both
directions.

Design notes worth keeping:

- **Every step samples for 2 s at 10 ms after a 150 ms settle**, and reports the
  count of HIGH samples rather than a single level. A line that cannot hold a
  steady level **fails whichever level was expected** — a marginal sneak path is
  not an interlock anyone can reason about, and a single `gpio_get_level()`
  would miss exactly the fault this tool exists to find.
- **The operator moves the key; the firmware moves GPIO 47.** Key position is
  read from KEY SENSE, so the program waits for the right position instead of
  asking anyone to type. Hands stay on the hardware. The only keyboard input is
  ENTER at the safety prompt and y/n for "is the coil LED lit" (step 3b — the
  one check that genuinely needs eyes).
- **All eight channel relays are driven inactive at boot** before anything else
  runs, closing the high-impedance window between power-on and GPIO config.
- Pins are duplicated from `pin_config.h` rather than included, so the tool
  still builds when the firmware tree is mid-edit. Keep them in step.
- Failure guidance is printed with the summary: which step failed maps to a
  specific fault class (sneak path around the key switch, welded contact, coil
  drive failure, stuck relay, indicator-only).

**Run it with `tools/armgate-test/run.sh`**, which sources the ESP-IDF
environment itself (same convention as `build_base.sh`) and defaults to the
base unit's by-id port.

**RESULT 2026-08-26 — ALL SEVEN STEPS PASS.** First run on chip #4 after the
bug #28 indicator rework:

| Step | Result | Detail |
|---|---|---|
| 0. KEY SENSE tracks key | PASS | SAFE → ARM → SAFE all observed |
| 1. Software leg alone (key SAFE + GPIO 47 driven) | PASS | ARM SENSE steady LOW, 0/200 samples |
| 2. Hardware leg alone (key ARM + GPIO 47 low) | PASS | ARM SENSE steady LOW, 0/200 |
| 3. Both legs (key ARM + GPIO 47 driven) | PASS | ARM SENSE steady **HIGH, 200/200**, relay pulled in |
| 3b. Coil LED lit with relay in | PASS | operator confirmed |
| 4. Relay releases (GPIO 47 back low) | PASS | ARM SENSE steady LOW, 0/200 |
| 5. Key to SAFE drops the relay | PASS | ARM SENSE steady LOW, 0/200 — key broke the coil |

**Every window was 200/200 or 0/200 — not one mixed sample anywhere.** That is
the result worth recording: the concern after bug #28 was a *marginal* sneak
path delivering LED-sized current around the key switch, and a marginal path
would have shown up as a partial count long before it showed up as a level.
There is none.

**What this closes.** Both legs of the §5.4.4 hardware AND gate are verified
at the node, in both directions, and the coil LED now agrees with the node —
so the operator-facing rule "green = SAFE, red coil LED = fire bus live" is
trustworthy on this unit again. Bug #28's resolution is confirmed by
measurement rather than by inspection. **The pre-fire-test verification the
bug #28 entry asked for is done.**

**Safety:** it energises the arm relay, so VBAT reaches the fire bus.
Igniters must be disconnected. Reflash `./build_base.sh flash` when done.

**Also measured this session: the siren draws under 200 mA steady**, closing
the 1N5819 rating question from bug #27 with a 5x margin.

---

### Firmware 1.1.2–1.1.8 (2026-08-26)

Consolidated record — the G2 campaign and its fixes shipped seven releases in
one day. One line each; `changelog.md` carries the detail.

- **1.1.2** — siren continuous in ARMED + continuity-loss disarm (bug #29).
- **1.1.3** — `PRE_FIRE_DELAY_MS` 2 s → 5 s (operator decision from T-A17).
- **1.1.4** — remote ARM-refusal reporting (base in ERROR names the flag).
- **1.1.5** — channel-mismatch toast ("CHANNEL MISMATCH ERROR").
- **1.1.6** — no-silent-refusals sweep; base answers commands in ERROR with
  NACK `0x0E`.
- **1.1.7** — fire-button ring LED reports state, not the button.
- **1.1.8** — bug #30: level-triggered backstop for the continuity-loss
  disarm.

---

### Firmware 1.1.2 — Siren Continuous in ARMED (2026-08-26)

**Operator finding.** The 500 ms ARMED pulse **interferes with the siren's own
internal modulation**. The device runs its own sweep; gating the 12 V supply at
1 Hz restarts that sweep on every edge, so it never reaches the loud part of
its cycle. The pulsed ARMED warning came out *quieter and less attention-getting
than a steady tone* — the opposite of the intent.

The pattern dates from FSD v1.1 (2026-03-22), when the base had a plain buzzer
whose only available modulation *was* on/off gating. Once that became a real
siren with its own oscillator, the pattern stopped earning its place, and
nobody had heard it on hardware until bug #27's driver was fitted five days
ago.

**Change.** `siren_start_pulse()` is removed. ARMED now calls
`siren_start_continuous()`, so the siren sounds without interruption from ARM
through PRE_FIRE and FIRING. PRE_FIRE re-asserts continuous rather than
assuming it, so the state does not depend on how ARMED was entered.

**LINK_LOST and ERROR stay patterned** (500/500 × 4 cycles, and 3 × 200 ms
blasts). Those patterns carry information — they distinguish a fault from an
armed pad — and they are short enough that the modulation interference does not
matter over a few cycles.

**Two side effects worth recording:**

- Base battery draw during ARMED roughly doubles versus the old 50 % duty
  cycle. Immaterial: `ARM_TIMEOUT_MS` bounds ARMED at 10 s.
- The siren flyback diode now switches **once per sequence** instead of once
  per second. That retires the repetitive-avalanche derating concern raised
  when the 1N5819 was fitted (bug #27) — the 1 A part now has an easy life, and
  only the steady-current question remains.

The N2 stale-callback protection in `rlc_siren.c` is unchanged and still
needed. The infinite (`-1`) pattern is gone with the pulse, so N2's first
failure mode is no longer reachable by construction, but link-lost and error
are still finite periodic patterns and can still park a callback on the mutex.

---

### Bug #29 — Base stays ARMED when the armed igniter loses continuity (2026-08-26, RESOLVED same day, fw 1.1.2)

**Symptom, found on target during fire-sequence testing.** Arm a channel, then
disconnect the igniter. The base **stays ARMED**: arm relay energised, fire path
live, siren sounding, and neither unit indicates that the igniter has gone. The
only exits were the 10 s arm timeout or an operator disarm.

**This was a specification defect before it was a code defect.** Continuity was
checked once, at arm time (`guard_arm()` guard 2). After that, band changes only
triggered a `STATUS_UPDATE` — the FSM never saw them, because the continuity
change callback carried no arguments and said nothing more than "something
moved".

FSD §7.3.1 stated the position explicitly: *"Continuity-loss disarm during
ARMED/PRE_FIRE states is not implemented... The brief window between arming and
firing (bounded by ARM_TIMEOUT_MS) makes mid-arm igniter disconnection an
accepted low-probability risk."* Three things are wrong with that:

1. **The rationale was obsolete by five months.** v1.8 (2026-03-23) removed the
   disarm because continuity sensing was *disabled* in ARMED/PRE_FIRE under the
   old shared-MOSFET design, so any reading would have been stale. The v1.10
   SPDT redesign (2026-03-23, the very next revision) made continuity live
   throughout ARMED and PRE_FIRE — the channel relay sits on NC until FIRING.
   The removal was never revisited against the new hardware.
2. **The document contradicted itself for 25 revisions.** §4.x's Phase 3 test
   criteria have listed *"All disarm triggers work (switch, command, link loss,
   **continuity → OPEN**, battery)"* the whole time.
3. **The risk is not low-probability.** An igniter leaving the circuit is
   precisely what happens when a person is at the pad handling it — the one
   moment when being armed matters most.

**Fix (FSD v1.35, fw 1.1.2).** New `EVT_CONTINUITY_CHANGED` carries the channel
number *and* the new band from `continuity_task` to the base FSM;
`armed_channel_went_open()` gates the disarm. The band travels in the event
rather than being re-read by the FSM, because the round-robin sampler may have
moved the channel on again by the time the event is dequeued. The queue send is
a 10 ms blocking one, matching the arm-sense sibling — a dropped event here
would silently leave the base armed on an open igniter, the exact failure being
fixed.

**Scoped deliberately.** Getting this wrong in the other direction would break
firing entirely:

| Condition | Disarms? | Why |
|---|---|---|
| Band = OPEN | **Yes** | Matches arming guard 2 — OPEN is the only band that blocks arming |
| Band = MARGINAL / SHORT | No | Informational per §7.3.1 step 2; unchanged |
| Armed channel | **Yes** | — |
| Any other channel | No | Informational. T-A18 is the regression guard |
| State = ARMED, PRE_FIRE | **Yes** | Relay on NC, so the reading is live and real |
| State = FIRING | **No** | The relay is on NO and the NC sense line is physically disconnected — the armed channel reads OPEN *by design*. Acting on it would abort every fire pulse the instant it started |
| State = POST_FIRE | **No** | OPEN is the **success** indicator: it means the igniter fired |

**Detection latency: up to ~800 ms** — one channel per 100 ms round-robin over
eight channels, plus classifier hysteresis. Accepted: it is well inside
`ARM_TIMEOUT_MS`, and this is a safety backstop rather than a real-time
interlock. Do not read it as a fast interlock in a test report.

**The remote needs no change.** It learns of the disarm from the resulting
`STATUS_UPDATE`, whose `continuity_bands` field already shows the channel as
OPEN. No new NACK reason: there is no command to NACK, because the trigger is a
spontaneous hardware event.

**Retest owed:** T-A16 (disconnect while ARMED), T-A17 (disconnect during the
PRE_FIRE countdown), T-A18 (disconnect a *non-armed* channel — base must stay
ARMED). None has been run yet.

---

### Bug #28 — ARM RELAY LED lights with the key in SAFE (2026-08-23, **RESOLVED 2026-08-26**)

**Symptom.** Turning the base key switch to the **SAFE** position illuminates the
**ARM RELAY** LED — the red one wired across the arm relay coil terminals
(FSD §5.4.4). The relay itself does **not** pull in. Firmware is in IDLE, so the
IRLZ44N gate on GPIO 47 is not driven.

**Why this is odd.** Per §5.4.4 the coil LED is passive and sits directly across
the coil, so it can only light when coil current flows — which requires *both*
the key switch ON (COM→NO connects VBAT to coil+) and the MOSFET driven. In SAFE
the coil+ node is disconnected from VBAT entirely (COM→NC), so the LED should be
dark regardless of what the firmware does.

**What the symptom implies.** An LED lighting while the relay stays out means a
path is delivering enough current to light an LED (a few mA) but far less than
the coil needs to pull in (a 12 V automotive relay wants on the order of 150 mA).
That is the signature of a **high-impedance sneak path feeding the LED around
the intended one**, not of a relay failing to operate. Candidates, cheapest test
first:

1. **LED anode on the wrong side of the key switch** — wired to the NC (SAFE)
   node instead of across the coil, or the KEY-position red LED and the coil red
   LED swapped. Would light in SAFE with no current path through the coil at all.
2. **A shared "GND" that is not GND** — e.g. the green SAFE LED's return tied to
   the coil(−)/MOSFET drain node instead of true ground, so SAFE current returns
   through the coil LED.
3. **Back-feed through the key sense divider** (§5.4.3b, 27 kΩ/10 kΩ on GPIO 42)
   or the arm sense divider (§5.4.3, GPIO 21). 12 V through 27 kΩ is ~0.4 mA —
   dim, but visible on a modern high-efficiency LED.

**Measurements that separate them, in order:**

- Battery disconnected: continuity-check the key SW NC and NO nodes against both
  red LED anodes. This settles case 1 in seconds.
- Key in SAFE, powered: measure arm relay **coil+ to GND** and **coil− to GND**.
  If coil+ sits at ~0 V the LED is not across the coil — case 1 confirmed.
- Check whether the LED tracks the firmware. If it lights in SAFE *only* while
  the MOSFET is driven, the return path is shared (case 2); if it lights
  regardless of GPIO 47, it is case 1 or 3.
- Lift one leg of the suspect LED and re-check. If the key switch then behaves,
  the fault is in the indicator wiring alone.

**Safety framing.** Most likely this is an indicator-wiring fault and nothing
more — the relay is not energising, so the fire path is not live, and the
firmware's own arm sense (GPIO 21, which reads the fire bus rather than the LED)
reported `arm=0` throughout, consistent with the relay genuinely being out. But
the key switch is **one of the two independent legs of the hardware AND gate**
(§5.4.4), the leg that is supposed to hold even with the ESP32 unpowered or
crashed. A sneak path that delivers current around that switch deserves to be
understood rather than assumed harmless, and until it is:

- The arm indicators cannot be trusted to report the true state of the fire bus.
  The operator-facing rule "green = SAFE, red coil LED = fire bus live" is
  currently wrong on this unit.
- **No fire testing until this is resolved**, on any channel. The mask was
  widened to 0xFF in this same session; that is a firmware gate and it does not
  cover a hardware-side interlock anomaly.

**RESOLUTION (2026-08-26).** Indicator wiring corrected on the base by soldering
rework. The ARM RELAY LED now lights **only** when the arm relay is actually
energised — i.e. only with the key switch ON *and* GPIO 47 driven, which is the
behaviour 5.4.4 specifies. The sneak path is gone.

**A second indicator fault was found and fixed in the same session:** the
arm-key red and green LEDs were lighting **simultaneously** with the key in the
SAFE position. They now read correctly — **red = ARMED, green = SAFE**. This is
the same class of fault as candidate 2 above (a return path that is not true
GND), on the key-position indicator pair rather than the coil LED, and it
explains why the original symptom looked like it implicated the key switch leg.

**Consequences:**

- The operator-facing rule "green = SAFE, red coil LED = fire bus live" is now
  true on this unit. The arm indicators can be trusted again.
- Both legs of the hardware AND gate are accounted for: the anomaly was in the
  indicators, not in the key switch's break of the coil circuit.
- **The fire-testing hold imposed by this bug is lifted.** This was the last
  hardware gate; `FIRE_PROTECTED_CHANNEL_MASK` is already 0xFF (2026-08-23) and
  all seven Majors from the 2026-08-21 review round are fixed and confirmed.

**Before the first fire test, re-verify the AND gate directly** — the indicator
wiring was just reworked, so prove the two legs independently rather than
inferring them from the LEDs:

1. Key SAFE + GPIO 47 driven → relay stays out, ARM SENSE (GPIO 21) reads 0.
2. Key ON + GPIO 47 low → relay stays out, ARM SENSE reads 0.
3. Key ON + GPIO 47 driven → relay pulls in, ARM SENSE reads 1, coil LED lit.

Measure ARM SENSE at the node with a meter, not only in the log, at least once.

---

### 217 Ω Sense-Branch Resistors Fitted — Thresholds Recalibrated (2026-08-23)

Fitted on **all eight** channels in the position the previous entry specifies
(R_ref → [ADC pin + both clamps + R_pull] → 217 Ω → relay NC), closing both gaps
in the 1N5819 assessment: fault current into the 3.3 V rail is now limited to
~41 mA, and the pin sits at ~3.55 V during a fault instead of 3.9–4.2 V.
The 1N5819 clamps on CH7/CH8 were confirmed fitted at the same time.

**The resistor is in the sense current path, so every reading moved.** Firmware
constants re-derived from V = 3.3 × Rx/(R_ref + Rx) with Rx = (217 + R_ign) ∥ 100 kΩ:

| Constant | Was | Now | Boundary |
|---|---|---|---|
| `CONT_MARGINAL_UV` | 66000 | **261000** | unchanged at ~67 Ω |
| `CONT_OPEN_UV` | 432000 | **586000** | unchanged at ~500 Ω |
| `CONT_R_SENSE_OHM` | — | **217** | new, documents the part |

**On-target verification, five known loads plus an open channel.** Predicted
against measured, base running the rebuilt firmware:

| Ch | Load | Predicted | Measured | Band | Correct? |
|---|---|---|---|---|---|
| 1 | Amazon fireworks igniter | 205 mV | 205–208 mV | CONNECTED | yes |
| 2 | 14.9 Ω | 216 mV | 215–219 mV | CONNECTED | yes |
| 3 | 74.3 Ω | 267 mV | 269–271 mV | MARGINAL | yes — just over the 67 Ω boundary |
| 4 | 2k16 | saturates | 969 mV (4095) | OPEN | yes |
| 5 | 4k28 | saturates | 969 mV (4095) | OPEN | yes |
| 6 | Klima igniter | ~205 mV | 205–209 mV | CONNECTED | yes |
| 7 | nothing | 3.19 V | 969 mV (4095) | OPEN | yes |
| 8 | Amazon fireworks igniter | ~205 mV | 203–205 mV | CONNECTED | yes |

Model and hardware agree within ~2 mV across the range, and back-calculating
R_sense from the three resistive loads gives 216–219 Ω against a part marked
217 Ω. The pre-fix capture is the predicted silent degradation exactly: every
connected channel read `MARG`, `cont=0x882a`, and nothing could ever be
CONNECTED. After the constant change: `cont=0x4425`, all eight correct.

**The boot self-test caught it, as designed.** `test_continuity_classification()`
carries hardcoded µV vectors against the old boundaries; with only the constants
changed the base failed three vectors at boot and **halted** rather than running
with a mismatched pair. Vectors updated in the same commit. This is the second
time those vectors have earned their place.

**Also fixed:** the 5 s `cont raw/uV:` diagnostic line silently dropped channel 8
— `cbuf[160]` is exactly eight entries wide and the loop guard (`n < size − 24`)
stopped at seven. Buffer raised to 208.

**Range not affected.** Full scale (950 mV) is now reached at ~1117 Ω rather than
~1670 Ω. Everything above 500 Ω is OPEN, so nothing measurable was lost.

**Still open:** the TL431 rail clamp (bug #24). With 41 mA limiting and the
base's own rail load exceeding that, a single-channel fault no longer lifts the
rail on its own, so the clamp is now insurance for a multi-channel fault rather
than the primary defence.

---

### External Antennas on Both Units — 200 m Range Test (2026-08-23 / 2026-08-25)

The 0 Ω link was moved from the PCB antenna to the U.FL connector on **both**
units and external antennas fitted. No firmware change is needed — the
DevKitC-1 has no RF switch to configure.

**Range test (2026-08-25).** Base on the ground, remote hand-held at ~1.5 m,
200 m separation: **−93 dBm, link holding**. Still held with the remote also
placed on the ground. The link could only be dropped by unscrewing an antenna.

**What the test did and did not establish.** It did not find the edge —
unscrewing an antenna is a 20–30 dB step, so it only shows the limit is
somewhere beyond 200 m in that geometry. To locate it, walk out until `PING
miss` first appears in the base log and record the RSSI at the *first miss*,
not at the drop.

**−93 dBm at 200 m is ground-reflection limited, not hardware limited.** Free
space at 200 m / 2.45 GHz is 86 dB, which with ~+19 dBm TX and ~2 dBi per end
predicts ≈ −64 dBm. The 29 dB deficit is the two-ray ground reflection: with
h₁ ≈ 0.05 m (base on the ground) and h₂ ≈ 1.5 m the breakpoint is ~2.5 m, so
the path is deep into the **d⁴** regime:

```
PL = 40·log₁₀(200) − 20·log₁₀(0.05 × 1.5) = 114.5 dB  →  ≈ −92.5 dBm
```

against −93 dBm measured. The consequence matters: in d⁴, the ~4 dB of margin
left buys only ~25 % more distance — **roughly 250–260 m in that geometry**, not
400 m. Raising both units to ~1.5 m moves h₁h₂ from 0.075 to 2.25, about
**30 dB**, which restores the free-space limit and pushes the drop-out past a
kilometre. **Getting the base off the ground is worth more than any radio
change available to this design.**

**Expected drop-out threshold: −96 to −99 dBm.** ESP-NOW here runs at the IDF
default rate — nothing in the tree calls `esp_wifi_config_espnow_rate()`,
`esp_now_set_peer_rate_config()` or `esp_wifi_set_protocol()` — so 1 Mbps DSSS,
where the ESP32-S3 is sensitive to about −98 dBm at 8–10 % PER. The link
survives past that point because `tick_base()` needs **3 consecutive** missed
500 ms slots: with per-packet loss p the drop needs p³, so 10 % PER is a drop
every ~8 minutes, 30 % PER every ~20 s, and 50 % PER within seconds. Net effect
is failure at roughly 1–3 dB below sensitivity, over a transition only ~3 dB
wide — abrupt, with little warning.

**`ERR_COMM_DEGRADED` trips first**, at >30 % ping loss over the 10-slot window,
and guard 10 NACKs ARM with `COMM_DEGRADED`. Arming is therefore refused a
couple of dB before the link actually fails, which is the correct ordering.

**Do not rely on the RSSI reading as a warning.** ESP32 RSSI is uncalibrated
(±3–6 dB is normal), reported values compress in the high −90s so the last few
dB may never appear, and `RSSI_AVERAGE_WINDOW = 3` smooths and lags it further.
A fade deep enough to kill the link can pass without moving the number.

**Note the direction asymmetry:** the base logs its view of the remote's
packets. The weaker of the two directions sets the limit and it is not
necessarily the one being displayed.

**Corroborated by T-S11.** The 5-consecutive-send-failure path was triggered
on-target during the earlier RF shielding test at a reported **−98 dBm**, which
lands inside the −96 to −99 dBm window predicted here.

---

### Bug #19 UPDATE — LED swap did not fix it, fault is in the strip (2026-08-23)

The 4th LED was removed and replaced with the LED taken from position 8. The
symptom is unchanged: **pixels 1-3 render, 4-8 dark.** The stuck-blue behaviour
at pixel 4 is gone (it now stays dark), which is consistent with a fresh part
that has never latched a frame.

**What this rules in and out.** A replacement part at the same position
reproducing the same break means the original diagnosis — "dead LED controller
*or* a broken joint between pixel 3's DOUT and pixel 4's DIN" — resolves to the
second branch: the **data path into pixel 4**, i.e. the strip's copper or the
solder joints at that position. Two caveats before calling it a trace fault:

1. **The donor LED is unproven.** Position 8 never lit, so the LED moved into
   position 4 was never demonstrated working. Removing and reflowing a WS2812
   twice is also a good way to kill one.
2. **The joints are the new variable.** A hand-reworked pad is a more likely
   open than factory copper.

**Next step, in order:** (a) with the strip powered and a frame being sent,
scope or meter pixel 3's DOUT pad — if it is toggling, the fault is downstream
of it; (b) continuity-check pixel 3 DOUT pad → pixel 4 DIN pad with a DVM, which
settles copper vs joint in seconds; (c) if the copper is open, bodge a wire
across it; (d) only if DIN is receiving data and the pixel still does not light
is the replacement LED itself suspect. `tools/strip-diag` paints the static
frames needed for (a).

T-L18 stays **FAIL**.

**RESOLVED (2026-08-26) — strip replaced.** The whole strip was swapped and
all eight pixels now respond correctly. **The fault was pixel 3's output
stage:** it rendered its own colour correctly throughout, but no longer passed
the data signal downstream — which is why every test that looked at pixel
3's *appearance* said it was healthy.

This is branch (a) of the next-step list above, and it retires the two theories
that had accumulated around the position: the donor LED was never the problem,
and neither were the reworked joints at pixel 4. A WS2812 whose driver works but
whose DOUT stage is dead presents exactly as "the break is between pixel 3 and
pixel 4", because electrically it is — the break is simply *inside* pixel 3
rather than in the copper between them.

**Lesson for the next chain fault:** a pixel that lights correctly is not
evidence that it is passing data. Probe DOUT, don't infer from the colour.
T-L18 now **PASSES**; T-L15 is unblocked for channels 4-8.

---

### Bug #27 — Base siren not connected (2026-08-21, **RESOLVED (hardware) 2026-08-26**)

Surfaced while closing out the 1.1.1 review round: the operator ran the
arm→PRE_FIRE→FIRING sequence to verify N2 and reported that **the siren is not
wired**. GPIO 40 currently drives nothing — the IRLZ44N driver has not been
fitted.

**Two separate consequences, and the second is the bigger one.**

1. **N2 cannot be verified.** Both of its failure modes are *silent* ones — a
   siren stuck on after `siren_off()`, and a siren that falls quiet through the
   whole 2 s PRE_FIRE countdown. Neither is observable with the output
   disconnected, so the N2 fix rests on code inspection alone. Bench tests 2
   and 3 from the review are blocked on this.

2. **The pad has no audible warning at all.** ARMED pulse, PRE_FIRE/FIRING
   continuous, LINK_LOST 4-cycle and ERROR 3-blast patterns are all produced in
   firmware and all go nowhere. The remote's buzzer (§5.5.7, GPIO 16) is
   operator feedback in the operator's hand — it is in the wrong physical
   location to warn anyone standing near the igniter, which is the siren's
   entire purpose. This is a safety-function gap, not just a test-coverage gap,
   and it is independent of any firmware finding.

**Fix — FSD §5.4.8 / §5.4.10, base unit, GPIO 40, active HIGH:**

```
              VBAT+ (12 V)
                   │
             ┌─────┴─────┐
             │   SIREN   │◄── flyback diode: cathode VBAT+, anode drain
             └─────┬─────┘     (1N4007, or 1N5819/SS14)
                   │ drain
           ┌───────┴───────┐
           │    IRLZ44N    │
           │ gate    source│
           └──┬─────────┬──┘
    150 Ω     │         │
GPIO 40 ──/\/\──┤       │
                │       │
          10 kΩ ⌇       │
                │       │
               GND     GND
```

- **10 kΩ gate pull-down is mandatory here.** GPIOs are high-impedance from
  power-on until `siren_init()` runs; without it, Cgd coupling can partially
  turn the MOSFET on during the power-on transient. A siren that sounds by
  itself at boot trains operators to disbelieve it — the worst outcome for a
  warning device.
- **Flyback diode is mandatory.** A siren is an inductive load; the
  de-energise spike will kill the MOSFET without it.
- Polarity is **active HIGH** (MOSFET), the opposite of the remote's buzzer
  (**active LOW**, BC547 inverts). Easy to get backwards when wiring both.
- GPIO 40 is also the ESP32-S3's MTDO. Harmless — the board uses USB
  Serial/JTAG, and GPIO 42 (MTMS) is already committed to key sense — but
  pin-based JTAG is not available on this hardware.

**Retest when fitted:** review bench tests 2 (siren continuous across
ARMED→PRE_FIRE) and 3 (stops and stays stopped after disarm / arm timeout /
CEASE_FIRE), plus the LINK_LOST 4-cycle and ERROR 3-blast patterns.

**HARDWARE FITTED (2026-08-26).** The IRLZ44N driver is installed on GPIO 40
with its 150 Ω gate series resistor and 10 kΩ gate pull-down, and a
**1N5819** Schottky flyback diode is fitted across the siren (cathode VBAT+,
anode drain) — the same part already stocked for the continuity-sense
clamps, and one of the parts 5.4.8 names for this position.

**One rating to confirm:** the 1N5819 is 40 V / 1 A. At turn-off the diode
carries the full siren current, so it is correctly rated only if the siren draws
**under 1 A** at 12 V. Most 12 V pad sirens sit at 200-500 mA, which is
comfortable; a high-output horn can exceed 1 A. Measure the siren's steady
current once with the driver on. If it is above ~700 mA, move to an SS34 or
similar 3 A part.

> **MEASURED 2026-08-26: under 200 mA steady.** The 1N5819 is correctly rated
> with a 5x margin. This item is closed — no diode change needed.

> **Update (later the same day, fw 1.1.2):** the 500 ms ARMED pulse was removed,
> so this diode now switches **once per sequence** rather than once per second.
> The repetitive-avalanche derating argument no longer applies — only the
> steady-current question above remains, and it is now a comfortable margin
> rather than a tight one.

**RETEST COMPLETE 2026-08-26 — all six checks PASS. Review finding N2 is
closed by measurement**, having rested on code inspection alone since
2026-08-21.

| # | Check | Result |
|---|---|---|
| A | Silent at power-on (10 kΩ gate pull-down holds GPIO 40 through the boot transient) | **PASS** — silent throughout boot |
| B | Continuous across ARMED→PRE_FIRE (review bench test 2, N2 case 2) | **PASS** — unbroken tone across the transition |
| C | Stops and stays stopped after disarm (review bench test 3, N2 case 1) — all three routes: CEASE_FIRE, arm timeout, key OFF | **PASS** ×3, 10 s of silence after each |
| D | LINK_LOST pattern = 4 cycles of 500/500 | **PASS** — 4 counted; confirms `SIREN_LINK_LOST_CYCLES` derives from the constant (m10) |
| E | Link recovery **mid-pattern** → immediate silence, stays silent | **PASS** — log shows recovery at 3080 ms, inside the 4000 ms window |
| F | ERROR pattern = 3 short blasts at 200 ms, then silence | **PASS** |

**Two notes on what these do and do not prove.**

*Test B is weaker than it looks, and it no longer matters.* The operator
released fire ~1.5 s into the 5 s countdown, so the siren was observed
continuous across the **transition** rather than through a full countdown. That
is sufficient: N2 case 2 required a stale tick from a running periodic timer,
and ARMED no longer runs one (the pulse was removed in fw 1.1.2). There is
nothing left to dispatch a stale callback during ARMED→PRE_FIRE.

*Test E is the one that actually exercised N2's surviving failure mode.* Case 1
(stuck ON after `siren_off()`) depended on the infinite `-1` pattern, which went
away with the ARMED pulse — so the original bench test 3 can no longer reach it.
The risk migrated to LINK_LOST and ERROR, which are still periodic. Test E puts
`siren_off()` (from the link-recovery path, FSD §7.2.8) against a pattern that
is genuinely mid-cycle, which is the only remaining route to a parked callback
re-driving the output ON with the timer stopped. The 3080 ms recovery timestamp
in the log confirms the window was hit rather than missed. **If a future change
reintroduces a periodic ARMED pattern, test E is the one to re-run, not B.**

---

### Post-Fix Code Review Round — firmware 1.1.1 (2026-08-21)

Second full-codebase review of the day, run against commit 28293b6 (the fix
commit for `Code_Review_AllPhases_20260821_1430.md`). Written up in
`Code_Review_AllPhases_20260821_1523.md`.

**Re-verification result: all seven prior Majors (2.1–2.7) fixed and confirmed
in source**, along with the large majority of the 32 minors. The structural
debt three reviews had been carrying is paid: the continuity classifier and
the base arm-state derivation now live in `rlc_common` as pure functions that
the boot self-test and the host tests compile directly, so the duplicate-copy
divergence (Phase-2 finding M2) is no longer possible.

**Two new Majors were found**, both introduced or left standing by the fix
commit. Neither is on the ignition path.

#### N1 — arm key ON at power-up was never registered

The debounce fix in 28293b6 suppressed the spurious "released" callback that
every input fired on its first stable reading. It did so by making the first
stable determination set the state *without invoking the callback at all*.

The remote's arm switch cached `s_armed` only from that callback. So with the
key already turned to ARM at power-up:

- `arm_switch_is_armed()` stayed **false indefinitely**;
- the arm indicator LED never lit;
- every encoder long-press was refused with "TURN ARM KEY FIRST";
- the only recovery was to physically toggle the key off and back on.

This is the ordinary operator flow of turning the key before powering up, or
power-cycling a remote left in ARM — the normal recovery action at a pad. It
fails safe (arming refused, never granted), which is why it is Major and not
Critical, but it is a total loss of the arm path.

The suppression could not simply be reverted: `rlc_fire_button.c` now *relies*
on it for the fresh-press interlock (a button held at boot must not read as a
press — FSD §5.5.3, review 4.12). The fix is therefore per-consumer — the arm
switch task, and the base's arm-sense and key-sense tasks, now adopt the
debounced state by polling `rlc_debounce_get_state()` every cycle. FSD §5.3.1
now states both halves of the contract as requirements, and host tests
T-D01…T-D06 pin them from both sides.

The base was less exposed because `arm_sense_init()` seeds its cached bools
from a raw read, but it shares the shape: a raw read that catches a transient,
or a key turned during the first 160 ms, was never corrected until the next
real transition. Fixed the same way.

#### N2 — siren stale-callback race, only half-fixed by the mutex

The previous round added a mutex to the siren for finding 5.4. The mutex
serialises the *state*, but `esp_timer_stop()` does not cancel a callback that
has already been dispatched — only `esp_timer_delete()` waits, and that cannot
be called from a path holding the lock the callback wants. So a callback can be
parked on the lock while a task reconfigures the pattern underneath it, then
act on the new state as if it were the old one. Both failure modes 5.4 named
were still reachable:

| Failure | Mechanism | Reached by |
|---|---|---|
| Siren stuck ON after disarm | `siren_off()` left the cycle count at −1 (infinite, from the ARMED pattern). The stale tick toggled the output back ON, with the timer stopped — nothing ever turned it off again. | Any ARMED→disarm: arm timeout, CEASE_FIRE, key off |
| Siren silent through PRE_FIRE | The PRE_FIRE pattern sets the cycle count to 0 (steady ON, no timer). The stale tick read 0 as "pattern finished" and drove the siren OFF — no pad warning for the full 2 s countdown. | Every launch: ARMED→PRE_FIRE is always preceded by a running 500 ms timer |

Fixed with a pattern-active flag set under the same mutex by every start/stop
path; a superseded callback returns without touching the output, and
`siren_off()` clears the cycle count too. FSD §12.3 now documents why the mutex
alone is insufficient, so the next person does not remove the flag as
redundant.

#### N3 — remote rebooted every 11.4 s (CRITICAL, found on target)

**Not found by either review — found by flashing.** Both static passes checked
that every task self-registers with the TWDT (5.10/5.11); neither asked *when
the TWDT is reconfigured relative to those registrations*.

`esp_task_wdt_reconfigure()` rebuilds the subscriber list. The remote called it
at §9.13 step 8 — after `display_start_task()`. Observed on the first flash of
this round:

```
I (1585) rlc_disp: display task started (prio 2, core 1)
...
E (1985) task_wdt: esp_task_wdt_reset(705): task not found     <- 195x, at 20 Hz
...
E (11435) task_wdt: Task watchdog got triggered. ...
Guru Meditation Error: Core  0 panic'ed (LoadProhibited). Exception was unhandled.
rst:0xc (RTC_SW_CPU_RST)
```

Chain: reconfigure drops the display and buzzer subscriptions → the display
task's `esp_task_wdt_reset()` fails at its full 50 ms frame rate → the unfed
watchdog triggers → **the trigger handler itself panics** walking stale entries
→ reboot. Every boot, reproducibly, at 11.4 s.

Three consequences, in order of severity: the remote was unusable; the §9.6
watchdog coverage that fix 5.10 added to the display and buzzer tasks was
silently void, so a hung SPI transaction could still freeze an "ARMED" screen
forever; and this is the *actual* root cause of the "task not found" boot
bursts that the 14:30 review attributed to registration ordering (5.11). That
5.11 fix was correct on its own terms, but by moving registration earlier —
ahead of the reconfigure — it turned an intermittent problem into a certain
one.

The base was clean only by accident: it happened to call `rlc_watchdog_init()`
before starting any task.

**Fix.** `rlc_watchdog_init()` split in two:

- `rlc_watchdog_init()` — reconfigure only. Called as the first statement of
  `app_main` on **both** units, before any task exists. Now §9.13 **step 0**.
- `rlc_watchdog_register_self()` — subscribes `app_main`. Called immediately
  before the housekeeping loop. Now §9.13 **step 11**. Kept separate because
  the init in between (SPI display, NVS, Wi-Fi start, up to 3× 500 ms peer
  retries) can exceed the 5 s timeout on its own — subscribing at step 0 would
  trade one spurious panic for another.

Verified on target: 45 s continuous, both units, zero TWDT errors, zero panics,
one boot each.

#### Minors fixed in the same round

| # | Fix |
|---|---|
| m1 | Remote now treats `EVT_BATTERY_CRITICAL` in LINK_LOST as terminal, matching the base. The battery task edge-triggers once per crossing, so discarding it there lost it permanently — the remote recovered the link and returned to service on a critical pack. LINKING now also handles `EVT_LINK_LOST`. |
| m2 | Ping health window and the base's expected-ping-slot tracker are reset with the session and on every entry to LINKED. Previously the tracker froze while LOST and then back-filled one "miss" per elapsed 500 ms slot on recovery, saturating the 10-slot window — so ARM was NACKed `COMM_DEGRADED` for the first ~5 s after *every* link recovery, on a link that was fine. |
| m3 | Stale-status timeout latches (`s_last_status_rx_ms` cleared). It used to re-run its whole block — warning log, LED pattern, channel re-sync — on every 50 ms tick until fresh data arrived: a 20 Hz log flood during exactly the condition an operator needs to read the log through. |
| m4 | `firing_exit()`'s comment claimed every FIRING exit funnels through it; the normal pulse-completion path deliberately does not (that is the one case where the pulse *did* finish, so §7.2.5 is honoured via POST_FIRE instead). Comment corrected rather than the code. Also collapsed a POST_FIRE double-entry to ERROR — one battery-critical event produced two `relay_all_safe()` passes (20 ms `vTaskDelay` each), two siren restarts and a duplicate log line. |
| m5 | FIRING max-duration backstop now calls `fire_timer_stop()`. It synthesises the completion event, but if the notification was lost *because the timer misbehaved*, the timer was still armed. |
| m6 | `rlc_link_next_seq()` drops the link on overflow like the three internal senders already did, instead of silently returning 0. The base's `send_ack()`/`send_nack()` did not check the return, so they would have emitted seq-0 frames the peer rejects as replay — all ACKs stopping with no diagnostic. Both call sites now check. |
| m7 | ESP-NOW send failures are counted, not logged from Wi-Fi task context. The per-failure `ESP_LOGW` fired hardest exactly when the link was already struggling, and logging takes the stdout lock. Exposed as `rlc_espnow_get_send_failure_total()` and printed as `txfail=` in both units' 5 s status lines — better diagnostics than the line it replaced. |
| m8 | The handshake no longer sends a fabricated STATUS_UPDATE. It was a Phase-1 placeholder hardcoding `base_state = STATE_IDLE` with zero error flags — and since the app-state guard rejects LINK_REQUESTs only for the *busy* states, a base in ERROR or LINK_LOST answered every handshake by asserting it was safe. Replaced with an application hook that asks the status task to push the real thing. |
| m9 | Five unchecked `xTaskCreatePinnedToCore` calls checked. Two are fatal: the base's arm-sense task (no relay feedback, no key sense, no weld detection, while the FSM's getters keep returning the init seed) and the remote's arm-switch task (arm key unreadable). Three log loudly. |
| m10 | `SIREN_LINK_LOST_DURATION_MS` was dead config — the 4-cycle count was a bare literal. Now derived. |
| m11 | `s_bands[]` made `volatile`, like its `s_uv`/`s_raw` siblings. It gates arming and is read from three tasks. |
| m12 | Arm-verify window is now `ARM_SENSE_VERIFY_TIMEOUT_MS` in `rlc_config.h`, not a bare `200` in the FSM. |
| m13 | An interrupted ARM re-syncs the selected channel. `wait_for_ack()` consumes `EVT_ENCODER_ROTATE` and returns INTERRUPTED without applying it, and that path did not call `do_enter_idle()` — so display and strip cursor showed the pre-rotation channel while the next long-press would arm a different one. |
| 5.7 | Remote input callbacks are registered *before* the tasks that drive them start. They used to be wired up at the end of init, so any press, key turn or detent in the first couple of hundred milliseconds was silently dropped. |
| — | Base strip is initialised before the boot self-tests, so a self-test failure can actually be signalled on it. It previously halted with `LED_PATTERN_ERROR` set on an uninitialised strip — a silent halt. The remote already did this correctly. |

**Firmware bumped 1.1.0 → 1.1.1.** Arm-path behaviour changed on both units, so
the strict version gate is doing real work: a half-flashed pair now refuses to
link rather than running mismatched safety logic. Flash base and remote
together.

**Host tests:** 10 binaries / 217 checks → **12 binaries / 265 checks**, all
passing. The debounce engine was stubbed out in the host harness; the stub is
removed and the real engine is compiled in (it is pure C with no ESP
dependencies, so the stub was never needed).

#### On-target verification (2026-08-21, firmware 1.1.1)

Both units flashed. Board identity confirmed by MAC before flashing, against
the `by-id` ports in the build scripts:

| Port (by-id) | MAC | Role | Matches config |
|---|---|---|---|
| `usb-1a86_USB_Single_Serial_5B5E042156-if00` | `44:1b:f6:81:f1:70` | BASE (chip #4) | ✓ `BASE_MAC_ADDR` |
| `usb-1a86_USB_Single_Serial_5B5E043219-if00` | `ac:a7:04:e2:f2:8c` | REMOTE (chip #2) | ✓ `REMOTE_MAC_ADDR` |

| Test | Result |
|---|---|
| Boot self-tests | **PASS** — 12/12 suites on each unit, v1.1.1 |
| TWDT errors / panics / unexpected reboots | **PASS** — zero over 45 s continuous on both (was: remote rebooting at 11.4 s every boot) |
| Link establishment | **PASS** — remote links on LINK_REQUEST attempt 1; LINKING→IDLE in 40 ms |
| Link loss + recovery | **PASS** — base detects PING drought at 1548 ms → LINK_LOST; recovers 880 ms later on the remote's re-handshake; both FSMs follow |
| Bidirectional version check (5.7) | **PASS** — `LINK_REQUEST from remote fw 1.1.1` accepted, token agreed both ends |
| Base telemetry | 12.34 V, RSSI −27, `txfail=0`, `key=1`, `arm=0`, `err=0x00`, continuity stable at `0x5425` |
| Remote telemetry | 8.00 V, RSSI −24, `missed=0`, `txfail=0`, contact 184–400 ms |
| `txfail=` counter (m7) | **PASS** — present in both status lines, reads 0 on a healthy link |

#### Operator bench tests (2026-08-21, firmware 1.1.1)

| # | Test | Result |
|---|---|---|
| 1 | **N1 — arm key ON before power-up.** Key to ARM first, then power up (or reset with the key left on). Arm LED lights, long-press accepted. | **PASS** — operator-confirmed. This is the regression that made 1.1.0's arm path unusable; it is now verified on hardware, not just by host test. |
| 2 | **N2 case 2 — siren through PRE_FIRE.** Arm, hold fire, siren must sound continuously across the ARMED→PRE_FIRE transition. | **SEQUENCE ONLY — N2 NOT VERIFIED.** The arm→PRE_FIRE→FIRING path ran correctly, but **the siren output (GPIO 40) is not connected on the base**, so the audible behaviour the N2 fix exists to protect was not observed. See below. |
| 3 | **N2 case 1 — siren after disarm.** Must stop and stay stopped after arm timeout or CEASE_FIRE. | **NOT RUN** — blocked on the same missing siren connection. |
| 4 | Full arm → pre-fire → fire → post-fire on channel 1, display's four-state BASE field tracked through it. | **PASS** — operator-confirmed. |

**Open: the siren is not wired.** Both N2 failure modes are *silent* ones — a
siren stuck on after disarm, and a siren that goes quiet through the pre-fire
countdown. Neither is observable without the output connected, so the N2 fix
currently rests on code inspection alone. Fit the driver (GPIO 40, IRLZ44N
low-side per §5.4.8/§5.4.10 — the 10 kΩ gate pull-down is the boot-safety part,
and the coil needs its flyback diode) and re-run tests 2 and 3 before treating
N2 as closed.

Everything else this round changed is now covered: N1 by operator test 1 plus
host tests T-D01…T-D06, N3 by the 45 s clean-run capture, and the fire path by
operator test 4.

---

### Bug #26 ROOT CAUSE — ~64 Ω in the continuity return (2026-08-21)

**Two independent faults, found in sequence.**

**Fault 1 — ADC calibration was disabled.** `rlc_continuity.c` hardcoded
`s_cali_handles[i] = NULL` with the note that curve fitting produced corrupted
handles, and that "raw conversion is sufficient because the thresholds have
wide margins". Bench measurement disproved the second claim. Re-enabled with
defensive guards (non-OK return or NULL handle falls back to raw); no crash
observed on chip #4, and the readings changed while raw stayed identical,
confirming it is active.

**Fault 2 — the continuity return path carries ~64 Ω.** With calibration on,
every sense node reads ~400 mV above where it should. Modelling a common return
resistance `R_g` carrying the summed sense current fits **every channel**:

| CH | Load | Predicted | Measured | Error |
|---|---|---|---|---|
| 1 | 0.1 Ω | 400.1 mV | 400 mV | −0 |
| 2 | 14.9 Ω | 413.3 mV | 411 mV | −2 |
| 3 | 74.3 Ω | 464.9 mV | 465 mV | +0 |
| 4 | 2160 Ω | 1552.0 mV | 1541 mV | −11 |
| 5 | 4280 Ω | 2035.2 mV | 2023 mV | −12 |
| 6 | Klima igniter | 401.8 mV | 400 mV | −2 |
| 7 | Amazon igniter | 401.8 mV | 400 mV | −2 |

Total sense current 6.28 mA × 64 Ω = the observed 400 mV lift. A resistive
divider cannot produce that pattern any other way.

**Predicted outcome once the return is repaired** — all eight bands correct,
including the GOOD band that was previously thought unmeasurable:

| CH | Load | Node | Band |
|---|---|---|---|
| 1 | 0.1 Ω | 0.1 mV | SHORT ✓ |
| 2 | 14.9 Ω | 15.1 mV | GOOD ✓ |
| 3 | 74.3 Ω | 73.7 mV | MARGINAL ✓ |
| 4 | 2160 Ω | 1308 mV | MARGINAL ✓ |
| 5 | 4280 Ω | 1857 mV | OPEN ✓ |
| 6-8 | igniters ~2 Ω | ~2 mV | GOOD ✓ |

**Verification step: measure resistance from the igniter low-side / continuity
ground terminal to the DevKit GND pin.** Expect < 1 Ω; the model predicts ~64 Ω.

**Earlier hypotheses, all disproved and worth recording:** R_ref missing (the
pin rests at 3.6 V open, so R_ref is present); an ADC dead zone at 12 dB
attenuation (0 dB changed nothing); and an ESP ground offset (DevKit GND to
battery negative measures 4.8 mV in circuit). The "raw 0" readings seen before
the DevKit was reseated were a *different* manifestation of the same bad
grounding — that time lifting the ESP's own ground, driving the inputs negative.

**The 3.71 V rail reading is also explained and is NOT a regulator fault.** The
DevKit measures 3.35 V in circuit against its own GND, and 3.30 V on a new
board. The bug #24 LDO replacement was probably unnecessary; that incident and
this one are the same underlying problem — **the grounding of this system is
bad**, and it has now produced three different misleading symptoms.

---

### Final On-Target Verification — All 8 Channels (2026-08-21)

Both units reflashed, all self-tests passing, linked at −24 dBm. Known loads on
every channel simultaneously:

| CH | Load | raw | Band | |
|---|---|---|---|---|
| 1 | 0.1 Ω 10 W | 11 | CONNECTED | ✓ |
| 2 | 14.9 Ω | 45 | CONNECTED | ✓ |
| 3 | 74.3 Ω | 282 | MARGINAL | ✓ |
| 4 | 2k16 | 4095 (sat) | OPEN | ✓ |
| 5 | 4k28 | 4095 (sat) | OPEN | ✓ |
| 6 | Klima igniter | 13 | CONNECTED | ✓ |
| 7 | Amazon igniter | 10 | CONNECTED | ✓ |
| 8 | Amazon igniter | 9 | CONNECTED | ✓ |

`cont=0x5425`. **8/8 correct**, against 2/8 when the investigation began.

This closes B2-C07 and B2-C09, and retires B2-C08 with the SHORT band.

**Margin note:** CH3 at ~70 mV sits only ~4 mV above the 66 mV
CONNECTED/MARGINAL boundary, with ±4 counts of sample noise. It classifies
correctly and hysteresis holds it, but a load near 74 Ω is genuinely close to
the line — worth knowing before reading too much into a band change there.

**Self-test caught a real defect during this work.** The three-band merge
updated the hysteresis classifier and its mirror but left
`test_continuity_hysteresis()`'s vectors asserting SHORT transitions, so both
units halted at boot refusing to run. Two things masked it: the halt is silent
by design, and the earlier `rlc_rgb_led_set_pattern()` init-safety fix turned
what would have been a visible reboot loop into a clean quiet halt. Also worth
recording — **pyserial asserts DTR on open, which on this CH340 auto-reset
circuit holds EN low**; opening with `dtr=False, rts=False` was required to see
any output. A silent board is not necessarily an unpowered one.

---

### Continuity Bands Reduced to Three; SHORT Merged Away (2026-08-21)

**Decision: `CONT_SHORT` merged into `CONT_CONNECTED`, and `CONT_GOOD` renamed
`CONT_CONNECTED`.**

**Why.** Three controlled experiments were run on CH1 with a fitted 220 Ω
sense-branch offset resistor, comparing a real igniter against a deliberate
short:

| Experiment | Separation | Implied igniter R |
|---|---|---|
| Run 1, back-to-back | 6.78 counts (t=7.7) | 1.77 Ω |
| Run 2, across power cycles | 2.93 counts (t=2.2) | 0.77 Ω |
| Run 3, both states stable | 4.42 counts (t=7.1) | 1.15 Ω |

The igniter measures **1.5–1.9 Ω** on a DVM. The effect is statistically real
and always in the right direction, but the **magnitude is not reproducible** —
it varied 2.3× for a setup that never physically changed. At 0.888 mV/Ω one ohm
is 3.8 counts, and the signal (≈6 counts), single-reading noise (sd ≈2.5),
lead contact resistance (≈3.8 counts per ohm of clip) and run-to-run drift
(≈2 counts) are all the same size. A midway threshold misclassifies **19 % of
single readings**; an 8-sweep average would cut that to 0.7 % but the
threshold's own position is uncertain by ±2 counts, which dominates.

**A band that cannot be measured must not be reported**, so it was removed
rather than shipped as a guess.

**Naming.** `GOOD` was factually wrong — it asserts igniter health the
measurement cannot support, and is actively misleading on a shorted pair of
leads. `CONNECTED` states only what is known: current can flow.

**Wire compatibility.** The 2-bit encoding is unchanged. Value 3 is retained in
the enum and decoded on display, simply never emitted, so no protocol version
bump was needed and a pre-merge peer still interoperates.

**The 220 Ω offset trial.** Fitted on CH1 only, then removed. It did exactly
what was predicted: fixed the ADC's low-end non-linearity (CH1 became stable at
206 mV with 3 counts of noise, matching the 209 mV model to within 3 mV) but
added no resolution, because lifting a short and an igniter by the same amount
does not separate them. Recorded because the reasoning generalises: **an offset
buys linearity, only current buys resolution.**

**Rejected alternative.** Raising the test current to ~3.4 mA (750 Ω ∥ 1.5 kΩ +
680 Ω ∥ 1.8 kΩ) would give 2.25 mV/Ω and make the band genuinely measurable at
~14.5 counts separation. It was declined because it cuts the no-fire margin from
50× to 15× for a purely informational band that never blocked arming.

### As-Built Hardware Update (2026-08-21)

| Item | Status |
|---|---|
| RC snubbers, all 8 channel relays (NO–COM) | **FITTED** |
| RC snubber, arm relay (NO–COM) | **FITTED** |
| 1N5819 clamps to GND and 3V3, **all 8 sense pins** | **FITTED** (CH7–CH8 confirmed fitted 2026-08-23) |
| 220 Ω sense-branch series resistor, all channels | **FITTED 2026-08-23** — 217 Ω on all 8; thresholds recalibrated, see the 2026-08-23 entry |
| 3.3 V rail clamp | **NOT FITTED** — still wanted, now belt-and-braces rather than primary |
| 220 Ω sense-branch offset, CH1 | Trialled then removed; superseded by the 217 Ω fitted on all channels |
| Continuity ground return | Repaired, all grounds within 0.3 Ω |
| Base ESP32 antenna | **External** — 0 Ω link moved from the PCB antenna to the U.FL connector, external antenna fitted (2026-08-23) |

This is most of the bug #18 protection BOM. **`FIRE_PROTECTED_CHANNEL_MASK` is
still `0x01`** and now understates the hardware: all eight channels have both
clamps and snubbers. Widening it to `0xFF` is a fire-path safety gate change and
is left for explicit confirmation rather than assumed.

---

### 1N5819 Continuity Clamps — Assessment, All 8 Channels (2026-08-21)

**Verdict: the right part at this node, and it does protect the pins. Two gaps
remain, and one of them got eight times larger when the clamps went from one
channel to eight.**

**Why 1N5819 is correct here, and why that does not contradict bug #22.** There
is **zero series resistance** between the relay NC contact and the ADC pin — the
sense node *is* the pin — so the clamp carries the entire fault current, limited
only by the arc and the pack. The 1 A rating is the feature: a BAT85 (200 mA) or
1N5711 (15 mA) would fail on the first real event. On the remote VBAT node
(bug #22) the divider limits fault current to ~110 µA, leakage dominates, and
1N5819 is the wrong part. Same part, opposite conclusions, because the nodes are
not alike. Both entries should be read together before either is "corrected".

**Leakage, against the 0 dB thresholds:**

| Path | Reverse bias | Effect | Direction |
|---|---|---|---|
| 3V3-side, load connected | ~3.2 V | error = I_leak x R_load; at the 67 Ω boundary 20 µA → 1.3 mV, 200 µA → 13 mV | *upward* — a good igniter drifts toward MARGINAL. Conservative: MARGINAL warns, only OPEN blocks arming |
| GND-side, channel open | 3.19 V | pulls the open node down; OPEN stops saturating the 950 mV full scale only above **~700 µA** | would eventually read MARGINAL instead of OPEN — unsafe direction, but an order of magnitude away at 25 °C |

1N5819 leakage is 1 mA max at 40 V / 25 °C and 10 mA at 100 °C, and roughly
doubles per 10 °C. At ~3.2 V reverse and bench temperature there is an order of
magnitude of margin; a base box in direct sun is the case to check, not the
bench.

**Two-minute verification, worth repeating hot:** disconnect all igniters and
measure each node with a DVM. Expect 3.194 V; total GND-side leakage is
`(3.3 − V)/3300 − V/100k`.

**Gap 1 — the rail injection path, now x8.** During a fault the 3V3-side diode
dumps the arc **into the 3.3 V rail**. Nothing on that rail can sink amps, so the
rail rises and takes out everything on it. Fixes, in priority order:

1. **220 Ω sense-branch resistor per channel** (see the assessment below) — the
   single highest-value change, since it converts an unlimited injection into
   ~41 mA.
2. **Active rail clamp** — TL431 shunt at ~3.57 V. A 3V6 zener is not adequate:
   soft knee, ±5% tolerance spanning 3.42–3.78 V, and ~3.9 V needed to sink
   40 mA, above the 3.6 V absolute maximum. This corrects the "3.6 V zener
   clamp" recommendation recorded under bug #24.
3. **The rail's own load already helps.** The ESP32-S3 plus LED strip draws well
   over 41 mA whenever the base is live, so with the 220 Ω fitted the LDO simply
   backs off and the rail does not rise at all for a single-channel fault. The
   TL431 is belt-and-braces for that case; it earns its place if several
   channels ever fault together.

Note that a rail clamp does **not** address the bug #24 ground-lift incident: a
shunt referenced to a lifted ground moves with it. That one is answered by the
repaired return (all grounds within 0.3 Ω) and secured connectors, and the
3.71 V reading is now understood as a grounding measurement artefact.

**Gap 2 — the clamp level is above absolute maximum.** 1N5819 V_f is ~0.6 V at
1 A and ~0.9 V at surge, so during a fault the pin sees 3.9–4.2 V against the
ESP32-S3's 3.6 V maximum. Damage-limiting, not damage-proof: it turns a
guaranteed kill at 12 V into a survivable excursion. The 220 Ω fixes this too —
at 41 mA the diode drops ~0.25 V and the pin sits at ~3.55 V, inside the limit.

---

### Re-adding the 220 Ω Sense-Branch Resistor — Consequences (2026-08-21)

The 220 Ω trialled on CH1 and removed for lack of resolution gain is, in its
*protection* role, the missing piece above. Its position is the same one that
produced the measured 206 mV floor: **R_ref → [ADC pin + both clamps + R_pull] →
220 Ω → relay NC**. It must not go between the node and the pin — there it
limits nothing and turns clamp leakage into a pin offset instead.

**What it buys**

| Effect | Before | After |
|---|---|---|
| Fault current into the 3V3 rail | unlimited (arc/pack) | (12.6 − 3.6)/220 ≈ **41 mA** |
| Pin voltage during fault | 3.9–4.2 V (over abs max) | ~3.55 V (inside abs max) |
| ADC low-end linearity | igniters sit at 2 mV, raw ~9, in the worst INL region | floor lifted to 206 mV — measured stable at 3 counts noise, matching the model within 3 mV |
| Test current | 1.00 mA | 0.9375 mA (no-fire margin improves slightly) |

**What it costs**

- **Every threshold moves.** With `V = 3.3 x (220 + R)/(3520 + R)`:

  | Load | Node | Meaning |
  |---|---|---|
  | 0 Ω (short) | 205.8 mV | new floor |
  | 2 Ω igniter | 207.6 mV | CONNECTED |
  | 67.4 Ω | 264.2 mV | **new `CONT_MARGINAL_UV` ≈ 264000 µV** (was 66000) |
  | 500 Ω | 587.5 mV | **new `CONT_OPEN_UV` ≈ 588000 µV** (was 432000) |
  | open | 3.194 V | saturates → OPEN, unchanged |

  Fit it without moving the constants and **no channel can ever read CONNECTED**
  — everything sits above 66 mV, so the display shows MARGINAL permanently.
  Arming would still work (only OPEN blocks), which makes this a silent
  degradation rather than an obvious failure. Change both constants in the same
  commit as the hardware.
- **Sensitivity drops 6.25 %** (3.3 kΩ → 3.52 kΩ total): 0.9375 mV/Ω instead of
  1.0. The CONNECTED band spans 58 mV instead of 66 mV. Immaterial at 0.23 mV/LSB.
- **All eight channels must get it, matched.** A ±5 % part spreads the floor by
  ±10 mV, which is ±11 Ω on a 67 Ω boundary — 17 %. Use 1 % metal film
  (`RC220E`), or hand-sort a matched set of 5 % parts against a DVM.
- **Clamp leakage now develops across it.** The 3V3-side leakage flows out
  through 220 Ω + R_load rather than R_load alone: +4.4 mV at 20 µA, +44 mV at
  200 µA. Measure the floor **hot**, not just at bench temperature.
- **Dissipation during a fault** is 9 V²/220 ≈ 0.37 W. A 1/4 W part survives a
  1 s pulse; prefer 1 W, and fusible would be better still, consistent with
  R_ref1/R_ref2.
- **It does not restore the SHORT band.** Proven, twice over: an offset buys
  linearity, only current buys resolution.

**Recalibration is mandatory** — re-run the known-load sweep on all eight
channels and derive the two constants from measurement rather than from the
formula above.

---

### Bug #26 RESOLVED (mostly) — ground repaired, 0 dB adopted (2026-08-21)

**User repaired the return path** (wiring error found, heavier gauge fitted, all
grounds now within 0.3 Ω). The 400 mV lift vanished and the mid-range became
accurate immediately — CH4 within 8 mV of prediction, CH5 within 5 mV.

That isolated the last fault cleanly: **a genuine ADC low-end collapse at
12 dB**. With good grounding, 73.7 mV read raw 70 (ideal 91), but 15.1 mV
collapsed to raw 1 and 2 mV to raw 0.

**0 dB attenuation then worked** — the earlier trial had been invalidated by the
return fault pinning every channel. Adopted along with the documented 500 Ω
OPEN boundary (`CONT_OPEN_UV` 1500000 → 432000 µV), which the 0 dB range also
requires since 1.5 V is unreachable.

| CH | Load | Predicted | Measured | Expected | Got | |
|---|---|---|---|---|---|---|
| 1 | 0.1 Ω | 0.1 mV | 0 | SHORT | SHORT | ✓ |
| 2 | 14.9 Ω | 15.1 mV | 12 mV | GOOD | **GOOD** | ✓ |
| 3 | 74.3 Ω | 73.7 mV | 73 mV | MARGINAL | **MARGINAL** | ✓ |
| 4 | 2160 Ω | 1308 mV | saturated | OPEN | **OPEN** | ✓ |
| 5 | 4280 Ω | 1857 mV | saturated | OPEN | **OPEN** | ✓ |
| 6-8 | igniters ~2 Ω | 2.0 mV | 0 | GOOD | SHORT | ✗ |

**5/8 correct, up from 2/8.** CH3 landed within 0.7 mV of prediction.

**Residual limitation.** The ADC floor at 0 dB sits around 5 mV, so anything
below roughly 5 Ω reads 0 and classifies SHORT. Real e-matches (1–3 Ω) fall in
that band. At the specified 1 mA test current a 2 Ω igniter develops 2 mV,
which is at the edge of what this ADC can do.

**Operationally, the important distinction works:** OPEN — the only band that
blocks arming — is now correct and set at the documented 500 Ω. A missing or
broken igniter is detected. What cannot be distinguished is a good low-Ω
igniter from a genuine dead short, since both present under 5 mV.

**Options for the residual, none yet adopted:**

1. **Accept and relabel** — merge SHORT into GOOD as "connected", since the two
   are not separable at 1 mA. Honest, firmware-only, loses shorted-lead
   detection (which does not currently work anyway).
2. **Raise the test current.** `R_ref` 3.3 kΩ → 1 kΩ gives 3.3 mA and puts a
   2 Ω igniter at 6.6 mV (raw ~28 at 0 dB). Still ~15x below the ~50 mA no-fire
   threshold, versus the present ~50x. Hardware change, erodes a deliberate
   safety margin.
3. **Amplify** the sense node. Proper fix, most work.

**Also worth aligning:** the GOOD/MARGINAL boundary is 66000 µV (~67 Ω) while
FSD §5.4.2 documents 20 Ω. Neither test load falls between, so it changed no
result here, but the code and spec still disagree.

---

### Bug #24 RECURRENCE — rail measured high again with chip #4 (2026-08-21)

DVM measurements taken while diagnosing bug #26 put the **continuity supply at
~3.72 V**, from two independent readings:

| Measurement | Implies supply |
|---|---|
| CH2 open-circuit rest voltage **3.60 V** (FSD §5.4.2 predicts 3.19 V) | 3.72 V |
| CH7 open-circuit rest voltage **3.60 V** | 3.72 V |
| 16.9 mV across a 14.9 Ω load | 3.76 V |

Bug #24 recorded chip #3 destroyed with the rail at **3.68 V**. This is the same
condition, with chip #4 fitted, **live on the bench**.

The ESP32-S3 GPIO absolute maximum is VDD + 0.3 V, so an open continuity channel
at 3.60 V is at or over the limit on all eight ADC pins simultaneously.

**This plausibly explains bug #26 as well:** six ADC channels reading a hard 0
regardless of input is consistent with input-stage damage from sustained
overvoltage. CH4 and CH5 — the two that still work — happened to carry
low-value loads holding them well below the rail.

**Action: measure the 3.3 V rail directly and do not power the base until it is
back at 3.3 V.**

---

### Bug #26 — Six continuity channels read raw ADC 0 (2026-08-21, OPEN)

**Bench test.** Known loads on all eight channels:

| CH | Load | Predicted pin | Raw ADC | Band | Expected |
|---|---|---|---|---|---|
| 1 | 0.1 Ω | 100 µV | **0** | SHORT | SHORT ✓ (no information) |
| 2 | 14.9 Ω | 14 831 µV | **0** | SHORT | GOOD ✗ |
| 3 | 74.3 Ω | 72 611 µV | **0** | SHORT | MARGINAL ✗ |
| 4 | 2160 Ω | 1 288 671 µV | 1284 | MARGINAL | MARGINAL ✓ (value 20 % low) |
| 5 | 4280 Ω | 1 829 240 µV | 2026 | OPEN | OPEN ✓ (value 11 % low) |
| 6 | Klima igniter | ~2 mV | **0** | SHORT | GOOD ✗ |
| 7 | Amazon igniter | ~2 mV | **0** | SHORT | GOOD ✗ |
| 8 | Amazon igniter | ~2 mV | **0** | SHORT | GOOD ✗ |

**Two of eight bands correct.** All three real igniters report SHORT — the
display tells the operator "terminals shorted, possible wiring fault" for a
correctly connected igniter.

**First hypothesis (ADC dead zone) was tested and DISPROVED.** The theory was
that the GOOD band (0.5–66 mV) sat below the ESP32-S3's response floor at 12 dB
attenuation. Switching the continuity channels to **0 dB** (0–950 mV, ~13x the
resolution) changed nothing for the six dead channels — still raw 0 — while
CH4 and CH5 correctly saturated at 4095, proving the attenuation change had
taken effect. At 0 dB, CH3's predicted 72.6 mV should read ~313 counts.

**Therefore the floor is not an ADC range limit.** Those six pins are either
genuinely near 0 V, or those ADC channels are not functioning. The experiment
was reverted; 12 dB at least reads the high end correctly.

**Leading hypothesis: R_ref missing or open on the affected channels.** With no
3.3 V feed through R_ref, the pin is tied toward GND through the load and
R_pull, reading 0 **regardless of what is connected** — which matches the data
exactly, since 0.1 Ω, 14.9 Ω, 74.3 Ω and three igniters all read identical 0.

**Decisive test RUN 2026-08-21 — R_ref hypothesis DISPROVED.** DVM on the pin:

| Channel | Load | Pin voltage | ADC reports |
|---|---|---|---|
| CH2 | 14.9 Ω | **16.9 mV** | raw 0, SHORT |
| CH2 | none | **3.60 V** | OPEN ✓ |
| CH7 | Amazon igniter | **0.75 mV** | raw 0, SHORT |
| CH7 | none | **3.60 V** | OPEN ✓ |

The front end works: R_ref is present, the pin rests high when open, and the
sense voltage is genuinely there. **The ADC simply does not respond to 16.9 mV**
— not at 12 dB, and not at 0 dB where it should have read ~73 counts.

So the ESP32-S3 ADC cannot resolve the GOOD band on this circuit at any
attenuation. A 2 Ω igniter develops ~2 mV; even a perfect 12-bit converter on
the 0 dB range is ~8 counts, and this one reads 0 at 17 mV. **The fix must
raise the sense voltage — more test current, or amplification — not tune
thresholds or ADC settings.**

Note the open-circuit rest voltage of **3.60 V against the 3.19 V the FSD
predicts**: see the bug #24 recurrence entry above. That overvoltage may itself
be why six channels read a hard 0.

Working channels are GPIO 5 and 6; dead are GPIO 2, 4, 7, 8, 9, 10. No firmware
conflict on those pins (relays are 11–18, arm relay 47, siren 40, key sense 42,
arm sense 21, VBAT 1) and the battery ADC on GPIO 1 works, so the ADC1
peripheral itself is healthy.

**Also found:** CH4 and CH5 read 20 % and 11 % below prediction, and
inconsistently, so it is not a single scale error. Continuity ADC calibration
is deliberately disabled (`rlc_continuity.c`, documented: curve-fitting
produced corrupted handles and LoadProhibited panics on some ADC1 channels), so
readings use the uncalibrated `(raw × 3300) / 4095` fallback — verified to match
both in-range channels exactly. The stated justification, "wide threshold
margins", is what this test contradicts.

**Historical note:** test B2-A02/B2-C02 recorded "CH1 with ~2 Ω resistor = GOOD,
32000–33000 µV". A 2 Ω load cannot produce 32 mV (32 mV is ~33 Ω), and today a
2 Ω load reads 0. That baseline is unreliable.

**Not changed pending diagnosis:** `CONT_OPEN_UV` remains 1500000 µV (~2828 Ω)
even though FSD §5.4.2 documents ">500 Ω". Tightening the only band that blocks
arming, on readings that are not trustworthy, would be worse than the mismatch.

---

### Bug #25 — No hardware undervoltage cut-off on either battery (2026-08-20, OPEN)

**Gap.** Neither unit has a low-voltage disconnect, and **FSD §5.6 does not
specify one** — §5.6.1 and §5.6.2 define only chemistry, capacity, connector,
regulation and the *firmware* thresholds. Protection today is entirely software:

| Unit | Pack | CRITICAL threshold | Per cell |
|---|---|---|---|
| Base | 3S LiPo 5000 mAh | `BASE_VBAT_CRITICAL_MV` 9000 | 3.00 V |
| Remote | 2S LiPo 2200 mAh | `REMOTE_VBAT_CRITICAL_MV` 6400 | 3.20 V |

**Why software alone is not enough.**

1. **ERROR does not disconnect the load.** Hitting the critical threshold puts
   the FSM into a latched ERROR state, which stops *operation* — it does not cut
   power. The regulators, display backlight (the remote's dominant load), status
   LEDs, siren driver and MCU all keep drawing from the pack afterwards.
2. **It only works while the firmware runs.** A brownout, a halt, a watchdog
   loop, or a unit simply left switched on after a launch day all discharge the
   pack with nothing watching.
3. **The consequences are not recoverable.** Below roughly 3.0 V/cell a LiPo
   takes permanent capacity loss; below roughly 2.5 V/cell it becomes unsafe to
   recharge at all — internal shorting on the next charge is a fire risk. The
   base's own threshold already *sits at* 3.00 V/cell, so there is no margin
   between "firmware says stop" and "pack is being damaged".

**Fix.** Fit a hardware undervoltage cut-off between each pack and the load, set
above the firmware threshold so the firmware still gets to report and latch
ERROR first. Candidate approaches, in rising order of effort:

- A protection-circuit module (PCM/BMS) matched to the pack — simplest, but many
  hobby modules cut at ~2.5-2.8 V/cell, which is already too late.
- A discrete comparator (e.g. TL431 or a micropower comparator) driving a
  P-channel MOSFET or load switch, with hysteresis so it latches off rather than
  oscillating at the threshold.
- A dedicated load-switch IC with programmable UVLO.

Suggested cut-out points, leaving the firmware room to act first: **base ~9.6 V
(3.2 V/cell)**, **remote ~6.8 V (3.4 V/cell)**. Both need hysteresis or a latch,
because an unloaded LiPo recovers above the threshold after disconnect and would
otherwise cycle.

**Note the ordering requirement:** the cut-off must be *above* the firmware
CRITICAL threshold, or the hardware will disconnect before the operator ever
sees the ERROR screen explaining why.

---

### Bug #22 — Remote GPIO 1 has no overvoltage clamp (2026-08-19, OPEN)

**State.** The 3.3 V zener on the remote's VBAT ADC node was removed to fix
bug #21 and **has not been replaced**. GPIO 1 is currently protected only by
the divider's series impedance.

**Risk.** Feeding the remote's battery input from a base-range supply (12 V+)
puts >4.2 V on a 3.3 V pin. With the 18 kΩ/10 kΩ divider the fault current
through the ESP32's internal ESD diodes is limited to roughly 110 µA at 12 V
in, which is survivable but is not protection to rely on — this is the bug #18
failure class that has already destroyed two base ESP32s.

**Fix.** Fit a low-leakage Schottky from the ADC node to the 3.3 V rail:
**BAT54** (or BAT85 / BAT43 / 1N5711). Anode to the divider centre / ADC pin,
cathode to 3.3 V. Leakage around 1 µA gives ~6 mV of droop into the present
6429 Ω source — negligible, versus the 144 µA and 925 mV the zener produced.

**Do NOT fit a 1N5819.** As a 1 A power Schottky its large junction leaks far
more, and in this position the leakage flows from the 3.3 V rail *into* the
node, biasing readings **upward** — the dangerous direction for a battery
threshold, since it masks a flat pack. A 1N4148 is an acceptable interim
(nanoamp leakage) at the cost of clamping at ~4.0 V rather than ~3.6 V.

**Error budget for any candidate part**, against the present divider:

| Divider | Thevenin Z | Leakage for < 20 mV error |
|---|---|---|
| 18 kΩ/10 kΩ (now) | 6429 Ω | < 3.1 µA |
| 3.0 kΩ/1.2 kΩ (bug #23) | 857 Ω | < 23 µA |

Fixing bug #23 relaxes this requirement 7.5×, so the two are best done together.

**Verification:** re-run the sweep with `tools/vbat-cal` after fitting. A flat
implied ratio means the part is fine; drift means it is leaking. Do not take
this from a datasheet — the rig exists, and the measurement takes ten minutes.

---

### Bug #23 — Remote VBAT divider has no ADC headroom (2026-08-19, OPEN)

**Problem.** The specified 2.8:1 divider (18 kΩ + 10 kΩ) puts a fully charged
2S pack at **3000 mV on the ADC pin — 97 % of the ADC's usable ceiling** of
3160 mV. FSD §5.5 states this outright: "0–3.0 V ADC range for 0–8.4 V
battery". The ESP32-S3's 12 dB attenuator departs from linear above roughly
3.0 V, so the top of the normal operating range sits in the compression zone.

**Measured effect.** After bug #21 was fixed, the implied ratio still declines
from 2.8469 at 4.94 V to 2.8024 at 8.56 V — a 1.6 % drift that is entirely ADC
compression, not the resistors. It is the dominant remaining error, ~0.7 % at
full charge.

**Not safety-critical.** The arming thresholds (6400/6600/7000) sit at 71-78 %
of the ADC range, comfortably linear, and all three under-read slightly so
protection trips early. What suffers is the **display gauge** near full charge.

**Fix.** Rescale to **3.0 kΩ / 1.2 kΩ**: ratio 3.50, a full pack at 2400 mV
(76 % of ceiling), Thevenin impedance 857 Ω, draw 2.0 mA — negligible on a
2200 mAh pack (over 1000 hours). The lower impedance also relaxes the bug #22
clamp-leakage requirement 7.5×.

**On adoption:** `REMOTE_VBAT_DIVIDER_RATIO` becomes ~3.5 and **this
calibration must be re-run** — the current 2.8211 is specific to the present
resistors.

**The base has the same class of problem** (a full 3S pack sits at 92 % of its
ceiling, ~0.7 % error) and would benefit from ~5.5:1. Not separately tracked;
worth doing when the base is next open for the bug #18 clamps.

---

### Bug #21 — Remote VBAT sense is non-linear (2026-08-19, OPEN)

**Symptom.** Calibrating the remote's divider against a DVM (5.33-8.57 V) gives
an implied ratio that drifts from **3.08 to 4.01** — 30 % across the sweep —
instead of the constant 2.8 a resistive divider must produce.

| V_true | pin mV (via adc_cali) | implied ratio | firmware would report | error |
|---|---|---|---|---|
| 5.33 | 1729 | 3.08 | 4843 | −9.1 % |
| 5.79 | 1815 | 3.19 | 5082 | −12.2 % |
| 6.27 | 1891 | 3.32 | 5296 | −15.5 % |
| 6.77 | 1960 | 3.45 | 5488 | −18.9 % |
| 7.56 | 2045 | 3.70 | 5726 | −24.3 % |
| 8.00 | 2087 | 3.83 | 5842 | −27.0 % |
| 8.57 | 2136 | 4.01 | 5979 | −30.2 % |

**This cannot be a resistor-value error.** A resistive divider is linear; no
combination of values produces a drifting ratio. Something non-linear is loading
the sense node, or the ADC input itself is damaged. The pin voltage falls
progressively short of an ideal 2.8:1 divider — by 174 mV at 5.33 V, rising to
925 mV at 8.57 V — which is the signature of a clamp conducting harder as the
node rises.

**Consequence — showstopper for field use.** With the FSD §5.6.2 production
thresholds (7000 / 6600 / 6400), **every voltage across the whole 2S range reads
below CRITICAL**. A freshly charged pack would put the remote straight into
STATE_ERROR at boot. This is precisely why the thresholds must not be restored
until the sense circuit is fixed.

**Resolves an earlier misreading.** On 2026-08-19 the remote reported
`vbat=5740 mV` and the pack was suspected of being over-discharged. Working that
back through this calibration puts the true pack voltage at **~7.6 V** — healthy.
The pack was fine; the sense circuit was lying. (Not yet proven that the fault
predates that reading — measuring the pack directly settles it.)

**ROOT CAUSE (confirmed 2026-08-19): a 3.3 V zener to ground on the ADC node.**
DVM on GPIO 1 read **2.10 V with 7.95 V in**, agreeing with the ADC's 2.087 V —
so the ESP32 is measuring correctly and the node really is being dragged down.
The board carries a 3.3 V zener from the ADC pin to ground.

Low-voltage zeners (< 5 V) break down by the Zener mechanism, which has a very
soft knee: they leak substantially below nominal Vz, unlike avalanche parts.
Back-calculated leakage against the divider's 6429 Ω Thevenin impedance:

| V_in | ideal node | actual | droop | zener current |
|---|---|---|---|---|
| 5.33 | 1904 | 1730 | 174 mV | 27 µA |
| 6.77 | 2418 | 1960 | 458 mV | 71 µA |
| 7.95 | 2839 | 2100 | 739 mV | 115 µA |
| 8.57 | 3061 | 2136 | 925 mV | 144 µA |

**The part is not faulty** — it is behaving as a 3.3 V zener does. This is a
design error, amplified by the high-impedance divider. It also explains why the
base is unaffected: the base's VBAT node has no zener, and its small residual
drift runs in the opposite direction (ADC compression, not clamp loading).

**Spec deviation.** FSD §5.5 specifies only "2.8:1 (18 kΩ + 10 kΩ)" for this
node — **no zener**. The clamp is an as-built addition absent from the spec, the
mirror image of bug #18, where the FSD specified zeners on base GPIO 21/42 that
were not fitted.

**Fix.**

1. *Essential:* replace the 3.3 V zener with a **BAT54 Schottky to the 3.3 V
   rail** (~1 µA leakage instead of 144 µA → ~6 mV droop). Clamping at ~3.6 V
   retains overvoltage protection; simply removing the zener would leave GPIO 1
   exposed to the >4.5 V fault the docs warn about. BAT54S is already the
   recommended part for base GPIO 21/42 protection.
2. *Strongly recommended alongside:* rescale the divider to **3.0 kΩ / 1.2 kΩ**
   (ratio 3.50, 2400 mV at 8.4 V = 76 % of the ADC ceiling, Thevenin 857 Ω,
   2.0 mA draw — negligible on a 2200 mAh pack). The FSD's own "0–3.0 V ADC
   range for 0–8.4 V battery" puts full charge at 95 % of the ceiling, the same
   headroom problem measured on the base.

If both are done, `REMOTE_VBAT_DIVIDER_RATIO` becomes 3.5 and this calibration
must be re-run. If only the zener is swapped, linearity is restored and the
remote inherits the base's ~0.7 % compression error at full charge.

**No calibration applied.** A non-linear fault cannot be corrected with a gain.

---

### Bug #20 — Shipped crypto keys are public; two of three comms layers are ineffective (2026-08-19, **RESOLVED 2026-08-27, fw 1.1.20**)

**Resolved 2026-08-27 (fw 1.1.20).** Keys rotated to random values and moved to
`components/rlc_common/include/rlc_secrets.h` — gitignored, mode 600, generated
by `./tools/gen-secrets.sh` from `/dev/urandom`. `rlc_config.h` has **no
fallback**: a build without real keys fails with an instructive `#error`,
because a silent fallback is precisely how ASCII placeholders survived to ship.

Leak prevention is **enforced, not intended**. `.gitignore` alone is bypassed by
`git add -f`, so a tracked pre-commit hook (`tools/git-hooks/pre-commit`,
enabled with `git config core.hooksPath tools/git-hooks`) refuses any commit
that stages the secrets file under any path, or that defines a key macro with
non-zero bytes in any file. Both leak routes were attempted and both refused.

Verified on target by the **integrity CRC self-test value**, which is derived
from the key: it changed `0xE74979F0` → `0x45222AE8`, and reads identically on
both units. That is the evidence, not the fact that they linked.

**The old keys remain permanently public** — in git history across many commits
on a public repo, very likely already cloned and cached. Rewriting history would
not reliably retract them. Rotation does not un-publish them; it makes them
irrelevant. Never reuse those values.

**Caught during this work: `build_base.sh` / `build_remote.sh` silently
swallowed flash failures.** esptool was piped into `tail -3`, so the pipeline's
exit status was tail's and a failed flash exited 0 with nothing alarming
printed. A serial-contention error left BOTH units on the previous firmware
while the scripts reported success — the stale build was only caught by reading
version banners off the devices. Now the flash output is captured, the exit
status checked, and a failure prints
`*** FLASH FAILED — <UNIT> IS STILL RUNNING ITS PREVIOUS FIRMWARE ***` and
exits 1. In a project whose safety rule is "flash both units together", a
silently-failed flash is the wrong thing to be quiet about.


**Problem.** `ESPNOW_PMK`, `ESPNOW_LMK` and `CMD_INTEGRITY_KEY` are compile-time
constants committed to `rlc_config.h`, and the repository is **public on
GitHub**. Anyone who reads the repo holds the keys.

FSD §6.2.1 calls ESP-NOW's AES-128-CCM "the system's **primary security boundary
against external adversaries**". With the PMK/LMK public, that boundary does not
exist against anyone who has seen the source. The same applies to the
application-layer CRC32 integrity check (§6.2.2), which is keyed with the
equally public `CMD_INTEGRITY_KEY` — it still detects accidental corruption, but
it does not detect forgery.

Of the three communication-security layers, only the third survives:

| Layer | FSD | Status with public keys |
|---|---|---|
| AES-128-CCM encryption (ESP-NOW) | §6.2.1 | **Ineffective** — PMK/LMK are public |
| CRC32-C integrity with pre-shared key | §6.2.2 | **Ineffective against forgery**; still catches corruption |
| Replay protection (session token + sequence) | §6.2.2 | **Effective** — the token is random per link-up, not compile-time |

Note the residual protection is meaningful: a random session token is generated
at each link-up, so a captured frame cannot simply be replayed into a later
session. The gap is *forgery* by someone who has read the repository, not replay.

**Also inaccurate:** FSD §6.2.1 says the PMK is "stored in `protocol_config.h`".
No such file exists — all three keys live in
`components/rlc_common/include/rlc_config.h`.

**Not a bench problem.** This has no effect on bench testing or on the current
test backlog. It matters before any field use where an adversary is in the
threat model.

**Fix (deferred — keys to be rotated later).** Options, roughly in order of
effort:

1. **Rotate the keys and keep them out of the repo.** Move the three constants
   to an untracked header (e.g. `rlc_secrets.h`, git-ignored) with a checked-in
   `rlc_secrets.h.example`. Cheapest change that closes the exposure; still
   requires recompiling and reflashing both units to change keys, per §6.2.1.
2. **NVS provisioning.** Store the keys in NVS, written once per unit at
   provisioning time. FSD v1.8 recorded compile-time keys as a known limitation
   and v1.14 explicitly walked that back ("compile-time keys are acceptable") —
   that judgement was made before the repository was public and should be
   revisited.

Rotating alone is not sufficient while the keys remain in tracked files: git
history preserves every previous value, so a rotated key committed to the repo
is public the moment it is pushed.

---

### Bug #24 — Base ESP32 chip #3 destroyed by floating 3.3 V rail (2026-08-20, chip replaced / protection OPEN)

**Incident.** During bench work the base's ground connection was accidentally
disconnected. With no ground return the 3.3 V regulator lost its reference and
the rail floated to **3.68 V** — above the ESP32-S3 absolute maximum VDD of
3.6 V. Chip #3 (`44:1B:F6:D4:0D:68`) died: silent on the UART bootloader and
on native USB despite confirmed power and manual BOOT entry, while the CH340
COM bridge still enumerated fine (i.e. "no serial data received", not a sync
error — the chip's TX never moved).

**Diagnosis path.** Rail measured 3.68 V at both 3.3 V pins; manual
bootloader entry produced no response; native USB JTAG absent from the bus.
3.3 V present + over-spec + silent = dead chip, not dead regulator in the
no-output sense — the regulator was replaced and the rail verified at
**3.29 V** afterwards.

**Repair (2026-08-20).** The replacement board inserted turned out to be the
"retired" old remote board `5B5E042156` / MAC `44:1B:F6:81:F1:70`, retired in
July with a "SPI flash damaged" diagnosis (suspected reverse-polarity
battery). Bench retest disproved that: bootloader, flash ID (16 MB), flash
read, and a write+verify+erase cycle on a scratch sector at 0xFF0000 all
passed. The board was enrolled as **base chip #4**: `BASE_MAC_ADDR` updated
in `rlc_config.h`, `build_base.sh` default port updated, both units
reflashed, link verified (LINK_ACK token `0x9f673ef9`, both IDLE, rssi
−44/−52, base vbat 12.0 V, cont 0x0003, bug #18 gate active). This is the
third base ESP32 destroyed electrically (chip #1, #2 bug #18, #3 this bug) —
the base is 4-for-4 on chips consumed.

**Still open — protect the rail.** The chip swap fixes the symptom, not the
failure mode. Recommended, cheapest first:

1. **Secure the ground path** — screw terminals or keyed/locking connectors
   for power and ground so an accidental disconnect becomes physically hard.
2. **3.6 V zener clamp (or crowbar) across the 3.3 V rail** — a future rail
   float dumps into the clamp instead of the ESP32.
3. **eFuse on the input** (e.g. TPS2596 class: OVP + reverse polarity +
   current limit) — would have prevented all three base chip deaths, and
   covers the remote too if fitted there.

Note the remote's July "reverse-polarity battery" flash-damage diagnosis is
now also suspect (that board's flash benches healthy, bug #24 retest) — but
it was replaced under that diagnosis, so the record stands unless the board
misbehaves in service as chip #4.

**Also observed post-reflash (untracked):** both units emit a ~100 ms burst
of `task_wdt: esp_task_wdt_reset(): task not found` errors immediately after
boot, then run clean. Identical on both units, so pre-existing firmware
behaviour rather than hardware — worth a low-priority look.

---


---

## Phase 5 — Hardening and Final Testing

**FSD ref:** §4.3 Phase 5, §15 (Test Requirements)
**Status:** IN PROGRESS (since 2026-08-27) — the Phase Overview table had this
row at "NOT STARTED" long after work began; corrected 2026-08-28

### Phase 5 Development Tasks

| # | Task | FSD ref | Status |
|---|------|---------|--------|
| 1 | Complete test suite execution (all §15 tests) | §15 | TODO |
| 2 | Watchdog stress testing | §15.4 | TODO |
| 3 | Range testing (10 m, 50 m, 100 m, 200 m) | §15.1 | **200 m DONE** (2026-08-25, −93 dBm holding, base on the ground). 10/50/100 m and the drop-out point still TODO |
| 4 | Power consumption measurement | §14 | TODO |
| 5 | Edge case testing (rapid toggling, button mashing, power cycling) | §15 | **PART-RUN 2026-08-27 — found one safety defect.** E1 rapid arm/disarm cycling PASS, E2 fire-button mashing **FAILED (fired the channel)** → fixed in fw 1.1.29, retest PASS, E3 encoder during countdown PASS, E4 channel change while armed PASS. Still to run: power cycling *under load* (during FIRING), simultaneous-input cases (fire release + arm switch off together), and sustained repetition for drift |
| 6 | Documentation: build instructions, flash procedure, wiring diagram | §4.3 | TODO |
| 7 | Final version number setting | §4.3 | TODO |
| 8 | Rotate ESP-NOW/integrity keys and move them out of the tracked repo (bug #20) | §6.2.1, §6.2.2 | TODO |
| 9 | Runtime display health check (FSD §5.5.6): 5 s panel-ID re-read; display failure during ARMED/PRE_FIRE/FIRING → CMD_DISARM + ERROR | §5.5.6 | **DONE 2026-08-27 (fw 1.1.9)** — `display_health_check()` in `rlc_display.c`, run inside `display_task` so it is serialised with frame writes; SPI return codes counted (they were all discarded before); two consecutive bad reads required; failure posts `EVT_DISPLAY_FAULT`, and the remote FSM then ceases fire / disarms and latches ERROR from any state. Still to do on target: **T-S10b** — pull the display flex mid-session and confirm the disarm. |
| 10 | G3 test: two complete fire cycles per power-on (BF-01 regression — fire timer stop + checked `gptimer_start`) | §15.3 | **DONE 2026-08-27 — host AND on target.** Two full arm→fire→complete cycles on one power cycle into a 12 V 50 W halogen on ch 1, fw 1.1.18. **0 reboots**, uptime continuous 451744 → 541944 ms across both, finished `state=1 IDLE err=0x00`. Cycles timing-identical (5000 ms countdown, 1050 ms pulse, ~2040 ms cooldown) — that symmetry is the proof, since it is exactly what the bug prevented. **The one-launch-per-power-cycle restriction is lifted.** See the section below. |
| 11 | FSM host event-injection harness (FSD §4.5) | §4.5, §15.5 | **DONE 2026-08-27** — `tests/host/test_base_fsm.c` (111 checks) drives the production base FSM. Discharges the review-substitute for T-F06/F07/F09 and T-S12/S13, the host half of T-A05, the base half of T-U07, and gives bug #30 positive verification. **Remote FSM harness still TODO** — same technique, `rlc_remote_fsm.c`. |
| 12 | Host suite wired into the build (`build_base.sh` / `build_remote.sh` run `tests/host/run.sh` and refuse to build on failure) | §15.5 | **DONE 2026-08-27** — the runner existed but nothing invoked it. Still no CI runner; that remains TODO. |
| 13 | T-C06 replay tool: capture a real frame off the air and re-transmit it | §15.1 | TODO — the *rule* is now host-tested (`test_seqgap.c` T-U04) and the base emits NACK 0x08 rather than dropping silently, but no on-air capture/replay tool exists. |

### RESOLVED — Arming-sequence order enforced, fw 1.1.19 (2026-08-27)

Raised by the operator while T-S08 passed: the guards correctly *refuse*
out-of-order input, but they refuse it **silently**, so the operator learns
nothing about why nothing happened. The system should require the sequence to
be walked in order and say so when it is not:

1. **Fire button pressed with no arm active** → toast. Today the press is
   simply ignored, which is indistinguishable from a dead button — the exact
   failure mode §7.2.9a was written against ("a refusal that only beeps is
   indistinguishable from an input that never registered, and the natural
   response to apparent non-response is to try again").
2. **Arm key turned while the fire button or encoder is already held** → toast.
   The fresh-press rule already makes this safe (T-S04/T-S08 prove it), but the
   operator gets no indication that their held input is being discarded.
3. **Only this order may progress:** arm key ON → encoder held then released
   (long-press to arm) → fire button held.

4. **Extended after T-S04 (operator, same session): a held button should
   INTERRUPT the sequence, not merely be reported.** If the fire button is
   held at the moment of arming, the arm should be refused or aborted outright
   rather than proceeding into ARMED where the held input is silently
   discarded. Arming into a state where the operator is already pressing fire
   and nothing happens is the confusing case — better to refuse the arm and
   name the reason than to enter a state whose most obvious input is inert.

This is a usability-of-safety issue rather than a safety hole: the interlocks
work, and T-S04/T-S08 both confirm a button held through ARMED entry cannot
fire. What is missing is the operator feedback, and now also the active abort —
which is precisely the gap §7.2.9a closed for commands in 1.1.6 and for the
link handshake in 1.1.17. The input layer is the third place it was never
applied.

**Implemented in fw 1.1.19 and verified on target:**

```
656857  arm switch ON with fire button held    -> RELEASE FIRE BUTTON FIRST
662167  arm switch ON with encoder held        -> RELEASE ENCODER FIRST
668947  FIRE pressed while not armed — refused -> NOT ARMED - ARM FIRST
670287  ARM rejected: fire button already held -> RELEASE FIRE BUTTON FIRST
```

The last is the behavioural change: the long-press is consumed and refused
rather than acted on. Previously it armed the base with the operator already
holding fire, and the held press then did nothing.

**The arm switch is deliberately not forced off on a bad sequence.** It is a
physical switch the firmware cannot move, so clearing its state internally
would put the display out of step with the panel in front of the operator. The
refusal that carries the safety weight is the one on the ARM.

`encoder_button_is_pressed()` added to expose state the encoder already
tracked. The checks live in the remote FSM's STATE_IDLE handler, alongside the
existing `ARM rejected: arm switch OFF` guard — which the same capture shows
still working, so the new guards join a family rather than replacing one.

### Bug #31 Two-Cycle Regression — PASSED ON TARGET (2026-08-27)

The gating item for the one-launch-per-power-cycle restriction. Bug #31 left the
fire GPTimer running after a completed pulse, so the *second* `fire_timer_start()`
of a power cycle hit `ESP_ERROR_CHECK` and panic-rebooted the base **with the arm
relay and the channel relay still energised** — the igniter carrying full current
for the whole panic-and-reboot interval. It had never been hit because no test had
ever completed a pulse and re-armed on one boot.

**It does not need pyrotechnics.** `POST_FIRE → IDLE` is a pure
`POST_FIRE_COOLDOWN_MS` timer with no continuity condition, so the second arm is
available whether or not the load burned through. The defect is in the timer, not
the igniter. Run into a **12 V 50 W halogen** on channel 1 — ~4.2 A hot against a
20 A contact rating, and switching a 12 V halogen is the duty these automotive
relays were designed for. The lamp also gives visible per-pulse confirmation,
which an igniter cannot (it only fires once).

Firmware 1.1.18, base uptime continuous throughout:

```
cycle 1   467644  IDLE -> ARMED (ch 1, sense verified)
          472764  ARMED -> PRE_FIRE
          477764  Fire timer started: ch 1, 1000 ms      (5000 ms countdown)
          477764  PRE_FIRE -> FIRING
          478814  Fire timer stopped                      (1050 ms pulse)
          478834  FIRING -> POST_FIRE
          480874  POST_FIRE -> IDLE                       (2040 ms cooldown)

cycle 2   527304  IDLE -> ARMED (ch 1, sense verified)
          528164  ARMED -> PRE_FIRE
          533164  Fire timer started: ch 1, 1000 ms
          533164  PRE_FIRE -> FIRING
          534214  Fire timer stopped
          534234  FIRING -> POST_FIRE
          536274  POST_FIRE -> IDLE
```

**0 reboots in the capture**, final state `state=1 IDLE err=0x00`. The two cycles
are timing-identical, which is the substance of the result: the second behaved
exactly like the first.

**Cosmetic finding.** Cycle 1 logs `E (477764) gptimer: gptimer_stop(418): timer
is not running` immediately before the timer starts — the fix's defensive
stop-before-every-start, firing on the first pulse of a power cycle when there is
genuinely nothing to stop. It does not appear on cycle 2, because by then the
completion handler has left the timer in a stoppable state. Harmless, but it
prints at **ERROR** level on every first shot, which will send someone hunting a
fault that is not there. Worth guarding with a "has ever been started" flag or
demoting the log.

### Fire Tests T-F03 / T-F08, and a Third Bug #31 Exit Path (2026-08-27)

Run on fw 1.1.19 into the 12 V 50 W halogen on channel 1. T-F03 was produced by
accident — the operator released the fire button early on the first attempt —
which is a better test than a planned one, because the release landed mid-pulse
rather than at a rehearsed moment.

```
T-F03   843847  Fire timer started: ch 1, 1000 ms
        843847  PRE_FIRE -> FIRING (ch 1)
        844387  Fire timer stopped               <- 540 ms of 1000 ms
        844407  FIRING -> IDLE (CEASE_FIRE)      <- IDLE, not POST_FIRE
        844557  arm sense changed: DISARMED

T-F08   854907  Fire timer started: ch 1, 1000 ms
        855957  Fire timer stopped               <- 1050 ms
        855977  FIRING -> POST_FIRE
        858017  POST_FIRE -> IDLE                <- 2040 ms cooldown
```

**Bug #31 coverage extended.** `Fire timer stopped` appears on the **cease-fire**
path as well as on completion. The two-cycle regression proved the completion
path releases the timer; this shows the ceased path does too, so a cease-fire
followed by another arm+fire cannot hit BF-01 either. That is a second exit path
covered, and it came free.

**Three pulses on one power cycle** across this capture (two completed, one
ceased), 0 reboots, 0 panics, 0 watchdog events, finishing `state=1 IDLE
err=0x00` at uptime 912707. That exceeds the two-cycle requirement of Phase 5
task 10 — under bug #31 the second start would have panicked with the relays
energised.

### All Eight Channels Fired — halogen substitute (2026-08-27)

Channels 2–8 had never been fired. Run with a single 12 V 50 W halogen moved
channel to channel, fw 1.1.27.

```
ch 1  366344 ARMED  367194 PRE_FIRE  372194 FIRING  373254 POST_FIRE  375274 IDLE
ch 2  389544 ARMED  390604 PRE_FIRE  395644 FIRING  396714 POST_FIRE  398724 IDLE
ch 3  409464 ARMED  410444 PRE_FIRE  415484 FIRING  416554 POST_FIRE  418594 IDLE
ch 4  432454 ARMED  433334 PRE_FIRE  438324 FIRING  439354 POST_FIRE  441374 IDLE
ch 5  452914 ARMED  454584 PRE_FIRE  459574 FIRING  460644 POST_FIRE  462674 IDLE
ch 6  473994 ARMED  475084 PRE_FIRE  480124 FIRING  481194 POST_FIRE  483224 IDLE
ch 7  495694 ARMED  496604 PRE_FIRE  501594 FIRING  502664 POST_FIRE  504674 IDLE
ch 8  515674 ARMED  516294 PRE_FIRE  521334 FIRING  521804 CEASE_FIRE (470 ms)
      525694 ARMED  526624 PRE_FIRE  531614 FIRING  532684 POST_FIRE  534724 IDLE
```

Every arm was **sense-verified** and every `Fire timer started` named the
selected channel. **9 pulses on one power cycle**, 0 reboots, 0 panics, 0
watchdog events, uptime continuous 331584 → 582104 ms, finishing `state=1 IDLE
err=0x00` with the battery essentially unchanged (11473 → 11464 mV).

**The mapping is proven, not assumed.** Only the channel carrying the lamp reads
CONNECTED, and arming requires continuity — so the lamp lighting on the selected
channel is end-to-end proof of the channel-to-relay mapping for all eight. A
crossed relay would have shown as nothing lighting.

**Channel 8's first attempt was an early release**, not a fault: `FIRING -> IDLE
(CEASE_FIRE)` at 470 ms with the remote's `type=0x23` cease-fire ACK. Under
fw 1.1.24+ that toasts `CH 8 PULSE CUT SHORT`, so it was visible at the time.

**Incidentally the strongest bug #31 evidence yet.** Phase 5 task 10 required
two cycles per power-on; this did nine, mixing completed pulses with a
cease-fire, on a single boot.

**Channel 3's MARGINAL reading was a 68 Ω resistor** fitted as an igniter
surrogate — not a fault, and not the leakage path first suspected. Back-
calculating `1123/269000/MARG` through `V = 3.3*Rx/(3300+Rx)` and subtracting the
217 Ω sense-branch resistor gives ~76 Ω, within ~12% of the actual part, so the
divider model is sound; the reading was read correctly and attributed wrongly.
Worth recording anyway for the band behaviour it demonstrates: at 268–270 mV it
sat 7–9 mV above the 261000 µV CONNECTED/MARGINAL boundary, and **MARGINAL does
not block arming — only OPEN does.** A 68 Ω channel would arm, fire, and report
a successful sequence while delivering ~160 mA, nowhere near enough to fire
anything. With a real igniter present its 1.5 Ω dominates and reads CONNECTED,
so this only matters when the igniter is missing — which is exactly when a
confident arm is least welcome.

**Still not fired into a real igniter.** A lamp does not burn through, so T-S19
and the green `OPEN - LIKELY FIRED` path remain unverified.

### Fault-Injection Keys for T-S05/S07/S09/S15/S16 (2026-08-27)

Five §15.4 tests were unreachable from outside the firmware. Keys added to the
two consoles; **all guarded by the existing Kconfig options, so the stock
binaries are unchanged** and no version bump was warranted.

| Key | Console | Test | Why it cannot be induced otherwise |
|---|---|---|---|
| `g` | base | T-S15, T-S16 | Forces `rlc_link_is_healthy()` false. The real condition is >30% loss over a 10-sample ping window; RF shielding produces that only approximately and **cannot time it**, and T-S16 needs the degradation to land inside the 5 s pre-fire countdown |
| `x` | base | T-S07 | Hangs the **FSM task** without feeding the TWDT. Deliberately that task, because it is the one the watchdog covers — spinning anywhere else would prove nothing about coverage of the safety state machine |
| `c` | base | T-S05 | Flips one bit of the next outgoing command *after* `rlc_msg_build()` computes its integrity CRC, so the frame is well-formed but must be rejected (App D.3 → NACK `0x06`) |
| `l` | remote | T-S09 | Sends a LINK_REQUEST while linked. `tick_remote()` only sends one in LINKING or LOST, and rebooting to force the issue takes ~1.9 s — by which time the base has hit link loss at 1.5 s and disarmed |

**The shared hooks live in `rlc_common/rlc_link.c`, not in either unit's
injection module.** The state they manipulate is inside the link layer, and
reaching into it from `rlc_base`/`rlc_remote` would invert the dependency.
Guarded by `CONFIG_RLC_FAULT_INJECTION || CONFIG_RLC_REMOTE_FAULT_INJECTION`.

**T-S05 corrupts a command rather than a status frame** on purpose: the command
path is the one with a guard worth testing, and silently dropping a bad command
was CM-02's finding in the 1.1.9 review.

Not yet run — the keys are the deliverable here. T-S15/S16 and T-S09 need an
operator to arm; T-S07 reboots the base by design and its second criterion (all
relays de-energised after the reboot) is the half that matters.

### T-S15 / T-S16 — degraded-link guards verified (2026-08-27)

Both run with the base `g` injection, which forces `rlc_link_is_healthy()`
false. The real condition is >30% loss over a 10-sample ping window — RF
shielding produces that only approximately and **cannot time it**, and T-S16
needs the degradation to land inside the 5 s countdown.

**T-S15** — forced degraded, then a normal arm attempt:
`NACK sent: type=0x20 reason=0x0d (COMM DEGRADED)`.

**T-S16** — the injection was fired **automatically** on the base logging
`ARMED -> PRE_FIRE`, landing 40 ms into the countdown, rather than timed by
hand:

```
327358  ARMED -> PRE_FIRE (ch 8)
        >>> degraded link injected
332348  PRE_FIRE comm degraded — abort        <- guard 4, rlc_base_fsm.c:908
332378  DISARMED -> IDLE
332528  arm sense changed: DISARMED
```

0 `Fire timer started`, operator confirmed no fire, abort at 4990 ms — the
PRE_FIRE→FIRING boundary.

**The first T-S16 run produced the right outcome but not the proof.** Its
capture dropped most of its lines (44 total, timestamps jumping 109658 →
114678 → 116288) including the guard's W-level message, leaving the identity of
the aborting guard established only by elimination: it went to IDLE rather than
LINK_LOST, ruling out guard 2, and the operator held the button with CMD_FIRE
repeats flowing, ruling out the dead-man. That is weaker than a log line for a
fire-path interlock, so it was re-run to capture it. Worth remembering that the
serial capture drops data often enough to lose a single decisive line.

### T-S05 / T-S09 — verified with the new injection keys (2026-08-27)

**T-S05.** The `c` key had to move to the **remote** console. The base does use
`rlc_link_send_cmd()`, but only for ACK and NACK — it never sends commands — and
the integrity CRC that produces NACK `0x06` is checked only on `CMD_*` frames
received by the base (`rlc_link.c`). The key as first committed was on the base,
which contradicted its own commit message; the base's copy is kept but
documented for what it actually does (corrupts an outgoing ACK/NACK, the reverse
direction).

```
573908  CMD integrity CRC mismatch (type 0x20)
580648  NACK sent: type=0x20 reason=0x0d (COMM DEGRADED)
```

The first line is the test passing. The second is the remote's automatic ARM
retry — the injection is one-shot, so the retry was clean — landing on a `g`
flag left set from T-S16 and being refused on guard 10 instead. That is why the
operator saw COMM DEGRADED rather than an integrity error: **setup error, not a
test failure**, and the log disambiguates the two.

**T-S09.** The `l` key was fired automatically 70 ms after the base logged
`IDLE -> ARMED`, from a script watching the base port and writing to the
remote's — the 10 s arm window is too tight to hand-time reliably.

```
2525808  IDLE -> ARMED (ch 8, sense verified)
2525878  LINK_REQUEST from remote fw 1.1.27
2525878  LINK_REQUEST rejected by app-state guard (busy)
2525878  LINK_REJECT sent, reason=0x02
2535818  ARM TIMEOUT (10009 ms) — auto-disarm
```

**The criterion is proven by absence:** no LINK_ACK, no new session token, no
dropped arm. The base stayed ARMED and ran its full timeout. Also confirms
`LINK_REJECT_BUSY` end to end — the 1.1.17 behaviour that made the FSD's
"silently ignores" wording obsolete.

### T-S07 — watchdog reboot verified, and a diagnostic trap (2026-08-27)

Run with the base `x` key, which hangs `bfsm_task` without feeding the TWDT.

```
2685038  INJECT: hanging FSM task — watchdog should reboot within 5 s
2689288  Task watchdog got triggered. The following tasks/users did not reset
2689288   - battery_task (CPU 0)
2689288  Tasks currently running:
2689288  CPU 0: bfsm_task
         rst:0xc (RTC_SW_CPU_RST)
```

**4250 ms** from hang to reset, inside the 5 s requirement. Post-boot:
`state=1 armed=0 firing=0 arm=0 err=0x00`, with continuity reading normally on
all channels — that last part is the evidence the **channel** relays are
de-energised, since continuity sensing only works with them in NC. `arm=0` is
the arm relay.

**The watchdog output names the wrong task, and it is worth knowing.** The
headline blames `battery_task` — but that is the *victim*: it shares CPU 0 with
the spinning `bfsm_task` and was starved of the time it needed to feed. The
culprit appears only on the "Tasks currently running" line. Anyone reading a
real watchdog trip on this unit and chasing `battery_task` would be debugging
the wrong task. This is inherent to how the TWDT reports, not a firmware
defect — but the trip that finally matters in the field will look exactly like
this one.

The injection deliberately hangs the FSM task rather than any other, because
that is the task the TWDT is meant to cover for the safety state machine. It
also, as it turns out, demonstrates that a hang there takes down a co-scheduled
task's watchdog feed first.

### Boot Display Health Check Hardened — fw 1.1.28 (2026-08-27)

Found while working out what a harness could substitute for **T-S10**, which is
not runnable here (soldered display). Reading the code the test targets found
two defects a real MOSI break would have walked straight through — a better
outcome than running the test would have been.

1. **The boot read discarded the SPI transaction status.** §5.5.6 requires it to
   be checked: *"a health check that succeeds only because the SPI layer
   swallowed an error is not a health check."* The **periodic** check has done
   so since 1.1.9; the **boot** read never did. It now snapshots `s_spi_errors`
   around the read, the same pattern the periodic check already uses.

2. **The test was `s_panel_id != 0`.** A broken MOSI leaves the panel with no
   command to answer and MISO undriven, which reads `0x00000000` (caught) or
   floats to `0xFFFFFFFF` (**not** caught). The remote would boot believing a
   dead panel healthy — and every screen after that is a lie, including ARMED.
   Both undriven signatures are now rejected.

**§5.5.6 contradicted itself and the firmware implemented the weaker clause:**
*"any non-zero read-back is considered valid"* against *"only a zero or
**garbage** read-back … is treated as a fault"* — all-ones is both garbage and
non-zero. Spec corrected (v1.45) to require rejecting both signatures and
checking the SPI status.

Verified on target: the real panel still reports
`ILI9488 init: … ID 0x2A403300 (healthy)`, so the tightened check does not
reject the clone ID this hardware actually uses.

### Edge-Case Testing — E2 defeated the dead-man (2026-08-27)

Phase 5 task 5, aimed at known seams rather than random mashing.

| | Case | Result |
|---|---|---|
| E1 | Rapid arm/disarm cycling, faster than the 500 ms weld hysteresis | PASS — no false `RELAY WELDED`, no stuck ARMED |
| E2 | Fire-button mashing while ARMED | **FAILED — fired the channel.** See below |
| E3 | Encoder rotation during the countdown | PASS — immediate abort, no pulse |
| E4 | Channel change while ARMED | PASS — disarmed, did not re-arm on ch 2's MARGINAL surrogate |

#### E2 — mashing the fire button fired the channel

```
655397  ARMED -> PRE_FIRE (ch 1)
660437  PRE_FIRE -> FIRING (local countdown elapsed)   <- full 5040 ms, no abort
660917  Fire button released — CEASE_FIRE
```

Other attempts in the same run aborted correctly (`Fire button released during
PRE_FIRE — abort` at 645787, 652757, 675397), so this was not a mis-run: on that
attempt the FSM never saw a release across five seconds of mashing.

**Mechanism.** The fire button used symmetric `DEBOUNCE_8BIT` at a 10 ms poll,
so a release was only reported after **80 ms of continuous release**. Mash faster
and the shift register never reaches all-high: no release is reported, the FSM
sees a continuous hold, `CMD_FIRE` repeats keep flowing, and **both dead-man
layers stay satisfied** — the remote's release detection and the base's
`FIRE_AUTHORIZATION_TIMEOUT_MS` both sit downstream of that one decision, so
neither can catch it.

**Not only about deliberate mashing.** A worn or chattering contact produces the
identical signal, as would a shaking hand. The operator would believe they were
not holding the button while the system fired. §1's premise is *"releasing the
button at any point cuts current — a dead-man switch, not a latch"*.

**The underlying error was symmetry.** For a dead-man the directions have
opposite consequences: a missed release fires an igniter the operator has let go
of; a spurious release only aborts, which is the direction that cuts current.
Demanding equal evidence for both makes the system exactly as reluctant to stop
as to start.

**Faster polling was considered and rejected.** It narrows the blind window
without closing it, and 8 samples at 1 ms is 8 ms — inside typical bounce
duration (1-10 ms) — so it would erode the bounce rejection debouncing exists
for, in *both* directions, while costing 10x the polling.

**Fixed in fw 1.1.29** by opt-in `rlc_debounce_set_fast_release()`: press keeps
8 samples (80 ms), release needs 2 (20 ms), which sits between bounce (1-10 ms,
rejected) and a human release (30-80 ms, caught). Other consumers keep symmetric
debouncing, correct for a sensor. Pinned by `test_debounce.c` T-D07/T-D08, and
**the test was verified to FAIL against the old behaviour** rather than merely
passing alongside it.

**Retest, logged (2026-08-27).** The first retest rested on operator observation
because the captures had been killed to free the ports for flashing; re-run with
both units logging. Six separate mashing bursts, every one aborted, **0
`Fire timer started` on the base**:

```
494597 ARMED -> PRE_FIRE   494667 released during PRE_FIRE — abort   (70 ms)
497577 ARMED -> PRE_FIRE   497597 abort                              (20 ms)
500497 ARMED -> PRE_FIRE   500547 abort                              (50 ms)
504637 ARMED -> PRE_FIRE   504657 abort                              (20 ms)
507937 ARMED -> PRE_FIRE   507957 abort                              (20 ms)
511477 ARMED -> PRE_FIRE   511497 abort                              (20 ms)
```

The abort latencies — mostly **20 ms, exactly the 2-sample threshold** — are the
fix visible in the timing. The same releases previously produced nothing at all;
the defect capture shows PRE_FIRE at 655397 surviving a full 5040 ms. E1 re-run
alongside: a dozen rapid arm/disarm cycles, clean `DISARMED -> IDLE` each, no
false `RELAY WELDED`.

**Incidental, not a defect:** two `ARM NACK: 0x04 (NO CONTINUITY)` appeared
during rapid cycling on a channel that reads fine at rest. Continuity sensing
only works with the channel relay in NC and there is a 50 ms settling delay
(`CONT_RELAY_DROPOUT_MS`), so re-arming faster than that can catch a reading
that has not re-settled and the base refuses — the safe direction. It means very
rapid re-arming can occasionally be rejected with no cause the operator can see.

### Phase 5 Review Fixes Applied — fw 1.1.30 (2026-08-28)

`Code_Review_Phase5_20260828_0641.md` (RLC-REVIEW-ALL-009, verdict MAYBE) found
one Critical and six Majors, all of them in the **operator-information layer on
the fire path** — nothing that energises a relay or extends a pulse, but the
remote could be silent or wrong about a live pad. All seven are fixed, plus ten
of the minors and three of the info items. Both builds clean, all host tests
pass (`tests/host/run.sh`, including the base FSM injection harness).

**CRIT-01 — the buzzer nudge silenced the alarms.**
`buzzer_set_background()` ended with `buzzer_play(BUZZER_OFF)` so a state-tone
change took effect at once. `buzzer_play()` is an atomic overwrite of a
one-deep mailbox (the RM-05 fix), and `check_timers()` sets the background from
the state on every 50 ms tick — so any handler that beeped *and* left
ARMED/PRE_FIRE/FIRING had its pattern destroyed microseconds later, before the
player task could dequeue it. `ALARM_LINK_LOST` on link loss while armed,
`ALARM_CRITICAL` on battery/display-fault/multi-arm, and every FIRE-guard
refusal triple were inaudible. The two fixes that made it (RM-05's atomic
mailbox, 1.1.27's state tones) were each right on their own; the interaction
was not. The player task now *polls* the background between pattern slices
(≤20 ms) and on a 100 ms idle wait, so a background change never touches the
mailbox at all. A repeating alarm now survives a background change, which is
what an alarm is for.

**MAJ-01 — "IGNITION ACTIVE" over a base in terminal ERROR.** The remote's
FIRING status handler was a whitelist (`POST_FIRE || IDLE`). A base that
latched ERROR mid-pulse — arm-relay weld fault, say — keeps sending
STATUS_UPDATEs, so the frames stayed fresh, the stale backstop never ran, and
the remote sat in FIRING indefinitely repeating CMD_FIRE at 5 Hz. Now a
blacklist, matching PRE_FIRE, with ERROR/LINK_LOST reported as base faults
rather than as a cut-short pulse.

**MAJ-02 — FIRE COMPLETE for a channel that never fired.** Completion was
`base_state == POST_FIRE || local_elapsed >= FIRE_PULSE_DURATION_MS`, and the
local clock runs from the *remote's* FIRING entry — its own countdown expiring,
which says nothing about the pad. A base abort during the 5 s countdown plus
one lost STATUS_UPDATE was enough to certify a never-energised channel. A new
`s_base_reached_firing` latch (set when a status actually shows the base in
FIRING — the base pushes one on entry) now gates both the completion backstop
and the "cut short" wording; without it the outcome is reported as
`CH n ENDED - NOT CONFIRMED`, which asserts nothing.

**MAJ-03 — the NACKs were being thrown away.** §8.4 forbids the base answering
a CMD_FIRE while it is on the firing path, so a NACK for a repeat can only mean
it has left — within ~200 ms, against up to 2 s for a status poll. PRE_FIRE and
FIRING now handle `EVT_CMD_NACK` for MSG_CMD_FIRE and end the sequence, which
collapses the windows MAJ-01 and MAJ-02 live in.

**MAJ-04/05 — screen precedence.** The 10 s boot splash outranked every FSM
state, so the remote could sit in ARMED under "Connecting to base" for ~9 s;
the FIRE COMPLETE hold outranked LINK_LOST, showing a green success screen over
a declared-dead link. One `live_state`/`alarmed_state` predicate now gates both
(FSD §10.2.1, §10.2.4a updated to match).

**MAJ-06 + minors.** Missing beeps added to the three refusal paths that only
showed a message ("TURN ARM KEY FIRST", "ARM CANCELLED", "BASE ENDED
SEQUENCE"); the buzzer is initialised *before* the boot display check so a dead
panel can be reported audibly (MIN-11); the FIRE ACK channel mismatch (MIN-05)
and a key-off ARM abort (MIN-09) are named instead of blamed on the link; a
second CMD_ARM inside the arm-verify window is NACK'd, first-ARM-wins as §7.2.2
already required (MIN-01); `EVT_LINK_RECOVERED` handled in base FIRING (MIN-03,
latent until `FIRE_PULSE_DURATION_MS` passes ~1.5 s); multi-flag error toasts
clamp to the worst flag plus a count instead of truncating mid-word (MIN-07,
new `rlc_error_flags_brief()` with host test T-E08); no continuity glyph beside
an unknown igniter band (MIN-08); `LINK WEAK` in the top bar so the display
agrees with the buzzer and the ARM guard (MIN-10); remote input events posted
with a 10 ms block and a log on drop instead of a zero timeout (MIN-06);
`s_channel` volatile in the encoder (INF-05). INF-01, INF-02 and INF-08 got the
comments they asked for.

**The three deferred minors — settled by operator decision, same day:**

- **MIN-02 — arm-verify timeout.** §7.2.2 said two contradictory things (step 2:
  "disarm and set `ERR_RELAY_FAULT`", which §13.1 makes terminal; guard-failure
  line: NACK and remain in IDLE) and the firmware did the second. Resolved as
  **two strikes**: the first timeout de-energises the relay, NACKs 0x0B and
  stays IDLE — retryable, because a 200 ms window over a 160 ms sense debounce
  leaves ~40 ms for the relay to operate (INF-01) and a slow relay is not
  necessarily a broken one — while `ARM_VERIFY_FAULT_STRIKES` (2) consecutive
  timeouts latch `ERR_RELAY_FAULT` and enter terminal ERROR. Any successful
  verify clears the count. Weld detection (§13.1 case b) is untouched: still
  terminal on first sight. Pinned by three new T-FSM02 cases (120 checks in the
  base FSM harness, up from 111).
- **MIN-04 — dropped command was silent on the wire.** The zero-timeout forward
  stays (it runs with the link state mutex held; blocking there would stall the
  receive path), but a drop now answers **NACK 0x0F `BASE_BUSY`** on the same
  locked path replay and CRC refusals already use. One wrinkle worth recording:
  the MAJ-03 fix treats a NACK for a repeated CMD_FIRE as "the base left the
  firing path", which is wrong for this reason code — a refused frame is not a
  departure, and the base is still counting its dead-man from the last repeat it
  took. Both PRE_FIRE and FIRING therefore ignore 0x0F specifically, or the new
  NACK would have introduced a false abort mid-pulse.
- **MIN-12 — guard-4 parity.** Documentation only, by decision: §8.2.3 guard 4
  and §8.2.4 guard 2 now describe the ≤30 %-over-10-pings rate test the code has
  always used, and say why it is sufficient — the ignition interlock is the
  base's own dead-man (500 ms) plus contact freshness (1000 ms) at
  PRE_FIRE→FIRING (§7.2.4), not this guard. The known consequence is recorded
  too: the remote may commit on a 4 s-old status while the base re-checks at 1 s,
  so a link that dies during the countdown gives a full 5 s count that then
  aborts at the base.

**Verified on target 2026-08-28 (fw 1.1.30, commit c7b7547)** — full detail in
`Test_Report_Phase5_Review_Fixes.md`. Eight on-target tests, 8 PASS, seven
corroborated by dual-console captures:

| ID | Test | Fix | Result |
|---|---|---|---|
| T-30-01 | Base power cut while ARMED | **CRIT-01** | PASS — continuous alarm through the 16 s outage, silent on recovery |
| T-30-02 | Long-press with arm switch OFF | MAJ-06 | PASS — triple beep + `TURN ARM KEY FIRST` |
| T-30-03 | Arm then disarm by encoder | CRIT-01 | PASS — heartbeat, then the long disarm beep survives the background change |
| T-30-04 | State tone tempo | 1.1.27 regression | PASS — ARMED heartbeat vs ~4 Hz firing tone unaffected by the player rewrite |
| T-30-05 | Arm inside the 10 s splash hold | MAJ-04 | PASS (operator-observed; capture lost to a logger bug) |
| T-30-06 | Base key to SAFE mid-pulse | MAJ-02/03 | PASS — `CH 1 CUT SHORT - BASE KEY` **150 ms** after the key turn |
| T-30-07 | Clean full pulse | MAJ-02 regression | PASS — `Fire complete (base state=6, 1107 ms)` → FIRE COMPLETE |
| T-30-08 | Link lost inside the FIRE COMPLETE hold | MAJ-05 | PASS — green screen cancelled 4.2 s into the hold |

The CRIT-01 acceptance test is the one that matters: with ch1 armed, cutting
base power now produces a continuous audible alarm that stops on recovery. That
condition was completely silent in 1.1.29.

Incidental confirmation during setup: pulling the 3S pack while ARMED drove the
base through `Arm sense LOW during ARMED` → `-> ERROR (flags=0x02: VBAT
CRITICAL)`, terminal and surviving the pack's return, while the remote showed
`BASE DISARMED`, then a `BASE FAULT` status band, then refused a re-arm with
`BASE ERROR: VBAT CRITICAL` — the MIN-07 one-line brief rendering as designed.

**Still to do on target — all need fault-injection builds:**

- ~~**MAJ-01**~~ — **DONE 2026-08-28 (fw 1.1.34 injection, new base key `r`).**
  Posts a real `EVT_ARM_SENSE_FAULT` from any state, so the base reports a
  truthful ERROR instead of the `e` key's falsified IDLE. Auto-injected 13 ms
  into a live pulse: base latched `-> ERROR (flags=0x04: RELAY FAULT)` mid-FIRING
  and reported it; the remote left FIRING **16 ms** later via the 1.1.30
  blacklist — `[TOAST] BASE ERROR: RELAY FAULT`, band → RELAY WELDED. Exactly
  the scenario MAJ-01 was written for: no "IGNITION ACTIVE" assertion over a
  dead pulse.
- ~~**MAJ-02 false-completion**~~ — **effectively closed by MAJ-03.** With the
  status frame suppressed, the NACK ends the sequence within one repeat interval,
  so the remote never reaches the elapsed-time backstop that produced the false
  FIRE COMPLETE. The gate itself was observed refusing to over-claim in
  T-30-10B (`base state=4`, 793 ms, no FIRING status ever seen).

**New finding (MINOR) — the gate under-claims on a fast release.** Observed
live: the base entered FIRING and pushed its triggered status; the operator
released the fire button 190 ms later, before that frame arrived, so
`s_base_reached_firing` was still false and the remote reported
`CH 1 ENDED - NOT CONFIRMED` for a channel that had genuinely carried current
for ~200 ms. Under-claiming is the safe direction relative to the defect MAJ-02
fixed. **Operator decision 2026-08-28: reworded in fw 1.1.31** to
`CH n OUTCOME UNKNOWN - TREAT AS LIVE` — the remote's knowledge is unchanged, but
the operator is told what it means for them at the pad instead of having to
interpret "not confirmed", which reads too easily as "nothing happened". The
evidence gate itself is untouched, and deferring classification to let the
status land was rejected as timing complexity on a safety path. Recorded in
`Test_Report_Phase5_Review_Fixes.md` §4 finding 4 / §5 and FSD §8.2.6.
- ~~**CRIT-01 critical-error half**~~ — **DONE 2026-08-28, both keys, both PASS
  (audible).** `b` (battery-critical) injected automatically at ARMED entry:
  continuous urgent alarm from the ARMED→ERROR transition — completely silent in
  1.1.29, which is what CRIT-01 was about. `d` (display fault) from ARMED: same
  alarm, and the base disarmed immediately (`CMD_DISARM` ACKed **1 ms** after the
  remote entered ERROR, base `DISARMED -> IDLE` 13 ms after that).
  **The `b` run found a new defect:** the battery-critical path from ARMED did
  *not* disarm the base — the arm relay ran its full 10 s ARM TIMEOUT while the
  remote sat in terminal ERROR unable to command it safe. The display-fault
  handler sends `CMD_DISARM` for exactly this reason; the battery path never had
  it. **Fixed in fw 1.1.35** and re-verified: armed → safe in **26 ms** (was
  10 s), siren stopping immediately, operator-confirmed.
- ~~**MAJ-03 attribution**~~ — **DONE 2026-08-28.** Isolated with the base's `s`
  injection key: the abort's triggered status frame was suppressed, leaving the
  NACK as the only signal. Ended the sequence in **160 ms from PRE_FIRE**
  (`FIRE repeat NACKed (0x05) during PRE_FIRE`) and **200 ms from FIRING**
  (`FIRE repeat NACKed (0x05) — base left the firing path`, toast
  `CH 1 ENDED - NOT CONFIRMED`). Established in the process that MAJ-03 is a
  **backstop, not the primary detector** — `firing_exit()` calls
  `status_update_trigger()` on every FIRING exit, so in normal operation the
  status frame always wins (150 ms, measured). Base reflashed with a normal
  build and verified free of injection symbols afterwards.
- **MIN-10** — `LINK WEAK` needs ≥30 % ping loss; distance or shielding rather
  than injection.

**Tooling note:** the serial logger used for this session held stale file
handles across USB re-enumeration, silently capturing nothing after a unit
reset. It cost one test capture and produced one incorrect "this did not happen"
conclusion that had to be retracted. Fixed by reopening a port after 20 s
without data — worth carrying into any future bench session.

**Tooling (2026-08-28):** that fix is now a tool — `tools/serial_log.py`, a
timestamped dual-console logger (wall-clock stamps so base and remote captures
correlate offline; auto-reopen on USB re-enumeration). Its `--send-on
PATTERN:BYTES` rule auto-injects a fault key the instant a log pattern appears,
which is how this session hit the `r` key 13 ms into a pulse and `b` at ARMED
entry — windows too tight to hand-time. **Two incident notes for future bench
sessions:** (1) a stale logger holding a port breaks esptool *silently* — the
flash command can exit 0 through a pipe while pySerial threw "multiple access";
always redirect flash output to a file and echo `$?` yourself, and stop all
loggers before flashing. (2) immediately after a flash-reset boot the two units
once came up unlinked and stayed wedged (base BOOT, remote LINKING, zero link
traffic); a clean `esptool --after hard_reset chip_id` on both cleared it.
 Suspect the wedge whenever a post-flash boot shows no LINK traffic at all.

### Phase 5 FSD Safety Tests (§15.4)

| ID | Test | Status |
|----|------|--------|
| T-S01 | Power cycle base while armed → safe boot | **PASS** 2026-08-27 — booted IDLE with the key still in ARM, no auto-rearm, relays NC. Operator confirmed **no lamp flash** through boot, so no GPIO glitch on the channel drive (a free look at what T-S06 scopes). Also latched `ERR_VBAT_CRITICAL` at **7287 mV** on the way down as power was removed — correct, and a third independent pass of T-S03 |
| T-S02 | Power cycle remote while base armed → link loss disarm | **PASS** 2026-08-27 — `PING drought (1542 ms) — LINK LOST` then arm sense DISARMED 170 ms later; last PING to fire-path-dead **1712 ms**. Detection at 1542 ms vs the FSD's "within 1.5 s": the extra 42 ms is link-task tick granularity on a `>= 3 x 500 ms` threshold, not drift |
| T-S03 | Base battery below VBAT_CRITICAL_MV → ERROR state | **PASS** (2026-08-26) — taken to 7978 mV, latched `ERROR flags=0x02`, correctly stayed latched at 12.1–12.7 V until power cycle |
| T-S04 | Hold fire button at boot, then arm → no fire (fresh press) | **PASS** 2026-08-27 — button held through remote boot and through arming; no countdown, no pulse, lamp dark. The debouncer seeding at boot with the button already low does not read as authorisation |
| T-S05 | Corrupt message (bit flip) → rejected | **PASS** 2026-08-27 (remote `c` injection) — `CMD integrity CRC mismatch (type 0x20)` on the base, corrupted CMD_ARM rejected in the link layer before reaching the FSM, and the sequence number deliberately not advanced. The remote's *visible* toast that run was COMM DEGRADED, from the automatic retry landing on a leftover `g` flag — the retry was uncorrupted, since the injection is one-shot |
| T-S06 | GPIO init order verification (oscilloscope on boot) | **PARTIAL PASS** 2026-08-27 — no logic analyser available; run instead with the 12 V halogen on ch 1 across ~10 consecutive base power cycles. **No flicker on any cycle**, arm and fire relays solid throughout. This catches a relay actually pulling in (a sustained wrong gate level), which is the failure that matters; it cannot catch a microsecond gate transient, which is what the written criterion measures — though a relay armature has milliseconds of mechanical inertia and physically cannot respond to one. Recorded as PARTIAL, not a pass on the written criterion |
| T-S07 | Watchdog: infinite loop → reboot within 5 s | **PASS** 2026-08-27 (base `x` injection) — FSM task hung at 2685038, `Task watchdog got triggered` at 2689288 = **4250 ms**, `rst:0xc (RTC_SW_CPU_RST)`. Post-boot `state=1 armed=0 firing=0 arm=0 err=0x00` and continuity reading on all channels, which requires the channel relays in NC — both criteria met |
| T-S08 | Hold fire button + arm → no fire (fresh press required) | **PASS** 2026-08-27 — button held before and through ARMED entry; no fire. Release-then-press did start the countdown, confirming the `0xFF→0x00` transition is what authorises |
| T-S09 | LINK_REQUEST while ARMED → **rejected with `LINK_REJECT_BUSY`** (was "silently ignored" before fw 1.1.17); session and armed state unaffected | **PASS** 2026-08-27 (remote `l` injection, fired automatically 70 ms after the base logged ARMED) — `LINK_REQUEST from remote fw 1.1.27` → `rejected by app-state guard (busy)` → `LINK_REJECT sent, reason=0x02`. **The criterion is proven by what did not happen:** no LINK_ACK, no new session token, no dropped arm — the base stayed ARMED and ran its full `ARM TIMEOUT (10009 ms)`. A handshake attempt cannot reset the session out from under a live pad. ~~FSD §15.4 row still says "silently ignores" and needs updating~~ — **FSD corrected 2026-08-28 (v1.48)**: §6.4.1, §7.2.6 and the App D exception table now all state the `LINK_REJECT_BUSY` behaviour |
| T-S10 | Display SPI failure at boot → ERROR | TODO — **not runnable on this hardware**: the display is soldered, and unsoldering a working panel to run a test is a poor trade. An injection could only substitute for half of it (the boot-halt *response*), never for whether a real MOSI break is *detected*. **Investigating that half found two defects instead**, both fixed in fw 1.1.28 — see below. The response half is separately evidenced by the remote harness `d` key (T-S07/REMOTE FAULT work) |
| T-S11 | 5 consecutive send failures → immediate link loss | PASS | Triggered on-target during RF shielding test (RSSI -98 dBm) |
| T-S12 | Fire pulse on link loss (COMPLETE_PULSE=true) | TODO |
| T-S13 | Fire pulse on link loss (COMPLETE_PULSE=false) | TODO |
| T-S14 | Arm timeout (10 s auto-disarm) | **PASS** (2026-08-26) — `ARM TIMEOUT (10022 ms)` against a 10000 ms constant, observed repeatedly |
| T-S15 | ERR_COMM_DEGRADED blocks arming | **PASS** 2026-08-27 (base `g` injection) — `NACK sent: type=0x20 reason=0x0d (COMM DEGRADED)`, guard 10 at `rlc_base_fsm.c:335`. Remote named the reason rather than timing out |
| T-S16 | ERR_COMM_DEGRADED blocks firing | **PASS** 2026-08-27 (base `g` injected automatically 40 ms into the countdown) — `327358 ARMED->PRE_FIRE`, `332348 PRE_FIRE comm degraded — abort`, `332378 DISARMED->IDLE`, arm sense released 150 ms later. **0 `Fire timer started`**. Aborted at 4990 ms, the PRE_FIRE→FIRING boundary. The only guard that can stop a pulse already in progress, never previously exercised on hardware |
| T-S17 | Key switch sense verifies key switch (FSD wording; this row previously said "arm switch") | **PASS** 2026-08-27 — `key=0` in SAFE, `key=1` in ARM, both transitions clean; guard 1 then passed with two successful arms, each verified by arm sense 170 ms after the relay drive (`arm verify started` → `sense HIGH` → `sense verified`). Both auto-disarmed at exactly 10000 ms, re-confirming T-S14 |
| T-S18 | Key switch sense fault detection → NACK 0x01 (FSD wording) | TODO — **no injection substitutes for this one.** It tests a *hardware* property: with the internal pulls disabled (`rlc_arm_sense.c`), a broken sense wire is held at the safe level by the external divider's 100 kΩ leg to ground. LOW = key OFF (`key_on = !new_state`), so a break fails safe. Forcing `key_sense_get_debounced()` false would only re-test guard 1, which fires routinely already, and would prove nothing about the wire. Needs physical access; if run, **break the connection on the key-switch side of the divider**, not at the GPIO — disconnecting at the GPIO orphans the divider and leaves the pin genuinely floating, which tests something different and less safe |
| T-S19 | Post-fire igniter status via continuity | **PASS** — operator attestation 2026-08-27: burn-through was verified with a real igniter during earlier fire testing. Corroborated by T-A17 (2026-08-26), which records an igniter firing on this rig, though no post-fire continuity reading was logged at the time. Intact-load half independently re-confirmed 2026-08-27: the halogen reads `● STILL CONNECTED` after a pulse. **The operator-facing display half was added later (fw 1.1.22)** — the FIRE COMPLETE screen shows the fired channel's band live, `○ OPEN - LIKELY FIRED` / `▲ MARGINAL - CHECK` / `● STILL CONNECTED` / `IGNITER ?`; the green OPEN path has not itself been seen on the panel, since that needs a load that opens |

---

## Hardware Reference

| Item | Value |
|------|-------|
| Base MAC | `44:1B:F6:81:F1:70` (chip #4, 2026-08-20 — ex-remote #1 board, flash damage diagnosis disproved; #3 `44:1B:F6:D4:0D:68` destroyed by 3.68 V rail bug #24; #2 `44:1B:F6:81:FA:F8` & #1 `94:A9:90:31:18:38` destroyed) |
| Remote MAC | `AC:A7:04:E2:F2:8C` (chip #2; #1 `44:1B:F6:81:F1:70` now serving as base chip #4) |
| Base serial (COM port) | `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E042156-if00` (chip #4 board, 2026-08-20; the `5B5E044219` adapter belonged to the dead chip #3 board) |

> **By-id caveat.** A COM-port by-id identifies the **CH340 adapter on that board**, so it survives swapping the ESP32 *chip* — which is why it was chosen — but **not** swapping the whole board. Chip #4 is the ex-remote #1 board, so the base moved from `5B5E044219` to `5B5E042156` on 2026-08-20. Check `ls /dev/serial/by-id/` after any board change; flashing the wrong unit is otherwise silent.
| Remote serial (COM port) | `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E043219-if00` (verified 2026-08-19 by `read_mac` → `ac:a7:04:e2:f2:8c`; was `...5B5E042156` before the board swap) |
| ESP-IDF version | v5.4.1 |
| Target | ESP32-S3 (xtensa) |
| Flash size | 16 MB |
| PSRAM | 8 MB OCT |

## Firmware Version History

Both units must run the same MAJOR.MINOR.PATCH or they refuse to link, so every
bump means **flash base and remote together**. Full rationale for each entry is
in `components/rlc_common/include/rlc_version.h`; this table exists because
entries 1.1.3–1.1.7 were previously recorded only there and nowhere in this
document. Brought up to date through 1.2.0 on 2026-08-28 (the table had been
left at 1.1.9 — review finding INF-12).

| Version | Date | Unit(s) | Change |
|---|---|---|---||
| 1.2.3 | 2026-09-01 | base | **`SIREN_BOOT_TEST` — one-chirp boot-complete siren test** (FSD §12.2, v1.53, operator request). A single 200 ms siren blast at the end of a successful boot, so the operator hears the unit is up and the siren itself has just been exercised (its only previous sounds were fault and armed states — a dead driver could stay undetected until a pad warning was needed). Sounded only after every mandatory init step passes; a halted boot sounds `SIREN_ERROR` instead and never chirps. Amends the bug #27 "silent at power-on" retest property in wording only: that verified no *uncommanded* sound during the power-on transient, which is unchanged. Base-only, audible-only, no protocol change; **flash both units together**. ||
| 1.2.2 | 2026-09-01 | remote | **Main-screen continuity legend removed; status band enlarged** (FSD §10.2.2, v1.52, operator request). The legend row under the channel grid restated what every grid cell already shows — each cell draws the glyph *and* the band's name in the band's colour — so it carried no information the grid did not, while occupying 22 px of the system status band, whose whole purpose is legibility from across a launch site. On the main screen only, the band now starts at the legend's former row: 90 px tall against 68 on every other screen (whose centre box is pinned immediately above the standard band top by a compile-time assert and cannot move without shrinking). The main screen's two status rows are re-centred in the taller band. Remote-only, display-only, no protocol change; version bumped because a changed binary sharing a version number is what the strict check exists to prevent. **Both units flashed 1.2.2 and link verified 2026-09-01** (base UART capture: IDLE, rssi −33, no errors). |
| 1.2.1 | 2026-08-30 | remote | **Fault-injection boot banner** (FSD §10.2.1, v1.51). A remote built with `CONFIG_RLC_REMOTE_FAULT_INJECTION` now shows a red frame and a red `!! FAULT INJECTION BUILD / NOT SAFE FOR LIVE USE !!` block on the splash, displacing the club credit. The build already announced itself four ways, but all four are developer-facing — none was visible to an operator picking the remote up at a firing point, which is the person a build that lies about its own state must not mislead. Display-only, no protocol change; version bumped because a changed binary sharing a version number is what the strict check exists to prevent, so **flash both units together**. Known gap: a *base* built with `CONFIG_RLC_FAULT_INJECTION` cannot be signalled this way — the remote knows only its own build and the base does not advertise its own on the wire; closing that needs a protocol field and a decision. |
| 1.2.0 | 2026-08-28 | both | **FINAL — Phase 5 release.** Version-only bump over 1.1.35 (no code delta). Marks the close of the Phase 5 review round and both on-target campaigns; bug #29 regression suite complete — cleared for live fire. Final-build audit recorded with the tag: fault-injection consoles compile to nothing (options default n, absent from every sdkconfig, zero injection symbols in both stock ELFs), display-profile harness gone since 1.1.11, `CONT_TRACE_INTERVAL_MS 0` for field, hw-test projects outside the main build. Deferred past release: T-S12/S13, T-S18, T-C06, range/power measurements, remote FSM host harness, CI. |
| 1.1.35 | 2026-08-28 | remote | Battery-critical from ARMED now sends `CMD_DISARM` before entering ERROR, mirroring the display-fault handler. Found live during the CRIT-01 `b` retest: the base arm relay ran its full 10 s ARM TIMEOUT while the remote sat in terminal ERROR with no way to command the pad safe. Re-verified on target: armed → safe in **26 ms** (was 10 s). PRE_FIRE/FIRING were never exposed — their `CEASE_FIRE` makes the base disarm. |
| 1.1.34 | 2026-08-28 | remote | A base-aborted countdown names its cause. Found live during the bug #29 T-A17 retest: the NACK answering a fire repeat beat the cause-carrying STATUS_UPDATE by 7 ms and the remote toasted raw `[NACK] WRONG STATE`. The NACK path now says `BASE ENDED SEQUENCE` and latches the channel; the first status in IDLE settles it one-shot — channel disarmed + band OPEN → `CONTINUITY LOST - DISARMED` with `BEEP_CONTINUITY_LOST`. PRE_FIRE status-exit gains the same RM-07 discrimination. |
| 1.1.33 | 2026-08-28 | base | `gptimer_stop: timer is not running` false ERROR on the first shot of a power cycle silenced — the BF-01 defensive stop now only calls `gptimer_stop()` when a mirror of the driver's RUN state says it is running. The stop itself is unchanged when the timer IS running, so BF-01's protection is not weakened. |
| 1.1.32 | 2026-08-28 | remote | Finding 6: the ARM ACK-timeout branch now sends `CMD_DISARM` like every sibling failure path. A timeout is what a lost ACK looks like, so it was the branch most likely to have left the base armed while the remote sat in IDLE showing READY TO ARM — up to 2 s of live pad before the §8.2.3 reconciliation caught it. |
| 1.1.31 | 2026-08-28 | remote | Unconfirmed-outcome toast reworded to `CH n OUTCOME UNKNOWN - TREAT AS LIVE`. On-target testing showed the MAJ-02 evidence gate lands on the unknown case for any fire-button release within ~200 ms of ignition (the FIRING status has not arrived yet), where "NOT CONFIRMED" reads as "nothing happened". Wording only; gate untouched. |
| 1.1.30 | 2026-08-28 | both | Phase 5 review fix round (`Code_Review_Phase5_20260828_0641.md`, RLC-REVIEW-ALL-009). **CRIT-01** the buzzer background nudge no longer destroys a just-queued alarm — the link-lost and critical alarms were silent whenever the transition started in ARMED/PRE_FIRE/FIRING; **MAJ-01** remote FIRING syncs on any base state off the firing path (a base in terminal ERROR left it showing IGNITION ACTIVE); **MAJ-02** FIRE COMPLETE / cut-short need positive evidence the base reached FIRING; **MAJ-03** NACKs for repeated CMD_FIRE are heeded; **MAJ-04/05** splash and FIRE COMPLETE never cover a live or alarmed state; **MAJ-06** three refusal paths gained their missing beep; MIN-01/03/05/07/08/09/10/11 and INF-05. |
| 1.1.29 | 2026-08-27 | remote | **SAFETY DEFECT (E2).** Asymmetric dead-man debounce — 80 ms to press, 20 ms to release. Mashing the fire button used to fire the channel: symmetric 8-bit debouncing never reported a release, so both dead-man layers stayed satisfied. |
| 1.1.28 | 2026-08-27 | remote | Boot display health check rejects both undriven signatures (0x00000000/0xFFFFFFFF) and checks the SPI return status, as the periodic check has since 1.1.9. |
| 1.1.27 | 2026-08-27 | remote | Audible state tones: `ALARM_ARMED` (~0.8 Hz heartbeat) and `ALARM_FIRING` (~4 Hz), as backgrounds so one-shot beeps do not kill them. Cut-short discrimination for the FIRING→IDLE case. |
| 1.1.26 | 2026-08-27 | remote | Removed the 200 ms clock-skew slack that let 1.1.25 still call a base-side abort at 802 ms of a 1000 ms pulse "complete". |
| 1.1.25 | 2026-08-27 | remote | The remote no longer claims FIRE COMPLETE for a pulse the base cut short — both end with `base_state == IDLE`. |
| 1.1.24 | 2026-08-27 | remote | Cease-fire toasts: a cease-fire says the channel *was* energised (`CH n PULSE CUT SHORT` / `CH n CUT SHORT - ARM OFF`), which a silent return to IDLE could not distinguish from a countdown abort. |
| 1.1.23 | 2026-08-27 | remote | FIRE COMPLETE screen holds for 10 s (5 s was still too short to read the igniter line and act). |
| 1.1.22 | 2026-08-27 | remote | FIRE COMPLETE screen decoupled from `POST_FIRE_COOLDOWN_MS` (its own `FIRE_COMPLETE_SCREEN_MS`), plus the live igniter-continuity line. |
| 1.1.21 | 2026-08-27 | remote | Status band drawn only where it carries information. |
| 1.1.20 | 2026-08-27 | both | **Bug #20** — crypto keys rotated out of the repo into gitignored `rlc_secrets.h`, with a pre-commit guard. |
| 1.1.19 | 2026-08-27 | both | The arming sequence must be walked in order, and says so: ARM is refused while the fire button or encoder is already held. |
| 1.1.18 | 2026-08-27 | both | A firmware mismatch also reaches a remote running the older firmware. |
| 1.1.17 | 2026-08-27 | base | The base says why it refused a handshake (`LINK_REJECT_BUSY` instead of a silent drop) — T-S09. |
| 1.1.16 | 2026-08-27 | base | No more false RELAY WELDED on a normal disarm. |
| 1.1.15 | 2026-08-27 | remote | Status band no longer claims SAFE on a dead link. |
| 1.1.14 | 2026-08-27 | remote | The one-key / both-keys distinction made visible. |
| 1.1.13 | 2026-08-27 | remote | Status band separates one key turned from two. |
| 1.1.12 | 2026-08-27 | remote | System status band, and a background-fill fix. |
| 1.1.11 | 2026-08-27 | remote | Stock build — the T-D09 display profiling harness removed again. |
| 1.1.10 | 2026-08-27 | remote | Display refresh fix (T-D09 failed on target). |
| 1.1.9 | 2026-08-27 | both | Full-codebase review fix round (`Code_Review_AllPhases_20260827_0308.md`). **BF-01/bug #31 (CRITICAL)** fire-timer fix; BF-02 PRE_FIRE heartbeat-freshness as its own guard; BF-03 `SIREN_CONTINUITY_LOST`; BF-04/CI-05 latched boot-failure halts; BF-07 FSM queue before the arm-sense task; CM-01 link-state lock; CM-02 replay/CRC NACKs; CM-03 data-gap detection; CM-04 truncated ACK/NACK dropped; CM-05 seq 0 rejected; CM-06 unused esp-now dependency removed; **DS-01** runtime display health check; DS-02/03 display corrections; RM-01/02/03/05/06/07/09/11; CI-01 relay-settling delay; CI-02 `ERR_VBAT_LOW`; CI-04 `led_task` on the TWDT; buzzer task back to §9.10's priority 1 / core 1. |
| 1.1.8 | 2026-08-26 | base | Bug #30 — level-triggered backstop for the continuity-loss disarm, plus a re-check at arm-verify completion. |
| 1.1.7 | 2026-08-26 | remote | Fire-button ring LED reports *state*, not the button: red only when a press would actually do something (remote ARMED/PRE_FIRE/FIRING **and** a fresh STATUS_UPDATE confirming the same channel armed at the base). It had shown the operator's finger since Phase 2. |
| 1.1.6 | 2026-08-26 | both | No silent refusals left. New `NACK_BASE_ERROR` (0x0E): the base now answers commands while in ERROR instead of discarding them — a timeout carries no reason, so an operator could not tell a dead link from a base needing a power cycle. Every remaining operator-facing refusal branch now beeps and toasts. New NACK code, hence both units. |
| 1.1.5 | 2026-08-26 | remote | Remote displays `CHANNEL MISMATCH ERROR` when the base ACKs an ARM for a channel the operator did not select. The disarm and beep were already right; only the message was missing. Found by T-A13 once the fault-injection harness could produce a malformed ACK. |
| 1.1.4 | 2026-08-26 | remote | Remote no longer fails silently when an ARM cannot be granted: a new local guard refuses (naming the flag) when the cached status shows the base in ERROR, and the ACK-timeout path beeps and toasts. |
| 1.1.3 | 2026-08-26 | both | `PRE_FIRE_DELAY_MS` 2000 → **5000** by operator decision after T-A17: 2 s was too short to act inside — the operator could not disconnect an igniter within the countdown and one fired. Both units run their own countdown against this constant. |
| 1.1.2 | 2026-08-26 | base | Siren continuous from ARMED through PRE_FIRE and FIRING (the 500 ms pulse fought the siren's own modulation); continuity loss on the armed channel disarms from ARMED or PRE_FIRE instead of being informational (bug #29). |
| 1.1.1 | 2026-08-21 | both | Post-review fix round: arm-key state adopted at boot (N1), siren stale-callback race (N2), 11 minors. |

## Task Priority Reference (FSD §9.10)

### Base Unit

| Task | Priority | Core | Stack | Phase |
|------|----------|------|-------|-------|
| `espnow_rx` | 8 | any | 4096 | 1 DONE |
| `arm_switch_task` | 7 (highest safety) | 0 | 4096 | 2 |
| `rlc_link` (link_task) | 6 | 0 | 4096 | 1 DONE |
| `continuity_task` | 5 | 0 | 4096 | 2 |
| `state_machine_task` (bfsm_task) | 4 | 0 | 8192 | 3 DONE |
| `battery_task` | 3 | 0 | 3072 | 2 |
| `status_update_task` | 3 | 0 | 4096 | 2 |
| `siren_task` | 2 | 1 | 2048 | 3 DONE |
| `rgb_led_task` | 1 (lowest) | 1 | 2048 | 1 DONE |

### Remote Unit

| Task | Priority | Core | Stack | Phase |
|------|----------|------|-------|-------|
| `espnow_rx` | 8 | any | 4096 | 1 DONE |
| `fire_button_task` | 7 (highest safety) | 0 | 3072 | 2 |
| `arm_switch_task` | 6 | 0 | 3072 | 2 |
| `rlc_link` (link_task) | 6 | 0 | 4096 | 1 DONE |
| `state_machine_task` (rfsm_task) | 4 | 0 | 8192 | 3 DONE |
| `cmd_fire_repeat_task` (fire_rep) | 4 | 0 | 2048 | 3 DONE |
| `battery_task` | 3 | 0 | 3072 | 2 |
| `encoder_task` | 3 | 0 | 4096 | 2 |
| `display_task` | 2 | 1 | 8192 | 4 DONE |
| `buzzer_task` | 1 | 1 | 2048 | 2 (was an unpinned 5 until fw 1.1.9 — a UI task above the safety FSM) |
| `rgb_led_task` | 1 (lowest) | 1 | 2048 | 1 DONE |

## Build Commands

```
./build_base.sh flash          # Build + flash base (COM by-id; override with -p)
./build_remote.sh flash        # Build + flash remote (COM by-id; override with -p)
./build_base.sh flash -p PORT  # Custom port
```
