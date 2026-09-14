# Full-Project Code Review — All Phases + Post-Release Work (fw 1.2.18)

> **Addendum (same day):** every actionable finding in this review — both
> Majors and all Minors/Infos except the previously-accepted INF-03
> (non-atomic status snapshot, display-only) — was fixed in firmware
> **1.2.19** (FSD v1.70). See `changelog.md` and `rlc_version.h` for the
> fix narrative. This document remains the point-in-time review record.

**Document ID:** RLC-REVIEW-ALL-010
**Reviewer:** Code Review Agent (3 parallel tracks: base unit, remote unit, common/tools)
**Date:** 2026-09-14
**Scope:** Entire codebase — Phases 0–5 plus post-release fw 1.2.1–1.2.18
**FSD Reference:** RLC_Functional_Specification_v1_14.md (RLC-FSPEC-001 v1.69, 2026-09-14)
**Commit Reviewed:** f853bb6 (fw 1.2.18)

---

## Verdict: PASS WITH NOTES

The codebase is in strong shape for a safety-critical hobby system. Every hazardous-path property verified sound: the two-break fire path (arm relay + channel relay, arm key strictly in the coil-drive path) holds on every code path; dead-man, ARM_TIMEOUT backstop, link-loss disarm, brown-out, and TWDT fail-safes are all implemented and traced. All findings from the 2026-08-27 and 2026-08-28 reviews were re-verified as fixed with **zero regressions** — the review cycles visibly landed. Two MAJOR findings remain, both narrow windows inside the remote FSM's `wait_for_ack()` where a safety event (display fault, battery critical) is consumed without the FSD-mandated disarm/ERROR response; both are bounded by the base's independent fail-safes and both are small fixes. Nothing found blocks live fire, but the two MAJOR items should be fixed before the next launch event.

**Counts:** 0 Critical · 2 Major · 11 Minor · 16 Info

---

## Table of Contents

1. [Coverage Analysis](#1-coverage-analysis)
2. [Deviation Report](#2-deviation-report)
3. [Plan vs. Implementation](#3-plan-vs-implementation)
4. [Edge Cases & Safety](#4-edge-cases--safety)
5. [Concurrency & Platform Issues](#5-concurrency--platform-issues)
6. [Error Handling](#6-error-handling)
7. [Code Quality](#7-code-quality)
8. [Summary](#8-summary)
9. [Recommendation](#9-recommendation)

---

## Files Reviewed

| Area | Files |
|------|-------|
| Base unit (`components/rlc_base/`) | rlc_base_main, rlc_base_fsm, rlc_base_state, rlc_relay, rlc_continuity, rlc_arm_sense, rlc_base_battery, rlc_fire_timer, rlc_siren, rlc_faultinject, rlc_status_update |
| Remote unit (`components/rlc_remote/`) | rlc_remote_main, rlc_remote_fsm, rlc_remote_state, rlc_display, rlc_encoder, rlc_buzzer, rlc_arm_switch, rlc_fire_button, rlc_remote_battery, rlc_remote_faultinject |
| Common (`components/rlc_common/`) | rlc_link, rlc_espnow, rlc_message, rlc_protocol.h, rlc_config.h, rlc_version.h, pin_config.h, rlc_debounce, rlc_continuity_class, rlc_arm_state, rlc_battery, rlc_watchdog, rlc_selftest, rlc_rgb_led |
| Tools | tools/mkvideoband.py, tools/rlcv_repack.py |
| Build config | CMakeLists (root + components), build_base.sh, build_remote.sh, sdkconfig.base/.remote, partitions_remote.csv, main/main.c (harmless stub) |
| Prior reviews checked | Code_Review_AllPhases_20260827_0308, Code_Review_Phase5_20260828_0641 |

---

## 1. Coverage Analysis

### Base unit (§7, §9, §12.2, §13)

| Requirement | Status |
|---|---|
| §7.1–7.2.9 FSM states, guards, exceptions, terminal ERROR, no-silent-refusal | DONE (one canceller missing a NACK — B-MIN2) |
| §7.2.2 non-blocking arm verify + two-strike escalation (v1.47) | DONE |
| §7.2.3–7.2.6 fire sequence, dead-man, pulse, cooldown | DONE |
| §7.2.7 all 7 disarm triggers (edge + level backstop) | DONE |
| §7.2.8 LINK_LOST entry/recovery/siren | DONE |
| §7.3.1 continuity monitoring, events, chirps | PARTIAL — no dedicated MARGINAL-on-armed-channel advisory (B-MIN3) |
| §7.3.2 arm sense + weld detection | DONE |
| §7.3.3 battery monitoring | DONE |
| §7.4.1/7.4.2 relay encapsulation, HW-timer firing, ISR discipline | DONE |
| §9.1–9.9 fail-safes, dual-key, dead-man, auto-disarm, watchdog, GPIO order, brown-out, struct self-test | DONE (§9.6 flag surfacing gap — B-MIN1) |
| §9.10 task table | DONE (stale siren_task row — B-INF6; stack doc drift — B-INF7) |
| §9.13 boot sequence + boot chirp | DONE |
| §12.2 siren patterns (all traced exact) | DONE |
| §13 error flags | PARTIAL — ERR_WATCHDOG_RESET never set (B-MIN1) |

### Remote unit (§5.5, §8, §10, §12.1)

| Requirement | Status |
|---|---|
| §5.5.1–5.5.5 encoder, SPDT arm key (NC GPIO 2), fire button, LEDs, battery | DONE — all verified against real wiring |
| §5.5.6 display + health check | DONE with gap — EVT_DISPLAY_FAULT can be swallowed (R-MAJ1, R-MIN4) |
| §5.5.7/§12.1 buzzer incl. background state tones | DONE |
| §8.2 state transitions, guards, named refusals | DONE (one hole — R-MAJ2) |
| §8.4 repeated CMD_FIRE / repeat-NACK | DONE |
| §10.2/10.3 screens, refresh, splash band (v1.68 geometry, 480×160, 5 Hz) | DONE |
| §9.10/9.12/9.13 priorities, ISR safety, boot order | DONE (documented deviations, see §2) |

### Common / protocol / tools (§6, §9.6/9.9, §14)

| Requirement | Status |
|---|---|
| §6.2.1 ESP-NOW encryption, keys out of tree | DONE |
| §6.2.2 seq/token/CRC32-C, overflow, boot vector test | DONE |
| §6.3 message formats / App A asserts | DONE (LINK_REJECT 0x03 documented addition) |
| §6.4.1/6.4.1a/6.4.1b handshake, retry, 5-fail loss, queue-decoupled recv | DONE (sync-send-error gap — C-INF5; dead-man stamping deviation, see §2) |
| §6.4.2 heartbeat, loss, recovery | DONE (per-miss beep → edge beep deviation, see §2) |
| §6.4.3/6.4.4 status updates, modular gaps, NACK codes incl. 0x0E/0x0F | DONE |
| §14 constants centralised | DONE (FSD §14.4 display clock table stale — C-MIN3) |
| Splash tooling vs 2 MB partition | DONE (repack validation light — C-MIN4) |

---

## 2. Deviation Report

| ID | Severity | Where | Deviation |
|---|---|---|---|
| R-MAJ1 | **MAJOR** | `rlc_remote_fsm.c:676-700` | `wait_for_ack()` silently consumes and drops `EVT_DISPLAY_FAULT` (one-shot, latched in rlc_display.c:2186/2365-2374). Panel flex fails during an ARM/FIRE ACK wait → FSM never runs the §5.5.6 handler (CMD_DISARM + ERROR); a FIRE ACK arriving moments later runs a full pulse with a dead panel. Buzzer still sounds, release still works, base dead-man bounds the hazard. FSD §5.5.6. CONFIRMED (full path traced). |
| R-MAJ2 | **MAJOR** | `rlc_remote_fsm.c:696-699` | Inline `EVT_BATTERY_CRITICAL` in `wait_for_ack()` enters ERROR without sending CMD_DISARM/CEASE_FIRE — every state handler for the same event does (ARMED :1260-1274 per CRIT-01 'b'). Battery-critical during ARM ACK wait + lost ACK → base holds arm relay closed for its full 10 s ARM_TIMEOUT while the remote sits in unrecoverable terminal ERROR. Bounded by base fail-safes. FSD §8.3.4/§5.5.6. CONFIRMED. |
| D-1 | MINOR (spec text) | FSD §6.4.1b/§7.2.4 | FSD says dead-man timestamp is "updated directly in the receive callback"; implementation stamps wire-receive time in the callback and carries it through queues — equivalent-or-better freshness (queue latency not credited), FSD text not updated. |
| D-2 | MINOR (spec text) | FSD §6.4.2 | Per-miss 80 ms beep implemented as one beep on the edge into degraded (RM-07 resolution, documented in code); FSD still says "on each individual ping failure". |
| D-3 | MINOR (spec text) | FSD §14.4 vs `rlc_config.h:488,516` | FSD still lists `DISPLAY_SPI_CLOCK_HZ 20000000` and omits the 10 MHz read clock; code ships 40/10 MHz since 1.2.13–1.2.18. Same DOC-16 class as prior review — the one table an operator would check is wrong. |
| D-4 | INFO (spec text) | FSD §8.2.3 | Fire press in IDLE now beeps + toasts "NOT ARMED - ARM FIRST" (§7.2.9a-conformant); §8.2.3 still reads "ignored (no buzzer, no display change)". |
| D-5 | INFO (spec text) | FSD §9.13 | Remote boot order brings up display before ADC/ESP-NOW, forced by the encoder-before-ADC hardware constraint (CI-06); code comment guards it, the FSD table doesn't. |
| D-6 | INFO (spec text) | FSD §9.10 | Stale `siren_task` row (siren is esp_timer-driven per §12.3); battery stack sizes differ upward from the table. |

---

## 3. Plan vs. Implementation

Implementation plans exist only for the Phase 3 review fixes (`Implementation_Plan_Phase3_Review_Fixes.md`, `_002.md`); those phases closed long ago and their fixes were re-verified as landed in the prior reviews. **No implementation plan exists for Phases 4–5 or the post-release fw 1.2.x sessions — section skipped** for those, per Development_Progress.md and changelog.md which document the work session-by-session instead. The changelog/progress record for the fw 1.2.8–1.2.18 splash work matches the commits reviewed.

---

## 4. Edge Cases & Safety

**Verified sound (no findings):**
- Two-break fire path: channel relay energized only in PRE_FIRE→FIRING after re-verifying key sense AND arm-sense HIGH (`rlc_base_fsm.c:1090-1101, 1109`); `relay_fire_set()` refuses unprotected channels (bug #18 gate, `rlc_relay.c:83-88`); arm key is coil-drive only (GPIO 42 sense). No path energizes a fire relay without both breaks.
- Fail-safes: relay GPIOs safe-first before peripherals (`rlc_base_main.c:162-163`); boot failure latches safe + siren + halt; link loss from any armed state disarms + `relay_all_safe()`; all 6 FIRING exits drop relays; 4.5 s max-duration backstop closes the lost-notification hole; brown-out LVL 7 confirmed in build config.
- Watchdog reset reboots safe per §9.1 (but see B-MIN1: the remote is never *told*).

**Findings:**

| ID | Severity | Location | Concern | Risk |
|---|---|---|---|---|
| R-MAJ1 | MAJOR | rlc_remote_fsm.c:676-700 | Display fault swallowed in ACK wait (see §2) | Fire sequence on a dead panel; bounded by base dead-man — moderate |
| R-MAJ2 | MAJOR | rlc_remote_fsm.c:696-699 | Battery-critical in ACK wait skips disarm (see §2) | Base holds arm relay ≤10 s with remote in terminal ERROR — moderate |
| B-MIN1 | MINOR | codebase-wide; rlc_protocol.h:89 | `ERR_WATCHDOG_RESET` never set — no `esp_reset_reason()` check anywhere; TWDT reboot reports `err=0x00`, operator can't distinguish silent base reboot from continuous session. FSD §13.1 bit 5 | Low (informational), CONFIRMED |
| B-MIN2 | MINOR | rlc_base_fsm.c:651-658 | Link-loss during arm-verify cancels ARM but sends no NACK — the one §7.2.2-listed canceller left unanswered | Low (link is down; NACK may not arrive), CONFIRMED |
| B-MIN3 | MINOR | rlc_base_fsm.c:671-737 | No dedicated advisory when armed channel goes MARGINAL — only the generic band-change INFO line | Low, CONFIRMED |
| R-MIN3 | MINOR | rlc_display.c:1863-1878 | Band blit runs even when first-frame decode fails → uninitialized PSRAM shown instead of documented "last good frame" | Boot cosmetics only; needs an asset that passes container validation but fails decode, PLAUSIBLE |
| R-MIN5 | MINOR | rlc_remote_main.c:193-198 | Self-test failure halt is LED+log only — `buzzer_init()` runs after `rlc_selftest_run()` (same class MIN-11 fixed for display check) | Near-zero reachability (compile-time guarantees), CONFIRMED |
| C-MIN4 | MINOR | tools/rlcv_repack.py:28-33 | No bounds validation: `off`/`ln` unchecked vs `len(b)`; Python slicing silently clamps → corrupt/truncated output blob instead of an error; firmware RLCV header check is the only backstop | Low (tool fed by its own generator), CONFIRMED |

---

## 5. Concurrency & Platform Issues

**Verified sound:**
- Single-owner FSM model on both units; mailbox buzzer semantics (RM-05); display owns the SPI bus with manual CS on both devices, 40 MHz write / 10 MHz read — exactly what the ILI9488 clone requires.
- The fw 1.2.18 task-starvation fix is **sound**: `xTaskDelayUntil` pacing + explicit 20 ms yield on overrun bounds display_task's core-1 share regardless of frame cost, keeping the TWDT-watched CPU1 idle task fed.
- `rlc_link.c`: single-task-owner + mutex for external readers; replay/token/CRC gates correctly ordered (token → 0x08 → 0x06 → 0x0F); `s_rx_last_seq` not advanced on CRC failure; seq overflow drops to LOST on every sender.
- `rlc_espnow.c`: queue depth 16 with prio-8 worker, wire-time stamping in recv callback, Wi-Fi-context-safe 5-failure latch with documented ABBA analysis.
- Encoder ISR is IRAM-resident, uses only `xQueueSendFromISR` (§9.12). Encoder-before-ADC ordering enforced with an un-reorderable comment (CI-06).
- All input callbacks use 10 ms blocking sends; TWDT reconfigured before any task exists on both units.

**Findings:**

| ID | Severity | Location | Concern | Risk |
|---|---|---|---|---|
| C-INF5 | INFO | rlc_espnow.c:236-244, 87-116 | Only send-*callback* failures count toward the 5-consecutive threshold; synchronous `esp_now_send()` errors (e.g. NO_MEM) don't — a persistent queuing failure never trips the §6.4.1a fast path; 1.5 s heartbeat drought remains the backstop | Bounded, slower detection; PLAUSIBLE |
| C-INF6 | INFO | rlc_link.c:937-954 | `s_send_failure_pending` read-then-clear race (volatile bool, no critical section) can lose one Wi-Fi-task notification | Worst case one missed immediate-loss trigger; heartbeat covers; CONFIRMED |
| R-MIN4 | MINOR | rlc_display.c:2186-2192, 2365-2374 | `EVT_DISPLAY_FAULT` is one-shot per power cycle: a full FSM queue at the 10 ms-blocking send drops it forever ("dropped!" log, `failed_reported` latch prevents retry) | Same consequence class as R-MAJ1; CONFIRMED |
| C-INF7 | INFO | rlc_link.c:594-628 | Duplicate LINK_ACK while LINKED calls `reset_session()` unconditionally (prior CM-08, still open) — replay-rejected PINGs until next handshake | Benign, self-healing; PLAUSIBLE |

---

## 6. Error Handling

**Verified sound:** every prior error-handling finding (BF-01..07, CI-01/02/05, CM-02/04/07, MIN-01..MIN-11, MAJ-01..06, CRIT-01) re-checked as fixed with no regressions. Boot failures latch safe with siren/LED on both units; ADC failure fails continuity to OPEN with a real event (armed channel disarms); display degrades to plain band on every splash failure path except R-MIN3; battery ADC failure returns stale value (safe); CRC/seq/token gates all NACK with correct codes.

Remaining gaps: R-MAJ1/R-MAJ2/R-MIN4 (§4-5 above); B-MIN1 (watchdog flag); C-MIN4 (repack tool).

---

## 7. Code Quality

Consistently high: numbered fix-provenance comments (checked accurate against code), documented rationale for every non-obvious decision (ABBA analysis, starvation-fix reasoning, clock-split explanation), host-testable pure modules, secrets out of tree with hard `#error`. Substantive items only:

| ID | Severity | Location | Issue |
|---|---|---|---|
| B-INF4 | INFO | rlc_base_fsm.h:21 | Stale doc "Must be called AFTER rlc_link_init()" contradicts the BF-07-mandated order — a future refactor "fixing" the order re-opens the boot-window weld-fault drop. Fix the comment. |
| C-MIN1 | MINOR | rlc_link.c:727 | Stale comment "allow 0 seq after reset" contradicts CM-05's unconditional `seq <= last` rejection — same refactor-regression risk class. |
| C-MIN2 | MINOR | rlc_link.h:69-75, 184-191 | Guard-callback doc still says busy LINK_REQUEST is "silently ignored"; implementation sends LINK_REJECT_BUSY (fw 1.1.17). API-contract doc drift. |
| R-MIN6 | MINOR | rlc_remote_fsm.c:480-492 | `do_disarm_and_idle()` still doesn't re-sync `s_selected_channel` from the encoder (prior INF-06, still open) — cursor can lag one detent after disarm. |
| R-INF10 | INFO | rlc_config.h:519 | `DISPLAY_ROTATION` dead constant (orientation hard-coded via MADCTL 0x68). |
| C-INF8 | INFO | mkvideoband.py:356-362 | `grade()` LUT is a linear rescale of the whole tonal range, not the documented hard cap — every pixel is darkened. Doc-only. |
| C-INF9 | INFO | build_remote.sh:47-49 | `splash` step depends on build/partition_table existing; fresh clone → opaque parttool failure. |
| C-INF10 | INFO | rlc_link.c:1102-1107; rlc_rgb_led.c:330 | Pointless trampoline wrapper; RGB init logs default pixel count (1) though strip is 8. |
| B-INF5 | INFO | rlc_base_fsm.c:679-682 | CMD_FIRE with arm sense LOW NACKs "BASE KEY OFF" when the true fault is arm-relay feedback lost — `NACK_ARM_SENSE_FAULT` (0x0B) would name it truthfully. Action is correct and safe. |
| B-INF8 / R-INF-class | INFO | rlc_status_update.c:49-81 | Non-atomic state/channel/flag snapshot (prior INF-03) — display-only, self-corrects ≤2 s, accepted. |

---

## 8. Summary

| Category | Critical | Major | Minor | Info |
|----------|----------|-------|-------|------|
| Spec conformance | 0 | 2 (R-MAJ1, R-MAJ2 — FSD §5.5.6/§8.3.4 response paths) | 5 (B-MIN2, B-MIN3, D-1, D-2, D-3) | 4 (D-4, D-5, D-6, B-INF6) |
| Plan conformance | 0 | 0 | 0 | 1 (no plans for Phases 4+; changelog record used instead) |
| Correctness | 0 | 0 | 3 (R-MIN3, R-MIN5, C-MIN4) | 2 (C-INF7, C-INF8) |
| Safety | 0 | 0 | 1 (B-MIN1) | 1 (B-INF5) |
| Concurrency | 0 | 0 | 1 (R-MIN4) | 2 (C-INF5, C-INF6) |
| Error handling | 0 | 0 | 0 | 1 (B-INF8) |
| Code quality | 0 | 0 | 2 (C-MIN1, C-MIN2) | 5 (B-INF4, R-MIN6, R-INF10, C-INF9, C-INF10) |

**Prior-review status:** every base, remote, and common finding from RLC-REVIEW-ALL-008 (2026-08-27) and RLC-REVIEW-ALL-009 / Phase 5 (2026-08-28) re-verified — all marked-fixed items are fixed in code; the only still-open items are the previously accepted/documented ones (INF-03 non-atomic snapshot, INF-06 encoder re-sync, CM-08 duplicate-LINK_ACK). **No regressions.**

---

## 9. Recommendation

**GO — with two conditions before the next launch event:**

1. **Fix R-MAJ1 and R-MAJ2** (both in `wait_for_ack()`, `rlc_remote_fsm.c:676-700`): handle `EVT_DISPLAY_FAULT` inline the way `EVT_LINK_LOST` is handled (CMD_DISARM/CEASE_FIRE + ERROR per §5.5.6), and send CMD_DISARM/CEASE_FIRE before the inline battery-critical ERROR transition. Consider making the display-fault send retryable (R-MIN4) at the same time.
2. **Update FSD §14.4** to the real 40/10 MHz display clocks (D-3) and bless the two documented deviations (D-1 dead-man stamping, D-2 edge beep) so the next review doesn't re-litigate them.

Everything else — B-MIN1 (`esp_reset_reason()` → `ERR_WATCHDOG_RESET`), the missing arm-verify-cancel NACK (B-MIN2), the stale comments that invite refactor regressions (B-INF4, C-MIN1, C-MIN2), repack bounds checks (C-MIN4) — is maintainer's-pace polish. The fire path itself, on both units, verified sound end to end.
