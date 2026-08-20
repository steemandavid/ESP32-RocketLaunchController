# ESP32 Rocket Launch Controller — Changelog

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
