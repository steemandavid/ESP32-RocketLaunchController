# ESP32 Wireless Rocket Launch Controller

A two-unit wireless launch controller for model rocketry, built on the ESP32-S3.
A **base unit** sits at the pad wired to up to 8 igniter channels; a handheld
**remote unit** controls it over an encrypted ESP-NOW link from a safe distance
(~200 m design target).

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
   siren starts pulsing.
7. Operator **presses and holds fire** → 2 s pre-fire countdown, continuous siren.
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

**Link** — ESP-NOW AES-128-CCM encryption, an application-layer CRC32-C
integrity check over header + payload + a compile-time key, and replay
protection via monotonic sequence numbers plus a random per-session token.
Both units must run byte-identical firmware versions (MAJOR.MINOR.PATCH) or the
link is refused.

## Repository layout

| Path | Contents |
|---|---|
| `main/` | Entry point; dispatches to base or remote `app_main` by Kconfig |
| `components/rlc_common/` | ESP-NOW wrapper, link manager, message/CRC layer, battery ADC, debounce, RGB LED, watchdog, self-tests |
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

The host tests compile the real rendering code against mock ESP-IDF headers and
assert its output directly. They run once per unit, because the two units are
not configured identically.

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

- Fire testing is restricted to **channel 1 only** — channels 2–8 lack the
  Schottky clamps and contact snubber added after two base ESP32s were destroyed
  by relay-arc coupling (tracked as bug #18). The restriction is enforced in
  firmware by `FIRE_PROTECTED_CHANNEL_MASK`, not just by procedure.
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
- The base's NeoPixel strip has a **dead pixel at channel 4** (bug #19): the
  fourth LED in the data chain no longer forwards data, so channels 4–8 are
  unusable on that unit. Channels 1–3 and the whole remote strip are fine. The
  fix is to reflow or replace that LED. `tools/strip-diag/` is a standalone
  bring-up firmware for diagnosing this class of fault.

## Documentation

| Document | Contents |
|---|---|
| `RLC_Functional_Specification_v1_14.md` | The specification of record (currently at v1.17 internally) — hardware, protocol, state machines, display, test requirements |
| `Development_Progress.md` | Per-phase task and test tracking, hardware reference, bug history |
| `RLC_Project_Summary.md` | Plain-language overview written for club members |
| `changelog.md` | Session-by-session development log |
| `Phase{1,2,3}_Code_Review*.md` | Code reviews against the specification |

## Hardware

ESP32-S3-DevKitC-1 (ESP32-S3-WROOM-1 N16R8) on both units. Base: 3S 5000 mAh
LiPo, 8 relay channels, key switch, arm relay with feedback sense, siren, 8-pixel
status strip. Remote: 2S 2200 mAh LiPo, rotary encoder, arm switch, fire button,
buzzer, ILI9488 480×320 SPI display, 8-pixel status strip. Pin assignments are in
`components/rlc_common/include/pin_config.h` and FSD §5.

## License

MIT — see [LICENSE](LICENSE).
