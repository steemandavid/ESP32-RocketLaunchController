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

The operator selects a channel on the remote, completes a deliberate multi-step
arming procedure, then presses and **holds** a fire button. Releasing the button
at any point cuts current to the igniter — it is a dead-man switch, not a latch.

1. Both units power up and link automatically.
2. Operator selects a channel (1–8) with the rotary encoder.
3. Someone at the pad turns the base unit's physical **key switch** to ARM.
4. Operator flips the remote's physical **arm switch**.
5. Operator **long-presses** the encoder (500 ms) to send the arm command.
6. The base checks its guard conditions and energises the arm relay; the pad
   siren sounds continuously and stays on for the rest of the sequence.
7. Operator **presses and holds fire** → 2 s pre-fire countdown.
8. Channel relay closes for a fixed 1 s fire pulse.
9. Releasing the button at any time during 6–8 cuts power immediately.
10. Relays de-energise, the system returns to idle, and continuity is re-checked
    to confirm the igniter burned through.

## Safety design

No single hardware or software fault should be able to cause ignition. The full
argument lives in the functional specification (§7, §9, §13); in summary:

**Hardware** — three independent break points in the fire path (key switch, arm
relay, channel relay); a hardware AND gate so the arm relay needs *both* the key
switch and a firmware-driven MOSFET; fail-safe relay defaults with gate
pulldowns that hold during boot; passive battery-powered status LEDs that work
even with the ESP32 dead; ~1 mA current-limited continuity sensing; active-low
inputs so a broken wire reads as "safe".

**Firmware** — ten guard conditions before the arm relay may close; arm-sense
feedback that detects a welded relay; a dead-man repeat-message scheme during
firing; 10 s auto-disarm; and an unrecoverable ERROR state that requires a
physical power cycle rather than attempting to self-heal.

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
| `tools/` | Bench utilities — GPIO blink, LED finder, WS2812 strip diagnostic, battery-divider calibration, test scripts |
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

./tests/host/run.sh             # host-compiled unit tests, no hardware needed
```

The host tests compile the **real firmware sources** against mock ESP-IDF
headers and assert their behaviour directly — currently the LED strip renderer,
battery ADC sampling, error-flag naming, the base arm-state derivation, the
rotary encoder's quadrature decoder, and the shift-register debounce engine.
They run once per unit, because the two units are not configured identically;
a test whose hardware exists on only one unit compiles to a skip on the other.
Currently 12 binaries, 265 checks.

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
flushes only the dirty bounding box over SPI, at 10 Hz. Continuity is shown with
distinct shapes as well as colours, so the grid stays readable regardless of
colour vision. No text is drawn smaller than 12×16 px per character — anything
smaller proved unreadable at arm's length in the field.

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
| 3 | State machines and command processing | Code complete |
| 4 | Display | Code complete, layouts not yet verified on target |
| 5 | Hardening and final testing | Not started |

Known open items before any field use:

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
  sense-branch resistor per channel. Channels 2–8 have nonetheless **never been
  fired** — treat the first shot on each as a test.
- **Fire testing is unblocked as of 2026-08-26.** Bug #28 (the base ARM RELAY
  LED lighting with the key in SAFE) turned out to be indicator wiring, not the
  hardware AND gate, and is fixed; a second indicator fault — the key-position
  red and green LEDs lighting simultaneously in SAFE — was fixed at the same
  time. All three arm LEDs now report correctly. Re-verify the AND gate at the
  node before the first shot, since the indicator wiring was just reworked.
- **The base siren is now driven** (bug #27): IRLZ44N on GPIO 40 with its gate
  resistor, pull-down and a 1N5819 flyback diode. The pad has an audible warning
  for the first time. It sounds **continuously** from ARMED through PRE_FIRE and
  FIRING as of firmware 1.1.2 — the old 500 ms ARMED pulse fought the siren's own
  internal modulation and came out quieter than a steady tone. The siren bench
  retests that close review finding N2 have **not** been run yet. The siren
  measures under 200 mA steady, so the 1 A diode has a 5x margin.
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
- The ESP-NOW encryption keys and the integrity-check key are committed to this
  public repository (bug #20), so two of the three communication-security layers
  offer no protection against anyone who has read the source. Only the replay
  protection, whose session token is random per link-up, still holds. The keys
  need rotating **and** moving out of tracked files before field use.
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
| `RLC_Functional_Specification_v1_14.md` | The specification of record (currently at v1.33 internally — the filename lags) — hardware, protocol, state machines, display, test requirements |
| `Development_Progress.md` | Per-phase task and test tracking, hardware reference, bug history |
| `RLC_Project_Summary.md` | Plain-language overview written for club members |
| `changelog.md` | Session-by-session development log |
| `Phase{1,2,3}_Code_Review*.md` | Code reviews against the specification |
| `Code_Review_AllPhases_20260821_1430.md` | Full-codebase review: 7 Major findings, 4 gating live-fire, plus a documentation-consistency audit. All seven fixed in 28293b6. |
| `Code_Review_AllPhases_20260821_1523.md` | Post-fix re-review: all 7 prior Majors verified fixed; 2 new Majors found (arm key at boot, siren stale-callback race) and 13 minors. Fixed in firmware 1.1.1. |

## Hardware

ESP32-S3-DevKitC-1 (ESP32-S3-WROOM-1 N16R8) on both units. Base: 3S 5000 mAh
LiPo, 8 relay channels, key switch, arm relay with feedback sense, siren, 8-pixel
status strip. Remote: 2S 2200 mAh LiPo, rotary encoder, arm switch, fire button,
buzzer, ILI9488 480×320 SPI display, 8-pixel status strip. Pin assignments are in
`components/rlc_common/include/pin_config.h` and FSD §5.

## License

MIT — see [LICENSE](LICENSE).
