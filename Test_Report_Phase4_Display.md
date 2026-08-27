# Test Report — Phase 4 Display (T-D01…T-D09)

**Date:** 2026-08-27
**Tester:** Code Test Agent (automated + log capture) + David Steeman (visual observation)
**FSD Reference:** `RLC_Functional_Specification_v1_14.md` (internally v1.44), §10
**Commit Tested:** `c3b5745` plus the T-D09 profiling instrumentation added during this run
**Firmware:** 1.1.9 both units, flashed from this tree at the start of the session
**Scope:** Phase 4 on-target display tests, remote unit ILI9488 480×320

---

> **Update, same day — Finding 1 fixed and retested.** T-D09 now **PASSES** at
> 10.0 Hz and the T-D06 deviation is closed: the pre-fire countdown steps at
> ~100 ms as FSD §10.3 requires. Final tally **9 PASS / 0 FAIL**. The original
> failing run is preserved below as recorded; see §6 for the fix and the
> retest measurements.

## Executive Summary

All nine Phase 4 display tests were executed on target. **Eight pass, one fails.**
Every screen the FSD specifies was rendered on the real panel and behaved as
written: the splash holds its full 10 s, the continuity grid tracks the base,
the encoder cursor moves, the ARMED border pulses, the NACK overlay names its
reason in words and retires cleanly, and the link-lost screen's two counters
advance rather than freezing.

The failure is **T-D09**: the main screen renders at **3.3 Hz**, below the
FSD §10.3 floor of ≥5 Hz. The cause is not the panel or the SPI clock but the
dirty-rectangle scheme — a single bounding box over updates scattered from the
top bar to the bottom instruction line degenerates to the whole screen on every
frame, so the "partial" refresh costs the same 230 ms as a full redraw. The
same root cause makes the pre-fire countdown step in ~300 ms chunks rather than
the 100 ms §10.3 requires; the operator judged that acceptable to read, and
T-D06 is recorded as a pass on that judgement with the deviation noted here.

Neither issue touches the fire path. The display task runs at priority 2 on
core 1; the fire button task runs at priority 7 and the FSM at 4, so a slow
frame delays what the operator *sees*, never the dead-man release. That was
confirmed in the T-D06 runs, where both aborts were logged within the same
frame period as the button release.

---

## 1. Automated Test Results

Host unit suite, run before every firmware build in this session:

| Suite | Result |
|---|---|
| `tests/host/run.sh` — 16 binaries | **418 checks, 0 failures** |

Relevant to this scope: `tests/host/test_armstate.c` covers **T-M01…T-M07**, the
derivation behind the main screen's `BASE`/`REMOTE` status line — including
T-M02, the welded-relay-with-key-OFF case, and T-M06, the 40-character
scale-2 line budget. The logic behind the status line was therefore already
verified before any of the tests below; T-D01…T-D09 test the rendering.

### 1.1 Instrumentation added for T-D09

T-D09 asks for two numbers that cannot be obtained by looking at the panel: a
full-redraw duration and a steady-state frame period. Neither was instrumented.
Added under a new Kconfig option:

- `main/Kconfig.projbuild` — `CONFIG_RLC_DISPLAY_PROFILE`, default `n`,
  `depends on RLC_UNIT_REMOTE`.
- `components/rlc_remote/src/rlc_display.c` — times the render-and-flush
  section of each frame; logs full-redraw cost per screen change and the frame
  period averaged over 20 frames. Passive: changes no drawing and no timing.
- `build_remote.sh` — `--profile` flag, mirroring the existing `--inject`
  pattern: appended to the working `sdkconfig` only, never to
  `sdkconfig.remote`; wipes a build dir configured the other way; and fails the
  build if the option did not reach `sdkconfig.h`.

The firmware under test was built with `--profile`. This is the only difference
from a stock `c3b5745` build.

---

## 2. Test Results

| ID | Test | Result | Evidence |
|----|------|--------|----------|
| T-D01 | Panel ID read-back at boot | **PASS** | `ILI9488 init: 480x320 RGB666 @ 20 MHz, ID 0x2A403300 (healthy)` — exact expected clone ID |
| T-D02 | Splash holds 10 s, then transitions | **PASS** | Splash drawn at 1827 ms, MAIN redraw at 11767 ms → 9.94 s held against `SPLASH_MIN_DURATION_MS` 10000. Content and clean transition confirmed visually |
| T-D03 | Continuity grid matches base STATUS_UPDATE | **PASS** | Visual. Bridged channel read CONNECTED (●), remaining seven OPEN (○) |
| T-D04 | Encoder rotation moves selection cursor | **PASS** | Visual. Cursor tracked rotation one channel per detent; encoder trace `divider=4`, `isr=90 valid=67 step=16` shows the divider rejecting partial quadrature |
| T-D05 | ARMED screen, red pulse, arm-sense confirmed | **PASS** | Visual — border confirmed pulsing, not static. `IDLE -> ARMED (ch 1)`, `full redraw ARMED: 228957 us` |
| T-D06 | Pre-fire countdown smoothness (100 ms steps) | **PASS** (operator judgement) | Two runs, both aborted correctly. Countdown judged legible and acceptable. **Measured 301 ms steps, not 100 ms — see Finding 2** |
| T-D07 | NACK overlay text + 3 s timeout, clean restore | **PASS** | Visual. Refusal named in words; main screen restored with no overlay residue — first on-target exercise of the forced-redraw path at `rlc_display.c:1161` |
| T-D08 | Link-lost screen and recovery | **PASS** | See §2.1 — both counters advance, recovery clean |
| T-D09 | Full-screen redraw time and frame rate | **FAIL** | Full redraw 228–236 ms; steady state **300 ms = 3.3 Hz** vs FSD §10.3 ≥5 Hz. See Finding 1 |

**Totals: 8 PASS / 1 FAIL / 0 SKIP out of 9**

### 2.1 T-D08 evidence

The FSD §10.2.5 requirement is not merely that the screen appears, but that both
dynamic fields come from counters that keep advancing after the link drops. The
capture confirms this directly:

```
930827  state=7  missed=3  contact=5627ms   attempts=2
940827  state=7  missed=3  contact=15627ms  attempts=7
953017  LINK_REQUEST sent (attempt 1)
955017  LINK_ACK accepted, token=0x3a00681c
955017  link state 4 -> 3
955337  T-D09 full redraw MAIN: 235346 us
```

`ms_since_contact` advanced 5627 → 15627 ms across the outage and
`linkreq_attempts` advanced 2 → 7, while `missed_pings` stayed frozen at 3
throughout. `missed_pings` is the field whose update path is gated behind
`s_state == RLC_LINK_STATE_LINKED`, and deriving the display from it was the
2026-08-19 bug that pinned the screen at "1 s ago" and "Attempts 3". That fix is
now positively verified on target rather than by inspection.

Recovery on base power-up was clean: LINK_ACK accepted and a full MAIN redraw
320 ms later, with no intermediate error or mismatch screen.

### 2.2 T-D06 evidence

```
799337  IDLE -> ARMED (ch 1)
800597  ARMED -> PRE_FIRE (ch 1)
801127  full redraw FIRING: 228320 us
803677  Fire button released during PRE_FIRE — abort   → held 3.1 s
803677  -> IDLE

814017  IDLE -> ARMED (ch 1)
815017  ARMED -> PRE_FIRE (ch 1)
815267  full redraw FIRING: 228568 us
817937  Fire button released during PRE_FIRE — abort   → held 2.9 s
817937  -> IDLE
```

No pulse in either run. This independently re-confirms the T-F02 result of
2026-08-26. Frame-period averages spanning the FIRING window: 286.63 ms and
301.11 ms — the firing screen is no faster than any other.

---

## 3. Findings

| # | Severity | Category | Description | FSD Ref | Test |
|---|----------|----------|-------------|---------|------|
| 1 | **MAJOR** | Performance | Dirty-rectangle refresh is inert on the main screen; display runs at 3.3 Hz against a ≥5 Hz requirement | §10.3 | T-D09 |
| 2 | MINOR | Spec deviation | Pre-fire countdown steps in ~301 ms, not the specified 100 ms. Same root cause as Finding 1 | §10.3 | T-D06 |
| 3 | INFO | Test equipment | Remote CH340 USB adapter dropped off the bus three times during the session | — | — |

### Finding 1 — Dirty-rectangle refresh is inert on the main screen (MAJOR)

**Measured.** Full redraw 228–236 ms across every screen (SPLASH, MAIN, ARMED,
FIRING). Steady-state main-screen frame period 299.99 / 300.00 / 300.37 /
301.80 / 304.19 ms across repeated 20-frame windows — 3.2–3.3 Hz.

**Why the number is exactly 300.** `CONFIG_FREERTOS_HZ` is 100, so the tick is
10 ms, and `display_task` calls `vTaskDelay(pdMS_TO_TICKS(DISPLAY_FRAME_MS))`
*after* the work. Period = `ceil(work/10)*10 + 100`. A 300 ms period puts the
per-frame work at 190–200 ms — indistinguishable from the 230 ms full redraw,
which is the diagnosis.

**Cause.** There is one dirty bounding box (`s_dx0/s_dy0/s_dx1/s_dy1`,
`rlc_display.c:264`), and `flush()` (`rlc_display.c:460`) streams its union.
`draw_main_dynamic()` (`rlc_display.c:726`) marks the top bar at y≈0, the
channel grid mid-screen, the status line at y≈245 and the instruction text at
`DH-30` ≈ y 290. The union spans y=0→306 at full width — effectively the whole
480×320 panel, which at 20 MHz is ~184 ms of pure SPI transfer before any
per-row `memcpy` from PSRAM through the bounce buffer.

So Phase 4 development task 12 and the FSD §10.3 claim of partial refresh are
not delivering on the screen that matters most. A scattered-update screen is
precisely the case a single bounding box handles worst.

**Suggested direction (not implemented).** Replace the single box with a small
set of dirty rects — or flush per marked region — so the top bar, grid, status
line and instruction line are transferred independently. The top bar alone is
roughly a tenth of the panel. This is a contained change to `mark_dirty()` and
`flush()`, but it is a design change and was deliberately not made mid-test-run.

**Not a safety issue.** `display_task` is priority 2 on core 1; the fire button
task is priority 7 and the FSM is 4. A slow frame delays what the operator sees,
never the dead-man release — confirmed in §2.2, where both aborts were logged
within one frame period of the button release.

### Finding 2 — Pre-fire countdown steps at 301 ms, not 100 ms (MINOR)

FSD §10.3 states the countdown "shall update every 100 ms (smooth countdown
display)". Measured 301.11 ms on the FIRING screen. The operator judged the
countdown legible and acceptable in use, and T-D06 is recorded as a pass on that
basis; this entry records the measured deviation from the written requirement.
Resolving Finding 1 resolves this automatically. If Finding 1 is not resolved,
§10.3 should be amended to match what the hardware delivers rather than left
stating a figure the firmware misses by 3×.

### Finding 3 — Remote USB serial adapter dropped off the bus (INFO)

The remote's CH340 adapter vanished from the USB bus three times during the
session, taking its `/dev/serial/by-id` symlink with it and re-enumerating
(`ttyACM1`, symlink timestamp 09:24 against the base's 03:03). Two log captures
were lost as a result.

**This is not a firmware fault.** The board itself never reset — uptime ran
continuously through the dropouts, past 777 s and on to 965 s. It is the adapter
or its cable. Recorded because an adapter that disappears mid-run is the kind of
fault that gets misattributed to firmware later, and because it cost two
captures here.

---

## 4. Coverage Analysis

| FSD Requirement | Test(s) | Status |
|---|---|---|
| §10.1 / §9.13 step 6 — panel ID read-back at boot | T-D01 | COVERED |
| §10.2.1 — splash screen content and hold | T-D02 | COVERED |
| §10.2.1 — firmware mismatch screen | — | **UNTESTED** — needs deliberately mismatched builds |
| §10.2.2 — main status screen, continuity grid | T-D03, T-D04 | COVERED |
| §10.2.2 — BASE/REMOTE arm-state line | T-M01…T-M07 (host) | COVERED (logic); `WELD!` never rendered on target |
| §10.2.3 — armed screen, pulsing border | T-D05 | COVERED |
| §10.2.4 — firing screen, countdown | T-D06 | PARTIAL — countdown verified, `IGNITION ACTIVE` never rendered (needs a real pulse) |
| §10.2.4a — fire complete screen | — | **UNTESTED** — needs a completed pulse (T-F01) |
| §10.2.5 — link lost screen, advancing counters | T-D08 | COVERED |
| §10.2.6 — error screen | — | **UNTESTED** — T-S10 / T-S10b |
| §10.2.7 — NACK overlay, 3 s, clean restore | T-D07 | COVERED |
| §10.3 — partial refresh | T-D09 | **FAILED** |
| §10.3 — ≥5 Hz refresh | T-D09 | **FAILED** (3.3 Hz) |
| §10.3 — 100 ms countdown steps | T-D06 | **DEVIATION** (301 ms) |

Four screens specified in §10.2 have still never been rendered on the panel:
the firmware-mismatch screen, the fire-complete screen, the error screen, and
the `IGNITION ACTIVE` state of the firing screen. Three of them require events
outside this test scope (a completed fire pulse, a display fault, mismatched
builds). They should not be assumed working.

---

## 5. Recommendation

Phase 4 moves from "code complete, unverified" to **verified with one open
performance defect**. The screens are correct; the refresh strategy is not.

1. **Fix Finding 1** before Phase 4 is called complete. It is contained, it is
   the difference between meeting and missing a written requirement, and it
   resolves Finding 2 for free.
2. **Re-run T-D09 and T-D06** after that fix, using `--profile`.
3. **Pick up the four unrendered screens** opportunistically: the fire-complete
   and `IGNITION ACTIVE` screens fall out of T-F01 at no extra cost, and the
   error screen falls out of T-S10b. Add them as observations to those tests
   rather than as new ones.
4. **Check the remote's USB cable** (Finding 3) before relying on serial capture
   for any timing-sensitive test.

The one-launch-per-power-cycle restriction from bug #31 is unaffected by this
run and still stands until the on-target two-cycle test (Phase 5 task 10).

---

## 6. Finding 1 Fixed — Retest (same day)

### 6.1 What was actually wrong

The original diagnosis — one dirty bounding box spanning the whole panel — was
correct but incomplete. Investigating the fix showed a second, larger cause:
**`draw_field()` erases and repaints every field on every frame regardless of
whether its text changed** (`rlc_display.c`, `fill_rect` + `draw_text`, no
comparison against previous content). So the bounding box was not merely
pessimistic; the pixels genuinely were all being rewritten. A rect list alone
would still have transmitted almost the whole panel.

A third cause governed the frame rate independently of transfer cost: the loop
called `vTaskDelay(DISPLAY_FRAME_MS)` *after* the frame's work, so the period
was `work + 100 ms`. Even with an instantaneous flush, that could never have
produced the 100 ms period FSD §10.3 requires of the countdown.

### 6.2 The fix

1. **Pixel diffing against a shadow copy.** `flush()` now compares the dirty box
   row by row against a shadow buffer of what the panel was last sent, and
   transmits only the spans that actually differ, coalescing consecutive
   changed rows into runs. A second PSRAM buffer (460800 bytes) was added for
   the shadow; PSRAM is 8 MB and was not under pressure.

   Diffing was chosen over hand-maintained per-field invalidation deliberately.
   A missed invalidation leaves a stale pixel on the panel, and this display
   shows ARMED — a stale ARMED is the one failure it must not have. A pixel
   comparison cannot get that wrong by construction, and it needed no changes
   to any drawing code.

2. **Fixed-period pacing.** `xTaskDelayUntil` replaces `vTaskDelay`, so the
   period is `DISPLAY_FRAME_MS` regardless of frame cost. On overrun — a full
   redraw still costs more than 100 ms — the wake time is re-based rather than
   allowing a burst of catch-up frames with no delay.

3. **Profiling extended** to accumulate over the whole 20-frame window rather
   than point-sampling one frame in twenty. This mattered: point-sampling
   reported `0 px` flushed for 30 s on a screen that was in fact updating, an
   artefact that would have read as either a perfect result or a frozen panel.

### 6.3 Retest measurements

| Metric | Before | After | Requirement |
|---|---|---|---|
| Steady frame period | 300 ms (3.3 Hz) | **100.00 ms (10.0 Hz)** | ≥5 Hz (§10.3) |
| Period during PRE_FIRE | 301 ms | **101 ms** | 100 ms countdown (§10.3) |
| Render + flush, steady | ~195 ms | **33 ms avg, 37 ms max** | — |
| Pixels sent per frame, steady | ~153600 (whole panel) | **~1200 worst frame** | — |
| Full redraw (partial-change transition) | 232 ms | **89–202 ms** | transitions only |
| Full redraw (whole-panel change) | 232 ms | **215–250 ms (153600 px)** | transitions only |

The worst-case figure needed instrumenting separately, because after the fix a
"full redraw" on a screen change only transmits what differs from the *previous*
screen: MAIN costs 202 ms for 116124 px, SPLASH only 105 ms for 36106 px. None
of those is an all-pixels-change case.

Three whole-panel flushes were subsequently measured:

| Case | Time | Pixels |
|---|---|---|
| Boot panel clear (`display_init`) | 215 ms | 153600 in 1 run |
| MAIN → LINK_LOST (amber field) | 236 ms | 153600 in 1 run |
| LINK_LOST → MAIN | **250 ms** | 153600 in 1 run |

**250 ms is the worst case**, not the 215 ms of the boot clear — the boot clear
only fills black and so carries almost no render cost, whereas a real screen
transition includes drawing the new screen. The transfer is bounded by SPI
throughput, not by the diff: 153600 px × 3 bytes at 20 MHz is ~184 ms of wire
time before any rendering.

Steady-state windows read `99.99 / 100.00 ms` repeatedly. Windows containing a
screen transition read 101–106 ms, because a full redraw overruns the 100 ms
budget and the pacing re-bases; this is expected and remains within §10.3.

Full redraws also got cheaper (232 → 89–188 ms) because a screen change now
only transmits what differs from the *previous screen* — the top bar is often
identical between MAIN and ARMED, so it is no longer resent.

Worst observed single-frame render was 115 ms, on the ARMED/FIRING screens whose
pulsing border animates a large region. That exceeds the 100 ms budget on those
frames, absorbed by the re-basing described above.

### 6.4 Verification

- **Visual:** operator confirmed the main screen, encoder cursor, ARMED screen
  and firing countdown all render correctly with no stale pixels, torn text or
  leftover fragments — the specific regression risk of a diffing flush.
- **Countdown:** confirmed stepping at ~100 ms rather than the previous ~300 ms
  lurch. **T-D06's §10.3 deviation is closed.**
- **Host suite:** 418 checks, 0 failures — no regression.
- **Incidental:** the retest also produced `ARM NACK: 0x04 (NO CONTINUITY)` on
  channel 8, exercising the T-D07 overlay path a second time.

### 6.5 Revised result

| ID | Before | After |
|---|---|---|
| T-D06 | PASS with §10.3 deviation | **PASS**, deviation closed |
| T-D09 | **FAIL** (3.3 Hz) | **PASS** (10.0 Hz) |

**Final: 9 PASS / 0 FAIL.** Findings 1 and 2 are closed. Finding 3 (the USB
adapter) is unrelated to firmware and remains open as a bench-equipment note.

### 6.6 Outstanding

- ~~Version number not bumped.~~ **Done — firmware 1.1.10, both units
  reflashed and relinked** (`LINK_ACK accepted`, which a version mismatch would
  have refused). Boot banner and `Version comparison self-test: PASS (v1.1.10)`
  confirm it on the remote.
- ~~T-D08 has not been re-run under the new flush.~~ **Re-run on 1.1.10 —
  PASS.** See §6.7.
- ~~T-D02's visuals have not been re-checked on 1.1.10.~~ **Confirmed visually**
  on 1.1.10, alongside its timing (splash 1737 → 11647 ms = 9.91 s held,
  redrawing 36106 px in 9 runs).
- **The profiling harness was removed in firmware 1.1.11**, and both units now
  run 1.1.11 stock. 1.1.11 renders identically to 1.1.10 — the harness was
  passive and compiled out by default — but **none of the timing figures in
  this section can be reproduced on the shipped binary.** Recover
  `CONFIG_RLC_DISPLAY_PROFILE` / `./build_remote.sh --profile` from git history
  at 1.1.10 if the display refresh needs re-measuring.
- **The remote is running a `--profile` build.** Passive, but not a stock
  build. Reflash without `--profile` when profiling is no longer wanted.
- The whole-panel memcmp costs ~33 ms per frame, a third of the frame budget,
  because the dirty box still spans the panel (everything is still repainted
  into the framebuffer). Caching field text so unchanged fields are not
  redrawn would shrink the box and cut most of that. Not required to meet
  §10.3, and not done.

### 6.7 T-D08 re-run under the new flush — firmware 1.1.10

T-D08 originally passed on the old whole-box flush. Because the flush mechanism
was replaced underneath it, and LINK_LOST was the only §10.2 screen the diffing
code had never rendered, the pass was not treated as carrying over. Re-run:

```
179287  link state 3 -> 4
179587  full redraw LINK_LOST: 235670 us, 153600 px in 1 run(s)
180827  state=7  missed=3  contact=3637ms   attempts=1
185827  state=7  missed=3  contact=8637ms   attempts=4
190827  state=7  missed=3  contact=13637ms  attempts=6
195827  state=7  missed=3  contact=18637ms  attempts=9
200827  state=7  missed=3  contact=23637ms  attempts=11
205827  state=7  missed=3  contact=28637ms  attempts=14
210827  state=7  missed=3  contact=33637ms  attempts=16
219337  LINK_ACK accepted, token=0xeeff21ef → link state 4 -> 3 → IDLE
219637  full redraw MAIN: 250185 us, 153600 px in 1 run(s)
```

**Result: PASS.** `ms_since_contact` advanced in exact 5 s steps across a 40 s
outage (3637 → 33637 ms), `linkreq_attempts` advanced 1 → 16, and
`missed_pings` stayed frozen at 3 throughout — the 2026-08-19 regression
remains fixed. Recovery was clean. Operator confirmed no leftover pixels from
the main screen on the amber field, and none of the amber on return.

`STATUS_UPDATE stale timeout (43149 ms)` is logged at reconnect. That is correct
behaviour — the cached base status genuinely had expired during a 40 s outage —
not a fault.

**Why this run mattered beyond re-confirming T-D08.** Both transitions flushed
**153600 px in a single run**: MAIN → LINK_LOST changes every pixel on the
panel, as does the return. That exercises the diff's all-changed path on a real
screen transition, which nothing else in the test set does — every other
transition shares some content with the screen before it. The diff detected the
complete change, coalesced it into one run rather than fragmenting it, and
dropped no rows. Combined with the operator seeing no residue, that closes the
principal regression risk of replacing the flush.

It also supplied the true worst-case redraw figure of 250 ms (§6.3), which is
higher than the boot clear and would have been missed otherwise.
