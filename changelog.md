# ESP32 Rocket Launch Controller — Changelog

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

### Notes

- Base firmware rebuilt clean after the `rlc_link.h` change — the base unit was
  not flashed or otherwise touched.
- A full-screen redraw is ~460 kB over SPI (~180 ms at 20 MHz), which exceeds
  one 100 ms frame. That only happens on screen changes; steady-state frames
  push a few kB. Worth measuring properly under T-D09.
- `task_wdt: esp_task_wdt_reset(): task not found` at boot is pre-existing and
  unrelated to this work.

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
