# Test Report — Phase 5 Review Fixes (fw 1.1.30)

**Date:** 2026-08-28
**Tester:** Code Test Agent (automated + log analysis) + David Steeman (manual/on-target)
**FSD Reference:** `RLC_Functional_Specification_v1_14.md` (content revision v1.47)
**Commit Tested:** `c7b7547` — firmware **1.1.30** on both units
**Scope:** The fixes made for `Code_Review_Phase5_20260828_0641.md` (RLC-REVIEW-ALL-009) — CRIT-01, MAJ-01…06, and the minors settled by operator decision (MIN-02, MIN-04, MIN-12)

---

## Executive Summary

Eleven on-target tests were run against both units flashed at 1.1.30, backed by
the full host suite (467 checks). **All eleven passed**, including the headline
Critical fix: with the pad armed, cutting base power now produces a continuous
audible alarm that stops on recovery — the condition that was completely silent
in 1.1.29. Ten of the eleven are corroborated by console captures from both
units; one (T-30-05) is operator-observed only, because the logging tool went
deaf at the moment of the remote reset it was testing.

The fire path behaved correctly in five live pulses into the halogen on ch1: a
clean pulse still reports FIRE COMPLETE, a pulse cut by the pad key reports
`CH 1 CUT SHORT - BASE KEY` **150 ms** after the key turn, and the FIRE COMPLETE
screen yields immediately to LINK LOST when the link drops inside its hold.

**MAJ-03 was isolated and proven** using the base's fault-injection build to
suppress the status frame that normally beats it: the NACK path ended the
sequence in **160 ms from PRE_FIRE** and **200 ms from FIRING**. That work also
established that MAJ-03 is a backstop rather than the primary detector, because
`firing_exit()` pushes a status update on every FIRING exit — a narrower claim
than the review made for it.

Not covered on target: MAJ-01 (base in terminal ERROR mid-pulse), CRIT-01's
critical-error half, and MIN-10. MIN-02 and MIN-04 are covered by host tests and
by construction respectively. One new MINOR finding was raised (§4, finding 4):
the MAJ-02 evidence gate under-claims for a fire-button release within ~200 ms
of ignition.

---

## 1. Automated Test Results

Host suite, run via `tests/host/run.sh` (also gated automatically by
`build_base.sh` / `build_remote.sh` before every firmware build).

| Binary | Unit | Checks | Result |
|---|---|---|---|
| `test_armstate.c` | base / remote | 27 / 27 | PASS |
| `test_base_fsm.c` | base | 120 | PASS |
| `test_battery.c` | base / remote | 13 / 13 | PASS |
| `test_debounce.c` | base / remote | 37 / 37 | PASS |
| `test_encoder.c` | remote | 15 | PASS |
| `test_errflags.c` | base / remote | 38 / 38 | PASS |
| `test_seqgap.c` | base / remote | 21 / 21 | PASS |
| `test_strip.c` | base / remote | 30 / 30 | PASS |

**Automated totals: 467 checks, 0 failures, 0 errors.**

Two suites grew with this fix round:

- `test_base_fsm.c` 111 → **120 checks** — three new T-FSM02 cases pinning
  **MIN-02**: the first arm-verify timeout NACKs `0x0B` and latches no error
  flag; the second consecutive one sets `ERR_RELAY_FAULT` and enters ERROR with
  relays safe; a successful verify in between clears the strike count.
- `test_errflags.c` 31 → **38 checks** — new T-E08 for `rlc_error_flags_brief()`
  (**MIN-07**): worst-flag selection, `+n` suffix, severity ordering, the
  40-character overlay budget, and a 4-byte buffer safety case.

Both firmware images also build clean (no warnings) for base and remote, and
the on-target self-test passes on both: `Version comparison self-test: PASS
(v1.1.30)`.

---

## 2. Manual Test Results

All timings below are from the dual-console capture (`/tmp/rlc_base.log`,
`/tmp/rlc_remote.log`), relative to logger start.

| ID | Test | Fix under test | Result | Evidence |
|----|------|---------------|--------|----------|
| T-30-01 | Base power cut while ARMED | **CRIT-01** | **PASS** | log + operator |
| T-30-02 | Long-press with arm switch OFF | MAJ-06 | **PASS** | log + operator |
| T-30-03 | Arm, then disarm by encoder | **CRIT-01** | **PASS** | log + operator |
| T-30-04 | State tone tempo (ARMED vs firing) | 1.1.27 regression | **PASS** | operator |
| T-30-05 | Arm inside the 10 s splash hold | MAJ-04 | **PASS** | operator only — capture lost |
| T-30-06 | Base key to SAFE mid-pulse | MAJ-02 / MAJ-03 | **PASS** | log + operator |
| T-30-07 | Clean full pulse | MAJ-02 regression | **PASS** | log + operator |
| T-30-08 | Link lost inside FIRE COMPLETE hold | MAJ-05 | **PASS** | log + operator |
| T-30-09 | Status outage across ignition (incidental) | §8.3 stale timeout, INF-08 | **PASS** | log |
| T-30-10A | PRE_FIRE abort, status frame suppressed | **MAJ-03** | **PASS** | log + operator |
| T-30-10B | FIRING abort, status frame suppressed | **MAJ-03**, MAJ-02 gate | **PASS** | log + operator |

**Manual totals: 11 PASS / 0 FAIL / 0 SKIP.**

T-30-09/10A/10B were run against a base flashed with the fault-injection build
(`./build_base.sh flash --inject`), using its `s` key to suppress STATUS_UPDATE.
The base was reflashed with a normal build immediately afterwards and verified:
`CONFIG_RLC_FAULT_INJECTION` absent from the built config and zero injection
symbols in `rlc.elf`.

### 2.1 T-30-01 — the Critical fix (CRIT-01)

The defect: `buzzer_set_background()`'s OFF-nudge was an atomic overwrite of a
one-deep mailbox, so an alarm queued in the same 50 ms FSM tick as a transition
out of ARMED/PRE_FIRE/FIRING was destroyed before the player task could dequeue
it. Link-lost and critical alarms were therefore **silent whenever the fault
arrived while armed**.

```
[404.08] remote: rlc_rfsm: IDLE -> ARMED (ch 1)
[407.09] remote: rlc_link: LINK LOST (3 missed pings)
[407.09] remote: rlc_rfsm: -> LINK_LOST          <- transition originates in ARMED
[423.11] remote: rlc_link: LINK_ACK accepted -> rlc_rfsm: -> IDLE
```

Operator report: continuous urgent alarm (200 ms on / 200 ms off) alongside the
LINK LOST screen for the whole 16 s the base was down, silence on recovery.
This is the exact path that was silent in 1.1.29.

### 2.2 T-30-02 / T-30-03 — audible refusals and the disarm beep

```
[5796.10] remote: rlc_rfsm: ARM rejected: arm switch OFF
[5796.10] remote: rlc_disp: [TOAST] TURN ARM KEY FIRST        + triple beep (MAJ-06)

[5809.11] remote: rlc_rfsm: IDLE -> ARMED (ch 1)              + arm-confirm double beep
[5809.00] base:   rlc_bfsm: IDLE -> ARMED (ch 1, sense verified)
[5813.72] remote: rlc_rfsm: DISARMED -> IDLE                  + long disarm beep (CRIT-01)
```

T-30-02 is the guard that previously showed its message with no sound at all.
T-30-03 exercises a beep issued in the same tick as the ARMED→OFF background
change — the mailbox collision at the heart of CRIT-01.

### 2.3 T-30-06 — cut short by the pad key, and how fast

```
[139.22] base:   rlc_bfsm: PRE_FIRE -> FIRING (ch 1); Fire timer started 1000 ms
[139.82] base:   arm_sense: key switch changed: OFF
[139.82] base:   rlc_bfsm: FIRING -> IDLE (key switch OFF)
[139.97] remote: rlc_rfsm: Pulse cut short at the base after 704 ms (key=0)
[139.97] remote: rlc_disp: [TOAST] CH 1 CUT SHORT - BASE KEY
```

Three things verified at once: the wording names the **pad key** (the `key=0`
variant) rather than blaming the base generally; the message is **"cut short"
rather than "not confirmed"**, which is MAJ-02's evidence gate correctly
allowing the stronger claim because a STATUS_UPDATE had shown the base in
FIRING; and the operator learns within **150 ms** of the key turn instead of
waiting up to 2 s for a status poll.

**Attribution — resolved in §2.7.** The 150 ms here is the base's triggered
STATUS_UPDATE on disarm, not the MAJ-03 NACK path: the capture contains no
`FIRE repeat NACKed` line, and no NACK was sent by the base because the remote
stopped repeating before one could arrive. MAJ-03 was subsequently isolated by
suppressing that frame (T-30-10A/B).

### 2.4 T-30-07 — a genuine completion is still reported as one

```
[101.35] base:   rlc_bfsm: PRE_FIRE -> FIRING (ch 1); Fire timer started 1000 ms
[102.35] base:   rlc_fire: Fire timer stopped
[102.55] base:   rlc_bfsm: FIRING -> POST_FIRE
[102.52] remote: rlc_rfsm: Fire complete (base state=6, 1107 ms)
```

`base state=6` is POST_FIRE, the authoritative completion signal; local elapsed
read 1107 ms against the 1000 ms pulse, consistent with the 1105 ms measured
when the no-slack rule was adopted in 1.1.26. The MAJ-02 evidence gate does not
interfere with the normal path.

### 2.5 T-30-08 — FIRE COMPLETE yields to LINK_LOST

```
[685.04] base:   rlc_bfsm: PRE_FIRE -> FIRING (ch 1)
[686.24] base:   rlc_bfsm: FIRING -> POST_FIRE          <- FIRE COMPLETE screen goes up
[686.84] base:   (console silent — base power pulled)
[690.42] remote: rlc_link: link state 3 -> 4 -> rlc_rfsm: -> LINK_LOST
[698.63] remote: LINK_ACK -> IDLE; [TOAST] BASE STATUS LOST; [TOAST] DATA GAP
```

The link dropped 4.2 s into the 10 s hold and the green success screen was
replaced immediately by LINK LOST with its alarm. The follow-up toasts on
reconnection (11.6 s status outage, then a sequence gap across the base's
restart) are both correct.

### 2.6 T-30-05 — evidence lost, result operator-observed

The test itself (reset the remote, arm inside the 10 s splash hold, expect the
ARMED screen to replace the splash) was reported PASS by the operator. Its
console capture was lost: pressing RST re-enumerates the remote's USB, and the
logging tool held a stale file handle that returned empty reads instead of
erroring, so nothing was recorded. The remote's uptime counter independently
confirms a reset happened at the right moment (uptime restarted ≈14:43), but
the screen behaviour itself has no log support — the display state is not
logged in any case.

Recorded as **PASS (operator-observed)**. The tool has since been fixed to
reopen a port after 20 s without data; a re-run under the fixed tool would still
only add the boot banner and arm timestamps, not the screen contents.

### 2.7 T-30-10A / T-30-10B — MAJ-03 isolated (Group B)

MAJ-03 cannot fire in normal operation, and the reason is in the base:
`firing_exit()` calls `status_update_trigger()` on **every** FIRING exit, and
the PRE_FIRE aborts do the same via `do_disarm()`. The triggered STATUS_UPDATE
therefore always beats the NACK — measured at 150 ms in T-30-06, with no
`FIRE repeat NACKed` line anywhere in that capture. **MAJ-03 is a backstop for
a lost status frame, not the primary detector**, which is a materially narrower
claim than the review's "collapses the MAJ-01/02 windows 10x".

To exercise it the frame has to be removed. Both halves were run with the base's
`s` injection key toggled on late in the countdown by a test director script, so
that the abort's triggered frame was suppressed while the NACK path stayed live.

**T-30-10A — PRE_FIRE half:**

```
[81.30] remote: ARMED -> PRE_FIRE
[84.33] base:   INJECT: STATUS_UPDATE suppression ON        (3 s into the countdown)
[85.14] base:   key switch changed: OFF -> Key switch OFF during PRE_FIRE — abort
[85.14] base:   DISARMED -> IDLE                            (status trigger suppressed)
[85.34] base:   NACK sent: type=0x22 reason=0x05 (WRONG STATE)
[85.30] remote: FIRE repeat NACKed (0x05) during PRE_FIRE
[85.30] remote: [NACK] WRONG STATE -> -> IDLE
```

**160 ms** from key turn to sequence end, with the status frame provably gone.

**T-30-10B — FIRING half** (the state that would otherwise keep asserting
"IGNITION ACTIVE"):

```
[27.24] base:   INJECT: STATUS_UPDATE suppression ON        (1 s before ignition)
[28.04] both:   -> FIRING, fire timer 1000 ms               (FIRING status suppressed)
[28.64] base:   key switch OFF -> FIRING -> IDLE            (600 ms into the pulse)
[28.84] base:   NACK sent: type=0x22 reason=0x05 (WRONG STATE)
[28.84] remote: FIRE repeat NACKed (0x05) — base left the firing path
[28.84] remote: Sequence ended without a FIRING status (base state=4, 793 ms)
[28.84] remote: [TOAST] CH 1 ENDED - NOT CONFIRMED -> -> IDLE
                (reworded to 'CH 1 OUTCOME UNKNOWN - TREAT AS LIVE' in fw 1.1.31)
```

**200 ms** — exactly one repeat interval. `base state=4` is the stale cached
PRE_FIRE, confirming no FIRING status was ever received; that is also why the
MAJ-02 evidence gate correctly declined to claim the channel was energised. The
5 s stale timeout arrived 3.2 s later as a leftover, which is what would have
ended the sequence without this fix.

### 2.8 T-30-09 — status outage across ignition (incidental)

A mistimed first attempt at T-30-10 suppressed status 2 s into the countdown and
held the button through ignition:

```
[66.48] remote: ARMED -> PRE_FIRE
[68.69] base:   INJECT: STATUS_UPDATE suppression ON
[71.49] both:   -> FIRING, fire timer 1000 ms               (ch1 energised)
[71.69] remote: STATUS_UPDATE stale timeout (5047 ms) -> [TOAST] BASE STATUS LOST
[71.69] base:   FIRING -> IDLE (CEASE_FIRE)                 (pulse cut at ~200 ms)
```

This is INF-08 from the review demonstrated on hardware: `STATUS_STALE_TIMEOUT_MS`
and `PRE_FIRE_DELAY_MS` are both 5000 ms, so a status outage beginning mid
countdown expires within ~200 ms of ignition. The outcome is fail-safe — the
remote issues CEASE_FIRE and the pulse is cut — and the operator is told
"BASE STATUS LOST" rather than being left to guess. Recorded as a PASS of the
§8.3 stale-data path.

---

## 3. Coverage Analysis

| Fix | FSD ref | Covered by | Status |
|---|---|---|---|
| CRIT-01 buzzer background never silences a queued pattern | §12.1 | T-30-01, T-30-03 | **COVERED** (link-lost half) / PARTIAL (critical-error half needs injection) |
| MAJ-01 FIRING syncs on any base state off the firing path | §8.2.6 | — | **UNTESTED on target** — needs a truthful-ERROR injection |
| MAJ-02 completion needs positive FIRING evidence | §8.2.6 | T-30-06, T-30-07, T-30-10B | **COVERED** — positive path, cut-short path, and the gate refusing to claim energisation without evidence |
| MAJ-03 NACKs on repeated CMD_FIRE are heeded | §8.4 | T-30-10A (160 ms), T-30-10B (200 ms) | **COVERED** — proven in both PRE_FIRE and FIRING with the status frame suppressed |
| MAJ-04 splash never covers a live state | §10.2.1 | T-30-05 | **COVERED** (operator-observed) |
| MAJ-05 FIRE COMPLETE never covers a live/alarmed state | §10.2.4a | T-30-08 | **COVERED** |
| MAJ-06 refusal paths beep as well as toast | §7.2.9a | T-30-02, T-30-03 | **COVERED** |
| MIN-02 arm-verify two strikes | §7.2.2, §13.1 | T-FSM02 (3 host cases) | **COVERED (host)** |
| MIN-04 NACK 0x0F on a dropped command | §6.3.3 | — | **UNTESTED** — a 16-deep queue overrun is not naturally reachable |
| MIN-07 one-line error brief | §13.2a | Observed live: `[TOAST] BASE ERROR: VBAT CRITICAL` | **COVERED** |
| MIN-10 LINK WEAK in the top bar | §10.2.2 | — | **UNTESTED** — needs ≥30 % ping loss |
| MIN-11 buzzer before boot display check | §9.13 | Boot log ordering (buzzer 978 ms, display 1638 ms) | **COVERED (inspection)** |
| MIN-12 guard-4 wording | §8.2.3, §8.2.4 | Documentation only | **N/A** |

### Incidental confirmations

An unplanned run during setup (the 3S pack pulled while ARMED) exercised the
base's battery-critical path end to end and confirmed several behaviours that
were not on the test list:

```
[373.05] base:   Arm sense LOW during ARMED — relay feedback lost -> DISARMED -> IDLE
[374.65] base:   -> ERROR (flags=0x02: VBAT CRITICAL)      (terminal; survived the pack returning)
[373.05] remote: [TOAST] BASE DISARMED  -> status band: BASE FAULT
[389.07] remote: ARM rejected: base in ERROR (flags=0x02)
[389.07] remote: [TOAST] BASE ERROR: VBAT CRITICAL
```

That last line is **MIN-07** rendering as designed (one flag named, no mid-word
truncation), and the ARM guard 4 refusal working against a base in terminal
ERROR. The base correctly refused to leave ERROR when the pack came back.

---

## 4. Findings

| # | Severity | Category | Description | Test |
|---|---|---|---|---|
| 1 | INFO | Tooling | The session's serial logger held stale file handles across USB re-enumeration, silently recording nothing after a unit reset. It caused one lost test capture and one incorrect "did not happen" conclusion that had to be retracted. Fixed mid-session (reopen after 20 s of silence). | T-30-05 |
| 2 | INFO | Expected noise | `E gptimer: gptimer_stop(418): timer is not running` appears at every `PRE_FIRE -> FIRING`. This is the BF-01 defensive stop-first in `fire_timer_start()` hitting an already-stopped timer; the firmware tolerates `ESP_ERR_INVALID_STATE` by design. The driver logs it at error level, which reads alarmingly in a fire-path log. | T-30-06/07/08 |
| 3 | INFO | Coverage | MAJ-01, CRIT-01's critical-error half and MIN-10 still need fault-injection work; MAJ-01 additionally needs a new injection key, because the existing `e` key deliberately falsifies `base_state` to IDLE and so cannot present a truthful ERROR state to the remote. | §3 |
| 4 | **MINOR — accepted** | Operator information | **MAJ-02's evidence gate under-claims for a fire-button release within ~200 ms of ignition.** Observed live: base entered FIRING at t=244.72 and pushed its triggered status; the operator released at t=244.91 (190 ms later); the frame had not yet been received, so `s_base_reached_firing` was false and the remote reported `CH 1 ENDED - NOT CONFIRMED` for a channel that had genuinely carried current for ~200 ms. Under-claiming is the safe direction relative to the defect MAJ-02 fixed. **Operator decision 2026-08-28: reworded in fw 1.1.31** to `CH n OUTCOME UNKNOWN - TREAT AS LIVE` — see §5. | T-30-10 setup run |
| 5 | INFO | Diagnostics | `send_nack()` does log (`NACK sent: type=0x22 reason=0x05`). Its absence from the T-30-06 capture was not a logging gap: the remote stopped repeating 150 ms after the key turn, so no repeat ever reached the base to be NACKed. | T-30-06 |

| 6 | **MINOR — fixed in 1.1.32** | Correctness / operator information | **Operator-reported base/remote arm desync, not captured.** During Group A testing the operator saw `NOT ARMED - ARM FIRST` on a fire press while the base was armed, followed a moment later by `BASE STATE MISMATCH - DISARMED`. Both messages are diagnostic: the first is emitted *only* from the remote's IDLE fire-press handler, and the second *only* when a STATUS_UPDATE arrives with `channel_armed_bitmask != 0` while the remote is in IDLE. So the base was armed — relay closed, siren running — with the remote unaware and its status band reading READY TO ARM. Not in any capture (the logger had gone stale during that period, see finding 1), and a deliberate re-run on 2026-08-28 did not reproduce it: 59 status samples per unit, no desync window, no mismatch toast. See §4.1. | reported, unreproduced |

Finding 4 is the only firmware-behaviour finding of this test round; it is a
consequence of a deliberate design decision rather than a coding error. It was
put to the operator and **closed by rewording the message in fw 1.1.31** (§5).

### 4.1 Finding 6 — the arm-desync window

Three mechanisms can put the base in ARMED with the remote in IDLE:

1. **Lost ARM ACK.** The base arms and ACKs; the ACK is lost; the remote times
   out and reports `NO RESPONSE FROM BASE`. **This is the only ARM failure
   branch that does not send `CMD_DISARM`** — every sibling does (ACK-after-key-off,
   operator interruption, channel mismatch, key-off retry abort). It is also the
   branch most likely to have left the base armed, since a timeout is exactly
   what a lost ACK looks like.
2. **Remote restart while the base is armed.** The remote boots into IDLE; the
   base stays armed until its `ARM_TIMEOUT_MS` (10 s). Deterministic, and
   plausible for the reported event — the remote was reset repeatedly during
   this session for banner reads and reflashing.
3. **Lost `CMD_DISARM`** from any remote-side disarm, which is also
   fire-and-forget.

All three are bounded by the §8.2.3 reconciliation: the remote's IDLE
STATUS_UPDATE handler broadcasts `DISARM` and toasts `BASE STATE MISMATCH -
DISARMED` within one status interval (≤2 s), with the base's 10 s arm timeout as
the outer backstop. The operator's report — refusal, then mismatch toast "a
moment later" — matches that sequence exactly, so the safety machinery behaved
as designed.

What remains is that **for up to ~2 s the pad is live while the remote's display
says READY TO ARM**, and a fire press in that window is refused with a message
that says the opposite of the pad's true state. Mechanism 2 was offered as a
deterministic reproduction but could not be run (no reset access on the remote
in its enclosure).

**Fixed in fw 1.1.32:** `CMD_DISARM` is now sent on the ARM ACK-timeout branch,
matching every other failure path. It closes mechanism 1 immediately instead of waiting for
reconciliation, and a DISARM to a base that never armed is harmless — §7.2.7
makes it idempotent with an ACK. Mechanisms 2 and 3 remain covered by the
reconciliation only, which is the correct place for them.

---

## 5. Recommendation

**Group A is complete and clean: 8/8 on target, 467/467 on host.** The Critical
fix is confirmed on hardware in the exact condition that was silent before, and
all three live pulses reported their outcome correctly, including the 150 ms
cut-short report.

**MAJ-03 is now fully verified** (§2.7): 160 ms in PRE_FIRE, 200 ms in FIRING,
with the status frame provably suppressed. Its scope is narrower than the review
implied — it is a backstop for a lost frame, since `firing_exit()` always pushes
a status update — but within that scope it works exactly as designed.

Remaining work:

1. **MAJ-01** — add a test-only injection key that puts the base into terminal
   ERROR *without* the `e` key's `base_state` lie, then confirm the remote in
   FIRING reports `BASE ERROR: <flag>` instead of holding "IGNITION ACTIVE".
   This is the last gating item from the review that has no on-target evidence.
2. **CRIT-01 critical-error half** — with the remote injection build, `b`
   (battery critical) and `d` (display fault) from ARMED must both sound
   `ALARM_CRITICAL`.
3. **MIN-10** — `LINK WEAK` needs ≥30 % ping loss; achievable with distance or
   shielding rather than injection.
4. ~~**Finding 4**~~ — **closed 2026-08-28 in fw 1.1.31: the message is now
   `CH n OUTCOME UNKNOWN - TREAT AS LIVE`.** Deferring the classification of a
   release in FIRING was rejected — it adds timing complexity to a safety path
   for the sake of a message, and the evidence rule is deliberately strict. The
   fix is in the wording instead: the remote's epistemic position is unchanged
   (it cannot confirm the outcome), but the operator is now told what that means
   for them at the pad rather than being left to interpret "not confirmed",
   which reads too easily as "nothing happened". Verified to fit the 440 px
   overlay at 36 characters, with a `_Static_assert` guarding the limit.

Both units must be reflashed with normal builds after any injection testing.

**Live fire:** the review's gating findings (CRIT-01, MAJ-01, MAJ-02, MAJ-04)
are fixed and three of the four are confirmed on target. MAJ-01 remains
verified by code and host reasoning only. That is a materially better position
than the review's "no live fire until fixed", but the MAJ-01 injection test is
worth doing before the next live-fire session, since it is the one case where
the remote could still be asserting something about a base nobody has watched
it disagree with.
