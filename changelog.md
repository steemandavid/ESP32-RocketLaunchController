# ESP32 Rocket Launch Controller — Changelog

## 2026-08-28 (third session) — FINAL release: fw 1.2.0, Phase 5 closed

- **T-A18 closed** (start of session): 68 Ω resistor on ch2, pulled ~1.5 s
  after arming ch1 — ch2 MARG→OPEN, base stayed ARMED through its full
  `ARM TIMEOUT (10022 ms)`, remote silent on the non-armed channel, correct
  `BASE DISARMED` at timeout. **Bug #29 regression suite complete.**
- **Final-build audit — no test harness or fault injection in the release:**
  both injection consoles are wholly `#if CONFIG_*_FAULT_INJECTION`-gated
  (options default n, absent from every sdkconfig, `#warning` + boot banner
  when on); **zero injection symbols in both stock ELFs** by `nm`; the
  display-profile harness was removed at 1.1.11; `CONT_TRACE_INTERVAL_MS`
  is 0 for field builds with its code `#if`-gated; the `rlc-hw-test-*`
  bring-up projects are outside the main build; no leftover inject mark
  files.
- **fw 1.2.0** — version-only bump over 1.1.35 (no code delta), marking the
  Phase 5 release. Host suite 467 checks / 0 failures on both builds. Both
  units flashed with stock 1.2.0, clean hard-reset, **linked on the first
  attempt** (`LINK_REQUEST from remote fw 1.2.0`, both IDLE, rssi −44/−43),
  boot banner `RLC Firmware v1.2.0`, zero injection mentions in either log.
- **Docs sweep:** Development_Progress Phase 5 → COMPLETE (release 1.2.0)
  with the deferred list recorded (T-S10b, T-S12/S13, T-S18, T-C06, range,
  power, remote FSM host harness, CI); version table through 1.2.0; README
  Phase 5 row updated; FSD revision 1.50 records the release and the audit.
- Git tag `v1.2.0`.

## 2026-08-28 (second session) — MAJ-01/CRIT-01 closed on target; two live defects found and fixed; fw 1.1.32 → 1.1.35

Full detail in `Test_Report_Phase5_OnTarget_20260828.md`. Host suite 467
checks / 0 failures at every build; both units left on stock 1.1.35, linked,
injection-free.

- **Cleanups:** FSD "silently ignores" → `LINK_REJECT_BUSY` in three places
  (v1.48); the first-shot-of-a-power-cycle `gptimer_stop` false ERROR
  silenced (fw 1.1.33, verified on target); Development_Progress Phase
  Overview table brought up to date.
- **MAJ-01 verified** with new base injection key `r` (a real
  `EVT_ARM_SENSE_FAULT`, auto-injected 13 ms into a live pulse): base latched
  ERROR mid-FIRING with truthful flags; the remote left FIRING 16 ms later
  showing `BASE ERROR: RELAY FAULT`, band RELAY WELDED. No IGNITION ACTIVE
  over a dead pulse.
- **CRIT-01 verified, both keys audible from ARMED** (`b` battery-critical,
  `d` display fault) — and the `b` run found a real gap: the battery path
  from ARMED entered ERROR **without disarming the base**, which ran its
  full 10 s ARM TIMEOUT while the remote sat terminal. **Fixed in fw
  1.1.35**; re-verified armed → safe in **26 ms**.
- **Bug #29 regression:** T-A16 PASS (base disarm 10 ms, toast 110 ms
  end-to-end); T-A17 PASS after the retest itself found the raw-NACK toast
  defect — a repeat NACK beating the cause-carrying status by 7 ms produced
  `[NACK] WRONG STATE` — **fixed in fw 1.1.34**, §8.4 now forbids showing
  the raw reason for a repeat NACK (FSD v1.49); T-A18 **PASS** later the
  same day (68 Ω on ch2 pulled while ch1 armed — base ran its full 10 s ARM
  TIMEOUT, remote silent on the non-armed channel). **Bug #29 regression
  suite complete; cleared for live fire.**
- **Tooling:** `tools/serial_log.py` — timestamped dual-console logger with
  auto-reopen on USB re-enumeration and `--send-on` auto-injection. Two
  incident notes recorded: a stale port holder makes flashing fail
  *silently* through a pipe (always redirect to a file and check `$?`
  yourself; stop loggers before flashing), and a post-flash boot can come up
  link-wedged — no LINK traffic at all means hard-reset both units.

## 2026-08-28 — On-target test campaign for the review fixes; fw 1.1.30 → 1.1.32

Guided bench testing of everything fixed in 1.1.30, written up in
`Test_Report_Phase5_Review_Fixes.md`. **11 on-target tests, 11 PASS**, host suite
467 checks / 0 failures throughout. Five live pulses into the halogen on ch1.

### The Critical fix, confirmed on hardware

T-30-01: with ch1 armed, cutting base power produced a **continuous audible
alarm** that stopped on recovery — the condition that was completely silent in
1.1.29. `IDLE -> ARMED` at 404.08, `-> LINK_LOST` at 407.09 (originating in
ARMED, which is what made it silent before), `LINK_ACK -> IDLE` at 423.11.

### Group A — no injection needed

| Test | Fix | Result |
|---|---|---|
| Base power cut while ARMED | CRIT-01 | alarm sounds, stops on recovery |
| Long-press with arm switch OFF | MAJ-06 | triple beep + `TURN ARM KEY FIRST` |
| Arm then disarm by encoder | CRIT-01 | long disarm beep survives the state-tone change |
| State tone tempo | 1.1.27 regression | ARMED heartbeat vs ~4 Hz firing tone intact |
| Arm inside the 10 s splash | MAJ-04 | splash gives way to the ARMED screen |
| Base key to SAFE mid-pulse | MAJ-02/03 | `CH 1 CUT SHORT - BASE KEY` **150 ms** after the key turn |
| Clean full pulse | MAJ-02 regression | `Fire complete (base state=6, 1107 ms)` → FIRE COMPLETE |
| Link lost inside FIRE COMPLETE hold | MAJ-05 | green screen cancelled 4.2 s into the hold |

### Group B — MAJ-03 isolated with fault injection

`firing_exit()` calls `status_update_trigger()` on **every** FIRING exit, so the
triggered status frame always beats the NACK in normal operation (150 ms,
measured, with no NACK even sent). **MAJ-03 is a backstop for a lost frame, not
the primary detector** — a narrower claim than the review made for it.

Isolated by suppressing the frame with the base's `s` injection key, driven by a
director script that watched for `ARMED -> PRE_FIRE` and sent the key at a set
offset into the countdown:

- **PRE_FIRE half:** key to SAFE mid-countdown → `FIRE repeat NACKed (0x05)
  during PRE_FIRE` → `[NACK] WRONG STATE`, **160 ms**.
- **FIRING half:** key to SAFE 600 ms into the pulse → `FIRE repeat NACKed
  (0x05) — base left the firing path`, **200 ms** (one repeat interval), with
  `base state=4` proving no FIRING status was ever received.

Base reflashed with a normal build afterwards and verified: injection config
absent, zero injection symbols in `rlc.elf`.

### fw 1.1.31 — unconfirmed-outcome wording

Testing exposed how often MAJ-02's evidence gate lands on the unknown case: the
base pushes its FIRING status on entering the state, so a fire-button release
within ~200 ms of ignition (measured >190 ms) reaches the classifier first. The
channel had carried current for ~200 ms and the remote said "ENDED - NOT
CONFIRMED". Accurate about the remote's knowledge, wrong about the operator's
next move. Now **`CH n OUTCOME UNKNOWN - TREAT AS LIVE`** at all three sites,
with a `_Static_assert` pinning the 36-character overlay limit. Wording only —
the gate is untouched.

### fw 1.1.32 — a failed ARM undoes itself

Operator hit `NOT ARMED - ARM FIRST` on a fire press while the base was armed,
followed by `BASE STATE MISMATCH - DISARMED`. Both messages are diagnostic, so
the base really was armed with the remote unaware. Reviewing the ARM failure
branches found the asymmetry: **the ACK-timeout branch was the only one that did
not send `CMD_DISARM`** — and a timeout is exactly what a lost ACK looks like,
so it was the branch most likely to have left the base armed. It now disarms
like its siblings. A deliberate re-run did not reproduce the original event (59
status samples per unit, no desync window), so it is recorded as finding 6 with
the three candidate mechanisms, all bounded by the §8.2.3 reconciliation.

### Flashing trap worth knowing

Two no-link episodes after back-to-back flashing turned out to be the version
check working as designed: the first-flashed unit reboots on the new firmware,
handshakes with its still-stale peer, and **latches `VERSION_MISMATCH`**, which
stops `LINK_REQUEST` entirely ("stuck until power cycle"). The remote showed the
FIRMWARE MISMATCH screen the whole time. Changing flash order only moves the
latch to the other unit. **Reset the first-flashed unit after the second
finishes** — a DTR/RTS pulse over the serial adapter is enough. Documented in
the README. Whether a latched mismatch should retry and self-clear once the peer
matches is left as an open spec question.

### Tooling note

The session's serial logger held stale file handles across USB re-enumeration,
silently recording nothing after a unit reset. It cost one test capture (T-30-05)
and produced one incorrect "this did not happen" conclusion that had to be
retracted. Fixed to reopen a port after 20 s without data.

### Still open

- **MAJ-01** — the remote in FIRING over a base in terminal ERROR. Needs a new
  test-only injection key: the existing `e` deliberately falsifies `base_state`
  to IDLE, so it cannot present a truthful ERROR.
- **CRIT-01's critical-error half** (remote `b`/`d` from ARMED) and **MIN-10**
  (`LINK WEAK`, needs ≥30 % ping loss).

## 2026-08-28 — Review RLC-REVIEW-ALL-009 fixed: fw 1.1.29 → 1.1.30

All seven Critical/Major findings of `Code_Review_Phase5_20260828_0641.md`
fixed, plus ten minors and three info items. Every defect this round was in the
operator-information layer on the fire path: nothing energised a relay or
extended a pulse, but the remote could be silent about a link loss while armed,
assert an ignition over a base in terminal ERROR, or certify a shot that never
happened.

- **CRIT-01** `buzzer_set_background()`'s `BUZZER_OFF` nudge atomically
  overwrote the one-deep pattern mailbox, destroying any alarm or beep queued
  in the same FSM tick. Link-lost and critical alarms were silent whenever the
  transition started in ARMED/PRE_FIRE/FIRING, as was every FIRE-guard refusal.
  The player task now polls the background (≤20 ms slices, 100 ms idle wait)
  instead of being nudged through the mailbox.
- **MAJ-01** remote FIRING now syncs on *any* base state off the firing path,
  not just POST_FIRE/IDLE; ERROR and LINK_LOST are named as base faults.
- **MAJ-02** FIRE COMPLETE and "cut short" require a STATUS_UPDATE that
  actually showed the base in FIRING; otherwise the outcome is reported as
  `CH n ENDED - NOT CONFIRMED`.
- **MAJ-03** NACKs answering the repeated CMD_FIREs are acted on (~200 ms)
  instead of discarded (up to 2 s).
- **MAJ-04/05** the boot splash and the FIRE COMPLETE hold no longer cover a
  live (ARMED/PRE_FIRE/FIRING) or alarmed (LINK_LOST) state.
- **MAJ-06** the three refusal paths with a message but no beep now beep.
- Minors: buzzer up before the boot display check (MIN-11); FIRE ACK channel
  mismatch (MIN-05) and key-off ARM abort (MIN-09) named instead of blamed on
  the link; first-ARM-wins in the verify window (MIN-01); `EVT_LINK_RECOVERED`
  handled in base FIRING (MIN-03); `rlc_error_flags_brief()` for one-line
  toasts with host test T-E08 (MIN-07); no glyph for an unknown igniter band
  (MIN-08); `LINK WEAK` in the top bar (MIN-10); 10 ms blocking posts for
  remote input events (MIN-06); `volatile s_channel` (INF-05).
- Docs: FSD v1.47 (§6.3.3, §7.2.2, §8.2.3, §8.2.4, §8.2.6, §8.4, §10.2.1,
  §10.2.2, §10.2.4a, §12.1, §13.1, §14.1 + revision row), Development_Progress
  entry and firmware-version table brought up to date through 1.1.30 (INF-12),
  README (FSD version reference, review row, Phase 5 status, and why a state
  tone can never silence an alarm).

The three minors that needed an operator decision were settled the same day and
are in 1.1.30 as well:

- **MIN-02** arm-verify timeout is now **two strikes**: first timeout NACKs 0x0B
  and stays IDLE (retryable — ~40 ms of margin over the sense debounce means a
  slow relay is not necessarily a broken one), second consecutive timeout latches
  `ERR_RELAY_FAULT` and enters terminal ERROR. Cleared by any successful verify;
  weld detection stays terminal on sight. New `ARM_VERIFY_FAULT_STRIKES`, and
  three new T-FSM02 cases pin it.
- **MIN-04** a command dropped by a full FSM queue is answered with a new
  **NACK 0x0F `BASE_BUSY`** instead of only a log line. The remote ignores that
  reason for repeated CMD_FIRE — a refused frame is not the base leaving the
  firing path, and ending a live pulse on one would be a false abort.
- **MIN-12** documentation only: §8.2.3/§8.2.4 now state the ping-failure *rate*
  test the firmware implements, and record that ignition is gated by the base's
  dead-man and contact-freshness guards (§7.2.4), not by this one.

### Flashed and verified on target

Both units built and flashed at the end of the session (host suite gates every
firmware build: **467 checks, 0 failures**, base FSM harness 120 of them — up
from 111 with the new MIN-02 cases).

| Unit | by-id (board serial, survives chip swaps) | MAC | Result |
|---|---|---|---|
| Base (chip #4) | `usb-1a86_USB_Single_Serial_5B5E042156-if00` | `44:1b:f6:81:f1:70` | `=== RLC Base Unit v1.1.30 ===`, `BOOT -> IDLE` |
| Remote | `usb-1a86_USB_Single_Serial_5B5E043219-if00` | `ac:a7:04:e2:f2:8c` | `=== RLC Remote Unit v1.1.30 ===`, `LINKING -> IDLE` |

MACs were read with `esptool read_mac` before flashing (note: this esptool build
uses the underscore spellings, `read_mac` / `write_flash`, not the hyphenated
ones). Link verified from the base log: `LINK_REQUEST from remote fw 1.1.30` →
`LINK_ACK sent` → `link state 2 -> 3`, RSSI −38. The MIN-11 ordering change is
visible in the remote boot log — buzzer up at 978 ms, display task at 1638 ms.

Expected bench artefact, not a regression: the base latched
`ERROR flags=0x02 VBAT CRITICAL` 4.3 s after boot with `vbat=0 mv`, because the
3S pack was not connected and it was running on USB alone. The pack must be on
for any functional testing.

### Still to do on target

The audible half of the review is fixed but **not yet heard**. From ARMED, with
the pack connected: kill the link, drive the battery critical, force a display
fault, and walk each FIRE-guard refusal — every one must now beep as well as
toast (that is the CRIT-01 acceptance test). MAJ-01/02 want the fault-injection
build to drive the base into ERROR mid-pulse and to drop the abort STATUS_UPDATE.

## 2026-08-28 — Full code review RLC-REVIEW-ALL-009: verdict MAYBE (1 Critical, 6 Major)

Read-only review of the whole codebase at `a101077` (fw 1.1.29), focused on
the arm-fire sequence, error handling, and toast/status screens — the Phase 5
delta since RLC-REVIEW-ALL-008 (fw 1.1.8). Written to
`Code_Review_Phase5_20260828_0641.md`. No source files were touched.

### Prior review's findings — all re-verified fixed

BF-01 (fire timer, Critical) intact across all three layers; CM-01 mutex,
DS-01 runtime display health check, TT-04 host FSM harness, CM-02/04/05,
RM-05/06, BF-02/03 all confirmed in place. The fw 1.1.29 asymmetric debounce
was traced through every layer (debounce → fresh-press → repeat task → base
dead-man) and is correct; mash closes at the event layer.

### New findings — the operator-information layer

Nothing found this round extends a pulse or energizes a relay. What broke is
what the operator is told:

1. **CRIT-01** — `buzzer_set_background()`'s `BUZZER_OFF` nudge is an
   `xQueueOverwrite` on the depth-1 mailbox, so it atomically deletes any
   alarm queued in the same FSM tick. The link-lost and critical-error alarms
   are **completely silent when the transition originates in ARMED/PRE_FIRE/
   FIRING**, as are every FIRE-guard refusal beep, the disarm BEEP_LONG, the
   arm-confirm double, and the FIRING "PULSE CUT SHORT" triples. §7.2.9a's
   audible half fails on the highest-hazard paths; T-A20 passed because its
   cases originate in IDLE where the idempotence guard saves the alarm.
2. **MAJ-01** — remote FIRING only syncs on base POST_FIRE/IDLE (whitelist);
   a base that enters ERROR mid-pulse (weld fault) leaves the remote showing
   IGNITION ACTIVE + firing tone + 5 Hz repeats indefinitely; button release
   then says "PULSE CUT SHORT" about a base needing a power cycle.
3. **MAJ-02** — false FIRE COMPLETE: base aborts during PRE_FIRE (never
   energizes) + one lost STATUS_UPDATE ⇒ remote's local-elapsed backstop
   (`fired_ms ≥ 1000` from *local* FIRING entry) shows the 10 s green screen
   for a channel that never carried current. cf797c0 closed the during-pulse
   case; the before-energization case is still open.
4. **MAJ-03** — root amplifier: the base NACKs stray CMD_FIRE repeats within
   ~200 ms of leaving the firing path (WRONG_STATE from IDLE, BASE_ERROR from
   ERROR) but the remote discards EVT_CMD_NACK outside `wait_for_ack()`, so
   abort detection waits for the 2 s status cadence.
5. **MAJ-04/05** — display precedence: the 10 s splash hold outranks the
   ARMED/FIRING screens (splash over a live armed pad for up to ~9 s), and
   FIRE COMPLETE outranks LINK_LOST during its hold (green screen over a
   declared-dead link).
6. **MAJ-06** — three refusal paths have a toast but no buzzer at all
   (arm-guard-1 key-off, ARM −4 "CANCELLED", PRE_FIRE "BASE ENDED SEQUENCE"),
   independent of CRIT-01.

Plus 12 Minor (double-CMD_ARM in the verify window; arm-verify timeout
doesn't latch ERR_RELAY_FAULT though §7.2.2 says to — FSD self-contradiction;
FIRE −2 mislabeled "NO RESPONSE"; arm-retry key-off misattributed to the
base; "LINK OK" bar while degraded; boot display-fault has no buzzer;
error-toast truncation; IGNITER ? draws an OPEN glyph; …) and 12 Info.

### Recommendation in the review

Conditional GO: bench/fault-injection work can continue now; fix CRIT-01 +
MAJ-01/02/04 and reflash both units before the next live-fire session. All
four are small, localized fixes. MAJ-03 (heed the NACKs) collapses the
MAJ-01/02 windows 10× and is worth doing in the same pass. MIN-03
(EVT_LINK_RECOVERED unhandled in FIRING) becomes mandatory before any
`FIRE_PULSE_DURATION_MS` increase past ~1.5 s.

Docs to sweep: FSD §15.4 T-S09 row still says "silently ignores";
Development_Progress firmware-version history table still ends at 1.1.9;
DOC-03/04/TT-01 (hw-test-spec GPIO/by-id leftovers) not re-verified this
round.

## 2026-08-27 — Phase 4 verified, bug #20 closed, firmware 1.1.11 → 1.1.27

A long session. Phase 4's display suite run and its one failure fixed, the
bug #31 gating restriction lifted, bug #20 (public crypto keys) closed, five
§15.4 safety tests passed, and a run of operator-reported display and audio
gaps fixed. Twelve commits, all pushed.

### Phase 4 display tests — T-D01…T-D09

Ran the whole suite on target. **8 PASS, 1 FAIL** initially; T-D09 fixed the
same day, final **9/9**. Write-up in `Test_Report_Phase4_Display.md`.

T-D09 asks for two numbers nothing measured, so a `CONFIG_RLC_DISPLAY_PROFILE`
harness was added, used, and removed again in 1.1.11. **The figures below
cannot be reproduced on a stock build** — recover the harness from git history
at 1.1.10 to re-measure.

| Metric | Before | After |
|---|---|---|
| Steady frame period | 300 ms (3.3 Hz) | **100.00 ms (10.0 Hz)** |
| Period during PRE_FIRE | 301 ms | 101 ms |
| Render + flush | ~195 ms | 33 ms avg |
| Pixels sent per frame | ~153600 (whole panel) | ~1200 worst frame |
| Full-panel redraw, worst case | 232 ms | 250 ms |

**Three causes, only the first of which the code review had found:**

1. One dirty bounding box, unioned from the top bar at y=0 to the instruction
   line at y=DH-30 — effectively the whole panel.
2. `draw_field()` repaints every field every frame regardless of whether its
   text changed, so the pixels genuinely *were* all being rewritten. A rect
   list alone would not have helped.
3. `vTaskDelay` ran **after** the frame's work, making the period `work+100 ms`.
   100 ms was unreachable even with an instantaneous flush.

`flush()` now diffs the dirty box row by row against a shadow copy of what the
panel was last sent and transmits only changed spans; the loop is paced with
`xTaskDelayUntil`. Diffing rather than per-field invalidation is deliberate: a
missed invalidation leaves a stale pixel, and this display shows ARMED.

**A measurement lesson.** The first profiling build sampled one frame in twenty
and reported `0 px` flushed for 30 s on a screen that *was* updating.
Point-sampling could not distinguish a perfectly efficient display from a
frozen one. The profiling was changed to accumulate over the whole window
before the result was believed.

T-D08 was re-run rather than assumed to carry over, since the flush mechanism
was replaced underneath it. It turned out the most informative run of the set:
MAIN ↔ LINK_LOST changes *every* pixel, and both transitions flushed 153600 px
in a single run — so the diff detects a complete change, coalesces it rather
than fragmenting it, and drops no rows.

### Bug #31 two-cycle regression — PASSED, restriction lifted

The gating item for the one-launch-per-power-cycle caution.

**It did not need pyrotechnics.** `POST_FIRE → IDLE` is a pure
`POST_FIRE_COOLDOWN_MS` timer with no continuity condition, so the second arm is
available whether or not the load burned through — the defect was in the timer,
not the igniter. Run into a **12 V 50 W halogen** on ch 1 (~4.2 A hot against a
20 A contact rating; switching a 12 V halogen is the duty these automotive
relays were designed for), which also confirms each pulse visibly, something an
igniter cannot do because it only fires once.

```
cycle 1  467644 ARMED  472764 PRE_FIRE  477764 FIRING  478814 timer stopped
         478834 POST_FIRE  480874 IDLE
cycle 2  527304 ARMED  528164 PRE_FIRE  533164 FIRING  534214 timer stopped
         534234 POST_FIRE  536274 IDLE
```

0 reboots, 0 panics, 0 watchdog events over a 9.6 minute capture. Cycles
timing-identical — that symmetry is the substance, since behaving the second
time exactly as the first is what the bug prevented.

Later, T-F03 showed `Fire timer stopped` on the **cease-fire** path too, so both
exits from FIRING release the timer. Three pulses on one power cycle across that
capture, exceeding the two-cycle requirement.

### Bug #20 — crypto keys rotated and removed from the repo

PMK, LMK and the CRC integrity key were literal ASCII placeholders
(`RLC_PMK_DEFAULT!`) committed to a **public** repository — guessable without
even reading the source. Two of three link-security layers offered nothing.

- Keys moved to `components/rlc_common/include/rlc_secrets.h` — gitignored,
  mode 600, generated by `./tools/gen-secrets.sh` from `/dev/urandom`.
- `rlc_config.h` has **no fallback**: a build without real keys fails with an
  instructive `#error`. A silent fallback is how the placeholders shipped.
- **Leak prevention enforced, not intended.** `.gitignore` is bypassed by
  `git add -f`, so `tools/git-hooks/pre-commit` refuses any commit staging the
  secrets file under any path, or defining a key macro with non-zero bytes in
  any file. Enable with `git config core.hooksPath tools/git-hooks`. Both leak
  routes were attempted and refused.
- Verified by the **integrity CRC self-test value**, which derives from the key:
  `0xE74979F0` → `0x45222AE8`, identical on both units.

**The old keys remain permanently public** — in history across many commits on
a public repo, likely already cloned and cached. Rotation does not un-publish
them, it makes them irrelevant. Never reuse those values.

### Build scripts silently swallowed flash failures

Found while doing the above, and it had already misled the session. esptool was
piped into `tail -3`, so the pipeline's exit status was tail's: **a failed flash
exited 0 with nothing alarming printed.** A serial-contention error (leftover
capture processes holding the ports) left BOTH units on the previous firmware
while the scripts reported success. Caught only by reading version banners off
the devices.

Both scripts now capture the output, check the status, and print
`*** FLASH FAILED — <UNIT> IS STILL RUNNING ITS PREVIOUS FIRMWARE ***`
before exiting 1. It fired for real twice more the same session.

**A reasoning error worth recording:** having seen a missing "Done." and noticed
it was suspicious, the conclusion drawn was that a successful *link* proved the
new keys were live. It did not — two units both running stale firmware link
perfectly well. A link proves the units agree with each other, not what they
agree on.

### §15.4 safety tests — 3/19 → 8/19, no igniters

| | Result |
|---|---|
| T-S01 | **PASS** — base power-cycled while armed: boots IDLE with key still in ARM, no auto-rearm, relays NC, no lamp flash through boot. Latched `ERR_VBAT_CRITICAL` at **7287 mV** on the way down — correct, and a third pass of T-S03 |
| T-S02 | **PASS** — `PING drought (1542 ms)` → arm sense DISARMED 170 ms later; last PING to fire-path-dead **1712 ms**. The 42 ms over 1500 is link-task tick granularity, recorded as measured |
| T-S04 | **PASS** — button held through remote boot and arming: no fire |
| T-S08 | **PASS** — button held into ARMED entry: no fire; release-then-press did start the countdown |
| T-S17 | **PASS** — key sense both directions, guard 1 passes, arm verified by sense 170 ms after relay drive; auto-disarm at exactly 10000 ms (re-confirms T-S14) |

**T-S09 is not reachable on target as written.** `tick_remote()` only sends
LINK_REQUEST in LINKING or LOST, so a linked remote never sends one, and
rebooting it to force one takes ~1.9 s — by which time the base has hit link
loss at 1.5 s and disarmed. Needs a harness key. Its FSD row also still says
"silently ignores", which our own LINK_REJECT change made false.

### §15.3 — T-F03 and T-F08 PASS, neither needed ignition

T-F03 was produced **by accident** (early release on the first attempt), which
is better than a planned one because it landed mid-pulse rather than at a
rehearsed moment: pulse cut at **540 ms** of 1000, `FIRING -> IDLE
(CEASE_FIRE)`. Note the exit is to IDLE, not POST_FIRE — the FSM distinguishes
ceased from completed.

T-F08 recorded as **PASS by log timing** (1050 ms against a 1000 ms constant),
method stated: the stop line is written after the callback runs, so it bounds
the pulse at 1000–1050 ms and cannot resolve better. No scope was used.

**T-F01 is now the only fire test needing a real igniter.**

### System status band (1.1.12 → 1.1.15, 1.1.21)

A coloured field across the bottom of the screen reporting the state of the fire
path, legible from across a launch site.

| Band | State |
|---|---|
| Green `SAFE` | base safe, remote arm switch off |
| Yellow `BASE KEY ARMED` / `REMOTE ARMED` | one key turned |
| Orange `READY TO ARM` | both keys — one long-press from a live relay |
| Red `ARM RELAY LIVE` | arm relay engaged |
| Flashing `RELAY WELDED` | contacts closed when they should not be |
| Red `BASE FAULT` / `REMOTE FAULT` | a unit has faulted |
| Grey `STATUS UNKNOWN` | not known |

All seven non-nominal states verified on target using the base `w`/`e`/`s`
injections, a deliberately mismatched base build, and a new remote harness.

Requested as a full-screen border; built as a bottom band because the channel
grid fills the panel width exactly (`_Static_assert`) and a border would have
had to shrink the cells.

**Defects found while building it:**

- `draw_text_centred_bg()` cleared the **full panel width** before writing, so
  every refresh of a live value notched the left and right edges of whatever
  frame the text sat in. Operator-reported on LINK LOST; it was also breaking
  the ARMED/FIRING/FIRE COMPLETE box outlines.
- **Band showed green with the base switched off.** Link loss trips at 1500 ms
  but status is only stale at 4000 ms, so for 2.5 s it rendered the last state
  received before power was cut. Now gated on link state; verified grey 10 ms
  after link loss.
- **False `RELAY WELDED` on every normal disarm**, measured at 180 and 220 ms.
  On ARMED → IDLE the base reports IDLE before the arm sense falls, which is
  exactly the weld condition. Pre-existing — the main screen's BASE field had
  been doing it too — but the band made it a full-width flash. A weld must now
  hold **500 ms**; during the window the state reports as ARMED, never anything
  safer. Hysteresis is in the display, not in the pure, host-tested
  `rlc_base_arm_state()`.
- The instruction line tested **only** the remote arm switch, so with the base
  key still SAFE it read "HOLD ENCODER TO ARM" — an instruction the base
  refuses.
- **Two colours clearly separated in the source were identical on the panel**
  (`0xFFDC00` vs `0xFF6000`, both read as orange). Pushed to `0xFFFF00` vs
  `0xFF5000`, and the distinction is now carried by wording as well as hue.
  *Separation in the constants is not separation on the glass.*

**Scoped back in 1.1.21** after an operator report that it covered the splash
progress bar and the LINK LOST reconnect text. Removed from SPLASH, LINK_LOST
and FW_MISMATCH; on the latter two not as a judgement call — `system_status()`
gates on link state, so on both the band could only ever return grey. A field
that can show exactly one value carries no information.

### Handshake refusals are no longer silent (1.1.17, 1.1.18)

**Protocol change: `MSG_LINK_REJECT` (0x03).**

`handle_link_request()` refused a handshake with a bare `return` on two paths —
firmware mismatch, and the app-state guard when the base is armed. The remote
cannot tell a refusal from a base that is off or out of range, so it retried
every 2 s forever behind a splash frozen at "Attempt 5 / 5" — which reads as a
hung boot. The base knew exactly what was wrong and said so only on its own LED
strip, at the pad.

The §10.2.1 mismatch screen had been **unreachable**: the remote's own check
reads the version out of a LINK_ACK the base never sent on a mismatch. The
base-side check added in 5.7 to make mismatches *clearer* is what pre-empted it.

1.1.18 added the old-firmware fallback: on a mismatch the base also sends a
LINK_ACK carrying its version with the session token zeroed, so a remote with no
LINK_REJECT handler still latches VERSION_MISMATCH. Verified with a simulated
pre-1.1.17 remote.

### Arming sequence enforced (1.1.19)

Raised by the operator during T-S04/T-S08. Both tests passed — a held button
cannot fire — but the refusals were **silent**.

| Trigger | Now |
|---|---|
| Fire pressed in IDLE | `NOT ARMED - ARM FIRST` |
| ARM attempted with fire held | **ARM refused** + `RELEASE FIRE BUTTON FIRST` |
| Arm switch ON with fire/encoder held | `RELEASE FIRE BUTTON FIRST` / `RELEASE ENCODER FIRST` |

The arm switch is deliberately **not** forced off on a bad sequence — it is a
physical switch the firmware cannot move, and pretending otherwise would put the
display out of step with the panel.

### FIRE COMPLETE screen (1.1.22 → 1.1.26)

- Duration decoupled from `POST_FIRE_COOLDOWN_MS` into display-only
  `FIRE_COMPLETE_SCREEN_MS`, then 2 s → 5 s → **10 s**. The shared constant is a
  fire-path parameter; raising it would have extended the base's cooldown as a
  side effect.
- Decoupling opened a hazard, closed in the same change: the screen is now
  **cancelled the instant the FSM enters ARMED/PRE_FIRE/FIRING**, because a
  summary of the last shot must never cover a live pad.
- Countdown relabelled `CLEARS IN` — after ~2 s the base really is IDLE, so an
  "IDLE IN" countdown would state something untrue.
- **Live igniter status** (operator-facing half of T-S19): `○ OPEN - LIKELY
  FIRED` / `▲ MARGINAL - CHECK` / `● STILL CONNECTED` / `IGNITER ?`, refreshed
  every frame. Says LIKELY because OPEN cannot distinguish a burned igniter from
  a lead that fell off.

**The serious one (1.1.25/1.1.26).** Investigating a missing cease-fire toast
found the remote **announcing FIRE COMPLETE for a pulse the base had cut
short** — captured at 550 ms of 1000 with the base key turned off. A completed
pulse runs FIRING → POST_FIRE → IDLE; a base-side cut goes FIRING → IDLE
directly, and both were seen as `base_state == IDLE`.

Fixed by timestamping the remote's own FIRING entry. **1.1.25's first attempt
was still wrong** — 200 ms of clock-skew slack meant a key turn at 802 ms landed
2 ms inside the threshold and still read as complete. Retest caught it. The
slack is gone, and the skew it guarded against was unfounded in the wrong
direction (a real completion measured 1105 ms, over not under). POST_FIRE is
authoritative anyway: `rlc_base_fsm.c` calls `status_update_trigger()` on
entering it. Final margin: 655 ms vs 1055 ms against a 1000 ms threshold.

### Cease-fire and state tones (1.1.24, 1.1.27)

Cease-fire returned to IDLE silently, losing the fact that matters most when
someone then walks to the rail: **the channel was energised, just briefly.**
Indistinguishable from a countdown abort where no current ever flowed.

| Trigger | Toast |
|---|---|
| Fire button released | `CH n PULSE CUT SHORT` |
| Arm switch off during firing | `CH n CUT SHORT - ARM OFF` |
| Base cut the pulse | `CH n CUT SHORT - BASE KEY` / `AT BASE` |

Not a §7.2.9a violation — that covers refusals, aborts and failures, and a
cease-fire is a *successful* operator action. The v1.39 audit finding "only five
log-without-display sites, all legitimate" was correct by its own terms. The gap
was in a neighbouring category: **operator-initiated state changes whose
consequences the operator needs to know about.** The same blind spot may exist
elsewhere and would not be caught by re-running that audit.

**State tones (1.1.27):** ARMED and FIRING had no sound at all on the remote.

| Phase | Pattern | Rate |
|---|---|---|
| ARMED | 80 on / 1120 off | ~0.8 Hz heartbeat |
| PRE_FIRE + FIRING | 90 on / 160 off | ~4 Hz |

The tempo gap is the point. ARMED is deliberately sparse: both pre-existing
repeating alarms are ~2.5 Hz *fault* patterns, so an urgent armed tone would
read as "something is wrong". Needed a new mechanism — `buzzer_set_background()`
— because ARMED is full of one-shots (arm-confirm double, a triple from every
FIRE guard refusal) and a plain repeating tone is replaced by the first of them
and never returns. Driven from the FSM tick, not transitions, for the same
reason `fire_button_set_live()` is.

### All eight channels fired — halogen substitute

Channels 2–8 had never been fired. Run with one 12 V 50 W halogen moved channel
to channel, fw 1.1.27. Every arm **sense-verified**, every `Fire timer started`
naming the selected channel, all reaching `POST_FIRE → IDLE`.

**9 pulses on one power cycle**, 0 reboots, 0 panics, 0 watchdog events, uptime
continuous 331584 → 582104 ms, battery essentially unchanged (11473 → 11464 mV).

**The channel-to-relay mapping is proven, not assumed.** Only the channel
carrying the lamp reads CONNECTED, and arming requires continuity — so the lamp
lighting on the selected channel is end-to-end proof of the mapping for all
eight. A crossed relay would have shown as nothing lighting.

Channel 8's first attempt was an **early release**, not a fault: `FIRING → IDLE
(CEASE_FIRE)` at 470 ms with the remote's `type=0x23` ACK. Under 1.1.24+ that
toasts `CH 8 PULSE CUT SHORT`, so it was visible at the time. Re-armed and fired
cleanly.

Incidentally **the strongest bug #31 evidence yet** — Phase 5 task 10 asked for
two cycles per power-on; this did nine, mixing completed pulses with a
cease-fire, on one boot.

**Channel 3's MARGINAL reading was a 68 Ω igniter surrogate**, not a fault.
Back-calculating `1123/269000/MARG` through `V = 3.3*Rx/(3300+Rx)` and
subtracting the 217 Ω sense-branch resistor gives ~76 Ω — within ~12% of the
real part, so the divider model is sound and the reading was correct; the
*interpretation* (a leakage path) was wrong. Recorded for the band behaviour it
shows: **MARGINAL does not block arming — only OPEN does**, so a 68 Ω channel
arms, fires and reports success while delivering ~160 mA. A real igniter's
1.5 Ω dominates and reads CONNECTED, so this only bites when the igniter is
missing.

**T-S19 is PASS** — burn-through was verified with a real igniter during earlier
fire testing (operator attestation; T-A17 corroborates igniters firing on this
rig, though no post-fire continuity reading was logged then). The green
`OPEN - LIKELY FIRED` path has not been seen on the *panel*, since the display
half postdates that testing.

**T-F01 does not need an igniter.** Its FSD criteria are sequence mechanics —
siren continuous across both transitions, relay energised for the pulse
duration, auto-disarm to NC. The halogen run discharged the latter two ×8. The
outstanding piece is the **siren no-gap check**, which is audible.

### §15.3 complete, §15.4 to 13/19 — and T-F01 needed no igniter

**T-F01 PASS.** Its FSD criteria turned out to be *sequence mechanics*, not
pyrotechnics: siren continuous across both transitions, relay energised for
`FIRE_PULSE_DURATION_MS`, auto-disarm to NC. The halogen run had already
discharged the latter two on all 8 channels; the operator confirmed the siren
unbroken through `PRE_FIRE → FIRING`, the one transition never measured.

**Bug #27's row was stale and I quoted it repeatedly.** It said the siren
retests were owed and N2 was code-inspection-only. They ran on 2026-08-26 — six
checks, all PASS — and both the README and this changelog recorded that
correctly. Only the `Development_Progress.md` bug row was never updated. It also
meant `ARMED → PRE_FIRE` was *already* measured gapless, narrowing T-F01 to a
single transition. Fixed.

**Five fault-injection keys added** for tests unreachable from outside the
firmware. All Kconfig-guarded, so the stock binaries are unchanged.

| Key | Console | Test |
|---|---|---|
| `g` | base | T-S15 / T-S16 — force `rlc_link_is_healthy()` false |
| `x` | base | T-S07 — hang the FSM task without feeding the TWDT |
| `c` | remote | T-S05 — corrupt the next outgoing command |
| `l` | remote | T-S09 — LINK_REQUEST while linked |

**`c` was first wired to the wrong unit.** The commit message said it corrupts a
*command* "because the command path is the one with a guard worth testing" —
then put the key on the base, which only ever sends ACK/NACK. Caught by checking
the callers before running the test rather than after.

**T-S15 PASS** — `NACK 0x0D (COMM DEGRADED)`, arm refused by guard 10.

**T-S16 PASS** — the important one: the only guard that can stop a pulse
*already in progress*, never previously exercised on hardware. The injection was
fired **automatically** on the base logging `ARMED -> PRE_FIRE`, landing 40 ms
into the countdown:

```
327358  ARMED -> PRE_FIRE (ch 8)
332348  PRE_FIRE comm degraded — abort        <- guard 4
332378  DISARMED -> IDLE
```

Zero `Fire timer started`. **The first T-S16 run gave the right outcome but not
the proof** — its capture dropped most of its lines including the guard's
message, leaving the aborting guard identified only by elimination. Re-run to
capture it, because that is thin evidence for a fire-path interlock.

**T-S05 PASS** — `CMD integrity CRC mismatch (type 0x20)`, rejected in the link
layer with the sequence number deliberately not advanced. The operator saw
`COMM DEGRADED` because the remote's automatic retry (uncorrupted — the
injection is one-shot) landed on a `g` flag I had left set from T-S16. Setup
error, not a test failure.

**T-S09 PASS**, proven by absence: `LINK_REQUEST rejected by app-state guard
(busy)` → `LINK_REJECT sent, reason=0x02`, then no LINK_ACK, no new session
token, no dropped arm — the base stayed ARMED through its full 10 s timeout. A
handshake attempt cannot reset the session out from under a live pad. Its FSD
row's "silently ignores" wording predated 1.1.17 and our own change made it
false; corrected.

**T-S07 PASS** — 4250 ms from hang to `rst:0xc (RTC_SW_CPU_RST)`, post-boot
`arm=0` with continuity reading on all channels (which requires the channel
relays in NC).

**The watchdog output names the wrong task.** It blamed `battery_task` — the
*victim*, starved of CPU 0 by the spinning `bfsm_task`, which appears only on
the "Tasks currently running" line. A real trip in the field will look identical
and anyone chasing `battery_task` would debug the wrong thing. Inherent to the
TWDT's reporting, not a defect.

### The last three §15.4 tests — and a defect found by not running one

**T-S06 PARTIAL PASS.** No logic analyser, so run with the halogen on ch 1
across ~10 consecutive base power cycles: no flicker on any, arm and fire relays
solid. This catches a relay actually *pulling in* — a sustained wrong gate
level, the failure that matters. It cannot see a microsecond gate transient,
which is what the written criterion measures, though a relay armature has
milliseconds of inertia and physically cannot respond to one. Recorded as
PARTIAL, not a pass on the written criterion.

**T-S18 stays open, and no harness will ever close it.** It tests a *hardware*
property: with the internal pulls disabled, a broken sense wire is held at the
safe level by the external divider's 100 kΩ leg, and LOW = key OFF. Forcing
`key_sense_get_debounced()` false would only re-test guard 1, which fires
routinely, and would prove nothing about the wire. If ever run: **break the
connection on the key-switch side of the divider**, not at the GPIO —
disconnecting there orphans the divider and leaves the pin genuinely floating,
which is a different and less safe test.

**T-S10 is not runnable (soldered display) — and working out why produced more
than the test would have.** An injection could substitute for the boot-halt
*response*, never for whether a real MOSI break is *detected*. Reading that
detection code found two defects, both fixed in **fw 1.1.28**:

1. **The boot read discarded the SPI transaction status**, which §5.5.6 already
   required — *"a health check that succeeds only because the SPI layer
   swallowed an error is not a health check."* The **periodic** check has
   honoured that since 1.1.9; the **boot** read never did.
2. **The test was `s_panel_id != 0`.** A broken MOSI leaves MISO undriven: it
   reads `0x00000000` (caught) or floats to **`0xFFFFFFFF`** (not caught). The
   remote would boot believing a dead panel healthy — and every screen after
   that is a lie, including ARMED.

**The spec contradicted itself and the firmware implemented the weaker clause:**
*"any non-zero read-back is considered valid"* against *"only a zero or
**garbage** read-back … is treated as a fault"*, when all-ones is both. §5.5.6
corrected (FSD v1.45) to require rejecting both undriven signatures and checking
the SPI status. Verified on target that the real clone panel still reports
`ID 0x2A403300 (healthy)` — the tightened check does not reject this hardware.

### Edge-case testing found a dead-man defeat — fw 1.1.29

Phase 5 task 5, aimed at known seams rather than random mashing. E1 (rapid
arm/disarm), E3 (encoder during countdown) and E4 (channel change while armed)
all passed. **E2 did not: mashing the fire button fired the channel.**

```
655397  ARMED -> PRE_FIRE (ch 1)
660437  PRE_FIRE -> FIRING (local countdown elapsed)   <- full 5040 ms, no abort
660917  Fire button released — CEASE_FIRE
```

Other attempts in the same run aborted correctly, so this was not a mis-run: on
that attempt the FSM never saw a release across five seconds of mashing.

**Mechanism.** The fire button used symmetric `DEBOUNCE_8BIT` at a 10 ms poll,
so a release was only reported after **80 ms of continuous release**. Mash faster
and the shift register never reaches all-high: no release reported, FSM sees a
continuous hold, `CMD_FIRE` repeats keep flowing, and **both dead-man layers stay
satisfied** — the remote's release detection and the base's
`FIRE_AUTHORIZATION_TIMEOUT_MS` both sit downstream of that one decision, so
neither can catch it.

**Not only about deliberate mashing.** A worn or chattering contact produces the
identical signal, as would a shaking hand. The operator would believe they were
not holding the button while the system fired.

**The underlying error was symmetry**, and it was in the *spec* as much as the
code — §5.3 stated the shift-register rules as universal. For a dead-man the two
directions have opposite consequences: a missed release fires an igniter the
operator has let go of; a spurious release only aborts, which is the direction
that cuts current.

**Faster polling was considered and rejected.** It narrows the blind window
without closing it, and 8 samples at 1 ms is 8 ms — inside typical bounce
duration (1–10 ms) — so it would erode the bounce rejection debouncing exists
for, in *both* directions, at 10× the polling cost.

**Fix:** opt-in `rlc_debounce_set_fast_release()`. Press keeps 8 samples (80 ms);
release needs 2 (20 ms), which sits between bounce (1–10 ms, rejected) and a
human release (30–80 ms, caught). Other consumers keep symmetric debouncing.
Pinned by `test_debounce.c` T-D07/T-D08, and **the test was verified to FAIL
against the old behaviour** rather than merely passing alongside it. §5.3 now
requires the asymmetry (FSD v1.46).

**Retest, logged on both units** — the first retest had rested on observation
because the captures were killed to free the ports for flashing:

```
494597 ARMED -> PRE_FIRE   494667 released during PRE_FIRE — abort   (70 ms)
497577 ARMED -> PRE_FIRE   497597 abort                              (20 ms)
500497 ARMED -> PRE_FIRE   500547 abort                              (50 ms)
504637 ARMED -> PRE_FIRE   504657 abort                              (20 ms)
507937 ARMED -> PRE_FIRE   507957 abort                              (20 ms)
511477 ARMED -> PRE_FIRE   511497 abort                              (20 ms)
```

Six bursts, every one aborted, **0 `Fire timer started`**. The latencies —
mostly exactly the 20 ms threshold — are the fix visible in the timing.

**Incidental, not a defect:** two `ARM NACK: 0x04 (NO CONTINUITY)` during rapid
cycling on a channel fine at rest. Continuity sensing needs the channel relay in
NC and there is a 50 ms settling delay, so re-arming faster than that catches an
unsettled reading and the base refuses — the safe direction. It does mean very
rapid re-arming can be rejected with no cause the operator can see.

### New tooling

| Path | Purpose |
|---|---|
| `tools/gen-secrets.sh` | Generates the gitignored crypto keys from `/dev/urandom` |
| `tools/git-hooks/pre-commit` | Refuses commits containing key material |
| `./build_remote.sh --inject` | Remote fault-injection console (`d` display fault, `b` battery critical) |
| base `--inject` key `w` | Reports `ERR_RELAY_FAULT` — a weld otherwise needs GPIO 21 jumpered on a live base |

### Notes and follow-ups

- **§15.3 is complete.** T-F01/F02/F03/F08 PASS, T-F04/F05 by earlier evidence,
  T-F06/F07/F09 discharged by the host FSM harness. None needed live ignition.
- ~~T-S19 needs burn-through~~ **PASS** (attested, earlier igniter testing). The green OPEN path has not been seen on the display, which postdates it.
- **Phase 5 task 5 (edge cases) is PART-RUN.** Still to do: power cycling
  *under load* (during FIRING — T-S01 covered armed, not firing), simultaneous
  inputs (fire release and arm switch off together), and sustained repetition
  for drift. Given E2, the simultaneous-input cases look most likely to find
  something: they probe the same assumption, that events arrive one at a time
  and are each seen.
- **§15.4 is 14/19** (incl. T-S06 partial). Only **T-S10 and T-S18** genuinely
  open, both blocked on physical access rather than effort — a soldered display
  and a soldered key-sense wire. T-S10's *substance* is addressed by the 1.1.28
  fix even though the test itself cannot run. T-S12/S13 remain physically
  unreachable and discharged by host test T-FSM06.
- The remote harness does **not** unblock T-F07/T-F09 — they need injections at
  specific FSM transitions that have not been written.
- `gptimer_stop(): timer is not running` is logged at **ERROR** level on the
  first shot of every power cycle (the defensive stop finding nothing to stop).
  Benign but will send someone chasing a phantom fault.
- Bench note: the operator must **remove USB** to power-cycle either unit, since
  USB 5 V backfeeds them — so power-cycle tests lose that unit's log.
- The remote's CH340 adapter drops off the USB bus intermittently, and leftover
  capture processes holding a port are what caused the silent flash failures.
  Kill captures before flashing.

## 2026-08-27 — All review findings fixed: firmware 1.1.9, FSD v1.44, host FSM harness

Fixed every finding of `Code_Review_AllPhases_20260827_0308.md` — the Critical,
all 8 Majors, and the Minors — plus the Info-level items that were worth acting
on. Firmware 1.1.8 → **1.1.9** (both units; the version check is strict, so
flash them together). FSD v1.43 → **v1.44**.

### The Critical — BF-01 / bug #31: fire timer left running after a completed pulse

`fire_timer_stop()` was called on every exit from FIRING **except the
successful one**. An expired one-shot GPTimer alarm auto-disables the *alarm*,
not the *timer* — the driver stays in `GPTIMER_FSM_RUN` — so the second
arm-and-fire cycle of a power cycle called `gptimer_start()` on a running
timer. Under ESP-IDF v5.4.1 (what this project builds with) that returns
`ESP_ERR_INVALID_STATE`, and the `ESP_ERROR_CHECK` around it called `abort()`.

The panic happens **after** `relay_fire_set(ch, true)`. Both the arm relay and
the channel relay are energised at that instant and nothing in a panic path
de-energises them, so the igniter carried full current for the entire
panic-print-and-reboot interval — over 100 ms, against an e-match that fires in
single-digit milliseconds. The base then rebooted mid-FIRING and the remote saw
a link drop rather than FIRE COMPLETE.

Never observed because no test had ever completed a pulse and re-armed on the
same power cycle: T-F02, the only G3 fire test run, aborts before the pulse.

Fixed in three layers, because one would have been enough only until the next
refactor:

1. `fire_timer_stop()` on the `EVT_FIRE_PULSE_DONE` path.
2. An unconditional `gptimer_stop()` at the top of `fire_timer_start()`, so
   correctness does not depend on which exit path ran last.
3. `fire_timer_start()` now returns `esp_err_t` instead of using
   `ESP_ERROR_CHECK`. On failure the FSM cuts the pulse, runs
   `relay_all_safe()` and latches `ERR_INTERNAL`. **Nothing on the fire path
   may `abort()`.**

Regression-tested by `tests/host/test_base_fsm.c` T-FSM05: two complete
arm→fire→pulse-done→cooldown cycles plus a fault-injected timer-start failure
that must end in ERROR with the igniter de-energised. It runs on every build.

### The 8 Majors

- **DS-01 — FSD §5.5.6 runtime display health check did not exist.** The panel
  ID was read once at boot and every SPI return code was discarded, so a panel,
  flex or connector that failed mid-session simply froze the last rendered
  frame — potentially an ARMED screen reading "CONTINUITY CONNECTED" — while
  the FSM went on accepting fire commands. The TWDT cannot catch this: the task
  keeps flushing happily into a dead bus. Now: SPI errors counted, 5 s panel-ID
  re-read inside `display_task` (serialised with frame writes by construction),
  two consecutive bad reads required before declaring failure, and a failure
  posts `EVT_DISPLAY_FAULT` — on which the remote FSM ceases fire, disarms and
  latches ERROR from any state. The spec's own "during IDLE state" wording was
  corrected too: a check that only ran in IDLE could never detect the failure
  the next sentence required it to react to.
- **CM-01 — unlocked cross-task race in `rlc_link_send_status_update()`.** It
  runs on `status_update_task` and mutated `s_tx_seq` (a non-atomic RMW),
  `s_status_update_seq` and `set_state()` while `link_task` held the mutex over
  the same state. Duplicate sequence numbers were reachable, and the peer
  rejects those as replay. Now fully locked, with the frame build and radio
  send moved outside the lock.
- **TT-04 — zero automated tests for either safety FSM.** New host
  event-injection harness: `tests/host/test_base_fsm.c` compiles the production
  `rlc_base_fsm.c` against recording fakes and asserts outcomes — which relay
  moved, which siren pattern sounded, which NACK went out — across 111 checks
  in nine groups (arming guards, arm-verify window, continuity-loss disarm
  including the bug #30 backstop, PRE_FIRE guards, two fire cycles, every
  FIRING exit, weld fault, ERROR terminality, timeouts and recovery). This
  discharges §4.5, the "verify by code review" substitute agreed for
  T-F06/F07/F09 and T-S12/S13, the host half of T-A05, the base half of T-U07,
  and positive verification of bug #30.
- **DOC-01/02/03/04/13** — already closed by the v1.43 doc sweep in the
  previous session; re-verified.
- **TT-01 — `tools/test_tr04.py` ports stale and crossed.** `BASE_PORT` pointed
  at the adapter of dead chip #3 (not present on this machine at all) and
  `REMOTE_PORT` pointed at the *base* board, so running the script as written
  would have halted the remote and talked to the base as if it were the remote.
  Corrected, made overridable with `--base` / `--remote`, and it now refuses to
  run against a port that does not exist, printing the live by-ids.
- **TT-02 — `vbat_fit.py` could not parse any real capture.** It expected
  `CSV,`/`PLATEAU,` records; `vbat-cal` emits `MEDIAN ...` lines. It exited
  "No CSV records found" on every log, leaving only the manual `--pairs` path
  usable. Now parses the live format (dropping over-range readings, which are
  an indication and not a measurement) and still accepts the historic one.

### Minors and selected Info

- **BF-02** — PRE_FIRE→FIRING guard 2 (heartbeat freshness) had been folded
  into guard 4's failure-*rate* check. 2 misses in 10 is 20 %, which passes the
  30 % test, and still means ~1.5 s of silence at the moment of ignition. Now
  an explicit `rlc_link_ms_since_contact()` test that aborts to LINK_LOST, per
  §7.2.4, while guard 4 continues to abort to IDLE.
- **BF-03** — `SIREN_CONTINUITY_LOST` (§12.2) implemented and sounded at all
  three continuity-loss disarm sites; that disarm had been audibly identical to
  a key-off disarm, i.e. silent.
- **BF-04 / CI-05 / RM-09** — boot failures used to `return` out of app_main,
  leaving the relays safe but the FSM in BOOT, no ERROR state, no error siren,
  and the housekeeping loop gone. Both units now latch a halt with siren/buzzer
  and LED. `rlc_battery_init()`'s return is checked, and its internal
  `ESP_ERROR_CHECK`s — which could reboot-loop — are gone.
- **BF-05/06/07** — dead `base_fsm_post_event()` removed (a zero-timeout poster
  every caller had already abandoned); siren `gpio_config`/`esp_timer` returns
  checked with NULL-safe fallbacks; the FSM event queue is created before the
  arm-sense task starts, so a contact weld present at power-on is no longer
  dropped into a NULL queue.
- **CM-02** — App D.3's NACK 0x08 (replay) and 0x06 (integrity) are emitted
  instead of dropping the frame silently, which contradicted the project's own
  no-silent-refusals principle. A corrupted frame deliberately does **not**
  advance the rx sequence counter.
- **CM-03** — `update_sequence` data-gap detection implemented (the field had
  been generated correctly and never consumed), with modular comparison so the
  uint16 wrap is not a 65535-frame gap.
- **CM-04** — truncated ACK/NACK frames are dropped rather than forwarded with
  zeroed fields, which decoded as "UNKNOWN ERROR" or as an unmatched ACK the
  operator waited out.
- **CM-05** — seq 0 is rejected for every message type. The old
  `rx <= last && last != 0` form left an unlimited seq-0 replay window at the
  start of every session; since every sender pre-increments, a plain `<=`
  admits the real first frame anyway. One shared `seq_is_replay()` now matches
  `rlc_seq_validate()`'s semantics exactly.
- **CM-06** — the `espressif/esp-now` managed component was declared but never
  used (the code is written against native `esp_now_*`). Removed: dead flash
  and an avoidable supply-chain surface on a safety-critical unit.
- **CM-07** — every silent queue drop on the receive path now logs; an overrun
  used to be indistinguishable from RF loss in a post-mortem.
- **CI-01** — `CONT_RELAY_DROPOUT_MS` had been dead since it was defined. The
  sampler now skips a channel for 50 ms after its relay de-energises, so no
  reading is taken while the NO→NC contacts bounce.
- **CI-02** — `ERR_VBAT_LOW` (§13.1 bit 0) is now set, derived live in
  `status_update_task` rather than latched, so the remote can show a base
  "VBAT LOW" warning before an ARM is refused.
- **CI-04** — `led_task` registered with the TWDT. A hung RMT refresh froze the
  status strip, ARMED blink included, while the watchdog stayed happy.
- **CI-06** — the encoder-before-ADC ordering constraint (GPIO 4/5 are
  ADC1_CH3/CH4) is now documented at the call site. It was correct only by
  accident of layout; "sort these into §9.13 step order" would have silently
  re-broken the knob.
- **CI-09/10** — `CONT_TRACE_INTERVAL_MS` defaulted to 1000 while its own
  comment said "set to 0 for field use", so production builds wrote a trace
  line per second into the log an operator has to read a fault out of; now 0.
  `rlc_rgb_led_init()`'s return is checked on both units.
- **RM-01/02/03/05/06/07/11 and DS-02/03** — encoder short-press in IDLE now
  answers ("HOLD TO ARM") instead of being dropped; §8.2.2 `num_channels`
  adaptation implemented end to end (encoder wrap, local ARM guard, "N/A" cells
  on the display); the FIRE path gained the `WAIT_FOR_ACK_INTERRUPTED` branch
  the ARM path already had, so an operator-cancelled fire no longer reads "NO
  RESPONSE - FIRE ABORTED"; the buzzer queue is a one-deep mailbox written with
  `xQueueOverwrite`, closing the race that let a stale pattern play; the
  fire-repeat task checks the physical button; `BEEP_CONTINUITY_LOST` and
  `BEEP_PING_FAIL` are finally played (the first on a continuity-caused disarm,
  the second on the rising edge into a degraded link); `s_prefire_start_ms` is
  read and written under a lock, so the countdown cannot tear; the channel grid
  no longer overflows 480 px and clips the right border of channels 4 and 8;
  and the ARMED screen shows "CONTINUITY ?" rather than asserting OPEN on stale
  data.
- **RM-04 / CI-03** — `buzzer_task` moved from an unpinned priority 5 back to
  §9.10's 1 / core 1. A UI task had been running above the safety FSM, in
  direct violation of that section's SHALL; the spec was not relaxed to match.

### Tests and tooling

- Host suite: **12 binaries / 265 checks → 16 / 418**, 0 failures. New:
  `test_base_fsm.c` (111) and `test_seqgap.c` (T-U04/T-U09/T-U16, 21 each).
- `build_base.sh` and `build_remote.sh` now run `tests/host/run.sh` before every
  firmware build and **refuse to build on failure** (`RLC_SKIP_HOST_TESTS=1`
  bypasses). The runner existed but nothing had ever invoked it, so a regression
  could reach a board without anyone running the tests.
- Unit-specific tests print `SKIPPED` instead of "0 checks, 0 failures", which
  had read identically to a passing test (TT-07).
- New stubs: `esp_task_wdt.h`, `esp_err.h`, `freertos/queue.h`, plus task-notify
  and pinned-create shims.

### Documentation

FSD → **v1.44**: §7.2.4 guard 2 restated as a freshness test that must not be
folded into guard 4, and its action 2 now requires a stopped-first,
return-checked fire-timer start that never aborts; §7.2.5 gains the explicit
"stop the fire timer" step on the successful path; §5.5.6 corrected and
tightened; §5.4.6/§7.3.1 relay settling specified; §9.10 task tables audited
against the built firmware (`espnow_rx` was missing entirely, the link manager
was listed under an old name at the wrong priority); §8.2.2 `num_channels`
behaviour pinned; §12.1 `BEEP_PING_FAIL` given rising-edge semantics; §15.5
documents the new FSM harness test-by-test; §14.5 trace default 0.
Development_Progress gains bug #31, a **Firmware Version History** table
(1.1.3–1.1.7 had been recorded only in `rlc_version.h`), and Phase 5 items
9–13. README and Project Summary updated (the "10 guard conditions" list
actually listed 7).

### Build and verification

Both units rebuilt clean from scratch after every batch of edits; host suite
green throughout.

```
./tests/host/run.sh          # 16 binaries, 418 checks, 0 failures
./build_base.sh              # Verified: base_app_main in binary
./build_remote.sh            # Verified: remote_app_main in binary
```

Two things surfaced only at build time and are worth remembering:

- **`-Werror=format-truncation` is load-bearing here.** Adding the early return
  for the RM-02 "N/A" cell in `draw_channel_cell()` inhibited inlining of the
  caller's `for (ch = 1; ch <= 8; ...)` loop, so GCC lost the range of `ch` and
  rejected the pre-existing `snprintf(label, sizeof(label), "CH%d", ch)` into
  `char[8]` that had compiled for months. Fixed by bounding `ch` explicitly at
  the top of the function rather than by widening the buffer — the range guard
  is the honest fix and it also makes the function safe to call directly.
- **Removing `espressif/esp-now` from `components/rlc_common/idf_component.yml`
  regenerates `dependencies.lock` and prunes `managed_components/`.** Both are
  gitignored, so this is invisible in the diff; `espressif__cmake_utilities`
  disappeared too, since it was only a private dependency of esp-now. Confirmed
  with `grep -c esp-now dependencies.lock` → 0.

### Files touched

40 modified, 5 added, ~1650 insertions.

| Area | Files |
|---|---|
| Fire path (base) | `rlc_fire_timer.{c,h}`, `rlc_base_fsm.{c,h}`, `rlc_relay.c`, `rlc_siren.{c,h}`, `rlc_base_main.c`, `rlc_continuity.{c,h}`, `rlc_status_update.c` |
| Comms | `rlc_link.{c,h}`, `rlc_message.{c,h}`, `rlc_fsm_events.h` |
| Remote | `rlc_remote_fsm.c`, `rlc_remote_main.c`, `rlc_display.c`, `rlc_buzzer.c`, `rlc_encoder.{c,h}` |
| Common | `rlc_battery.c`, `rlc_rgb_led.c`, `rlc_config.h`, `rlc_version.h`, `idf_component.yml` |
| Tests | **new** `test_base_fsm.c`, `test_seqgap.c`; **new stubs** `esp_err.h`, `esp_task_wdt.h`, `freertos/queue.h`; modified `run.sh`, `test_encoder.c`, `stubs/freertos/task.h`, `stubs/esp_adc/adc_oneshot.h` |
| Tools | `tools/test_tr04.py`, `tools/vbat_fit.py` |
| Build | `build_base.sh`, `build_remote.sh` |
| Docs | FSD, `Development_Progress.md`, `README.md`, `RLC_Project_Summary.md`, `changelog.md` |

### Watch out for

- **The display health check has not been soaked on hardware.** It disarms on
  two consecutive panel-ID mismatches against the value latched at boot. This
  panel is a clone reporting a non-standard `0x2A403300`; if that read turns
  out not to be perfectly repeatable, the check can disarm spuriously. Leave
  the remote linked and idle for an hour and watch for
  `display health check FAILED` before trusting it at a pad.
- **The BF-01 fix is proven in the host harness, not on the bench.** The
  power-cycle-between-launches practice should stay until Phase 5 item 10 has
  been run on target.
- **`CONT_TRACE_INTERVAL_MS` is now 0.** Set it back to 1000 in
  `rlc_config.h` when doing continuity work at the bench, or the per-sweep raw
  ADC trace will be missing and it will look like the sampler stopped.
- **`buzzer_task` dropped from priority 5 to 1.** Beep timing under load may be
  slightly less crisp than before. That is the intended trade — a UI task must
  not preempt the safety FSM.
- The base FSM harness calls `process_event()` / `check_timers()` directly and
  never runs `base_fsm_task`. If a future change moves logic **into** the task
  loop rather than into those two functions, the tests will silently stop
  covering it.

### Not done, and why

- **T-C06 on-air replay tool.** The rule is now host-tested and the base emits
  NACK 0x08, but capturing a real frame off the air and re-transmitting it
  needs a third radio. Tracked as Phase 5 item 13.
- **Remote FSM host harness.** The base FSM was the one on the fire path;
  the remote's is the obvious next application of the same technique. Phase 5
  item 11.
- **On-target two-fire-cycle run (G3).** The BF-01 fix is regression-tested in
  the host harness; the bench confirmation is Phase 5 item 10 and is what
  actually lifts the one-launch-per-power-cycle restriction.
- **CI runner** (TT-12 second half) and **bug #20 key rotation** remain open.

---

## 2026-08-27 — Full-codebase review vs FSD: verdict FAIL (1 Critical), documentation swept to FSD v1.43

Read-only review of all production code, tests/tooling, and documentation
against the FSD, run as 7 parallel tracks (base fire path, remote operator
path, comms/protocol, common infra, display, tests+tools, documentation
consistency). No code was modified. Report:
`Code_Review_AllPhases_20260827_0308.md` (commit reviewed: `5b6515f`, fw 1.1.8).

### Verdict and headline numbers

**FAIL** — 1 Critical, 8 Major, 44 Minor, 38 Info.

**The Critical (BF-01) gates live fire:** `fire_timer_stop()` is never called
on the *successful* fire-pulse completion path. The GPTimer stays in RUN
state, so on a **second launch after one power cycle** `gptimer_start()`
returns `ESP_ERR_INVALID_STATE` and the `ESP_ERROR_CHECK` in
`fire_timer_start()` panics and reboots the base **with the channel and arm
relays energized** — uncontrolled ignition pulse duration, then a mid-fire
reboot. Verified against the actual toolchain (ESP-IDF v5.4.1 per
`build_base/project_description.json`). Never seen in testing because no test
has completed a pulse and then re-armed on one boot (T-F02 aborts pre-pulse).
Toolchain-sensitive: on IDF 5.5.x the panic disappears and becomes a silent
timing hazard instead — the fix (stop the timer on EVT_FIRE_PULSE_DONE, plus
a checked return instead of `ESP_ERROR_CHECK`) is required under both.
**Operational mitigation until fixed: power-cycle the base between launches.**

### The 8 Majors

| ID | Finding |
|---|---|
| DS-01 | FSD §5.5.6 runtime display health check entirely missing — a mid-session panel/SPI fault freezes the last-rendered ARMED screen while the FSM keeps accepting fire commands. Added to the Phase 5 table |
| CM-01 | Unlocked race in `rlc_link_send_status_update()` (base status task vs link task on tx-seq/state) — same class the author already fixed in `rlc_link_send_cmd` |
| TT-04 | Zero automated tests for either safety-critical FSM (§4.5 mandate). A host event-injection harness would discharge §4.5, T-F06/F07/F09's review-substitute, T-A05's host half, T-U04/07/09/16, T-C06 and bug #30 positive verification in one work item |
| DOC-01/13/TT-03 | FSD App D.4 + T-F05 contradicted the implemented v1.35 continuity-loss disarm ("informational only") — fixed in this session's doc sweep |
| DOC-02 | Stale "DATA STALE — CANNOT ARM" text in §8.2.3/App B.2 — fixed |
| DOC-03/04/TT-05 | Both 2026-08-21 hw-test-spec defects still present (base spec: RGB on GPIO 47, actually 48; remote spec flashed the *base* board's by-id) — fixed |
| TT-01 | `tools/test_tr04.py` ports stale/wrong (BASE_PORT = dead chip #3 adapter; REMOTE_PORT = now the base board). Live by-ids: base `5B5E042156`, remote `5B5E043219` — **not fixed, tool repair pending** |
| TT-02 | `vbat_fit.py` cannot parse current `vbat-cal` output (`CSV,` vs `MEDIAN`/`ADCMAP,` lines; verified by execution) — **not fixed, tool repair pending** |

### Verified clean

- Bug #30 / fw 1.1.8 level-triggered continuity backstop: sound and
  effectively unbypassable in its spec scope (two independent layers).
- All FSD §14 constants and Appendix C pins match the code exactly
  (`PRE_FIRE_DELAY_MS=5000`, `FIRE_PULSE_DURATION_MS=1000`, margin 250;
  zero pin mismatches across 21 base + 18 remote).
- Protocol 1:1 vs App A (message types, NACK codes, error flags, struct
  sizes); fuzz/replay frame safety solid.
- Host test suite re-run during review: **12 binaries / 265 checks /
  0 failures** — exactly matching the FSD's claim.
- Prior-review fixes (N3 TWDT ordering, 2026-08-21 Majors) re-verified in
  place.

### Documentation sweep (same session, 9 files, +121/−82)

FSD bumped to **v1.43** (revision row added): App D.4 and T-F05 rewritten to
the implemented continuity-loss disarm semantics; "DATA STALE — CANNOT ARM"
→ "NO BASE STATUS DATA" (2 places); SIREN_ARMED table → continuous (v1.35);
SHORT-band remnants annotated deprecated in §3/§5.4.2/§7.3.1/§14.5; App C.1
GPIO 42 spare-list error fixed + key-sense row added; §14 gained the missing
constants (`FIRE_PROTECTED_CHANNEL_MASK`, ADC atten/full-scale, splash, trace,
VBAT_FULL, calibrated divider ratios); §7.2.9a moved from inside §6 to after
§7.2.9; T-C0x expected results updated to v1.18 LED behaviour; T-S12/S13
annotated unreachable; §10.2.0 palette as-built note; three
`FIRE_PULSE_DURATION_MS` "2000 ms" prose staleness fixed (code/§14.1 = 1000).
Base hw-test spec: GPIO 47→48 + 8-pixel wording. Remote hw-test spec: by-id
now the remote's own debug unit (MAC `AC:A7:04:E2:F2:8C`), board-serial
alternative noted. Development_Progress: header → v1.43/fw 1.1.8; consolidated
fw 1.1.2–1.1.8 section (was zero-coverage); DS-01 + BF-01 two-fire-cycle test
added to Phase 5 table; T-S07 2 s→5 s; stale "thresholds not restored" clause
deleted. Test_Report_Phase3_G2 header reconciled with its own totals.
README: v1.43, BF-01 critical notice in Known-open-items, review added to
docs table. Project Summary list structure fixed. Gotron shopping list:
band-boundary table recalculated against production thresholds (261/586 mV,
217 Ω), bug #27 marked RESOLVED.

### Follow-ups

1. **Fix BF-01** (fire timer stop + checked `gptimer_start` return), then add
   a G3 test: two complete fire cycles per power-on.
2. DS-01 display health check, CM-01 mutex — next firmware items.
3. Repair `test_tr04.py` and `vbat_fit.py` (TT-01/TT-02).
4. Phase 5 kickoff with the FSM host-injection harness (TT-04) as centerpiece.
5. Remaining 44 Minors at maintainer's pace; RM-06 (fire-repeat button check)
   and CI-06 (encoder-before-ADC comment) are one-liners.

## 2026-08-26 — Hardware bugs closed, G2 arming suite complete, firmware 1.1.1 → 1.1.8

Hardware rework by the operator closed the last three open hardware defects.
Two firmware changes followed from testing the result.

### Hardware (operator, this session)

| Item | Result |
|---|---|
| Bug #28 — ARM RELAY LED lit with the key in SAFE | **FIXED.** Indicator wiring corrected; the coil LED now lights only when the arm relay is genuinely energised |
| Arm-key red + green LEDs lit *simultaneously* in SAFE (untracked, found in the same rework) | **FIXED.** Now red = ARMED, green = SAFE |
| Bug #27 — siren driver not fitted | **FITTED.** IRLZ44N on GPIO 40, 150 Ω gate series, 10 kΩ gate pull-down, 1N5819 flyback across the siren (cathode VBAT+, anode drain) |
| Bug #19 — base LED strip dark from pixel 4 | **FIXED.** Strip replaced; all 8 pixels respond |

**Bug #28 was an indicator fault, not an interlock fault.** Both LED problems
were sneak paths in the indicator wiring — a return that was not true GND.
Neither touched the key switch's break of the arm relay coil circuit, so the
hardware AND gate of FSD §5.4.4 was never compromised. The operator rule
"green = SAFE, red coil LED = fire bus live" is true on this unit again, and
**the fire-testing hold this bug imposed is lifted.**

**Bug #19's real root cause was pixel *3*, not pixel 4.** Pixel 3 rendered its
own colour correctly throughout but had a dead output stage, so it passed no
data downstream. That is why every visual test called it healthy and why the
break appeared to be at pixel 4. The donor-LED and reworked-joint theories that
had accumulated around position 4 were both wrong. **Lesson: a pixel that
lights correctly is not evidence that it is passing data — probe DOUT.**

**Flyback diode — the 1N5819 is correct here.** It is one of the parts FSD
§5.4.8 names for this position and the same part already stocked for the
continuity clamps. One rating to confirm on the bench: it is 40 V / **1 A** and
carries the full siren current at turn-off, so it is correctly rated only if
the siren draws under 1 A at 12 V. Above ~700 mA, move to an SS34-class 3 A
part. (This became a much softer constraint later in the session — see the
siren change below.)

### Firmware 1.1.2 — siren continuous in ARMED

**Operator finding once the siren was audible for the first time:** the 500 ms
ARMED pulse **interferes with the siren's own internal modulation.** The device
runs its own sweep; gating the 12 V supply at 1 Hz restarts that sweep on every
edge, so it never reaches the loud part of its cycle. The pulsed ARMED warning
was *quieter and less attention-getting than a steady tone* — the opposite of
the intent.

The pattern dates from FSD v1.1 (2026-03-22), when the base had a plain buzzer
whose only available modulation *was* on/off gating. It outlived its rationale
by five months and nobody had heard it on hardware until the driver was fitted
five days ago.

`siren_start_pulse()` removed. ARMED calls `siren_start_continuous()`, so the
siren sounds unbroken from ARM through PRE_FIRE and FIRING. PRE_FIRE re-asserts
continuous rather than assuming it, so the state does not depend on how ARMED
was entered. **LINK_LOST and ERROR stay patterned** — those patterns carry
information (they distinguish a fault from an armed pad) and are short enough
that the interference does not matter.

Two side effects:

- Base draw during ARMED roughly doubles versus the old 50 % duty. Immaterial —
  `ARM_TIMEOUT_MS` bounds ARMED at 10 s.
- **The flyback diode now switches once per sequence instead of once per
  second**, which retires the repetitive-avalanche derating concern above.
  Only the steady-current question remains, and it is now a comfortable margin.

The N2 stale-callback protection in `rlc_siren.c` is unchanged and still
needed. The infinite (`-1`) pattern is gone with the pulse, so N2's first
failure mode is unreachable by construction — but link-lost and error are still
finite periodic patterns and can still park a callback on the mutex.

### Bug #29 — base stayed ARMED with the igniter disconnected

**Found on target during fire-sequence testing.** Arm a channel, then
disconnect the igniter: the base **stayed ARMED** with the arm relay energised,
the fire path live and the siren sounding, and neither unit indicated the
igniter was gone. The only exits were the 10 s arm timeout or an operator
disarm.

**This was a specification defect before it was a code defect.** Continuity was
checked once, at arm time (`guard_arm()` guard 2); after that the change
callback carried no arguments and said only "something moved", so the FSM never
saw band changes at all. FSD §7.3.1 stated the position explicitly —
*"Continuity-loss disarm during ARMED/PRE_FIRE states is not implemented ... an
accepted low-probability risk."* Three things were wrong with it:

1. **The rationale was obsolete by five months.** v1.8 (2026-03-23) removed the
   disarm because continuity sensing was *disabled* in ARMED/PRE_FIRE under the
   old shared-MOSFET design, so readings would have been stale. The v1.10 SPDT
   redesign — the very next revision — made continuity live throughout ARMED and
   PRE_FIRE, because the channel relay sits on NC until FIRING. The removal was
   never revisited against the new hardware.
2. **The document contradicted itself for 25 revisions.** §4.x's Phase 3 test
   criteria have listed *"All disarm triggers work (switch, command, link loss,
   continuity → OPEN, battery)"* the entire time.
3. **The risk is not low-probability.** An igniter leaving the circuit is
   precisely what happens when a person is at the pad handling it — the one
   moment when being armed matters most.

**Fix.** New `EVT_CONTINUITY_CHANGED` carries the channel number *and* the new
band from `continuity_task` to the base FSM; `armed_channel_went_open()` gates
the disarm. The band travels in the event rather than being re-read, because
the round-robin sampler may have moved the channel on again by the time the
event is dequeued. The queue send is a 10 ms blocking one, matching the
arm-sense sibling — a dropped event here would silently leave the base armed on
an open igniter, the exact failure being fixed.

**Scoped deliberately.** Getting this wrong in the other direction breaks firing
outright:

| Condition | Disarms? | Why |
|---|---|---|
| Band = OPEN | **Yes** | Matches arming guard 2 — OPEN is the only band that blocks arming |
| Band = MARGINAL / SHORT | No | Informational per §7.3.1 step 2; unchanged |
| Armed channel | **Yes** | — |
| Any other channel | No | Informational. T-A18 is the regression guard |
| State = ARMED, PRE_FIRE | **Yes** | Relay on NC, so the reading is live and real |
| State = FIRING | **No** | Relay on NO, NC sense line physically disconnected — reads OPEN *by design*. Acting on it would abort every fire pulse the instant it started |
| State = POST_FIRE | **No** | OPEN is the **success** indicator: it means the igniter fired |

**Detection latency: up to ~800 ms** — one channel per 100 ms round-robin over
eight channels, plus classifier hysteresis. Well inside `ARM_TIMEOUT_MS`, and
accepted: this is a safety backstop, not a real-time interlock. Do not record
it as one in a test report.

**The remote needed no change.** It learns of the disarm from the resulting
`STATUS_UPDATE`, whose `continuity_bands` field already shows the channel OPEN.
No new NACK reason — there is no command to NACK, because the trigger is a
spontaneous hardware event.

### Version

**Firmware 1.1.1 → 1.1.8 over the session.** 1.1.2 = continuous siren +
continuity-loss disarm (base-only). 1.1.3 = `PRE_FIRE_DELAY_MS` 5 s. 1.1.4 =
remote reports why an ARM was refused. 1.1.5 = channel-mismatch toast. 1.1.6 =
no silent refusals / NACK `0x0E`. 1.1.7 = fire-button ring LED reports state.
1.1.8 = bug #30 level-triggered continuity backstop. No wire-protocol change at
any step except the 1.1.6 NACK reason, but
the strict version check covers all three components, so **both units must be
flashed together** — which they were, at each bump.

### Files changed

| File | Change |
|---|---|
| `components/rlc_base/src/rlc_siren.c` | `siren_start_pulse()` and `SIREN_PULSE_HALF_MS` removed; N2 comments reconciled with the loss of the infinite pattern |
| `components/rlc_base/include/rlc_siren.h` | `siren_start_pulse()` removed; `siren_start_continuous()` documented as the ARMED→FIRING warning |
| `components/rlc_base/src/rlc_base_fsm.c` | Both ARMED entry paths call `siren_start_continuous()`; new `armed_channel_went_open()` helper; continuity-loss disarm in the ARMED and PRE_FIRE handlers |
| `components/rlc_base/src/rlc_base_main.c` | `on_io_change()` takes channel + band and posts `EVT_CONTINUITY_CHANGED` with a 10 ms blocking send |
| `components/rlc_base/src/rlc_continuity.c` | Change callback carries channel + band (both the normal and the ADC-fail-safe path) |
| `components/rlc_base/include/rlc_continuity.h` | `continuity_register_change_cb()` signature + rationale |
| `components/rlc_common/include/rlc_fsm_events.h` | `EVT_CONTINUITY_CHANGED = 0x19` and its payload struct |
| `components/rlc_common/include/rlc_version.h` | 1.1.1 → 1.1.8 (bumped at each release over the session) |
| `RLC_Functional_Specification_v1_14.md` | → **v1.35**: §5.4.4 as-built indicator note, §5.4.8 as-built driver note, §7.2.2/§7.2.3/§7.4.1 siren, §7.2.7/§7.3.1 continuity-loss disarm, state diagram, both transition tables, new tests T-A16/T-A17/T-A18, two revision entries (v1.34 hardware, v1.35 firmware) |
| `Development_Progress.md` | Bugs #19/#27/#28 → RESOLVED with full entries; new bug #29 entry; new "Firmware 1.1.2 — Siren Continuous in ARMED" entry; T-L18 → PASS; G2 widened to T-A01..T-A18; fire-testing hold lifted |
| `README.md` | Fire-testing status, siren status, bug #19 note, operating-sequence siren wording |
| `RLC_Project_Summary.md` | Club-facing doc: removed the badly stale "ESP32 destroyed, awaiting replacement" paragraph; siren wording; continuity-watched-while-armed added to the interlock list |

### Verification

- Both units build clean — **zero warnings, zero errors**.
- Host test suite: **0 failures** across all six suites.
- Both units flashed with 1.1.2 over their by-id ports (base
  `usb-1a86_USB_Single_Serial_5B5E042156-if00`, remote
  `...5B5E043219-if00`).

### Added: `tools/armgate-test` — arm-relay AND-gate verifier

Standalone bring-up firmware that proves the FSD §5.4.4 hardware AND gate **at
the ARM SENSE node**, not from the indicator LEDs — written because bug #28 and
its sibling were both indicator-wiring faults, so after that rework the LEDs are
exactly what cannot be the instrument.

Six steps: the three table rows (key SAFE + GPIO 47 driven → sense 0; key ARM +
GPIO 47 low → sense 0; key ARM + GPIO 47 driven → sense 1 and relay in), plus
step 0 validating KEY SENSE itself (a stuck input would otherwise make the run
a silent no-op reporting PASS), step 4 catching a relay that pulls in and never
releases, and step 5 reaching the key-SAFE case from an energised relay so the
key-switch leg is proven from both directions.

Each step samples ARM SENSE every 10 ms for 2 s after a 150 ms settle and
reports the HIGH-sample count; an unstable line fails whichever level was
expected. The operator moves the key, the firmware moves GPIO 47, and key
position is read from KEY SENSE — so the program waits rather than asking anyone
to type. All eight channel relays are driven inactive at boot. Failure guidance
maps each failing step to a fault class.

Ships with `run.sh`, which sources the ESP-IDF environment itself and checks
the port exists before building — `idf.py` is not on the PATH of a fresh shell,
so the bare-`idf.py` instruction in the first version of the README failed on
first use.

Builds clean. Also measured this session: **the siren draws under 200 mA
steady**, closing the 1N5819 rating question from bug #27 with a 5x margin —
recorded in the FSD, `Development_Progress.md` and `README.md`.

### Test campaign — G2 arming suite, bug #29 tests, siren bench tests

Everything below was run on target this session, after the hardware rework. The
base's serial log was captured throughout, so results are backed by timestamped
traces rather than observation alone. Full write-up in
`Test_Report_Phase3_G2.md`.

**G2 (FSD §15.2): 14 PASS, 0 FAIL, 2 N/A, 2 deferred.**

The three new bug #29 tests all pass. T-A16 disarmed 920 ms after the igniter
was pulled — pure round-robin sampling latency, with the FSM logging its
decision in the *same millisecond* as the band change. T-A17 aborted the
countdown with no fire pulse, and a repeat `CMD_FIRE` arriving 40 ms later was
NACKed `WRONG STATE`, which proves the fire button was still held (the first
attempt had aborted via `CEASE_FIRE` instead — a false pass the log caught).
T-A18 held ARMED across three band transitions on a non-armed channel.

**Two traces worth keeping.** T-A06 vs T-A07 show the arm relay dropping
*before* the FSM finishes when the key breaks the coil (+10 ms), and *after* it
when software commands the disarm (+160 ms) — the §5.4.4 AND gate's two legs
behaving differently, from the operational side. And the accidental ignition
during T-A17 validated the **FIRING exclusion** in bug #29's scoping: 680 ms
into the pulse the armed channel's band went OPEN (relay on NO, sense line
physically disconnected by design) and the FSM correctly ignored it. Scoped the
other way, that pulse and every future one would have aborted mid-fire.

**Incidentally banked:** T-S03 (base below VBAT_CRITICAL → ERROR, and correctly
still latched at 12.1–12.7 V afterwards) and T-S14 (arm timeout at 10022 ms
against a 10000 ms constant).

**Siren bench tests: six checks, all PASS — review finding N2 closed by
measurement**, having rested on code inspection alone since 2026-08-21. Silent
at power-on; continuous across ARMED→PRE_FIRE; stops and stays stopped after
all three disarm routes; LINK_LOST = 4 cycles of 500/500; ERROR = 3 blasts at
200 ms; and link recovery mid-pattern silences it immediately and permanently.

That last check is the one that now matters. N2's stuck-on mode depended on the
infinite `-1` pattern that disappeared with the ARMED pulse, so the original
bench test 3 can no longer reach it — the risk migrated to the still-periodic
LINK_LOST and ERROR patterns. The log shows recovery at 3080 ms, inside the
4000 ms window, so the case was genuinely hit. **If a periodic ARMED pattern is
ever reintroduced, that is the regression test, not bench test 2.**

### Three stale FSD tests corrected

None was a firmware fault; all three had drifted from the build and would have
produced false failures for anyone running the suite cold.

- **T-A05 contradicts T-A08.** Arming a second channel requires an encoder
  rotation, which T-A08 requires to disarm — satisfying one destroys the
  other's precondition. Confirmed on target: the remote sent `CMD_DISARM`, never
  a second ARM. The NACK 0x0A guard stays as defence-in-depth against a remote
  bug or replayed frame; it belongs in host tests, not the on-target suite.
- **T-A15 is superseded** and **T-A09's band list was wrong** — both still
  described a SHORT band merged into CONNECTED on 2026-08-21 (bug #26).

### Defect found and fixed — remote failed silently when an ARM was refused

Reported by the operator: with the base in ERROR, a long-press to arm produced
no beep, no message, nothing on the wire. Three causes compounded. The base's
ERROR handler is a bare `break;` so it discards `CMD_ARM` without a NACK; the
remote's ACK-timeout branch was the **only** failure path with no operator
feedback (every guard, NACK and channel-mismatch beeps and toasts); and the
remote never inspected `base_state == STATE_ERROR` despite receiving it in every
STATUS_UPDATE. A base that never answered was indistinguishable from a
long-press that had not registered.

Fixed remote-side in **1.1.4**: a new guard refuses locally and names the fault
(`BASE ERROR: VBAT CRITICAL`), and the timeout path now reports `NO RESPONSE
FROM BASE`. Verified on target. Left open deliberately: whether the base should
NACK from ERROR at all — that changes the contract of a deliberately terminal
state and was not altered unilaterally.

### PRE_FIRE_DELAY_MS 2000 → 5000 (firmware 1.1.3)

T-A17 requires disconnecting an igniter *during* the countdown. At 2 s the
operator could not act in time and **an igniter fired**. Raised temporarily to
10 s to complete the test, then settled by operator decision at 5 s: long enough
to act inside, short enough not to invite the fatigue-release the original value
was chosen to avoid. Both units flashed together — the remote runs its own
countdown against the same constant.

### Tooling — `tools/armgate-test`

Standalone firmware proving the §5.4.4 AND gate **at the ARM SENSE node** rather
than from the indicator LEDs, written because bug #28 and its sibling were both
indicator-wiring faults. Six steps beyond the three-row truth table: step 0
validates KEY SENSE itself (a stuck input would make the run a silent no-op
reporting PASS), step 4 catches a relay that never releases, step 5 proves the
key-switch leg from the armed side.

**Result: all seven steps PASS, every window 0/200 or 200/200 with no mixed
samples anywhere.** That is the finding — a marginal sneak path shows up as a
partial count long before it shows as a level, so bug #28's fix is confirmed by
measurement rather than inspection.

Two bugs of my own were fixed getting it running: `run.sh` now sources ESP-IDF
(bare `idf.py` is not on a fresh shell's PATH), and the console moved to UART0.
It had been built for the native USB port, which presents as a **boot loop** —
the ROM banner still reaches UART0 so the monitor shows repeated
`rst:0x1 (POWERON)` while nothing the app prints ever arrives. Documented in the
tool README so the next tool in this repo doesn't repeat it.

### Fault-injection harness — T-A11 and T-A13 closed (firmware 1.1.5)

The last two arming tests could never be run: neither is inducible from outside
the firmware. Link loss trips at 3 missed pings (1.5 s), long before the
staleness timeout, so no amount of interfering with the radio produces "linked
but stale" — it produces LINK_LOST. And nothing in normal operation emits a
malformed ACK.

Both injections turned out to be base-side, which kept the harness small.
`CONFIG_RLC_FAULT_INJECTION` (default off, base only, `./build_base.sh
--inject`) adds a UART0 console: `s` withholds STATUS_UPDATE while heartbeats
keep flowing, `a` corrupts the channel of one ARM ACK, `?` prints state.

**Four independent guards** keep a build that deliberately lies to the remote
from being mistaken for a real one: a compile `#warning`, a boot banner plus an
`ESP_LOGE`, a flash-time warning, and `sdkconfig.base` never being modified.
The build script also cleans the build directory when switching modes and
**fails the build** if the option did not actually reach the built config.

**T-A11 PASS** — remote refused locally (`NO BASE STATUS DATA`) with zero ARM
frames on the wire. The FSD's expected text (`DATA STALE — CANNOT ARM`) was
wrong; no build has ever shown it. Corrected.

**T-A13 PARTIAL, then PASS.** The safety behaviour was right first time: the
base ACKed ch5 for a ch4 request, the remote refused to arm and commanded a
disarm, relay out 190 ms later. But the display showed **nothing** — the
mismatch branch beeped without saying why, leaving an operator with a base that
had briefly armed a channel nobody selected and no explanation. Same defect
class as the ACK-timeout path fixed earlier today. Fixed in 1.1.5; retest passes
end to end.

**G2 is now 16 PASS / 0 FAIL / 2 N/A.**

### Two harness bugs that masqueraded as firmware failures

Worth recording, because both produced a convincing false negative.

1. **`idf.py set-target` regenerates `sdkconfig` from the defaults**, discarding
   the option appended before it — producing a *normal* build wearing an
   injection build's log messages. T-A11 duly "failed" because nothing was
   being suppressed. The option is now appended after `set-target`, and the
   script verifies it reached `build_base/config/sdkconfig.h` before flashing.
2. **Stack overflow in `fi_console`** — 3072 bytes is not enough for ESP-IDF
   stdio. The first `printf` from the task rebooted the base, silently clearing
   every injection flag. The boot banner survived only because it runs on
   `app_main`'s much larger stack, which made the harness look healthy. Raised
   to 8192 and moved to `ESP_LOG`.

Both presented identically: the base was rebooting (`rst:0xc`) between the
injection command and the arm attempt, so suppression was off by the time the
operator armed and the arm correctly succeeded. **When an injection test fails,
check the injection before the firmware.**

The base was reflashed with a normal build afterwards, verified two ways: no
`CONFIG_RLC_FAULT_INJECTION` in the built config, and zero injection symbols in
the ELF.

### No silent refusals — firmware 1.1.6, validated on target

Two operator decisions, taken after the day's testing kept surfacing the same
shape of problem.

**The base now answers commands while in ERROR** (new NACK reason `0x0E`)
instead of discarding them. Its ERROR handler was a bare `break;`, so the
remote could only time out — and a timeout carries no reason, which is how an
operator ends up unable to distinguish a dead link from a base needing a power
cycle. All four commands are answered, DISARM included: refusing a disarm on an
already-safe base looks odd, but "I am in ERROR" is the operative fact in every
case, and silently ACKing would report "all is well" about a unit that is out
of service.

The NACK payload is a fixed 6 bytes, so it cannot carry *which* error — and does
not need to. `error_flags` arrives in every STATUS_UPDATE, so the remote
resolves the generic code against its own cache and shows **"BASE ERROR: VBAT
CRITICAL"** rather than a bare code. No wire-format change.

**Every remaining silent branch on the remote now beeps and displays.** The
FIRE guard family was the worst of it — arm key off, base not armed, stale
status, degraded link, send failure, key-off-after-ACK and no-response all
dropped the remote to IDLE without a word, with the operator holding a fire
button watching nothing happen. Also covered: ARM send and retry failures, ARM
cancellation, base/remote state mismatch, the stale-status timeout, and
base-ended-sequence. New FSD §7.2.9a states this as a requirement, with the
rationale recorded: a refusal that only beeps is indistinguishable from an input
that never registered, and the natural response to apparent non-response is to
try again — the wrong instinct at a pad.

An audit leaves five log-without-display sites, all legitimate: multi-arm
(displays via another path), `do_enter_error()` (always reached through
`do_enter_error_text()`), and three init failures before the display exists.

### Validation — T-A19 and T-A20 PASS

```
BASE    NACK sent: type=0x20 reason=0x0e (BASE IN ERROR)
REMOTE  ARM NACK: 0x0e (BASE IN ERROR)
REMOTE  [NACK] BASE ERROR: VBAT CRITICAL
```

Plus the stale-status timeout now showing `BASE STATUS LOST` (log-only before)
and a base disarming under an ARMED remote showing `BASE DISARMED` (log-only
before).

**Making T-A19 testable was the harder half.** The remote's local ERROR guard
refuses to send an ARM whenever its cached status shows ERROR, so in normal
operation the base never gets to answer and `0x0E` is unreachable; the
real-world gap, a base failing between status updates, is under 100 ms.

The harness's first `e` command suppressed STATUS_UPDATE and left a ~4 s window
to arm inside. That is a race, not a test — an operator acting on a written
instruction cannot reliably hit it, and a miss is indistinguishable from a
failure. It now falsifies `base_state` to IDLE in STATUS_UPDATE while leaving
`error_flags` truthful, which keeps the local guard quiet with no time limit
**and** keeps the enrichment path as the thing under test. The injection defeats
the guard without defeating the behaviour being measured.

**T-A20 is recorded as spot-checked, not exhaustive.** Three branches were
triggered on hardware; the rest are compiled and reviewed but not individually
exercised, and most need a race or an injection to reach. Several will come out
naturally during G3, the first time the FIRE path runs in anger.

Base reflashed with a normal build afterwards, verified clean two ways: option
absent from the built config, zero injection symbols in the ELF.

**G2 final: 18 PASS / 0 FAIL / 2 N/A out of 20.**

### Fire button ring LED now reports state (firmware 1.1.7)

Reported from the bench: the ring does not go red when armed. **It never did.**
`rlc_fire_button.c` has driven the LEDs from the debounce callback since
`aafacd0` (Phase 2) — red while held, green while released — so the ring showed
the operator's own finger rather than whether pressing the button would do
anything. FSD line 1110 has specified ARMED/PRE_FIRE/FIRING since v1.13, so this
was an unimplemented requirement, not a regression.

New `fire_button_set_live()` is driven from the remote FSM **every tick**, not
on transitions alone: the base dropping out underneath an ARMED remote arrives
as a `STATUS_UPDATE`, not as a local state change. **Red requires both halves**
— remote in ARMED/PRE_FIRE/FIRING *and* a fresh `STATUS_UPDATE` confirming the
same channel armed at the base. `fire_is_live()` deliberately reuses the same
two conditions the §8.2.4 FIRE guards check, so the ring and the guards cannot
disagree: red exactly when a press would be accepted.

The press-driven behaviour was removed rather than kept as an override. A press
in IDLE is ignored (§8.2.3), so flashing red for it reports an action that will
not happen — the misleading-indicator failure §7.2.9a exists to prevent.

### Code review of the session's changes — one MAJOR found

`Code_Review_Session_20260826_2159.md`. Three questions were posed; one found a
real defect, two came back clean on evidence rather than assumption.

**Q1 — can `armed_channel_went_open()` fire in an unconsidered state?** No — but
the inverse is the defect, and it became **bug #30 (MAJOR, open)**. The
continuity-loss disarm is edge-triggered with no level-triggered backstop.
`STATE_IDLE` has no `EVT_CONTINUITY_CHANGED` branch; the M1 arm verify leaves
the FSM in IDLE for up to 200 ms with `s_armed_channel == 0`; and
`continuity_task` posts only on band *change*. An igniter going OPEN inside that
window has its event dropped and never regenerated, so the base enters ARMED
already-open and stays there until the arm timeout — **the exact hazard bug #29
was created to prevent**. The same hole exists if the FSM queue is full when the
event is posted. Fix is a level-triggered re-check on entry to ARMED. **Must be
fixed before G3.**

**Q2 — NACKing from ERROR vs. link sequence handling?** Clean. Same
`rlc_link_next_seq()` path as every other NACK, which already drops the link
rather than emitting a seq-0 frame on overflow; all calls on the FSM task; no
new concurrency. Two observations recorded: a bounded ~5 Hz NACK burst if the
base enters ERROR while the remote's fire-repeat is running (self-limits within
one status interval), and the fact that NACKs to DISARM/CEASE_FIRE are *not*
operator-visible because those sends are fire-and-forget — so the new feedback
reaches the operator for ARM and FIRE only.

**Q3 — do the new `display_toast()` calls block?** Clean. The only competing
holder of `s_req_mutex` releases it after a ~150-byte snapshot copy, before any
rendering or SPI. All new calls are on the FSM task with no lock held, and the
`check_timers()` stale toast sits inside the branch that latches, so it fires
once per episode rather than at 20 Hz.

The review is a **self-review** — same agent, same session — and says so. Its
blind spots correlate with the author's, so an independent pass before live fire
would still be worth having.

### Bug #30 fixed (firmware 1.1.8) — level-triggered backstop

Two fixes, covering different halves. A continuity re-check at **arm-verify
completion**, aborting with `NACK_NO_CONTINUITY` rather than arming and relying
on an edge already consumed. And a **periodic level check** in `check_timers()`
(~50 ms) for ARMED/PRE_FIRE — which is what covers an event dropped by a full
FSM queue, something an entry check alone does not.

That second point is a correction to the review's own first draft, which claimed
an entry check closed both cases. It does not: a queue-full drop while *already*
ARMED happens after entry, and nothing re-examines it. Both checks were needed,
and the review was amended to say so.

**Verification is partial and recorded as such.** T-F02 confirmed the entry
check does not false-positive and the backstop does not fire spuriously, but
neither has been positively triggered — that needs a disconnection inside a
200 ms window or a deliberately full queue. A `--inject` key that drops the next
`EVT_CONTINUITY_CHANGED` would exercise it directly and is the natural next
harness addition.

### G3 started — T-F02 PASS, three tests found unreachable as written

Deliberately ran the **non-firing** tests first: if an abort path is broken,
that is worth finding before lighting anything.

**T-F02 PASS** — released the fire button 1.36 s into the countdown, no
`Fire timer started`, clean return to IDLE. It did double duty: the arm
completed through the **verify path** that the new bug #30 entry check sits on,
regression-testing the fix; and it confirmed the v1.1.7 ring LED going
green→red on arm and back on abort, following state rather than the button.

**T-F04 and T-F05 recorded as covered by existing evidence** rather than re-run
— T-A17 already produced `NACK 0x05` for a fire command outside ARMED, and
T-A16 already proved continuity is live during ARMED.

**Three tests cannot be run as written**, found by timing analysis before
attempting them:

- **T-F06 (link lost during firing) is unreachable, and it is the interesting
  one.** The pulse is 1000 ms; link loss needs 3 missed pings = 1500 ms. Stop
  the remote before the transition and the dead-man aborts instead of firing;
  stop it at or after, and the pulse has finished before the base can notice.
  **There is no window in which link loss can be observed during FIRING** — which
  makes `COMPLETE_PULSE_ON_LINK_LOSS` and the C1 `s_link_lost_pending` logic
  unreachable config. Operator decision: leave the pulse at 1 s, since it suits
  the igniters and a fire-path constant should not be changed to suit a test.
  Verify that logic by review instead.
- **T-F07 (dead-man timeout)** needs `CMD_FIRE` to stop while the link stays up.
  Releasing the button sends CEASE_FIRE (that is T-F02); killing the remote
  stops its pings too, so link loss trips first. Needs a remote-side injection.
- **T-F09 (PONG missed at the transition boundary)** — same shape.

All three want a **remote-side** injection harness; the existing one is
base-only.

**Still to run:** T-F01 (full sequence), T-F03 (release mid-pulse) and T-F08
(pulse timing, needs a scope). All three fire. Deferred to a fresh session
rather than run at the end of a long one.

### Still owed

- ~~T-A11 and T-A13~~ — **DONE, both PASS.**
- **G3 firing tests: T-F01, T-F03, T-F08.** All three fire; T-F08 needs a scope.
  Channels 2–8 have never been fired.
- **A remote-side injection harness** would close T-F06, T-F07 and T-F09, plus
  positively trigger the bug #30 backstop.
  Channel 1 has now fired once, unplanned, and the whole chain worked.
- **Bug #24 — no rail clamp on the base.** Unchanged this session, and the
  highest-consequence item left: three ESP32s have died on that unit via the
  3.3 V rail.

### Superseded during the session

- ~~T-A16 / T-A17 / T-A18~~ — **DONE, all PASS.**
- ~~Siren bench tests~~ — **DONE, all six PASS. N2 closed.**
- ~~Re-verify the hardware AND gate at the node~~ — **DONE, all seven steps
  PASS** (see the tooling section above).
- ~~Measure the siren's steady current~~ — **done: under 200 mA.**

---

## 2026-08-25 — External antennas on both units, 200 m range test, link-budget analysis

### Hardware

Both 0 Ω antenna links (base and remote) moved to the **external antenna**
position and external antennas fitted. No firmware change — the DevKitC-1 has
no RF switch.

### Range test

Base on the ground, remote hand-held at ~1.5 m, 200 m separation:
**−93 dBm, link holding.** Still held with the remote also on the ground. The
link could only be dropped by unscrewing an antenna.

The test did **not** find the edge — unscrewing an antenna is a 20–30 dB step.
To locate it, walk out until `PING miss` first appears in the base log and
record the RSSI at the *first miss*, not at the drop.

### Expected drop-out threshold: −96 to −99 dBm

Nothing in the tree calls `esp_wifi_config_espnow_rate()`,
`esp_now_set_peer_rate_config()` or `esp_wifi_set_protocol()`, so ESP-NOW runs
at the IDF default — 1 Mbps DSSS, where the ESP32-S3 is sensitive to ~−98 dBm
at 8–10 % PER.

The link survives past that because `tick_base()` needs **3 consecutive**
missed 500 ms slots. With per-packet loss p, a drop needs p³:

| PER | P(3 in a row) | Time to drop |
|---|---|---|
| 10 % | 0.001 | ~8 min |
| 30 % | 2.7 % | ~20 s |
| 50 % | 12.5 % | seconds |

So failure lands 1–3 dB below sensitivity, across a transition only ~3 dB wide
— abrupt, with little warning. `ERR_COMM_DEGRADED` (>30 % loss over the 10-slot
window, guard 10 → NACK `COMM_DEGRADED`) refuses arming a couple of dB earlier,
which is the correct ordering.

**The RSSI reading is not a usable warning:** uncalibrated (±3–6 dB), compressed
in the high −90s so the last few dB may never show, and smoothed/lagged by
`RSSI_AVERAGE_WINDOW = 3`. Also note the base displays *its* view of the
remote's packets; the weaker direction sets the limit and need not be the one
shown.

### Key finding — the range limit is height, not radio

−93 dBm at 200 m is **29 dB worse than free space** (86 dB FSPL, ~+19 dBm TX,
~2 dBi per end → ≈ −64 dBm expected). That deficit is two-ray ground
reflection. With h₁ ≈ 0.05 m and h₂ ≈ 1.5 m the breakpoint is ~2.5 m, so the
path is deep in the **d⁴** regime:

```
PL = 40·log₁₀(200) − 20·log₁₀(0.05 × 1.5) = 114.5 dB  →  ≈ −92.5 dBm
```

versus −93 dBm measured. Consequences:

- The ~4 dB of margin left buys only ~25 % more distance — **~250–260 m in that
  geometry**, not 400 m.
- Raising both units to ~1.5 m moves h₁h₂ from 0.075 to 2.25 — about **30 dB**,
  restoring the free-space limit and pushing drop-out past a kilometre.

**Getting the base off the ground is worth more than any radio change available
to this design.**

### Files changed

| File | Change |
|---|---|
| `RLC_Functional_Specification_v1_14.md` | → v1.33: §6.1 measured range, drop-out window, antenna row; revision entry |
| `Development_Progress.md` | Antenna section rewritten with the range test, the d⁴ analysis and the threshold reasoning |
| `README.md` | Range line now cites the measurement and the height caveat |

No firmware changes this session.

---

## 2026-08-23 — Sense resistors fitted: thresholds recalibrated, fire gate opened, two hardware bugs

### What was done in hardware (by the operator, before this session)

| Change | Effect |
|---|---|
| 217 Ω sense-branch resistors on **all 8** continuity channels | Closes both bug #18 gaps — arc into the 3V3 rail limited to ~41 mA, pin held at ~3.55 V |
| 1N5819 clamps confirmed on CH7/CH8 | Clamp coverage complete (already recorded 2026-08-21) |
| LED 4 on the base strip removed, LED from position 8 fitted in its place | Symptom unchanged — bug #19 |
| 0 Ω link moved from PCB antenna to U.FL; external antenna fitted on the base ESP | No firmware change needed |

### 1. The 217 Ω broke continuity classification, silently

The resistor sits **in the sense current path** (R_ref → [ADC pin + clamps +
R_pull] → 217 Ω → relay NC), so every reading rose by ~204 mV. With the old
thresholds every connected channel read `MARG` and **nothing could ever read
CONNECTED** — `cont=0x882a` on the pre-fix capture. Arming would still have
worked (only OPEN blocks), which is exactly the silent degradation the
2026-08-21 note predicted for this part.

Constants re-derived from `V = 3.3 × Rx/(R_ref + Rx)`, `Rx = (217 + R_ign) ∥ 100 kΩ`,
keeping the same physical boundaries:

| Constant | Was | Now | Boundary |
|---|---|---|---|
| `CONT_MARGINAL_UV` | 66000 | **261000** | unchanged, ~67 Ω |
| `CONT_OPEN_UV` | 432000 | **586000** | unchanged, ~500 Ω |
| `CONT_R_SENSE_OHM` | — | **217** | new |

**Verified on target** against the operator's known loads. Predicted vs measured
agreed within ~2 mV across the whole range, and R_sense back-calculated from the
three resistors gives 216–219 Ω against a part marked 217 Ω:

| Ch | Load | Predicted | Measured | Band |
|---|---|---|---|---|
| 1 | Amazon fireworks igniter | 205 mV | 205–208 | CONNECTED |
| 2 | 14.9 Ω | 216 mV | 215–219 | CONNECTED |
| 3 | 74.3 Ω | 267 mV | 269–271 | MARGINAL (correctly — just over 67 Ω) |
| 4 | 2k16 | saturates | 969 (4095) | OPEN |
| 5 | 4k28 | saturates | 969 (4095) | OPEN |
| 6 | Klima igniter | ~205 mV | 205–209 | CONNECTED |
| 7 | nothing | 3.19 V | 969 (4095) | OPEN |
| 8 | Amazon fireworks igniter | ~205 mV | 203–205 | CONNECTED |

`cont=0x4425` after the change — all eight correct.

**The boot self-test caught the mismatch and halted the base.**
`test_continuity_classification()` carries hardcoded µV vectors; changing only
the constants failed three of them at boot and the base refused to run rather
than operating with a mismatched pair. Vectors updated in the same change.
Second time those vectors have earned their keep.

**Side fix:** the 5 s `cont raw/uV:` diagnostic line silently dropped channel 8
— `cbuf[160]` holds exactly eight entries and the loop guard (`n < size − 24`)
stopped at seven. Buffer raised to 208.

Full scale (950 mV) is now reached at ~1117 Ω instead of ~1670 Ω. Nothing
measurable was lost: everything above 500 Ω is OPEN.

### 2. `FIRE_PROTECTED_CHANNEL_MASK` widened 0x01 → 0xFF

By explicit operator decision, closing the gate that has restricted fire testing
to channel 1 since 2026-07-21. The protection BOM is complete on every channel:
RC snubbers (8 channel relays + arm relay), 2× 1N5819 per sense pin, 217 Ω
sense-branch resistor per channel. The 217 Ω is what tipped it — it is the part
that keeps the pin inside the 3.6 V absolute maximum during a fault.

`relay_init()` no longer emits the `bug #18 gate ACTIVE` warning; its absence at
boot confirms the mask. Still outstanding: the TL431 rail clamp (bug #24), now
covering the multi-channel fault case rather than the single-channel one.

**Channels 2–8 have still never been fired.**

### 3. Bug #28 (NEW) — ARM RELAY LED lights with the key in SAFE

Turning the base key to SAFE illuminates the red ARM RELAY LED (the one across
the arm relay coil, FSD §5.4.4) while the relay itself stays de-energised.
Firmware in IDLE, so GPIO 47 is not driven; the firmware's own arm sense
(GPIO 21, reading the fire bus rather than the LED) reported `arm=0` throughout.

An LED lighting while the relay stays out means a path delivering a few mA where
the coil needs ~150 mA — a **high-impedance sneak path around the intended one**.
Candidates and the measurements that separate them are written up in
`Development_Progress.md`. Most likely indicator wiring only, but the key switch
is one of the two independent legs of the hardware AND gate, so **fire testing
is on hold until it is resolved.**

### 4. Bug #19 update — the LED swap did not fix it

Pixels 1–3 still render, 4–8 dark. A replacement part at the same position
reproducing the same break points at the data path into pixel 4 (copper or
joints) rather than the LED. Two caveats before calling it a trace fault: the
donor LED came from position 8, which never lit, so it was never proven working;
and a hand-reworked pad is a likelier open than factory copper. Next step is a
DVM continuity check from pixel 3 DOUT to pixel 4 DIN. T-L18 stays FAIL.

### 5. External antenna — not yet proven

Base RSSI reads −33 to −38 dBm after the change, against −36/−37 on the same
bench earlier the same day. Inside run-to-run spread; proves nothing at bench
distance. Needs an LOS range test — a misplaced 0 Ω link looks healthy on the
bench and fails in the far field.

### Files changed

| File | Change |
|---|---|
| `components/rlc_common/include/rlc_config.h` | `CONT_R_SENSE_OHM` added; two thresholds rebased; mask 0x01 → 0xFF |
| `components/rlc_common/src/rlc_selftest.c` | Classification vectors rebased to the new boundaries |
| `components/rlc_base/src/rlc_base_main.c` | `cbuf` 160 → 208 (ch8 was dropped) |
| `RLC_Functional_Specification_v1_14.md` | → v1.32: recalibration, mask widening, bug #28 |
| `Development_Progress.md` | Bug #28 added; bugs #18/#19 updated; three new dated sections |
| `README.md`, `rlc-hw-test-base/RLC_Base_Hardware_Test_Specification.md` | Stale thresholds and channel-1-only text corrected |

### Verification

- Both units rebuilt and reflashed, no warnings.
- Base running: self-tests pass, all 8 channels classify correctly, link up
  (`rssi=-38`, `txfail=0`), no watchdog trips.
- Host renderer tests: 30 checks, 0 failures.

### Notes / gotchas

- **Any change to the sense-branch resistor requires re-deriving both thresholds
  *and* the self-test vectors.** Changing only the constants bricks the boot.
- The base's UART console needs an RTS-toggle reset after flashing before it
  starts emitting; opening the port with `stty` alone can leave it silent.

---

## 2026-08-21 — Post-fix re-review, firmware 1.1.1, both units flashed and tested

### What ran

Second full-codebase review of the day, against commit 28293b6 (the fix commit
for the 14:30 review). Written up as
`Code_Review_AllPhases_20260821_1523.md`. Then: fixes applied, documentation
updated, both units built and flashed, on-target testing.

### Review result

**All seven prior Majors (2.1–2.7) verified fixed in source**, along with the
large majority of the 32 minors. The classifier and arm-state extractions into
`rlc_common` close the duplicate-copy anti-pattern (Phase-2 M2) that had
survived three review rounds.

**Two new Majors**, both introduced or left standing by the fix commit, neither
on the ignition path:

- **N1 — the arm key being ON at power-up was never registered.** The debounce
  fix suppressed the first stable reading's callback (correct — the fire button
  needs that for its fresh-press interlock), but the remote's arm switch cached
  its state *only* from that callback. Key already turned to ARM at boot →
  `arm_switch_is_armed()` false forever, LED dark, every long-press refused
  with "TURN ARM KEY FIRST", recoverable only by toggling the key. That is the
  ordinary flow of turning the key before powering up. Fails safe, but the arm
  path was dead.
  Fixed per-consumer (the suppression itself must stay): the arm-switch task
  and the base's arm-sense/key-sense tasks now adopt the debounced state by
  polling `rlc_debounce_get_state()`. FSD §5.3.1 states both halves of the
  contract; T-D01…T-D06 pin them from both sides.

- **N2 — the siren mutex added last round was only half a fix.**
  `esp_timer_stop()` does not cancel an already-dispatched callback, so a stale
  tick could still (a) drive the siren back ON after `siren_off()` with the
  timer stopped — permanently, or (b) read the PRE_FIRE pattern's zero cycle
  count as "finished" and silence the siren for the whole 2 s countdown. (b)
  opens on every launch, since ARMED→PRE_FIRE is always preceded by a running
  500 ms timer. Fixed with a pattern-active flag; FSD §12.3 records why the
  mutex alone is not enough.

### N3 — found by flashing, not by reading (CRITICAL)

Neither static pass caught this. Both verified that every task self-registers
with the TWDT (5.10/5.11); neither asked *when the TWDT is reconfigured
relative to those registrations*.

`esp_task_wdt_reconfigure()` rebuilds the subscriber list. The remote called it
at boot step 8, after `display_start_task()` — so the display and buzzer tasks
were silently unsubscribed, the display logged `task not found` at 20 Hz, the
unfed watchdog triggered at 11.4 s, and **the trigger handler panicked**
(`LoadProhibited`) walking its stale entries. The remote rebooted every 11.4 s,
on every boot. It also voided the watchdog coverage fix 5.10 had added to those
two tasks, so a hung SPI transaction could still freeze an "ARMED" screen
forever.

Ironically, 5.11 (self-register at task entry) is what made this certain rather
than intermittent — it moved registration earlier, ahead of the reconfigure.
The base was clean only by accident, calling `rlc_watchdog_init()` before any
task started.

Fixed by splitting the function: `rlc_watchdog_init()` reconfigures only and is
now the first statement of `app_main` on both units (FSD §9.13 step 0);
`rlc_watchdog_register_self()` subscribes `app_main` just before its
housekeeping loop (step 11), kept separate because the SPI/NVS/Wi-Fi/peer-retry
init between them can exceed the 5 s timeout on its own.

### Minors fixed

m1 remote battery-critical in LINK_LOST now terminal (matching the base) plus
LINKING handling EVT_LINK_LOST · m2 ping health window + expected-slot tracker
reset with the session, ending the spurious ~5 s `COMM_DEGRADED` arm block
after every link recovery · m3 stale-status timeout latches instead of
re-firing at 20 Hz · m4 `firing_exit()` invariant comment corrected, POST_FIRE
double-ERROR collapsed · m5 backstop stops the GPTimer · m6
`rlc_link_next_seq()` drops the link on overflow, both base call sites check ·
m7 send failures counted not logged from Wi-Fi context, surfaced as `txfail=`
in both status lines · m8 fabricated handshake STATUS_UPDATE (`base_state =
IDLE`, a false "safe" from a base in ERROR) replaced with a real push via a new
application hook · m9 five unchecked `xTaskCreate` calls checked, two fatal ·
m10 `SIREN_LINK_LOST_DURATION_MS` derived instead of dead · m11 `s_bands[]`
volatile · m12 `ARM_SENSE_VERIFY_TIMEOUT_MS` named · m13 interrupted ARM
re-syncs the channel selection · 5.7 remote input callbacks registered before
their tasks start · base strip initialised before the self-tests so a
self-test halt is actually visible.

### Firmware version

**1.1.0 → 1.1.1.** Arm-path behaviour changed on both units, so the strict
version gate does real work here: a half-flashed pair refuses to link rather
than running mismatched safety logic. Flash base and remote together.

### Tests

Host suite grown from 10 binaries / 217 checks to **12 binaries / 265 checks**,
all passing. New `tests/host/test_debounce.c` compiles the real engine — the
harness had been stubbing it out, which is part of why N1 was invisible. The
stub is deleted and `test_encoder.c` now links the real debouncer too.

### Documentation

FSD → v1.31: new §5.3.1 (debounce initial-state contract, both rules as
requirements), §12.3 pattern-cancellation requirement, §6 handshake
STATUS_UPDATE tightened from description to requirement, §7.2.2 arm-verify
window named and its non-blocking cancellation semantics spelled out, §14.1
gained `ARM_SENSE_VERIFY_TIMEOUT_MS` and `FIRE_PULSE_BACKSTOP_MARGIN_MS`,
§15.5 gained T-D01…T-D06 and had the stale `CONT_GOOD`/`SHORT` band names in
T-U10/T-U12 corrected to the three-band scheme, and §9.13 gained the N3 TWDT
ordering requirement as steps 0 and 11. README and Development_Progress updated
to match.

### On target

Board identity confirmed by MAC before flashing (base `44:1b:f6:81:f1:70` on
`...5B5E042156`, remote `ac:a7:04:e2:f2:8c` on `...5B5E043219`), both matching
the configured peers. Both units flashed.

Verified: 12/12 boot self-test suites on each; **zero TWDT errors, panics or
unexpected reboots over 45 s continuous on both** (against a remote that was
rebooting every 11.4 s before the N3 fix); link on LINK_REQUEST attempt 1,
LINKING→IDLE in 40 ms; link-loss detection at 1548 ms and recovery 880 ms later
with both FSMs following; bidirectional fw 1.1.1 version check; base 12.34 V /
remote 8.00 V, RSSI −24, `missed=0`, `txfail=0`.

### Operator bench tests

**PASS:** N1 (arm key turned to ARM *before* power-up — LED lights, long-press
accepted; this is the regression that made 1.1.0's arm path unusable) and a
full arm → pre-fire → fire → post-fire cycle on channel 1 with the display's
four-state BASE field tracked through it.

**Open — N2 is not verified on hardware.** The siren output (GPIO 40) is not
connected on the base, so while the ARMED→PRE_FIRE→FIRING sequence ran
correctly, the audible behaviour was not observed, and the "siren stuck on
after disarm" test could not be run at all. Both N2 failure modes are silent
ones, which is exactly why a disconnected output cannot exercise them. The N2
fix rests on code inspection until the driver is fitted (IRLZ44N low-side per
FSD §5.4.8/§5.4.10, 150 Ω gate series + 10 kΩ gate pull-down for boot safety,
flyback diode across the coil) and tests 2 and 3 are re-run.

### Commit

`f76ff2d` — 26 files, +1457/−141. Firmware, host tests, review document and all
four doc files in one commit.

### Session closeout — bug #27 raised, two FSD defects fixed

Answering "what pin does the siren go on?" exposed that **§5.4.8 Siren Output
never stated its GPIO** — the parameter table gave signal type, quantity,
function and driver, but not the pin, while its sibling §5.4.9 names it right
in the heading ("Arm Relay Output (GPIO 47)"). The number was only recoverable
from Appendix C.1 or `pin_config.h`. That is a documentation defect in the one
section someone reads *while holding a soldering iron*. §5.4.8 now leads with
GPIO 40, states base-only scope, and carries the full gate network (150 Ω
series, 10 kΩ pull-down, flyback diode), the boot-safety rationale, and the
GPIO 40 = MTDO note.

Also found and fixed while in there: **a stale cross-reference** — the
continuity ADC table cited "same as battery ADC, **§5.4.8**", which is the
siren. Battery ADC is §5.4.7. Almost certainly a leftover from the v1.12
section renumbering.

**Bug #27 opened: base siren not connected.** Promoted out of the session
narrative into the Open Bugs index, because it is not merely a test-coverage
gap. Beyond blocking N2 verification, it means **the pad has no audible warning
at all** during ARMED/PRE_FIRE/FIRING — every siren pattern the firmware
produces goes nowhere. The remote's buzzer is operator feedback in the
operator's hand and is in the wrong physical location to substitute. That is a
safety-function gap independent of any firmware finding, which is why it now
has a number and a detailed entry with the drive circuit.

Added a bug #27 section to `docs/Gotron_Shopping_List.md` listing what is
needed (IRLZ44N, 150 Ω, 10 kΩ, flyback diode, the sounder). **Part codes and
prices deliberately left blank** — every other row in that document carries a
verified Gotron code, and guessing would be worse than an obvious gap. Noted
that `RC10K` is already on the order for the TL431 divider, so that line may
only need its quantity raised, and that the design already calls for 10
IRLZ44N so the parts bin is worth checking first.

---

## 2026-08-21 — All findings from Code_Review_AllPhases_20260821_1430.md fixed

### What ran

Fix session against every Major (2.1–2.7) and every minor (4.5–4.15,
5.3–5.16, §6, §7) in the all-phases review. Known-open items (NACK 0x0C
guard, palette, CONT_MARGINAL 67 Ω value, bug #18 mask 0x01, crypto keys)
were deliberately left as tracked. Verified end-to-end: host tests
(10 binaries × 2 units, all pass) and full builds of both units
(`./build_base.sh`, `./build_remote.sh`, entry points verified in binaries).

### Majors — all 7 fixed

| # | Fix |
|---|---|
| 2.1 | DISARM during arm-verify now calls `abort_arm_verify(NACK_WRONG_STATE)` before the idempotent ACK (`rlc_base_fsm.c`) |
| 2.2 | New `firing_exit(safe_state)` helper funnels all six FIRING exits; `ERR_VBAT_CRITICAL` latched during FIRING → terminal ERROR (FSD §7.2.5) |
| 2.3 | Continuity ADC failure now fails safe to OPEN: `CONT_SAMPLE_FAILED` sentinel, per-channel `s_adc_configured` + `s_adc_failed` latches, one-shot logging (`rlc_continuity.c`) |
| 2.4 | Remote arm-key interlock: `wait_for_ack` returns `WAIT_FOR_ACK_INTERRUPTED (-4)` on key-off/button/encoder; ARM retry loop re-checks `arm_switch_is_armed()`; "Guard 0" on FIRE paths; key-off after ACK sends DISARM/CEASE_FIRE; fire-repeat gated on key (`rlc_remote_fsm.c`) |
| 2.5 | Classifier deduplicated: new `rlc_common/src/rlc_continuity_class.c` (+ header) holds the production three-band classifier; `rlc_continuity.c` and `rlc_selftest.c` both call it |
| 2.6 | Wire-receive timestamp stamped in the ESP-NOW recv callback (`esp_timer_get_time()/1000`), threaded through `rlc_link_on_rx(received_ms)` — never re-stamped |
| 2.7 | Send-failure handler only latches a flag in WiFi-task context; consumed in link_task (duplicate ESP_LOGE removed) |

### Minors — highlights

- **4.5–4.7** FIRING pulse backstop timer (`FIRE_PULSE_BACKSTOP_MARGIN_MS`
  250); key-off and switch-off in IDLE abort pending arm-verify; key-sense
  guard in ARMED EVT_CMD_FIRE.
- **4.8–4.11** remote: ACK/NACK correlation via `s_pending_cmd_type/_seq`;
  IDLE reconcile sends DISARM(0xFF) if base still armed; stale-status
  timeout sends CEASE_FIRE in PRE_FIRE/FIRING.
- **4.12/4.13** fire-button "fresh press" API deleted (edge-triggered events
  already guarantee it, documented in header); relay `gpio_config` failure is
  fatal at boot.
- **5.3–5.16** J4-style blocking queue sends for callbacks; siren mutex;
  arm-sense weld-fault single-shot latch; link-state fixes (overflow → LOST,
  remote version check → VERSION_MISMATCH, locked state+token read,
  LINK_REQUEST slow retry interval); **TWDT self-registration everywhere**
  (`esp_task_wdt_add(NULL)` at task entry — `rlc_watchdog_add_task()`
  deleted); buzzer/display TWDT coverage; ISR decoder moved to
  `IRAM_ATTR encoder_feed()`; encoder task creation checked;
  `CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y` in sdkconfig.defaults (confirmed set in
  both resolved sdkconfig.h files).
- **§6** `adc_cali_raw_to_voltage` checked (fallback estimate instead of
  0 mV); debouncer initial determination fires no callback (was a spurious
  "released" at boot from the 0xFFFF seed); `test_tr04.py` exits 1 on
  FAIL/INCOMPLETE.
- **§7** dead APIs deleted: `remote_fsm_get_armed_channel_ptr`,
  `remote_fsm_stop_fire_repeat`, `remote_fsm_is_fire_repeat_active`,
  `remote_battery_get_status`, `base_state_get_continuity` (returns-0 trap),
  empty `display_main_status()` stub. New shared `rlc_common`
  `rlc_arm_state.h/.c` — display and `test_armstate.c` both use the real
  source (test `#include`s the `.c`; mirror removed). Buzzer header timings
  corrected to actual step tables; `CONT_MARGINAL_UV` comment ~20 Ω → ~67 Ω;
  idf floor `>=5.4`.

### Compile-breaker caught during the minors batch

`rlc_remote_battery.c` still called the deleted `rlc_watchdog_add_task()`
(5.11 batch had missed the creator-side call there). Removed; task creation
now checked, dead `s_status` static became a local.

### Deliberately deferred (low risk, documented)

4.14/4.15 (seq-0 replay window, LINK_REQUEST replay DoS), 5.16 (battery clip
bias — log-only), ESP-NOW deinit race (never-called path).

### Verification

- `./tests/host/run.sh` — all pass (test_encoder skipped on BASE by design).
- `./build_base.sh` / `./build_remote.sh` — clean, `base_app_main` /
  `remote_app_main` verified in binaries.
- **Not flashed** — new images are in `build_base/rlc.bin` and `build/rlc.bin`
  awaiting a decision to flash (by-id ports per global rule).

## 2026-08-21 — Full-codebase review + documentation audit (no code changes)

### What ran

`/codereviewer all code` at commit cd4ddf0, plus a parallel documentation
consistency audit. Read-only review: three parallel review agents (base /
remote / common+tests+tools) fed by a context agent, every Major finding
re-verified in source before inclusion. Host tests executed during the review:
10 binaries, 217 checks, all pass.

**Output:** `Code_Review_AllPhases_20260821_1430.md` — verdict **MAYBE**, with
a summary also added to `Development_Progress.md` and the README docs table.

### The four findings that gate live-fire

1. **DISARM during the 200 ms arm-verify window is ACKed "already safe" but
   does not abort the pending ARM** (`rlc_base_fsm.c:362`). The base can
   complete IDLE→ARMED after the remote displayed "disarmed". CEASE_FIRE in
   the same window does call `abort_arm_verify()` — DISARM must too.
2. **The remote can command FIRE with the arm key OFF** — three compounding
   gaps in `rlc_remote_fsm.c`: `wait_for_ack()` swallows the switch-off event,
   the ARM retry loop re-sends without re-checking `arm_switch_is_armed()`,
   and the ARMED fire guards never check the key. The hardware key-in-coil-path
   AND gate still blocks actual firing, which is why this is Major, not
   Critical.
3. **Continuity ADC read failure classifies as CONNECTED** —
   `sample_channel()` returns 0 on any `adc_oneshot_read` failure and 0 µV is
   the only arming-permitting band (`rlc_continuity.c:111`). Fails permissive;
   must fail to OPEN (or hold the previous band).
4. **`ERR_VBAT_CRITICAL` latched during FIRING is dropped on the operator-abort
   exits** (CEASE_FIRE / arm-sense-lost / key-off / DISARM), then detonates as
   a spurious power-cycle-terminal ERROR at the *next* POST_FIRE entry
   (`rlc_base_fsm.c:558, 605`).

### The other three Majors (assurance / infrastructure)

- Boot self-test still runs a **copy** of the continuity classifier
  (`rlc_selftest.c:485`) — the Phase-2 M2 anti-pattern, now including the
  rewritten three-band vectors; `test_armstate.c` mirrors display logic the
  same way.
- **C3 timestamp is not captured in the ESP-NOW callback** — it is stamped in
  the worker task behind two queues (`rlc_link.c:803`) while three comments
  claim otherwise; it feeds the 500 ms dead-man window.
- **Send-failure handler blocks the WiFi task** — portMAX_DELAY state mutex +
  10 ms queue send from `espnow_send_cb` context (`rlc_link.c:626`), an ABBA
  shape against `link_task` at the exact moment the link is failing.

32 minors and 14 infos are in the review doc, including: `rlc_link.h` documents
the busy-guard polarity inverted vs. the implementation; display and buzzer
tasks are not TWDT-registered (a hung SPI transaction freezes "ARMED" on screen
forever); root cause found for the known TWDT "task not found" boot bursts
(tasks reset before their creator registers them); remote selected-channel can
go stale vs. the encoder after LINK_LOST. Every Phase 1–3 review fix was
re-verified present except M2 above.

### Documentation audit highlights (30 findings)

Dangerous: the **remote** hw-test spec's flash command targets the **base**
board's by-id (`RLC_Remote_Hardware_Test_Specification.md:339`, MAC
44:1B:F6:81:F1:70 is base chip #4); the **base** hw-test spec wires the LED to
**GPIO 47 — the arm-relay pin** (`RLC_Base_Hardware_Test_Specification.md:241`).
The FSD's v1.29 three-band rollout missed ~13 spots (§3 glossary four-band text,
§14.5 still 1500000 µV, §7.2.4 "2000 ms" pulse, 15-vs-5 link retries, §10.2.2
SHORT glyph, T-A15/T-U10/T-U12). `Phase3_Code_Review_002.md` still stands at
FAIL with no closure note though all its fixes landed. Full list in the review
doc §8 cross-reference; none fixed this session.

### Notes

- No code was modified this session — review artifacts and doc pointers only.
- The 220 Ω sense-resistor / TL431 rail-clamp analysis (FSD v1.30,
  `Development_Progress.md`, shopping list) was authored outside this review
  session and committed as found.

## 2026-08-21 — Continuity: three bands, SHORT merged away as unmeasurable

### The decision

`CONT_SHORT` is merged into `CONT_CONNECTED`, and `CONT_GOOD` is renamed
`CONT_CONNECTED`.

Three controlled experiments compared a real igniter against a deliberate short
on CH1:

| Experiment | Separation | Implied igniter R |
|---|---|---|
| back-to-back | 6.78 counts (t=7.7) | 1.77 Ω |
| across power cycles | 2.93 counts (t=2.2) | 0.77 Ω |
| both states stable | 4.42 counts (t=7.1) | 1.15 Ω |

The igniter is 1.5–1.9 Ω on a DVM. The effect is real and always correctly
signed, but its **magnitude varied 2.3× for a setup that never changed**. At
0.888 mV/Ω the signal, the noise, the shorting lead's own contact resistance and
run-to-run drift are all ~2–6 counts. A midway threshold misclassifies 19 % of
single readings.

**A band that cannot be measured must not be reported.**

### The rename

`GOOD` asserted igniter health the measurement cannot support, and was actively
wrong on a shorted pair of leads. `CONNECTED` states only what is known: a
low-resistance path exists.

The 2-bit wire encoding is **unchanged** — value 3 stays in the enum and still
decodes on display, it is simply never emitted — so no protocol version bump was
needed and a pre-merge peer still interoperates.

### The 220 Ω offset trial, and why it was removed

Fitted on CH1, then removed. It did exactly what was predicted: CH1 became
stable at 206 mV with 3 counts of noise, matching the 209 mV model to within
3 mV, curing the ADC's low-end collapse. But it added no resolution, because
lifting a short and an igniter by the same amount does not separate them.

Worth recording because the reasoning generalises: **an offset buys linearity,
only current buys resolution.**

Raising the current to ~3.4 mA would have made the band genuinely measurable
(~14.5 counts). Declined: it cuts the no-fire margin from 50× to 15× for a band
that never blocked arming.

### As-built hardware

| Item | Status |
|---|---|
| RC snubbers, all 8 channel relays + arm relay | **FITTED** |
| 1N5819 clamps to GND and 3V3, CH1–CH6 | **FITTED** |
| 1N5819 clamps, CH7–CH8 | Pending parts |
| Continuity ground return | Repaired, grounds within 0.3 Ω |

Most of the bug #18 protection BOM is now in place. **`FIRE_PROTECTED_CHANNEL_MASK`
remains `0x01`** and now understates the hardware — channels 1–6 have clamps and
snubbers. Widening it to `0x3F` is a fire-path safety gate change and was left
for explicit confirmation rather than assumed.

### Code

Enum, classifier, hysteresis, self-test vectors and mirrors, display labels,
glyph table and legend all updated. The diamond glyph that marked SHORT is
removed along with its `fill_diamond()` helper. FSD **v1.29**. Full host suite
green.

**Caught during verification:** two private label tables in `rlc_base_main.c`
(the 1 Hz TRACE line and the 5 s continuity line) still read
`{ "OPEN", "GOOD", "MARG", "SHRT" }` after the enum rename, so the base log
contradicted the remote display. Both now read `CONN`, with index 3 — the
deprecated SHORT slot — also mapping to `CONN` so a stale cached band cannot
print a name the system no longer uses.

### On-target verification

All eight channels were confirmed classifying correctly under the three-band
scheme before the label fix:

| CH | Load | Reading | Band |
|---|---|---|---|
| 1 | Amazon igniter | 4 mV | CONNECTED |
| 2 | 14.9 Ω | 11-13 mV | CONNECTED |
| 3 | 74.3 Ω | 68-70 mV | MARGINAL |
| 4 | 2k16 | saturated | OPEN |
| 5 | 4k28 | saturated | OPEN |
| 6-8 | igniters | 3-4 mV | CONNECTED |

**8/8 correct** — up from 2/8 at the start of this investigation.

### Both units were halting at boot — caught and fixed

The silence after the label fix was **not** the bench being powered down, as
first assumed. Both units were halting in `rlc_selftest`:

```
E FAIL: hysteresis GOOD->SHORT transition — got 1, expected SHORT
E FAIL: hysteresis SHORT stability — got 1, expected SHORT
E self-tests FAILED — halting
```

The three-band merge updated the hysteresis *classifier* and its mirror, but
left `test_continuity_hysteresis()`'s **vectors** still asserting SHORT
transitions. The self-test did exactly its job — refusing to run firmware whose
classifier and its own expectations disagree.

Two things had masked it. The halt is silent by design, and the 2026-08-21 fix
that made `rlc_rgb_led_set_pattern()` safe before init turned what would have
been a visible reboot loop into a clean, quiet halt. And pyserial asserts DTR on
open, which on this CH340 auto-reset circuit held EN low — opening with
`dtr=False, rts=False` was needed to see any output at all. Worth remembering:
**a silent board is not necessarily an unpowered one.**

Vectors rewritten for three bands, covering the two boundaries that are
actually measurable (CONNECTED↔MARGINAL, MARGINAL↔OPEN) plus a new case
asserting that a deprecated SHORT value from a pre-merge peer folds into
CONNECTED rather than persisting.

### Final on-target verification

Both units reflashed, all self-tests passing, linked at −24 dBm.

| CH | Load | raw | Band | |
|---|---|---|---|---|
| 1 | 0.1 Ω | 11 | CONNECTED | ✓ |
| 2 | 14.9 Ω | 45 | CONNECTED | ✓ |
| 3 | 74.3 Ω | 282 | MARGINAL | ✓ |
| 4 | 2k16 | 4095 | OPEN | ✓ |
| 5 | 4k28 | 4095 | OPEN | ✓ |
| 6 | Klima igniter | 13 | CONNECTED | ✓ |
| 7 | Amazon igniter | 10 | CONNECTED | ✓ |
| 8 | Amazon igniter | 9 | CONNECTED | ✓ |

`cont=0x5425`. **8/8 correct**, against 2/8 when this investigation began.

### Documentation audit

The three-band merge left several places still describing four:

- `Development_Progress.md` LED strip colour table (GOOD/SHORT rows and the
  `RLC_COLOR_CONT_GOOD` constant name), and the shape-coding note that still
  listed the retired diamond.
- FSD §6 protocol field table — the `continuity_bands` wire-value legend still
  read `01 = GOOD, 11 = SHORT`. Now records CONNECTED, marks 11 as deprecated
  and never emitted, and states explicitly that the encoding is unchanged so no
  version bump was required.
- FSD §10.2.2 channel-grid description and the §11 colour table's orange SHORT
  diamond entry.

**Three pending Phase 2 tests were closed by this session's work**, having been
open since April: B2-C07 (CH2–CH8 individual loads) and B2-C09 (MARGINAL
classification) both PASS from the eight-channel verification, and B2-C08
(SHORT classification) is retired along with the band itself.

Left alone deliberately: `archive/` holds superseded spec revisions,
`Phase2_Code_Review.md` records what was true when written, and the
`rlc-hw-test-base` spec describes standalone bring-up firmware with its own
band implementation that does not share the RLC enum.

## 2026-08-20 — Encoder rotation sense; strip tests were committed red

### Rotation reversed

The v1.27 decoder moved the channel selection opposite to the knob. Which way a
KY-040 counts depends on how A and B are wired to the MCU, so this is a board
property rather than a decoder property — added **`ENC_REVERSED`** (1 as built),
the same treatment `RLC_STRIP_REVERSED` gets. Applied by negating the direction
before the divider accumulator, so the divider and reversal logic are untouched.
Host test **T-Q07** pins the sense in both directions, so a rewire has to update
the constant rather than silently inverting the operator's controls.

### Two strip tests had been failing since the previous commit

Found while running the suite untruncated. The dirty-frame optimisation gave the
driver hidden state — a shadow of the last transmitted frame — and
`test_strip.c`'s `reset()` did not clear it, so the driver correctly skipped
writing pixels it believed were already correct while the mock's output buffer
had been zeroed underneath it.

A harness gap rather than a firmware defect: nothing in firmware clears the
strip behind the driver's back, and the shadow starts invalid at boot so the
first frame is always transmitted.

**They were committed red, and the reason is worth recording:** the test output
was filtered through `| head -16`, which cut off exactly the two binaries that
broke. Run the suite untruncated before committing.

Full suite now green: **217 checks across 10 binaries** (5 test files x 2 unit
builds). FSD **v1.28**.

*(Correction: this was first recorded, and committed in `d09e228`'s message, as
186 checks. The arithmetic was wrong — the actual total is 217. The commit
message stands as history; the figure is corrected here and in
Development_Progress.)*

### Documentation audit

- README's description of the host tests still said they "compile the real
  rendering code" — accurate when the strip renderer was the only test file,
  now four files out of date. It lists what the suite actually covers, and
  notes that a test whose hardware exists on only one unit compiles to a skip
  on the other.

## 2026-08-20 — Encoder oversensitivity: the spec was never implemented

**Report.** Channel selection overshoots, and felt worse since the NeoPixel
strip went in — suspected interference.

**Finding.** FSD §5.5.1 specifies a cycle-position quadrature decoder and an
`ENC_DIVIDER` pulse divider. **Neither existed.** The decoder was the Gray-code
level comparison the section explicitly rejects (`if (A != B) CW else CCW`),
`ENC_DIVIDER` appeared nowhere in the codebase, and B was configured to
interrupt but had no handler attached — losing three of four transitions.

Every accepted edge became a channel change. And because the decoder had no
notion of a *legal* transition, an electrical glitch on A was indistinguishable
from a detent, producing a step in a direction set by whatever B happened to
read. So the strip is a plausible noise source, but the reason it manifested is
that the decoder accepted anything.

**Fix.** Implemented as specified — cycle-position decoding that discards any
transition not exactly one position around the cycle, `ENC_DIVIDER` **4**
(raised from the spec's 3 by request), reversal resetting the accumulator,
ISRs on both lines on both edges, lockout back to the spec'd 2 ms. Bounce is
now rejected inherently: chattering one line toggles between adjacent states,
so the accumulator oscillates about zero.

**Strip contribution reduced without touching the frame rate.** Slowing it
would have degraded the ERROR triple-flash and boot chase, so instead
`rlc_rgb_led.c` keeps a shadow of the last transmitted frame and skips
`led_strip_refresh()` when nothing changed. A steady map now transmits nothing;
a pulsing cursor sends twice a second instead of twenty times.

**Measured on target:** counters flat at zero while idle — no phantom counts —
then `isr=40 valid=35 step=8` after deliberate rotation. `valid/step = 4.4`
against a divider of 4, with 5 of 40 edges rejected as illegal: bounce the old
decoder would have turned into channel changes.

**Diagnostics kept:** `encoder_get_stats()` and an `enc[isr= valid= step=]`
field on the remote's periodic log, so a noisy input is visible in the log
rather than only felt at the knob.

Host tests T-Q01…T-Q06 cover the divider, illegal transitions, bounce,
reversal, seeding, and the guarantee that one detent moves exactly one channel.
The test runner's include path now reaches the remote component, and the
encoder test compiles to a skip under the base build since the hardware is
remote-only. FSD **v1.27**, with an as-built note on §5.5.1 recording that the
section described behaviour the firmware never had.

## 2026-08-20 — Bug #25: no battery undervoltage cut-off; channel-1 clamp as-built

### Bug #25 — no hardware undervoltage cut-off on either pack

Raised after reviewing the bug #18 protection work. Checking the spec first
turned this from "not fitted yet" into something worse: **FSD §5.6 never
specified one.** §5.6.1 and §5.6.2 define chemistry, capacity, connector and
regulation, but pack protection rests entirely on firmware thresholds.

Three reasons that is insufficient for LiPo:

1. **ERROR does not disconnect the load.** Crossing `*_VBAT_CRITICAL_MV` latches
   the FSM into ERROR, which halts *operation* — regulators, display backlight
   (the remote's dominant load), status LEDs, siren driver and MCU all keep
   drawing afterwards.
2. **It only works while firmware runs.** A brownout, a halt, or a unit simply
   left switched on after a launch day discharges the pack unobserved.
3. **There is no margin.** `BASE_VBAT_CRITICAL_MV` is 9000 mV — *exactly*
   3.00 V/cell, the level at which permanent capacity loss begins. Below roughly
   2.5 V/cell a LiPo becomes unsafe to recharge.

Recorded with suggested trip points (base ~9.6 V, remote ~6.8 V) and the
ordering constraint that matters: **the hardware cut-off must sit above the
firmware threshold**, or power is removed before the operator ever sees the
ERROR screen explaining why. Hysteresis or a latch is required, since an
unloaded LiPo recovers above the threshold after disconnect.

Documented as FSD §5.6.2a (v1.26), in the Open Bugs index, and in the README's
known-open-items list.

### Numbering collision caught

The entry was first written as #24, but #24 had already been taken by the
chip #3 rail-float incident committed outside this session (`c1d6c09`).
Renumbered to **#25** and the index reordered newest-first. Worth noting the
bug list is now shared across sessions and can move underneath a working copy.

### Channel-1 ADC clamp recorded as-built

Bug #18's channel-1 protection was recorded only as "clamping diodes". Now
pinned down: **2x 1N5819, one to GND and one to +3.3 V**, on the continuity ADC
pin. That vagueness is the same doc/hardware gap class that produced #18 and #21.

Assessed the part choice rather than assuming it carried over from the bug #22
advice against 1N5819: it does not. That warning was about a 6.4 kΩ battery
divider, where tens of µA become hundreds of mV. The continuity node's impedance
is dominated by the igniter (~10 Ω when GOOD, ~434 Ω at the MARGINAL limit), so
leakage shifts nothing across the 0.5 mV / 66 mV / 1.5 V thresholds. The 1 A
rating is in fact an advantage here, because there is **zero series resistance**
between the sense node and the ADC pin, so the clamp carries the full fault
current.

Recommended adding **~1 kΩ between the sense node and the ADC pin** so a 12 V
fault delivers ~8 mA into the 3.3 V rail instead of amps — without it, the
3V3-side clamp dumps the whole arc into the rail and can take out everything on
it. DC reading is unaffected; the ADC input is high-impedance.

### Stale base-port commands found and fixed

The chip #4 board swap (committed outside this session) moved the base's COM
port from `5B5E044219` to `5B5E042156`. `build_base.sh` had been updated, but
two **runnable commands** still named the old adapter and would have flashed or
monitored nothing:

- the bug #18 test-tooling block in `Development_Progress.md`
- the flash command in `rlc-hw-test-base/RLC_Base_Hardware_Test_Specification.md`

Added a caveat next to the port table, because the original reasoning has a
limit worth stating: a COM-port by-id identifies **the CH340 adapter on that
board**. It survives swapping the ESP32 *chip* — three times, as intended — but
not swapping the whole board. Chip #4 is the ex-remote #1 board, hence a
different adapter. Historical port references in this changelog were left
alone; they record what was true when written.

### Snubber placement answered

Both relay types: across the switched pair, **NO to COM** — not across the coil,
which is the flyback diode's job. Arm relay (VBAT+ on NO, fire bus on COM) is
the priority, since after the bug #18 ordering fix it is the contact that breaks
the full 6 A and its erosion threatens the primary interlock. Channel relays get
the same 47 Ω / 100 nF across NO–COM to address the make-arc, which the software
fix cannot close. Nothing across NC–COM: that path carries ~1 mA of sense current
and never switches under load.

## 2026-08-20 (late) — Base chip #3 killed by floating rail; chip #4 is the resurrected remote board (bug #24)

### The incident

An accidental ground disconnect during bench work let the base's 3.3 V rail
float to **3.68 V** — above the ESP32-S3 absolute maximum of 3.6 V — killing
chip #3 (`44:1B:F6:D4:0D:68`). Diagnosis path: the CH340 COM bridge still
enumerated but esptool got "no serial data received" (chip TX never moved)
across every reset mode *and* manual BOOT entry; native USB JTAG absent from
the bus entirely; 3.68 V measured at both 3.3 V pins. Powered + in-bootloader
+ silent = dead chip. The regulator was replaced and the rail verified at
**3.29 V** before any new silicon was wired in.

### The "new" board wasn't new

The replacement board inserted turned out to be the **retired old remote
board** (`5B5E042156`, MAC `44:1B:F6:81:F1:70`), pulled from service in July
with a "SPI flash damaged" diagnosis (suspected reverse-polarity battery).
Bench retest disproved that: bootloader, flash ID (16 MB), flash read, and a
write+verify+erase cycle on a scratch sector at `0xFF0000` all passed. The
board is healthy and was enrolled as **base chip #4**.

### Changes

- `rlc_config.h`: `BASE_MAC_ADDR` → `44:1B:F6:81:F1:70` (chip genealogy in the comment)
- `build_base.sh`: default port → `usb-1a86_USB_Single_Serial_5B5E042156-if00`
- `Development_Progress.md`: bug #24 logged (open-bugs table + full entry with
  rail-protection recommendations); Hardware Reference table updated
- Both units rebuilt and reflashed
- Commit `c1d6c09`

### Link verification

```
[BASE]   rlc_espnow: ESP-NOW init ch 11, MAC 44:1b:f6:81:f1:70
[BASE]   rlc_link: LINK_REQUEST from remote fw 1.1.0 → LINK_ACK sent, token=0x9f673ef9
[BASE]   rlc_bfsm: BOOT -> IDLE (link established)
[REMOTE] rlc_link: LINK_ACK accepted → rlc_rfsm: LINKING -> IDLE
         rssi −44/−52, base vbat 12.0 V, cont=0x0003, bug #18 gate active
```

### Notes and follow-ups

- **Rail protection still OPEN** (bug #24): secure the ground path (screw
  terminals / keyed connectors), 3.6 V zener clamp across the 3.3 V rail, and
  ideally an input eFuse — that last would have prevented all three base chip
  deaths. The base is 4-for-4 on ESP32s consumed.
- **Remote was left in ERROR: `CRITICAL battery: 0 mV`.** If its battery was
  simply unplugged during bench work, reconnecting it clears it; if the
  battery was connected, the remote's sense path (GPIO 4/5 ADC) needs
  investigating.
- **`task_wdt: esp_task_wdt_reset(): task not found`** bursts for ~100 ms
  after boot on *both* units, then clean — pre-existing firmware quirk,
  untracked, low priority.
- July's remote "flash damaged" diagnosis is now suspect (that board benches
  healthy) — record stands unless chip #4 misbehaves in service.

## 2026-08-20 — Arm sense reporting corrected end to end (firmware 1.1.0)

Started as a question about what "HW OFF" meant on the remote's status line.
The review found the field meant nothing useful, and that a neighbouring field
was making a false safety claim.

### What was wrong

- `STATUS_UPDATE` carried the **key switch twice** — `base_arm_switch`
  (debounced) and `arm_switch_hw` (raw). One bit of information in two fields.
- The protocol header documented both as *arm sense*, a different signal on a
  different pin. The display labels were written from those comments.
- **The arm relay feedback (GPIO 21) was never transmitted at all.**
- **The ARMED screen showed "SENSE CONFIRMED" derived from the key switch**,
  asserting arm-relay confirmation the remote had never received. The base's own
  interlock was sound, but the display's claim was not derived from it and would
  have read CONFIRMED with a dead arm-sense circuit.
- Stale status rendered identically to "key off" — absence of data shown as a
  safety guarantee.

### Fix

Fields renamed to `base_key_switch` (GPIO 42) and `base_arm_sense` (GPIO 21),
the second now carrying the real debounced arm sense. **The struct stays 14
bytes** — the redundant raw copy was repurposed into the field its name already
claimed — so the size assert and offset self-test are untouched.

**Firmware 1.0.0 → 1.1.0.** The field's meaning changed while its size did not,
so nothing structural would stop a mixed pair from misinterpreting it. The
strict version gate is the only thing that does, and only if the version moves.

### Four-state BASE field

| Key | Arm sense | Shown | Colour |
|---|---|---|---|
| OFF | LOW | `SAFE` | green |
| ON | LOW | `READY` | amber |
| any | HIGH, in ARMED/PRE_FIRE/FIRING | `ARMED` | red |
| any | HIGH elsewhere, or `ERR_RELAY_FAULT` | `WELD!` | flashing |
| — | stale | `?` | grey |

`ARMED` and `WELD!` are driven by the arm sense, never the key. A welded relay
leaves the fire path live with the key OFF, so a key-driven display would print
SAFE over an energised igniter circuit — that failure mode is what ruled out
using the key switch, and what ruled out an AND of the two. The `WELD!` check
also tests `base_state` instead of waiting on the base's weld confirm count, so
it warns earlier than `ERR_RELAY_FAULT`.

Renders as `SEL CH 1   BASE READY   REMOTE ARMED` — 36 of the 40 characters
available at the scale-2 font floor. The ARMED screen now reports the real sense
as `ARM SENSE OK` / `NOT OK`.

### Tests

New host file, T-M01…T-M07, 27 checks: the normal SAFE→READY→ARMED progression,
the welded-relay case with the key off, `ERR_RELAY_FAULT` precedence, stale
never reading SAFE, a sweep proving the key switch alone can never produce ARMED
or WELD! in **any** base state, the 40-character budget for every label, and a
guard on the 14-byte struct size. Renumbered to T-M to avoid colliding with the
existing T-A arming tests in §15.2.

### Verified

Both units rebuilt, flashed **together** (the version gate requires it) and
confirmed linked at v1.1.0, IDLE, no errors. FSD **v1.25** documents the
four-state derivation in §10.2.2 and the corrected protocol fields.

### Documentation audit afterwards

The first pass at the FSD missed more than it caught, so a second sweep:

- **The §6 protocol field table still described the old fields.** Rows 6 and 7
  now document `base_key_switch` (a precondition only) and `base_arm_sense`
  (the hazard signal), including a note that pre-1.1.0 firmware wrongly carried
  a raw key copy there. The appendix C struct listing was corrected too.
- **Both screen mock-ups had orphaned lines.** Replacing one line each in
  §10.2.2 and §10.2.3 left behind "Remote switch: SAFE", "Arm sense: OFF" and
  "Arm sense: CONFIRMED", which then duplicated or contradicted the new
  combined lines. Removed, and box widths re-checked by character count rather
  than by eye — byte length is misleading here because the art uses multi-byte
  box-drawing glyphs.
- **The ARMED mock-up's inner text was stale** from the earlier scale-2
  legibility pass: "PRESS AND HOLD / FIRE TO LAUNCH" is one string in the code
  ("HOLD FIRE TO LAUNCH"), and "Continuity: OK" is rendered "CONTINUITY GOOD".
- **Test B2-A04 was obsolete, not merely reworded.** It verified
  `arm_switch_hw` matched the raw key GPIO — a property that no longer exists.
  Rewritten to verify `base_arm_sense` follows the arm relay and that the
  remote shows BASE ARMED only while it is HIGH.
- Version reference in the Phase 1 task table updated to v1.1.0.

Archive revisions and historical review documents were deliberately left
untouched; they correctly record what was true when written.

## 2026-08-19 (late) — LINK LOST screen counter stuck at 1 s

**Symptom.** The link-lost screen showed "Last contact: 1 s ago" and never
advanced.

**Root cause.** Both dynamic fields on that screen came from `missed_pings`,
whose update path in `rlc_link.c` sits behind
`if (s_state != RLC_LINK_STATE_LINKED) return;`. The counter therefore stops the
moment the link is declared lost, frozen at `HEARTBEAT_FAIL_THRESHOLD` = 3. The
display computed `3 x 500 ms / 1000` = **1 s, forever** — the arithmetic matches
the reported symptom exactly, which is what confirmed the diagnosis rather than
just inspection. The counter was never measuring elapsed time; it was a miss
counter that stops precisely when it is needed.

A second bug on the same screen: "Attempts N" also used `missed_pings`, so it
was both frozen and mislabelled — `linkreq_attempts` was already maintained and
exported but unused.

**Fix.** Added `rlc_link_status_t.ms_since_contact`, from a new
`s_last_contact_ms` recording the wire-receive timestamp of every well-formed
frame from the peer. Set in `process_frame()` right after the MAC filter and
parse, so it covers every message type and both roles, and uses the receive
timestamp rather than `now_ms()` so queue latency is not counted as airtime.
The attempts line now uses `linkreq_attempts`. Display switches to minutes past
600 s.

**A trap worth recording.** The first attempt took the state mutex around the
timestamp write. `link_task` already holds that mutex across the whole
`process_frame()` call and it is a **non-recursive** FreeRTOS mutex, so the link
task deadlocked instantly — TWDT fired and the remote went into a reboot loop.
The watchdog report named `rlc_link` with both CPUs idle, which distinguishes a
block from a spin. Fixed by removing the lock (the caller holds it) with a
comment at the site so nobody re-adds it.

**Verified on target**, 50 s induced outage with the base held in reset:

| | During outage | On recovery |
|---|---|---|
| `missed_pings` (old source) | **frozen at 3 throughout** | 0 |
| `ms_since_contact` (new) | 2354 → 47354 ms | 153 ms |
| `linkreq_attempts` | 1 → 23 | 0 |

The remote's periodic status log gained `contact=` and `attempts=` fields —
that is how the above was measured, and it makes this class of freeze visible in
logs as well as on screen.

FSD **v1.24**: §10.2.5 now documents which counter each field must come from and
why `missed_pings` is unsuitable for either. Its ASCII mock-up of the screen was
also corrected — it still showed "Ping attempts: 7" and a stale field order,
left over from the scale-2 legibility pass that renamed the line to
"Attempts 7   RSSI -45 dBm". The mock-up now matches the code.

Checked `.claude/RESUME.md` while auditing docs: it is an untracked Claude Code
checkpoint artifact from an unrelated session, not project documentation, and
was left alone.

## 2026-08-19 (late) — Battery sampling hardened against clipping

`rlc_battery.c` took a **single** raw ADC read per call and fed an 8-deep mean.
The divider calibration earlier today showed why that fails: a noisy bench
supply produced 600-1500 counts of sample spread with individual samples
clipping at ADC full scale, and **a clipped sample can only bias a mean
upward** — making a flat pack read as healthy, the one direction a battery
guard must never fail in.

### Change

Each reading is now the **median of a 33-sample burst** at 1 ms spacing,
feeding the existing 8-deep moving average. Odd count so the median is a real
sample rather than an interpolation; the spacing spreads the burst over ~33 ms
so samples decorrelate from supply ripple instead of landing in the same part
of every cycle. Bursts where more than a quarter of samples clip now log a
warning — the median has already discarded them, but persistent clipping means
supply noise or an input over range and must not pass silently.

Cost is immaterial: sampling runs at 1 Hz in tasks that feed a 5 s watchdog.

Constants live in `rlc_config.h` (`VBAT_BURST_SAMPLES`, `VBAT_BURST_GAP_MS`,
`VBAT_RAIL_COUNTS`) with the rationale recorded beside them.

### Measured

Host test T-B03 quantifies it: in a burst where 9 of 33 samples clip, the mean
reads **571 counts high** — about **+2 V** through the base's 4.3148 divider
ratio — while the median is exact.

On target after reflashing, 30 s per unit:

| Unit | vbat spread | Clipping warnings |
|---|---|---|
| Base | 43 mV (0.35 %) | 0 |
| Remote (on the noisy bench supply) | **20 mV (0.24 %)** | 0 |

Both units linked throughout, IDLE, `err=0x00 (NONE)`.

### Tests

New host file, T-B01…T-B07, 13 checks: sort helper, constant burst, the
clipped-burst case contrasted directly against the mean, zero dropouts, the
full path with divider ratio applied, retention of the last good value on total
ADC failure, and a guard that the burst size stays odd. Needed new ADC stubs
(`tests/host/stubs/esp_adc/`) that script the raw values the driver reads.

### Deliberately not changed

The 8-deep moving average is still a mean. Each input is now already a robust
median, so that is sufficient — and making it a median too would slow the
response to a genuine voltage collapse, a behavioural change in a safety path
that the evidence did not justify.

### Docs

FSD **v1.23**: new §5.6.3 on battery ADC sampling, burst constants in §14.1,
T-B rows in §15.5, and the stale "8-sample moving average" wording corrected in
§4, §5.4.7 and §7.3.3. Development_Progress gained a section with the on-target
figures, and `rlc_battery.h`'s header comment was corrected too.

README's safety-design section gained the median rationale — it belongs there
alongside the other defensive decisions, and the bench figure (a burst with 9 of
33 samples clipped reading ~2 V high as a mean, exactly right as a median) makes
the case concretely rather than abstractly.

Audited the remaining "8-sample" references and left them alone deliberately:
`archive/` holds superseded FSD revisions, `Phase2_Code_Review.md` records what
was true when it was written, and the two `rlc-hw-test-*` specs describe
standalone bring-up firmware that does not share `rlc_battery.c`. Those are
accurate for their own code; changing them would have introduced errors rather
than fixed them.

## 2026-08-19 (bench, cont.) — Remote calibrated, thresholds restored, bugs #22/#23

Removing the bug #21 zener restored the remote's sense path: implied ratio now
spans 1.6 % across 4.94-8.56 V instead of 30 %. `REMOTE_VBAT_DIVIDER_RATIO`
2.8 → **2.8211** (gain-only over the operating band, 57 mV worst case). The
resistors were 0.75 % off nominal — never the fault.

The bench supply was noisy enough to clip individual samples at full scale,
which can only bias a mean upward, so `tools/vbat-cal` now reports a **median**
of 129 samples. Measured 2× more stable than the mean (14 counts of
line-to-line variation against 31) and immune to the clipping.

**FSD §5.6.2 production thresholds restored** now the sense is trustworthy:
MIN_ARM 3200→7000, MIN_OPERATE 3100→6600, CRITICAL 3000→6400, FULL 4200→8400.
Verified against the calibration data — 6400 reads 6356, 6600 reads 6561,
7000 reads 6971 — so every threshold under-reads slightly and protection trips
early rather than late.

Confirmed on target: the remote boots to IDLE reporting `vbat=7267 mV` and
links at −40 dBm. Before this work the same pack would have read ~5500 mV and
locked the unit in STATE_ERROR.

### New bugs tracked

- **#22 — remote GPIO 1 has no overvoltage clamp.** The zener was removed and
  not replaced; the divider's series impedance is the only limit. Fix is a
  BAT54-class Schottky to the 3.3 V rail. Recorded explicitly that a 1N5819 is
  unsuitable — its leakage flows rail→node and would bias readings *upward*,
  the direction that masks a flat pack.
- **#23 — remote divider has no ADC headroom.** A full 2S pack sits at 97 % of
  the ADC ceiling; the FSD's own "0–3.0 V for 0–8.4 V" wording bakes it in.
  Accuracy only, ~0.7 % at full charge; thresholds sit at 71-78 % and are
  unaffected. Fix is 3.0 kΩ/1.2 kΩ (ratio 3.5), which also relaxes the #22
  clamp-leakage requirement 7.5×, so the two are best done together. The base
  has the same class of problem at 92 %.

Bug #21 downgraded to PARTIAL — sense correct, protection still missing.

Both units reflashed with current RLC firmware and verified linked on target:

| Unit | Port | State | Battery | Link |
|---|---|---|---|---|
| Base | `…5B5E044219` (COM) | IDLE, `err=0x00 (NONE)` | 12210 mV | −31 dBm |
| Remote | `…5B5E043219` (COM) | IDLE | 7267 mV | −40 dBm |

The base's USB was moved back to its CH340 COM port, so **both units are now on
stable board-serial by-ids** that survive chip swaps — the documented
configuration. The base produces no console output over its native USB port,
since the RLC firmware's console is UART0; only `tools/vbat-cal` and the hw-test
firmware use USB-Serial/JTAG.

`err=0x00 (NONE)` in the base log is the named-error-flag work from earlier in
the session confirmed on target.

### Documentation reconciled

- README's "bench-test battery thresholds" open item is **resolved** and was
  replaced with bugs #22 and #23.
- The Phase 4 finding that flagged the bench thresholds is struck through and
  marked resolved, recording *why* the ordering mattered: restoring them before
  calibrating would have been actively harmful, since with the zener fitted a
  fully charged pack read 5979 mV and would have locked the remote in
  STATE_ERROR with nothing pointing at the divider.

## 2026-08-19 (bench) — Battery divider calibration: base done, remote reveals bug #21

Method: DVM at the board terminals as reference, `tools/vbat-cal` streaming raw
ADC counts, and each chip's own `adc_cali` linearisation curve dumped from the
device (a pure function of the raw count, so it needs no applied voltage). Raw
counts alone proved uninterpretable — fitting them directly gave 322 mV
worst-case error with S-shaped residuals, which is ADC non-linearity, not the
divider. Mapping through the curve first is what made the data usable.

### Base — calibrated, divider was fine

`BASE_VBAT_DIVIDER_RATIO` 4.3 → **4.3148** (gain-only, fitted over the 8.7–12.9 V
operating band, 0.70 % worst-case). The 0.34 % correction says the resistors were
always within tolerance; the divider was never the error source.

Two findings that are not firmware:

- **The ADC runs out of headroom.** A full 3S pack (12.6 V) puts 2920 mV on the
  pin — 92 % of the ADC's 3163 mV ceiling; the 12.92 V test point hit 95 %.
  Incremental scale collapses from ~3.58 mV/count mid-range to 2.42 at the top.
  A ~5.5:1 divider would land the whole range in the linear region.
- **Sampling noise now dominates.** ~130 counts peak-to-peak (±1.7 %) on the
  bench supply; `rlc_battery.c` averages 8 single reads a second apart, leaving
  ~±0.6 %. An oversampling burst would help. Not applied — it touches a safety
  path and is the user's call.

Offset models fitted better (39 mV vs 89 mV) but were **rejected**: a +424 mV
offset is large and physically unexplained, a straight line absorbing ADC
curvature that would extrapolate badly. Honest 0.7 % beats a fragile fudge.

Error direction is conservative both before and after: the firmware under-reads
near the arming thresholds (87–102 mV before, 50–68 mV after), so it blocks
arming slightly early rather than late.

### Remote — calibration aborted, bug #21 raised

The implied ratio drifts **3.08 → 4.01 across the sweep, 30 %**. A resistive
divider is linear by definition, so no resistor value explains this: something
non-linear loads the sense node, or the ADC input is damaged. Pin voltage falls
short of an ideal 2.8:1 divider by 174 mV at 5.33 V rising to 925 mV at 8.57 V —
the signature of a clamp conducting harder as the node rises.

**Showstopper:** the firmware under-reads by 9 % at the bottom growing to 30 % at
full charge, so with the FSD §5.6.2 production thresholds (7000/6600/6400)
*every* voltage in the 2S range reads below CRITICAL. A freshly charged pack
would put the remote straight into STATE_ERROR at boot. Restoring those
thresholds is blocked until the circuit is fixed.

**Corrects an earlier conclusion.** This morning's `vbat=5740 mV` prompted a
suggestion that the pack was over-discharged. Back-calculated through this data
the true pack voltage was **~7.6 V** — healthy. The pack was fine; the sense
circuit was lying. Whether the fault predates today is not yet proven; measuring
the pack directly settles it.

No ratio applied — a non-linear fault cannot be corrected with a gain.
Next diagnostic: DVM directly on GPIO 1 while sweeping, to separate an external
clamp from a damaged ADC input.

### Tooling

- `tools/vbat-cal` gained a boot-time dump of the chip's `adc_cali` curve
  (`ADCMAP` records) and a `sdkconfig.uart` variant, since the remote is reached
  over its CH340 bridge while the base is on native USB — the console has to
  come out the port you are actually connected to.
- `tools/vbat_fit.py` gained a `--pairs RAW:REF_MV` mode for hand-noted readings
  and a raw-counts model alongside the calibrated-mV ones. Validated against
  synthetic data with an injected ratio before being trusted on real numbers.
- Evidence preserved under `docs/calibration/`: measurements, every fit
  considered, and both chips' ADC curves, so the constants are reproducible
  rather than magic.

## 2026-08-19 (evening) — Named error flags; battery calibration rig

### Error flags are shown by name

Prompted by a real "BASE ERROR 0x02" on the remote, which required looking up
a bitmask in a header to learn it meant a critically low base battery.

Every bit 0-7 now has a canonical name, defined once in `rlc_protocol.h`:

| Bit | Flag | Name |
|---|---|---|
| 0 | `ERR_VBAT_LOW` | `VBAT LOW` |
| 1 | `ERR_VBAT_CRITICAL` | `VBAT CRITICAL` |
| 2 | `ERR_RELAY_FAULT` | `RELAY FAULT` |
| 3 | *(reserved)* | `RESERVED BIT3` |
| 4 | `ERR_COMM_DEGRADED` | `COMM DEGRADED` |
| 5 | `ERR_WATCHDOG_RESET` | `WATCHDOG RESET` |
| 6 | `ERR_INTERNAL` | `INTERNAL FAULT` |
| 7 | *(undefined)* | `UNDEFINED BIT7` |

The two meaningless bits get names too, so an unexpected flag is reported
rather than silently dropped.

The remote now shows `BASE ERROR 0x02: VBAT CRITICAL`. **Multiple flags cycle
rather than truncate:** the line holds 40 characters at the scale-2 font floor,
which fits one named flag, so when several are set they rotate at 2 s with an
`(n/total)` counter — `BASE ERROR 0x06: VBAT CRITICAL (1/2)`. That is a
documented deviation from FSD §13.2's "stacked if needed" wording; stacking
would either truncate or breach the legibility floor. The base's UART log gains
the full comma-separated list next to the hex.

New helpers `rlc_error_flag_str()`, `rlc_error_flags_count()`,
`rlc_error_flag_nth()` and `rlc_error_flags_str()` follow the existing
`rlc_nack_reason_str()` convention. `rlc_error_flags_str()` avoids stdio so the
shared protocol header stays dependency-light. Covered by a new host test file,
T-E01…T-E07, 31 checks — including buffer-truncation safety and the 0x02 case
that started this.

### Battery divider calibration rig

The measurement chain is `vbat = adc_cali_raw_to_voltage(raw) x divider_ratio`
— **gain-only, no offset term** — with ESP-IDF curve fitting handling ADC
non-linearity from eFuse data. Both units read GPIO 1, ADC1, 12-bit, 12 dB. The
suspect quantities are therefore the divider ratios themselves (`4.3` base,
`2.8` remote), which are nominal values subject to resistor tolerance.

Two new pieces:

- **`tools/vbat-cal/`** — capture firmware. Streams CSV at 2 Hz, averaging 64
  samples per record, reporting **raw counts as well as calibrated mV**. That
  separation is the point: a wrong ratio shows up as constant proportional
  error, poor ADC calibration as curvature in the residuals; with only mV the
  two are indistinguishable. Prints per-unit input limits in its banner.
- **`tools/vbat_fit.py`** — host-side fitter. Detects the plateaus where a
  supply was held at a setpoint (rejecting transition records), pairs them in
  order with reference voltages, and fits both a gain-only and a gain+offset
  model, reporting per-point residuals and a recommendation.

**The fitter was validated against synthetic data before use**: injected a true
ratio of 4.17 with transition noise, and it recovered 4.1704, found exactly the
7 expected plateaus, and correctly recommended the gain-only model.

Method notes agreed for the session:

- Reference should be a **DMM at the board terminals**, not the PSU display —
  supply readouts are commonly 1-2 % out, and that error would be calibrated
  straight into the firmware.
- Input limits, exceeding which destroys the chip: base 8.0-12.6 V sweep, never
  above 13.0 V; remote 5.5-8.4 V sweep, never above 8.6 V. **Feeding base
  voltages into the remote puts >4 V on a 3.3 V pin** — the bug #18 failure
  class.
- More points than a line strictly needs, so residuals can confirm linearity
  and catch ADC compression near the top of the 12 dB range.

### Follow-up

Once calibrated, the remote's bench battery thresholds (3200/3100/3000 and
`REMOTE_VBAT_FULL_MV` 4200) should be replaced with the FSD §5.6.2 production
values for the 2S pack (7000/6600/6400, full 8400). That open item is listed in
the README and Phase 4 findings.

FSD bumped to **v1.21** (new §13.2a, T-E rows in §15.5).

## 2026-08-19 (merge) — Branch merged to main; bug #20 raised

### Merged to main

`docs/fsd-v1.16-accuracy-corrections` merged into `main` with `--no-ff`
(`acb8bb5`) and pushed. It was a pure fast-forward situation — `main`'s tip was
the merge base, so no commits existed on `main` that weren't on the branch and
no conflict was possible. The merge commit exists to summarise 17 commits whose
branch name had long since stopped describing them.

`main` now carries Phase 4's display, the LED igniter strip, the bug #18 channel
gate, the host test suite, `tools/strip-diag`, and FSD v1.15→v1.19. The public
repository has a README on its front page for the first time.

### Caught during the merge — test runner was not executable

`tests/host/run.sh` was committed as mode **644**. It ran fine in-session
because the working copy had been `chmod +x`'d, but the bit was never recorded,
so `./tests/host/run.sh` — the exact command the README documents — would fail
on a fresh clone. Fixed in `3d0fe86` via `git update-index --chmod=+x`, and
verified by cloning `main` into a temp directory and running the suite there:
30 checks × 2 orientations, 0 failures.

(`tools/test_tr04.py` is also 644, but appears to be invoked as
`python3 tools/test_tr04.py`, so it was left alone.)

### Bug #20 — shipped crypto keys are public (OPEN, deferred by decision)

`ESPNOW_PMK`, `ESPNOW_LMK` and `CMD_INTEGRITY_KEY` are compile-time constants in
`rlc_config.h`, and the repository is public. They were already on `main` before
the merge, so nothing was newly exposed — but FSD §6.2.1 calls AES-128-CCM "the
system's primary security boundary against external adversaries", and that claim
does not hold when the keys are readable.

| Layer | FSD | Status |
|---|---|---|
| AES-128-CCM (ESP-NOW) | §6.2.1 | Ineffective — keys public |
| CRC32-C integrity, pre-shared key | §6.2.2 | Ineffective against forgery; still catches corruption |
| Replay protection (session token + sequence) | §6.2.2 | **Effective** — token is random per link-up |

No effect on bench testing. **Keys to be rotated later** — user's decision.
Recorded that rotation alone is insufficient while the keys live in tracked
files, since git history preserves superseded values; they need to move to an
untracked header or NVS provisioning. Note FSD v1.14 explicitly accepted
compile-time keys, a judgement made before the repo was public and worth
revisiting.

Also corrected: §6.2.1 cited `protocol_config.h` as the key location. No such
file exists — the keys are in `components/rlc_common/include/rlc_config.h`.

### Documentation

- FSD **v1.20**: bug #20 note in §6.2.1, key-location correction, revision row.
- `Development_Progress.md`: full bug #20 record with fix options, plus a new
  Phase 5 development task for the key rotation.
- `README.md`: bug #20 added to the known-open-items list.
- `RLC_Project_Summary.md`: the club-facing letter listed the three
  communication-security layers without qualification. Added an honest caveat —
  overstating the security to club members would be worse than the bug.
- `Development_Progress.md` gained an **Open Bugs index** near the top. With
  three open bugs (#18, #19, #20) scattered across ~400 lines of a 1100-line
  document, there was no way to see the blocking items at a glance. The table
  gives each bug a class, a status and — most usefully — what it *blocks*, and
  points at the non-blocking items tracked elsewhere (bench battery thresholds,
  the §7 arming guard, the §10.2.0 palette deviation).

## 2026-08-19 (bench) — Strip bring-up: orientation is per unit, and bug #19

Bringing the new strip rendering up on real hardware turned up two separate
problems on the base, neither of them in the layer logic.

### Base strip was dark — 5 V not connected

Resolved by the user. Worth recording that the base's UART-bridge port also
vanished mid-session (the board was replugged onto its native USB port), which
is why the console went quiet: the RLC firmware's console is on UART0.

### Orientation is NOT the same on both units

v1.18 assumed both strips were wired data-in at the channel-8 end. They are
not. Characterised with the new `tools/strip-diag` firmware — a single-pixel
walk along the chain lit channel 1 first on the base:

| Unit | Data-in end | Mapping | `RLC_STRIP_REVERSED` | Built-in LED |
|---|---|---|---|---|
| Base | channel 1 | channel N → pixel `N-1` | 0 | channel 1 |
| Remote | channel 8 | channel N → pixel `7-(N-1)` | 1 | channel 8 |

`RLC_STRIP_REVERSED` is now selected per unit via `CONFIG_RLC_UNIT_BASE`
(`rlc_config.h` gained `#include "sdkconfig.h"` for this). The host renderer
tests build and run **once per unit**, so both orientations are asserted —
30 checks each, all passing.

### Bug #19 — dead pixel at channel 4 on the base strip (OPEN)

Channels 1-3 render correctly, channel 4 is stuck solid blue and never updates,
channels 5-8 stay dark — stable across every pattern.

`tools/strip-diag` paints *static* solid frames (red/green/blue/yellow/white)
and walks a single pixel. Channels 1-3 rendered all five colours correctly, so
the data line from GPIO 48 is clean. The fault is at the 4th pixel in the chain:
it holds a value latched at power-up and never updates, so its data input is not
receiving valid bits — dead LED controller, or a broken joint between pixel 3's
DOUT and pixel 4's DIN. Channels 5-8 are dark because nothing valid propagates
past it and a WS2812 that never received a frame stays off.

Explicitly **not** a supply or logic-level problem. An earlier hypothesis blamed
3.3 V data into a 5 V strip; the static-frame evidence disproved it — marginal
levels corrupt the pixels nearest DIN and flicker, rather than producing three
perfect pixels and a stable stuck one. Recording that here so nobody re-buys a
level shifter.

**Fix required (hardware):** reflow or replace the 4th LED, or cut the strip
after pixel 3 and splice in a replacement.

### New tool

`tools/strip-diag/` — standalone WS2812 bring-up firmware for GPIO 48. Paints
known static frames, walks a single pixel to identify the DIN end, and varies
RMT resolution and brightness. It builds against the **project's own**
`managed_components/espressif__led_strip` via `EXTRA_COMPONENT_DIRS`, so it
exercises byte-for-byte the same driver as the RLC firmware. Console is on
USB-Serial/JTAG (native USB port).

### Verified

- Remote, by eye: ch1 red (SHORT), ch2-8 yellow (OPEN), ch2 breathing as the
  selected channel. Mapping, colours, cursor and orientation all correct.
- Both units rebuilt and reflashed; link healthy (rssi −35, no missed pings).
- Host suite: 30 checks × 2 orientations, 0 failures.

### Docs

FSD bumped to **v1.19** (§11.0 pixel-order table, §5.4.11, §5.5.8, §14.1).
`Development_Progress.md` gained the per-unit orientation table and the full
bug #19 record, and its LED test table now reflects what was verified by eye.
README updated: per-unit strip orientation noted, bug #19 added to the
known-open-items list, `tools/strip-diag` and `tests/host/` added to the
repository layout, and `./tests/host/run.sh` documented under building and
flashing.

### Session commits (branch `docs/fsd-v1.16-accuracy-corrections`)

| Commit | Subject |
|---|---|
| `e1dbe9d` | LED strip is now an igniter status display on both units |
| `96bd306` | Strip orientation is per unit; add strip-diag; record bug #19 |

### Open items carried forward

- **Bug #19** — dead 4th pixel on the base strip. Hardware fix required
  (reflow/replace that LED, or cut after pixel 3 and splice). Channels 4-8 are
  unusable on the base until then.
- **T-L15/T-L16** — a continuity change moving the right pixel, and daylight
  legibility of the alarm wink, both still need the operator. T-L15 is blocked
  on bug #19 for channels 4-8.
- **FSD §7 remote-battery arming guard (NACK `0x0C`)** — deliberately kept out
  of scope; still unimplemented.
- **Production battery thresholds** — `rlc_config.h` still carries the bench
  values sized for a USB rail, not the 2S remote pack.
- **FSD §10.2.0 palette** — still specifies blue for GOOD; the as-built palette
  is green/light-green/yellow/red.

### Note

The remote's LiPo came disconnected during the base work, so it reads
`vbat=0 mV` and sits in STATE_ERROR (known bench behaviour — USB alone does not
energise the VBAT divider). Reconnect the pack to return to IDLE.

## 2026-08-19 (later) — LED strip becomes an igniter status display, both units

The 8-way NeoPixel strip did not reflect igniter status: only one pixel lit,
and the built-in LED still carried its old link-status job. Root cause of the
symptom was simply that the base was running a pre-`8ad4a6f` binary, where
`s_pixel_count` stays 1 and pixels 1–7 never receive data. The design problem
underneath was real, though: continuity was only ever visible in `IDLE`, and
every other state painted all 8 pixels a single colour.

### Design

The strip is now an **igniter continuity display on both units**. System status
*modulates* the channel map rather than replacing it. Six rendering layers,
highest first:

| # | Layer | Rendering |
|---|---|---|
| 1 | `ARMED`/`PRE_FIRE`/`FIRING` | Whole strip red — unchanged |
| 2 | `ERROR` | Red triple flash, map dimmed 20 % in the gap |
| 3 | Alarm wink | 300 ms full-strip flash every 3 s; concurrent alarms alternate |
| 4 | Stale (remote) | Whole map dimmed to 10 % |
| 5 | Breathing | Base: whole map on key ON. Remote: cursor channel on arm switch ON |
| 6 | Channel map | Continuity; channel of interest pulses |

`BOOT`, `LINKING`, `IDLE`, `LINK_LOST` and `POST_FIRE` all fall through to
layer 6. Cyan chase before the first continuity data. Alarm colours — amber
(link), magenta (battery), white (arm fault) — cannot be confused with any
continuity colour.

Layers 3 and 4 compose deliberately: STATUS_UPDATE can be late while the link
is healthy, so dim means "old data" and a wink means "something is wrong".

### Hardware facts pinned down

Data-in is at the **channel-8 end on both strips**, so channel N is pixel
`7-(N-1)` (`RLC_STRIP_REVERSED`). The built-in NeoPixel is in **parallel**
(confirmed on the bench — built-in and channel-8 pixel lit together on the
remote), so it mirrors pixel 0 = channel 8 and now carries no meaning of its own.

### Removed

- Boot-time RSSI bar and blue boot pulse (`set_rssi()`, `led_show_rssi_bar()`).
- Whole-strip `IDLE` green, `LINK_LOST` amber, `POST_FIRE` amber.
- The 250 ms whole-strip orange ping-miss flash (`flash_overlay()`) — it wiped
  the map and blocked the LED task 250 ms per miss. The 80 ms buzzer beep stays.
- Dead code never called by anything: `LED_PATTERN_CHANNEL_STATUS`,
  `LED_PATTERN_PING_FAIL`, `rlc_rgb_led_set_state()`.
- `LED_PATTERN_IDLE_ARM_ON` was documented but never set; the key-ON warning is
  now a feed rather than a pattern.

### Architecture

`rlc_rgb_led.c` is unit-agnostic — one layer resolver, both units, only the
feeds differ. All feeds are published from each unit's housekeeping loop at
10 Hz, never from an FSM, so the fire path is untouched; the FSMs set only the
firing-path and ERROR patterns. Animation phase derives from
`esp_timer_get_time()` rather than a frame counter, so patterns are stable
across scheduling jitter. Feed globals are now `volatile`.

The remote's map comes from the cached STATUS_UPDATE via
`remote_fsm_get_status()`, which already returned a freshness flag — the one
genuine asymmetry between the units, and the reason layer 4 exists.

### Tests

**First host-compiled test suite in this project.** `./tests/host/run.sh`
compiles `tests/host/test_strip.c`, which includes `rlc_rgb_led.c` directly and
links it against mock `led_strip` / FreeRTOS / `esp_timer` headers, capturing
and asserting every emitted pixel. **30 checks, 0 failures** — T-L01…T-L09
(FSD §15.5).

On target, both units flashed:

- Base boots and links, no watchdog trips — rssi −34 dBm, vbat 11618 mV.
- Remote boots and links, no watchdog trips — rssi −42 dBm, vbat 5740 mV.
- Link-loss alarm path exercised by holding the remote in reset: base detected
  loss in 1.5 s, held LINK_LOST for 25 s, recovered to IDLE cleanly.

### Docs

FSD bumped to **v1.18**: §11 fully rewritten, §5.5.8 changed materially (the
remote now has an external strip, not just the on-board LED), §5.4.11 pixel
order, §7.1/§8.1 state tables, §6.4.2 missed-ping action, §14.1 constants,
§15.5 T-L01…T-L09. README and Development_Progress updated.

### Not done / follow-ups

- **T-L14…T-L17 need eyes on the strip**: colours by eye, a continuity change
  moving the right pixel, daylight legibility of the wink, and the remote cursor
  following the encoder. Everything testable without the operator is green.
- Expected state right now, from the logs: **all 8 pixels yellow** on both units
  (cont=0x0000, nothing connected); base map breathing (key ON), remote breathing
  channel 1 only. No winks — both linked, both packs above their arming floors.
- The FSD §7 **remote-battery arming guard / NACK 0x0C** was deliberately left
  out of scope; it is an arming-guard fix, not an LED fix. Still open.
- Remote pack read **5740 mV**. That is above the *bench* `REMOTE_VBAT_MIN_ARM_MV`
  of 3200 so no alarm fires, but it is well under the FSD §5.6.2 production value
  of 7000 — with production thresholds restored this would alarm and block arming.
  Worth checking whether that pack is over-discharged.

## 2026-08-19 — Phase 4: remote display implementation (FSD §10)

Built the remote unit's display functionality end to end, deliberately kept
independent of the ongoing base firing-sequence debugging. Everything is
remote-side except one additive field in the shared link status struct.

### Architecture

`components/rlc_remote/src/rlc_display.c` (~1200 lines) replaces the Phase 1–3
logging stub.

| Element | Choice | Why |
|---|---|---|
| Panel init | Ported verbatim from the validated `rlc-hw-test-remote` sequence | Known-good on this clone (ID `0x2A403300`) |
| Framebuffer | 480×320×3 RGB666 in **PSRAM** (460,800 B) | The board has 8 MB OCT PSRAM; no per-pixel SPI round trips |
| Flush | Dirty **bounding box** only, streamed row-by-row through an internal-RAM DMA bounce buffer | FSD §10.3 partial refresh; PSRAM is not the DMA source |
| Ownership | `display_task` (prio 2, core 1, 8192 stack — FSD §9.10) is the only toucher of SPI | The FSM and input tasks never block on the panel |
| Frame rate | 10 Hz (`DISPLAY_FRAME_MS` 100) | FSD §10.3 requires ≥ 5 Hz; pre-fire countdown wants 100 ms |
| Screen choice | Derived from the remote FSM state, with latched overrides (ERROR, FW mismatch) and a timed overlay (NACK/toast) | No duplicated selection logic at call sites |

Text is the 5×7 bitmap font from the hardware test, scaled 1–4×. Continuity is
drawn with **shape as well as colour** — filled circle (GOOD), triangle
(MARGINAL), ring (OPEN), diamond (SHORT) — so the grid survives red-green
colour blindness, per FSD §10.2.0.

### Screens implemented (FSD §10.2)

Splash + progress bar, firmware mismatch, main status (top bar with RSSI bar /
ping RTT / both battery gauges, 4×2 continuity grid, legend, arm-sense line,
context prompt), armed (pulsing red border, large channel number, arm-sense
confirmation), pre-fire/firing (100 ms countdown, then "IGNITION ACTIVE" on
red), fire complete (2 s with return countdown), link lost (amber), error, and
the 3 s NACK overlay. All 14 Phase 4 development tasks are now DONE.

### Supporting changes

| Change | File | Purpose |
|---|---|---|
| `remote_fsm_get_status()` | `rlc_remote_fsm.c/h` | Spinlock-guarded snapshot of the cached STATUS_UPDATE (continuity bands, base battery, arm sense, error flags). All 5 cache-update sites refactored through a new `cache_status()` helper. |
| `remote_fsm_get_prefire_remaining_ms()` | `rlc_remote_fsm.c/h` | Drives the pre-fire countdown |
| `rlc_link_status_t.ping_rtt_ms` | `rlc_link.c/h` | PING→PONG round-trip computed in `handle_pong()` for the top bar |
| Display health check | `rlc_remote_main.c` | FSD §9.13 step 6 / T-S10: `display_init()` failure **or** a zero ID read-back halts the remote in ERROR |
| FSM display hooks | `rlc_remote_fsm.c` | The three `/* Phase 4 */` placeholders became real calls, plus NACK overlays on ARM/FIRE rejection, toasts for local rejections (arm key off, battery low, stale status, degraded link), and `display_fire_complete()` |
| `do_enter_error_text()` | `rlc_remote_fsm.c` | All 6 battery-critical paths now latch "REMOTE BATTERY CRITICAL" so the ERROR screen says something |

### On-target result

Flashed and booted successfully:

```
I (1584) rlc_disp: ILI9488 init: 480x320 RGB666 @ 20 MHz, ID 0x2A403300 (healthy)
I (1584) rlc_disp: display task started (prio 2, core 1)
```

Links to the base in ~30 ms, no watchdog trips, no crash. T-D01 (panel ID
read-back) **PASS**; T-D02…T-D09 (visual layout checks) still pending — the
layouts have not been verified by eye.

**Bench caveat:** with no LiPo connected the remote's battery ADC reads 0 mV, so
the FSM enters ERROR at ~4.9 s (pre-existing Phase 2/3 behaviour) and the panel
sits on the ERROR screen. Connect the remote battery to reach IDLE and see the
main status screen.

### Remote serial port changed

The documented remote by-id `usb-1a86_USB_Single_Serial_5B5E042156-if00` no
longer exists. Enumerated `/dev/serial/by-id/` and confirmed by `read_mac`:

| Port | MAC | Unit |
|---|---|---|
| `usb-1a86_USB_Single_Serial_5B5E043219-if00` | `ac:a7:04:e2:f2:8c` | **Remote** |
| `usb-1a86_USB_Single_Serial_5B5E044219-if00` | `44:1b:f6:d4:0d:68` | Base |

`build_remote.sh` and `Development_Progress.md` updated to `…5B5E043219`. (Note:
`esptool` in this IDF v5.4.1 install takes `read_mac`, not `read-mac`.)

### Splash screen refinements (follow-up)

- New `SPLASH_MIN_DURATION_MS` in `rlc_config.h` (5 s, then raised to **10 s** on
  request). The display task holds the splash for that long from
  `display_init()` regardless of how fast the link comes up — linking completes
  in well under a second, which is too fast to read. ERROR and firmware
  mismatch still take precedence over the hold.
- While the hold runs after linking, the status line switches to "Connected to
  base" in blue with live RSSI, and the progress bar counts the remaining hold
  down, so the screen reads as deliberate rather than stuck.
- Added `VRO - VLAAMSE RAKET ORGANISATIE` (blue, under a divider rule) and
  `(C) 2026 David Steeman` (footer). The 5×7 font has no `©` glyph.
- Battery gauge endpoints moved out of the display into `rlc_config.h` as
  `REMOTE_VBAT_FULL_MV` / `BASE_VBAT_FULL_MV`, beside the thresholds they must
  track.

### Battery threshold findings (from a "remote power fail" report)

The user reported the base flagging a remote power failure while running from a
12.8 V supply. Two findings, neither of them a display bug:

1. **The base never checks the remote's battery.** FSD §7 (line 1357) requires
   NACK `0x0C` ("REMOTE BATTERY LOW") when the PING-reported remote voltage is
   below `REMOTE_VBAT_MIN_ARM_MV`. `remote_battery_voltage_mv` arrives in every
   PING but nothing in `components/rlc_base/` reads it; `check_arm_guards()`
   tests only the base's own pack. **Requirement not implemented.**
2. **`rlc_config.h` still holds the bench-test overrides** — 3200 / 3100 /
   3000 mV, sized for the 3.3 V USB rail, versus the FSD §5.6.2 production 2S
   values 7000 / 6600 / 6400. As shipped the remote would arm on a 2S pack at
   3.3 V per cell. Must be switched back (along with `REMOTE_VBAT_FULL_MV`
   4200 → 8400) before field use.

What the user was actually seeing is the **remote judging itself**:
`rlc_remote_battery.c` samples GPIO 1 (ADC1_CH0) every 1 s through the
18 kΩ/10 kΩ (2.8:1) divider; below `REMOTE_VBAT_CRITICAL_MV` it posts an
edge-triggered `EVT_BATTERY_CRITICAL` and the remote FSM enters STATE_ERROR
(unrecoverable). The bench reading is **0 mV — the divider is unfed**, not a
flat pack; USB power does not energise the VBAT sense. Also flagged: the
divider is sized for 8.4 V full scale, so feeding the remote's battery input
from 12.8 V would put ~4.6 V on GPIO 1, above the 3.3 V absolute maximum —
the same failure class as bug #18.

### Base 8-pixel status strip + shared colour config

An 8-way NeoPixel strip is wired to the base's `PIN_RGB_LED` (GPIO 48), sharing
the data line with the DevKit's built-in NeoPixel — the built-in LED therefore
mirrors pixel 0 (channel 1). One pixel per igniter channel:

| Continuity | Colour | Constant |
|---|---|---|
| GOOD | dark green `#006400` | `RLC_COLOR_CONT_GOOD` |
| MARGINAL | light green `#90EE90` | `RLC_COLOR_CONT_MARGINAL` |
| OPEN | yellow `#FFFF00` | `RLC_COLOR_CONT_OPEN` |
| SHORT | red `#FF0000` | `RLC_COLOR_CONT_SHORT` |

Defined once in `rlc_config.h` as HTML `0xRRGGBB` values and used by **both**
the strip and the remote display's channel grid, so pad and handheld always
agree; restyling is a one-line change.

Other strip uses (the user invited these):

| Pattern | Strip |
|---|---|
| `IDLE` | Channel map (base only; the remote's single pixel keeps solid green) |
| `IDLE_ARM_ON` | Map breathing 100 %/25 % — status stays readable while the key-ON warning stays obvious |
| `BOOT`/`LINKING` | RSSI bar once the peer is heard (green ≥ −60, amber ≥ −80, red below); blue pulse until then |
| `ERROR` | Red triple flash unchanged, map dimmed to 20 % in the 700 ms gap |
| `ARMED`, `PRE_FIRE`, `FIRING` | **Unchanged** whole-strip red per FSD §11 — the firing-path signal should not be diluted into a data display |

New driver API: `rlc_rgb_led_set_channel_bands()`, `set_active_channel()`,
`set_rssi()`. Fed from the base **housekeeping loop** every 100 ms rather than
from the FSM, so the fire path is untouched.

Two consequences worth noting:

- **Deviation from FSD §10.2.0**, which specifies blue for GOOD precisely to
  avoid a red-green pair for colour-blind operators. Display shape coding still
  carries the meaning without colour, but on the strip colour is the only
  channel, and dark-green vs light-green at `RGB_LED_BRIGHTNESS` 30 differ
  mostly in brightness. FSD needs updating or the palette reverting.
- About 20 display call sites had been borrowing `C_GOOD`/`C_OPEN`/`C_MARGINAL`
  as generic blue/red/yellow accents. Left alone, the ERROR frame and the word
  "ARMED" would have turned yellow. They now use dedicated
  `C_FAULT`/`C_WARN`/`C_INFO`/`C_GREEN`.

### Display legibility — scale 2 is the floor

Field feedback: scale-1 text (6x8 px/char) is unreadable at arm's length;
"Connected to base" (scale 2, 12x16 px) is the reference size. Nothing is now
drawn below scale 2 — the only remaining scale-1 arguments in `rlc_display.c`
are frame and rule thicknesses in pixels.

Tripling the area of every small string forced layout changes: channel cells
shortened 86 → 80 px to free two scale-2 status rows, and nine strings
abbreviated to fit 480 px at 12 px/char (`Turn ARM key, then hold encoder to
arm channel N` → `TURN ARM KEY TO ARM CH N`, `HOLD FIRE BUTTON - RELEASE TO
ABORT` → `RELEASE TO ABORT`, and so on — full table in
`Development_Progress.md`). The NACK overlay no longer falls back to scale 1
for long strings; every NACK reason string fits at scale 2.

Tightest fits to watch in daylight: `MARGINAL` is 96 px in a 118 px cell, and
the arm-sense row runs to 420 px of 480 at its widest.

### Notes

- Base firmware rebuilt clean after the `rlc_link.h` change — the base unit was
  not flashed or otherwise touched.
- A full-screen redraw is ~460 kB over SPI (~180 ms at 20 MHz), which exceeds
  one 100 ms frame. That only happens on screen changes; steady-state frames
  push a few kB. Worth measuring properly under T-D09.
- `task_wdt: esp_task_wdt_reset(): task not found` at boot is pre-existing and
  unrelated to this work.
- **The base was never flashed this session** — the strip changes are built at
  `build_base/rlc.bin` and await a flash when the pad side is clear. The remote
  was reflashed after every change and boots clean each time.
- A `README.md` was added to the repository this session.

---

## 2026-08-17 — Bug #18 audit, firmware channel gate, as-built hardware deviations (FSD v1.17)

Focus: the bug that has now destroyed two base ESP32s during fire-path testing
(Dev-Progress bug #18 — relay arc coupling VBAT onto unclamped GPIO inputs).

### Audit: the software half of the fix is complete and correct

- `relay_all_safe()` (`components/rlc_base/src/rlc_relay.c`) de-energises the arm
  relay first, waits `RELAY_ARM_RELEASE_MS` (20 ms), then drops the channel relays.
- Traced every call site: **all 13** de-energise paths in `rlc_base_fsm.c` route
  through `relay_all_safe()` (end-of-pulse, cease-fire, disarm, key-off,
  arm-sense-lost, link-lost, error entry). `relay_fire_set(ch, true)` at the
  PRE_FIRE→FIRING transition is the **only** place a channel relay is energised.
  Nothing bypasses the ordering. No code change needed here.

### The software fix does not remove the hazard — three findings

1. **It covers the break, not the make.** The arm relay energises on entry to
   ARMED, so VBAT is already live on the fire bus when the channel contact
   transfers NC→NO at fire start. Bounce/arc at *make* can still couple VBAT
   toward the NC contact (the unclamped ADC pin). No relay ordering closes that
   window — only the clamp diodes + contact snubber do. The clamps are
   **mandatory**, not belt-and-braces.
2. **Nothing in firmware prevented firing channels 2–8**, which have no clamps.
   "Test channel 1 only" was operator discipline recorded in a changelog. One
   encoder mis-turn = third dead ESP32. → fixed this session (see below).
3. **The arm relay is now the sole contact breaking 6 A DC.** Its failure mode
   under unsnubbed DC arcing is contact **welding** — and a welded arm relay
   leaves VBAT permanently on the fire bus, defeating the primary fire-path
   interlock. `weld_check()` detects it (hard ERROR, power-cycle to clear), so it
   fails safe, but all switching wear now lands on the one contact the safety
   case depends on.

### As-built hardware deviations (confirmed with the user)

The FSD described protection that is **not installed**:

| Item | FSD says | As-built 2026-08-17 |
|---|---|---|
| GPIO 21 arm sense | 27 kΩ/10 kΩ divider + 3.3 V zener | divider only — **no zener** |
| GPIO 42 key sense | 27 kΩ/10 kΩ divider + 3.3 V zener | divider only — **no zener** |
| Arm relay contact | (snubber assumed) | **no snubber** |
| Ch 1 continuity ADC | — | clamp diodes + snubber fitted |
| Ch 2–8 continuity ADC | — | **unprotected** |

Risk correction made this session: GPIO 21 is **not** in the same class as the
dead ADC pins. The continuity front end is `3.3V → 3.3 kΩ → sense node → NC
contact` with the ADC pin tapping the sense node — i.e. **zero** series
resistance to VBAT, hence instant death. GPIO 21 has 27 kΩ in series, so DC VBAT
is ~0.33 mA into the pin's internal clamp (survivable); its exposure is
inductive spikes at contact break (~200 V → ~7 mA, marginal and cumulative).

**Protection BOM to fit:**

| Part | Where | Purpose |
|---|---|---|
| BAT54S dual Schottky (mid→GPIO, ends→3V3/GND) + ~10 nF to GND, or the spec'd 3.3 V zener | GPIO 21, GPIO 42 | clamp spike excursions on the fire-path sense nodes |
| 47 Ω 0.5 W + 100 nF film, ≥ 100 V | across arm relay contact | suppress the 6 A DC break arc |
| SMBJ18A/20A-class TVS | arm relay COM → GND | clamp the inductive kick at the sensed node |

### Firmware change — bug #18 channel gate

New in `components/rlc_common/include/rlc_config.h`:

```c
#define FIRE_PROTECTED_CHANNEL_MASK    0x01  /* channel 1 only (2026-07-21) */
#define CHANNEL_IS_PROTECTED(ch) \
    (((ch) >= 1) && ((ch) <= NUM_CHANNELS) && \
     ((FIRE_PROTECTED_CHANNEL_MASK >> ((ch) - 1)) & 1u))
```

- `guard_arm()` (`rlc_base_fsm.c`) — new **guard 4b** NACKs ARM on any channel
  outside the mask. Reuses `NACK_INVALID_CHANNEL` so the **wire protocol and the
  remote firmware are unchanged**; the real reason is logged on the base.
  Deliberately ordered **after** guard 4 (already-armed) so **T-A05** still
  returns `NACK_CHANNEL_ALREADY_ARMED` (0x0A) as the test spec expects.
- `relay_fire_set()` (`rlc_relay.c`) — refuses to energise an unprotected
  channel relay (last line of defence). De-energising is **always** allowed.
- `relay_init()` — logs a warning every boot while the mask != `0xFF`:
  `bug #18 gate ACTIVE — firing allowed on mask 0x01 only`.
- **Bump the mask to `0xFF` once channels 2–8 get their clamps + snubbers.**

Base firmware builds clean (`./build_base.sh`, `base_app_main` verified in
binary). **Not flashed** — no hardware was connected this session.

### Documentation

- `Development_Progress.md` — bug #18 section rewritten with the audit result,
  the gate, the residual make-window, the arm-relay wear/weld path, the as-built
  table and the BOM. Phase 3 fire-test note now says channel-1-only is
  **enforced in firmware**, not just by operator discipline.
- `RLC_Functional_Specification_v1_14.md` → **v1.17** (2026-08-17) with an
  as-built deviation callout in §5.4.3, "NOT FITTED" markers on the §5.4.3 /
  §5.4.3b protection rows, the §5.4.9 circuit diagram, and a changelog row.
  (Filename still says `v1_14` — stale, content is v1.17.)

### Operator decision recorded

Chosen: **keep the gate at `0x01`, fit the GPIO clamps before the next fire
pulse**, snubber + TVS before an extended campaign. Rejected alternatives were
gating everything to `0x00` (blocks the whole G3 campaign) and proceeding with no
hardware changes.

### Notes / follow-ups

- **Before the next fire pulse:** fit BAT54S (or zener) on GPIO 21 and GPIO 42.
  This is the only item standing between the project and resuming G3.
- **Reflash both units** — the remote still has not been flashed since the chip #2
  MAC change (`AC:A7:04:E2:F2:8C`), and the base binary now carries the channel
  gate. Re-verify LINK_ACK and confirm the new boot warning appears in the base log.
- **T-F08 (scope timing):** the delivered igniter pulse is now
  `FIRE_PULSE_DURATION_MS` **+ arm-relay release time**, because current ends when
  the arm relay opens, not when the channel relay does.
- Base pack was reading ~6.6 V at the end of the 2026-07-21 session — verify it is
  not over-discharged before connecting.
- Doc/hardware mismatches like the phantom zener are exactly the defect class the
  v1.16 review pass was chasing; worth a dedicated as-built audit of the base board
  against §5.4 before the campaign.

## 2026-07-31 — Remote chip #2 bring-up (chip #1 flash-damaged)

### Remote MAC update
- Remote chip #1 (`44:1B:F6:81:F1:70`) suffered **flash damage** and was replaced with **remote chip #2**, MAC **`AC:A7:04:E2:F2:8C`** (dated 2026-07-22).
- Updated `REMOTE_MAC_ADDR` in `components/rlc_common/include/rlc_config.h`, with an inline comment recording the old chip's MAC and failure cause.
- Memory index (`reference_serial_ports.md`) refreshed to track remote/base by stable by-id serials + current MACs (`/dev/ttyACMx` numbers are volatile).

### Notes / follow-ups
- The remote's **native-USB by-id** path embeds the chip MAC, so it changes with this swap — prefer the remote's **COM-port** by-id (`usb-1a86_USB_Single_Serial_5B5E042156-if00`, stable across chip swaps). See the 2026-07-21 by-id table.
- Reflash the remote (full image) with the new firmware so ESP-NOW peering matches the updated MAC; re-verify the base↔remote LINK_ACK.
- Also asked/answered this session: the base ESP32 fry during the fire pulse (Dev-Progress bug #18) — root cause was `relay_all_safe()` de-energising channel relays before the arm relay, arcing 12 V/6 A onto the unclamped continuity ADC inputs (GPIO 2–10) → latch-up. Fix = reverse the order (arm relay OFF → wait 20 ms → channels OFF) **plus** Schottky clamp diodes on GPIO 2–10.

## 2026-07-21 — Display validation, doc review (FSD v1.16), base chip #3 bring-up, USB by-id migration

### Remote display validation (Phase 4 de-risked)
- **Problem:** the remote ILI9488 SPI display showed nothing.
- **Two root causes:** (1) the *main* remote firmware's display driver is still a Phase-4 stub (`components/rlc_remote/src/rlc_display.c`); (2) MISO/MOSI were physically swapped on the remote.
- **Fix:** validated the panel with the `rlc-hw-test-remote` firmware (real ILI9488 driver at `rlc-hw-test-remote/main/hw_display.c`). Panel reads ID **`0x2A403300`** (non-standard — an ILI9488-class clone), inits and paints correctly (RGB666, 20 MHz, SPI2). Corrected the MISO/MOSI swap; MOSI/SCLK confirmed canonical.
- **Console gotcha:** the hw-test-remote CLI runs over **USB-Serial/JTAG (native USB port)**, not UART. Connect with `minicom -b 115200 -D <native-USB-by-id> -o`.

### Documentation review → FSD v1.16 (commit `531faed`)
- Ran two review agents + manual verification. Findings: the uncommitted FSD had been **reverted v1.15 → v1.14** (re-introducing the key-sense/GPIO-42 arming circular-dependency bug), buzzer/alarm timings had drifted from `rlc_buzzer.c`, watchdog 2 s vs coded 5 s, ILI9488 "expected ID" wording, hw-test console claims, stale version citations.
- Restored FSD to v1.15, applied all corrections, bumped to **v1.16** with a changelog row.

### Base chip #3 bring-up (commit `7b28b3a`)
- Base chip #2 (`…FA:F8`) was destroyed in the fire-test overvoltage (Dev-Progress bug #18). Installed chip #3; read its MAC via esptool (BOOT+RESET into download mode): **`44:1B:F6:D4:0D:68`**.
- Updated `BASE_MAC_ADDR` in `components/rlc_common/include/rlc_config.h`; reflashed **both** base and remote (full images). ESP-NOW link verified (LINK_ACK, rssi=-35). **G0 smoke passes** with chip #3.
- Hardware protection installed on **channel 1 only**: clamping diodes on the ADC input + snubber across the relay contact. Channels 2–8 still unprotected → **test channel 1 ONLY**.

### USB by-id migration (commit `bbe0df1`) + global preference
- Replaced every `ttyACMx`/`ttyUSBx` reference with stable `/dev/serial/by-id/` paths across `build_base.sh`, `build_remote.sh`, `build.sh`, `Development_Progress.md`, both hw-test specs, and `tools/test_tr04.py`.
- Convention: prefer each board's **COM-port** by-id (UART-bridge serial — stable across ESP32 chip swaps) over the native-USB by-id (which embeds the chip MAC and changes on every swap).

| Board | Port | by-id | Verified |
|---|---|---|---|
| Base | COM | `usb-1a86_USB_Single_Serial_5B5E044219-if00` | yes (MAC D4:0D:68) |
| Remote | COM | `usb-1a86_USB_Single_Serial_5B5E042156-if00` | yes (MAC F1:70) |
| Base | native USB | `usb-Espressif_USB_JTAG_serial_debug_unit_44:1B:F6:D4:0D:68-if00` | volatile (chip MAC) |
| Remote | native USB | `usb-Espressif_USB_JTAG_serial_debug_unit_44:1B:F6:81:F1:70-if00` | stable (remote not swapped) |

- Note: `usb-1a86_…_56B6002627…` (ttyACM1) is an **unrelated radiosonde receiver** (RS41spoofer project), NOT the RLC base — a prior assumption that it was the base was wrong.
- Created global **`~/.claude/CLAUDE.md`** with a cross-project rule: always identify USB serial devices by stable by-id, never `ttyACMx`/`ttyUSBx`.

### Phase 3 testing — resuming (channel 1 only)
- Blocker resolved (chip #3 + channel-1 protection). Next: G2 arming (T-A01..T-A15), then G3 fire (T-F01..T-F09) on channel 1, then T-R06 (POST_FIRE idempotent ACKs). T-R05 (multi-arm) stays SKIP — no fault-injection path; code-reviewed.
- Pending: connect batteries (base 3S ~12 V, remote 2S) + a channel-1 continuity load; bring base key switch ON.
- **Power note:** battery + USB serial together is the intended setup (ESP32 sees 3.3 V from a regulator either way; relays need the real battery). Main residual risk = USB **backfeed** to the host → use a USB isolator/hub. Base was reading ~6.6 V at session end — verify the pack isn't over-discharged before use.

### Commits this session (branch `docs/fsd-v1.16-accuracy-corrections`)
- `531faed` FSD v1.16 — documentation accuracy corrections after display validation + review
- `7b28b3a` base: update BASE_MAC_ADDR for chip #3
- `bbe0df1` docs: reference USB serial ports by stable by-id (never ttyACMx)

### Notes / follow-ups
- **Channel-1-only testing** until channels 2–8 receive the clamping diodes + snubber.
- Use a **USB isolator** when connecting batteries (protects the host from backfeed and from relay-arc/ground transients).
- `RLC_Project_Summary.md` remains untracked (pre-existing; user's call whether to commit).
