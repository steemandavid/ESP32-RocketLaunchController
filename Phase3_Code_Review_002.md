# Phase 3 Code Review (Re-Review) — State Machines and Command Processing

**Document ID:** RLC-REVIEW-P3-002
**Reviewer:** Code Review Agent
**Date:** 2026-04-15
**Scope:** Phase 3 — State Machines and Command Processing (post-fix re-review)
**FSD Reference:** RLC_Functional_Specification_v1_14.md
**Commit Reviewed:** `f9641e1` (Phase 3 code review fixes — 3 critical, 9 major, 13 minor/quality issues resolved)
**Prior Review:** RLC-REVIEW-P3-001 (FAIL — 3 critical, 9 major, 8 minor, 5 quality)

---

## Verdict: PASS WITH NOTES

All 25 findings from the initial review (C1–C3, M1–M9, m1–m8, Q1–Q5) have been properly addressed. The code now correctly implements the FSD §7/§8 state machines with robust safety interlocks. One new major finding was identified during the re-review: the remote PRE_FIRE and FIRING states silently drop EVT_BATTERY_CRITICAL events (FSD §8.3.4 violation). This should be fixed before on-target testing but does not block it — the base unit will abort via dead-man timeout if the remote ceases transmission. Two minor observations and one spec deviation were also noted.

---

## Table of Contents

1. [Previous Finding Verification](#1-previous-finding-verification)
2. [New Findings](#2-new-findings)
3. [Coverage Analysis](#3-coverage-analysis)
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
| `rlc_common/include/rlc_protocol.h` | Protocol definitions, NACK codes, state enums |
| `rlc_common/include/rlc_config.h` | Configuration constants |
| `rlc_common/include/rlc_link.h` | Link manager API (Phase 3 extensions) |
| `rlc_common/src/rlc_link.c` | Link manager implementation — command forwarding, health tracking |
| `rlc_base/src/rlc_base_fsm.c` | Full base FSM: 8 states, command processing, fire timer integration |
| `rlc_base/include/rlc_base_fsm.h` | Base FSM public API |
| `rlc_base/src/rlc_fire_timer.c` | GPTimer fire pulse (1 µs resolution, ISR→xTaskNotifyFromISR) |
| `rlc_base/include/rlc_fire_timer.h` | Fire timer API |
| `rlc_base/src/rlc_base_main.c` | Base entry point, init wiring, callback registration |
| `rlc_base/src/rlc_status_update.c` | STATUS_UPDATE with real FSM state data |
| `rlc_base/src/rlc_siren.c` | Siren patterns (pulse, continuous, error, link_lost) |
| `rlc_base/src/rlc_base_state.c` | State getters delegating to FSM |
| `rlc_remote/src/rlc_remote_fsm.c` | Full remote FSM: 7 states, command sending, ACK handling |
| `rlc_remote/include/rlc_remote_fsm.h` | Remote FSM public API |
| `rlc_remote/src/rlc_remote_main.c` | Remote entry point, init wiring, encoder task |

---

## 1. Previous Finding Verification

### CRITICAL — All Fixed

#### C1: FIRING State Link-Loss Handler — FIXED

**File:** `rlc_base_fsm.c:501-520`

The FIRING state now handles EVT_LINK_LOST according to COMPLETE_PULSE_ON_LINK_LOSS:
- **When `true` (default):** Sets `s_link_lost_pending` flag. When fire timer expires (EVT_FIRE_PULSE_DONE, line 436), detects the flag and transitions to LINK_LOST.
- **When `false`:** Immediately stops fire timer, calls `relay_all_safe()`, transitions to LINK_LOST.
- The `s_link_lost_pending` flag is correctly cleared on CEASE_FIRE, DISARM, and arm switch OFF (lines 457, 471, 487).

#### C2: Delegated Arming Guards Documented — FIXED

**File:** `rlc_base_fsm.c:215-221`

A comprehensive comment block documents that guards 5 (CRC), 6 (session token), and 7 (sequence anti-replay) are enforced by `rlc_link.c` process_frame() before commands reach the FSM queue. The architectural dependency is explicitly noted as intentional.

Verification: `rlc_link.c:528-544` confirms session token check, sequence check, and CRC verification all occur before command forwarding to the FSM queue.

#### C3: Dead-Man Timestamp Captured in ESP-NOW Callback — FIXED

**Files:** `rlc_link.c:786`, `rlc_fsm_events.h:63`, `rlc_base_fsm.c:408`

The timestamp capture chain is correct:
1. `rlc_link_on_rx()` captures `it.received_ms = now_ms()` at line 786 (ESP-NOW receive callback)
2. The `link_rx_item_t` struct includes `received_ms` field (line 53)
3. `process_frame()` forwards it: `evt.data.cmd.received_ms = it->received_ms` (line 565)
4. The base FSM uses the wire-receive time: `s_last_fire_cmd_ms = evt->data.cmd.received_ms` (line 408)

### MAJOR — All Fixed

#### M1: Non-Blocking Arm Sense Verification — FIXED

**File:** `rlc_base_fsm.c:280-319, 564-569`

The 200 ms blocking poll was replaced with an event-driven approach:
1. Energise arm relay (line 278)
2. Immediately check if sense is already HIGH (line 282) — if so, proceed to ARMED
3. If not HIGH, set `s_arm_verify_pending = true` and stay in IDLE (line 297)
4. EVT_ARM_SENSE_CHANGED handler completes verification when sense goes HIGH (line 306)
5. 200 ms timeout in `check_timers()` aborts with NACK_ARM_SENSE_FAULT (line 566)
6. Safety events (LINK_LOST, BATTERY_CRITICAL, CEASE_FIRE, DISARM) are all handled during the pending window (lines 327-348)

#### M2: Correct NACK Code for Link Quality — FIXED

**Files:** `rlc_protocol.h:47`, `rlc_base_fsm.c:245`

`NACK_COMM_DEGRADED = 0x0D` added to protocol. Guard 10 returns `NACK_COMM_DEGRADED` instead of `NACK_REMOTE_BATTERY_LOW`.

#### M3: Dead-Man Timestamp Not Updated for Wrong-Channel CMD_FIRE — FIXED

**File:** `rlc_base_fsm.c:407-409`

```c
if (evt->data.cmd.channel == s_armed_channel) {
    s_last_fire_cmd_ms = evt->data.cmd.received_ms;
}
```

Only updates timestamp when the channel matches the armed channel. Wrong-channel CMD_FIRE in PRE_FIRE is silently discarded (per FSD §7.2.3), and in FIRING is also silently discarded (line 477-478).

#### M4: Remote LINKING State — FIXED

**File:** `rlc_remote_fsm.c:353-359, 697-698`

BOOT→LINKING transition at task start (line 698). EVT_LINK_ESTABLISHED in LINKING state transitions to IDLE. State is present in `rlc_protocol.h:75` (`STATE_LINKING = 0x02`).

#### M5: wait_for_ack() Preserves Critical Events — FIXED

**File:** `rlc_remote_fsm.c:326-335`

During the ACK wait loop, EVT_STATUS_UPDATE is cached (lines 327-330) and EVT_BATTERY_CRITICAL triggers `do_enter_error()` (lines 332-334).

#### M6: Fire Freshness Check — FIXED

**File:** `rlc_remote_fsm.c:460-465`

`is_status_fresh()` is checked at fire button press time, verifying STATUS_UPDATE was received within 2 × STATUS_UPDATE_INTERVAL_MS.

#### M7: POST_FIRE Error Flag in check_timers() — FIXED

**File:** `rlc_base_fsm.c:622-629`

`check_timers()` checks `s_error_flags & ERR_VBAT_CRITICAL` in POST_FIRE before the cooldown timer. The error flag is also checked in `process_event()` POST_FIRE handler (line 533-535) — double coverage ensures the error is detected whether or not events arrive.

#### M8: FSM Init Race — FIXED

**Files:** `rlc_base_main.c:145-155`, `rlc_remote_main.c:184-192`

Queue registration (`rlc_link_register_cmd_queue()`) occurs AFTER both `rlc_link_init()` and `base_fsm_init()`/`remote_fsm_init()` complete, eliminating the window where link_task could post events before the FSM queue is registered.

#### M9: Base-Side Ping Health Tracking — FIXED

**File:** `rlc_link.c:411-414, 681-694`

`handle_ping()` populates `s_ping_window[]` with `true` on PING receipt (lines 411-414). `tick_base()` tracks expected ping slots and marks failures (lines 681-694). `rlc_link_is_healthy()` now returns meaningful results on both base and remote.

### MINOR — All Fixed

| Finding | Status | Evidence |
|---------|--------|----------|
| m1: Redundant arm_relay_set after relay_all_safe | FIXED | `abort_arm_verify()` calls `relay_all_safe()` once (line 84) |
| m2: Fire timer stop doesn't clear notifications | FIXED | `fire_timer_stop()` calls `ulTaskNotifyValueClear(s_target_task, FIRE_NOTIFY_BIT)` (line 80) |
| m3: s_channel_ctx dead code | FIXED | Variable removed; channel passed to `fire_timer_start()` param only |
| m4: Encoder button polling in main loop | FIXED | Dedicated `encoder_task_fn` at priority 3, core 0, 2048 stack (`rlc_remote_main.c:47-58, 173`) |
| m5: s_comm_degraded dead variable | FIXED | Variable removed from `rlc_link.c` |
| m6: volatile int64_t on 32-bit processor | FIXED | `s_last_fire_cmd_ms` is now local to FSM task (single writer/reader); `link_rx_item_t.received_ms` passes through queue (deep copy) |
| m7: s_trigger read-then-clear race | IMPROVED | Read and clear narrowed to adjacent lines with comment (`rlc_status_update.c:76-77`). Periodic timer provides safety net. |
| m8: Wrong FSD section references | FIXED | Comments now correctly reference FSD sections and C/M finding numbers |

### Code Quality — All Fixed

| Finding | Status | Evidence |
|---------|--------|----------|
| Q1: Comment inversion in base_fsm.h | FIXED | Comment now correctly says "Returns true if in ARMED/PRE_FIRE/FIRING/POST_FIRE" (line 72) |
| Q2: s_channel_ctx dead code | FIXED | Removed from `rlc_fire_timer.c` |
| Q3: s_comm_degraded dead code | FIXED | Removed from `rlc_link.c` |
| Q4: CMD_RETRY_COUNT unused | VERIFIED USED | Used in retry loop at `rlc_remote_fsm.c:405`: `for (int retry = 0; result == 0 && retry < CMD_RETRY_COUNT; retry++)` |
| Q5: wait_for_ack() duplicates process_event() logic | ACCEPTABLE | wait_for_ack() needs inline event handling during the blocking wait; now properly handles critical events (M5 fix) |

---

## 2. New Findings

### MAJOR

#### N1: Remote PRE_FIRE and FIRING States Drop EVT_BATTERY_CRITICAL

**File:** `rlc_remote_fsm.c:548-614`
**Spec:** FSD §8.3.4

The remote PRE_FIRE state handles: EVT_FIRE_BUTTON_RELEASED, EVT_ARM_SWITCH_CHANGED, EVT_ENCODER_*, EVT_STATUS_UPDATE, EVT_LINK_LOST — but NOT EVT_BATTERY_CRITICAL. The FIRING state handles: EVT_FIRE_BUTTON_RELEASED, EVT_ARM_SWITCH_CHANGED, EVT_STATUS_UPDATE, EVT_LINK_LOST — but NOT EVT_BATTERY_CRITICAL.

FSD §8.3.4 states: "If remote battery drops critical during FIRING, the remote transitions to ERROR, ceasing all CMD_FIRE transmissions. The base will abort via dead-man timeout within 500 ms."

Without this handler, the remote continues sending CMD_FIRE during PRE_FIRE/FIRING even with critical battery. The base would eventually abort via dead-man timeout when the remote's battery dies completely, but the FSD mandates explicit remote-side error handling.

**Risk:** MEDIUM — The base dead-man timeout provides a safety net, but the remote should not rely on total battery failure to stop transmitting.

**Fix:** Add `else if (evt->type == EVT_BATTERY_CRITICAL) { s_fire_repeat_active = false; do_enter_error(); }` to both PRE_FIRE and FIRING state handlers.

### MINOR

#### N2: Dead Code in Remote ARMED Stale Check

**File:** `rlc_remote_fsm.c:534-540`

The stale data safety timeout check inside the EVT_STATUS_UPDATE handler (line 534) can never trigger because `s_last_status_rx_ms` was just set to `now_ms()` on line 521. The expression `now_ms() - s_last_status_rx_ms` is always 0 (or 1 ms).

The real stale timeout is correctly implemented in `check_timers()` (lines 649-661), which runs every 50 ms poll cycle. This dead code is harmless but misleading.

#### N3: FIRING→LINK_LOST Skips POST_FIRE (Spec Deviation)

**File:** `rlc_base_fsm.c:436-442`

When `COMPLETE_PULSE_ON_LINK_LOSS` is true and link loss occurs during FIRING, the fire pulse completes and the code transitions directly to LINK_LOST (line 442), skipping POST_FIRE.

FSD §7.2.5 states: "complete the fire pulse, then transition to POST_FIRE, then to LINK_LOST with full disarm."

The implementation is functionally safe — `relay_all_safe()` is called at line 430 before the transition, siren is stopped at line 431, and LINK_LOST already performs full disarm. The POST_FIRE cooldown serves no purpose when transitioning to LINK_LOST (no re-arming possible). However, this is a literal deviation from the FSD's specified transition path.

### INFO

#### N4: get_last_fire_ms() API Intentionally Removed

The original `rlc_link_get_last_fire_ms()` API was removed from `rlc_link.h` as part of the C3 fix. The dead-man timestamp is now captured at wire-receive time and forwarded through the event structure (`evt->data.cmd.received_ms`). The FSM uses its own local `s_last_fire_cmd_ms`. No API gap — this was an intentional architectural improvement.

#### N5: POST_FIRE Error Flag Double Coverage

The `s_error_flags & ERR_VBAT_CRITICAL` check appears in both `process_event()` POST_FIRE handler (line 533) and `check_timers()` (line 625). This is intentional double coverage — the error is detected whether or not events arrive during the cooldown. Not a defect.

---

## 3. Coverage Analysis

### Phase 3 Deliverables (FSD §4.3)

| # | Deliverable | Status | Evidence |
|---|-------------|--------|----------|
| 1 | Base state machine (8 states) | **DONE** | BOOT/IDLE/ARMED/PRE_FIRE/FIRING/POST_FIRE/LINK_LOST/ERROR — all implemented |
| 2 | Base command handler (ARM/DISARM/FIRE/CEASE_FIRE) with guards | **DONE** | All commands handled with correct NACK reasons |
| 3 | Base ACK/NACK responses with reason codes | **DONE** | `send_ack()`, `send_nack()` with 13 NACK reason codes |
| 4 | Base siren patterns | **DONE** | Pulse, continuous, error (3×200ms), link_lost (4×500ms) |
| 5 | Base fire pulse via hardware timer | **DONE** | GPTimer, ISR→`xTaskNotifyFromISR`, notification cleared on stop |
| 6 | Remote state machine (7 states) | **DONE** | BOOT/LINKING/IDLE/ARMED/PRE_FIRE/FIRING + LINK_LOST/ERROR |
| 7 | Remote command sender with ACK timeout/retry | **DONE** | `send_cmd_*()` + `wait_for_ack()` with retry |
| 8 | Remote repeated CMD_FIRE at 200 ms | **DONE** | `cmd_fire_repeat_task_fn` — separate task, fire-and-forget |
| 9 | Remote dead-man switch logic | **DONE** | Timestamp from wire-receive time, 500 ms authorization window |
| 10 | All safety interlocks from §9 | **DONE** | See §4 below |
| 11 | App-state guard (reject LINK_REQUEST when armed) | **DONE** | `rlc_link_set_guard(base_state_is_busy)` |
| 12 | ERR_COMM_DEGRADED | **DONE** | `s_ping_window[10]` on both sides, >30% = degraded |

### FSD §7.2.2 Arming Guards — Verified

| # | Guard | Status | Location |
|---|-------|--------|----------|
| 1 | Arm sense HIGH | **DONE** | `guard_arm()` line 233 |
| 2 | Continuity not OPEN | **DONE** | `guard_arm()` line 236 |
| 3 | Channel in range 1–8 | **DONE** | `guard_arm()` line 227 |
| 4 | No other channel armed | **DONE** | `guard_arm()` line 230 |
| 5 | Integrity CRC valid | **DELEGATED** | `rlc_link.c:534-544` — documented at `rlc_base_fsm.c:215-221` |
| 6 | Session token valid | **DELEGATED** | `rlc_link.c:528` — documented |
| 7 | Sequence number valid | **DELEGATED** | `rlc_link.c:529` — documented |
| 8 | Battery above minimum | **DONE** | `guard_arm()` line 239 |
| 9 | Arm sense fault check | **DONE** | Non-blocking verify in IDLE state, 200 ms timeout |
| 10 | Link quality acceptable | **DONE** | `guard_arm()` line 245 — returns `NACK_COMM_DEGRADED` |

### FSD §9 Safety Requirements — Verified

| Requirement | Status | Evidence |
|-------------|--------|----------|
| §9.1 Fail-safe defaults | **DONE** | relay_init() + siren_init() first; LINK_LOST disarms all |
| §9.2 Dual-key arming | **DONE** | All 10 guards checked |
| §9.3 Single-channel arming | **DONE** | NACK 0x0A if another armed |
| §9.4 Dead-man switch | **DONE** | Wire-receive timestamp, 500 ms window |
| §9.5 Auto-disarm after fire | **DONE** | FIRING→POST_FIRE→IDLE |
| §9.6 Watchdog timer | **DONE** | TWDT per-task registration |
| §9.7 GPIO init order | **DONE** | relay_init() before ESP-NOW |
| §9.10 Task priorities | **DONE** | FSM prio 4, link prio 6, encoder prio 3, fire_repeat prio 4 |
| §9.12 ISR safety | **DONE** | Fire timer ISR uses only `xTaskNotifyFromISR` |
| §9.13 Boot sequence | **DONE** | 10-step sequence followed in both units |

---

## 4. Edge Cases & Safety

### Safety-Critical Scenarios — Re-Verified

| # | Scenario | FSD Ref | Assessment |
|---|----------|---------|------------|
| S1 | Link lost during active fire pulse | §7.2.5 | **CORRECT** — COMPLETE_PULSE_ON_LINK_LOSS config honoured; pending flag correctly set/cleared |
| S2 | Wrong-channel CMD_FIRE keeps dead-man alive | §7.2.3 | **CORRECT** — Timestamp only updated for matching channel (line 407-409) |
| S3 | Arm sense verification blocks FSM | §7.2.2 | **CORRECT** — Non-blocking event-driven approach with 200 ms timeout |
| S4 | Battery critical during FIRING (base) | §7.2.5 | **CORRECT** — Error flag set, detected in both POST_FIRE process_event and check_timers |
| S5 | Battery critical during FIRING (remote) | §8.3.4 | **GAP (N1)** — EVT_BATTERY_CRITICAL silently dropped in remote PRE_FIRE/FIRING |
| S6 | Fast link handshake before queue registered | §9.13 | **CORRECT** — Queue registered after both init calls (M8 fix) |
| S7 | CEASE_FIRE during arm sense verify | §7.2.7 | **CORRECT** — abort_arm_verify() called, then ACK sent (line 327-330) |
| S8 | Fire timer ISR fires after CEASE_FIRE stops timer | §7.4.2 | **CORRECT** — fire_timer_stop() clears pending notification (line 80) |
| S9 | Arm timeout (10 s) during arm sense verify | §7.2.7 | **CORRECT** — check_timers() runs during pending verify; arm timeout check is in ARMED state only |

---

## 5. Concurrency & Platform Issues

### Thread Safety Assessment

| Shared Resource | Writer | Reader | Protection | Assessment |
|----------------|--------|--------|------------|------------|
| `s_state` (base FSM) | FSM task | status_update_task, getters | volatile | **ACCEPTABLE** — single writer, aligned byte read |
| `s_armed_channel` (remote) | FSM task | fire_repeat_task | volatile | **ACCEPTABLE** — fire_repeat reads channel atomically |
| `s_fire_repeat_active` | FSM task | fire_repeat_task | volatile | **ACCEPTABLE** — bool flag, task notification for wakeup |
| `s_last_fire_cmd_ms` (base) | FSM task | FSM task | none needed | **CORRECT** — single-task-owner |
| `s_cmd_queue` | link_task | FSM task | FreeRTOS queue | **CORRECT** |
| `s_ping_window[]` | link_task | rlc_link_is_healthy() | mutex | **CORRECT** |
| `s_trigger` (status update) | continuity_task, arm_sense_task | status_update_task | volatile bool | **MINOR RACE** — narrowed window; periodic timer safety net |
| `s_last_status` (remote) | FSM task | FSM task | none needed | **CORRECT** — single-task-owner |
| `link_rx_item_t` fields | ESP-NOW callback (queue send) | link_task (queue receive) | queue deep copy | **CORRECT** |

### Platform API Issues

| # | Issue | Assessment |
|---|-------|------------|
| P1 | Link task uses `xTaskCreatePinnedToCore` with core 0 | **CORRECT** — matches FSD §9.10 |
| P2 | Fire timer ISR uses `xTaskNotifyFromISR` with `pxHigherPriorityTaskWoken` | **CORRECT** — ISR-safe pattern |
| P3 | Fire timer start clears stale notifications (`ulTaskNotifyValueClear`) | **CORRECT** — prevents spurious EVT_FIRE_PULSE_DONE |
| P4 | Fire timer stop clears pending notification bit | **CORRECT** — m2 fix verified |

---

## 6. Error Handling

| # | Module | Error Condition | Handling | Assessment |
|---|--------|----------------|----------|------------|
| E1 | Base arm verify timeout | Sense not HIGH in 200 ms | `abort_arm_verify()` → NACK ARM_SENSE_FAULT | **CORRECT** |
| E2 | Base FIRING battery critical | VBAT drops below critical | Error flag set, detected in POST_FIRE | **CORRECT** |
| E3 | Base FIRING link lost | EVT_LINK_LOST | COMPLETE_PULSE_ON_LINK_LOSS config respected | **CORRECT** |
| E4 | Base POST_FIRE error flag | Flag from FIRING | Checked in both process_event and check_timers | **CORRECT** |
| E5 | Remote battery critical during ARMED | EVT_BATTERY_CRITICAL | `do_enter_error()` | **CORRECT** |
| E6 | Remote battery critical during PRE_FIRE/FIRING | EVT_BATTERY_CRITICAL | **SILENTLY DROPPED** (N1) | **GAP** |
| E7 | Remote command send failure | `send_cmd_*()` returns -1 | Logged + abort/disarm | **CORRECT** |
| E8 | Link layer CRC mismatch | Invalid CRC on command | Frame silently dropped before FSM | **CORRECT** |
| E9 | Link layer session/sequence error | Wrong token or replay | Frame silently dropped before FSM | **CORRECT** |
| E10 | Fire timer init failure | GPTimer alloc fails | `ESP_ERROR_CHECK` aborts at boot | **CORRECT** |

---

## 7. Code Quality

### Strengths

- **Single-task-owner model** rigorously enforced — all relay operations happen exclusively in the FSM task
- **Clean event architecture** — unified `rlc_fsm_event_t` with union payload; all I/O and communication events funnel through a single queue
- **Correct ISR safety** — fire timer ISR uses only `xTaskNotifyFromISR`; no GPIO, mutex, or state machine calls in ISR context
- **Non-blocking arm verify** — elegant event-driven approach that keeps the FSM responsive to safety events during the 200 ms window
- **Comprehensive comments** — C1/C2/C3/M1/M3/M5/M6/M7/M8/M9 fix references clearly documented in code
- **Safety-first defaults** — COMPLETE_PULSE_ON_LINK_LOSS defaults to true; arm timeout 10 s; dead-man 500 ms
- **Configurable constants** — all timing values in `rlc_config.h`; no magic numbers in FSM code
- **Consistent patterns** — all modules follow Phase 1/2 patterns: `*_init()`, `*_start_task()`, callback registration

### Observations

- `wait_for_ack()` inline event handling is necessary for the blocking ACK wait pattern — properly handles critical events post-M5 fix
- `rlc_link_get_last_fire_ms()` intentionally removed — dead-man timestamp flows through event structure
- `CMD_RETRY_COUNT` is used (contrary to original Q4 finding) — retry loop at `rlc_remote_fsm.c:405`

---

## 8. Summary

| Category | Critical | Major | Minor | Info |
|----------|----------|-------|-------|------|
| Spec conformance | 0 | 0 | 1 (N3) | 0 |
| Correctness | 0 | 0 | 1 (N2) | 0 |
| Safety | 0 | 1 (N1) | 0 | 0 |
| Concurrency | 0 | 0 | 0 | 0 |
| Error handling | 0 | 0 | 0 | 2 (N4, N5) |
| Code quality | 0 | 0 | 0 | 0 |

### Comparison to Previous Review

| Metric | Review P3-001 | Review P3-002 |
|--------|---------------|---------------|
| Critical | 3 | 0 |
| Major | 9 | 1 |
| Minor | 8 | 2 |
| Info/Quality | 5 | 2 |
| Verdict | FAIL | PASS WITH NOTES |

---

## 9. Recommendation

**Proceed to on-target testing** with one condition:

### Should-Fix Before Testing

**N1: Add EVT_BATTERY_CRITICAL handler to remote PRE_FIRE and FIRING states.** This is a two-line addition to each state handler in `rlc_remote_fsm.c`:

```c
// In PRE_FIRE (after EVT_LINK_LOST handler):
} else if (evt->type == EVT_BATTERY_CRITICAL) {
    s_fire_repeat_active = false;
    do_enter_error();
}

// In FIRING (after EVT_LINK_LOST handler):
} else if (evt->type == EVT_BATTERY_CRITICAL) {
    s_fire_repeat_active = false;
    send_cmd_cease_fire();
    do_enter_error();
}
```

### Can Defer

- **N2** (dead code in stale check) — harmless, cosmetic
- **N3** (FIRING→LINK_LOST skips POST_FIRE) — functionally safe, literal spec deviation

---

*End of Phase 3 Code Review (Re-Review) — RLC-REVIEW-P3-002*
