# Phase 3 Code Review — State Machines and Command Processing

**Document ID:** RLC-REVIEW-P3-001
**Reviewer:** Code Review Agent
**Date:** 2026-04-14
**Scope:** Phase 3 — State Machines and Command Processing
**FSD Reference:** RLC_Functional_Specification_v1_14.md
**Commit Reviewed:** `744240c` (Phase 3 code complete, uncommitted working tree)

---

## Verdict: FAIL

Phase 3 implements the full arming and firing state machines for both base and remote units, with correct architecture (single-task-owner model, hardware timer for fire pulse, command forwarding via queue). The code builds cleanly on both targets and follows established patterns from Phases 1–2. However, three critical safety defects must be resolved before on-target testing: (1) the base FIRING state silently ignores link loss — the `COMPLETE_PULSE_ON_LINK_LOSS` configuration is never consulted; (2) 3 of 10 FSD-mandated arming guards are delegated to the link layer without explicit FSM verification; and (3) the dead-man timestamp is captured in task context rather than the ESP-NOW receive callback, introducing unbounded latency under queue back-pressure. Several additional major findings (200ms FSM blocking during arm sense verification, missing LINKING state on remote, `wait_for_ack()` discarding critical events) warrant attention before field use.

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

| File | Purpose |
|------|---------|
| `rlc_common/include/rlc_fsm_events.h` | Shared event types, event struct, notification bit definitions |
| `rlc_base/src/rlc_base_fsm.c` | Full base FSM: BOOT→IDLE→ARMED→PRE_FIRE→FIRING→POST_FIRE + LINK_LOST + ERROR |
| `rlc_base/include/rlc_base_fsm.h` | Base FSM public API |
| `rlc_base/src/rlc_fire_timer.c` | GPTimer fire pulse (1 µs resolution, ISR→xTaskNotifyFromISR) |
| `rlc_base/include/rlc_fire_timer.h` | Fire timer API |
| `rlc_remote/src/rlc_remote_fsm.c` | Full remote FSM: BOOT→IDLE→ARMED→PRE_FIRE→FIRING + LINK_LOST + ERROR |
| `rlc_remote/include/rlc_remote_fsm.h` | Remote FSM public API |
| `rlc_common/src/rlc_link.c` | Command frame forwarding, integrity CRC verification, dead-man timestamp, link health |
| `rlc_common/include/rlc_link.h` | Extended link manager API (6 new Phase 3 functions) |
| `rlc_base/src/rlc_base_state.c` | Delegates to FSM getters |
| `rlc_base/include/rlc_base_state.h` | Added `base_state_get_firing_channel()`, `base_state_is_busy()` |
| `rlc_base/src/rlc_siren.c` | Added `siren_start_error()` (3 short blasts 200 ms on/off) |
| `rlc_base/include/rlc_siren.h` | Declared `siren_start_error()` |
| `rlc_base/src/rlc_status_update.c` | Populates `channel_armed_bitmask` and `channel_firing_bitmask` from FSM state |
| `rlc_base/src/rlc_base_main.c` | Starts FSM task, wires arm sense/fault callbacks, sets link guard callback |
| `rlc_remote/src/rlc_remote_main.c` | Starts FSM + fire-repeat tasks, wires all input callbacks |
| `rlc_remote/src/rlc_remote_state.c` | Delegates to FSM getters |
| `rlc_common/include/rlc_config.h` | Added `COMPLETE_PULSE_ON_LINK_LOSS` (default: 1) |
| `rlc_base/CMakeLists.txt` | Added `rlc_base_fsm.c`, `rlc_fire_timer.c` |
| `rlc_remote/CMakeLists.txt` | Added `rlc_remote_fsm.c` |

---

## 1. Coverage Analysis

### Phase 3 Deliverables Checklist (FSD §4.3 Phase 3)

| # | Deliverable | Status | Evidence |
|---|-------------|--------|----------|
| 1 | Base state machine (8 states) | **DONE** | `rlc_base_fsm.c` — BOOT/IDLE/ARMED/PRE_FIRE/FIRING/POST_FIRE/LINK_LOST/ERROR |
| 2 | Base command handler (ARM/DISARM/FIRE/CEASE_FIRE) with guards | **PARTIAL** | `guard_arm()` implements 7 of 10 guards; CRC/token/seq delegated to link layer |
| 3 | Base ACK/NACK responses with reason codes | **DONE** | `send_ack()`, `send_nack()` with `rlc_nack_reason_str()` |
| 4 | Base siren patterns (pulse ARMED, continuous PRE_FIRE/FIRING, error, link_lost) | **DONE** | `siren_start_pulse/continuous/error/link_lost()` |
| 5 | Base fire pulse via hardware timer | **DONE** | `rlc_fire_timer.c` — GPTimer, ISR→`xTaskNotifyFromISR` |
| 6 | Remote state machine (7 states) | **PARTIAL** | Missing LINKING state; BOOT→IDLE directly |
| 7 | Remote command sender with ACK timeout/retry | **DONE** | `send_cmd_arm/fire/disarm/cease_fire()` + `wait_for_ack()` |
| 8 | Remote repeated CMD_FIRE at 200 ms (fire-and-forget) | **DONE** | `cmd_fire_repeat_task_fn` — separate task |
| 9 | Remote dead-man switch logic | **PARTIAL** | Timestamp captured in task context, not ESP-NOW callback (FSD violation) |
| 10 | All safety interlocks from §9 | **PARTIAL** | 3 of 10 arming guards not verified in FSM; FIRING link-loss not handled |
| 11 | App-state guard (reject LINK_REQUEST when armed) | **DONE** | `rlc_link_set_guard(base_state_is_busy)` |
| 12 | ERR_COMM_DEGRADED (>30% failure in 10 pings) | **PARTIAL** | `rlc_link_is_healthy()` computes on-demand; base side has no ping window |

### FSD §7.2 Arming Guards — Verified

| # | Guard (FSD §7.2.2) | Status | Location |
|---|---------------------|--------|----------|
| 1 | Base key switch ARMED (arm sense HIGH) | **DONE** | `guard_arm()` checks `arm_sense_get_debounced()` |
| 2 | Continuity not OPEN | **DONE** | `guard_arm()` checks continuity band |
| 3 | Channel in range 1–8 | **DONE** | `guard_arm()` checks `ch >= 1 && ch <= 8` |
| 4 | No other channel armed | **DONE** | `guard_arm()` checks `s_armed_channel == 0` |
| 5 | Integrity CRC valid | **DELEGATED** | Verified in `rlc_link.c:524-537` before forwarding; not checked in FSM |
| 6 | Session token valid | **DELEGATED** | Verified in `rlc_link.c:521` before forwarding; not checked in FSM |
| 7 | Sequence number valid (anti-replay) | **DELEGATED** | Verified in `rlc_link.c:522` before forwarding; not checked in FSM |
| 8 | Battery above VBAT_MIN_ARM_MV | **DONE** | `guard_arm()` checks battery voltage |
| 9 | Arm sense fault check | **DONE** | Post-energise verification at `rlc_base_fsm.c:251-259` |
| 10 | Link quality acceptable (ERR_COMM_DEGRADED not set) | **DONE** | `guard_arm()` calls `rlc_link_is_healthy()` |

### FSD §9 Safety Requirements — Verified

| # | Requirement | Status | Evidence |
|---|-------------|--------|----------|
| §9.1 | Fail-safe defaults (all outputs off at boot) | **DONE** | `relay_init()` + `siren_init()` before ESP-NOW |
| §9.2 | Dual-key arming (10 guards) | **PARTIAL** | 7 of 10 in FSM; 3 delegated to link layer |
| §9.3 | Single-channel arming only | **DONE** | `guard_arm()` NACK 0x0A if another armed |
| §9.4 | Dead-man switch (500 ms) | **PARTIAL** | Timestamp captured in wrong context |
| §9.5 | Auto-disarm after fire | **DONE** | FIRING→POST_FIRE→IDLE pipeline |
| §9.6 | Watchdog timer | **DONE** | `rlc_watchdog.c` — TWDT per-task registration |
| §9.7 | GPIO init order | **DONE** | `relay_init()` first in both `base_main.c` and `remote_main.c` |
| §9.10 | Task priorities | **PARTIAL** | See §5 findings on link_task core pinning |
| §9.12 | ISR safety | **DONE** | Fire timer ISR uses only `xTaskNotifyFromISR` |
| §9.13 | Boot sequence | **DONE** | All 10 steps followed in both units |

---

## 2. Deviation Report

### CRITICAL

#### C1: FIRING State Silently Ignores Link Loss

**File:** `rlc_base_fsm.c:366-424`
**Spec:** FSD §7.2.5

The FIRING state has **no handler for EVT_LINK_LOST**. The event falls through to the default case (no-op). The FSD requires: "Link lost during FIRING → COMPLETE_PULSE_ON_LINK_LOSS (default true) → complete pulse then LINK_LOST. When false → immediate cut + LINK_LOST." Neither behaviour is implemented. The `COMPLETE_PULSE_ON_LINK_LOSS` configuration constant is defined but never consulted. If the link drops during an active fire pulse, the FSM remains in FIRING indefinitely until the hardware timer expires — the fire pulse completes but no transition to LINK_LOST occurs, and no siren alarm sounds.

#### C2: Three Arming Guards Delegated Without FSM Verification

**File:** `rlc_base_fsm.c:192-217`
**Spec:** FSD §7.2.2 guards 5, 6, 7

The `guard_arm()` function does not check integrity CRC, session token, or sequence number validity. These are verified in `rlc_link.c` (lines 521-522, 524-537) before the command event is forwarded to the FSM queue. However, the FSD lists these as explicit arming guard conditions that must ALL be true. The delegation creates a maintenance risk: if the link layer's validation is relaxed in a future refactor, the FSM will accept invalid commands. The FSD requires the FSM to reject commands that fail these checks — the current architecture implicitly depends on the link layer's correct behaviour without asserting this dependency in the FSM.

#### C3: Dead-Man Timestamp Captured in Task Context, Not ESP-NOW Callback

**File:** `rlc_link.c:542-543`
**Spec:** FSD §6.4.1b, §7.2.4 guard 1

`s_last_fire_cmd_ms` is updated inside `process_frame()`, which runs on the link_task after dequeuing from `s_rx_queue`. The FSD explicitly states: "last CMD_FIRE received timestamp SHALL be updated in the ESP-NOW receive callback, not deferred to the state machine task. This ensures the timestamp is not delayed by lower-priority task scheduling." Under queue back-pressure (which can happen during command bursts), the timestamp reflects queue processing time, not wire-receive time. This introduces unbounded latency that could cause false dead-man timeouts during PRE_FIRE→FIRING transitions.

### MAJOR

#### M1: 200 ms Blocking Poll During Arm Sense Verification

**File:** `rlc_base_fsm.c:251-259`
**Spec:** FSD §7.2.2, §4.7

After energising the arm relay, the FSM polls arm sense in 10 ms intervals for up to 200 ms (`vTaskDelay(pdMS_TO_TICKS(10))`). During this window, the FSM task cannot process any events from its queue — including EVT_CMD_CEASE_FIRE, EVT_LINK_LOST, or EVT_BATTERY_CRITICAL. If the arm relay energises but the sense line does not go HIGH (welding fault), the FSM is deaf to safety-critical events for up to 200 ms while the arm relay is energised. The arm sense change callback already exists and posts EVT_ARM_SENSE_CHANGED; the verification should be refactored to use it.

#### M2: Wrong NACK Code for Link Quality Guard

**File:** `rlc_base_fsm.c:215`
**Spec:** FSD §7.2.2 guard 10, §6.3.3

Guard 10 (link quality check) returns `NACK_REMOTE_BATTERY_LOW` (0x0C) when `rlc_link_is_healthy()` fails. The remote would display "REMOTE BATTERY LOW" when the actual problem is link degradation. No NACK code exists for communication degradation in the protocol — this is either a protocol design gap or the guard should return a more appropriate existing code (e.g., 0x05 WRONG_STATE).

#### M3: Dead-Man Timestamp Updated for Wrong-Channel CMD_FIRE

**File:** `rlc_link.c:542-543`
**Spec:** FSD §7.2.3, §7.2.4

The link layer updates `s_last_fire_cmd_ms` for every CMD_FIRE received, regardless of channel. Wrong-channel CMD_FIRE messages that should be "silently discarded" per FSD §7.2.3 keep the dead-man timestamp alive. An adversary (or a buggy remote) sending CMD_FIRE on the wrong channel would prevent dead-man timeout from triggering.

#### M4: Remote Missing LINKING State

**File:** `rlc_remote_fsm.c:339-344`
**Spec:** FSD §8.1, §8.2.1, §8.2.2

The FSD specifies remote states as "BOOT → LINKING → IDLE → ARMED → PRE_FIRE → FIRING + LINK_LOST + ERROR". The implementation has no LINKING state — the FSM transitions BOOT→IDLE directly on EVT_LINK_ESTABLISHED. The link manager handles the LINK_REQUEST/LINK_ACK handshake internally, but the FSM cannot drive a "Connecting..." display or manage link retry behaviour. Whether this is functionally critical depends on the display module's independent handling of link status.

#### M5: `wait_for_ack()` Discards Critical Events

**File:** `rlc_remote_fsm.c:293-330`
**Spec:** FSD §8.2.3, §8.2.4

During the 500 ms ACK wait, `wait_for_ack()` drains events from the FSM queue. EVT_STATUS_UPDATE and EVT_BATTERY_CRITICAL events arriving during this window are silently discarded. A battery critical event during CMD_ARM ACK wait would be lost, potentially allowing arming with critical battery. STATUS_UPDATE events are dropped, meaning the freshness check at fire time could use stale data.

#### M6: Fire Attempt Does Not Verify STATUS_UPDATE Freshness

**File:** `rlc_remote_fsm.c:436-482`
**Spec:** FSD §8.2.4 guard 1

In ARMED state, fire button press checks `s_last_status.channel_armed_bitmask` to verify the base shows the channel as armed. However, `is_status_fresh()` is not checked at fire time — it was verified at arm time but not re-verified when the fire button is pressed. FSD §8.2.4 guards require "(1) STATUS_UPDATE confirms armed, (2) link healthy." The freshness guard is missing.

#### M7: POST_FIRE Battery Error May Be Silently Dropped

**File:** `rlc_base_fsm.c:418-424, 428-438, 518-526`
**Spec:** FSD §7.2.5

Battery critical during FIRING sets `s_error_flags & ERR_VBAT_CRITICAL` and defers the error to POST_FIRE. In POST_FIRE, the error flag check (line 435) runs inside `process_event()`, not in `check_timers()`. If no events arrive during the 2000 ms cooldown, `check_timers()` transitions POST_FIRE→IDLE at line 523 without ever checking the error flag. The battery critical error is silently dropped.

#### M8: FSM Init Race with Link Task

**File:** `rlc_base_main.c:139-155`, `rlc_remote_main.c:158-174`
**Spec:** FSD §9.13

`rlc_link_init()` starts the link_task immediately. `base_fsm_init()` / `remote_fsm_init()` registers the FSM command queue AFTER the link_task is running. A fast LINK_ACK handshake could cause EVT_LINK_ESTABLISHED to arrive before `s_cmd_queue` is registered. The event would be silently dropped. Typical ESP-NOW latency (50–200 ms) makes this unlikely but not impossible.

#### M9: Base Side Has No Ping Health Window

**File:** `rlc_link.c:676-681`
**Spec:** FSD §7.2.2 guard 10, §7.2.4 guard 4

`s_ping_window[]` is only populated on the remote side (in `tick_remote()` and `handle_pong()`). The base unit receives PINGs but never populates its own health window. `rlc_link_is_healthy()` always returns true on the base because the window is empty (zero failures). This means the base has no ERR_COMM_DEGRADED detection — FSD §7.2.2 guard 10 and §7.2.4 guard 4 cannot be meaningfully enforced.

### MINOR

#### m1: Redundant `arm_relay_set(false)` After `relay_all_safe()`

**File:** `rlc_base_fsm.c:261-264`

In the arm sense fault path, `relay_all_safe()` is called followed by `arm_relay_set(false)`. `relay_all_safe()` already de-energises the arm relay. The second call is redundant but harmless.

#### m2: `fire_timer_stop()` Does Not Clear Pending Notifications

**File:** `rlc_fire_timer.c:77`

If the timer is stopped (CEASE_FIRE) after the ISR fires but before the FSM processes the notification, a spurious EVT_FIRE_PULSE_DONE could be generated on the next event cycle. The FSM should handle this by checking timer state, but the timer module does not defensively clear notifications on stop.

#### m3: `s_channel_ctx` Set But Never Read

**File:** `rlc_fire_timer.c:57`

`s_channel_ctx` is stored during `fire_timer_start()` but never communicated to the receiving task. The ISR only sends `FIRE_NOTIFY_BIT` via `xTaskNotifyFromISR`. The FSM tracks the firing channel via its own `s_firing_channel`.

#### m4: Encoder Button Polling in Main Loop, Not Dedicated Task

**File:** `rlc_remote_main.c:191`
**Spec:** FSD §9.10

FSD §9.10 specifies an `encoder_task` at priority 3, core 0, stack 2048. The implementation runs `encoder_poll_button()` from the main housekeeping loop instead of a dedicated task. This means encoder polling runs at whatever priority the main task has, potentially lower than the FSD-specified priority 3.

#### m5: `s_comm_degraded` Declared But Never Written

**File:** `rlc_link.c:106`

`s_comm_degraded` is declared `volatile bool` but never assigned anywhere. `rlc_link_is_healthy()` recomputes degradation on every call by iterating `s_ping_window`. The variable is dead code.

#### m6: `s_last_fire_cmd_ms` is `volatile int64_t` on 32-bit Processor

**File:** `rlc_link.c:99`

The FSD atomicity justification says "single aligned 32-bit write is atomic on ESP32-S3." But the variable is `int64_t` (8 bytes), and ESP32-S3 is a 32-bit Xtensa. 64-bit writes are NOT atomic. The mutex protection in `rlc_link_get_last_fire_ms()` is correct, but the volatile declaration contradicts the atomicity rationale.

#### m7: `s_trigger` Read-Then-Clear Race in Status Update

**File:** `rlc_status_update.c:25, 75, 80`

`s_trigger` is read at line 75 and cleared at line 80. If `status_update_trigger()` fires between these two lines, the new trigger is lost. Worst case: event-driven STATUS_UPDATE delayed by one periodic interval (2 s). Not safety-critical.

#### m8: Wrong FSD Section References in Comments

**Files:** `rlc_base_fsm.c:279, 467`

Comments reference FSD §7.2.2 and §7.2.5 where §7.2.7 and §7.2.7 are correct. Minor documentation error.

---

## 3. Plan vs. Implementation

No implementation plan file was found (`Implementation_Plan_*.md` does not exist). Section skipped.

---

## 4. Edge Cases & Safety

### Safety-Critical Scenarios

| # | Scenario | FSD Ref | Assessment | Risk |
|---|----------|---------|------------|------|
| S1 | Link lost during active fire pulse | §7.2.5 | **CRITICAL (C1)** — EVT_LINK_LOST silently ignored in FIRING state. Fire pulse completes but no LINK_LOST transition or siren alarm occurs. | **HIGH** |
| S2 | Wrong-channel CMD_FIRE keeps dead-man alive | §7.2.3 | **MAJOR (M3)** — Dead-man timestamp updated for any CMD_FIRE, even wrong-channel. | **MEDIUM** |
| S3 | Arm sense verification blocks FSM for 200 ms | §7.2.2 | **MAJOR (M1)** — CEASE_FIRE and link loss unprocessable during welding check. | **MEDIUM** |
| S4 | Battery critical during FIRING → error dropped in POST_FIRE | §7.2.5 | **MAJOR (M7)** — If no events arrive during cooldown, error flag never checked. | **LOW** |
| S5 | Fast link handshake before FSM queue registered | §9.13 | **MAJOR (M8)** — EVT_LINK_ESTABLISHED could be dropped, leaving FSM stuck in BOOT. | **LOW** |
| S6 | `wait_for_ack()` discards battery critical | §8.2.3 | **MAJOR (M5)** — Critical battery event lost during CMD_ARM ACK wait. | **MEDIUM** |
| S7 | Arm timeout fires during 200 ms welding check | §7.2.7 | Blocked by M1 — arm timeout check cannot run during the poll loop. | **LOW** |
| S8 | CEASE_FIRE during arm sense verification | §7.2.7 | Blocked by M1 — CEASE_FIRE not processable during 200 ms window. | **MEDIUM** |
| S9 | Fire pulse timer ISR fires after CEASE_FIRE stops timer | §7.4.2 | **MINOR (m2)** — Stale notification could generate spurious EVT_FIRE_PULSE_DONE. | **LOW** |

### Edge Cases Verified Correct

| # | Scenario | Assessment |
|---|----------|------------|
| S10 | Arm relay energise + sense HIGH verified within 200 ms | Correct — `rlc_base_fsm.c:251-259` polls at 10 ms intervals |
| S11 | CMD_DISARM in IDLE returns idempotent ACK | Correct — `rlc_base_fsm.c:278-284` |
| S12 | Wrong-channel CMD_FIRE in PRE_FIRE/FIRING silently discarded | Correct — `rlc_base_fsm.c:345-348` |
| S13 | Multi-arm detection (remote checks channel_armed_bitmask) | Correct — handled in remote FSM |
| S14 | Arm timeout (10 s) auto-disarms | Correct — `check_timers()` in base FSM |
| S15 | Post-fire cooldown prevents immediate re-arm | Correct — POST_FIRE NACKs CMD_ARM with 0x05 |
| S16 | LINK_REQUEST rejected during ARMED/PRE_FIRE/FIRING/POST_FIRE | Correct — `base_fsm_is_busy()` guard callback |
| S17 | Siren silenced immediately on LINK_LOST→IDLE recovery | Correct — `do_enter_idle()` calls `siren_off()` |

---

## 5. Concurrency & Platform Issues

### Thread Safety Assessment

| Shared Resource | Writer(s) | Reader(s) | Protection | Assessment |
|----------------|-----------|-----------|------------|------------|
| `s_state` (base FSM) | FSM task | `status_update_task`, `base_state_*()` getters | `volatile` | **ACCEPTABLE** — single writer, aligned byte read atomic on ESP32-S3 |
| `s_armed_channel` (remote FSM) | FSM task | `fire_repeat_task` via `volatile uint8_t*` | `volatile` | **MINOR RISK** — TOCTOU between reading channel and active flag |
| `s_fire_repeat_active` (remote FSM) | FSM task | `fire_repeat_task` | `volatile` | **MINOR RISK** — same as above |
| `s_last_fire_cmd_ms` | link_task | FSM task via `rlc_link_get_last_fire_ms()` | `s_state_mutex` | **CORRECT** — mutex-protected read |
| `s_cmd_queue` | link_task | FSM task | FreeRTOS queue | **CORRECT** — single-consumer queue |
| `s_trigger` (status update) | `continuity_task`, `arm_sense_task` | `status_update_task` | `volatile bool` | **MINOR RACE** — read-then-clear without atomic; periodic timer provides safety net |
| `s_ping_window[]` | link_task (remote only) | `rlc_link_is_healthy()` | none (single writer) | **ACCEPTABLE** on remote; **GAP** on base (window never populated) |

### Platform API Issues

| # | Issue | File | Assessment |
|---|-------|------|------------|
| P1 | Link task uses `xTaskCreate` without core pinning | `rlc_link.c:747` | FSD §9.10 specifies core 0; scheduler may choose core 1 |
| P2 | `esp_timer` callback (siren) runs in timer task context | `rlc_siren.c:17` | Safe — single caller from FSM task, timer task only reads |
| P3 | Fire timer ISR correctly uses `xTaskNotifyFromISR` with `pxHigherPriorityTaskWoken` | `rlc_fire_timer.c:23-34` | Correct pattern |
| P4 | GPTimer `user_ctx` passed as NULL | `rlc_fire_timer.c:48` | Channel context stored in static instead of passed through ISR mechanism |

---

## 6. Error Handling

| # | Module | Error Condition | Handling | Assessment |
|---|--------|----------------|----------|------------|
| E1 | Base FSM arm sense | Sense does not go HIGH within 200 ms | `relay_all_safe()` + ERROR state | **CORRECT** |
| E2 | Base FSM FIRING | Battery critical | Error flag set, deferred to POST_FIRE | **PARTIAL** — flag may be dropped if no events arrive in POST_FIRE (M7) |
| E3 | Base FSM FIRING | Link lost | **NOT HANDLED** — EVT_LINK_LOST silently ignored (C1) | **CRITICAL** |
| E4 | Base FSM POST_FIRE | Error flag from FIRING | Checked in `process_event()` only | **PARTIAL** — not checked in `check_timers()` (M7) |
| E5 | Remote FSM | Battery critical during ACK wait | **SILENTLY DROPPED** — event consumed and discarded in `wait_for_ack()` (M5) | **MAJOR** |
| E6 | Link layer | Sequence number overflow | `rlc_link_next_seq()` returns 0, callers check and reject | **CORRECT** |
| E7 | Link layer | Queue full on command forward | Non-blocking send drops event silently | **ACCEPTABLE** — depth 16 adequate for normal operation |
| E8 | Fire timer | GPTimer init failure | `ESP_ERROR_CHECK` aborts at boot | **CORRECT** — safety-critical, must not continue without timer |

---

## 7. Code Quality

### Strengths

| # | Area | Evidence |
|---|------|---------|
| Q1 | Single-task-owner model | All relay operations happen exclusively in the FSM task — no relay access from ISR, link task, or other tasks |
| Q2 | Clean event architecture | Unified `rlc_fsm_event_t` with union payload; all I/O and communication events funnel through a single queue |
| Q3 | Correct ISR safety | Fire timer ISR uses only `xTaskNotifyFromISR`; no GPIO, mutex, or state machine calls in ISR context |
| Q4 | Consistent pattern | All modules follow Phase 1/2 patterns: `*_init()`, `*_start_task()`, callback registration |
| Q5 | Safety-first defaults | `COMPLETE_PULSE_ON_LINK_LOSS` defaults to true; arm timeout 10 s; dead-man 500 ms; all relays de-energised at boot |
| Q6 | Configurable constants | All timing values in `rlc_config.h`; no magic numbers in FSM code |

### Issues

| # | File | Issue |
|---|------|-------|
| Q1 | `rlc_base_fsm.h:72` | Comment says "Returns false if in ARMED/PRE_FIRE/FIRING/POST_FIRE" — inverted (returns true for those states) |
| Q2 | `rlc_fire_timer.c:57` | `s_channel_ctx` stored but never read — dead code |
| Q3 | `rlc_link.c:106` | `s_comm_degraded` declared but never written — dead code |
| Q4 | `rlc_config.h:30` | `CMD_RETRY_COUNT` defined but never referenced |
| Q5 | `rlc_remote_fsm.c` | `wait_for_ack()` inline event handling duplicates logic from `process_event()` |

---

## 8. Summary

| Category | Critical | Major | Minor | Info |
|----------|----------|-------|-------|------|
| Spec conformance | 3 (C1, C2, C3) | 3 (M4, M6, M9) | 2 (m4, m8) | 0 |
| Correctness | 0 | 4 (M2, M3, M5, M7) | 3 (m1, m2, m3) | 2 |
| Safety | 0 | 1 (M1) | 1 (m5) | 1 |
| Concurrency | 0 | 1 (M8) | 2 (m6, m7) | 1 |
| Error handling | 0 | 0 | 0 | 2 |
| Code quality | 0 | 0 | 0 | 5 |

### Phase 2 Review Follow-Up

| P2 Finding | Status in P3 |
|------------|--------------|
| M1: Welding callback semantics ambiguous | **ADDRESSED** — Separate `arm_sense_register_fault_cb()` API wired in `rlc_base_main.c:123`; FSM receives distinct EVT_ARM_SENSE_FAULT event |
| M3: Encoder poll not called from task | **PARTIAL** — `encoder_poll_button()` called from remote main housekeeping loop (not a dedicated task). FSD §9.10 `encoder_task` not created. |

---

## 9. Recommendation

**Do not proceed to on-target testing** until the three critical findings are resolved.

### Must-Fix Before Testing (Critical)

1. **C1: FIRING link-loss handler.** Add an EVT_LINK_LOST case to the FIRING state that checks `COMPLETE_PULSE_ON_LINK_LOSS`: if true, set a flag to transition to LINK_LOST after the fire timer expires; if false, immediately call `relay_all_safe()` and transition to LINK_LOST. This is a direct FSD §7.2.5 requirement.

2. **C2: Document or assert delegated arming guards.** The link layer already validates CRC/token/sequence before forwarding commands to the FSM queue. Add explicit comments in `guard_arm()` documenting this dependency, and/or add defensive asserts that verify the event was forwarded (not injected). If the architecture is intentional (link layer as gatekeeper), the FSD mapping should acknowledge the delegation.

3. **C3: Dead-man timestamp in receive callback.** Move `s_last_fire_cmd_ms` update from `process_frame()` to `rlc_link_on_rx()` (the ESP-NOW receive callback). Add a `received_ms` field to `link_rx_item_t` so the timestamp is captured at interrupt time and passed through the queue, or check the message type in the callback and update the timestamp there.

### Should-Fix Before Commit (Major)

4. **M1: Non-blocking arm sense verification.** Refactor the 200 ms polling loop to use the existing EVT_ARM_SENSE_CHANGED event. Energise the arm relay, start a 200 ms timeout, and handle the sense change or timeout as events in the normal event loop.

5. **M7: POST_FIRE error flag check in `check_timers()`.** Add `if (s_error_flags & ERR_VBAT_CRITICAL) { enter_error(); return; }` to `check_timers()` so the error is detected even if no events arrive during the cooldown.

6. **M9: Base-side ping health tracking.** Populate `s_ping_window[]` on the base side (in `handle_ping()` or `tick_base()`) so `rlc_link_is_healthy()` returns meaningful results on the base unit.

7. **M2: Correct NACK code for link quality guard.** Either add a `NACK_COMM_DEGRADED` reason code to the protocol, or use an existing code that does not mislead the operator.

### Can Defer to Hardening (Minor)

8. **M3–M6, M8, m1–m8** — Address during Phase 5 hardening. None are safety-critical blockers for on-target testing of the core fire sequence.

---

*End of Phase 3 Code Review — RLC-REVIEW-P3-001*
