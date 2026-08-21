# All-Phases Code Review — Full Firmware (Phases 1–4)

**Document ID:** RLC-REVIEW-ALL-001
**Reviewer:** Code Review Agent (3 parallel reviewers + lead verification)
**Date:** 2026-08-21
**Scope:** Entire firmware codebase — rlc_base, rlc_remote, rlc_common, main, host tests, tools (scope explicitly requested as "all code", not a single phase)
**FSD Reference:** RLC-FSPEC-001 v1.29 (`RLC_Functional_Specification_v1_14.md` — filename stale, header is truth)
**Commit Reviewed:** cd4ddf0

---

## Verdict: MAYBE

The fire-path architecture is fundamentally sound: no path was found where software spontaneously closes a relay that should not be closed, and no corrupted, injected, or mis-sequenced frame can reach the FSM without the encrypted peer MAC, live session token, strictly-greater sequence, and keyed CRC. All previously flagged Phase 1–3 safety fixes (C1–C3, M1–M9, J1–J7, R1–R2, N1, m1–m2, M5, M8–M9) were re-verified present and correct. However, **seven Major findings** were confirmed — the two most serious being operator-model divergences in the arm/fire path (base becomes ARMED after confirming DISARM; remote can command FIRE with the arm key OFF) and a continuity failure mode that fails permissive instead of conservative. The four safety-relevant Majors (1, 2, 3, 4 below) should be fixed before the next live-fire test; the review is a conditional pass on everything else.

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
| components/rlc_base/src/rlc_base_fsm.c | Base FSM — arm/fire sequence, guards |
| components/rlc_base/src/rlc_continuity.c | Three-band continuity classifier + task |
| components/rlc_base/src/rlc_fire_timer.c | GPTimer 1 s fire pulse |
| components/rlc_base/src/rlc_relay.c | Arm + channel relay drivers |
| components/rlc_base/src/rlc_siren.c | Siren patterns |
| components/rlc_base/src/rlc_arm_sense.c | Arm relay feedback + weld detect |
| components/rlc_base/src/rlc_base_battery.c | Base VBAT monitor |
| components/rlc_base/src/rlc_base_state.c | Busy-state helper |
| components/rlc_base/src/rlc_status_update.c | 14-byte status push |
| components/rlc_base/src/rlc_base_main.c | Base init + event plumbing |
| components/rlc_remote/src/rlc_remote_fsm.c | Remote FSM — ARM/FIRE commands |
| components/rlc_remote/src/rlc_display.c | ILI9488 display rendering |
| components/rlc_remote/src/rlc_encoder.c | Cycle-position quadrature decoder |
| components/rlc_remote/src/rlc_arm_switch.c | Arm key debounce + LED |
| components/rlc_remote/src/rlc_fire_button.c | Fire button + fresh-press logic |
| components/rlc_remote/src/rlc_buzzer.c | Buzzer patterns |
| components/rlc_remote/src/rlc_remote_battery.c | Remote VBAT monitor |
| components/rlc_remote/src/rlc_remote_state.c | State cache wrapper |
| components/rlc_remote/src/rlc_remote_main.c | Remote init + event plumbing |
| components/rlc_common/src/rlc_link.c | Link FSM, session, sequencing |
| components/rlc_common/src/rlc_message.c | Framing + keyed CRC32-C |
| components/rlc_common/src/rlc_espnow.c | ESP-NOW wrapper + queues |
| components/rlc_common/src/rlc_selftest.c | Boot self-tests |
| components/rlc_common/src/rlc_battery.c | Shared ADC burst sampling |
| components/rlc_common/src/rlc_rgb_led.c | 6-layer strip renderer |
| components/rlc_common/src/rlc_watchdog.c, rlc_debounce.c | TWDT helper, debouncer |
| components/*/include/* (incl. rlc_config.h, rlc_protocol.h, pin_config.h) | Config, protocol, pins |
| main/main.c + build files | Unit dispatch |
| tests/host/*.c + run.sh | Host unit tests (executed: 217 checks, 10 binaries, PASS) |
| tools/vbat_fit.py, tools/test_tr04.py | Calibration + on-target test tooling |

All Major findings below were independently re-verified against the cited code lines by the lead reviewer.

---

## 1. Coverage Analysis

| FSD Requirement | Implementation | Status |
|---|---|---|
| §7.2.2 guard_arm() 10 guards | rlc_base_fsm.c:243–276 | DONE (guard 3 key re-check at ARMED→PRE_FIRE missing — finding 3.11) |
| §7.2.2 arm sense verify ≤200 ms, non-blocking (M1) | rlc_base_fsm.c:321–343 | DONE (DISARM-during-verify hole — finding 2.1) |
| §7.2.3 PRE_FIRE 2 s, dead-man 500 ms, repeats 200 ms | rlc_base_fsm.c:413–457 | DONE |
| §7.2.4 FIRING 1 s GPTimer pulse, ISR notify-only | rlc_fire_timer.c | DONE (no max-duration FSM backstop — finding 3.7) |
| §7.2.5 link loss in FIRING (C1, COMPLETE_PULSE_ON_LINK_LOSS) | rlc_base_fsm.c:566–583 | DONE |
| §7.2.5 battery critical during FIRING → ERROR after pulse | rlc_base_fsm.c:558–563, 604–607 | PARTIAL — flag dropped on abort exits, misfires later (finding 2.2) |
| §7.2.7 idempotent ACKs (J5) | rlc_base_fsm.c:591–598 | DONE |
| §7.2.9 ERROR terminal | rlc_base_fsm.c:620–623 | DONE |
| §5.4.2 three-band continuity, 432000 µV OPEN, hysteresis | rlc_continuity.c:64–107, rlc_config.h:221–234 | DONE (ADC-failure direction permissive — finding 2.3) |
| §6.2 sequence no-wrap, session token, strict version | rlc_link.c | PARTIAL — STATUS_UPDATE seq path wraps (5.6 below); base never version-checks remote (5.7) |
| §6.4.1a 5 send failures → link loss | rlc_espnow.c:78–98 | DONE (WiFi-task blocking — finding 2.7) |
| §6.4.1b wire-receive timestamp (C3) | rlc_link.c:803 | PARTIAL — stamped in worker task behind two queues; comments false (finding 2.6) |
| §6.4.2/§7 remote-battery NACK 0x0C guard | absent | MISSING — [KNOWN-OPEN] |
| §8.2.3/§8.2.4 remote arm-key precondition | rlc_remote_fsm.c:440–470, 535–570 | PARTIAL — checked at ARM only, not at FIRE or on retry (finding 2.4) |
| §8.2.5/§8.2.6 N1 battery critical PRE_FIRE/FIRING | rlc_remote_fsm.c:675–680, 710–715 | DONE |
| §9.6 TWDT per-task | rlc_watchdog.c + call sites | PARTIAL — display/buzzer tasks unregistered (5.9→3.14); reset-before-register race (3.15) |
| §9.9 boot self-tests (offsetof, CRC vector) | rlc_selftest.c:101–117 | DONE (hysteresis vectors test a copy — finding 2.5) |
| §10.2.2 four-state BASE field, WELD! derivation | rlc_display.c:512–553 | DONE |
| §10.2.5 LINK LOST ms_since_contact | rlc_display.c:879 | DONE |
| §10.2.0 continuity palette green/red vs blue | rlc_config.h:130–143 | DEVIATION — [KNOWN-OPEN] |
| §5.5.1 encoder cycle-position decoder | rlc_encoder.c | DONE (IRAM notes — 5.10) |
| Phase 1/2/3 review fixes (C1–C3, M1–M9, J/R/N/m series) | various | DONE — all re-verified present, cited in section 3 |

---

## 2. Deviation Report

### Major

**2.1 [MAJOR] DISARM during the arm-verify window does not cancel the pending ARM** — `rlc_base_fsm.c:362–365`
In STATE_IDLE, `EVT_CMD_DISARM` only sends an idempotent ACK. If a CMD_ARM is inside its 200 ms verify window (`s_arm_verify_pending == true`, arm relay already energized at :319), a DISARM arriving in that window is ACKed as "already safe" while verification continues; when sense goes HIGH (:347–359) the FSM completes IDLE→ARMED — siren pulsing, arm relay energized, FIRE acceptable — after the remote displayed "disarmed". Scenario: operator long-presses ARM, then rotates the encoder (which sends CMD_DISARM on channel change per FSD §10) within the 200 ms window. The sibling `EVT_CMD_CEASE_FIRE` handler (:366–371) correctly calls `abort_arm_verify()`; DISARM must do the same. Violates FSD §7.2.7's "already safe" premise.

**2.2 [MAJOR] ERR_VBAT_CRITICAL latched during FIRING is dropped on abort exits, then detonates as a spurious terminal ERROR** — `rlc_base_fsm.c:558–563, 604–607`
`EVT_BATTERY_CRITICAL` in FIRING only ORs the flag and waits for pulse completion. But CEASE_FIRE (:502), arm-sense-lost (:515), key-OFF (:528), and DISARM (:545) all exit to IDLE, and the link-lost-pending path (:487–493) exits to LINK_LOST — none clears or acts on `s_error_flags`, and IDLE/LINK_LOST never check it. Consequences: (a) FSD §7.2.5 "complete pulse, then ERROR" is not honored on operator-abort paths — the unit returns to service on a critical battery; (b) the stale flag fires at the *next* POST_FIRE entry (:605–607 unconditional `do_enter_error(0)`) — a spurious power-cycle-terminal ERROR mid-subsequent-launch. Fix in the shared FIRING exit sequence.

**2.3 [MAJOR] ADC read failure classifies the channel as CONT_CONNECTED — failure direction is permissive** — `rlc_continuity.c:111–127, 161–171`
`sample_channel()` returns 0 both for NULL handle and for any `adc_oneshot_read` failure (:122–126); `uv = 0` classifies as CONNECTED (0 < 66000), the only arming-permitting band. A persistent read failure (driver timeout, ADC lock contention with the priority-boosted battery task) reports every channel as ready to arm — an actually-open igniter reads CONNECTED and arming/fire is permitted (guard 2 blocks only OPEN). Related: a channel whose `config_channel` failed in `continuity_init()` is silently sampled from the wrong ADC input. Fix: sentinel on failure, hold previous band or classify OPEN (fail-safe).

**2.4 [MAJOR] Remote arm-key interlock can be defeated — FIRE commandable with the key OFF** — `rlc_remote_fsm.c:383–385, 481–487, 535–570`
Three compounding gaps: (a) `wait_for_ack()` consumes `EVT_ARM_SWITCH_CHANGED(off)` and returns 0 — the switch-off event is gone from the queue; (b) the IDLE retry loop treats 0 as "retry" and re-sends CMD_ARM *without re-checking* `arm_switch_is_armed()`; (c) the ARMED fire path checks multi-arm, base mask, freshness, and link health — but never the arm key. Scenario: key flipped off during the 0.5–1.5 s ACK/retry window, ACK arrives → remote ARMED with key off; later fire press passes every guard and transmits CMD_FIRE. Violates FSD §8.2.3/§8.2.4 (arm switch ON is a stated fire precondition). Fix: abort retry on interruption, treat switch-off as terminal, add `arm_switch_is_armed()` to the ARMED fire guards.

**2.5 [MAJOR] Boot self-test still duplicates the production continuity classifier (Phase-2 M2 violation)** — `rlc_selftest.c:485–490, 546–575`
`selftest_classify_uv()` is a verbatim copy of `classify_initial()` in `rlc_continuity.c:64–107` (the file's own comment admits "same logic"). A future edit to the production hysteresis logic passes at boot because the self-test runs the old copy — exactly the divergence M2 was raised against. The three-band vectors are good (including the deprecated-SHORT(3) fold case at :627–631) but only exercise the copy. Fix: share the classifier (as tests/host already do by including real sources).

**2.6 [MAJOR] C3 wire-receive timestamp is NOT captured in the ESP-NOW callback; three comments claim it is** — `rlc_espnow.c:38–43, 55–76`; `rlc_link.c:53, 563–564, 803`
`rlc_espnow_rx_item_t` has no timestamp field; the timestamp is taken in `rlc_link_on_rx()` (`rlc_link.c:803`) in the worker task, after the frame transited one 16-deep queue, and the frame then transits a second queue before `link_task`. Under bursts or WiFi-task congestion, `received_ms` lags true wire time by up to a queue drain; it feeds the 500 ms dead-man logic (FIRE_AUTHORIZATION_TIMEOUT_MS) — the exact hazard C3 was fixing. Practical latency is normally small, but the spec and the code's own documentation say the callback must stamp it. Fix: add `received_ms` to the rx item, stamp in `espnow_recv_cb`.

**2.7 [MAJOR] ESP-NOW send-failure handler blocks the WiFi task (portMAX_DELAY mutex + queue send + logging from callback context)** — `rlc_espnow.c:86–91`; `rlc_link.c:119, 626–634`
`espnow_send_cb` (WiFi-task context) invokes `espnow_send_failure_handler()` directly, which takes the state mutex with `portMAX_DELAY` (`rlc_link.c:119`) and can block 10 ms in a queue send plus log — while `link_task` holds that same mutex across `process_frame()` including its own `esp_now_send()`. ABBA shape: link_task holds the state mutex and takes WiFi-internal locks; the WiFi task holds WiFi-internal locks and waits on the state mutex — at exactly the moment the link is already failing. Fix: defer the state transition to `link_task` via flag/notification; never take an unbounded lock in a WiFi-task callback.

### Known deviations (tracked, not new)

- [KNOWN-OPEN] NACK 0x0C remote-battery arming guard unimplemented (FSD §6.4.2/§7).
- [KNOWN-OPEN] Palette green/red vs FSD §10.2.0 blue-for-GOOD.
- [KNOWN-OPEN] CONT_MARGINAL 67 Ω vs FSD §5.4.2 prose "~20 Ω" (deliberate).
- [KNOWN-OPEN #18] FIRE_PROTECTED_CHANNEL_MASK still 0x01 pending confirmation (widening to 0x3F now that snubbers + CH1–6 clamps are fitted).
- [KNOWN-OPEN #20/#23/#25] public crypto keys; divider headroom; no HW UV cutoff.
- Note: CONT_SHORT(3) does not render as CONNECTED (strip red at `rlc_rgb_led.c:115–123`, display red, host test T-L02 pins it) although the FSD/changelog say "value 3 folds to CONNECTED on decode". Unreachable in practice (strict version check), but spec, config comment, and test currently disagree — pick one story. (See finding 3.13.)

---

## 3. Plan vs. Implementation

| Plan Item | Planned | Actual | Status |
|---|---|---|---|
| Phase 1 fixes (commit 40ab607) | C1–C6, R1–R10 | All verified present (CRC scope incl. header, queue-context fix, TWDT, retry counter, seq-overflow re-link, 8-pixel strip) | Match |
| Phase 2 fixes (commit 7e55b99) | M1–M3, m1–m4 | M1 arm verify, m1 battery TWDT, m2 update_sequence verified; **M2 (self-test must not duplicate classifier) NOT honored** — see finding 2.5 | Partial |
| Phase 3 fixes (commits per Implementation_Plan_Phase3_Review_Fixes*.md) | C1–C3, M1–M9, m1–m8, J1–J7, R1–R2, N1 | All verified present at cited lines, including FIRING link-loss, dead-man chain, J1 fault consumer, R1 sentinel, R2 popcount, N1 remote battery-critical, J4 blocking safety sends, J5 idempotent ACKs, J7/R8 edge-triggered battery posts, m2 notification clear, M9 ping window | Match |
| Three-band continuity rollout (2026-08-21) | Bands, boundaries, hysteresis, 0 dB atten, label tables, self-test vectors | All verified in code; doc rollout incomplete (see doc-audit cross-ref below) | Match (code) |
| Host tests | 217 checks / 10 binaries | Executed during review: all pass, exit 0 | Match |

---

## 4. Edge Cases & Safety

| # | Finding | Risk |
|---|---|---|
| 4.1 | Finding 2.1 — base ARMED after confirmed DISARM | **High** — operator-model mismatch; bounded by 10 s arm timeout |
| 4.2 | Finding 2.4 — FIRE commandable with arm key off | **High** — local interlock defeated; hardware key-in-coil-path AND gate still blocks actual firing |
| 4.3 | Finding 2.3 — continuity fails permissive | **High** — open igniter reads ready-to-arm on ADC failure |
| 4.4 | Finding 2.2 — battery-critical flag stale/misfiring | **Medium** — unit returns to service on critical battery; later spurious terminal ERROR mid-launch |
| 4.5 | No max-duration backstop in FIRING — if `EVT_FIRE_PULSE_DONE` is ever lost, FSM loops in FIRING with relay energized; TWDT won't trip (task keeps feeding) — `rlc_base_fsm.c:477–585, 632–717` | Low (latent, refactor hazard) — cheap defense-in-depth |
| 4.6 | Arm-verify window ignores EVT_KEY_SWITCH_CHANGED and sense-LOW; key-off mid-verify misreports `NACK_ARM_SENSE_FAULT` — `rlc_base_fsm.c:345–390, 636–641` | Low (hardware AND keeps safe) |
| 4.7 | ARMED→PRE_FIRE doesn't re-check key switch (guard 3) — `rlc_base_fsm.c:395–406` | Low — one-line guard; bounded by PRE_FIRE→FIRING check + hardware AND |
| 4.8 | Remote ACKs not correlated to `acked_seq_number`/`acked_msg_type`; `s_pending_cmd_*` dead — stale ACK can satisfy a later FIRE wait, stale NACK can spuriously disarm — `rlc_remote_fsm.c:373–380, 56–57` | Medium-bounded (base-state sync backstop) |
| 4.9 | Remote selected-channel stale vs encoder after LINKING/LINK_LOST — display can highlight a different channel than a long-press arms — `rlc_remote_fsm.c:525–527, 610–613` | Medium — re-sync `s_selected_channel` on IDLE entry |
| 4.10 | No reconciliation when base reports armed channel while remote IDLE (CMD_ARM ACK lost) — base armed indefinitely, remote shows IDLE — `rlc_remote_fsm.c:509–524` | Medium (UI-vs-hazard divergence; safe against spurious fire) |
| 4.11 | Stale-timeout path can send `CMD_DISARM(0)`; never sends CEASE_FIRE in PRE_FIRE/FIRING — `rlc_remote_fsm.c:757–761` | Low |
| 4.12 | Fresh-press interlock documented in `rlc_fire_button.h:25–31` but not enforced; `fire_button_was_fresh_press()` dead | Low — enforce or delete |
| 4.13 | Reset/brownout: relay GPIOs float until `relay_init()`; no panic/brownout handler drives outputs safe first; `gpio_config()` returns unchecked — `rlc_relay.c:34–57` | Info — hardware question (gate pull-downs); brownout mid-FIRING plausible on sagging pack (#24/#25 family) |
| 4.14 | seq-0 replay window after every session reset — accepts only attacker frames that already hold token+keys — `rlc_link.c:493–590` | Low |
| 4.15 | Replayed LINK_REQUEST invalidates a live session (availability DoS, not fire hazard) — `rlc_link.c:475–479` | Low |

---

## 5. Concurrency & Platform Issues

| # | Finding | Ref |
|---|---|---|
| 5.1 | WiFi task blocked by send-failure handler (Major 2.7) | `rlc_espnow.c:86–91`, `rlc_link.c:626–634` |
| 5.2 | Timestamp lag through two queues (Major 2.6) | `rlc_espnow.c:38–43`, `rlc_link.c:803` |
| 5.3 | `EVT_ARM_SENSE_CHANGED` posted with zero-timeout send — bypasses the J4 10 ms safety-send rule used by its two sibling callbacks; dropped arm-sense-lost event delays disarm | `rlc_base_main.c:55–59` |
| 5.4 | Siren esp_timer callback vs task-context calls race on `s_siren_on`/`s_pulse_count` — siren can stick on | `rlc_siren.c:19–41, 87–99` |
| 5.5 | Weld-fault callback re-fires every 500 ms forever (counter never reset while condition persists) — permanent log/event spam in terminal ERROR | `rlc_arm_sense.c:130–151` |
| 5.6 | STATUS_UPDATE send path wraps `s_tx_seq` on overflow instead of re-linking; peer then rejects as replay; and `set_state` LINKED→LINKING emits no FSM event | `rlc_link.c:256–284, 150–165` |
| 5.7 | Base never version-checks the remote — mismatch surfaces as 1.5 s "link trouble" instead of VERSION_MISMATCH | `rlc_link.c:344–393` |
| 5.8 | `rlc_link.h:66–70` documents the busy-guard polarity **inverted** vs implementation (`rlc_link.c:357–361`) — a future guard written to the header contract would accept LINK_REQUESTs while ARMED. Current caller correct. | doc-only, safety API — fix immediately |
| 5.9 | `rlc_link_send_cmd()` reads `s_state` before taking the mutex | `rlc_link.c:890–892` |
| 5.10 | Display + buzzer tasks not TWDT-registered — hung SPI freezes last-rendered screen ("ARMED"/"PRE-FIRE") forever with no reset | `rlc_display.c:1021–1135`, `rlc_buzzer.c:52` |
| 5.11 | Root cause of the known "task not found" TWDT boot bursts: spawned tasks (prio 3) call `esp_task_wdt_reset()` before the creator (prio 1) runs `esp_task_wdt_add()` — register self at task entry instead | `rlc_watchdog.c:42–50` + call sites |
| 5.12 | Encoder ISR calls non-IRAM code (`gpio_get_level`) without IRAM_ATTR/`CONFIG_GPIO_CTRL_FUNC_IN_IRAM`; rotate callback ISR-context contract undocumented in header | `rlc_encoder.c:108–126` |
| 5.13 | Battery task self-boost to priority 24 for whole 33 ms burst — depends on `configMAX_PRIORITIES ≥ 25`; stalls FSM task up to ~33 ms | `rlc_base_battery.c:40–43` |
| 5.14 | `rlc_rgb_led_init()` leaves mutex/task allocation unchecked — NULL mutex hits configASSERT reboot loop (the documented, previously-fixed failure class) | `rlc_rgb_led.c:304–307` |
| 5.15 | `xTaskCreatePinnedToCore(encoder_task_fn, …)` return unchecked (only unchecked task creation) | `rlc_remote_main.c:186` |
| 5.16 | Median-of-33 then averaged over 8-deep circular buffer — a burst whose median clips biases voltage up for ~8 s; clip warning log-only | `rlc_battery.c:141–167` |

---

## 6. Error Handling

- Finding 2.3 is the headline: continuity ADC errors fail **permissive** — every other subsystem found degrades toward link loss/safe; this one degrades toward "ready to arm".
- `EVT_BATTERY_CRITICAL` silently discarded in base LINK_LOST/BOOT (`rlc_base_fsm.c:298–305, 611–618`) and, because the battery posts it once per crossing, the drop is permanent — unit idles on a critical battery after link recovery with only guard 8 holding.
- `adc_cali_raw_to_voltage()` unchecked in `rlc_battery.c:148` — silent 0 mV (fails safe, but silently).
- `rlc_link_next_seq()` returns 0 on overflow while its header says "caller should re-link" — API invites misuse (`rlc_link.c:912–921`).
- `tools/test_tr04.py:117–133` prints FAIL but exits 0 — scripted runners see success.
- Debouncer fires one spurious initial "released" callback per input at first stable-HIGH read (`rlc_debounce.c:50–57`) — current callers level-driven.
- ESP-NOW deinit deletes rx task potentially mid-callback; never-called path, noted only.

---

## 7. Code Quality

- Dead API surface: `remote_fsm_get_armed_channel_ptr()`, `remote_fsm_stop_fire_repeat()`/`is_fire_repeat_active()` public forms, `remote_battery_get_status()`, `base_state_get_continuity()` (returns 0 — a trap), `fire_button_was_fresh_press()`, empty display stubs, `s_pending_cmd_*`. Delete or wire up.
- `LINK_REQUEST_SLOW_INTERVAL_MS` defined, logged, never applied (`rlc_link.c:651–654`).
- Send-failure link-loss line logged twice per event.
- Comment defects (code correct): `rlc_continuity.c:5` / `rlc_continuity.h:4–5` still say "4-band"; `rlc_config.h:221` "~20 Ω" (is ~67 Ω); duplicated comment block `rlc_selftest.c:511–519`; "GOOD=1" encoding comments at `:668/:672`; `rlc_arm_switch.c:111–113` comment contradicts code; buzzer pattern comments mismatch step timings.
- Host test mirrors production logic instead of including it: `test_armstate.c:26–38` re-implements `base_arm_state()` — same M2 anti-pattern as finding 2.5.
- `main/idf_component.yml` floors `idf >=5.0.0` but code needs 5.4 APIs; managed `espressif/esp-now` component pulled in but unused (native `esp_now.h` only) — dead dependency.
- Clean and worth noting as such: `rlc_message.c` (CRC scope verified), `rlc_fire_timer.c`, `rlc_status_update.c`, encoder decoder logic, display locking/rendering, host `run.sh`, `vbat_fit.py`.

---

## 8. Summary

| Category | Critical | Major | Minor | Info |
|----------|----------|-------|-------|------|
| Spec conformance | 0 | 1 (2.1 DISARM-during-verify) | 3 | 1 |
| Plan conformance (Phase-2 M2) | 0 | 1 (2.5 self-test copy) | 1 | 0 |
| Correctness | 0 | 2 (2.2 VBAT flag; 2.4 arm-key) | 6 | 3 |
| Safety & robustness | 0 | 1 (2.3 continuity permissive) | 4 | 2 |
| Concurrency & platform | 0 | 2 (2.6 timestamp; 2.7 WiFi-task block) | 9 | 3 |
| Error handling | 0 | 0 | 4 | 2 |
| Code quality | 0 | 0 | 5 | 3 |
| **Total** | **0** | **7** | **32** | **14** |

Prior-review fix re-verification: every fix from Phase 1 (C1–C6/R1–R10), Phase 2 (M1/M3/m1–m4), and Phase 3 rounds 1–3 (C/M/m/J/R/N series) was confirmed present and correct at the cited lines — with the single exception of Phase-2 M2 (finding 2.5). Host tests executed during this review: 10 binaries, 217 checks, all pass.

Cross-reference: a parallel documentation audit (2026-08-21) found the FSD's three-band rollout incomplete (§3 glossary, §14.5 constants, §7.2.4 "2000 ms" pulse, 15-vs-5 retries, §10.2.2 SHORT glyph, T-A15/T-U10/T-U12), the remote hw-test spec flashing the **base** board's by-id, the base hw-test spec wiring the LED to GPIO 47 (arm relay), `Phase3_Code_Review_002.md` still standing at FAIL with no closure note, and ~20 further stale-doc items. None of those affect firmware behavior; they affect operators following the books.

---

## 9. Recommendation

**MAYBE — do not live-fire until findings 2.1–2.4 are fixed.** All four are small, local fixes (abort_arm_verify on DISARM; act on/clear ERR_VBAT_CRITICAL in the FIRING exit path; fail-safe sentinel in sample_channel; abort-on-interrupt + arm-key guard in the remote ARM retry and fire paths). Findings 2.5–2.7 (self-test copy, C3 timestamp, WiFi-task blocking) are correctness-of-assurance and infrastructure issues — schedule immediately after, before Phase 5 hardening. The remaining minors batch naturally into a hardening pass; none blocks bench testing. The hardware interlock chain (key-in-coil-path AND gate, arm relay, channel relay, snubbers) remains the effective barrier even while 2.1/2.4 are open, which is why these are Major and not Critical.

— Verified and synthesized by the lead reviewer; every Major was re-read in source before inclusion.
