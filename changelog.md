# ESP32 Rocket Launch Controller — Changelog

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
