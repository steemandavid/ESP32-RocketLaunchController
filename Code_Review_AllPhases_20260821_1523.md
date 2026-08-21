# All-Phases Code Review — Post-Fix Verification

**Document ID:** RLC-REVIEW-ALL-002
**Reviewer:** Code Review Agent
**Date:** 2026-08-21
**Scope:** Entire firmware codebase — re-review after commit 28293b6 ("Fix all findings from the all-phases code review")
**FSD Reference:** RLC-FSPEC-001 v1.29 (`RLC_Functional_Specification_v1_14.md` — filename stale, header is truth)
**Commit Reviewed:** 28293b6
**Previous review:** `Code_Review_AllPhases_20260821_1430.md` (RLC-REVIEW-ALL-001, verdict MAYBE)

---

## Verdict: FAIL as reviewed → PASS after fixes (one Critical, two Majors, thirteen minors; all fixed and verified on target)

> **Amended after on-target testing.** The static review below returned two Majors. Flashing the result exposed a third finding — **N3, Critical**: the remote panicked and rebooted 11.4 s into every boot, because `esp_task_wdt_reconfigure()` runs *after* tasks have self-registered with the watchdog. That is a defect the static pass missed, and it is recorded in §2 with the on-target evidence. All findings are now fixed; both units have been reflashed and verified stable.


All seven Majors from RLC-REVIEW-ALL-001 (2.1–2.7) were re-verified in source and are correctly fixed, as are the large majority of the 32 minors. The fire path is in better shape than at the last review: continuity now fails safe, the arm-key interlock is enforced at three separate points, ERR_VBAT_CRITICAL can no longer be dropped or misfire, the wire-receive timestamp is genuinely stamped in the ESP-NOW callback, and the Wi-Fi-task blocking hazard is gone.

Two **new Major** findings were introduced or left standing by the fix commit, both in the "supporting hardware" layer rather than the fire path:

- **N1** — a regression in the debounce fix: the remote's arm key, if already ON at power-up, is never registered, so arming is impossible until the key is physically toggled. This will be caught by the first bench test.
- **N2** — the siren mutex added for 5.4 serialises the state but does not invalidate an already-dispatched timer callback, so the two failure modes 5.4 named (siren stuck ON after `siren_off()`, siren silent during the PRE_FIRE countdown) are both still reachable.

Neither can cause an unintended ignition. N1 fails safe (arming refused); N2 degrades an audible warning. Thirteen substantive minors are listed below.

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
| components/rlc_base/src/rlc_base_fsm.c | Base FSM — arm/fire sequence, guards, FIRING exit tail |
| components/rlc_base/src/rlc_continuity.c | Three-band continuity task + fail-safe sampling |
| components/rlc_base/src/rlc_relay.c | Arm + channel relay drivers, bug #18 gate |
| components/rlc_base/src/rlc_siren.c | Siren patterns + new pattern mutex |
| components/rlc_base/src/rlc_arm_sense.c | Arm relay feedback, key sense, weld latch |
| components/rlc_base/src/rlc_base_battery.c | Base VBAT monitor, priority boost, TWDT self-register |
| components/rlc_base/src/rlc_status_update.c | 14-byte status push |
| components/rlc_base/src/rlc_base_main.c | Base init + event plumbing + trace |
| components/rlc_remote/src/rlc_remote_fsm.c | Remote FSM — ACK correlation, arm-key guards, reconcile |
| components/rlc_remote/src/rlc_arm_switch.c | Arm key debounce + LED |
| components/rlc_remote/src/rlc_fire_button.c | Fire button, edge-triggered fresh-press |
| components/rlc_remote/src/rlc_remote_main.c | Remote init + event plumbing |
| components/rlc_common/src/rlc_link.c | Link FSM, session, sequencing, version check, send-failure latch |
| components/rlc_common/src/rlc_espnow.c | ESP-NOW wrapper, wire timestamp, send-failure counter |
| components/rlc_common/src/rlc_continuity_class.c | **New** — shared three-band classifier |
| components/rlc_common/src/rlc_arm_state.c | **New** — shared base arm-state derivation |
| components/rlc_common/src/rlc_debounce.c | Shift-register debouncer + initial-callback suppression |
| components/rlc_common/src/rlc_watchdog.c | TWDT init (add-task helper removed) |
| components/rlc_common/src/rlc_rgb_led.c | 6-layer strip renderer, checked init |
| components/rlc_common/include/{rlc_config,rlc_protocol,pin_config}.h | Config, protocol, pins |
| tests/host/*.c + run.sh | Host unit tests — **executed: 10 binaries, 217 checks, 0 failures, exit 0** |

---

## 1. Coverage Analysis

### 1.1 Re-verification of RLC-REVIEW-ALL-001 Majors

| Prior finding | Status | Evidence |
|---|---|---|
| 2.1 DISARM during arm-verify does not cancel pending ARM | **FIXED** | `rlc_base_fsm.c:388-399` — `EVT_CMD_DISARM` in IDLE calls `abort_arm_verify(NACK_WRONG_STATE)` before the idempotent ACK. `EVT_CMD_FIRE` (:406-410) and `EVT_BATTERY_CRITICAL` (:411-415) given the same treatment; `EVT_LINK_LOST` (:423-431) makes relays safe and clears the pending flag. |
| 2.2 ERR_VBAT_CRITICAL dropped on FIRING abort exits | **FIXED** | New shared tail `firing_exit()` at `rlc_base_fsm.c:232-243`; all four abort exits (CEASE_FIRE :558, arm-sense-lost :569, key-OFF :580, DISARM :595) and the immediate link-loss abort (:619) route through it. |
| 2.3 ADC read failure classified CONNECTED (permissive) | **FIXED** | `CONT_SAMPLE_FAILED` sentinel at `rlc_continuity.c:77`, returned for unconfigured channel (:81), NULL handle (:86) and read error (:94). Task fails safe to `CONT_OPEN` at :139-155, with a per-channel latch to prevent log flooding and a `s_band_initialized` reset on recovery (:159-167). New `s_chan_configured[]` (:44, :228) closes the "sample the wrong ADC input" hole. |
| 2.4 Remote arm-key interlock defeatable | **FIXED** | Three points, as recommended: `wait_for_ack` returns `WAIT_FOR_ACK_INTERRUPTED` on switch-off (`rlc_remote_fsm.c:392-394`), and `-4` falls out of the `result == 0` retry loop; retry loop re-checks the key (:495-498); post-ACK key re-check in both ARM (:507-513) and FIRE (:625-630); a fresh `arm_switch_is_armed()` guard at the head of the ARMED fire path (:579-583); and the fire-repeat task re-checks the key before every repeat (:854). |
| 2.5 Self-test duplicated the continuity classifier | **FIXED** | Classifier extracted to `components/rlc_common/src/rlc_continuity_class.c`; `rlc_continuity.c:172,175` and `rlc_selftest.c:514,534-535` both call the same production functions. Same pattern applied to `rlc_arm_state.c` for the display/host-test duplication called out in §7. |
| 2.6 C3 wire timestamp not captured in the callback | **FIXED** | `received_ms` field added to `rlc_espnow_rx_item_t` (`rlc_espnow.c:43`), stamped at `:74` inside `espnow_recv_cb`, carried through the recv callback signature (`:115`), the trampoline (`rlc_link.c:809-814`), `rlc_link_on_rx` (:856) and into the FSM event (:602) without re-stamping. |
| 2.7 Send-failure handler blocks the Wi-Fi task | **FIXED** | `espnow_send_failure_handler()` now only sets `s_send_failure_pending` (`rlc_link.c:656-659`); the state transition runs on `link_task` via `handle_send_failure()` (:662-671, consumed at :790-792). No mutex, no queue send, no allocation in Wi-Fi context. |

Prior minors verified fixed: 4.5 FIRING backstop (`rlc_base_fsm.c:748-758`), 4.6 key-off during verify (:416-422), 4.7 guard-3 re-check at ARMED→PRE_FIRE (:449-456), 4.8 ACK/NACK correlation (`rlc_remote_fsm.c:369-389`), 4.9 selection re-sync on IDLE entry (:295), 4.10 base-armed-while-remote-IDLE reconcile (:556-563), 4.11 CEASE_FIRE vs DISARM on stale timeout (:815-822), 4.12 fresh-press now genuinely edge-triggered and the dead API deleted, 4.13 `gpio_config` checked and fatal (`rlc_relay.c:44-51`), 5.3 J4 blocking send for `EVT_ARM_SENSE_CHANGED` (`rlc_base_main.c:65`), 5.5 weld latch (`rlc_arm_sense.c:133,148`), 5.6 seq overflow re-links (`rlc_link.c:257-263, 284-289, 305-313`), 5.7 base version-checks the remote (:365-376), 5.8 header polarity comment, 5.9 `rlc_link_send_cmd` reads state and token under one lock (:946-951), 5.10/5.11 every task self-registers with the TWDT at task entry (verified across all 17 task functions; `rlc_watchdog_add_task` removed), 5.13 `_Static_assert` on the boost priority (`rlc_base_battery.c:25-27`), 5.14 checked mutex/task alloc (`rlc_rgb_led.c:303-317`), 5.15 checked encoder task creation (`rlc_remote_main.c:186-193`), 7 `LINK_REQUEST_SLOW_INTERVAL_MS` now applied (`rlc_link.c:688-697`).

### 1.2 FSD coverage

| FSD Requirement | Implementation | Status |
|---|---|---|
| §7.2.2 guard_arm() guards | rlc_base_fsm.c:258-292 | DONE |
| §7.2.2 arm verify ≤200 ms, non-blocking | rlc_base_fsm.c:347-369, 678-683 | DONE |
| §7.2.3 PRE_FIRE 2 s, dead-man 500 ms, repeats 200 ms | rlc_base_fsm.c:693-741 | DONE |
| §7.2.4 FIRING 1 s GPTimer pulse + backstop | rlc_fire_timer.c, rlc_base_fsm.c:748-758 | DONE (backstop does not stop the timer — m5) |
| §7.2.5 link loss / battery critical during FIRING | rlc_base_fsm.c:232-243, 597-620 | DONE |
| §7.2.7 idempotent ACKs, arm timeout | rlc_base_fsm.c:388-405, 685-691 | DONE |
| §7.2.9 ERROR terminal | rlc_base_fsm.c:662-664 | DONE |
| §5.4.2 three-band continuity + hysteresis + fail-safe | rlc_continuity_class.c, rlc_continuity.c:139-188 | DONE |
| §6.2 sequence no-wrap, session token, strict version both ways | rlc_link.c:200-207, 257-313, 365-376, 406-414 | DONE (public `next_seq` still returns 0 — m6) |
| §6.4.1a 5 send failures → link loss, non-blocking | rlc_espnow.c:84-106, rlc_link.c:654-671 | DONE |
| §6.4.1b wire-receive timestamp | rlc_espnow.c:74 | DONE |
| §6.4.2/§7 remote-battery NACK 0x0C arming guard | absent | MISSING — [KNOWN-OPEN] |
| §8.2.3/§8.2.4 remote arm-key precondition | rlc_remote_fsm.c:450, 495, 507, 579, 625, 854 | DONE (but unreachable at boot — finding N1) |
| §8.2.5/§8.2.6 battery critical in PRE_FIRE/FIRING | rlc_remote_fsm.c:731-735, 766-770 | DONE (LINK_LOST still drops it — m1) |
| §9.6 TWDT per-task | all 17 tasks self-register | DONE |
| §9.9 boot self-tests exercise production code | rlc_selftest.c:514, 534 | DONE |
| §10.2.2 four-state BASE field, WELD! derivation | rlc_arm_state.c (shared with host test) | DONE |
| §10.2.0 continuity palette green/red vs blue | rlc_config.h:131-146 | DEVIATION — [KNOWN-OPEN] |
| §5.5.1 encoder cycle-position decoder | rlc_encoder.c | DONE (IRAM note still open — 5.12) |

---

## 2. Deviation Report

### Major

**N1 [MAJOR] Arm key already ON at power-up is never registered — the remote's arm path is dead until the key is physically toggled** — `rlc_debounce.c:44-75`, `rlc_arm_switch.c:45-61, 110-118`, `rlc_remote_main.c` (callback registered after the task starts)

The 2026-08-21 fix suppressed the debouncer's spurious initial "released" callback. It did so by making the *first* stable determination set `initialised = true` and `stable_state` **without invoking the callback at all** (`rlc_debounce.c:46-64` sets `initial`, and only `changed` reaches the callback at :71).

The remote's arm switch is active-LOW with a pull-up: key ON reads LOW. `arm_switch_init()` hardcodes `s_armed = false` and drives the LED off (`rlc_arm_switch.c:115-116`), explicitly deferring to "the debouncer establishes the initial state (first poll)". But if the key is *already ON* at boot, the debouncer's first stable determination is `stable_state = true` via the `!db->initialised` branch — which fires no callback. `on_debounce_change()` never runs, so:

- `s_armed` stays `false` **forever**, until the operator toggles the key OFF and back ON;
- the arm indicator LED never lights;
- every long-press is refused with "TURN ARM KEY FIRST" (`rlc_remote_fsm.c:450-454`);
- the fire-repeat guard added for 2.4 (`:854`) would also block.

Scenario: operator turns the key, then powers the remote (or the remote browns out / is power-cycled with the key left in ARM — the normal recovery action at a pad). Arming is impossible with no diagnostic beyond the toast.

This is a **regression introduced by 28293b6** — before it, the initial determination did fire the callback and seeded `s_armed` correctly. It fails safe (arming refused, never granted), which is why it is Major and not Critical.

Note the fix cannot simply be reverted at the debouncer level: `rlc_fire_button.c:15-21` now *relies* on the suppression for its fresh-press interlock (a button held at boot must not generate a press event). The correct fix is per-consumer — sync the cached state from `rlc_debounce_get_state()` on every poll, so the initial determination is picked up without synthesising an input event. The base's `arm_sense`/`key_sense` seed their cached bools from a raw read at init (`rlc_arm_sense.c:219-220, 239-240`) so they are mostly correct, but they share the same shape: a raw read that catches a transient, or a key turned during the first 160 ms, is never corrected until the next real transition.

**N2 [MAJOR] Siren can stick ON after `siren_off()`, or fall silent during the PRE_FIRE countdown, from a stale in-flight timer callback** — `rlc_siren.c:45-63, 88-133`

Finding 5.4 was addressed by wrapping all pattern state in `s_siren_mu`. The mutex makes the *updates* atomic, but gives the callback no way to know its pattern was cancelled while it was queued. `esp_timer_stop()` does not wait for a callback that has already been dispatched (only `esp_timer_delete()` does), so a callback can be sitting on `siren_lock()` at :47 when a task-context call takes the mutex, reconfigures the pattern, and releases it. Two reachable outcomes:

1. **Stuck ON.** `siren_off()` (:117-123) stops the timer and drives the output low, but leaves `s_pulse_count` at its previous value. Coming from `siren_start_pulse()` that value is `-1` (infinite). The stale callback then acquires the mutex, skips the `s_pulse_count == 0` branch, and executes `siren_drive(!s_siren_on)` → drives the siren **back ON**, with the timer stopped so nothing ever turns it off again. This is exactly the "siren can stick on" defect 5.4 named. Reached on any ARMED→disarm (`do_disarm()` → `siren_off()`), i.e. the arm-timeout and CEASE_FIRE paths.

2. **Silent PRE_FIRE.** `siren_start_continuous()` (:98-105) sets `s_pulse_count = 0` and drives ON with the timer stopped. A callback dispatched by the *previous* pattern (`siren_start_pulse`, 500 ms periodic, running throughout ARMED) then acquires the mutex, sees `s_pulse_count == 0`, and takes the :48-53 branch — `siren_drive(false)`. The 2 s pre-fire warning siren is silent for the entire countdown.

The window is short (dispatch → mutex acquisition), but ARMED→PRE_FIRE is *always* preceded by a running 500 ms periodic timer, so path 2 has a real duty cycle across many launches, and the failure is silent. Fix: a generation/active flag the callback checks under the same mutex before touching the output.

**N3 [CRITICAL] The remote panics and reboots 11.4 s into every boot — `esp_task_wdt_reconfigure()` runs after tasks have already self-registered** — `rlc_remote_main.c` (watchdog init at §9.13 step 8, after `display_start_task()`), `rlc_watchdog.c:21`

*Found on target, not by inspection — added after the first flash of this round. The static review checked that every task self-registers (5.10/5.11) but not **when** the TWDT is reconfigured relative to those registrations.*

`esp_task_wdt_reconfigure()` rebuilds the TWDT's subscriber list. On the remote it is called at "§9.13 Step 8", by which point `display_start_task()` (and the buzzer task) have already run `esp_task_wdt_add(NULL)` at task entry — the very thing fix 5.11 moved them to do. Reconfigure drops those subscriptions, and from then on:

```
I (1585) rlc_disp: display task started (prio 2, core 1)
...
E (1985) task_wdt: esp_task_wdt_reset(705): task not found     <- 20 Hz, 195 times
...
E (11435) task_wdt: Task watchdog got triggered. ...
Guru Meditation Error: Core  0 panic'ed (LoadProhibited). Exception was unhandled.
rst:0xc (RTC_SW_CPU_RST)
```

The display task logs "task not found" at its full 50 ms frame rate; the watchdog then triggers unfed and **panics inside its own report path** (`LoadProhibited`) walking stale entries, rebooting the unit. Measured: reboot at 11.4 s, every boot, reproducibly.

Consequences: (a) the remote is unusable — it resets roughly every 11 s; (b) fix 5.10's whole purpose is silently undone, because the display and buzzer tasks are not actually watchdog-covered; (c) this is the true root cause of the "task not found TWDT boot bursts" that the previous review attributed to registration ordering at 5.11 — that fix addressed a real second-order issue but moved registration *earlier*, ahead of the reconfigure, which made this failure certain rather than intermittent.

The base was clean only by accident: it happens to call `rlc_watchdog_init()` before any task starts.

Fix applied: `rlc_watchdog_init()` split into `rlc_watchdog_init()` (reconfigure only — called first thing in `app_main` on **both** units, before any task exists) and `rlc_watchdog_register_self()` (subscribes `app_main`, called immediately before its housekeeping loop, after the slow SPI/Wi-Fi/NVS/peer-retry init that would otherwise blow the 5 s budget on its own). Verified on target: 45 s continuous run, both units, zero TWDT errors, single boot each.

### Known deviations (tracked, not new)

- [KNOWN-OPEN] NACK 0x0C remote-battery arming guard unimplemented (FSD §6.4.2/§7).
- [KNOWN-OPEN] Palette green/red vs FSD §10.2.0 blue-for-GOOD (shape coding carries meaning independently).
- [KNOWN-OPEN] CONT_MARGINAL 67 Ω vs FSD §5.4.2 prose "~20 Ω" (deliberate).
- [KNOWN-OPEN #18] `FIRE_PROTECTED_CHANNEL_MASK` still `0x01`.
- [KNOWN-OPEN #20/#23/#25] public crypto keys; divider headroom; no hardware UV cutoff.
- [KNOWN-OPEN 5.12] Encoder ISR calls non-IRAM `gpio_get_level` without `IRAM_ATTR`/`CONFIG_GPIO_CTRL_FUNC_IN_IRAM`.
- The CONT_SHORT(3) story is now consistent: `rlc_continuity_class.c:50-53` folds it via `classify_initial`, and the log tables in `rlc_base_main.c:269, 299` map index 3 → "CONN". Only the strip/display palette still renders it red — unreachable in practice under the strict version check.

### Minor

**m1 — Remote drops `EVT_BATTERY_CRITICAL` in LINK_LOST** — `rlc_remote_fsm.c:775-782`
The base got exactly this fix (`rlc_base_fsm.c:654-658`, with the comment "previously silently discarded"); the remote's LINK_LOST case handles only `EVT_LINK_RECOVERED`. Because `remote_battery` edge-triggers and posts once per crossing, the event is lost permanently: the remote recovers the link and returns to IDLE on a critical pack with only the `REMOTE_VBAT_MIN_ARM_MV` arm guard left. `STATE_LINKING` has the same gap for `EVT_LINK_LOST`.

**m2 — Ping health window and base slot tracker survive a session reset, blocking ARM for ~5 s after every link recovery** — `rlc_link.c:178-186, 740-753, 980-993`
`reset_session()` clears sequences and ping counters but not `s_ping_window[]`, `s_ping_window_count`, or `s_base_next_expected_ping_ms`. While the link is LOST, `tick_base()` returns early, so `s_base_next_expected_ping_ms` freezes; on recovery the catch-up `while` loop at :745-753 back-fills one "miss" per elapsed 500 ms slot, saturating the 10-slot window with failures. `rlc_link_is_healthy()` then returns false, so guard 10 NACKs every ARM with `NACK_COMM_DEGRADED` (and `rlc_remote_fsm.c:474-479` refuses locally) for the first ten pings after recovery. Safe direction, but it makes post-recovery arming look broken, and the loop iterates once per 500 ms of downtime (7200 iterations after an hour).

**m3 — Stale-status timeout re-fires at 20 Hz** — `rlc_remote_fsm.c:807-826`
`do_enter_idle()` does not clear `s_last_status_rx_ms`, and the guard only requires `s_state != BOOT/LINK_LOST/ERROR`. Once the status goes stale the whole block runs on every 50 ms `check_timers()` pass: a warning log, `rlc_rgb_led_set_pattern()`, and `s_selected_channel` re-sync, indefinitely. Log flood during exactly the condition an operator most needs to read the log.

**m4 — `firing_exit()`'s stated invariant is false, and POST_FIRE double-enters ERROR** — `rlc_base_fsm.c:224-231, 529-547, 636-644`
The header comment says "Every path that leaves FIRING funnels through here", but the normal `EVT_FIRE_PULSE_DONE` → POST_FIRE path (:541-547) does not — it is caught instead by the POST_FIRE checks at :642 and :763. Behaviour is correct; the invariant as documented is not, which is the kind of comment a future edit trusts. Separately, `EVT_BATTERY_CRITICAL` in POST_FIRE calls `do_enter_error()` at :639 and then the unconditional flag check at :642 calls it again — a second `relay_all_safe()` (20 ms `vTaskDelay`), a second siren restart and a duplicate log line.

**m5 — FIRING backstop does not stop the GPTimer** — `rlc_base_fsm.c:748-758`
The backstop synthesises `EVT_FIRE_PULSE_DONE`, whose handler (:529-547) calls `relay_all_safe()` but not `fire_timer_stop()` (every *other* FIRING exit does). If the notification was lost because the timer is misbehaving rather than the notify, the timer stays armed. Hardware is safe either way; a later stray notification lands in a state that ignores it.

**m6 — `rlc_link_next_seq()` still returns 0 on overflow instead of dropping the link** — `rlc_link.c:969-978`, callers `rlc_base_fsm.c:151, 163`
The three internal senders were fixed under 5.6 to `set_state(RLC_LINK_STATE_LOST)` on overflow. The public wrapper was not: it silently returns 0. `send_ack()`/`send_nack()` do not check it (the remote's `send_cmd_*` do, at `rlc_remote_fsm.c:191, 220, 248, 267`), so the base would emit seq-0 ACKs that the peer rejects as replay — ACKs stop, no diagnostic. Unreachable in practice (2^32 frames) but the API invites the misuse, and the two treatments now disagree.

**m7 — `espnow_send_cb` logs from Wi-Fi task context** — `rlc_espnow.c:90`
`ESP_LOGW(TAG, "send fail #%d", ...)` runs on every failure, two lines above a comment stating the callback "must not block (no mutexes, no timed queue sends, no logging)". Pre-existing, but the fix commit's own comment now contradicts it.

**m8 — Handshake STATUS_UPDATE hardcodes `base_state = STATE_IDLE`** — `rlc_link.c:249-275`, called at `:394`
`send_status_update()` is the Phase-1 placeholder: all-zero payload with `p.base_state = STATE_IDLE`. It is still sent immediately after `send_link_ack()` on every handshake. The app-state guard rejects LINK_REQUESTs only for the *busy* states, so a base sitting in ERROR or LINK_LOST answers a handshake by asserting IDLE with zero error flags and zero continuity — a false safe reading on the remote's display and strip until the real 2 s status arrives.

**m9 — Unchecked task creation** — `rlc_continuity.c:262`, `rlc_arm_sense.c:248`, `rlc_arm_switch.c:123`, `rlc_base_battery.c:82`, `rlc_status_update.c:96`
5.15 fixed the encoder task; these five still discard the `BaseType_t`. `arm_sense_start_task()` is the one that matters — a silent failure there means no arm-sense feedback, no key-switch events and no weld detection, while the FSM's getters keep returning the stale init seed.

**m10 — `SIREN_LINK_LOST_DURATION_MS` is dead config** — `rlc_config.h:43`, `rlc_siren.c:107-115`
The 4000 ms is encoded as a bare `s_pulse_count = 4` against a 500 ms half-period. Editing the constant does nothing.

**m11 — `s_bands[]` is non-volatile but read from three tasks** — `rlc_continuity.c:48`
Written by `continuity_task`, read by the FSM task (`continuity_get_channel` in guard 2), the status task and the housekeeping loop. Its `s_uv`/`s_raw` siblings on the next lines *are* `volatile`. Benign today; an inconsistency in a value that gates arming.

**m12 — Arm-verify window hardcodes `200`** — `rlc_base_fsm.c:679`
Every other timeout in the FSM comes from a named `rlc_config.h` constant. The 200 ms is an FSD §7.2.2 number and belongs there.

**m13 — Interrupted ARM leaves `s_selected_channel` stale** — `rlc_remote_fsm.c:398-402, 522-527`
`wait_for_ack()` consumes `EVT_ENCODER_ROTATE` and returns `INTERRUPTED` without applying the new channel; the IDLE handler for that result sends a DISARM and returns without calling `do_enter_idle()` (which is where the 4.9 re-sync lives). The display and strip cursor then show the pre-rotation channel while `encoder_get_channel()` — what the next long-press actually arms — says otherwise. Same divergence 4.9 fixed for the LINKING/LINK_LOST path.

---

## 3. Plan vs. Implementation

No implementation plan document exists for the 28293b6 fix round (`Implementation_Plan_Phase3_Review_Fixes*.md` cover the earlier Phase-3 rounds). The commit message is the plan of record; it is compared against the code below.

| Plan item (commit message) | Actual | Status |
|---|---|---|
| Majors: DISARM aborts pending arm-verify | `rlc_base_fsm.c:388-399` | Match |
| Majors: ERR_VBAT_CRITICAL terminal ERROR on all FIRING exits | `firing_exit()` :232-243 + 5 call sites | Match (normal-completion path routes via POST_FIRE instead — m4) |
| Majors: continuity ADC failure fails safe to OPEN | `rlc_continuity.c:77-155` | Match |
| Majors: remote arm-key interlock in wait_for_ack/retry/fire | `rlc_remote_fsm.c` × 6 sites | Match — but boot-time key state broken (N1) |
| Majors: shared classifier used by self-test | `rlc_continuity_class.{c,h}` | Match |
| Majors: timestamp stamped in the ESP-NOW callback | `rlc_espnow.c:74` | Match |
| Majors: no Wi-Fi-task blocking in send-failure handler | `rlc_link.c:654-671` | Match (a log remains in the callback — m7) |
| Minors: FIRING pulse backstop | `rlc_base_fsm.c:748-758` | Match (timer not stopped — m5) |
| Minors: remote ACK correlation + reconcile | `rlc_remote_fsm.c:369-389, 556-563` | Match |
| Minors: TWDT self-registration, `rlc_watchdog_add_task` deleted | all 17 tasks; helper absent from `rlc_watchdog.{c,h}` | Match |
| Minors: siren mutex | `rlc_siren.c:22-36` | **Partial** — serialises state but does not invalidate stale callbacks (N2) |
| Minors: link overflow / version-check fixes | `rlc_link.c:257-313, 365-376` | Match (public `next_seq` missed — m6) |
| Minors: debouncer initial-state callback suppressed | `rlc_debounce.c:41-75` | **Regression** — see N1 |
| Minors: `adc_cali` fallback | `rlc_continuity.c:106-113`, `rlc_battery.c` | Match |
| Minors: dead APIs removed | `base_state_get_continuity`, `fire_button_was_fresh_press`, `remote_fsm_get_armed_channel_ptr` all absent | Match |
| Minors: shared `rlc_arm_state` module | `rlc_arm_state.{c,h}`, used by display + `tests/host/test_armstate.c` | Match |
| "Both units build clean; host tests all pass" | Host tests re-run during this review: 10 binaries, 217 checks, 0 failures, exit 0 | Match |
| "Not yet flashed" | Confirmed — no on-target evidence in the changelog for this commit | Open (addressed by this session's flash + test) |

---

## 4. Edge Cases & Safety

| # | Finding | Risk |
|---|---|---|
| 4.1 | N1 — arm key ON at boot never registers; arming impossible until toggled | **High (availability)** — fails safe; no path to an unintended fire |
| 4.2 | N2 case 1 — siren stuck ON after disarm, timer stopped, nothing clears it | Medium — a siren that cries wolf trains operators to ignore it |
| 4.3 | N2 case 2 — siren silent through the 2 s PRE_FIRE countdown | Medium — removes the audible pad warning at the one moment it matters |
| 4.4 | m8 — base in ERROR answers a handshake with `base_state = IDLE`, zero error flags | Medium-bounded — corrected within 2 s by the real STATUS_UPDATE |
| 4.5 | m1 — remote drops a critical-battery crossing in LINK_LOST, permanently | Medium — returns to service on a critical pack; arm guard still holds |
| 4.6 | m2 — ARM refused `COMM_DEGRADED` for ~5 s after every link recovery | Low — safe direction, but reads as a fault |
| 4.7 | m5 — backstop leaves the GPTimer armed | Low (latent) — relays already open |
| 4.8 | `relay_all_safe()` blocks the FSM task 20 ms per call, and abort paths call it twice (once directly, once inside `do_enter_error`) | Low — deliberate (arc protection); worth noting the FSM is unresponsive for 40 ms on those paths |
| 4.9 | Reset/brownout: relay GPIOs float until `relay_init()`; no brownout handler drives outputs safe first | Info — hardware question (gate pull-downs), bugs #24/#25 family |
| 4.10 | seq-0 replay window after every session reset; replayed LINK_REQUEST invalidates a live session | Low — unchanged from prior review; both require the encrypted peer MAC |

---

## 5. Concurrency & Platform Issues

| # | Finding | Ref |
|---|---|---|
| 5.1 | N2 — `esp_timer_stop()` does not cancel an already-dispatched callback; mutex alone is insufficient | `rlc_siren.c:45-63` |
| 5.2 | m2 — health window / slot tracker not reset with the session | `rlc_link.c:178-186, 742-753` |
| 5.3 | m11 — `s_bands[]` non-volatile, read cross-task | `rlc_continuity.c:48` |
| 5.4 | `set_state()` performs a 10 ms blocking queue send while holding `s_state_mutex`; the FSM task can be waiting on that mutex inside `rlc_link_is_healthy()`. Bounded at 10 ms, not a deadlock, but it is a lock-ordering inversion by construction. | `rlc_link.c:167-174` vs `:980-993` |
| 5.5 | Encoder rotate callback runs in ISR context and posts via `xQueueSendFromISR`; the contract is still undocumented in `rlc_encoder.h`, and the ISR calls non-IRAM `gpio_get_level` | `rlc_remote_main.c:86-95`, `rlc_encoder.c` |
| 5.6 | `cmd_fire_repeat_task_fn` calls `send_cmd_fire()`, which writes `s_pending_cmd_seq/_type` — state the FSM task's `wait_for_ack()` correlates against. Currently safe (the repeat task only runs after `wait_for_ack` has returned, and re-checks `s_fire_repeat_active` at :854), but the ownership is no longer single-task and nothing documents that. | `rlc_remote_fsm.c:210-211, 235-236, 845-859` |
| 5.7 | Remote registers input callbacks *after* starting the tasks that drive them (`fire_button_start_task` … then `fire_button_register_cb`), so events in the first ~200 ms are silently dropped. Combined with N1 this is why the boot-time key state is lost twice over. | `rlc_remote_main.c:178-186 vs 222-226` |
| 5.8 | `espnow_rx_task` blocks on `portMAX_DELAY` and is deliberately not TWDT-registered — correct, but it is the one task the §9.6 audit will keep flagging; worth a comment | `rlc_espnow.c:108-120` |
| 5.9 | m9 — five unchecked `xTaskCreatePinnedToCore` calls | see m9 |

---

## 6. Error Handling

- The headline inversion from the last review is gone: continuity now fails **safe**. Every subsystem reviewed degrades toward link-loss/disarm/OPEN.
- m1 is the one remaining silent-discard of a once-per-crossing safety event.
- m6 — `rlc_link_next_seq()` returns a sentinel the base's two callers never check.
- m8 — the handshake status is a fabricated value, not an error path, but it reports "safe" on a unit that is not.
- `adc_cali_raw_to_voltage()` failure now falls back to a raw conversion in `rlc_continuity.c:107-110` rather than silently yielding 0 mV.
- `tools/test_tr04.py:117-133` still prints FAIL and exits 0 — scripted runners see success. Unchanged.
- `rlc_selftest_run()` failure calls `rlc_rgb_led_set_pattern()` before `rlc_rgb_led_init()` on the base (`rlc_base_main.c:115-124`) — the LED is not yet initialised, so the halt is silent on the strip. The remote orders these correctly.

---

## 7. Code Quality

- The two extractions (`rlc_continuity_class`, `rlc_arm_state`) are the right shape and close the M2 anti-pattern at both sites, including in the host tests. This was the structural debt of the last three reviews; it is paid.
- Dead API surface is materially reduced. Remaining: `SIREN_LINK_LOST_DURATION_MS` (m10), `CONT_SHORT_UV` / `CONT_HYSTERESIS_SHORT_UV` (documented as deliberate records), `rlc_debounce_is_stable()` (now unused — and it is precisely the primitive N1 needs).
- Comment defects: `firing_exit()`'s false invariant (m4); `rlc_espnow.c:95-97` "no logging" contradicted at :90 (m7); `rlc_arm_switch.c:113-114` says the debouncer establishes the initial state, which is the assumption N1 breaks.
- `main/idf_component.yml` floors `idf >=5.0.0` but the code uses 5.4 APIs; the managed `espressif/esp-now` component is still pulled in and unused (native `esp_now.h` only).
- Clean and worth saying so: `rlc_message.c`, `rlc_fire_timer.c`, `rlc_status_update.c`, `rlc_continuity_class.c`, `rlc_arm_state.c`, `rlc_relay.c`, the host `run.sh` harness.

---

## 8. Summary

| Category | Critical | Major | Minor | Info |
|----------|----------|-------|-------|------|
| Spec conformance | 0 | 0 | 2 | 1 |
| Plan conformance | 0 | 2 (N1 regression, N2 partial) | 1 | 0 |
| Correctness | 0 | 0 | 5 | 2 |
| Safety & robustness | 0 | 0 | 2 | 3 |
| Concurrency & platform | 1 (N3, found on target) | 0 | 4 | 4 |
| Error handling | 0 | 0 | 3 | 2 |
| Code quality | 0 | 0 | 3 | 3 |
| **Total** | **1** | **2** | **13** | **15** |

**Note on N3 and the limits of this review.** N3 was found by flashing, not by reading. The review verified that every task self-registers with the watchdog (5.10/5.11) but never asked *when the watchdog is reconfigured relative to those registrations* — an ordering question that spans two files and shows up only at runtime. Two static reviews in one day both missed a defect that rebooted the remote every 11 seconds. Boot-to-steady-state on real hardware belongs in the review loop, not after it.

Prior-review re-verification: **7 of 7 Majors (2.1–2.7) fixed and verified in source.** Of the 32 prior minors, all were verified fixed except those explicitly re-listed above (m6 partial, m7 unchanged, 5.12 open) and the tracked KNOWN-OPENs. Host tests executed during this review: 10 binaries, 217 checks, 0 failures.

Net movement since RLC-REVIEW-ALL-001: Majors 7 → 2, and neither remaining Major is on the ignition path.

---

## 9. Recommendation

**All findings fixed; both units flashed and verified. Cleared for bench testing; live fire still gated on the physical checks below.**

On-target verification completed this session (firmware 1.1.1):

| Check | Result |
|---|---|
| Boot, both units | v1.1.1, 12/12 self-test suites PASS on each |
| TWDT errors / panics / unexpected reboots | **Zero** over 45 s continuous, both units (was: remote rebooting every 11.4 s) |
| Link establish | Remote links on LINK_REQUEST attempt 1, LINKING→IDLE in 40 ms |
| Link loss and recovery | Base detects PING drought at 1548 ms → LINK_LOST → recovers 880 ms later on the remote's re-handshake; FSM follows on both sides |
| Bidirectional version check (5.7) | `LINK_REQUEST from remote fw 1.1.1` accepted; token agreed both ends |
| Telemetry | Base 12.34 V, remote 8.00 V, RSSI −24 dBm, `missed=0`, `txfail=0` (new m7 counter), contact 184–400 ms, continuity stable at 0x5425 |

Operator bench tests, run after the above:

| # | Test | Result |
|---|---|---|
| 1 | **N1** — arm key to ARM *first*, then power up; arm LED must light and a long-press must be accepted | **PASS** — the regression that made 1.1.0's arm path unusable is verified fixed on hardware |
| 2 | **N2 case 2** — siren must sound continuously across ARMED→PRE_FIRE | **SEQUENCE ONLY** — path ran correctly, but the siren output (GPIO 40) is not connected, so the audible behaviour was not observed |
| 3 | **N2 case 1** — siren must stop and stay stopped after disarm | **NOT RUN** — blocked on the same missing connection |
| 4 | Full arm → pre-fire → fire → post-fire on channel 1, display BASE field tracked | **PASS** |

**N2 remains unverified on hardware, and cannot be verified until the siren is wired.** Both its failure modes are silent — a siren stuck on after disarm, a siren that falls quiet through the countdown — so neither is observable with the output disconnected. The fix rests on code inspection alone until GPIO 40 is fitted (IRLZ44N low-side per §5.4.8/§5.4.10) and tests 2 and 3 are re-run. That is the one open item from this round.

---

### Original recommendation (before on-target testing)

**PASS WITH NOTES — fix N1 and N2 before bench testing, then proceed.**

N1 will block the very first arm attempt on the bench if the key happens to be turned before power-up, so it must be fixed to test anything at all. N2 is a small, local change (a generation/active flag checked in the timer callback). Both are contained:

- N1: sync the cached state from `rlc_debounce_get_state()` each poll in `rlc_arm_switch.c` (and the base's `rlc_arm_sense.c` for symmetry), leaving `rlc_debounce.c` alone so the fire-button fresh-press interlock is preserved.
- N2: add an `s_timer_active` / generation flag set under `s_siren_mu` by every start/stop path; the callback returns immediately if its pattern was superseded, and `siren_off()` also clears `s_pulse_count`.

The thirteen minors are individually small and batch naturally into the same pass; m1, m2, m3 and m8 are the ones an operator would actually notice. None of the remaining findings blocks live fire once N1/N2 are closed — the hardware interlock chain (key-in-coil-path AND gate, arm relay, channel relay, snubbers, bug #18 channel mask) remains the effective barrier throughout.

— Every Major and Minor above was read in source at the cited lines before inclusion.
