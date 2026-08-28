# Test Report — Phase 5 On-Target Campaign, 2026-08-28 (second session)

Firmware 1.1.32 → **1.1.35**. Scope: the three cleanups, the MAJ-01 and
CRIT-01 review items still open after `Test_Report_Phase5_Review_Fixes.md`,
and the bug #29 regression retest (T-A16/T-A17/T-A18) required before live
fire. Host suite 467 checks / 0 failures at every build. Both units restored
to stock (non-injection) 1.1.35 at the end and verified free of injection
symbols.

Captures: dual-console logs timestamped with host wall-clock
(`tools/serial_log.py`, new this session), so base and remote lines correlate
offline. Injection keys were delivered by the logger's `--send-on` auto-fire
rule where the window was too tight to hand-time.

## 1. Cleanups (doc / log hygiene)

| Item | Result |
|---|---|
| FSD "silently ignores LINK_REQUEST while busy" wording (§6.4.1, §7.2.6, App D) | **DONE** — all three now state `LINK_REJECT_BUSY` (0x02), no session consequences; FSD v1.48 |
| `gptimer_stop: timer is not running` false ERROR, first shot of a power cycle | **DONE, verified on target (fw 1.1.33)** — zero driver ERROR lines across the session's first-shot fires; `s_running` mirror in `rlc_fire_timer.c`, BF-01 stop unchanged when the timer IS running |
| Development_Progress Phase Overview table stale | **DONE** — Phase 3/4 marked complete-with-residuals, Phase 5 IN PROGRESS |

## 2. MAJ-01 — base latches ERROR mid-pulse (fw 1.1.34 injection, new key `r`)

The existing base `e` key falsifies `base_state` to IDLE, so it could never
present a truthful ERROR to a remote in FIRING. New key `r` posts a real
`EVT_ARM_SENSE_FAULT` (handled from any base state), making the base report
ERROR truthfully — the exact MAJ-01 scenario.

The `r` key was auto-injected **13 ms into a live pulse** (rule armed on
`PRE_FIRE -> FIRING`):

- Base: `-> ERROR (flags=0x04: RELAY FAULT)` mid-FIRING, STATUS_UPDATE pushed
  with the truthful flags.
- Remote: left FIRING **16 ms** later via the 1.1.30 blacklist —
  `[TOAST] BASE ERROR: RELAY FAULT`; status band → **RELAY WELDED**.
- No "IGNITION ACTIVE" assertion over a dead pulse. **PASS.**

## 3. CRIT-01 — ALARM_CRITICAL from ARMED (remote `b` / `d` keys)

Both halves audible — the exact condition that was completely silent in
1.1.29:

- **`b` battery-critical (auto-injected at ARMED entry):** continuous urgent
  alarm from the ARMED→ERROR transition. **PASS (audible)** — and it found a
  real defect, below.
- **`d` display fault (auto-injected at ARMED entry):** same alarm, plus the
  base disarmed immediately — `CMD_DISARM` ACKed **1 ms** after the remote
  entered ERROR, base `DISARMED -> IDLE` 13 ms after that. **PASS.**

### New defect found: battery-critical from ARMED left the base armed

The `b` run's contrast with `d` exposed the gap: the ARMED
`EVT_BATTERY_CRITICAL` handler entered ERROR **without sending anything to
the base**, so the arm relay ran its full **10 s ARM TIMEOUT** while the
remote sat in terminal ERROR, unable to command the pad safe. The
display-fault handler sends `CMD_DISARM` before entering ERROR for exactly
this reason; the battery path never had it. §9.1's "battery critical … if
armed, disarm" row was therefore not conformed to.

**Fixed in fw 1.1.35** (mirror of the display-fault logic; PRE_FIRE/FIRING
were never exposed — their `CEASE_FIRE` already makes the base disarm).

**Re-verified on target (fw 1.1.35 injection, same auto-inject rule):**

| Event | Time |
|---|---|
| Remote `IDLE -> ARMED (ch 1)`, `b` injected | 19:11:02.223 |
| Remote latches ERROR, sends `CMD_DISARM` | 19:11:02.237 |
| Base ACKs `CMD_DISARM` (type 0x21) | 19:11:02.236 |
| Base `DISARMED -> IDLE`, relay opens | 19:11:02.249 |

**26 ms armed → safe** (was 10 s). Operator: "Remote fault + siren stopped
immediately." **PASS.**

## 4. Bug #29 regression — T-A16/T-A17/T-A18 before live fire

- **T-A16 (fw 1.1.33): PASS.** Halogen lead pulled while ARMED: base
  disarmed **10 ms** after the band change; remote toast
  `CONTINUITY LOST - DISARMED` **110 ms** end-to-end, with the
  `BEEP_CONTINUITY_LOST` pattern.
- **T-A17 (fw 1.1.33 → 1.1.34): PASS after a fix.** Lead pulled during the
  countdown with the fire button held: base aborted correctly both runs, but
  the first retest toasted raw `[NACK] WRONG STATE` — the NACK answering a
  CMD_FIRE repeat beat the cause-carrying STATUS_UPDATE by 7 ms at the base
  and reached the remote's FSM first. Fixed in **fw 1.1.34**: the repeat-NACK
  path never shows the raw reason, says `BASE ENDED SEQUENCE`, and latches
  the channel; the first status in IDLE settles it with the RM-07
  discrimination. Retest: `CONTINUITY LOST - DISARMED`. **PASS.** Both race
  orderings now covered on target; the NACK-wins latch path itself is
  verified by inspection only (no remote FSM host harness yet).
- **T-A18 (fw 1.1.35): PASS** — run later the same day once a 68 Ω resistor
  was fitted to CH2 (read `267000 uV / MARGINAL`). Pulled ~1.5 s after arming
  ch1: ch2 MARG→OPEN between two continuity frames, the base stayed ARMED
  through its full `ARM TIMEOUT (10022 ms)` — the next FSM event after the
  pull was the normal timeout, not a disarm — and the remote showed no
  CONTINUITY LOST toast, only `BASE DISARMED` at timeout. The RM-07
  discrimination is rightly silent for a non-armed channel (ch1 never left
  CONNECTED). **Bug #29 regression suite complete; cleared for live fire.**

## 5. Incidents and tooling

- **Stale port holder:** a leftover logger process from an earlier session
  held both serial ports — loggers captured nothing, one flash pair failed
  *silently* (pySerial "multiple access" swallowed by a pipe; `tail`'s exit
  code masked it) leaving both units on old firmware, and the remote's
  ESP-NOW wedged (txfail 100%). Recovery: kill the holder, esptool
  hard-reset both units. **Both units' firmware version was verified from
  the link logs before testing resumed.**
- **Post-flash link wedge:** once, immediately after a flash-reset boot, the
  units came up unlinked and stayed wedged (base BOOT, remote LINKING, zero
  link traffic). A clean `esptool --after hard_reset chip_id` on both
  cleared it. Suspect the wedge whenever a post-flash boot shows no LINK
  traffic at all.
- **`tools/serial_log.py` (new):** timestamped dual-console logger with
  auto-reopen on USB re-enumeration and `--send-on PATTERN:BYTES` one-shot
  auto-injection — how the 13 ms-into-pulse and at-ARMED-entry injections
  were landed.

## 6. Version trail

| fw | Change |
|---|---|
| 1.1.33 | base: first-shot `gptimer_stop` false ERROR silenced |
| 1.1.34 | remote: base-aborted countdown names its cause (T-A17 finding) |
| 1.1.35 | remote: battery-critical from ARMED disarms the base (CRIT-01 `b` finding) |

Stock (non-injection) 1.1.35 on both units at session end, linked, verified
free of injection symbols in the boot logs.
