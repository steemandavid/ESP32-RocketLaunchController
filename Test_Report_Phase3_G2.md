# Test Report — Phase 3 G2 (Arming Suite) + Bug #29 Continuity-Loss Tests

**Date:** 2026-08-26
**Tester:** Code Test Agent (log analysis) + operator (hardware)
**FSD Reference:** RLC_Functional_Specification_v1_14.md (v1.37)
**Firmware tested:** 1.1.2 → 1.1.4 (changed mid-campaign, see §4)
**Commit:** af5d319
**Scope:** FSD §15.2 arming tests T-A01…T-A18, on target, both units

---

## Executive Summary

**14 PASS, 0 FAIL, 2 not runnable as written, 2 deferred to a fault-injection
harness.** The three new continuity-loss disarm tests (T-A16/17/18) added with
bug #29 all pass, including the regression guard that a *non-armed* channel
going OPEN must not disarm.

The campaign found **one firmware defect** (remote fails silently when an ARM
cannot be granted — fixed in 1.1.4), **three stale FSD tests** that could not
pass as written, and produced **one operator-driven configuration change**
(`PRE_FIRE_DELAY_MS` 2000 → 5000). Two Phase 5 safety tests, **T-S03** and
**T-S14**, were demonstrated incidentally and are recorded as evidence.

One igniter was expended during T-A17 when the 2 s countdown proved too short
to act inside — the direct cause of the countdown change.

---

## 1. Results

| ID | Test | Result | Evidence |
|----|------|--------|----------|
| T-A01 | ARM, continuity GOOD | **PASS** | `IDLE -> ARMED (ch 1, sense verified)`; non-blocking arm-verify completed in 170 ms (review finding M1 exercised on target). Siren **continuous**, display and RGB confirmed by operator |
| T-A02 | ARM with base key OFF | **PASS** | `NACK type=0x20 reason=0x01 (BASE KEY OFF)` |
| T-A03 | ARM with remote key OFF | **PASS** | No ARM traffic reached the base — remote blocked locally, display "TURN ARM KEY FIRST" |
| T-A04 | ARM channel with OPEN continuity | **PASS** | `NACK type=0x20 reason=0x04 (NO CONTINUITY)` on ch4 |
| T-A05 | ARM second channel while armed | **N/A** | Unreachable via the operator UI — see §3.1 |
| T-A06 | Base key OFF while armed | **PASS** | `Key switch OFF during ARMED — disarm`; arm sense went LOW at +10 ms, *before* the FSM finished at +20 ms — the hardware leg acting independently of software |
| T-A07 | Remote arm switch to DISARM while armed | **PASS** | `ACK type=0x21` then `DISARMED -> IDLE`; relay released 160 ms later — the reverse ordering to T-A06, confirming a different path |
| T-A08 | Rotate encoder while armed | **PASS** | Remote sent `CMD_DISARM`; operator confirmed channel selection advanced to CH2 |
| T-A09 | Bands visible with key in SAFE | **PASS** | ch1 206 mV CONN, ch2 215 mV CONN, ch3 268 mV MARG, ch4-8 969 mV OPEN, all with key SAFE. Band list in the FSD corrected — see §3.2 |
| T-A10 | ARM with arm sense fault | **PASS** | `arm verify started` with no sense confirmation → `arm sense verify timeout (200 ms)` → `NACK reason=0x0b (ARM SENSE FAULT)` |
| T-A11 | ARM with stale STATUS_UPDATE | **DEFERRED** | Not externally inducible — see §5 |
| T-A12 | ARM with low remote battery | **PASS** | Remote at 6.8 V (between MIN_OPERATE 6600 and MIN_ARM 7000). Zero ARM frames reached the base; display "REMOTE BATTERY LOW" |
| T-A13 | Wrong channel in CMD_ACK | **DEFERRED** | Not externally inducible — see §5 |
| T-A14 | ARM with MARGINAL continuity | **PASS** | `IDLE -> ARMED (ch 3)` with ch3 at 269000 µV = MARGINAL. Note: the 68 Ω test load sits only 8 mV above the 261 mV threshold with 5 mV hysteresis — a valid MARGINAL but close to the boundary |
| T-A15 | ARM with SHORT continuity | **N/A** | Band no longer exists — see §3.3 |
| T-A16 | Disconnect armed igniter while ARMED | **PASS** | Band change and `Continuity OPEN on armed ch 1 during ARMED — disarm` in the **same millisecond**; IDLE at +20 ms; relay physically out at +170 ms. Detection latency 920 ms from arming, within the predicted ~800 ms round-robin window |
| T-A17 | Disconnect armed igniter during PRE_FIRE | **PASS** | `Continuity OPEN on armed ch 1 during PRE_FIRE — abort`, no `Fire timer started`. A repeat `CMD_FIRE` arriving 40 ms later was NACKed `WRONG STATE`, proving the fire button was still held |
| T-A18 | Disconnect a NON-armed channel while armed | **PASS** | Three ch2 band transitions while ch1 armed; base stayed ARMED throughout and ended only on the 10 s arm timeout. No `Continuity OPEN on armed ch 1` anywhere |

**Totals: 14 PASS / 0 FAIL / 2 N/A / 2 DEFERRED out of 18.**

### 1.1 Safety tests demonstrated incidentally

| ID | Test | Result | Evidence |
|----|------|--------|----------|
| T-S03 | Base below VBAT_CRITICAL → ERROR | **PASS** | Base taken to 7978 mV (threshold 9000) → `-> ERROR (flags=0x02: VBAT CRITICAL)`. Correctly stayed latched at 12.1-12.7 V afterwards; cleared only by power cycle, as designed |
| T-S14 | Arm timeout (10 s auto-disarm) | **PASS** | `ARM TIMEOUT (10022 ms)` against a 10000 ms constant — 22 ms jitter. Observed on several runs |

---

## 2. Finding — remote fails silently when an ARM cannot be granted

**Severity: MAJOR (usability / operator safety). Fixed in firmware 1.1.4.**

Reported by the operator during the T-A12 setup: with the base in ERROR, a
long-press to arm produced **no beep, no message and no transmission**. Three
causes compounded:

1. **The base discards commands in ERROR without a NACK.** Its handler is a
   bare `break;` — deliberately inert. `CMD_ARM` disappears with no reply.
2. **The remote's ACK-timeout branch was the only failure path with no
   feedback.** All four guards, NACK and channel-mismatch each beep and toast;
   timeout only wrote a log line. A base that never answered was
   indistinguishable from a long-press that had not registered.
3. **The remote never inspected `base_state == STATE_ERROR`**, despite
   receiving it in every STATUS_UPDATE.

**Fix (remote-side):** a new arming guard refuses locally when the cached fresh
status shows the base in ERROR, naming the fault — `BASE ERROR: VBAT CRITICAL`.
Refusing locally is more robust than waiting on a NACK the base cannot send,
and naming the flag tells the operator which fault to investigate. The
ACK-timeout path now also beeps and shows `NO RESPONSE FROM BASE`, covering
every other no-reply case.

**Left open deliberately:** whether the base should NACK commands from ERROR
rather than discarding them. That changes the contract of a deliberately
terminal state and was not altered unilaterally. The remote-side guard resolves
the reported symptom.

**Verification owed:** drop the base below 9 V to latch ERROR, restore the
voltage, then attempt to arm — expect a triple beep and the named toast.

---

## 3. Stale specification tests

### 3.1 T-A05 is unreachable through the operator UI

T-A05 requires arming a second channel while one is armed. Selecting a second
channel requires an encoder rotation, and **T-A08 requires that an encoder
rotation while armed disarms immediately**. Satisfying T-A08 destroys T-A05's
precondition. Confirmed on target: the remote sent `CMD_DISARM` (0x21), never a
second `CMD_ARM`.

The `guard_arm()` guard 4 returning NACK 0x0A **remains correct and required**
— it is defence-in-depth against a remote-side bug, a replayed frame or a
malformed command, exactly the cases where the base must not assume the remote
disarmed first. The guard belongs in host tests; the on-target test does not.

### 3.2 T-A09's band list was stale

Its expected result listed a SHORT band. Corrected to CONNECTED / MARGINAL /
OPEN with the as-built resistance boundaries.

### 3.3 T-A15 is superseded

SHORT was merged into CONNECTED on 2026-08-21 (bug #26) because the distinction
sits below the measurement floor at the specified 1 mA test current.
`rlc_continuity_class.c` has three bands and `CONT_SHORT_UV` is unused. A 0 Ω
load arms normally and displays CONNECTED; there is no orange SHORT warning to
observe. The underlying case is covered by T-A01.

---

## 4. Configuration change — PRE_FIRE_DELAY_MS 2000 → 5000

T-A17 requires disconnecting an igniter *during* the pre-fire countdown. At
2000 ms the operator could not act in time and **an igniter fired**. The
countdown was temporarily raised to 10 s to complete the test, then settled by
operator decision at **5000 ms** as the operating value — long enough to act
inside, short enough not to invite the fatigue-release the original value was
chosen to avoid. FSD §14.1 updated; both units flashed together, since the
remote runs its own countdown against the same constant.

### 4.1 What the accidental ignition confirmed

The unplanned fire pulse validated the **FIRING exclusion** in bug #29's
scoping. 680 ms into the pulse the armed channel's band went OPEN — the relay
had switched to NO and physically disconnected the sense line, exactly as
designed — and the FSM correctly ignored it. Had the continuity-loss disarm
been applied to FIRING as well, that pulse and every future one would have
aborted mid-fire.

Also confirmed: PRE_FIRE ran exactly 2000 ms; `CMD_CEASE_FIRE` during FIRING
cut the pulse at ~810 ms of 1000 ms and went straight to safe (§7.2.5); and
post-fire continuity read OPEN, the spent-igniter indication of §7.3.1.

---

## 5. Deferred — T-A11 and T-A13 need a fault-injection harness

Neither is inducible from outside the firmware.

- **T-A11 (stale STATUS_UPDATE):** link loss triggers at 1.5 s, well before the
  5 s staleness timeout, so a stale-but-linked state cannot be produced by
  interfering with the radio. Note the FSD says ">4s" while
  `STATUS_STALE_TIMEOUT_MS` is 5000 — a minor drift worth reconciling.
- **T-A13 (wrong channel in CMD_ACK):** requires the base to emit a malformed
  ACK.

Both injections are **base-side only**: suppressing STATUS_UPDATE while
continuing to answer PINGs reproduces T-A11 exactly, and emitting the next ARM
ACK with a wrong channel reproduces T-A13. Agreed approach: a
`CONFIG_RLC_FAULT_INJECTION` Kconfig option defaulting to off, driven over the
base's UART, with a boot banner and a `#warning` so an injection build cannot be
mistaken for a real one.

---

## 6. Siren bench tests — review finding N2 closed

Run 2026-08-26 after the bug #27 driver was fitted. **Six checks, all PASS.**
N2 had rested on code inspection alone since 2026-08-21 because the siren
output was not connected.

| # | Check | Result |
|---|---|---|
| A | Silent at power-on | **PASS** — silent throughout boot; the 10 kΩ gate pull-down holds GPIO 40 through the high-impedance window before `siren_init()` |
| B | Continuous across ARMED→PRE_FIRE (bench test 2 / N2 case 2) | **PASS** — unbroken across the transition |
| C | Stops and stays stopped after disarm (bench test 3 / N2 case 1), all three routes | **PASS** ×3 — CEASE_FIRE, arm timeout, key OFF; 10 s silence after each |
| D | LINK_LOST = 4 cycles of 500/500 | **PASS** — 4 counted, confirming `SIREN_LINK_LOST_CYCLES` derives from the constant (minor m10) |
| E | Link recovery **mid-pattern** → immediate, permanent silence | **PASS** — log shows recovery at 3080 ms, inside the 4000 ms pattern |
| F | ERROR = 3 blasts at 200 ms, then silence | **PASS** |

### 6.1 What these prove, and what they do not

**Test B is weaker than it appears, and that is now acceptable.** Fire was
released ~1.5 s into the 5 s countdown, so the siren was observed continuous
across the *transition*, not through a full countdown. N2 case 2 required a
stale tick from a running periodic timer; ARMED no longer runs one, since the
pulse was removed in firmware 1.1.2. There is no longer a mechanism to test.

**Test E is the check that exercised N2's surviving failure mode.** Case 1
(output stuck ON after `siren_off()`, timer stopped, nothing left to clear it)
depended on the infinite `-1` pattern that went away with the ARMED pulse — so
the original bench test 3 can no longer reach it. The risk migrated to
LINK_LOST and ERROR, which remain periodic. Test E fires `siren_off()` from the
link-recovery path (§7.2.8) against a pattern that is genuinely mid-cycle,
which is the only remaining route to a parked callback re-driving the output.
The 3080 ms recovery timestamp confirms the window was hit rather than missed.

**If a future change reintroduces a periodic ARMED pattern, test E is the
regression test to re-run — not test B.**

---

## 7. Recommendation

The arming path is in good shape: every operator-reachable arming guard and
every disarm trigger behaves as specified, and the bug #29 continuity-loss
disarm is verified in both ARMED and PRE_FIRE with its regression guard intact.

Before G3 fire testing:

1. ~~Verify the 1.1.4 silent-failure fix on target~~ — **DONE 2026-08-26:
   operator confirmed the triple beep and `BASE ERROR: VBAT CRITICAL` toast
   where the long-press had previously been silent (§2).**
2. **Build the fault-injection harness** and close T-A11 and T-A13 (§5).
3. ~~Run the siren bench tests~~ — **DONE, all six PASS (§6). N2 closed.**
4. Note that **channels 2-8 have still never been fired**. Channel 1 has now
   fired once, unplanned, and the whole chain worked.
