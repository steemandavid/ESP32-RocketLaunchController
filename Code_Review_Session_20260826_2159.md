# Code Review — Firmware 1.1.1 → 1.1.7 (session of 2026-08-26)

**Document ID:** RLC-REVIEW-S20260826-001
**Reviewer:** Code Review Agent (self-review — see caveat)
**Date:** 2026-08-26
**Scope:** All firmware changed this session, 1.1.1 → 1.1.7
**FSD Reference:** RLC_Functional_Specification_v1_14.md (v1.41)
**Commit Reviewed:** adee6a6

> **Caveat on independence.** This review covers code written earlier in the same
> session by the same agent. Self-review does not substitute for an independent
> pass, and its blind spots are correlated with the author's. Every claim below
> was re-derived from source rather than from recollection, and the three
> operator-nominated questions were treated as the primary objectives. An
> independent review before live fire would still be worth having.

---

## Verdict: PASS WITH NOTES — one MAJOR to fix before G3

The session's changes are structurally sound and the two most-scrutinised
additions (continuity-loss disarm, no-silent-refusals) are correctly scoped.
**One MAJOR defect was found**: the continuity-loss disarm is edge-triggered
with no level-triggered backstop, so a band transition to OPEN that lands in
the 200 ms arm-verify window is dropped and never re-delivered — leaving the
base ARMED on an open igniter, which is precisely the hazard bug #29 was
created to prevent.

Of the three questions posed, **one found a real defect (Q1)**, one is clean
(Q3), and one is clean with a bounded minor (Q2).

---

## Files Reviewed

| File | Change |
|---|---|
| `rlc_base_fsm.c` | continuity-loss disarm, siren calls, NACK-from-ERROR |
| `rlc_base_main.c` | continuity callback → FSM event |
| `rlc_continuity.c/.h` | callback carries channel + band |
| `rlc_siren.c/.h` | ARMED pulse removed |
| `rlc_status_update.c` | injection hooks |
| `rlc_faultinject.c/.h` | new, test-only |
| `rlc_remote_fsm.c` | ERROR guard, `show_nack()`, ~15 toasts, `fire_is_live()` |
| `rlc_fire_button.c/.h` | state-driven ring LED |
| `rlc_protocol.h` | `NACK_BASE_ERROR` |
| `rlc_fsm_events.h` | `EVT_CONTINUITY_CHANGED` |
| `rlc_config.h` | `PRE_FIRE_DELAY_MS` 2000 → 5000 |

---

## Q1 — Can `armed_channel_went_open()` fire in an unconsidered state?

**No — but the inverse is the defect. It fails to fire in a state that matters.**

The predicate is evaluated in exactly two places, `STATE_ARMED` (:549) and
`STATE_PRE_FIRE` (:579). It cannot be reached from FIRING, POST_FIRE, IDLE,
LINK_LOST, ERROR or BOOT. The FIRING and POST_FIRE exclusions are correct and
were validated on target: during the unplanned ignition the armed channel's
band went OPEN 680 ms into the pulse (relay on NO, sense line disconnected by
design) and the FSM correctly ignored it.

### N1 — MAJOR: edge-triggered disarm with no level-triggered backstop

**`rlc_base_fsm.c`, `STATE_IDLE` case; `rlc_continuity.c:182`**

Three facts combine into a hole:

1. The M1 non-blocking arm verify leaves the FSM in **`STATE_IDLE`** with
   `s_arm_verify_pending` set and **`s_armed_channel` still 0**, for up to
   `ARM_SENSE_VERIFY_TIMEOUT_MS` (200 ms).
2. `STATE_IDLE` has **no `EVT_CONTINUITY_CHANGED` branch** — the event is
   silently discarded there.
3. `continuity_task` posts **only on band change** (`if (new_band !=
   s_bands[current_ch])`). Once a channel reads OPEN, no further event is
   emitted for it.

So: igniter goes OPEN during the verify window → event dropped in IDLE →
verify completes → base enters ARMED with the band **already** OPEN → no
further event will ever be posted → **the base remains ARMED on an open
igniter until the 10 s arm timeout**, with no disarm and no indication.

That is the exact failure bug #29 was created to eliminate, reachable through a
200 ms race. Guard 2 does not help: it passed legitimately before the
disconnection.

**The same hole exists on a second path.** `on_io_change()` posts with a 10 ms
blocking send and merely logs on failure. If the FSM queue is full, the event
is lost — and because the band has already changed, it is never regenerated.
The queue-full case is unlikely but the consequence is identical and silent.

**Probability:** low. It needs the disconnection to occur inside a 200 ms
window *and* the round-robin to sample that channel within it (~25 % of a
800 ms sweep). **Consequence:** the full bug #29 hazard, silently.

**Recommended fix — make it level-triggered on entry to ARMED.** On both ARM
completion paths, re-read the current band rather than waiting for a change:

```c
if (continuity_get_channel(ch) == CONT_OPEN) { /* refuse / disarm */ }
```

A level check on entry closes the verify-window race *and* the dropped-event
case, and is robust to any future missed edge. The event-driven path remains
the fast detector; the level check is the backstop. **An edge-triggered safety
monitor should always have one.**

---

## Q2 — Can NACKing from ERROR interact badly with link sequence handling?

**No sequence-handling hazard. One bounded minor and one honest limitation.**

`send_nack()` from ERROR uses the same `rlc_link_next_seq()` path as every
other NACK. That wrapper (m6) already drops the link rather than emitting a
seq-0 frame on overflow, and `send_nack()` checks the return. All calls are on
the FSM task, so no new concurrency is introduced. Counter exhaustion is not
reachable — 2^32 numbers against a handful of operator-driven attempts.

The FSD explicitly permits transmission from ERROR (§7.2.9 action 3, "Send one
final `STATUS_UPDATE` if possible"), so answering is not a spec violation.

### N2 — MINOR: bounded NACK burst if the base enters ERROR mid-sequence

If the base enters ERROR while the remote's `cmd_fire_repeat_task` is active,
the remote continues sending `CMD_FIRE` every `FIRE_REPEAT_INTERVAL_MS`
(200 ms) and the base now NACKs each one — a 5 Hz NACK stream where previously
there was silence.

Self-limiting: the base keeps sending `STATUS_UPDATE` in ERROR, so the
remote's PRE_FIRE/FIRING handler sees the base has left those states and syncs
to IDLE within one status interval (≤2 s), stopping the repeat. Worst case is
roughly ten extra frames. No action required; recorded so it is not mistaken
for a fault if seen on air.

### N3 — INFO: NACKs to DISARM/CEASE_FIRE are not operator-visible

`send_cmd_disarm()` and `send_cmd_cease_fire()` are fire-and-forget — they do
not call `wait_for_ack()`, so the resulting NACK arrives as an unhandled
`EVT_CMD_NACK` and is discarded. The stated intent ("feedback the remote can
display") is therefore met for **ARM and FIRE only**.

This is defensible — the operator learns the base is in ERROR from the
`STATUS_UPDATE` that drives both the display and the local ERROR guard — but
the asymmetry should be recorded rather than assumed.

### N4 — INFO: spec text vs. behaviour in ERROR

§7.2.9 action 3 says "Send **one final** `STATUS_UPDATE`". The status task has
no ERROR check and continues transmitting every 2 s. This is pre-existing, not
from this session, and is arguably the better behaviour — the remote's ERROR
guard and `show_nack()` enrichment both depend on fresh status while the base
is in ERROR. The spec text should be corrected to match.

---

## Q3 — Do any new `display_toast()` calls block?

**No. Clean.**

`display_toast()` → `overlay_post()` takes `s_req_mutex` with `portMAX_DELAY`,
but the only competing holder — the display task at `rlc_display.c:1009` —
holds it solely for a ~150-byte snapshot copy and releases it **before** any
rendering or SPI transfer. The five other takes (:1256–1318) are equally short
setters. Worst-case blocking is microseconds.

All new calls are on the remote FSM task, in `process_event()` or
`check_timers()`, with no lock held. None is in ISR context.

The `check_timers()` stale-status toast was specifically checked for a 20 Hz
flood: it sits inside the branch that latches `s_last_status_rx_ms = 0` (m3),
so it fires once per stale episode, not once per 50 ms tick.

---

## Other Findings

### N5 — INFO: `fire_is_live()` deliberately duplicates the FIRE guard conditions

`fire_is_live()` re-derives the same two conditions the §8.2.4 guards check
(remote state + fresh status confirming the channel armed). The duplication is
intentional — it makes the ring red exactly when a press would be accepted —
but it is now a two-place invariant. If a FIRE guard is ever added or changed,
`fire_is_live()` must change with it or the ring will lie. Worth a comment
cross-reference at the guard site.

### N6 — INFO: latch initialiser coupling in `fire_button_set_live()`

`s_live` initialises to `false` and `fire_button_init()` drives red off / green
on, so the latch agrees with the hardware. Correct as written, and commented —
but it is an implicit coupling between two files' initial states. If
`fire_button_init()`'s default ever changes, the first transition would be
swallowed.

### Verified correct

- **Siren:** `siren_start_continuous()` clears `s_timer_active` and
  `s_pulse_count`; the N2 stale-callback protection is intact and its remaining
  reachable case (LINK_LOST/ERROR mid-pattern) was validated on target.
- **Continuity callback:** both call sites pass `current_ch + 1`; the ADC
  fail-safe path correctly reports `CONT_OPEN`.
- **`s_armed_channel` lifecycle:** cleared in `do_disarm()`,
  `do_enter_error()`, `do_enter_link_lost()` — `armed_channel_went_open()`
  cannot match spuriously.
- **Fault injection:** whole-file `#if` guard, four independent
  build/run warnings, verified absent from the production build (config and
  ELF symbols both checked).

---

## Summary

| Category | Critical | Major | Minor | Info |
|---|---|---|---|---|
| Spec conformance | 0 | 0 | 0 | 1 (N4) |
| Correctness | 0 | **1** (N1) | 0 | 0 |
| Safety | 0 | **1** (N1, same defect) | 0 | 0 |
| Concurrency | 0 | 0 | 0 | 0 |
| Error handling | 0 | 0 | 1 (N2) | 1 (N3) |
| Code quality | 0 | 0 | 0 | 2 (N5, N6) |

---

## Recommendation

**Fix N1 before G3 fire testing.** It is a hole in the safety feature added
this session, its consequence is the exact hazard that feature addresses, and
the fix is a few lines: a level-triggered continuity re-check on entry to
ARMED. Everything else can be deferred.

N2, N3 and N4 are documentation or observation items. N5 and N6 are
maintainability notes.

The two questions that came back clean (Q2, Q3) are clean on the evidence
above, not by assumption — the mutex hold duration and the sequence allocator
were both read rather than presumed.
