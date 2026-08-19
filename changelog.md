# ESP32 Rocket Launch Controller — Changelog

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
