# Phase 5 Code Review — Hardening: Arm-Fire Sequence, Errors & Toast Screens

**Document ID:** RLC-REVIEW-ALL-009
**Reviewer:** Code Review Agent (3 parallel tracks: base FSM / remote+protocol / errors+screens, synthesized)
**Date:** 2026-08-28
**Scope:** Full codebase with focus on the arm-fire sequence, error handling, and toast/status screens — centered on the Phase 5 hardening delta since RLC-REVIEW-ALL-008 (fw 1.1.9 → 1.1.29)
**FSD Reference:** `RLC_Functional_Specification_v1_14.md` (content revision **v1.46**, 2026-08-27 — filename stale)
**Commit Reviewed:** `a101077` (main; firmware 1.1.29)

---

## Verdict: MAYBE

The hardware fire path is in the best shape it has ever been in: every prior-review Critical/Major was re-verified fixed (BF-01's three-layer fire-timer fix, CM-01's status-update locking, DS-01's runtime display health check, the fw 1.1.29 asymmetric debounce and its interaction with fresh-press detection are all correct and, where tested, pinned). No finding this round leaves a relay energized or extends a pulse. What this review found instead is a cluster of defects in the **operator-information layer** that sit directly on the fire path: one CRITICAL that silences the link-lost and critical-error **alarms** precisely when they originate from an armed state, and six MAJORs in which the remote's display/buzzer actively misdescribes the pad (stuck "IGNITION ACTIVE" over a base locked in terminal ERROR; a false FIRE COMPLETE for a channel that never carried current; the splash and FIRE COMPLETE screens outranking live/alarmed states). For a launch controller, telling the operator the wrong thing about a live pad is a safety defect, not a UX one — hence neither PASS nor FAIL: **safe for continued bench testing and fault-injection work immediately; fix CRIT-01 and MAJ-01/02/04 before the next live-fire session.**

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

| Track | Files |
|------|-------|
| Base arm-fire | `components/rlc_base/src/rlc_base_fsm.c`, `rlc_arm_sense.c`, `rlc_relay.c`, `rlc_fire_timer.c`, `rlc_continuity.c`, `rlc_status_update.c`, `rlc_siren.c`, `rlc_base_main.c` |
| Remote + protocol | `components/rlc_remote/src/rlc_remote_fsm.c`, `rlc_fire_button.c`, `rlc_arm_switch.c`, `rlc_encoder.c`, `rlc_remote_main.c`, `components/rlc_common/src/rlc_debounce.c`, `rlc_link.c`, `rlc_espnow.c`, `rlc_message.c`, `include/rlc_protocol.h`, `rlc_config.h`, `rlc_fsm_events.h` |
| Errors & screens | `components/rlc_remote/src/rlc_display.c` (full), `rlc_buzzer.c`, `components/rlc_common/src/rlc_rgb_led.c`, `rlc_siren.c`, NACK/error plumbing in `rlc_protocol.h` |

All Critical/Major findings below were independently re-verified against source by the synthesizer before inclusion.

---

## 1. Coverage Analysis

### 1.1 Prior review (RLC-REVIEW-ALL-008, verdict FAIL) — fix verification

| Prior finding | Status now |
|---|---|
| **BF-01** fire timer never stopped on completion (CRITICAL) | **FIXED, 3 layers, verified**: stop-first + tolerate `ESP_ERR_INVALID_STATE` (`rlc_fire_timer.c:67-71`), checked return → ERROR latch instead of `ESP_ERROR_CHECK` panic (`rlc_fire_timer.c:83-101`, `rlc_base_fsm.c:940-947`), `fire_timer_stop()` on the successful path (`rlc_base_fsm.c:644`) and every other FIRING exit (:635-735, :960-975); stale-notification clearing in start and stop |
| **CM-01** unlocked `rlc_link_send_status_update()` | **FIXED, verified**: all shared state under `s_state_mutex`, send outside the lock (`rlc_link.c:399-430`) |
| **DS-01** runtime display health check missing | **FIXED, verified**: 5 s ID re-read + `s_spi_errors` streak, serialized in the display task, latch + `EVT_DISPLAY_FAULT` → CEASE_FIRE/DISARM + ERROR (`rlc_display.c:1475+`, `1849-1864`; `rlc_remote_fsm.c:558-577`); fw 1.1.28 hardened the undriven-signature rejection |
| **TT-04** zero FSM tests | **FIXED**: host FSM harness exists (`tests/host/test_base_fsm.c`, 774 lines) and pins dead-man, wrong-channel, arm-verify, and two-cycle behavior |
| **CM-02/CM-04/CM-05** NACK-on-replay/CRC, truncated ACKs, seq-0 window | **FIXED, verified** (track B §"anti-replay") |
| **RM-05/RM-06** buzzer mailbox race, stray repeat after release | **FIXED** (`xQueueOverwrite`, repeat-task button poll) — see CRIT-01 for the new interaction this fix participates in |
| **BF-02** freshness test folded into rate test | **FIXED**: separate `rlc_link_ms_since_contact()` freshness guard, correct per-state abort routing |
| **BF-03** continuity-lost siren pattern | **FIXED**: `SIREN_CONTINUITY_LOST` at the disarm sites |
| **DOC-01/02/13** continuity-disarm / "DATA STALE" contradictions | Swept in FSD v1.43/v1.44 (spot-checked §7.2.7 semantics now consistent) |
| **DOC-03/04/TT-01** hw-test-spec GPIO 47, stale by-ids, `test_tr04.py` ports | **Not re-verified this round** — outside the review focus; recommend a 5-minute bench-doc check before those docs are next followed |

### 1.2 Phase 5 delta (fw 1.1.9 → 1.1.29)

| Feature | Status |
|---|---|
| BF-01 fix + two-cycle regression test | DONE, verified (incl. live halogen retest per Development_Progress) |
| Host FSM injection harness (TT-04) | DONE |
| Bug #20 key rotation (`rlc_secrets.h`, gitignored, pre-commit) | DONE (per changelog; not re-audited) |
| Cease-fire toasts (fw 1.1.24, f0a880c) | DONE — wording family verified consistent; audible half defeated by CRIT-01 |
| FIRE COMPLETE 10 s hold + live igniter line (fw 1.1.23/1.1.27) | DONE — band→text/colour mapping exact per §10.2.4a; screen-precedence defects MAJ-04/05 |
| Audible ARMED/FIRING state tones (fw 1.1.27) | DONE — tempo separation per §12.1; background-nudge interaction is CRIT-01 |
| Cut-short discrimination (fw 1.1.27, cf797c0) | PARTIAL — FIRING→IDLE case closed; PRE_FIRE-abort window open (MAJ-02) |
| Boot display health check hardening (fw 1.1.28) | DONE |
| Asymmetric dead-man debounce (fw 1.1.29, §5.3) | DONE, verified correct at every layer incl. fresh-press interaction; pinned by T-D07/T-D08 |

---

## 2. Deviation Report

### CRITICAL

**CRIT-01 — `buzzer_set_background()`'s OFF-nudge atomically deletes any alarm/beep queued in the same FSM tick — the link-lost and critical-error alarms are completely silent when the transition originates in ARMED/PRE_FIRE/FIRING.**
`rlc_buzzer.c:226-234` + `rlc_remote_fsm.c:1256-1272`, `:443`, `:461`

`buzzer_play()` is an `xQueueOverwrite` on a depth-1 mailbox (RM-05's fix — correct in isolation). `buzzer_set_background()` ends with `buzzer_play(BUZZER_OFF)` as a "nudge". The FSM task runs `process_event()` then `check_timers()` back-to-back; `check_timers()` sets the background tone from the state every tick (`:1256-1272`), and the `default:` branch sets `BUZZER_OFF`. So any `buzzer_play(X)` issued by a handler that also changes state out of ARMED/PRE_FIRE/FIRING is overwritten with OFF microseconds later — before the priority-1 buzzer task can ever dequeue it — and the resume loop (`rlc_buzzer.c:158-174`) only re-arms the ARMED/FIRING tones, so the pattern never comes back. Verified scenarios:

- **Link lost while ARMED** — `do_enter_link_lost()` queues `ALARM_LINK_LOST` (`:443`); same tick, background goes ARMED→OFF, nudge replaces it. **The link-lost alarm never sounds**, at exactly the moment the pad state is unknown. (From IDLE the idempotence guard saves it — failure is specific to armed-origin transitions, which is why T-A20 spot-checks passed.)
- **ERROR entered from armed states** (battery critical, multi-arm, `EVT_DISPLAY_FAULT` while ARMED at `:558-577`): `ALARM_CRITICAL` (`:461`, `:224`) swallowed the same way. Worst case DISPLAY_FAULT: screen dead by definition, buzzer silenced, only the LED triple-flash survives — both halves of §7.2.9a gone on the highest-hazard path.
- **Every FIRE-guard refusal and disarm beep from ARMED** (`:881-913` triple → `do_disarm_and_idle()`'s `BEEP_LONG` at `:433` → tick's OFF): net zero audible for "ARM KEY OFF - FIRE REFUSED", "BASE NOT ARMED", stale-status, link-degraded, send-failure, FIRE NACK, FIRE timeout, and the FIRING "PULSE CUT SHORT" triples (`:1092`, `:1106`, `:1178`). The toast still appears; §7.2.9a requires **both**, and v1.39 claims this family was fixed.
- `BEEP_DOUBLE` arm-confirm (`:747`) likewise replaced by the ARMED background nudge (lower impact — the heartbeat starting is itself a cue).

Violates FSD §7.2.9a, §12.1 (ALARM_LINK_LOST/ALARM_CRITICAL/BEEP_TRIPLE "Error/NACK"). Fix direction: the OFF-nudge must not destroy a just-queued pattern — e.g. only overwrite when the mailbox is empty and nothing is playing, or make the nudge a non-destructive "stop current pattern".

### MAJOR

**MAJ-01 — Remote FIRING never syncs to a base that entered ERROR (or LINK_LOST): "IGNITION ACTIVE" + firing tone + 5 Hz CMD_FIRE repeats over a base that is terminally broken.**
`rlc_remote_fsm.c:1132-1133`

The FIRING `EVT_STATUS_UPDATE` branch is a whitelist that acts only on `base_state == POST_FIRE || IDLE`; PRE_FIRE's equivalent (`:1049-1051`) is a blacklist that syncs on any departure. An arm-relay weld fault (`EVT_ARM_SENSE_FAULT`) mid-pulse drives the base to terminal ERROR — which keeps sending STATUS_UPDATEs (`rlc_status_update.c` sends in any state while linked) — but ERROR matches neither whitelist entry, so the remote stays in FIRING indefinitely (status stays fresh; the 5 s stale path never fires). Every repeated CMD_FIRE is NACKed `NACK_BASE_ERROR` and the NACK is discarded (MAJ-03). Hardware is safe (base relays off), but the remote asserts an ongoing ignition for as long as the button is held, and button release then reports "CH n PULSE CUT SHORT" — misdescribing a base needing a power cycle. Fix: mirror the PRE_FIRE blacklist — any base state outside {PRE_FIRE, FIRING, POST_FIRE} ends the sequence, classified by the existing discriminator.

**MAJ-02 — False FIRE COMPLETE (10 s green screen) for a channel that never carried current: base aborts during PRE_FIRE + one lost STATUS_UPDATE.**
`rlc_remote_fsm.c:1135-1157`

`completed = (base_state == POST_FIRE) || (fired_ms >= FIRE_PULSE_DURATION_MS)`, where `fired_ms` runs from the **remote's local** FIRING entry — silently assuming local FIRING entry ⇒ base energized the channel. The base can abort during the 5 s countdown (continuity OPEN, key off, arm-sense, battery, dead-man); the remote's local countdown keeps running. If the abort's triggered STATUS_UPDATE is lost (single-frame loss suffices; PONGs unaffected, link stays healthy) the next *periodic* status ≤2 s later shows IDLE with `fired_ms ≥ 1000` → **FIRE COMPLETE for a never-fired channel**. The stale-timeout backstop does not cover it (status age ≤3 s < 5 s at classification). Mirror image: abort status landing just after local FIRING entry with `fired_ms < 1000` → "CUT SHORT AT BASE", asserting current that never flowed. Same class as the cf797c0 fix, which closed the during-pulse case but not the before-energization case. Fix direction: require positive evidence the base reached FIRING (e.g. a status carrying the firing bitmask, or heeding the NACK stream per MAJ-03) before the elapsed-time backstop may declare completion.

**MAJ-03 — Remote discards the NACKs answering its repeated CMD_FIREs — the fastest base-abort signal it has (root amplifier of MAJ-01/02).**
`rlc_remote_fsm.c:1028-1066`, `:1069-1192`; base side `rlc_base_fsm.c:500-502` (IDLE → `NACK_WRONG_STATE`), `:796-811` (ERROR → `NACK_BASE_ERROR`)

`EVT_CMD_ACK`/`EVT_CMD_NACK` are consumed only inside `wait_for_ack()` (`:492-504`); PRE_FIRE and FIRING have no NACK handler. §8.4's fire-and-forget rule forbids the base answering *while in* PRE_FIRE/FIRING — but once the base leaves the firing path it NACKs every repeat within ~200 ms, cleanly correlated against the repeat task's current `s_pending_cmd_type/seq`. Discarding them makes base-abort detection latency a 2 s STATUS_UPDATE matter instead of 200 ms — exactly the window MAJ-01/02 live in. Fix: handle a correlated NACK for the pending fire as "base ended the sequence" (the correlation machinery already exists).

**MAJ-04 — The 10 s splash hold outranks the ARMED/FIRING screens: the display can show the boot splash over a live armed pad.**
`rlc_display.c:1590-1606`

Screen precedence is fw_mismatch > error > **splash** > fire_complete > FSM state; inputs are live well before the 10 s `SPLASH_MIN_DURATION_MS` expires and linking completes in ~1 s, so the FSM can sit in ARMED (relay closed, base siren on, buzzer heartbeat, LED red) for up to ~9 s under a "Connected to base" splash. The `fire_done` branch already got an ARMED/PRE_FIRE/FIRING escape with the comment "a summary of the last shot must never sit on top of a live ARMED state" (`:1602-1605`) — the same argument applies to the splash with more force. FSD §10.2.1 requires no such override. One-line precedence fix.

**MAJ-05 — FIRE COMPLETE outranks LINK_LOST during its 10 s hold: green success screen over a declared-dead link.**
`rlc_display.c:1602-1605`

The early-cancel set is only {ARMED, PRE_FIRE, FIRING}. If the link drops inside the hold (plausible: base power killed right after a shot), the FSM enters LINK_LOST with `ALARM_LINK_LOST` sounding while `fire_done && state==LINK_LOST` keeps the green FIRE COMPLETE screen for the remainder of the 10 s; the igniter line greys to "IGNITER ?" underneath. Spec-letter-compliant (§10.2.4a's cancel list omits LINK_LOST) but spec-spirit-contradictory — add LINK_LOST to the cancel set (in FSD and code).

**MAJ-06 — Three refusal/abort paths have a display message but no buzzer at all (independent of CRIT-01).**
`rlc_remote_fsm.c:644-648` (arm guard 1: "TURN ARM KEY FIRST" — the only sibling guard without `BUZZER_BEEP_TRIPLE`), `:751-757` (ARM interrupted, result −4: "ARM CANCELLED", no beep and no disarm beep follows), `:1049-1056` ("BASE ENDED SEQUENCE" in PRE_FIRE — tone cessation only, the weakest of the three). §7.2.9a requires both indications on every operator-triggerable refusal.

### MINOR

| ID | Location | Finding |
|----|----------|---------|
| MIN-01 | `rlc_base_fsm.c:412-446`, `:312` | Second CMD_ARM inside the 200 ms verify window silently replaces the pending verify — first ARM never answered (FSD §7.2.2 exception says first wins, subsequent NACK'd). Last-wins today; low reachability, annoyance-direction only. |
| MIN-02 | `rlc_base_fsm.c:853-860` | Arm-verify timeout NACKs 0x0B and stays IDLE without latching `ERR_RELAY_FAULT`, though §7.2.2 action 2 and §13.1 case (a) say disarm-and-ERROR. §7.2.2's own "remain in IDLE on guard failure" contradicts itself; needs an explicit spec decision either way. |
| MIN-03 | `rlc_base_fsm.c:634-735` | `EVT_LINK_RECOVERED` unhandled in FIRING: with `COMPLETE_PULSE_ON_LINK_LOSS=1`, recovery before pulse end strands the FSM in LINK_LOST with a live link, silently dropping commands until the next full loss/recovery. Unreachable with current constants (link-loss 1.5 s > FIRING 1.25 s) — becomes live if `FIRE_PULSE_DURATION_MS` ever exceeds ~1.5 s. |
| MIN-04 | `rlc_link.c` ~:846-862 | Commands forwarded to the FSM queue with `xQueueSend(..., 0)`; a burst can drop a CEASE_FIRE/DISARM (bounded: every state has a hard exit). The one remaining zero-timeout poster on a safety-relevant path — document or NACK-on-drop. |
| MIN-05 | `rlc_remote_fsm.c:921-971` | FIRE ACK-wait `-2` (channel mismatch) falls into the generic else → "NO RESPONSE - FIRE ABORTED"; the ARM path names it explicitly. |
| MIN-06 | `rlc_remote_main.c:64-114` | Remote input callbacks post with zero timeout (base migrated off this for BF-05); a dropped `EVT_FIRE_BUTTON_RELEASED` ends the sequence via base dead-man + "BASE ENDED SEQUENCE" instead of the operator's own cease-fire — fail-safe, but the fact is misreported. |
| MIN-07 | `rlc_remote_fsm.c:159-163`, `:685-693` | Multi-flag error toasts truncate mid-word ("…RELAY F") and can overrun the 440 px overlay box. Clamp to the most severe flag (the §13.2a cycling already exists on the main screen). |
| MIN-08 | `rlc_display.c:1265-1266` | FIRE COMPLETE "IGNITER ?" row draws an OPEN-ring glyph next to grey unknown-text — asserts a shape for an unknown state. |
| MIN-09 | `rlc_remote_fsm.c:721-724` | ARM retry aborted because the arm key was turned off is reported as "NO RESPONSE FROM BASE" — the exact misattribution class the −4 "FIRE CANCELLED" fix was written to prevent. |
| MIN-10 | `rlc_display.c:886-894` vs `rlc_link.c:1311+` | Degraded-but-LINKED window: top bar says "LINK OK" while the buzzer chirps and the ARM guard refuses "LINK DEGRADED". A "LINK WEAK" state would reconcile them. |
| MIN-11 | `rlc_remote_main.c:171-189` | Boot display-fault halt is LED+log only — `buzzer_init()` runs after the check, so no audible is possible for a total display failure at boot. |
| MIN-12 | Guard parity (§8.2.3 guard 4) | Spec says "last ping succeeded"; implementation uses ≤30 % failure over 10 pings — a just-failed ping still passes arming. Also the remote commits to the sequence with contact up to 4 s old vs the base's 1 s ignition freshness (base re-validates — safe, but the operator can see a full 5 s countdown die at expiry). |

### INFO

- **INF-01** `rlc_config.h:75` vs `rlc_arm_sense.c:240`: arm-verify window (200 ms) leaves ~40 ms margin over sense debounce (160 ms) + relay operate time — false `NACK_ARM_SENSE_FAULT` on a slow relay; safe direction, thin margin, deserves a comment.
- **INF-02** `rlc_fire_timer.c:22-33`: §7.4.2's "channel as timer-callback context + completion assertion" implemented as a channel-less notify — equivalent-or-safer simplification; note it in code or FSD.
- **INF-03** `rlc_status_update.c:55-70`: state/channels/flags snapshotted non-atomically — a transition-straddling frame can pair ARMED with mask 0; self-corrects ≤2 s, display-only.
- **INF-04** `rlc_base_fsm.c:711-716`: battery-critical-during-FIRING latch is terminal even for a transient igniter-current sag on a weak pack — spec-conformant; operator note.
- **INF-05** `rlc_encoder.c:44`: `s_channel` not `volatile` despite ISR writes (its neighbour `s_max_channel` is) — works on Xtensa, an LTO trap.
- **INF-06** `rlc_remote_fsm.c:424-436`: `do_disarm_and_idle()` doesn't re-sync `s_selected_channel` from the encoder (display highlight can lag one detent; no wrong-channel arm possible).
- **INF-07** `rlc_remote_fsm.c:1351-1355`: sub-ms window for one stray CMD_FIRE after CEASE_FIRE on the wire — benign by construction (base is IDLE, NACKs, cannot restart anything).
- **INF-08** `STATUS_STALE_TIMEOUT_MS` (5000) == `PRE_FIRE_DELAY_MS` (5000): a status outage starting exactly at fire-press makes both expire together — fail-safe direction; worth a comment.
- **INF-09** Overlay semantics: single-slot newest-wins (`overlay_post()`) — a guard toast can hide a "DATA GAP" toast posted 100 ms earlier. Acceptable for operator-paced events; noted.
- **INF-10** Error-screen wrap slices at 34 chars with no space awareness — latent (current strings fit).
- **INF-11** Same condition, two severities: base-in-ERROR found by local guard 4 shows an amber NOTICE toast; the same condition as NACK 0x0E shows the red COMMAND REJECTED overlay. Texts identical.
- **INF-12** Documentation: FSD §15.4 T-S09 row still says "silently ignores" (flagged in Development_Progress 2026-08-27, not yet swept); Development_Progress's firmware-version history table is stale (ends at 1.1.9).

---

## 3. Plan vs. Implementation

No Phase 5 implementation plan exists (the two `Implementation_Plan_Phase3_*` files are earlier, status Completed) — section applies only to Phase 3 conformance, which the prior review covered. For Phase 5, Development_Progress.md entries (fw 1.1.23–1.1.29) and changelog.md are the record; spot-checks found them accurate on behavior and honest about partial verification (e.g. the 1.1.29 mash retest numbers, T-S06 "partial pass"). The stale firmware-version table (INF-12) is the one bookkeeping gap.

---

## 4. Edge Cases & Safety

**What was verified sound** (full traces in track reports; the load-bearing items):

1. **Every FIRING exit is safe and timer-stopped** — all six base exits funnel through `firing_exit()`/`fire_timer_stop()` + `relay_all_safe()` + siren off; duplicate pulse-done events inert; the 4.5 backstop closes the lost-notification hole; `ERR_VBAT_CRITICAL` "complete pulse then ERROR" honored without double execution.
2. **Asymmetric debounce (fw 1.1.29)** — press 8/8, release 2/8, applied only to the fire button; fresh-press interlock intact (initial determination fires no callback); every ≥20 ms release now produces a cease-fire and re-firing requires full re-arm; the repeat task additionally polls the button directly, so even a dropped release event stops the repeats. The mash defect is closed at the event layer.
3. **Dead-man arithmetic** — 200 ms repeats vs 500 ms authorization from wire-receive time: one lost repeat is harmless; two consecutive losses only matter if they straddle countdown expiry, and then the base aborts (safe direction). Full packet-loss traces (initial fire lost / ACK lost / sustained one-way / sustained two-way) all converge safe.
4. **Cease-fire coverage** — PRE_FIRE and FIRING both handle button release, arm key, encoder, base-departure status, link loss, battery critical, display fault, stale status. No path keeps the channel firing without a cease-fire; worst case (remote crash) bounded by base dead-man + GPTimer.
5. **Anti-replay/session/CRC** — strict seq with the seq-0 window closed, per-frame token check, NACK-on-replay/CRC, truncated frames dropped, overflow drops the link rather than wrapping.
6. **Continuity-loss disarm** — edge + level backstop, ARMED/PRE_FIRE only, OPEN only, armed channel only; FIRING/POST_FIRE exclusions exactly per §7.2.7; audible (siren) + remote indication.
7. **Display overlay machinery** — mutex-snapshotted per frame, absolute-time latch cannot stick, retiring overlay forces full redraw; §13.2a error cycling exact; FIRE COMPLETE band mapping exact; link-lost screen fields from the right sources; BASE field driven by arm sense, never the key switch.

**The gaps** — CRIT-01 and MAJ-01…06 above — are all *operator-information* defects: nothing extends a pulse or energizes a relay, but the remote can (a) be silent about link loss/error exactly when armed, (b) assert "IGNITION ACTIVE"/"FIRE COMPLETE" about a pad that is safe-but-broken or never fired, and (c) cover live states with splash/success screens. For a system whose whole safety argument at the operator end is "the indications never lie," these need fixing before live fire.

**Remote/base desync matrix** (track B, verified where marked):

| Remote | Base | Trigger | Operator sees | Verdict |
|---|---|---|---|---|
| FIRING | POST_FIRE | normal | FIRE COMPLETE 10 s | ✓ |
| FIRING | IDLE | cut short mid-pulse | CUT SHORT toast | ✓ (cf797c0 holds) |
| FIRING | IDLE | aborted in PRE_FIRE, frame lost | **false FIRE COMPLETE** | ✗ MAJ-02 |
| FIRING | **ERROR** | weld fault mid-pulse | **firing tone indefinitely**; release says CUT SHORT | ✗ MAJ-01 |
| FIRING | LINK_LOST | pulse-end race | same as MAJ-01 window | ✗ MAJ-01 |
| PRE_FIRE | IDLE | base abort | "BASE ENDED SEQUENCE" ≤2 s | ✓ (no beep — MAJ-06) |
| ARMED | IDLE | continuity loss / arm timeout | named toast + distinct beep | ✓ |
| IDLE | ARMED | ARM ACK lost | "BASE STATE MISMATCH - DISARMED" + disarm-all | ✓ |
| ARMED/LINK_LOST | armed/lost | one-way loss | transient double-alarm ≤1.5 s, both disarm | ✓ |
| ACK-wait | — | — | all return codes handled, waits bounded | ✓ |

---

## 5. Concurrency & Platform Issues

- CRIT-01 is at heart a **cross-task ownership** bug: two writers to a depth-1 mailbox (FSM task handlers + the tick-driven background setter) with no ordering contract, where the "newest wins" primitive that fixed RM-05 became the hazard.
- MIN-04/MIN-06: the last zero-timeout queue posts on safety-relevant paths (base command forward, remote input callbacks). Both are bounded by hard deadlines elsewhere; align with the base's 10 ms blocking-send policy for uniformity.
- Otherwise clean: portMUX on the status cache, documented single-writer invariants for pending-cmd fields, ISR/task queue API split correct, buzzer/LED/siren task priorities per §9.10, TWDT subscriptions fed, siren esp_timer stale-callback race handled (N2), CM-01's lock correctly not held across `esp_now_send`.

---

## 6. Error Handling

- **§7.2.9a "no silent refusals" audit** — the display half is essentially complete (every traced refusal/abort path posts a toast or screen; ERROR answers all four commands with 0x0E; NACK enrichment from cached flags is correct). The **audible half fails in three ways**: CRIT-01 (swallowed wholesale on armed-origin transitions), MAJ-06 (three paths never play anything), MIN-11 (boot display fault has no buzzer yet).
- Misattribution family: MIN-05 (−2 as "NO RESPONSE"), MIN-09 (key-off retry abort as "NO RESPONSE"), MAJ-01's terminal "PULSE CUT SHORT" over a base in ERROR — all blame the wrong subsystem for an operator- or base-initiated event.
- Base error entry/latching verified: terminal ERROR from any state, `EVT_ARM_SENSE_FAULT` consumed pre-switch, relay-safe before ERROR, watchdog-reset flag surfaced. One spec decision needed: MIN-02 (arm-verify timeout vs `ERR_RELAY_FAULT`).

---

## 7. Code Quality

Consistently high, and the numbered-fix-comment discipline (BF/CM/DS/RM/m/J/R series) continues to make provenance traceable — several findings this round were confirmed *against* those comments (e.g. the RM-05 mailbox fix interacting fatally with the state-tone nudge). Substantive items only: the remote FSM's PRE_FIRE/FIRING status-handling asymmetry (blacklist vs whitelist, MAJ-01/03) is the one structural inconsistency; `rlc_display.c`'s screen-precedence chain now has three special cases with three different escape sets — a single documented precedence table (or shared "live-state" predicate) would prevent the next MAJ-04/05. The signalling contradictions found were all precedence/ownership bugs, not intent: the band/colour/word mappings themselves are internally consistent and spec-conformant throughout.

---

## 8. Summary

| Category | Critical | Major | Minor | Info |
|----------|----------|-------|-------|------|
| Spec conformance | 0 | 1 (MAJ-06 §7.2.9a) | 3 (MIN-02, MIN-09, MIN-12) | 2 |
| Correctness | 1 (CRIT-01) | 3 (MAJ-01/02/03) | 3 (MIN-01, MIN-05, MIN-06) | 4 |
| Safety / operator information | 0 | 2 (MAJ-04/05) | 2 (MIN-07, MIN-11) | 3 |
| Concurrency/RTOS | 0 | 0 | 2 (MIN-04 + MIN-06 counted above once) | 1 |
| Error handling | 0 | 0 | 2 (MIN-03, MIN-10) | 2 |
| Documentation | 0 | 0 | 0 | 2 (INF-12) |
| **Total** | **1** | **6** | **12** | **12** |

(Counts by primary category; several findings straddle two.)

---

## 9. Recommendation

**Conditional GO**: proceed with Phase 5 bench/fault-injection work now; **no live fire until CRIT-01, MAJ-01, MAJ-02, and MAJ-04 are fixed and reflashed to both units** — they are all small, localized fixes (one buzzer nudge guard, one whitelist→blacklist, one completion-evidence gate, one precedence line), but each puts a false or missing indication on a live pad.

Ordered action list:

1. **CRIT-01** — make the `buzzer_set_background()` OFF-nudge non-destructive (mailbox-empty-and-idle guard, or a separate "stop" op). Re-check by ear from ARMED: link-kill, battery-critical, display-fault, and every FIRE-guard refusal must beep.
2. **MAJ-01** — FIRING `EVT_STATUS_UPDATE`: blacklist instead of whitelist; classify via the existing discriminator; ERROR → a truthful "BASE ERROR" path, not "CUT SHORT".
3. **MAJ-02** — gate the elapsed-time completion backstop on positive evidence the base reached FIRING (firing-bitmask status or heeded NACK per MAJ-03).
4. **MAJ-03** — handle correlated `EVT_CMD_NACK` in PRE_FIRE/FIRING as "base ended the sequence" (collapses the MAJ-01/02 windows 10×).
5. **MAJ-04/05** — display precedence: add the live-state escape to the splash branch; add LINK_LOST to the FIRE COMPLETE cancel set (code + FSD §10.2.4a).
6. **MAJ-06 + MIN-11** — add the three missing beeps; init the buzzer before the boot display check.
7. **MIN-02** — spec decision: either §7.2.2 drops "set ERR_RELAY_FAULT" on verify timeout or the code latches it.
8. Remaining MINORs at maintainer's pace; MIN-03 becomes mandatory before any `FIRE_PULSE_DURATION_MS` increase past ~1.5 s.
9. Documentation sweep: FSD §15.4 T-S09 row, Development_Progress version-history table (INF-12), plus a quick check of DOC-03/04/TT-01 leftovers before hw-test docs are next used on the bench.

---

*Review artifacts: 3 track reports (base FSM / remote+protocol / errors+screens) synthesized into this document; every Critical/Major re-verified against source by the synthesizer (rlc_buzzer.c:214-242, rlc_remote_fsm.c:1040-1192/1256-1272, rlc_display.c:1580-1624, rlc_base_fsm.c:490-505/780-811, rlc_link.c:399-430, rlc_fire_timer.c:61-107). Read-only review — no source files modified.*
