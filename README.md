# ESP32 Wireless Rocket Launch Controller

A two-unit wireless launch controller for model rocketry, built on the ESP32-S3.
A **base unit** sits at the pad wired to up to 8 igniter channels; a handheld
**remote unit** controls it over an encrypted ESP-NOW link from a safe distance
(~200 m design target; measured 200 m at −93 dBm with external antennas, base on the ground — see FSD §6.1, where range near the ground is d⁴-limited and height matters far more than radio).

Developed for use with **VRO — Vlaamse Raket Organisatie**.

> ⚠️ **This system fires pyrotechnic igniters.** It is a personal project under
> active development and has **not** completed its hardening and field-test
> phase. Do not use it for live launches without reviewing the safety case in
> the functional specification, verifying your own build, and following your
> club's and national safety code.

---

## How it works

A single LCO (Launch Control Officer) runs the whole sequence: select a channel
on the remote, complete a deliberate multi-step arming procedure, then presses and **holds** a fire button. Releasing the button
at any point cuts current to the igniter — it is a dead-man switch, not a latch.

1. Both units power up and link automatically.
2. Operator selects a channel (1–8) with the rotary encoder.
3. The LCO turns the base unit's physical **base arm key** to ARM at the pad,
   then returns to the firing point.
4. The LCO turns the remote's physical **remote arm key** to ARMED.
5. Operator **long-presses** the encoder (500 ms) to send the arm command.
6. The base checks its guard conditions and energises the arm relay; the pad
   siren sounds continuously and stays on for the rest of the sequence.
7. Operator **presses and holds fire** → 5 s pre-fire countdown.
8. Channel relay closes for a fixed 1 s fire pulse.
9. Releasing the button at any time during 6–8 cuts power immediately.
10. Relays de-energise, the system returns to idle, and continuity is re-checked
    to confirm the igniter burned through.

## Safety design

No single hardware or software fault should be able to cause ignition. The full
argument lives in the functional specification (§7, §9, §13); in summary:

**Hardware** — two independent break points in the fire path (the arm relay and
the channel relay), both of which must close for current to reach an igniter.
The arm key switch is *not* one of them: it sits in the arm relay's **coil
drive** path, in series with a firmware-driven MOSFET, forming a hardware AND
gate. That is a stronger position than a third contact in series would be —
with the key in SAFE there is no coil current available, so no software fault
can close the first break at all. Plus fail-safe relay defaults with gate
pulldowns that hold during boot; passive battery-powered status LEDs that work
even with the ESP32 dead; ~1 mA current-limited continuity sensing; active-low
inputs so a broken wire reads as "safe".

**Firmware** — ten guard conditions before the arm relay may close; arm-sense
feedback that detects a welded relay; a dead-man repeat-message scheme during
firing; 10 s auto-disarm; and an unrecoverable ERROR state that requires a
physical power cycle rather than attempting to self-heal.

The fire button debounces **asymmetrically** — 80 ms to register a press, 20 ms
to register a release. Symmetric debouncing is right for a sensor and wrong for
a dead-man: a missed release fires an igniter the operator has let go of, while
a spurious release only aborts, which is the direction that cuts current. This
was a live defect until firmware 1.1.29, found by mashing the button in
edge-case testing — releases shorter than 80 ms were invisible, so the system
saw a continuous hold and fired.

The remote sounds two distinct state tones so the operator need not be looking
at the panel: a sparse ~0.8 Hz heartbeat while ARMED, and an insistent ~4 Hz
pattern through the pre-fire countdown and the pulse. The tempo gap is what
carries the meaning. They are deliberately unlike the two fault alarms, which
are both ~2.5 Hz, so "the pad is live" never sounds like "something is wrong".

A state tone is a *background* the player returns to, and changing it can never
silence anything: the player task polls for the change instead of being nudged
through its pattern mailbox. That distinction was a real defect until firmware
1.1.30 — the nudge was an atomic overwrite of a one-deep mailbox, so an alarm
queued in the same 50 ms tick as a state change was deleted before it could
sound. The link-lost and critical-error alarms were completely silent whenever
the fault arrived while ARMED, PRE_FIRE or FIRING, which is precisely when the
operator most needs to hear one.

Battery readings take the **median** of a 33-sample burst rather than a mean,
because a sample clipped at the ADC's full scale can only bias a mean *upward* —
making a flat pack read as healthy, which is the one direction a battery guard
must never fail in. This is not hypothetical: on a noisy bench supply a burst
with 9 of 33 samples clipped read about 2 V high as a mean, and exactly right as
a median.

**Link** — ESP-NOW AES-128-CCM encryption, an application-layer CRC32-C
integrity check over header + payload + a compile-time key, and replay
protection via monotonic sequence numbers plus a random per-session token.
Both units must run byte-identical firmware versions (MAJOR.MINOR.PATCH) or the
link is refused.

## Repository layout

| Path | Contents |
|---|---|
| `main/` | Entry point; dispatches to base or remote `app_main` by Kconfig |
| `components/rlc_common/` | ESP-NOW wrapper, link manager, message/CRC layer, battery ADC, debounce, continuity classifier, base arm-state derivation, RGB LED, watchdog, self-tests |
| `components/rlc_base/` | Base FSM, relays, continuity, key/arm sense, siren |
| `components/rlc_remote/` | Remote FSM, encoder, fire button, arm switch, buzzer, ILI9488 display |
| `rlc-hw-test-base/`, `rlc-hw-test-remote/` | Standalone hardware bring-up firmware with a serial CLI |
| `tests/host/` | Host-compiled unit tests — `./tests/host/run.sh`, no hardware needed |
| `tools/` | Bench utilities — GPIO blink, LED finder, WS2812 strip diagnostic, battery-divider calibration, **`armgate-test`** (proves the §5.4.4 arm-relay AND gate at the ARM SENSE node), **`gen-secrets.sh`** (generates the gitignored crypto keys), **`git-hooks/`** (pre-commit key-leak guard), test scripts |
| `archive/` | Superseded specification revisions |

Both units build from **one codebase**; the unit is selected by sdkconfig
(`sdkconfig.base` / `sdkconfig.remote`), and the build scripts verify the
expected `app_main` symbol landed in the binary.

## Building and flashing

Requires **ESP-IDF v5.4.1** and an ESP32-S3 (16 MB flash, 8 MB OCT PSRAM).

```bash
./build_base.sh                 # build base unit
./build_base.sh flash           # build + flash base to its default by-id port
./build_remote.sh flash         # build + flash remote
./build_remote.sh flash -p PORT # override the serial port

./build_base.sh flash --inject  # TEST ONLY — see below
```

**After flashing both units, reset the one you flashed first.** Both units run a
strict version check, and `VERSION_MISMATCH` is latched — `tick_remote()`
returns early in that state ("stuck until power cycle"), so the unit stops
sending `LINK_REQUEST` entirely. Flashing back-to-back always trips it: the
first unit reboots on the new firmware, handshakes with its still-stale peer,
latches the mismatch, and never asks again once the second unit catches up. The
symptom is a remote sitting on the FIRMWARE MISMATCH screen (or, if you flashed
the base first, a base that silently never links) with `attempts` frozen and
`contact` climbing. Changing the order does not help — it just moves the latch
to the other unit. A reset on the first-flashed unit clears it; a DTR/RTS pulse
over its serial adapter is enough, no physical access needed.

**`--inject`** builds the base with `CONFIG_RLC_FAULT_INJECTION`, a UART0
console used to run FSD tests T-A11 and T-A13. Neither is reachable otherwise:
link loss trips at 1.5 s, well before the staleness timeout, so interfering with
the radio can never produce "linked but stale"; and nothing in normal operation
emits a malformed ACK, and the remote's own ERROR guard normally stops an ARM
ever reaching a base in ERROR. Console keys: `s` withholds STATUS_UPDATE while
heartbeats keep flowing (T-A11), `a` corrupts the channel of one ARM ACK
(T-A13), `e` forces the base into ERROR while STATUS_UPDATE keeps reporting
IDLE so an ARM still reaches it (T-A19), `w` reports a welded arm relay,
`g` forces a degraded link (T-S15/T-S16), `x` hangs the FSM task to trip the
watchdog (T-S07), `c` corrupts the next outgoing command (T-S05), `?` prints
state.

**This firmware deliberately lies to the remote and is not safe for live use.**
It announces itself five ways — a compile `#warning`, a boot banner, a
flash-time warning, a build failure if the option did not reach the built
config, and (remote only, since 1.2.1) a red **FAULT INJECTION BUILD — NOT SAFE
FOR LIVE USE** banner on the boot splash, which is the only one of the five an
operator at a firing point can actually see. A fault-injection *base* has no
such tell: the remote knows only its own build, and the base does not advertise
its own over the link — and `sdkconfig.base` is never modified, so the option cannot leak into
a later normal build. Reflash with plain `./build_base.sh flash` afterwards.

`./build_remote.sh --inject` is the remote-side counterpart
(`CONFIG_RLC_REMOTE_FAULT_INJECTION`), with the same safeguards. Keys: `d`
posts a display fault, `b` a critical battery, `l` forces a LINK_REQUEST
while linked (T-S09). Both latch the remote's terminal
ERROR, which is the only way to reach the `REMOTE FAULT` status-band state —
it is latched by four conditions, none of them producible from the base or from
the air, and reaching them for real means pulling the display flex or
flattening the pack.

```

./tests/host/run.sh             # host-compiled unit tests, no hardware needed
```

The host tests compile the **real firmware sources** against mock ESP-IDF
headers and assert their behaviour directly — currently the **base safety state
machine**, the LED strip renderer, battery ADC sampling, error-flag naming,
protocol sequence rules, the base arm-state derivation, the rotary encoder's
quadrature decoder, and the shift-register debounce engine. They run once per
unit, because the two units are not configured identically; a test whose
hardware exists on only one unit declares itself SKIPPED on the other.
Currently 16 binaries, 418 checks.

`build_base.sh` and `build_remote.sh` run the suite before every firmware build
and refuse to build if it fails (`RLC_SKIP_HOST_TESTS=1` bypasses).

`tests/host/test_base_fsm.c` is the notable one: it compiles `rlc_base_fsm.c`
against recording fakes for the relays, siren, fire timer, sense inputs and
link, then injects `rlc_fsm_event_t` sequences and asserts what the FSM
actually did — which relay moved, which siren pattern sounded, which NACK
reason went out. That is how the arming guards, the dead-man, the
continuity-loss disarm and the bug #31 two-fire-cycle regression are covered
without a launch pad.

Including the production source rather than mirroring it is the point: a
duplicated copy of the continuity classifier passed its own boot self-test for
three review rounds while the real one drifted. Where a module is pure enough
to host-compile, the test includes the `.c` file.

Serial ports are referenced by stable `/dev/serial/by-id/` paths rather than
`/dev/ttyACMx`, which reorders between plug-ins. Current port and MAC
assignments are recorded in `Development_Progress.md`.

## Firmware architecture

Each unit runs a set of fixed-priority FreeRTOS tasks (full table in
`Development_Progress.md`, specified in FSD §9.10). Safety-critical inputs run
at the highest priorities: the base's arm-switch monitor at 7, the remote's fire
button at 7, link management at 6, the state machines at 4, and cosmetic output
(LEDs, siren, buzzer, display) at 1–2 on the second core.

The state machines are single-task-owner: all state is mutated on one task and
read elsewhere through getters, so there are no shared-state locks in the
safety path.

The remote's 480×320 ILI9488 display renders into a PSRAM framebuffer and
flushes over SPI at 10 Hz, sending only the pixels that actually changed: the
dirty bounding box is a coarse pre-filter, and the flush then diffs it row by
row against a shadow copy of what the panel was last sent. On a steady screen
that is around 1200 pixels a frame out of 153600. Continuity is shown with
distinct shapes as well as colours, so the grid stays readable regardless of
colour vision. No text is drawn smaller than 12×16 px per character — anything
smaller proved unreadable at arm's length in the field.

Across the bottom of **every** screen runs a **system status band**, a coloured
field reporting the state of the fire path so it is legible from across a
launch site without reading anything:

| Band | Meaning |
|---|---|
| Green `SAFE` | Base safe and remote arm switch off — positively confirmed |
| Yellow `BASE KEY ARMED` / `REMOTE ARMED` | One key turned; the arming sequence cannot proceed |
| Orange `READY TO ARM` | Both keys turned — one long-press from a live relay |
| Red `ARM RELAY LIVE` | Arm relay engaged, VBAT on the fire path |
| Flashing red/amber `RELAY WELDED` | Contacts closed when they should not be |
| Red `BASE FAULT` / `REMOTE FAULT` | A unit has faulted and cannot be trusted |
| Grey `STATUS UNKNOWN` | Not known — link down, stale, or before the first report |

Grey rather than green whenever the state is unknown: green is a positive claim
that the pad is safe to approach, so it must never appear on a dead link. One
key and two keys are separated because that is the only step in the sequence
where the risk actually changes. The band names its state in words as well as
colour, for the same reason the continuity grid pairs colour with shapes — and
because two hues that looked well separated in the source proved
indistinguishable on the actual panel.

It occupies the area that already held the status and instruction lines, so the
channel grid is untouched: that grid fills the panel width exactly, and a border
would have had to shrink the cells.

Both units carry an 8-pixel NeoPixel strip, one pixel per igniter channel,
showing the same continuity colours as the remote's grid: all three resolve
them from the `RLC_COLOR_CONT_*` constants in `rlc_config.h`, written as HTML
`0xRRGGBB` values, so restyling everything is a one-line change. The two strips
are wired data-in at opposite ends, so the channel-to-pixel mapping is a
per-unit setting (`RLC_STRIP_REVERSED`).

The strip is an igniter display first: system status *modulates* the channel
map rather than replacing it. Alarms — link lost, battery low, arm-sense fault
— appear as a brief full-strip colour wink every few seconds, so the map stays
readable while the warning stays unmissable, and concurrent alarms alternate
colours. On the remote, whose map arrives over the air rather than from local
sensing, the whole map dims when the cached data goes stale: last known, not
live. The armed and firing patterns are deliberately left as whole-strip red,
so the "pad is live" signal is never diluted into a data display.

## Status

| Phase | Name | Status |
|---|---|---|
| 0 | Hardware validation | Complete |
| 1 | Foundation and communication | Complete |
| 2 | Input/output and debouncing | Complete |
| 3 | State machines and command processing | Complete — G2 arming suite 18/18, G3 fire tests all pass or discharged |
| 4 | Display | Verified on target 2026-08-27 — 9/9 pass; status band added and its 7 states verified |
| 5 | Hardening and final testing | **Complete — release fw 1.2.0, 2026-08-28; currently fw 1.2.1.** §15.4 safety tests 14/19 (incl. T-S06 partial); bug #20 closed. Only T-S10 and T-S18 genuinely open, both blocked on physical access: a soldered display and a soldered key-sense wire. Phase 5 code review closed out 2026-08-28 in fw 1.1.30 (1 Critical, 6 Major, 13 Minor) and **verified on target the same day — 11 tests, 11 PASS** (`Test_Report_Phase5_Review_Fixes.md`), taking firmware to 1.1.32. **MAJ-01 and CRIT-01 closed on target later the same day** (fw 1.1.35, `Test_Report_Phase5_OnTarget_20260828.md`) — two more live defects found and fixed on the way (raw-NACK toast, battery-critical disarm); bug #29 regression suite T-A16/T-A17/T-A18 all PASS, **cleared for live fire**. Final-build audit clean (zero injection/harness symbols in both stock ELFs); both units on stock 1.2.0. **fw 1.2.1 (2026-08-30)** adds the remote's fault-injection splash banner (display-only, no protocol change); **fw 1.2.2 (2026-09-01)** removes the main screen's continuity legend and gives its space to the status band; **fw 1.2.3 (2026-09-01)** adds the base's one-chirp boot-complete siren test (`SIREN_BOOT_TEST`) — both units flashed together, link verified |

Known open items before any field use:

- ~~The display refreshes at 3.3 Hz, not the ≥5 Hz FSD §10.3 requires~~ **Fixed
  2026-08-27, now 10.0 Hz** (T-D09). The panel and the SPI clock were never the
  problem. There was one dirty bounding box, and the main screen updates the top
  bar, the mid-screen grid and the bottom instruction line, so its union was the
  whole 480×320 panel every frame — but the deeper cause was that `draw_field()`
  repaints every field every frame whether or not its text changed, so the
  pixels really were all being rewritten. Compounding it, the frame loop delayed
  100 ms *after* the work, making the period `work + 100 ms`, which could never
  have met §10.3's 100 ms countdown even with an instant flush. The flush now
  diffs against a shadow copy of what the panel was last sent and transmits only
  changed spans (~1200 px per frame against 153600), and the loop is paced with
  `xTaskDelayUntil`. Diffing rather than per-field invalidation is deliberate: a
  missed invalidation leaves a stale pixel, and this screen shows ARMED. See
  `Test_Report_Phase4_Display.md` §6.
- The continuity sense reports three bands — **CONNECTED**, **MARGINAL**,
  **OPEN** — not four. A `SHORT` band was specified but proved unmeasurable at
  the 1 mA test current: a dead short and a 1.5 Ω igniter differ by about a
  millivolt, which is the same size as the noise. The band is named CONNECTED
  rather than GOOD because it means only that current can flow, not that the
  igniter is sound.
- The bug #18 firmware gate `FIRE_PROTECTED_CHANNEL_MASK` was widened from
  channel 1 only to **all eight channels** on 2026-08-23, once the protection BOM
  was complete everywhere: RC snubbers on all eight channel relays and the arm
  relay, 2× 1N5819 clamps on every continuity sense pin, and a 217 Ω
  sense-branch resistor per channel. **All eight channels were fired on
  2026-08-27** into a 12 V 50 W halogen moved channel to channel — nine pulses
  on one power cycle, no reboots or faults. Because only the channel carrying
  the lamp reads CONNECTED, the lamp lighting on the selected channel also
  proves the channel-to-relay mapping end to end for all eight. Igniters have
  been fired on this rig previously (T-A17, and the T-S19 burn-through check);
  the halogen run was about the relay path, not the pyrotechnics.
- **Fire testing is unblocked as of 2026-08-26.** Bug #28 (the base ARM RELAY
  LED lighting with the key in SAFE) turned out to be indicator wiring, not the
  hardware AND gate, and is fixed; a second indicator fault — the key-position
  red and green LEDs lighting simultaneously in SAFE — was fixed at the same
  time. All three arm LEDs now report correctly. The AND gate was then verified
  **electrically at the ARM SENSE node** with `tools/armgate-test` — all seven
  steps pass, every sampling window 0/200 or 200/200 with no mixed samples, so
  the marginal sneak path that bug #28 raised is ruled out rather than assumed
  gone.
- **The base siren is now driven** (bug #27): IRLZ44N on GPIO 40 with its gate
  resistor, pull-down and a 1N5819 flyback diode. The pad has an audible warning
  for the first time. It sounds **continuously** from ARMED through PRE_FIRE and
  FIRING as of firmware 1.1.2 — the old 500 ms ARMED pulse fought the siren's own
  internal modulation and came out quieter than a steady tone. The siren bench
  retests are **done — all six pass**, closing review finding N2 by measurement.
  The siren measures under 200 mA steady, so the 1 A diode has a 5x margin.
- **The arming path is verified on target as of 2026-08-26.** The FSD §15.2
  suite ran **18 PASS / 0 FAIL**, with two tests found unrunnable as written
  (T-A05 contradicts T-A08; T-A15 tests a continuity band merged away in
  August). Four — T-A11, T-A13, T-A19 and T-A20 — needed the `--inject`
  fault-injection build described above. Full write-up in
  `Test_Report_Phase3_G2.md`.
- **G3 fire testing is complete.** T-F01, T-F02, T-F03 and T-F08 all PASS;
  T-F04 and T-F05 covered by earlier evidence; T-F06, T-F07 and T-F09 are
  unreachable as written and discharged by the host FSM harness. **None of them
  needed live ignition** — a 12 V 50 W halogen exercises the fire path
  identically and, unlike an igniter, survives to be fired repeatedly. T-F01's
  criteria turned out to be sequence mechanics (siren continuity, relay dwell,
  auto-disarm), not pyrotechnics. Three
  tests are **not reachable as written**: T-F06 because the 1000 ms pulse
  completes before link loss can be detected at 1500 ms (which also makes
  `COMPLETE_PULSE_ON_LINK_LOSS` unreachable config), and T-F07/T-F09 because
  they need faults only a remote-side injection harness could produce.
- **No silent refusals** (FSD §7.2.9a, firmware 1.1.6). Every refusal, abort or
  failure an operator can trigger produces both a sound and a message on the
  remote display — a log line is not operator feedback. A refusal that only
  beeps is indistinguishable from an input that never registered, and the
  natural response to apparent non-response is to try again, which is the wrong
  instinct at a pad. The base also **answers** commands while in terminal ERROR
  (NACK `0x0E`) rather than discarding them, so the remote can name the fault.
- ~~Bug #30 — continuity-loss disarm has no level-triggered backstop~~ **Fixed
  2026-08-26 (fw 1.1.8).** Found by code review, not by testing. The disarm was
  edge-triggered only, so an igniter going open inside the 200 ms arm-verify
  window had its event dropped with no edge left to report it. Now backed by a
  re-check at arm-verify completion *and* a periodic level check every ~50 ms.
  Positively verified since 2026-08-27 by `tests/host/test_base_fsm.c` T-FSM03,
  which drives both the event path and the level backstop.
- ~~Bug #31 — the fire timer was never stopped after a completed pulse~~ **Fixed
  2026-08-27 (fw 1.1.9) — proven on the host, not yet on the pad.** Filed by the
  full-codebase review as Critical finding **BF-01**; the same defect.
  The most serious defect found so far, and it had
  never been hit because no test had ever completed a fire pulse and re-armed on
  the same power cycle. An expired one-shot GPTimer alarm disables the alarm but
  leaves the driver running, so the *second* launch of a power cycle hit
  `ESP_ERROR_CHECK` on `gptimer_start()` and panicked — **with the arm relay and
  the channel relay still energised**, so the igniter carried full current for
  the whole panic-and-reboot interval. Fixed three ways (stop on completion,
  stop-first on every start, and a checked return that latches ERROR instead of
  aborting) and regression-tested by an automated two-cycle test that runs on
  every build (`tests/host/test_base_fsm.c` T-FSM05).
  **Verified on target 2026-08-27 and the power-cycle-between-launches
  restriction is lifted.** Two full arm→fire→complete cycles on one power cycle
  into a 12 V 50 W halogen: 0 reboots, uptime continuous across both, and the
  two cycles timing-identical — which is the substance of it, since behaving
  the second time exactly as the first is what the bug prevented.
  See `Code_Review_AllPhases_20260827_0308.md`.
- **Neither battery has a hardware undervoltage cut-off** (bug #25), and none was
  ever specified. Protection is firmware-only, and the ERROR state halts
  operation without disconnecting the load — so a unit left switched on, or one
  whose firmware has halted, will discharge a LiPo into the permanently-damaged
  and then unsafe-to-recharge region.
- The remote's VBAT sense pin has **no overvoltage clamp** (bug #22). The 3.3 V
  zener that caused bug #21 was removed to restore linearity and not yet
  replaced; a BAT54-class low-leakage Schottky to the 3.3 V rail is required.
  Until then the divider's series impedance is the only protection on GPIO 1.
- Both units' battery dividers leave **almost no ADC headroom** at full charge
  — the remote sits at 97 % of the ADC's usable ceiling, the base at 92 %
  (bug #23). Accuracy only, roughly 0.7 % at full charge; the arming thresholds
  sit lower in the range and are unaffected.
- The base does not yet act on the remote battery voltage it receives in PING
  (FSD §7 requires NACK `0x0C`).
- The continuity palette (`RLC_COLOR_CONT_*`) deviates from FSD §10.2.0, which
  specifies blue for GOOD to avoid a red-green pair; the specification needs
  updating to match, or the palette reverting.
- ~~The ESP-NOW encryption keys and the integrity-check key are committed to
  this public repository (bug #20)~~ **Rotated and removed from the repo
  2026-08-27 (fw 1.1.20).** They were literal ASCII placeholders
  (`RLC_PMK_DEFAULT!`) that had never been changed — guessable without even
  reading the source. Keys now live in `components/rlc_common/include/rlc_secrets.h`,
  which is gitignored and generated locally by `./tools/gen-secrets.sh`.
  `rlc_config.h` has **no fallback**: a build without real keys fails with an
  instructive `#error` rather than quietly linking a default, since a silent
  fallback is how the placeholders survived to ship. A tracked pre-commit hook
  (`tools/git-hooks/pre-commit`, enabled with
  `git config core.hooksPath tools/git-hooks`) refuses any commit that stages
  the secrets file under any path, or that defines a key macro with non-zero
  bytes anywhere — verified by attempting both leaks.
  **The old keys remain permanently public.** They are in git history across
  many commits on a public repo, very likely already cloned and cached;
  rewriting history would not reliably retract them. Rotation does not
  un-publish them, it makes them irrelevant. Never reuse those values.
- ~~The base's NeoPixel strip has a dead pixel (bug #19)~~ **Fixed 2026-08-26**
  by replacing the strip; all eight pixels respond. The real fault was the
  **third** LED's output stage: it rendered its own colour correctly but stopped
  forwarding data, which is why the break looked like it was at pixel 4. A pixel
  that lights is not evidence that it is passing data — probe DOUT.
  `tools/strip-diag/` is a standalone bring-up firmware for diagnosing this
  class of fault.

## Documentation

| Document | Contents |
|---|---|
| `RLC_Functional_Specification_v1_14.md` | The specification of record (currently at v1.54 internally — the filename lags) — hardware, protocol, state machines, display, test requirements |
| `Development_Progress.md` | Per-phase task and test tracking, hardware reference, bug history |
| `RLC_Project_Summary.md` | Plain-language overview written for club members |
| `docs/RLC_Operations_Manual.html` | **RLC-OPS-001** — operations manual: setup, controls, the safety case in full (interlocks, fail-safe matrix, residual risks), and the safe-ignition procedures for launches and static motor tests. A4 print stylesheet; open in a browser and print to PDF |
| `docs/RLC_Field_Reference_Card.html` | **RLC-OPS-002** — single-page A4 field card: firing sequence, abort actions, misfire drill, status band and limits |
| `docs/reference/` | Source drawings and photos the manual figures are derived from — the remote front panel SVG and the as-built base front plate photo (`RLC_base_front.jpg`). Re-derive figures from these rather than redrawing by eye |
| `Test_Report_Phase4_Display.md` | Phase 4 on-target display tests T-D01…T-D09 — 9 PASS / 0 FAIL (T-D09 failed at 3.3 Hz, fixed same day to 10.0 Hz, §6), plus a §10.2 coverage gap: four specified screens have never been rendered |
| `changelog.md` | Session-by-session development log |
| `Phase{1,2,3}_Code_Review*.md` | Code reviews against the specification |
| `Code_Review_AllPhases_20260821_1430.md` | Full-codebase review: 7 Major findings, 4 gating live-fire, plus a documentation-consistency audit. All seven fixed in 28293b6. |
| `Code_Review_AllPhases_20260821_1523.md` | Post-fix re-review: all 7 prior Majors verified fixed; 2 new Majors found (arm key at boot, siren stale-callback race) and 13 minors. Fixed in firmware 1.1.1. |
| `Code_Review_AllPhases_20260827_0308.md` | Full-codebase review vs FSD v1.42: verdict FAIL — 1 Critical (BF-01 fire timer not stopped after pulse → second launch per power cycle panics with relay energized), 8 Major, 44 Minor. **All findings fixed 2026-08-27 in firmware 1.1.9 / FSD v1.44**, including a host FSM harness that regression-tests the Critical one. |
| `Code_Review_Phase5_20260828_0641.md` | Phase 5 review vs FSD v1.46: verdict MAYBE — 1 Critical, 6 Major, 12 Minor, all in the *operator-information* layer (nothing energized a relay or extended a pulse; the remote could be silent or wrong about a live pad). **All Critical/Major and ten Minors fixed 2026-08-28 in firmware 1.1.30 / FSD v1.47**; the three findings needing an operator decision were settled the same day. |

## Hardware

ESP32-S3-DevKitC-1 (ESP32-S3-WROOM-1 N16R8) on both units. Base: 3S 5000 mAh
LiPo, 8 relay channels, key switch, arm relay with feedback sense, siren, 8-pixel
status strip. Remote: 2S 2200 mAh LiPo, rotary encoder, arm switch, fire button,
buzzer, ILI9488 480×320 SPI display, 8-pixel status strip. Pin assignments are in
`components/rlc_common/include/pin_config.h` and FSD §5.

The base's front plate as built (see the manual §3.2, Figure 3): SMA antenna
bulkhead far left, USB service socket top left, battery on/off toggle top right,
brass-barrel arm key on a red plate with three engraved passive lamps —
**SAFE / ARM / HOT** (HOT = arm relay live, the pad-is-live lamp that works even
with the ESP32 crashed) — and eight red channel modules along the bottom, **CH1
leftmost**, each with an igniter continuity lens, a fire-relay lens and a yellow
XT60 igniter socket. Siren, battery and status strip live inside the case.

## License

MIT — see [LICENSE](LICENSE).
