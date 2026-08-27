# All-Phases Code Review — Full Codebase vs FSD v1.42

**Document ID:** RLC-REVIEW-ALL-008
**Reviewer:** Code Review Agent (7 parallel tracks, synthesized)
**Date:** 2026-08-27
**Scope:** ALL production code (phases 0–4), tests/tooling, and documentation — compared against the FSD
**FSD Reference:** `RLC_Functional_Specification_v1_14.md` (header **v1.42** — filename stale)
**Commit Reviewed:** `5b6515f` (main, clean tree; firmware 1.1.8)

---

## Verdict: FAIL

One **CRITICAL** defect (BF-01) sits on the live-fire path and converts a normal *second* launch after one power cycle into an uncontrolled-duration ignition followed by a mid-fire reboot. It has never been exercised because no test has completed a fire pulse and then re-armed on the same power cycle. Everything else in the codebase is in markedly good shape: the fire-path interlocks, the v1.1.8 bug #30 backstop, protocol fuzz/replay safety, constants/pins conformance, and the host test suite (12 binaries / 265 checks / 0 failures, re-run during this review) all verified clean. The failure verdict is driven by BF-01 alone, plus a set of MAJOR spec gaps (display health check, FSM test coverage, link-task race) and documentation contradictions that materially misdescribe implemented safety behavior.

**Interim operational mitigation until BF-01 is fixed: power-cycle the base unit between launches.**

---

## Table of Contents

1. [Coverage Analysis](#1-coverage-analysis)
2. [Deviation Report](#2-deviation-report)
3. [Prior Review Verification](#3-prior-review-verification)
4. [Edge Cases & Safety](#4-edge-cases--safety)
5. [Concurrency & Platform Issues](#5-concurrency--platform-issues)
6. [Error Handling](#6-error-handling)
7. [Tests & Tooling](#7-tests--tooling)
8. [Documentation Inconsistencies](#8-documentation-inconsistencies)
9. [Code Quality](#9-code-quality)
10. [Summary](#10-summary)
11. [Recommendation](#11-recommendation)

---

## Files Reviewed

All production sources in `components/rlc_base` (11 files), `components/rlc_remote` (9 files), `components/rlc_common` (24 files), `main/`, plus `tests/host/`, `tools/`, `rlc-hw-test-base/`, `rlc-hw-test-remote/`, and all project documentation. Managed components excluded (vendor code).

| Track | Focus |
|------|------|
| BF | Base fire/safety path: base FSM, relays, fire timer, arm-sense, siren, status update, fault inject |
| RM | Remote operator path: remote FSM, fire button, encoder, arm switch, buzzer |
| CM | Comms: ESP-NOW, link manager, protocol, message/CRC, self-test |
| CI | Common infra: config/pins diff vs FSD, continuity, battery, RGB LED, watchdog, boot order |
| DS | Display: ILI9488 driver + all UI screens |
| TT | Tests & tools: host suite (executed), hw-test firmware, tools, FSD §15 mapping |
| DOC | Documentation consistency audit vs code and vs each other |

---

## 1. Coverage Analysis

### 1.1 By phase (Development_Progress.md claim vs verified)

| Phase | Claim | Verified |
|-------|-------|----------|
| 0 — HW validation | COMPLETE | Confirmed (with 2 stale spec defects, see DOC-03/04) |
| 1 — Foundation/comms | COMPLETE | Confirmed (§6/App A 1:1 conformance; 2 minor exceptions, CM-02/03) |
| 2 — I/O & debouncing | COMPLETE | Confirmed (§5.3/§5.4 implemented; §5.4.6 relay-dropout delay missing — CI-01) |
| 3 — State machines | CODE COMPLETE | Confirmed functionally; **zero automated FSM tests** (TT-04) |
| 4 — Display | CODE COMPLETE | All §10.2.x screens implemented; **§5.5.6 runtime health check missing** (DS-01) |
| 5 — Hardening/testing | NOT STARTED | Confirmed; §15 status: see §7 below |

### 1.2 Requirements-level highlights

- **Constants (FSD §14 vs `rlc_config.h`):** every timing/threshold constant matches, including `PRE_FIRE_DELAY_MS=5000`, `FIRE_PULSE_DURATION_MS=1000`, `FIRE_PULSE_BACKSTOP_MARGIN_MS=250`. Two documented deviations (calibrated divider ratios 4.3148/2.8211 vs nominal 4.3/2.8). Several code constants absent from §14 (DOC-16).
- **Pins (FSD App C vs `pin_config.h`):** all 21 base + 18 remote pins match exactly, zero mismatches. FSD's own spare-GPIO list is wrong (DOC-15).
- **Protocol (FSD §6/App A/App D vs `rlc_protocol.h`/`rlc_link.c`):** message types, NACK codes, error flags, and all packed struct sizes are exact 1:1. Three exceptions: NACK 0x08/0x06 never emitted (CM-02), `update_sequence` DATA-GAP detection missing (CM-03), duplicate-LINK_ACK handling deviates from App D.1 (CM-08, benign).
- **Safety (FSD §9):** §9.1 fail-safe defaults, §9.6/9.13 TWDT ordering, §9.7 GPIO-safe-first, §9.8 brown-out config — all verified. §9.10 task tables no longer fully describe reality (buzzer 5 vs spec 1; `espnow_rx` prio 8 absent from spec) — CI-03/RM-04.
- **Display (FSD §10):** every specified screen exists and renders per layout. §5.5.6's runtime display health check is the one gap (DS-01, MAJOR).

---

## 2. Deviation Report

### CRITICAL

**BF-01 — Fire timer never stopped on normal pulse completion → second launch panics with igniter energized**
`rlc_fire_timer.c:66-68`, `rlc_base_fsm.c:616-634`
In ESP-IDF, an expired one-shot alarm auto-disables only the alarm; the GPTimer driver stays in RUN state (verified against `esp_driver_gptimer/src/gptimer.c` — `gptimer_start()` CAS-expects `GPTIMER_FSM_ENABLE` and returns `ESP_ERR_INVALID_STATE` otherwise). Every FIRING exit path calls `fire_timer_stop()` **except the successful one** (EVT_FIRE_PULSE_DONE). On the next arm/fire cycle, `fire_timer_start()` wraps `gptimer_start()` in `ESP_ERROR_CHECK` → `abort()` → panic-print-reboot **while the channel relay and arm relay are energized**. The igniter carries full current for the panic+reboot interval (100+ ms; an e-match fires in ms), then the base reboots mid-FIRING and the remote sees a link drop instead of FIRE COMPLETE.
*Why never seen:* T-F02 (the only G3 fire test run) aborts before the pulse; no test has ever completed a pulse and re-armed on one power cycle.
**Toolchain sensitivity (verified):** the project builds against ESP-IDF **v5.4.1** (`/home/john/esp/esp-idf`, confirmed via `build_base/project_description.json`), where `gptimer_start()` on a running timer returns `ESP_ERR_INVALID_STATE` → the panic path above. A second IDF checkout on this machine (v5.5.2) instead returns `ESP_OK` ("already started, do nothing") — under a future 5.5.x upgrade the same defect would *not* panic but become a subtle timing hazard (`gptimer_set_raw_count(0)` on a running timer is documented as unsynchronized with the counting clock). The fix is required under both versions.
**Fix:** call `fire_timer_stop()` in the EVT_FIRE_PULSE_DONE handler (or defensively at the top of `fire_timer_start()`), and demote the `ESP_ERROR_CHECK` on `gptimer_start` to a checked return that enters ERROR. **Add a G3 test: two complete fire cycles per power-on.**

### MAJOR

**CM-01 — Unlocked cross-task race in `rlc_link_send_status_update()`**
`rlc_link.c:281-304`
Called from the base's `status_update_task` with no lock, while `link_task` mutates the same state under `s_state_mutex`. Unprotected: `s_tx_seq` (non-atomic RMW), `s_status_update_seq`, and `set_state()`. Failure: duplicate sequence numbers → the remote rejects a frame as replay → a dropped PONG becomes a counted miss (spurious COMM_DEGRADED/link flap) or a STATUS_UPDATE ages the remote's stale timer. The author fixed this exact class in `rlc_link_send_cmd` (line ~970) but not here. **Fix:** take `s_state_mutex` around the function.

**DS-01 — §5.5.6 runtime display health check entirely missing**
`rlc_display.c` (absence of the feature; only boot-time ID check at `rlc_display.c:1212-1225`)
FSD §5.5.6 mandates a 5 s panel-ID re-read during IDLE (inside `display_task`, serialized with writes) and requires display failure during ARMED/PRE_FIRE/FIRING → immediate CMD_DISARM + ERROR. Today the panel ID is read exactly once at boot and every SPI return code is discarded. Failure: a mid-session panel/flex/connector fault freezes the last-rendered frame — e.g. an ARMED screen showing "CONTINUITY CONNECTED" — while the FSM continues accepting fire commands. The TWDT cannot catch this (the task keeps flushing happily into a dead bus). Independently corroborated by the RM track. **Fix:** 5 s ID re-read in the idle path of `display_task`; count consecutive SPI failures; on failure while armed → FSM event → CMD_DISARM + ERROR latch. **Add to the Phase 5 task table** (currently absent from it).

**TT-04 — Zero automated tests for either safety-critical FSM**
`rlc_base_fsm.c`, `rlc_remote_fsm.c`, `tests/host/`
§4.5 mandates event-injection testability; no FSM harness exists. The dead-man guard, arm-verify, continuity-loss disarm scoping, `wait_for_ack` sentinels, and FIRE guards have no automated coverage, and several manual tests that should cover them are broken (TT-01) or physically unreachable (T-F06/F07/F09). Dev-Progress itself notes bug #30's verification is "partial". **Fix (Phase 5):** a host harness driving `rlc_base_fsm.c` with `rlc_fsm_event_t` sequences (the FSM is already queue-driven; existing stubs cover most needs) — this one lever simultaneously discharges §4.5, the T-F06/F07/F09 "verify by code review" substitute (for which no artifact exists — TT-06), T-A05's host-test half, T-U04/U09/U16, and positive verification of bug #30.

**DOC-01/DOC-13/TT-03 — FSD contradicts implemented continuity-loss disarm (3 locations)**
`RLC_Functional_Specification_v1_14.md` App D.4 (line 3249), §15.3 T-F05 (line 2825), §8.2.7-adjacent text
All three still state that continuity→OPEN while armed "does NOT trigger disarm (informational only)". That is the pre-v1.35 behavior abolished in fw 1.1.2; the disarm is implemented, spec-locked in §7.2.7/§7.3.1/App B.1, and tested (T-A16 PASS, 920 ms). Anyone running T-F05 as written records a false FAIL against correct firmware — or "fixes" the disarm back out. **Fix:** rewrite the three locations to the v1.35 semantics (see §8 below).

**DOC-02 — Stale "DATA STALE — CANNOT ARM" display text**
FSD §8.2.3 (line 1971) and App B.2 (line 3095); revision v1.38 claims this was corrected (and T-A11's row was) but these two were missed. Actual firmware text: "NO BASE STATUS DATA". **Fix both occurrences.**

**DOC-03/DOC-04/TT-05 — Both 2026-08-21 hw-test-spec defects still present**
- `rlc-hw-test-base/RLC_Base_Hardware_Test_Specification.md:245` — "WS2812 single-pixel driver … on **GPIO 47**". GPIO 47 is the arm relay output; the LED is GPIO 48 (per the doc's own pin table and `pin_config.h`). Also "single-pixel" — the strip is 8 px + on-board LED.
- `rlc-hw-test-remote/RLC_Remote_Hardware_Test_Specification.md:339` — flash command uses the JTAG-debug by-id of MAC `44:1B:F6:81:F1:70`, which is now the **base** board (chip #4). Following the spec today silently flashes remote hw-test firmware onto the base board. Also violates the project's board-serial-over-MAC by-id rule.
Development_Progress lines 67-70 note these "need fixing before those docs are followed on the bench" — they were never fixed. **Fix:** GPIO 47→48 (+ wording); remote by-id → the remote's own debug-unit by-id (MAC `AC:A7:04:E2:F2:8C`) with the board-serial `usb-1a86_USB_Single_Serial_5B5E043219-if00` noted as preferred if the console is on UART0.

**TT-01 — `tools/test_tr04.py` ports stale/wrong**
`tools/test_tr04.py:13-14` — `BASE_PORT` points at the dead chip #3 adapter (absent from `/dev/serial/by-id/`); `REMOTE_PORT` (`…42156`) is now the **base** board. Current live by-ids: base `5B5E042156`, remote `5B5E043219`. **Fix:** swap/update, or take ports from argv.

**TT-02 — `vbat_fit.py` cannot parse current `vbat-cal` output**
`tools/vbat_fit.py:29-30` vs `tools/vbat-cal/main/main.c` — the fitter's log mode expects `CSV,…`/`PLATEAU,…`; the firmware emits `MEDIAN …`/`ADCMAP,…`. Verified by execution: exits 1 "No CSV records found" on real logs. Only the manual `--pairs` path works (its LSQ math verified correct). **Fix:** align one side with the other; until then fix the docstring.

### MINOR — spec conformance / behavior

| ID | Location | Finding | Fix |
|----|----------|---------|-----|
| BF-02 | `rlc_base_fsm.c:857-862` | Link-health abort at pre-fire expiry goes to IDLE, spec says LINK_LOST; guard 2 (PONG ≤1000 ms freshness) merged into the 30%-failure-rate check, so ~1.5 s PONG staleness can pass at ignition | Check last-pong age explicitly; route abort to LINK_LOST |
| BF-03 | `rlc_siren.c`, base FSM disarm sites | `SIREN_CONTINUITY_LOST` (§12.2: 200/200×3) not implemented — continuity-loss disarm is audibly identical to key-off disarm (silence) | Add pattern, call at the three disarm sites |
| BF-04 | `rlc_base_main.c:162-178` | ESP-NOW/peer-reg failure → `return` from app_main: no ERROR state, no siren, FSM stays BOOT (relays safe, LED only) | Drive FSM to ERROR / sound error siren |
| RM-01 | `rlc_remote_fsm.c` IDLE | `EVT_ENCODER_SHORT_PRESS` unhandled — §8.2.3 "Hold to ARM" prompt is a persistent banner instead | Toast on event, or bless banner in FSD |
| RM-02 | remote FSM (absent) | §8.2.2 `num_channels` adaptation not implemented; ARM for a nonexistent channel degrades to a base NACK (safe) | Store count at LINK_ACK + local guard, or document single-codebase exemption |
| RM-03 | `rlc_remote_fsm.c:788-796` | Operator-initiated FIRE abort shows "NO RESPONSE - FIRE ABORTED" (misattributes abort to base); behavior itself is spec-correct | Add `WAIT_FOR_ACK_INTERRUPTED` branch (ARM path already has one) |
| RM-04 / CI-03 | `rlc_buzzer.c:142` | Buzzer task prio 5 unpinned vs §9.10's 1/core 1 — a UI task above FSM, violating the section's SHALL | Set 1/core 1 or document deviation |
| RM-05 | `rlc_buzzer.c:41-53,150-154` | `buzzer_play()` reset+send races the buzzer task's `SendToFront` → one stale pattern can play before the new one | Mailbox/`xQueueOverwrite` semantics |
| RM-06 | `rlc_remote_fsm.c:1007-1021` | Fire-repeat task checks flag + arm key but not `fire_button_is_pressed()` → up to ~1 stray CMD_FIRE after physical release (benign: base dead-man + CEASE_FIRE follow) | Add the button check at line 1016 |
| RM-07 | `rlc_buzzer.c:92-105` | `BEEP_PING_FAIL` / `BEEP_CONTINUITY_LOST` implemented but never played — §12.1 usages unmet; continuity disarm indistinguishable from arm timeout on the remote | Play distinctive pattern on OPEN-on-armed-channel disarm; drop or FSD-bless the ping-fail beep |
| CM-02 | `rlc_link.c:582-597` | App D.3-mandated NACK 0x08 (replay) / 0x06 (CRC fail) never emitted — silent drop contradicts §7.2.9a no-silent-refusals intent | Send CMD_NACK before returning |
| CM-03 | remote FSM | §6.4.3 `update_sequence` modular-gap "DATA GAP" warning not implemented (field generated correctly, never consumed) | Gap check on EVT_STATUS_UPDATE |
| CM-04 | `rlc_link.c:632-654` | Truncated ACK/NACK (`plen < 6`) forwarded with zeroed fields; zeroed reason decodes "UNKNOWN ERROR" | Drop/clamp below `sizeof(payload)` |
| CM-07 | `rlc_link.c:875` | Silent drop on full internal queue (no log, unlike espnow layer) — indistinguishable from RF loss post-mortem | Log the drop |
| CI-01 | `rlc_config.h:72` | `CONT_RELAY_DROPOUT_MS` dead — §5.4.6's ≥50 ms relay-settling delay before first post-fire continuity read not implemented (self-corrects in ≤800 ms; cannot alone permit arming) | Skip/pad a channel 50 ms after relay de-energise, or delete constant + record deviation |
| CI-02 | base battery task | `ERR_VBAT_LOW` (§13.1 bit 0) never set — remote can't show base "VBAT LOW" (arm still NACK'd 0x09 from live reading) | Set flag in battery task/housekeeping |
| CI-04 | `rlc_rgb_led.c:314`, `rlc_espnow.c` | `led_task` and `espnow_rx` not TWDT-registered (§4.7 says all; §9.6 narrows to critical). A hung RMT refresh freezes the status LED incl. "ARMED" blink while TWDT stays happy | Subscribe both or document exclusion |
| CI-05 | `rlc_base_main.c:159-231` | Init failures `return` (FSM never starts → no §5.4.8 error siren); `rlc_battery_init()` return ignored (contains ESP_ERROR_CHECK → reboot loop) | Latched halt (`vTaskDelay(portMAX_DELAY)`) + check init |
| CI-06 | `rlc_remote_main.c:154-158` | Encoder-before-ADC ordering (GPIO 4/5 = ADC1_CH3/CH4 constraint) satisfied but **undocumented** — a future "spec-order" reorder silently re-breaks it | One-line comment |
| DS-02 | `rlc_display.c:597-606` | Grid column 4 at x=484 > 480 — right border of channels 4/8 never drawn (clipped, cosmetic) | Shift grid left / shrink cells 2 px |
| DS-03 | `rlc_display.c:746-751` | ARMED screen prints "CONTINUITY OPEN" when status is stale — fail-safe direction but asserts a measurement the remote doesn't have (main screen correctly says "NO DATA") | "CONTINUITY ?" when `!status_fresh` |

### INFO (selected)

- **CM-05** — replay guard accepts `seq==0` frames while the rx counter is 0 (unlimited seq-0 replays would pass; blocked in practice by keyed CRC — but the key is public, bug #20). Reject seq 0 for session-bearing types.
- **CM-06** — managed `espressif/esp-now` component vendored but never used (code uses native `esp_now_*`) — dead flash + supply-chain surface. Remove or migrate deliberately.
- **CM-10 / DOC-14** — FSD internal inconsistencies: §6.4.1 diagram "15 attempts / 1 s heartbeat" and App D.2 "orange flash 250 ms" vs §14.1 (5 retries / 500 ms / no flash); §15.1 T-C01/C02/C04 expect pre-v1.18 whole-strip colors. Code follows §14.1 in all cases.
- **BF-05..07** — dead `base_fsm_post_event()`; ignored `gpio_config`/`esp_timer` returns on fire-path IO; arm-sense task started before FSM queue exists (weld-fault event drop window at boot).
- **RM-08..11** — pending-cmd shared-write invariant (verified to hold, documented); early-boot failures leave `s_state == BOOT` not ERROR; three documented conservative deviations existing only in code comments (multi-arm→ERROR, PRE_FIRE abort sends CEASE_FIRE, toast wording); theoretical torn read of `s_prefire_start_ms`.
- **CI-07..10** — LED mutex `portMAX_DELAY` (benign); torn 8-word continuity band snapshot (accepted for LED/status, FSM reads single band atomically); `CONT_TRACE_INTERVAL_MS` defaults to 1000 despite its own "set 0 for field use" comment (1 trace/s in production logs); `rlc_rgb_led_init` return ignored both units.
- **DS-04..09** — lock-free FSM scalar reads (benign on S3, formally against §4.7); PSRAM-alloc retry trap; RGB666 header comment vs 8-bit implementation (works on panel); dead display API stubs; prio-2 starvation analysis (safe); 1340-line file cleanly sectioned (split optional).
- **TT-12..14** — no CI, `run.sh` never invoked by build scripts; committed stale build artifacts containing `/dev/ttyACM0` and dead-chip by-ids; hw-test encoder uses divider 3 + pulse decoder vs production 4 + cycle-position (historical, now covered by host test).
- **DOC-10/18/25** — `.claude/RESUME.md` stale (2026-08-17); FSD revision table skips v1.9; Project Summary formatting glitch.

---

## 3. Prior Review Verification

- **Bug #30 / fw 1.1.8 level-triggered backstop (last review's MAJOR):** verified sound and effectively unbypassable in scope — two independent layers (entry re-check at arm-verify completion, `rlc_base_fsm.c:442-446`; level-triggered backstop in `check_timers()`, :817-824, ≤50 ms cadence), scope exactly per §7.2.7 incl. FIRING/POST_FIRE exclusions. Residual latency (sampler round-robin ≤~800 ms + hysteresis) is within what §7.2.7 req 4 accepts.
- **N3 TWDT reboot ordering:** in place on both units (step-0 reconfigure, step-11 subscribe, critical tasks self-register).
- **2026-08-21 AllPhases fixes:** re-verified present, except the two hw-test-spec defects that audit noted — **still not fixed** (DOC-03/04).
- **Open hardware bugs:** exactly #20 (public crypto keys — confirmed unchanged, and CM-05 compounds it), #22, #23, #24, #25 remain; #30 correctly marked RESOLVED with honest partial-verification caveats.

---

## 4. Edge Cases & Safety

1. **BF-01 is the only finding that changes the hazard analysis** (see above). Everything else on the fire path verified correct: hardware AND-gate interlock honored in driver order (arm relay first, 20 ms, bug-#18 arc rationale); dead-man timestamp taken at wire-receive per spec; wrong-channel FIRE in PRE_FIRE does not refresh the dead-man; `COMPLETE_PULSE_ON_LINK_LOSS` completes then goes LINK_LOST; battery-critical-during-FIRING "complete pulse then ERROR" latch preserved on every exit (m4); double EVT_FIRE_PULSE_DONE harmless; `fire_timer_stop`'s notify-clear (m2) correct.
2. **Frozen-display hazard** (DS-01) is the second genuine safety gap: operator commands against a lying display during ARMED/FIRING.
3. Stale-status posture is genuinely good: stale never renders SAFE or CONNECTED anywhere; base battery blanks; arm sense degrades to red "?". One exception: DS-03 (ARMED screen asserts OPEN on stale).
4. Fuzz/malformed-frame safety is solid: length caps at both hops, memcpy-based header parse, per-handler `plen` checks, packed-struct unaligned safety, three independent replay gates on CMD_FIRE (token, seq, keyed CRC).
5. Brown-out detector enabled (LVL 7); remaining power-rail risk is hardware bugs #24/#25, unchanged.

## 5. Concurrency & Platform Issues

- CM-01 is the one real locking defect (MAJOR, above).
- Documented, accepted deviations: link_task holds the state mutex across `esp_now_send` and a 10 ms blocking queue send (J4/2.7 ABBA analysis in code); `espnow_rx` not TWDT-registered with rationale.
- §9.10 task tables no longer describe reality: buzzer 5 (spec 1), `espnow_rx` 8 absent from spec, link 6 vs heartbeat 5. Bounded risk; update spec or code (see RM-04/CI-03).
- Priority ordering gives correct drain direction (espnow_rx 8 > link 6 > FSM 4); ISR discipline verified correct on both units (encoder ISR and GPTimer ISR: IRAM, FromISR primitives only).

## 6. Error Handling

- CI-05/BF-04: boot-failure paths on the base are fail-safe but under-signalled (no ERROR state/siren; one path can reboot-loop via ignored `ESP_ERROR_CHECK` inside battery init).
- CM-02: silent refusals on replay/CRC-fail contradict the project's own no-silent-refusals principle.
- Recovery paths verified: link recovery never re-enters ARMED; ADC failure fails safe to OPEN with re-init; PSRAM alloc failure halts with error screen.

## 7. Tests & Tooling

Host suite **executed during this review: 12 binaries, 265 checks, 0 failures** — exactly matching the FSD's claim; tests genuinely assert and compile real production sources (no mirrors).

FSD §15 status:
- **§15.1 comms:** 7/8 PASS; T-C05 PARTIAL (loss rate only at 0.5 m); **T-C06 (replay) MISSING** — no test, no tool, ~5 months stale.
- **§15.2 arming:** complete — 18 PASS, 2 N/A (A05's host-test half unmet; A15 superseded).
- **§15.3 fire:** 1/9 (T-F02). T-F06/F07/F09 confirmed physically unreachable as written (1 s pulse < 1.5 s link-loss detection) — the agreed "verify by code review" substitute has **no artifact**; TT-04's harness would discharge it. T-F05's row is wrong vs shipped firmware (DOC-13).
- **§15.4 safety:** 3/19. T-S12/S13 share T-F06's unreachability but are **not annotated** (TT-06); T-S13 also needs a `COMPLETE_PULSE_ON_LINK_LOSS=0` rebuild the row doesn't mention.
- **§15.5 unit:** boot self-test covers 11 T-U ids (T-U13 partial); **T-U04, T-U07, T-U09, T-U16 missing** (T-U07 = battery threshold *gating*, the safety behavior, vs sampling which is tested). §15.5's runner note overstates host coverage (TT-08).
- Tooling: TT-01/TT-02 (broken tools), TT-07 (base encoder host test = 0 checks, indistinguishable from pass), TT-10/TT-11 (nits), TT-12 (no CI). Positives: fault-injection harness verified production-safe end-to-end (Kconfig default n, file-level guard, sdkconfig isolation, build-dir wipe on mode switch); armgate-test validates its own instrument and treats unstable lines as failures.

## 8. Documentation Inconsistencies

Full actionable list (DOC-01…DOC-25) with exact locations and fix texts — 4 MAJOR, 17 MINOR, 4 INFO. Highest priority:

1. **DOC-01/13/TT-03** — FSD App D.4 :3249, §15.3 T-F05 :2825: continuity-loss disarm contradiction (MAJOR, above).
2. **DOC-02** — "DATA STALE — CANNOT ARM" at :1971 and :3095 → "NO BASE STATUS DATA".
3. **DOC-03/04/TT-05** — hw-test specs: GPIO 47→48; remote flash by-id points at the base board.
4. **DOC-05/06** — Development_Progress.md header (spec v1.33/fw 1.1.1 → v1.42/1.1.8); README :272 (v1.33 → v1.42).
5. **DOC-07** — FSD header date 2026-08-25 → 2026-08-26; footer "v1.14" → "v1.42".
6. **DOC-08/09** — Test_Report_Phase3_G2 header (mid-campaign values vs its own §1 totals); changelog "1.1.1 → 1.1.4" mid-session snapshot.
7. **DOC-11/12** — FSD §12.2 SIREN_ARMED "500/500" stale (v1.35 = continuous); SHORT-band remnants in §3/§5.4.2/§7.3.1/§14.5 (deprecated v1.29).
8. **DOC-15/16/17/23/24** — App C.1 spare list includes in-use GPIO 42; §14 missing constants (`FIRE_PROTECTED_CHANNEL_MASK`, `CONT_ADC_ATTEN`/full-scale, splash, trace, VBAT_FULL); §7.2.9a misplaced inside §6; App C.2 remote RGB "on-board, fixed" stale; §13.1 bit-3 rationale obsolete.
9. **DOC-19/20** — Dev-Progress T-S07 "2 s" → 5 s; "production battery thresholds not yet restored" — they are.
10. **DOC-21/22** — Gotron shopping list: pre-recalibration band-boundary table (would mislead bench verification); bug #27 marked unresolved though fixed 2026-08-26.
11. **Missing Dev-Progress coverage:** fw 1.1.3 (5 s delay) has zero mention; 1.1.4–1.1.7 have no entries; §10.2.0 palette deviation lacks an as-built note in the FSD itself.
12. **DOC-25** — Project Summary list formatting glitch (guards list shows 6 of 10).

## 9. Code Quality

Substantively good and improving: numbered fix-comment discipline (2.x/4.x/5.x/m/N/R/J series) makes prior-review provenance traceable; single-owner FSM discipline consistently held; pure/host-testable core (debounce, classifier, arm-state, battery) separated from hardware. Real nits only: dead code (`base_fsm_post_event`, buzzer patterns, display stubs, `CONT_RELAY_DROPOUT_MS`, `rlc_seq_validate` used only by selftest while production inlines a subtly different check — CM-05), the undocumented encoder-before-ADC ordering (CI-06), and `rlc_display.c` size (optional split).

## 10. Summary

| Category | Critical | Major | Minor | Info |
|----------|----------|-------|-------|------|
| Spec conformance | 0 | 3 (DOC-01/13, DOC-02, TT-05/DOC-03/04) | 12 | 6 |
| Correctness | 1 (BF-01) | 0 | 5 | 6 |
| Safety & robustness | 0 | 1 (DS-01) | 5 | 3 |
| Concurrency/RTOS | 0 | 1 (CM-01) | 3 | 7 |
| Error handling | 0 | 0 | 3 | 2 |
| Tests & tooling | 0 | 3 (TT-01, TT-02, TT-04) | 6 | 6 |
| Documentation | 0 | 0 | 10 | 4 |
| Code quality | 0 | 0 | 0 | 4 |
| **Total** | **1** | **8** | **44** | **38** |

## 11. Recommendation

**NO-GO for live fire beyond a single launch per power cycle until BF-01 is fixed and regression-tested.** With that one operational constraint, the system's fire path is otherwise verified sound.

Ordered action list:

1. **BF-01** (fire timer stop + checked `gptimer_start` return) — small fix, then G3 test: two complete fire cycles per power-on.
2. **DS-01** (display runtime health check + disarm-on-failure) — add to Phase 5 table explicitly.
3. **CM-01** (mutex in `rlc_link_send_status_update`).
4. **Documentation pass** (DOC-01…25 as listed in §8; the FSD continuity-disarm contradictions and hw-test-spec hardware references are the dangerous ones — they mislead a bench operator toward wrong hardware and wrong expected behavior).
5. **TT-01/TT-02** tool repairs (30 minutes each).
6. **Phase 5 kickoff with TT-04 as its centerpiece**: the FSM host-injection harness discharges §4.5, T-F06/F07/F09's review-substitute, T-A05's host half, T-U04/07/09/16, T-C06, and bug #30's positive verification in one work item.
7. Remaining MINORs at the maintainer's pace; RM-06 and CI-06 are one-liners worth doing immediately.

---

*Review artifacts: 7 track reports (BF/RM/CM/CI/DS/TT/DOC) synthesized into this document. Host test suite executed read-only during review: 12 binaries / 265 checks / 0 failures.*
