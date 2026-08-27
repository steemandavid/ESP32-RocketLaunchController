# ESP32 Wireless Rocket Launch Controller — Functional Specification

**Document ID:** RLC-FSPEC-001
**Version:** 1.44
**Date:** 2026-08-27
**Author:** David Steeman & Claude Code / Opus 4.6
**Status:** Draft for Development
**Target Platform:** ESP32-S3 (ESP-IDF framework)
**Board:** ESP32-S3-DevKitC-1 with ESP32-S3-WROOM-1 N16R8 module

---

## Revision History

| Version | Date | Changes |
|---|---|---|
| 1.0 | 2026-03-22 | Initial draft |
| 1.1 | 2026-03-22 | Added development phases, sub-agent instructions, configurable GPIO polarity, shift-register debounce, version numbering, RGB LED status, complete pin assignments, protocol exception handling, state machine exception analysis. Removed base buzzer. Changed continuity to bypass arm switch. Changed siren to pulse in ARMED state. Enforced strict firmware version matching. Single codebase architecture. |
| 1.2 | 2026-03-22 | Applied 27 accepted review findings. Reduced to 8 channels with relay feedback GPIO. Renamed auth_hash→integrity_crc (CRC32 is integrity check, not authentication; ESP-NOW AES-128-CCM is security boundary). Full MAJOR.MINOR.PATCH version matching (3-byte field). Added channel field to CMD_ACK, update_sequence to STATUS_UPDATE, remote_battery_voltage_mv to PING. Removed status_flags from LINK_ACK. Added circuit topology diagram, REMOTE_VBAT_MIN_ARM_MV, brown-out detection, peer registration in BOOT. Fire button debounce→80 ms (8-bit). Link-health guard at PRE_FIRE→FIRING. Continuity current ≤ 1 mA. Repeated CMD_FIRE fire-and-forget. Remote gets local PRE_FIRE state. Wi-Fi channel→11. ADC calibration API mandated. Static_asserts on all structs. Display colour constants. Continuity-loss buzzer pattern. |
| 1.3 | 2026-03-22 | Tightened ERR_RELAY_FAULT description in §13.1 — removed conditional "(if hardware feedback is available)" to match the firm relay feedback requirement established in §5.4.6, §7.2.2, and §9.2. |
| 1.4 | 2026-03-22 | Changed continuity sensing from digital inputs to ADC1 analogue inputs with 4-band classification (SHORT/GOOD/MARGINAL/OPEN). Added per-channel 100 kΩ pull-down resistors for reliable open-circuit detection. 64-sample oversampling at 100 ms per channel. Replaced binary continuity_bitmask in STATUS_UPDATE with 2-bit-per-channel continuity_bands field. Reassigned continuity pins from digital GPIOs to ADC1 GPIO 2–9. Updated circuit topology, display indicators, thresholds, tests, and struct definitions. |
| 1.5 | 2026-03-22 | External embedded software analyst review conducted. Findings documented in RLC_FSD_Review_v1_5. |
| 1.6 | 2026-03-22 | Applied all accepted findings from v1.5 review (C1–C5, H1–H8, M1–M10, L1–L9). Key changes: reordered struct fields for natural alignment; added periodic relay feedback monitoring; blocked LINK_REQUEST session reset while ARMED/PRE_FIRE/FIRING; made fire-pulse-on-link-loss configurable; added MOSFET switch and fusible resistor note for continuity circuit; added PRE_FIRE to remote state enum; specified CRC32 polynomial and sequence overflow semantics; added arm switch guard to PRE_FIRE transitions; specified wrong-channel CMD_FIRE handling in PRE_FIRE/FIRING; added NACK for low remote battery; added Wi-Fi channel survey note; added display health check; reduced encoder lockout; added power supply section; fixed duplicate section numbering; added ESP-NOW send failure handling; added task priority table; specified update_sequence wrap-around; raised REMOTE_VBAT_CRITICAL_MV to 3.2V; added initial channel selection; added logging specification; extended CRC to cover header; noted colour-blind accessibility; swapped GPIO 3 for GPIO 10. |
| 1.7 | 2026-03-22 | Applied 27 accepted findings from v1.6 review. Key changes: allocated GPIO 41 for shared continuity enable MOSFET; disabled continuity sensing in ARMED/PRE_FIRE/FIRING/ERROR states; specified ISR-safe signalling for fire pulse timer; added general ISR safety rule; dead-man timestamp captured in ESP-NOW receive callback; 50 ms relay dropout delay after FIRING; removed periodic relay feedback monitoring (check at arm-time only); added SIREN_ERROR and SIREN_CONTINUITY_LOST patterns; clarified channel relay stays open in ARMED; simplified LINK_REQUEST handling in POST_FIRE; 500 ms long-press to arm; ESP-NOW receive queue; boot sequence specification; removed boot-time Wi-Fi channel scan; encoder button 8-bit notation fix; TWDT per-task watchdog. |
| 1.8 | 2026-03-23 | Applied accepted findings from v1.7 review (C1–C5, H1–H7, M1–M10, L1–L9). Key changes: removed stale continuity guards from ARMED→PRE_FIRE and continuity-loss disarm from ARMED/PRE_FIRE (sensing disabled — stale data); fire pulse timer ISR now only signals task via xTaskNotifyFromISR(), all relay control in task context; added ARM_TIMEOUT_MS (10s) auto-disarm; explicit independent GND for continuity circuit; HEARTBEAT_INTERVAL_MS reduced to 500 ms (link loss detection now 1.5s); PONG freshness at PRE_FIRE→FIRING uses HEARTBEAT_INTERVAL_MS + HEARTBEAT_TIMEOUT_MS; ERR_COMM_DEGRADED blocks IDLE→ARMED and PRE_FIRE→FIRING; remote treats multiple bits in channel_armed_bitmask as error; siren silenced immediately on link recovery; ESP-NOW receive queue depth raised to 16; long-press timer starts from debounced transition; continuity thresholds in microvolts; LINK_ACK header token clarification; CMD_DISARM in IDLE ACK'd as idempotent; Fire Complete screen layout added; strict version matching rationale note; compile-time keys noted as known limitation; test renumbering; backlight always 100%; SPI2_HOST naming fix. |
| 1.10 | 2026-03-23 | Igniter circuit redesign: removed low-side relay entirely. Each channel relay changed to 12V automotive SPDT relay (20A contacts), driven via 10× IRLZ44N logic-level N-channel MOSFETs in low-side switch configuration. SPDT relay NC contact routes to continuity sense circuit; NO contact routes to fire path (VBAT+ via arm switch). Igniter low-side connected directly to ground. Fire path now has two independent break points (arm switch + channel relay). Removed P-channel MOSFET continuity enable switch (GPIO 41) — SPDT relay NC/NO switching provides inherent isolation between continuity sensing and fire path; continuity is always active when relay is de-energised. Replaced single 3.3 kΩ R_ref with two series fusible resistors (1.5 kΩ + 1.8 kΩ = 3.3 kΩ) for defence-in-depth against single-resistor short failure. Arm switch is now hardware-only SPST in fire path high-side, sensed exclusively via arm switch sense circuit (10 kΩ + 3.3V zener + 100 kΩ on GPIO 21); removed dedicated digital arm switch GPIO 39. Added arm switch sense as arming guard. Post-fire igniter status detected via continuity sense circuit after relay returns to NC. Removed all low-side relay references from state machine, relay control functions, STATUS_UPDATE protocol, safety requirements, boot sequence, pin assignments, and tests. Replaced `relay_lowside_set()` with `relay_fire_set()` for SPDT relay control. Updated STATUS_UPDATE struct: replaced `low_side_relay` field with `arm_switch_hw`. Added IRLZ44N MOSFET driver specification (10× MOSFETs, 8 relay coils + 1 siren + 1 arm relay, active-HIGH GPIO logic, gate series resistors + pull-downs + external flyback diodes). Specified relay type (12V automotive SPDT, 20A). Added code review agent to §4.4. Freed GPIO 38, 39, 41, 42 (4 spare). Added tests T-S17, T-S18, T-S19. |
| 1.11 | 2026-03-30 | Added §4.6 Code Reusability and §4.7 RTOS Architecture Requirements. Code must be written with reusability in mind (generic libraries, abstract interfaces, no project-specific coupling in shared components). Formalised RTOS best practices: all inter-task communication via FreeRTOS primitives (queues, semaphores, task notifications), mutex-protected shared state, ISR-to-task signalling only, priority inversion prevention, and race-condition avoidance rules. |
| 1.12 | 2026-04-13 | Documentation consistency audit. Fixed GPIO 47→48 for RGB LED in §3, §4.1, §11.3. Fixed v1.10 revision note: ULN2003A→IRLZ44N throughout, removed GPIO 48 from freed list. Added §5.4.9 Arm Relay Output section. Updated §5.4.10 (was §5.4.9) MOSFET quantity 9→10 (includes arm relay). Updated §5.4.11 (was §5.4.10) RGB LED to describe 8-pixel strip. Fixed §2.1 architecture diagram: arm switch sense label. Fixed Appendix C.1 pin count. Updated all cross-references for section renumbering. |
| 1.13 | 2026-04-14 | Arm relay redesign: physical arm key switch removed from fire path (cannot handle igniter current). Arm relay (SPDT, driven via IRLZ44N MOSFET in series with physical key switch) now provides primary fire path interlock — forming a hardware AND gate (key switch ON AND software MOSFET drive required). Key switch moved to arm relay coil drive path (SPDT, carries only coil current). Arm sense circuit now reads ARM SENSE node (arm relay COM output) through voltage divider (27 kΩ / 10 kΩ) + 3.3 V zener clamp. Added arm status feedback LEDs (green = SAFE, red = key position, red = relay energised). Added contact welding detection via arm sense. Updated §2.1, §3, §5.4.2, §5.4.3, §5.4.4, §5.4.5, §5.4.9, §5.4.10, §7.2.2, §7.2.5, §7.2.7, §7.4.1, §9.1, §9.2, §9.7, §9.13. |
| 1.14 | 2026-04-14 | Operational tuning: removed NVS key provisioning note (compile-time keys are acceptable). Made link retry more aggressive: 5 attempts at 2s intervals (was 15 at 2s then 5s). Changed missed ping RGB LED flash from 50ms to 250ms. Reduced PRE_FIRE_DELAY_MS from 5000ms to 2000ms. Reduced FIRE_PULSE_DURATION_MS from 2000ms to 1000ms. Made runtime UART logging compile-time optional (disabled by default, enabled via CONFIG_RLC_SERIAL_DEBUG_LOGGING). Updated §6.2.1, §6.4.1, §6.4.2, §7.2.3, §9.11, §14.1. |
| 1.15 | 2026-04-16 | Circuit documentation correction after on-target testing revealed errors. Split §5.4.3 into arm relay feedback (GPIO 21, renamed from "Arm Switch Position Sense") and new §5.4.3b key switch sense (GPIO 42). Fixed §5.4.4 key switch diagram to show key sense connection (was "No separate GPIO"). Fixed §5.4.5 circuit topology to include key sense circuit. Fixed §7.2.2 arming guards: guard 1 now checks key sense GPIO 42 (was incorrectly checking arm sense GPIO 21, creating circular dependency). Fixed §7.2.3, §7.2.4, §7.2.7 to distinguish key switch events from arm relay feedback events. Updated debounce table, protocol fields, task priorities, boot sequence, test descriptions, and hardware notes. Updated §5.4.9 arm relay output sensing description. |
| 1.16 | 2026-07-21 | Documentation accuracy corrections after remote display validation and full code review. Aligned all 8 buzzer/alarm pattern timings (§12.1 table, §6.4.2, App D.2) with `rlc_buzzer.c` (patterns had drifted shorter in firmware). Watchdog timeout 2 s → 5 s (§9.6, §14.1, T-S07) to match `WATCHDOG_TIMEOUT_S`. Softened ILI9488 display-ID health check (§5.5.6): clone panels report a non-standard ID (observed 0x2A403300); only a zero/garbage read-back or SPI failure is a fault. Fixed hw-test spec console transport (USB-Serial/JTAG `/dev/ttyACM*`, not UART0/`/dev/ttyUSB0`). Refreshed stale spec-version citations (Development_Progress.md, hw-test spec). Fixed stale code comments (`rlc_rgb_led.h` 50→250 ms; `rlc_watchdog.h` 2 s→5 s). |
| 1.17 | 2026-08-17 | Bug #18 as-built deviations and firmware channel gate. Recorded that the 3.3 V zener clamps on GPIO 21 (§5.4.3) and GPIO 42 (§5.4.3b) are **not fitted**, and that the arm relay contact has **no snubber** — the spec previously described all three as present. The bug #18 relay-order fix (arm relay OFF → 20 ms → channel relays OFF) makes the arm relay the sole contact breaking 6 A DC, placing that arc on the ARM SENSE node and putting the arm relay contact at risk of erosion/welding (detected by `weld_check()`, fails safe to ERROR). Added the required protection BOM (BAT54S or zener on GPIO 21/42, 47 Ω/100 nF contact snubber, SMBJ18A-class TVS on arm relay COM) and updated the §5.4.9 circuit diagram. Documented the new firmware gate `FIRE_PROTECTED_CHANNEL_MASK` (currently 0x01, channel 1 only): `guard_arm()` NACKs ARM on unprotected channels and `relay_fire_set()` refuses to energise them. Noted that the delivered fire pulse is now FIRE_PULSE_DURATION_MS + arm-relay release time (affects T-F08). |
| 1.18 | 2026-08-19 | RGB LED strip repurposed as an igniter continuity display on **both** units. §11 rewritten around a six-layer rendering model: the 8-pixel strip shows one pixel per igniter channel at all times, and system status *modulates* the map (alarm wink, stale dim, breathing) rather than replacing it — only the firing path (ARMED/PRE_FIRE/FIRING) and ERROR still take the whole strip. Removed the per-state whole-strip colours for IDLE, LINK_LOST and POST_FIRE, the boot-time RSSI bar, and the 250 ms whole-strip orange ping-miss flash (§11.2 — the 80 ms buzzer beep remains the per-miss indicator). §5.5.8: the **remote now carries an 8-pixel external strip**, not just the on-board LED. §5.4.11/§5.5.8: strip data-in is at the channel-8 end on both units, so pixel 0 — which the on-board LED mirrors in parallel — is channel 8; the on-board LEDs no longer carry independent meaning. Added alarm-wink palette and strip tuning constants to §14.1. Added host-compiled renderer tests (§15.5, T-L01…T-L09).
| 1.19 | 2026-08-19 | Strip orientation corrected to a **per-unit** setting after bench characterisation with the new `tools/strip-diag` firmware. The two strips are wired data-in at opposite ends: base at the channel-1 end (`RLC_STRIP_REVERSED = 0`, on-board LED mirrors channel 1), remote at the channel-8 end (`RLC_STRIP_REVERSED = 1`, on-board LED mirrors channel 8). v1.18 wrongly assumed both were reversed. Updated §11.0, §5.4.11, §5.5.8, §14.1. Host renderer tests (§15.5) now run once per unit so both orientations are asserted.
| 1.20 | 2026-08-19 | Recorded bug #20 in §6.2.1: the PMK, LMK and `CMD_INTEGRITY_KEY` are committed to a public repository, so the AES-128-CCM security boundary and the keyed CRC32 integrity check are ineffective against an adversary who has read the source; only the §6.2.2 replay protection (random per-link-up session token) still holds. Keys must be rotated **and** moved out of tracked files, since git history preserves superseded values. Also corrected §6.2.1's key location: `rlc_config.h`, not the non-existent `protocol_config.h`.
| 1.21 | 2026-08-19 | Error flags are now presented by name, not as a raw bitmask. Added §13.2a with the canonical display name for every bit 0-7 (including the reserved and undefined ones) and the documented deviation from §13.2's "stacked" wording: the single 40-character line at the §10.3 font floor holds one named flag, so multiple flags cycle at 2 s with an (n/total) counter rather than being truncated. Names live in `rlc_protocol.h` and are covered by host tests T-E01…T-E07 (§15.5).
| 1.22 | 2026-08-19 | Recorded bug #21 as an as-built deviation in §5.5: an undocumented 3.3 V zener on the remote's VBAT ADC node leaks 27-144 µA into the 6429 Ω divider, making the sense non-linear and under-reading by up to 30 %. Noted that the specified 0-3.0 V range already puts a full 2S pack at 95 % of the ADC's usable ceiling. Base divider calibrated (4.3 → 4.3148); remote calibration blocked.
| 1.23 | 2026-08-19 | Battery ADC sampling hardened. Added §5.6.3: each reading is now the **median of a 33-sample burst** before the existing 8-deep moving average, because a sample clipped at ADC full scale can only bias a mean upward and so make a flat pack read as healthy. Measured on the bench: in a burst with 9 of 33 samples clipped the mean read 571 counts high, about +2 V through the base's divider ratio, while the median was exact. Added the burst constants to §14.1 and host tests T-B01…T-B07 to §15.5. Updated the stale "8-sample moving average" wording in §4, §5.4.7 and §7.3.3.
| 1.24 | 2026-08-19 | Fixed the LINK LOST screen's two dynamic fields, both of which were driven from `missed_pings` — a counter gated behind the LINKED state that stops advancing the moment the link drops, freezing the display at "Last contact: 1 s ago" and "Attempts 3". Added `rlc_link_status_t.ms_since_contact`, a real elapsed time from the last well-formed frame from the peer, and pointed the attempt count at `linkreq_attempts`. Documented the correct field sources in §10.2.5.
| 1.25 | 2026-08-20 | Arm-sense reporting corrected end to end. `STATUS_UPDATE` previously carried the key switch **twice** (debounced as `base_arm_switch`, raw as `arm_switch_hw`) while the header documented both as arm sense, so the remote could never see the arm relay and the ARMED screen's "SENSE CONFIRMED" was derived from the key switch. Fields renamed to `base_key_switch` / `base_arm_sense` and the second now carries the real debounced arm sense (GPIO 21) — same 14-byte struct, no size change. Main status line reworked to §10.2.2's four-state BASE field (SAFE/READY/ARMED/WELD!, plus ? when stale), with ARMED and WELD! driven by the arm sense rather than the key. ARMED screen now reports the real sense. **Firmware bumped to 1.1.0** — the field's meaning changed while its size did not, so the strict version gate is what prevents a mixed pair from misinterpreting it. Host tests T-M01…T-M07 added to §15.5.
| 1.26 | 2026-08-20 | Recorded bug #25 as §5.6.2a: **no hardware undervoltage cut-off is fitted on either pack, and none was ever specified.** Pack protection rests entirely on firmware thresholds, which do not disconnect the load — the ERROR state halts operation while regulators, backlight, LEDs and MCU keep drawing — and which only apply while firmware runs. Noted that `BASE_VBAT_CRITICAL_MV` sits exactly at 3.00 V/cell, leaving no margin before permanent capacity loss. Required trip points and the ordering constraint (hardware cut-off must sit above the firmware threshold) documented. Also recorded the channel-1 continuity ADC clamp as-built: 2x 1N5819, one to GND and one to +3.3 V.
| 1.27 | 2026-08-20 | Encoder decoding brought up to what §5.5.1 already specified. The firmware had implemented **neither** the cycle-position decoder nor `ENC_DIVIDER` — it used the Gray-code level comparison the section explicitly rejects, and every accepted edge became a channel change; B also interrupted with no handler attached, losing three of four transitions. This caused the reported oversensitive channel selection and left the input open to electrical noise. Now implemented as specified with `ENC_DIVIDER` **4** (was documented as 3) and `ENC_LOCKOUT_US` 2 ms, both in §14.1. Added host tests T-Q01…T-Q06 and an as-built note to §5.5.1.
| 1.28 | 2026-08-20 | Added `ENC_REVERSED` (§5.5.1, §14.1): the encoder's rotation sense is a board property, since which way a KY-040 counts depends on how A and B are wired. Set to 1 as built — the v1.27 decoder moved the channel selection opposite to the knob. Host test T-Q07 pins the direction explicitly so a rewire has to update the constant rather than silently inverting the operator's controls.
| 1.29 | 2026-08-21 | Continuity reworked after bench characterisation. **SHORT merged into CONNECTED** (§5.4.2): at 1 mA a dead short and a 1.5-1.9 Ω igniter differ by 1-1.6 mV, within noise, drift and lead contact resistance; three runs on a fixed igniter implied 0.77/1.15/1.77 Ω. **`CONT_GOOD` renamed `CONT_CONNECTED`** — the band means a low-resistance path exists, not that the igniter is good. Wire encoding unchanged (value 3 retained, never emitted), so no version bump. Continuity ADC switched to **0 dB** attenuation and calibration **re-enabled** (it was hardcoded off, costing +369 mV of offset). `CONT_OPEN_UV` 1500000 → 432000 µV, finally the documented ~500 Ω. Recorded as-built hardware: snubbers on all 8 channel relays and the arm relay; 1N5819 clamps to GND and 3V3 on CH1-6 (CH7-8 pending).
| 1.30 | 2026-08-21 | **Continuity ADC clamps completed on all eight channels** (2x 1N5819 per channel, one to GND and one to +3V3). Recorded the assessment: the 1 A part is *correct* at this node, because there is zero series resistance between the relay NC contact and the ADC pin — the clamp carries the full fault current, which a BAT85 (200 mA) or 1N5711 (15 mA) would not survive — and node impedance is set by the igniter, so leakage does not disturb the bands. This does **not** contradict bug #22, where 1N5819 is the wrong part on a 6.4 kΩ divider. Two residual gaps added to the §5.4.9 protection BOM: (a) the 3V3-side clamps inject fault current directly into the 3.3 V rail, now on eight channels instead of one; (b) 1N5819 V_f puts the pin at 3.9-4.2 V during a fault, above the 3.6 V absolute maximum. Required: a **220 Ω sense-branch series resistor per channel** between the relay NC contact and the pin/clamp junction, and an **active 3.3 V rail clamp** (TL431 shunt at ~3.57 V — a 3V6 zener is not adequate in a 3.3/3.6 V window). Both change the continuity thresholds and require recalibration. |
| 1.31 | 2026-08-21 | Applied the post-fix code review (`Code_Review_AllPhases_20260821_1523.md`). **Firmware 1.1.0 → 1.1.1.** Two Majors: **(N1)** the debounce engine's first stable reading deliberately fires no callback — required by the fire button's fresh-press interlock (§5.5.3) — but the remote's arm switch cached its state only from that callback, so a key already turned to ARM at power-up was never seen and arming was impossible until the key was toggled. §5.3 now states the contract explicitly and requires consumers to adopt the initial determination by polling. **(N2)** `esp_timer_stop()` does not cancel an already-dispatched callback, so the siren's pattern mutex was not sufficient: a stale tick could drive the siren back ON after `siren_off()` (with the timer stopped, permanently) or silence it through the whole PRE_FIRE countdown. A pattern-active flag now invalidates superseded callbacks (§5.4.7). Minors: remote now treats a critical battery in LINK_LOST as terminal, matching the base; link health window and expected-ping tracker reset with the session, so ARM is no longer refused `COMM_DEGRADED` for ~5 s after every recovery; the stale-status timeout latches instead of re-firing at 20 Hz; `rlc_link_next_seq()` drops the link on overflow like its internal siblings; the placeholder handshake `STATUS_UPDATE` (which hardcoded `base_state = IDLE`, a false "safe" if the base was in ERROR) is replaced by a real push via a new application hook; ESP-NOW send failures are counted rather than logged from Wi-Fi task context; remote input callbacks are registered before their tasks start; five unchecked `xTaskCreate` calls checked, two of them fatal; `ARM_SENSE_VERIFY_TIMEOUT_MS` and `SIREN_LINK_LOST_DURATION_MS` are now real constants (§14.1). Host tests T-D01…T-D06 added (§15.5), and the debounce engine is no longer stubbed out in the host harness. **(N3, Critical, found on target after flashing)** `esp_task_wdt_reconfigure()` rebuilds the TWDT subscriber list, and the remote called it at boot step 8 — *after* `display_start_task()`. The display and buzzer tasks were silently unsubscribed, the display logged "task not found" at 20 Hz, and the unfed watchdog triggered at 11.4 s and panicked (`LoadProhibited`) inside its own report path, rebooting the unit on every boot. §9.13 gains a step 0 (reconfigure before any task) and a step 11 (`app_main` subscribes itself after the slow init), with the ordering stated as a requirement. |
| 1.32 | 2026-08-23 | **217 Ω sense-branch resistors fitted on all eight continuity channels**, closing both gaps the v1.30 assessment left open (arc into the 3.3 V rail limited to ~41 mA; pin held at ~3.55 V during a fault instead of 3.9–4.2 V). The resistor sits between the relay NC contact and the ADC pin/clamp junction, so it is **in the sense current path** and lifts every reading by ~204 mV. Thresholds re-derived against V = 3.3 × Rx/(R_ref + Rx), Rx = (R_sense + R_ign) ∥ R_pull: `CONT_MARGINAL_UV` 66000 → **261000** µV and `CONT_OPEN_UV` 432000 → **586000** µV, both the *same* ~67 Ω and ~500 Ω boundaries as before. New constant `CONT_R_SENSE_OHM` (§14.1). Verified on target against five known loads (igniter, 14.9 Ω, 74.3 Ω, 2k16, 4k28, open): predicted and measured agree within 2 mV and all six classify correctly. Full scale (950 mV) is now reached at ~1117 Ω instead of ~1670 Ω, which costs nothing because everything above 500 Ω is OPEN. Base ESP32 moved to an **external antenna** (0 Ω link relocated to the U.FL connector). **`FIRE_PROTECTED_CHANNEL_MASK` widened 0x01 → 0xFF** (§14.1) — the bug #18 protection BOM (snubber + 2x 1N5819 + 217 Ω) is now complete on all eight channels, so the firmware gate that restricted firing to channel 1 since 2026-07-21 is lifted. Channels 2-8 have still never been fired. New **bug #28**: the base ARM RELAY LED lights with the key in SAFE while the relay stays de-energised — an LED-current-sized sneak path around the §5.4.4 key switch. Fire testing is held until it is resolved. |
| 1.33 | 2026-08-25 | External antennas fitted on **both** units and a 200 m range test flown: **−93 dBm, link holding**, base on the ground, and the link could only be dropped by unscrewing an antenna. §6.1 records the measured range, the expected drop-out window (**−96 to −99 dBm** — 1 Mbps DSSS sensitivity ≈ −98 dBm, extended 1–3 dB because link loss needs *three consecutive* missed 500 ms slots, so failure occurs at roughly 30–50 % PER over a ~3 dB transition), and the antenna configuration. The key finding is that −93 dBm at 200 m is **two-ray ground-reflection limited, not hardware limited**: the measurement matches the d⁴ model to within 0.5 dB, the remaining margin buys only ~25 % more distance in that geometry (~250–260 m), and raising both units to ~1.5 m is worth ~30 dB. `ERR_COMM_DEGRADED` (>30 % loss over the 10-slot window) refuses arming a couple of dB before the link fails. Recorded that the RSSI reading is not a usable early warning — uncalibrated, compressed in the high −90s, and smoothed by `RSSI_AVERAGE_WINDOW`. |
| 1.34 | 2026-08-26 | **Three base hardware defects closed by soldering rework; fire testing unblocked.** **Bug #28** (ARM RELAY LED lit with the key in SAFE) was an indicator-wiring fault, now corrected: the coil LED lights only when the arm relay is genuinely energised. A second indicator fault found at the same time — the arm-key red and green LEDs lighting *simultaneously* in SAFE — was also fixed; they now read red = ARMED, green = SAFE. The hardware AND gate of 5.4.4 was never compromised: both faults were in the indicators, not in the key switch's break of the coil circuit. The operator rule "green = SAFE, red coil LED = fire bus live" is true on this unit again, and the fire-testing hold this bug imposed is lifted. **Bug #27** (siren not connected): the IRLZ44N driver on GPIO 40 is fitted with its 150 Ω gate series resistor, 10 kΩ gate pull-down and a 1N5819 flyback diode across the siren (cathode VBAT+, anode drain) — see the revised as-built note in 5.4.8. Two items stay open there: confirm the siren draws under 1 A so the 1 A diode is correctly rated, and run the N2 retest (bench tests 2 and 3, LINK_LOST 4-cycle, ERROR 3-blast, silent-at-power-on), which is still outstanding — N2 remains code-inspection-only. **Bug #19** (base LED strip dark from pixel 4): resolved by replacing the strip. Root cause was pixel 3's *output stage* — it rendered its own colour correctly but passed no data downstream, which is why it read as healthy in every visual test. T-L18 now PASSES. No firmware change in any of the three. |
| 1.35 | 2026-08-26 | **Firmware 1.1.1 → 1.1.2. Two on-target behaviour changes, both base-only.** **(1) The siren is continuous from ARMED through PRE_FIRE and FIRING.** The 500 ms ARMED pulse (inherited from v1.1, when the base still had a plain buzzer) fought the siren's own internal modulation: every power-up edge restarted its sweep, so the device never reached the loud part of its cycle and the pulsed ARMED warning was *less* audible than a steady tone. `siren_start_pulse()` removed; 5.4.8, 7.2.2, 7.2.3, 7.4.1, the state diagram and the transition tables updated. LINK_LOST and ERROR stay patterned — those patterns carry meaning and are short enough that the interference does not matter. Side effect: the flyback diode now switches once per sequence instead of once per second, which removes the repetitive-avalanche concern raised in v1.34. **(2) Continuity loss on the armed channel now disarms the base.** Found on target: arm a channel, pull the igniter lead, and the base stayed ARMED with the siren sounding and the fire path live, with nothing on either unit reporting it. v1.8 had removed this deliberately ("sensing disabled - stale data"), a rationale that the v1.10 SPDT redesign made obsolete - continuity is live throughout ARMED and PRE_FIRE because the channel relay sits on NC - and 4.x's own Phase 3 test criteria had listed "continuity to OPEN" as a required disarm trigger since then, so the document contradicted itself for 25 revisions. New `EVT_CONTINUITY_CHANGED` carries channel and band to the base FSM (7.3.1 step 4); 7.2.7 gains the trigger and the full requirements. Scoped deliberately: OPEN only (matching arming guard 2), armed channel only, ARMED and PRE_FIRE only - **not** FIRING, where the armed channel reads OPEN by design because its relay is on NO, and **not** POST_FIRE, where OPEN is the success indicator. Detection latency is up to ~800 ms (round-robin sampling), well inside `ARM_TIMEOUT_MS`. New tests T-A16, T-A17 and T-A18 (the last a regression guard that a non-armed channel going OPEN does *not* disarm). No wire-protocol change, but the strict version check covers all three components, so both units must be flashed together. |
| 1.36 | 2026-08-26 | **`PRE_FIRE_DELAY_MS` 2000 → 5000. Firmware 1.1.2 → 1.1.3.** Operator decision taken during the G2 test campaign. T-A17 requires the operator to disconnect an igniter *during* the pre-fire countdown; at 2 s that could not be done in time and an igniter fired. The countdown was temporarily raised to 10 s to complete the test, then settled at 5 s as the operating value: long enough to act inside, short enough that holding the fire button does not invite the fatigue-release the original 2000 ms was chosen to avoid. §7.2.2, §14.1, README and the project summary updated. No logic change; both units must be flashed together because the remote runs its own local countdown against the same constant. |
| 1.37 | 2026-08-26 | **G2 arming suite run on target; three stale tests corrected and one remote defect fixed (firmware 1.1.3 → 1.1.4).** 13 of 18 arming tests PASS (T-A01..A04, A06..A10, A14, A16..A18), T-A12 PASS, two are not runnable as written, and two await a fault-injection harness. **T-A05 reworded:** it is unreachable through the operator UI because selecting a second channel requires an encoder rotation, which T-A08 requires to disarm — the two tests contradict each other. The NACK 0x0A guard stays as defence-in-depth against a remote bug or replayed frame. **T-A15 marked superseded and T-A09's band list corrected:** both still described a SHORT band that was merged into CONNECTED on 2026-08-21 (bug #26), so neither could pass as written. **Remote defect found and fixed:** with the base in ERROR a long-press to arm produced no beep, no message and no transmission. The base's ERROR handler is inert and NACKs nothing, and the remote's ACK-timeout branch was the only failure path with no operator feedback, so a base that never answered was indistinguishable from a long-press that had not registered — and the remote never inspected `base_state == STATE_ERROR` despite receiving it in every STATUS_UPDATE. A new arming guard now refuses locally and names the flag (`BASE ERROR: VBAT CRITICAL`), and the timeout path reports `NO RESPONSE FROM BASE`. §8.2.3 guard list extended. Also demonstrated incidentally on target: **T-S03** (base below VBAT_CRITICAL → ERROR, and ERROR correctly stays latched after the voltage recovers) and **T-S14** (arm timeout at 10022 ms against a 10000 ms constant). |
| 1.38 | 2026-08-26 | **T-A11 and T-A13 closed with a fault-injection harness; firmware 1.1.4 → 1.1.5.** Neither test is inducible from outside the firmware — link loss trips at 1.5 s, long before the staleness timeout, so "linked but stale" cannot be produced by interfering with the radio; and nothing in normal operation emits a malformed ACK. New `CONFIG_RLC_FAULT_INJECTION` build option (base only, default off, `./build_base.sh --inject`) adds a UART0 console that can withhold STATUS_UPDATE while still answering PINGs (`s`) and corrupt the channel of one ARM ACK (`a`). The build emits a `#warning`, the firmware prints a boot banner, and the build script refuses to flash if the option did not actually reach the built config. **T-A11 PASS:** remote refused locally with `NO BASE STATUS DATA` and transmitted nothing. The FSD's expected text `DATA STALE — CANNOT ARM` was wrong and has been corrected — no build has ever displayed it. **T-A13 initially PARTIAL, now PASS:** the remote correctly detected the mismatch, refused to enter ARMED and commanded a disarm (relay out 190 ms after the bad ACK), but displayed nothing — the mismatch branch beeped without saying why, the same defect class as the ACK-timeout path fixed in v1.37. Fixed in **1.1.5**: the branch now shows `CHANNEL MISMATCH ERROR`, and the retest passes end to end. |
| 1.39 | 2026-08-26 | **No silent refusals. Firmware 1.1.5 → 1.1.6.** Two operator-driven decisions. **(1) The base now answers commands while in ERROR** with a new NACK reason `0x0E` ("BASE IN ERROR") instead of discarding them. Its ERROR handler was a bare `break;`, so the remote could only time out — and a timeout carries no reason, leaving an operator unable to distinguish a dead link from a base needing a power cycle. All four commands are answered, including DISARM: refusing a disarm on an already-safe base looks odd, but "I am in ERROR" is the operative fact in every case, and silently ACKing would report "all is well" about a unit that is out of service. The specific fault is not carried in the NACK — the payload is a fixed 6 bytes — so the remote names it from the `error_flags` it already caches, giving "BASE ERROR: VBAT CRITICAL" rather than a bare reason code. **(2) Every remaining silent branch on the remote now beeps and displays.** New §7.2.9a states this as a requirement. The FIRE guard family was the worst of it — arm key off, base not armed, stale status, degraded link, send failure, key-off-after-ACK and no-response all dropped the remote to IDLE without a word, with the operator holding a fire button watching nothing happen. Also covered: ARM send and retry failures, ARM cancellation, base/remote state mismatch, the stale-status timeout, and base-ended-sequence. An audit of the remote FSM leaves only five log-without-display sites, all legitimate: multi-arm (which displays via a different path), `do_enter_error()` (always reached through `do_enter_error_text()`), and three init failures that happen before the display task exists. New NACK code means both units must be flashed together. |
| 1.40 | 2026-08-26 | **v1.39's changes validated on target.** Three checks, all PASS. **T-A19 (new):** ARM while the base is in terminal ERROR → base sent `NACK 0x0E`, remote logged `ARM NACK: 0x0e (BASE IN ERROR)` and displayed **"BASE ERROR: VBAT CRITICAL"** — the reason travelling over the wire and being resolved to the specific fault from cached `error_flags`. Before v1.39 this was a silent timeout. **T-A20 (new):** spot-checked two of the newly-reporting branches — the stale-status timeout now shows "BASE STATUS LOST" (log-only before), and a base disarming underneath an ARMED remote now shows "BASE DISARMED" (log-only before). The fault-injection harness gained an `e` command for T-A19. Its first form suppressed STATUS_UPDATE and left a ~4 s window to arm, which is a race rather than a test; it now falsifies `base_state` to IDLE in STATUS_UPDATE while keeping `error_flags` truthful, so the remote's local ERROR guard stays quiet indefinitely and the enrichment path is still exercised. Base reflashed with a normal build afterwards and verified clean (option absent from the built config, zero injection symbols in the ELF). |
| 1.41 | 2026-08-26 | **Fire button ring LED now reports state instead of the operator's finger.** Reported from the bench: the ring does not go red when the system is armed. It never did — `rlc_fire_button.c` has driven the LEDs from the debounce callback since `aafacd0` (Phase 2), red while held and green while released, so the ring showed the button's position rather than whether pressing it would do anything. Line 1110 has specified ARMED/PRE_FIRE/FIRING since v1.13, so this was an unimplemented requirement rather than a regression. New `fire_button_set_live()` is driven from the remote FSM every tick — not on transitions alone, because the base dropping out underneath an ARMED remote arrives as a `STATUS_UPDATE`, not as a local state change. **Red requires both halves:** the remote in ARMED/PRE_FIRE/FIRING *and* a fresh `STATUS_UPDATE` confirming the same channel armed at the base, reusing the same two conditions the §8.2.4 FIRE guards check so the ring and the guards can never disagree — the ring is red exactly when a press would be accepted. The press-driven behaviour is removed rather than kept as an override: a press in IDLE is ignored (§8.2.3), so flashing red for it reports an action that will not happen, which is the misleading-indicator failure §7.2.9a exists to prevent. |
| 1.42 | 2026-08-26 | **Bug #30 fixed (firmware 1.1.7 → 1.1.8); G3 started and three of its tests found unreachable as written.** **Bug #30:** the continuity-loss disarm was edge-triggered with no level-triggered backstop — `continuity_task` posts only on band change, `STATE_IDLE` does not handle `EVT_CONTINUITY_CHANGED`, and the M1 arm verify parks the FSM in IDLE for up to 200 ms, so a disconnection inside that window was dropped with no edge left to report it. Fixed two ways: a re-check at arm-verify completion (abort with `NACK_NO_CONTINUITY`), and a periodic level check in `check_timers()` for ARMED/PRE_FIRE that also covers an event dropped by a full FSM queue — which an entry check alone does not. **T-F02 PASS**, which also regression-tested the fix (the arm completed through the verify path the new check sits on) and confirmed the v1.41 ring LED: green→red on arm, back to green on abort, following state rather than the button. **T-F04 and T-F05 recorded as covered** by existing evidence — T-A17 produced `NACK 0x05` for a fire command outside ARMED, and T-A16 proved continuity is live during ARMED. **T-F06, T-F07 and T-F09 are not reachable as written** — see their rows. T-F06's analysis is the notable one: with a 1000 ms pulse and 1500 ms link-loss detection there is no window in which link loss can be observed during FIRING, making `COMPLETE_PULSE_ON_LINK_LOSS` unreachable config. Operator decision: leave the pulse at 1 s and verify that logic by review rather than change a fire-path constant to suit a test. |
| 1.43 | 2026-08-27 | Doc-consistency sweep from full code review (App D.4, §15.3 T-F05, §8.2.3/App B.2 stale text, §12.2 siren table, SHORT-band remnants, App C corrections, §14 gaps). |
| 1.44 | 2026-08-27 | **All findings of the full-codebase review (RLC-REVIEW-ALL-008) applied; firmware 1.1.8 → 1.1.9.** Spec changes accompanying the fixes: §7.2.4 guard 2 restated as a freshness test that must not be folded into guard 4's failure-rate test, and its action 2 now requires a stopped-first, return-checked fire-timer start that never aborts; §7.2.5 gains an explicit "stop the fire timer" step on the *successful* pulse-completion path — the omission that made a second launch per power cycle panic with the igniter energised (BF-01, CRITICAL); §5.5.6's runtime display health check corrected to run in every state (the old "during IDLE" wording contradicted its own ARMED/PRE_FIRE/FIRING requirement) and given two-consecutive-failure and SPI-return-code requirements; §5.4.6/§7.3.1 relay-settling delay specified and now actually implemented (`CONT_RELAY_DROPOUT_MS` had been dead since it was defined); §9.10 task tables audited against the built firmware (`espnow_rx` added, link manager renamed and re-levelled, encoder stack corrected) and `buzzer_task` moved back to 1/core 1 in firmware rather than relaxing the spec; §12.1 `BEEP_PING_FAIL` pinned to rising-edge semantics; §14.5 `CONT_TRACE_INTERVAL_MS` default 1000 → 0 for field builds; §7.3.1 step 2 SHORT-band remnant corrected to MARGINAL. |

## Table of Contents

1. [Introduction](#1-introduction)
2. [System Overview](#2-system-overview)
3. [Definitions and Abbreviations](#3-definitions-and-abbreviations)
4. [Development Approach](#4-development-approach)
5. [Hardware Interface Specification](#5-hardware-interface-specification)
6. [Communication Protocol Specification](#6-communication-protocol-specification)
7. [Base Unit Functional Specification](#7-base-unit-functional-specification)
8. [Remote Unit Functional Specification](#8-remote-unit-functional-specification)
9. [Safety Requirements](#9-safety-requirements)
10. [Display Specification](#10-display-specification)
11. [RGB LED Status Specification](#11-rgb-led-status-specification)
12. [Audio Feedback Specification](#12-audio-feedback-specification)
13. [Error Handling](#13-error-handling)
14. [Configuration and Constants](#14-configuration-and-constants)
15. [Test Requirements](#15-test-requirements)
16. [Appendix A — Message Format Reference](#appendix-a--message-format-reference)
17. [Appendix B — State Transition Tables](#appendix-b--state-transition-tables)
18. [Appendix C — Pin Assignments](#appendix-c--pin-assignments)
19. [Appendix D — Protocol Exception Handling](#appendix-d--protocol-exception-handling)

---

## 1. Introduction

### 1.1 Purpose

This document provides the complete functional specification for a two-unit high-power rocket launch controller system called the **ESP32 Wireless Rocket Launch Controller**. It is intended to give a developer all the information necessary to implement the embedded firmware for both units without ambiguity.

### 1.2 Scope

The system consists of:

- **Base Unit** — stationed at the launch pad, controls ignition hardware for up to 8 channels.
- **Remote Unit** — held by the launch operator at a safe distance, provides command input and status display.

Both units are built on identical ESP32-S3-DevKitC-1 development boards with ESP32-S3-WROOM-1 N16R8 modules (16 MB Flash, 8 MB Octal PSRAM) and communicate wirelessly via ESP-NOW.

### 1.3 Applicable Standards and References

- NAR (National Association of Rocketry) safety code for high-power rocketry launch systems.
- Tripoli Rocketry Association range safety officer procedures.
- Espressif ESP-NOW programming guide (ESP-IDF v5.x).
- ILI9488 datasheet (4-wire SPI, 480×320 pixels).
- ESP32-S3-WROOM-1 datasheet (Espressif).
- ESP32-S3 Technical Reference Manual (Espressif).

### 1.4 Design Philosophy

Safety is the overriding design constraint. The system shall implement defence-in-depth: no single software or hardware fault shall be capable of causing an unintended ignition. All arming sequences require deliberate, multi-step human action on both units. The software shall fail safe — any anomaly (communication loss, invalid state, low battery) shall result in immediate disarming of all channels.

---

## 2. System Overview

### 2.1 Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        REMOTE UNIT                              │
│                                                                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌───────────────┐   │
│  │ Rotary   │  │ Arm/     │  │ Fire     │  │  ILI9488 LCD  │   │
│  │ Encoder  │  │ Disarm   │  │ Button   │  │  480×320 SPI  │   │
│  │ (+ push) │  │ Switch   │  │          │  │               │   │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └───────┬───────┘   │
│       │              │             │                │           │
│  ┌────┴──────────────┴─────────────┴────────────────┴───────┐   │
│  │                     ESP32-S3 (N16R8)                     │   │
│  │  ┌─────────────┐  ┌──────────────┐  ┌────────────────┐  │   │
│  │  │ State       │  │ ESP-NOW      │  │ Display        │  │   │
│  │  │ Machine     │  │ Comms        │  │ Manager        │  │   │
│  │  └─────────────┘  └──────┬───────┘  └────────────────┘  │   │
│  └──────────────────────────┼───────────────────────────────┘   │
│       │         │     │     │                                   │
│  ┌────┴───┐ ┌───┴──┐ ┌┴──┐ │                                   │
│  │ Buzzer │ │ VBAT │ │RGB│ │                                   │
│  │        │ │ ADC  │ │LED│ │                                   │
│  └────────┘ └──────┘ └───┘ │                                   │
│                             │  ESP-NOW (2.4 GHz, encrypted)     │
└─────────────────────────────┼───────────────────────────────────┘
                              │
              ════════════════╪═══════════════════
                   Wireless link (max ~200 m LOS)
              ════════════════╪═══════════════════
                              │
┌─────────────────────────────┼───────────────────────────────────┐
│                        BASE UNIT                                │
│                             │                                   │
│  ┌──────────────────────────┼───────────────────────────────┐   │
│  │                     ESP32-S3 (N16R8)                     │   │
│  │  ┌─────────────┐  ┌──────┴───────┐  ┌────────────────┐  │   │
│  │  │ State       │  │ ESP-NOW      │  │ I/O            │  │   │
│  │  │ Machine     │  │ Comms        │  │ Manager        │  │   │
│  │  └─────────────┘  └──────────────┘  └────────────────┘  │   │
│  └──────┬───────┬────────┬──────────┬───────────────────────┘   │
│         │       │        │          │                           │
│  ┌──────┴──┐ ┌──┴─────┐ ┌┴────────┐┌┴────────────┐            │
│  │Arm Key  │ │ VBAT   │ │ Siren   ││ RGB LED     │            │
│  │Switch   │ │ ADC    │ │         ││ (status)    │            │
│  │(SPDT)   │ └────────┘ └─────────┘└─────────────┘            │
│  └────┬────┘                                                    │
│       │ (coil drive path)                                       │
│  ┌────┴──────────────────────────────────────────────────┐      │
│  │              ARM RELAY (SPDT)                          │      │
│  │  NO = fire path to channel relays   NC = disconnected  │      │
│  │  Drive: key switch AND IRLZ44N MOSFET (AND gate)       │      │
│  └─────────────────────────┬─────────────────────────────┘      │
│                            │ ARM SENSE node                      │
│  ┌─────────────────────────┴───────────────────────────────┐    │
│  │              CHANNEL RELAYS (SPDT, 8×)                  │    │
│  │  NC = continuity sense    NO = fire path (via arm relay)│    │
│  └─────────────────────────┬───────────────────────────────┘    │
│                            │                                    │
│  ┌─────────┬─────────┬─────┴───┬─────────────────┐             │
│  │ CH 1    │ CH 2    │ CH 3   │  ...  │ CH 8    │             │
│  │ SPDT    │ SPDT    │ SPDT   │       │ SPDT    │             │
│  │ Relay + │ Relay + │ Relay +│       │ Relay + │             │
│  │ Cont.   │ Cont.   │ Cont.  │       │ Cont.   │             │
│  └─────────┴─────────┴────────┴───────┴─────────┘             │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │         ARM SENSE INPUT (GPIO 21, divider + zener)       │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Operational Concept

The launch sequence follows a strict multi-step procedure:

1. The base unit is placed at the launch pad and powered on. It enters IDLE state with all channel relays in their de-energised (NC) position. In this state, no current can reach any igniter because the fire path (via the relay NO contact) is disconnected. The NC contact routes each channel to its continuity sensing circuit, enabling continuous igniter monitoring.
2. The operator retreats to a safe distance with the remote unit.
3. The remote unit is powered on, discovers the base unit, and establishes a communication link.
4. The operator selects a channel on the remote using the rotary encoder.
5. The operator turns the physical arm key/switch on the remote, then presses the encoder button to confirm. This sends an ARM command for the selected channel.
6. The base unit validates that the arm sense input confirms VBAT is present on the fire path (key switch ON, arm relay energised). Only if both arm conditions are met does it enter the ARMED state. The siren begins sounding continuously. The channel relay remains de-energised until the FIRING state.
7. The operator presses and holds the fire button. The remote sends a FIRE command. The siren continues sounding (unchanged from ARMED).
8. The base unit energises the selected channel's SPDT relay (switching from NC/continuity to NO/fire path), applying battery power through the arm relay and channel relay to the igniter for a defined pulse duration, then automatically disarms. The siren is switched off.
9. Disarming any switch, losing communication, selecting a different channel, or any anomaly results in immediate de-energising of all relays (arm relay + channel relays, returning to safe position).

---

## 3. Definitions and Abbreviations

| Term | Definition |
|---|---|
| **Base** | The launch pad unit that controls igniter hardware |
| **Remote** | The handheld operator unit with display and controls |
| **Channel** | One igniter circuit (relay + continuity sense), numbered 1–8 |
| **Continuity** | Analogue measurement of the igniter circuit resistance via ADC. The continuity sensing circuit uses the SPDT relay's NC contact to route the igniter to the sense circuit when the relay is de-energised. Uses ≤ 1 mA test current. Results are classified into three bands: CONNECTED, MARGINAL, OPEN (the former SHORT band was folded into CONNECTED, v1.29). |
| **Continuity Band** | One of three classifications derived from the ADC continuity measurement (v1.29; SHORT is folded into CONNECTED): CONNECTED (< ~67 Ω, low-resistance path present — includes a dead short, which the measurement cannot distinguish), MARGINAL (~67–500 Ω, high resistance connection), OPEN (> ~500 Ω or no igniter). Boundaries are the `CONT_MARGINAL_UV` / `CONT_OPEN_UV` thresholds of §14.5. |
| **SPDT Relay** | Single Pole, Double Throw relay used per channel. NC contact connects to continuity sense circuit; NO contact connects to fire path (via arm relay). COM contact connects to igniter high-side. When de-energised, the igniter is routed to the continuity circuit. When energised, the igniter is connected to the fire path. |
| **Arm** | The act of enabling a channel for firing (entering ARMED state). The channel relay remains de-energised until the FIRING state. |
| **Fire** | The act of applying current to an igniter to initiate combustion |
| **ESP-NOW** | Espressif connectionless Wi-Fi communication protocol, operates on 2.4 GHz |
| **RSSI** | Received Signal Strength Indicator (dBm) |
| **LMK** | Local Master Key (ESP-NOW encryption) |
| **PMK** | Primary Master Key (ESP-NOW encryption) |
| **VBAT** | Battery voltage |
| **FSM** | Finite State Machine |
| **ADC** | Analogue-to-Digital Converter |
| **BOD** | Brown-Out Detector — hardware voltage monitoring on ESP32-S3 |
| **Arm Switch Sense** | A GPIO input that senses the arm relay output (ARM SENSE node) via a voltage divider and zener clamp. Simultaneously verifies that the physical key switch is ON, the arm relay MOSFET is driven, the arm relay contacts are closed, and VBAT is present on the fire path. |
| **Rotary Encoder** | Incremental encoder with quadrature A/B outputs and push-button |
| **Heartbeat** | Periodic ping/pong message pair used to assess link quality |
| **LOS** | Line of Sight |
| **WS2812** | Addressable RGB LED (NeoPixel), driven via RMT peripheral on GPIO 48 |

---

## 4. Development Approach

### 4.1 Codebase Architecture

The system shall be implemented as a **single codebase** with compile-time target selection. The build target (Base or Remote) is selected via a Kconfig option `CONFIG_RLC_UNIT_TYPE`.

```
rlc/
├── components/
│   ├── rlc_common/          # Shared code: protocol, messages, config, debounce,
│   │                        #   battery, version, RGB LED, watchdog, logging
│   ├── rlc_base/            # Base-only: relay control, continuity, siren,
│   │                        #   base state machine, command handler
│   └── rlc_remote/          # Remote-only: display, encoder, fire button,
│                            #   remote state machine, command sender, buzzer
├── main/
│   └── main.c               # Branches on CONFIG_RLC_UNIT_TYPE
├── Kconfig                   # CONFIG_RLC_UNIT_TYPE choice: BASE or REMOTE
├── CMakeLists.txt
├── sdkconfig.base            # Default sdkconfig for base builds
└── sdkconfig.remote          # Default sdkconfig for remote builds
```

The `main.c` entry point:

```c
#if defined(CONFIG_RLC_UNIT_BASE)
    base_app_main();
#elif defined(CONFIG_RLC_UNIT_REMOTE)
    remote_app_main();
#else
    #error "CONFIG_RLC_UNIT_TYPE must be set to BASE or REMOTE"
#endif
```

The base build links `rlc_common` + `rlc_base`. The remote build links `rlc_common` + `rlc_remote`. Unused components are not compiled into the binary.

### 4.2 Version Numbering

All code shall carry a version number in the format `MAJOR.MINOR.PATCH`:

- **MAJOR**: incremented for protocol-breaking changes (both units must be reflashed).
- **MINOR**: incremented for feature additions or behavioural changes.
- **PATCH**: incremented for bug fixes.

The version is defined in `rlc_common/include/rlc_version.h`:

```c
#define RLC_VERSION_MAJOR  1
#define RLC_VERSION_MINOR  0
#define RLC_VERSION_PATCH  0
#define RLC_VERSION_STRING "1.0.0"
```

The firmware version transmitted during link establishment uses MAJOR, MINOR, and PATCH (3 bytes: one byte each). **Both units must match on all three components** (major, minor, AND patch) to establish a link. If any component differs, the link is rejected. **Rationale:** strict MAJOR.MINOR.PATCH matching is an intentional safety decision — both units must run identical firmware to ensure consistent behaviour in a safety-critical system. The version string shall be displayed on the remote's splash screen and is incremented with every code change committed.

### 4.3 Development Phases

Development shall proceed in phases. Each phase produces a testable deliverable. The user will test each phase and provide feedback before the next phase begins.

#### Pre-Requisite — Hardware Validation

Before Phase 1 begins, all hardware peripherals on both units SHALL be validated using standalone test firmware. The hardware test projects are defined in separate specifications:

- **Base unit:** `RLC_Base_Hardware_Test_Specification.md` — located in `rlc-hw-test-base/`
- **Remote unit:** `RLC_Remote_Hardware_Test_Specification.md` — located in `rlc-hw-test-remote/`

These are independent ESP-IDF projects with no shared code with the main RLC codebase. Their sole purpose is to exercise and validate each hardware peripheral in isolation before the main system is built. Issues found during hardware validation are far cheaper to fix than after the full system is integrated.

Both hardware test projects SHALL be completed and all test criteria passed before Phase 1 development begins.

#### Phase 1 — Foundation and Communication

**Goal:** Both units boot, establish a link, and exchange heartbeats. RGB LED shows status.

Deliverables:
- Project scaffolding (CMake, Kconfig, component structure).
- `rlc_common`: ESP-NOW driver wrapper, message serialisation/deserialisation, protocol header and struct definitions, encryption setup, sequence number management, session token generation.
- `rlc_common`: RGB LED driver (WS2812 via RMT on GPIO 48), status colour patterns.
- `rlc_common`: Watchdog setup.
- `rlc_common`: Version header.
- Link establishment: LINK_REQUEST / LINK_ACK handshake with firmware version check.
- Heartbeat: PING / PONG at 500 ms intervals with RSSI capture.
- Link loss detection (3 missed pings).
- Base: boots, waits for link, responds to pings, RGB LED shows BOOT → IDLE → LINK_LOST.
- Remote: boots, sends link requests, sends pings, tracks RSSI, detects link loss, RGB LED shows BOOT → LINKING → IDLE → LINK_LOST.
- Unit tests for message serialisation, integrity CRC, sequence number validation.

**Test criteria:** Both units power on, link within 10 seconds, display stable RSSI, detect link loss within 1.5 seconds when separated, recover when returned to range.

#### Phase 2 — Input/Output and Debouncing

**Goal:** All hardware I/O is functional and debounced.

Deliverables:
- `rlc_common`: Shift-register debounce engine (generic, configurable polling rate).
- `rlc_common`: Battery voltage ADC driver (ADC1, median-of-burst plus 8-sample averaging, calibrated).
- `rlc_base`: GPIO configuration for all 8 channel SPDT relays, arm relay feedback sense (GPIO 21), key switch sense (GPIO 42), arm switch, siren. All outputs with configurable polarity.
- `rlc_base`: ADC1 configuration for battery voltage (GPIO 1) and 8 continuity inputs (GPIO 2–9).
- `rlc_base`: `relay_fire_set()`, `relay_fire_all_off()`, `relay_all_safe()` functions.
- `rlc_base`: Arm switch sense monitoring (single shared GPIO input from relay NO contact common node, §5.4.3).
- `rlc_base`: Continuity monitoring task (8 channels, ADC with 64-sample oversampling, 4-band classification with hysteresis).
- `rlc_base`: Arm switch monitoring (debounced).
- `rlc_base`: Battery monitoring with threshold detection.
- `rlc_base`: STATUS_UPDATE message generation (periodic + event-driven) with `continuity_bands` field.
- `rlc_remote`: Rotary encoder driver (interrupt-driven, channel 1–8 wrapping).
- `rlc_remote`: Fire button driver (debounced, fresh-press detection).
- `rlc_remote`: Arm switch monitoring (debounced).
- `rlc_remote`: Battery monitoring.
- `rlc_remote`: Buzzer pattern player task.
- Unit tests for debounce engine, battery threshold logic, continuity band classification.

**Test criteria:** Base correctly classifies continuity on all 8 channels using known resistor values (0 Ω, 2 Ω, 100 Ω, open). Arm switch state and battery voltage read correctly. Remote encoder selects channels 1–8, fire button and arm switch debounce correctly. Buzzer plays patterns. Status updates arrive at remote with correct continuity bands and bitmasks.

#### Phase 3 — State Machines and Command Processing

**Goal:** Complete arming and firing sequence works end-to-end.

Deliverables:
- `rlc_base`: Full base state machine (BOOT → IDLE → ARMED → PRE_FIRE → FIRING → POST_FIRE, plus LINK_LOST and ERROR).
- `rlc_base`: Command handler (CMD_ARM, CMD_DISARM, CMD_FIRE, CMD_CEASE_FIRE) with all guard conditions, ACK/NACK responses.
- `rlc_base`: Siren control (continuous from ARMED through PRE_FIRE and FIRING; patterned only for LINK_LOST and ERROR).
- `rlc_base`: Fire pulse via hardware timer.
- `rlc_remote`: Full remote state machine (BOOT → LINKING → IDLE → ARMED → FIRING, plus LINK_LOST and ERROR).
- `rlc_remote`: Command sender with ACK/NACK timeout handling and retry logic.
- `rlc_remote`: Repeated CMD_FIRE transmission at 200 ms while fire button held.
- `rlc_remote`: Dead-man switch logic (CMD_FIRE authorization timeout on base).
- All safety interlocks from §9.
- Integration tests: full arm → fire → auto-disarm sequence.

**Test criteria:** Complete fire sequence with LED or resistor load on channel output. All disarm triggers work (switch, command, link loss, continuity → OPEN, battery). NACK reasons displayed correctly. Channel change while armed triggers disarm.

#### Phase 4 — Display

**Goal:** Remote has a fully functional LCD display.

Deliverables:
- `rlc_remote`: ILI9488 SPI display driver.
- `rlc_remote`: Screen layout manager with all screens: Splash, Main Status (IDLE), Armed, Firing/Pre-Fire, Link Lost, Error.
- `rlc_remote`: Partial refresh (dirty-rectangle) for dynamic elements.
- `rlc_remote`: RSSI bar, battery voltage bars, continuity grid, channel selector highlight, status text, instruction prompts.
- `rlc_remote`: NACK reason display (human-readable text).
- `rlc_remote`: Firmware version mismatch screen.

**Test criteria:** All screens render correctly. Display updates at ≥ 5 Hz for dynamic elements. State transitions trigger full screen redraws. NACK reasons are readable.

#### Phase 5 — Hardening and Final Testing

**Goal:** System is robust and ready for field use.

Deliverables:
- Complete test suite execution (§15).
- Watchdog stress testing.
- Range testing (10 m, 50 m, 100 m, 200 m).
- Power consumption measurement.
- Edge case testing (rapid switch toggling, button mashing, power cycling under load).
- Documentation: build instructions, flash procedure, wiring diagram.
- Final version number set.

**Test criteria:** All tests in §15 pass. System operates reliably at 100 m LOS.

### 4.4 Sub-Agent Usage

The developer shall use sub-agents (Claude Code sub-agents or equivalent) as much as possible to parallelise work and maintain separation of concerns. Recommended sub-agent decomposition:

- **Protocol sub-agent**: implements `rlc_common` message structs, serialisation, integrity CRC, sequence numbers. Writes unit tests.
- **HAL sub-agent**: implements GPIO configuration, debounce engine, ADC driver, relay control functions, SPI display driver. Configurable polarity.
- **State machine sub-agent**: implements base and remote FSMs, transition logic, guard conditions.
- **Display sub-agent**: implements ILI9488 driver, screen layouts, partial refresh engine.
- **Comms sub-agent**: implements ESP-NOW wrapper, link establishment, heartbeat, RSSI tracking, command send/receive with ACK/NACK.
- **Test sub-agent**: writes unit tests and integration test harnesses.
- **Code review agent**: reviews written code and looks for bugs, issues, inconsistencies, gaps, and deviations from this specification. Produces `.md` review reports that the other agents use to fix their code.

Each sub-agent should produce code that is independently compilable and testable where possible.

### 4.5 Testability Requirements

The code shall be designed for testability:

- The `rlc_common` component shall compile on a host machine (ESP-IDF CMake host build or plain GCC) for unit testing without hardware. This requires abstracting hardware calls behind a HAL interface that can be mocked.
- All state machine transitions shall be testable by injecting events programmatically.
- The protocol layer shall be testable by feeding raw byte buffers and verifying parsed output.
- The debounce engine shall be testable by providing a sequence of simulated readings and verifying output.
- Integration tests shall use a two-unit bench setup with LEDs or resistors substituting for igniters, and jumper wires for continuity simulation.

### 4.6 Code Reusability

The firmware shall be written with reusability in mind so that individual components can be reused in future projects without modification or with minimal configuration changes. This is a mandatory architectural requirement, not an optional quality goal.

**General rules:**

- All modules in `rlc_common` (and sub-modules in `rlc_base` and `rlc_remote` where appropriate) shall be implemented as **generic, self-contained libraries** with well-defined public APIs. No module shall contain hard-coded project-specific values in its core logic — all project-specific parameters (GPIO numbers, thresholds, timing, buffer sizes) shall be passed in via configuration structs or `#define` constants at the call site.
- Each library module shall have a single, clearly defined responsibility (single-responsibility principle). A module that handles ADC sampling shall not also handle display rendering; a debounce engine shall not embed knowledge of which GPIO it is debouncing.
- Hardware abstraction layers (HALs) shall separate hardware-independent logic from platform-specific register access. Drivers shall expose abstract interfaces (e.g., `adc_read_mv(channel)` not `adc1_get_raw()`) so that the same logic can be retargeted to different hardware without rewriting.
- No component shall directly access the internal state of another component. All inter-component communication shall go through well-defined public API functions. Internal data structures shall be opaque to callers (declared in `.c` files, exposed only as `typedef struct foo_s *foo_handle_t` in headers where appropriate).
- Shared utility modules (debounce engine, battery ADC driver, RGB LED driver, logging, watchdog, version management) shall be completely decoupled from the RLC application. They shall not `#include` any RLC-specific headers other than common type definitions. This enables dropping them into a new ESP-IDF project with zero changes.
- Configuration shall be centralised. All tuneable parameters (timeouts, thresholds, queue depths, stack sizes, pin assignments) shall be defined in dedicated configuration headers or Kconfig symbols, not scattered across source files. This makes it straightforward to adapt the firmware to a different board or application.

**Naming and organisation:**

- Library modules shall use a consistent prefix (e.g., `debounce_`, `adc_`, `relay_`, `led_`, `buzzer_`) to avoid namespace collisions when reused.
- Each module shall reside in its own directory with a `CMakeLists.txt`, an `include/` directory for public headers, and a `.c` source directory. This follows the ESP-IDF component model and allows independent compilation and reuse.

### 4.7 RTOS Architecture Requirements

The firmware shall make full and correct use of FreeRTOS (bundled with ESP-IDF) as the underlying real-time operating system. All concurrency, scheduling, and inter-task communication shall follow FreeRTOS best practices. Special care shall be taken to avoid race conditions, priority inversion, deadlocks, and starvation.

**Task design:**

- The system shall use FreeRTOS tasks as defined in §9.10 (task priority table). Each task shall have a single, well-defined responsibility and run an infinite loop that blocks on a FreeRTOS primitive when idle (no busy-wait loops).
- Tasks shall not poll or spin-wait. Idle tasks shall block on `xQueueReceive()`, `xSemaphoreTake()`, `ulTaskNotifyTake()`, `vTaskDelay()`, or event-group waits.
- Task stack sizes shall be sized appropriately (see §9.10) and shall be validated using the `uxTaskGetStackHighWaterMark()` API during development. A stack watermark below 20% of the allocated size SHALL be flagged for review.

**Inter-task communication:**

- All data passed between tasks SHALL go through FreeRTOS primitives. The choice of primitive depends on the use case:
  - **Queues (`xQueueSend` / `xQueueReceive`):** for passing data items (messages, events with payloads, sensor readings) from one task to another. The ESP-NOW receive callback SHALL post to a queue (depth ≥ 16) as specified in §6.4.
  - **Task notifications (`xTaskNotifyGive` / `ulTaskNotifyTake`, `xTaskNotify` / `xTaskNotifyWait`):** for lightweight, single-bit signalling where no data payload is needed (e.g., timer ISR signalling the state machine task). Task notifications SHALL NOT be used to pass data values between tasks — use queues for that.
  - **Binary semaphores (`xSemaphoreCreateBinary`):** for synchronisation between an ISR and a task (e.g., "data ready" signal from a peripheral interrupt).
  - **Mutexes (`xSemaphoreCreateMutex`):** for protecting shared resources accessed by multiple tasks. A mutex SHALL be held for the minimum time necessary — acquire, perform the critical section, release immediately. No blocking calls (queue sends, delays) shall be made while holding a mutex.
  - **Counting semaphores:** for managing limited resource pools (e.g., available ADC channels, display buffer slots).
- Direct global variable sharing between tasks without synchronisation is **prohibited**. If a variable is written by one task and read by another, it SHALL be protected by a mutex or accessed exclusively through a queue or task notification.

**Shared state and race conditions:**

- Any state that is accessed by more than one task (e.g., the current FSM state, channel configuration, link status) SHALL be protected by a mutex or accessed only through a single "owner" task that serialises access via a command queue. The preferred pattern is the **single-owner model**: one task owns a data structure and other tasks send commands to it via a queue, eliminating the need for mutexes entirely.
- When the single-owner model is not practical, mutexes SHALL be used with the following rules:
  - Mutexes SHALL be taken before reading or writing shared state and released immediately after.
  - Mutexes SHALL NOT be nested (no taking a second mutex while holding the first) unless a strict lock ordering is documented and followed. Lock ordering violations are a fatal bug.
  - `xSemaphoreTake()` with a timeout SHALL be used for all non-ISR mutex acquisitions. Acquiring without a timeout risks deadlock.
- Atomic operations (`portENTER_CRITICAL` / `portEXIT_CRITICAL`, or `stdatomic.h` atomics) SHALL be used for simple flag variables shared between an ISR and a task where a full mutex is excessive. Critical sections SHALL be kept as short as possible (a few instructions at most).
- The dead-man timestamp update in the ESP-NOW receive callback (§6.4) is the only permitted exception to the "no bare globals in ISRs" rule, as it is a single aligned 32-bit write which is atomic on ESP32-S3.

**ISR safety:**

- All hardware timer callbacks and interrupt service routines SHALL use only ISR-safe FreeRTOS API variants (see §9.11): `xTaskNotifyFromISR()`, `xQueueSendFromISR()`, `xSemaphoreGiveFromISR()`. No mutexes, blocking calls, or direct function calls to state machine logic are permitted in ISR context.
- ISRs SHALL perform the minimum work necessary: read the interrupt source, clear the interrupt, and signal a task via a queue send or task notification. All processing shall happen in task context.
- ISR-to-task data transfer SHALL use `xQueueSendFromISR()` with a dedicated queue, never shared with task-to-task queue traffic on the same queue handle (to avoid ISR and task both calling send on the same queue, which can cause internal corruption even with ISR-safe variants).

**Priority inversion prevention:**

- Mutexes protecting shared resources accessed by tasks of different priorities SHALL use the **inheritance** protocol (enabled by passing `xSemaphoreCreateMutex()` — ESP-IDF enables priority inheritance by default). Priority inheritance SHALL NOT be disabled.
- Priority inversion scenarios SHALL be considered during design review. If a low-priority task (e.g., display) and a high-priority task (e.g., state machine) both access a shared resource, the mutex with priority inheritance ensures the low-priority task is temporarily boosted while holding the mutex.

**Watchdog and liveness:**

- Each FreeRTOS task SHALL subscribe to the ESP-IDF Task Watchdog Timer (TWDT) as specified in §9.10. The TWDT ensures that no task silently stalls.
- Tasks that block on FreeRTOS primitives with timeouts (queues, semaphores) SHALL reset the watchdog after each successful receive, or handle the timeout case (e.g., log a warning, transition to a safe state).

**Prohibited patterns:**

- No `vTaskDelay()` loops that act as polling (e.g., `while(1) { check_something(); vTaskDelay(10); }` when the event could be signal-driven). Replace with blocking on a queue, semaphore, or task notification.
- No disabling of the scheduler (`vTaskSuspendAll()` / `xTaskResumeAll()`) except in rare, well-documented cases where the critical section is too long for `portENTER_CRITICAL()` but too short to warrant a mutex.
- No direct task-to-task function calls that bypass FreeRTOS primitives (e.g., calling one task's handler function directly from another task). This breaks scheduling guarantees and makes deadlock analysis impossible.

---

## 5. Hardware Interface Specification

### 5.1 Development Board

Both units use identical boards: **ESP32-S3-DevKitC-1 with ESP32-S3-WROOM-1 N16R8** (16 MB Quad SPI Flash, 8 MB Octal SPI PSRAM).

Key constraints of this board:

| Constraint | Pins affected | Reason |
|---|---|---|
| Strapping pins — do not use | GPIO 0, 3, 45, 46 | Boot mode selection, JTAG |
| Octal PSRAM — not available | GPIO 33, 34, 35, 36, 37 | Internal SPI bus for PSRAM |
| USB — reserved | GPIO 19, 20 | USB D+/D- for programming/debug |
| UART0 — reserved | GPIO 43, 44 | Serial debug/programming via USB-to-UART bridge |
| On-board RGB LED | GPIO 48 | WS2812 — drives the 8-pixel igniter strip; on-board LED mirrors pixel 0 |

**Available GPIOs (24 general-purpose + GPIO48 for RGB LED):**
GPIO 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47

**Note:** GPIO 3 is technically usable after boot but is a strapping pin (JTAG select) and its state during reset affects debugging. It is deliberately excluded from the available list. See Appendix C.1 for the assignment of GPIO 10 in its place.

**ADC constraint:** ESP-NOW uses the Wi-Fi subsystem. On ESP32-S3, ADC2 (GPIO 11–20) is unreliable when Wi-Fi is active. All ADC readings **must** use ADC1 pins (GPIO 1–10).

### 5.2 Configurable GPIO Polarity

For all digital outputs (relays, siren, buzzer), whether the active state is HIGH or LOW shall be configurable as a compile-time constant in `pin_config.h`. Changing polarity requires only adjusting the polarity constants in `pin_config.h` and recompiling — no changes to logic code are needed. Example:

```c
#define PIN_RELAY_CH1          11
#define PIN_RELAY_CH1_ACTIVE   1    // 1 = active HIGH (IRLZ44N: HIGH = MOSFET on = relay energised)

#define PIN_SIREN              40
#define PIN_SIREN_ACTIVE       1    // 1 = active HIGH (IRLZ44N: HIGH = MOSFET on = siren on)
```

The relay and siren driver functions shall use these polarity constants to translate logical state (on/off) to physical GPIO level. With the IRLZ44N low-side MOSFET driver, all outputs are active HIGH (GPIO HIGH → MOSFET on → relay coil sinks to GND → coil energised). The polarity constants are retained for code portability in case a different driver topology is used in future.

### 5.3 Debounce Engine

All digital inputs (except the rotary encoder A/B pins) shall use a shift-register debounce method:

- A 16-bit variable is maintained per input.
- At each polling interval, the current GPIO reading (0 or 1) is shifted into the LSB of the variable, and the MSB is shifted out.
- The input is considered **stably LOW** (active) when the variable equals `0x0000` (16 consecutive LOW readings).
- The input is considered **stably HIGH** (inactive) when the variable equals `0xFFFF` (16 consecutive HIGH readings).
- Any other value means the input is in transition — the previous stable state is retained.

**Two register widths are used:**

| Input type | Register width | Polling interval | Debounce time | Stable values |
|---|---|---|---|---|
| **Fire button** | **8-bit** | 10 ms | **80 ms** | 0x00 = pressed, 0xFF = released |
| Base arm switch (via sense circuit §5.4.3) | 16-bit | 10 ms | 160 ms | 0xFFFF = arm relay closed (HIGH, VBAT on fire bus), 0x0000 = arm relay open (LOW) |
| Base key switch (via sense circuit §5.4.3b) | 16-bit | 10 ms | 160 ms | 0xFFFF = key ON (HIGH, VBAT at coil+), 0x0000 = key OFF (LOW) |
| Remote arm switch | 16-bit | 10 ms | 160 ms | 0x0000 = armed, 0xFFFF = disarmed |
| Encoder push button | 16-bit | 10 ms | 160 ms | 0x0000 = pressed, 0xFFFF = released |

**Note:** The base arm relay feedback sense circuit (§5.4.3) reads HIGH when VBAT is on the fire bus (arm relay closed), and the base key switch sense circuit (§5.4.3b) reads HIGH when the key switch is ON. Both are opposite polarity from the remote arm switch (active LOW with pull-up). The debounce engine handles both polarities — the stable values are configured per input.

**Note:** Continuity inputs use ADC sampling with multi-sample averaging and hysteresis-based band classification (see §5.4.2), not the shift-register debounce engine.

The fire button uses an 8-bit register (80 ms debounce) to minimise latency on release detection, which is safety-critical for the dead-man switch function. All other inputs use 16-bit registers (160 ms debounce).

The rotary encoder A/B pins remain interrupt-driven with an `ENC_LOCKOUT_US` (2 ms) lockout (shift-register debounce is not suitable for quadrature decoding). A cycle-position quadrature decoder is used, which gives consistent direction on every transition for half-step encoders (e.g. KY-040), unlike Gray code lookups which alternate direction. The encoder push button uses the shift-register method at 10 ms polling.

The debounce engine shall be implemented as a generic, reusable module in `rlc_common` that accepts a GPIO number, polling interval, register width (8-bit or 16-bit), and callback for state changes.

#### 5.3.1 Initial-state contract (added v1.31)

The shift register is seeded all-HIGH at init, so the first stable reading is a *determination*, not a transition. The engine therefore behaves as follows, and both halves are load-bearing:

1. **The first stable reading SHALL set the debounced state but SHALL NOT invoke the change callback.**
   This is required by the fire button's fresh-press interlock (§5.5.3): a button already held down at power-on must never produce a press event, only a release followed by a genuine press.

2. **The first stable reading SHALL be observable by polling** (`rlc_debounce_get_state()` / `rlc_debounce_is_stable()`).
   Consumers that maintain their own cached copy of the input state — the remote arm switch (§5.5.2), and the base arm sense and key sense (§5.4.3, §5.4.3b) — **SHALL adopt the debounced state by polling on every cycle**, not from the callback alone.

Rule 2 exists because omitting it is a real, shipped defect (review finding N1, firmware 1.1.0): the remote's arm switch cached its state only from the callback, so a key already turned to ARM at power-up left `arm_switch_is_armed()` false indefinitely. Every long-press was refused with "TURN ARM KEY FIRST", the arm indicator LED stayed dark, and the only recovery was to physically toggle the key off and back on. The failure is safe (arming refused, never granted) but total, and it appears in the ordinary operator flow of turning the key before powering up, or power-cycling a remote left in ARM.

Host tests **T-D01…T-D06** (§15.5) pin both rules; breaking either one breaks one of the two inputs.

### 5.4 Base Unit I/O

All GPIO pin numbers and polarities shall be defined in `pin_config.h`.

#### 5.4.1 Igniter Channel Outputs (8×)

| Parameter | Value |
|---|---|
| Signal type | Digital output, active HIGH (GPIO HIGH = MOSFET on = relay coil energised) |
| Quantity | 8 (channels 1–8) |
| Relay type | 12V automotive SPDT relay, 20A contact rating (e.g., typical 5-pin auto relay). Coil resistance ~80–150 Ω, coil current ~80–150 mA at 12V. |
| Driver | 10× IRLZ44N logic-level N-channel MOSFETs in low-side switch configuration (see §5.4.10). One MOSFET per relay coil (8 channels + 1 siren + 1 arm relay). |
| Default state at boot | Inactive (relay de-energised, NC position — igniter routed to continuity sense circuit) |
| Drive requirement | 3.3 V logic level into MOSFET gate via 150 Ω series resistor. 10 kΩ gate pull-down to GND ensures MOSFET is OFF when GPIO is high-impedance at boot. |

Each output, when driven active, energises an SPDT relay that switches the igniter from the continuity sensing circuit (NC) to the fire path (NO). The NO contact is connected to the ARM SENSE node (arm relay COM output, which carries VBAT when the arm relay is energised). The COM contact is connected to the igniter high-side. The igniter low-side is connected directly to ground. The output must be held active for the configured fire pulse duration, then returned to inactive (relay returns to NC position).

**Relay contact assignment:**
- **NC (normally closed):** connected to the continuity sense circuit (§5.4.2)
- **NO (normally open):** connected to ARM SENSE node (VBAT via arm relay, §5.4.9)
- **COM (common):** connected to the igniter high-side terminal

#### 5.4.2 Igniter Continuity Inputs (8×)

| Parameter | Value |
|---|---|
| Signal type | Analogue input (ADC1) |
| Quantity | 8 (one per channel, mapped 1:1 to channel outputs) |
| Pins | GPIO 2, 10, 4, 5, 6, 7, 8, 9 (ADC1_CH1, ADC1_CH9, ADC1_CH3 through ADC1_CH8). GPIO 10 replaces GPIO 3 (strapping pin). |
| ADC resolution | 12-bit |
| Sampling interval | 100 ms per channel in round-robin sequence (ch1, ch2, ..., ch8, ch1, ...). Each sample consists of 64 rapid consecutive ADC readings averaged (burst sampling). Full-system update period: 800 ms. |
| Oversampling | 64-sample moving average per channel for noise reduction |
| Conversion | `adc_cali_raw_to_voltage()` calibration API (same as battery ADC, §5.4.7) |

The continuity sensing circuit is external and **uses the SPDT relay's NC (normally closed) contact**. When the relay is de-energised (default state), the NC contact routes the igniter high-side to the continuity sense circuit. The igniter low-side is connected directly to ground, completing the sense current path. This means continuity sensing is inherently mutually exclusive with firing — the relay physically disconnects the sense circuit when energised for firing, and reconnects it when de-energised. Each channel has an independent sensing circuit:

```
3.3V ── R_ref1 (1.5kΩ, fusible) ── R_ref2 (1.8kΩ, fusible) ──┬── RELAY NC CONTACT ── (COM) ── igniter high-side
                                                                │                                      │
                                                                ├── R_pull (100kΩ) ── GND              igniter low-side
                                                                │                                      │
                                                                └── ADC pin (GPIO 2, 10, 4–9)         GND
```

The current-limiting resistors R_ref1 + R_ref2 (1.5 kΩ + 1.8 kΩ = 3.3 kΩ total) ensure the test current through the igniter does not exceed **≤ 1 mA** at 3.3V (I_max = 3.3V / 3.3kΩ = 1.0 mA). This provides a massive safety margin against all commercial igniter types (most e-matches have no-fire thresholds around 50 mA).

**Safety requirements for R_ref:** Both R_ref1 and R_ref2 SHALL be fusible or flame-retardant type resistors. Two resistors are used in series as defence-in-depth: if one resistor fails short (0 Ω), the remaining resistor still limits current to a safe level. Worst case: R_ref2 (1.8 kΩ) fails short, leaving 1.5 kΩ in series — current through a 2 Ω e-match would be 3.3V / (1.5kΩ + 2Ω) ≈ 2.2 mA, still far below the ~50 mA no-fire threshold. Both resistors failing short simultaneously is an extremely unlikely double-fault condition.

**Note:** Because the SPDT relay NC contact is used for continuity sensing, the continuity circuit is naturally isolated from the fire path. When the relay is de-energised, the igniter is connected to the sense circuit via NC. When the relay is energised for firing, the NC contact disconnects and the igniter is connected to the fire path via NO. There is no state in which the continuity sense voltage and battery firing voltage can simultaneously reach the igniter. No additional MOSFET switch is required to disable continuity sensing during firing — the relay provides this isolation mechanically.

**Continuity during FIRING state:** When a channel relay is energised for firing, the NC contact for that channel is physically disconnected. The ADC pin for that channel sees only the R_pull voltage divider (pulled towards 3.19V) and will read OPEN. This is expected and does not indicate a fault. After the fire pulse completes and the relay returns to NC, the continuity sense circuit automatically reconnects to the igniter and resumes meaningful readings.

The 100 kΩ pull-down resistor (R_pull) serves a critical function: **without it, an open circuit leaves the ADC pin floating**, producing undefined readings. With R_pull, an open circuit reads a defined voltage of 3.3 × 100k / (3.3k + 100k) ≈ **3.19V**, which is clearly distinguishable from any connected igniter. The pull-down draws negligible current (33 µA) and does not materially affect readings when an igniter is connected, since R_pull (100 kΩ) is vastly larger than any igniter resistance.

**Component count per channel:** 3 resistors (R_ref1 1.5 kΩ fusible + R_ref2 1.8 kΩ fusible + R_pull 100 kΩ). **Total additional components: 24 continuity resistors** (8× R_ref1 1.5 kΩ fusible + 8× R_ref2 1.8 kΩ fusible + 8× R_pull 100 kΩ) + **3 arm sense components** (1× 27 kΩ series + 1× 10 kΩ divider + 1× 3.3 V zener) + **3 key sense components** (1× 27 kΩ series + 1× 10 kΩ divider + 1× 3.3 V zener) + **9× 12V automotive SPDT relays** (8 channel + 1 arm relay) + **3 feedback LEDs** (1 green + 2 red) + **3 LED series resistors** + **10× IRLZ44N MOSFET driver circuits** (10 MOSFETs + 10 gate resistors + 10 pull-downs + 10 flyback diodes, see §5.4.10).

##### Continuity Band Classification

The ADC voltage is converted to a continuity band using threshold comparison with hysteresis. The V_adc values below assume R_ref = R_ref1 + R_ref2 = 3.3 kΩ, R_sense = 217 Ω (in the sense current path, §5.4.9), R_pull = 100 kΩ, V_supply = 3.3 V:

**THREE bands (revised 2026-08-21).** `CONT_SHORT` was merged into `CONT_CONNECTED`; see the deviation note below.

| Band | Enum | R_ign range | V_adc range | Meaning | Display | Arming |
|---|---|---|---|---|---|---|
| `CONT_OPEN` | 0 | > ~500 Ω / ∞ | > `CONT_OPEN_UV` (586000 µV) | No igniter connected or broken wire. Pulled to ~3.19 V by R_pull, which saturates the 0 dB ADC range. | Yellow ○ | **Blocks arming** (NACK 0x04) |
| `CONT_CONNECTED` | 1 | < ~67 Ω | < `CONT_MARGINAL_UV` (261000 µV) | **A low-resistance path is present.** Deliberately does *not* assert the igniter is good — a dead short across the terminals is in this band and is indistinguishable from a healthy e-match. | Dark green ● | Arming permitted |
| `CONT_MARGINAL` | 2 | ~67–500 Ω | `CONT_MARGINAL_UV` to `CONT_OPEN_UV` | High resistance — corroded clips, loose contact, damaged leads. May fail to fire. | Light green ▲ + "MARGINAL" | Warning, does not block arming |
| ~~`CONT_SHORT`~~ | 3 | — | — | **DEPRECATED, never produced.** Value retained so the 2-bit wire encoding is unchanged and a pre-merge peer still decodes; folded into CONNECTED on display. | — | — |

> **Why SHORT was removed (bench evidence, 2026-08-21).** At the specified 1 mA test current a dead short and a 1.5–1.9 Ω igniter differ by only 1–1.6 mV — the same magnitude as ADC noise, run-to-run drift, and the contact resistance of the shorting lead itself. Three controlled experiments on one physically fixed igniter returned implied resistances of **0.77, 1.15 and 1.77 Ω**, and the measured separation varied 2.9–6.8 counts for a setup that never changed. A midway threshold misclassifies ~19 % of single readings. Adding a 220 Ω sense-branch offset resistor was trialled: it fixed the ADC's low-end non-linearity but, as predicted, could not add resolution, and was removed. Raising the test current to ~3.4 mA would make the band measurable but costs the no-fire margin (50× → 15×), which was judged not worth it for a purely informational band. **A band that cannot be measured must not be reported.**

> **Naming.** The band is `CONNECTED`, not `GOOD`. "GOOD" would assert igniter health that the measurement cannot support, and would be actively wrong on a shorted pair of leads.

**Wire encoding alignment:** the enum values (CONT_OPEN=0, CONT_CONNECTED=1, CONT_MARGINAL=2, CONT_SHORT=3 deprecated) are identical to the 2-bit wire encoding in the `continuity_bands` field of STATUS_UPDATE. No mapping function is required. The encoding is **unchanged** by the three-band merge — value 3 is simply no longer emitted — so no protocol version bump was required.

**Hysteresis:** each threshold has a configurable hysteresis band (default: ±200 µV for SHORT boundary, ±5000 µV for MARGINAL boundary, ±50000 µV for OPEN boundary). A band transition is only registered when the averaged voltage crosses the threshold + hysteresis in the transition direction. This prevents oscillation at band boundaries.

**SHORT band reliability (DEPRECATED with the SHORT band, v1.29):** the SHORT/GOOD boundary at ~500 µV is near the ESP32-S3 ADC noise floor, and this analysis predates the merge — the bands are now CONNECTED, MARGINAL, OPEN. Retained as a record of why the distinction was abandoned (see the bench-evidence note above).

**Implication:** continuity readings are valid and meaningful whenever the channel relay is de-energised (NC position), regardless of arm switch position. The display always shows which igniters are connected and their connection quality. During FIRING (relay energised), the fired channel reads OPEN (expected — NC disconnected); all other channels remain readable.

#### 5.4.3 Arm Relay Feedback Sense Input (GPIO 21)

| Parameter | Value |
|---|---|
| Signal type | Digital input (GPIO) with voltage divider and zener clamp |
| Quantity | 1 (senses ARM SENSE node — the arm relay COM output / fire bus) |
| Pin | GPIO 21 |
| Function | Post-energise verification that the arm relay contacts are actually closed and VBAT is present on the fire bus. Used for the M1 arm relay verify flow and contact-welding detection (§5.4.3b provides separate key switch position sensing). |
| Protection | Voltage divider (27 kΩ / 10 kΩ) followed by 3.3 V zener diode clamp — **zener NOT FITTED as-built 2026-08-17, see deviation note below** |
| Debounce | 16-bit shift-register, 10 ms polling, 160 ms debounce (same engine as other digital inputs, §5.3) |

The arm sense circuit reads the ARM SENSE node, which is the arm relay COM output — i.e., the fire bus (§5.4.9). This node carries VBAT whenever the arm relay contacts close (NO→COM). The arm relay coil drive path (§5.4.4) requires both the physical key switch ON and the MOSFET driven, so arm sense HIGH implies both conditions are met. However, the firmware also has a **separate** key switch sense input (§5.4.3b, GPIO 42) that reads the key switch position directly, enabling the firmware to distinguish between key switch OFF and arm relay dropout.

The ARM SENSE node connects to GPIO 21 through a voltage divider (27 kΩ series, 10 kΩ to GND) with a 3.3 V zener diode clamp across the lower resistor.

> **AS-BUILT DEVIATION (2026-08-17) — the zener clamp is NOT fitted.** On the
> current base hardware, GPIO 21 (§5.4.3) and GPIO 42 (§5.4.3b) have the
> 27 kΩ/10 kΩ divider only; neither 3.3 V zener is installed, and the arm relay
> contact has no snubber. This matters because the bug #18 relay-order fix made
> the arm relay the sole contact breaking 6 A DC, putting that arc on the ARM
> SENSE node. The 27 kΩ series resistor limits DC VBAT to ~0.33 mA into the pin's
> internal clamp (survivable), so the exposure is inductive spikes at contact
> break, plus contact erosion trending toward a welded arm relay. Required before
> an extended fire-test campaign: BAT54S dual Schottky (or the spec'd zener) on
> GPIO 21 and GPIO 42, a 47 Ω/100 nF snubber across the arm relay contact, and an
> SMBJ18A-class TVS from arm relay COM to GND. Firmware restricted firing to
> channel 1 via `FIRE_PROTECTED_CHANNEL_MASK` until 2026-08-23, when the mask was
> widened to 0xFF with the protection BOM complete on all eight channels. See
> `Development_Progress.md` bug #18.
>
> **UPDATE 2026-08-21 — continuity clamps complete, two gaps remain.** All eight
> continuity ADC pins now carry 2x 1N5819 (to GND and to +3V3). Still required:
> (a) a **220 Ω sense-branch resistor per channel** (§5.4.9), without which the
> 3V3-side clamp dumps an unlimited arc into the 3.3 V rail on any of eight
> channels — **DONE 2026-08-23, 217 Ω fitted on all eight; thresholds re-derived,
> see v1.32**; (b) an **active 3.3 V rail clamp** — a TL431 shunt programmed to
> ~3.57 V (2.495 V x (1 + 4k3/10k)), cathode to 3V3, anode to GND, REF to the
> divider mid-point, 10 nF REF-to-anode. A 3V6 zener is **not** an adequate
> substitute: its knee is soft, a +/-5% part sits anywhere in 3.42-3.78 V, and it
> needs ~3.9 V to sink 40 mA — above the ESP32-S3's 3.6 V absolute maximum.

**Reading interpretation:**
- **Arm relay de-energised:** ARM SENSE node is disconnected from VBAT (arm relay COM↔NC, NC unused). R2 (10 kΩ) pulls the GPIO to ~0 V. GPIO reads LOW (debounced: 0x0000 = arm relay open).
- **Arm relay energised:** VBAT appears on ARM SENSE node via arm relay NO→COM. The voltage divider reduces VBAT to a safe range. The 3.3 V zener clamps any voltage above 3.3 V. GPIO reads HIGH (debounced: 0xFFFF = arm relay closed, VBAT on fire bus).

**Voltage divider calculation** (R1 = 27 kΩ, R2 = 10 kΩ):

| VBAT | Divider output (V_div = VBAT × R2 / (R1 + R2)) | After zener | GPIO reads |
|---|---|---|---|
| 0 V (relay off) | 0 V | 0 V | LOW (relay open) |
| 9.0 V (min operating) | 2.43 V | 2.43 V | HIGH (relay closed) |
| 12.6 V (full charge) | 3.41 V | 3.3 V (clamped) | HIGH (relay closed) |

**Contact welding detection:** The arm sense input uses the same 16-bit shift-register debounce engine as all other digital inputs (fire button, arm switch, encoder — §5.3), providing a 160 ms stable reading. When the arm relay is known to be de-energised (software-tracked intended state = OFF, not a raw GPIO readback), the firmware periodically checks that the debounced arm sense reads LOW. If the debounced sense reads HIGH while the relay is intended OFF, this indicates the arm relay contacts have welded shut — a critical hardware fault. The firmware SHALL set `ERR_RELAY_FAULT` and enter ERROR state.

**Component count:** 3 components total: 1× 27 kΩ series resistor (R1), 1× 10 kΩ divider/pull-down resistor (R2), 1× 3.3 V zener diode.

#### 5.4.3b Key Switch Sense Input (GPIO 42)

| Parameter | Value |
|---|---|
| Signal type | Digital input (GPIO) with voltage divider and zener clamp |
| Quantity | 1 (senses key switch NO output — direct key switch position) |
| Pin | GPIO 42 |
| Function | Direct read of the physical key switch position. HIGH = key switch ON (VBAT present at key switch NO output). Used in `guard_arm()`, PRE_FIRE guard, and FSM disarm triggers. Enables the firmware to detect key switch position independently of arm relay state, resolving the circular dependency where arming requires the arm relay to already be closed. |
| Protection | Voltage divider (27 kΩ / 10 kΩ) followed by 3.3 V zener diode clamp (identical to arm sense) — **zener NOT FITTED as-built 2026-08-17, see §5.4.3 deviation note** |
| Debounce | 16-bit shift-register, 10 ms polling, 160 ms debounce (same engine as arm sense, §5.3) |

The key sense circuit reads the key switch NO output — the same node that feeds VBAT+ to the arm relay coil positive terminal (§5.4.4). When the key switch is ON, VBAT+ appears at this node. When OFF, the node is disconnected from VBAT+ and pulled low. The key switch NO output connects to GPIO 42 through a voltage divider (27 kΩ series, 10 kΩ to GND) with a 3.3 V zener diode clamp — identical to the arm sense circuit on GPIO 21.

**Reading interpretation:**
- **Key switch OFF:** Key switch COM↔NC. NO output is disconnected from VBAT+. R2 (10 kΩ) pulls GPIO to ~0 V. GPIO reads LOW.
- **Key switch ON:** Key switch COM↔NO. VBAT+ appears at the key switch NO output (same node as arm relay coil+). The voltage divider reduces VBAT to a safe range. GPIO reads HIGH.

**Why a separate GPIO is needed:** The arm relay feedback (GPIO 21, §5.4.3) can only read HIGH after the arm relay has been energised. The arming guard must verify the key switch is ON *before* energising the arm relay — otherwise arming creates a circular dependency (can't arm because arm relay isn't closed, can't close arm relay because can't verify arm conditions). GPIO 42 reads the key switch position directly, independently of arm relay state.

**Component count:** 3 components total: 1× 27 kΩ series resistor (R1), 1× 10 kΩ divider/pull-down resistor (R2), 1× 3.3 V zener diode.

#### 5.4.4 Manual Arm/Disarm Switch (Hardware Interlock)

| Parameter | Value |
|---|---|
| Type | SPDT key switch or toggle switch |
| Location | In the arm relay coil drive path (between VBAT+ and arm relay coil positive terminal) |
| Function | Hardware safety interlock — physically interrupts arm relay coil current when OFF, preventing the arm relay from energising regardless of software state |
| Current carried | Arm relay coil current only (~80–150 mA at 12 V). Does NOT carry igniter current. |
| Sensing | Two independent sense circuits: (1) Key switch sense (§5.4.3b, GPIO 42) reads the key switch NO output directly, providing key position independently of arm relay state. (2) Arm relay feedback (§5.4.3, GPIO 21) reads the arm relay COM output (fire bus), which requires both this switch and the software MOSFET to be active. |

The arm key switch is a physical SPDT switch on the base unit. It is wired in series between VBAT+ and the arm relay coil positive terminal, forming one input of a hardware AND gate with the IRLZ44N MOSFET (§5.4.9):

```
VBAT+ ── Key SW COM ──┬── Key SW NC ─── Green LED + R ── GND   (SAFE indicator)
                       │
                       └── Key SW NO ──┬── Arm Relay Coil(+) ── Coil(−) ── IRLZ44N ── GND
                                       │
                                  Red LED + R ── GND
                                  (KEY position indicator)
```

**Key switch sense (§5.4.3b):** The key switch NO output node (same node as arm relay coil positive terminal) also connects to GPIO 42 through an independent voltage divider (27 kΩ / 10 kΩ) with 3.3 V zener clamp. This provides direct key switch position sensing independently of arm relay state.

**Switch positions:**
- **OFF (COM → NC):** Arm relay coil disconnected from VBAT. No current can flow through the coil, regardless of MOSFET state. Green LED illuminated — indicates SAFE. Arm relay cannot energise.
- **ON (COM → NO):** VBAT connected to arm relay coil positive terminal. Arm relay CAN energise if the MOSFET is also driven by firmware (GPIO 47 HIGH). Red LED illuminated — indicates key is in ARM position.

The arm key switch and the IRLZ44N MOSFET form a **hardware AND gate**: both must be active (key ON AND software drive HIGH) for the arm relay to energise. Neither a software bug alone nor a physical switch malfunction alone can energise the arm relay.

**Visual feedback LEDs (passive — no GPIO required):**

| LED | Connected to | Colour | Meaning |
|---|---|---|---|
| Green LED + series R | Key switch NC contact to GND | Green | Key switch in SAFE/OFF position |
| Red LED + series R | Key switch NO contact to GND | Red | Key switch in ARM/ON position |
| Red LED + series R | Across arm relay coil terminals | Red | Arm relay coil energised (both key AND software active) |

All three LEDs are passive — they operate directly from VBAT through the switch/relay contacts and require no GPIO. They function correctly even if the ESP32-S3 is unpowered or has crashed.

> **AS-BUILT (2026-08-26): all three indicator LEDs verified correct after
> rework.** Two faults were found and fixed on the base: the coil red LED lit
> with the key in SAFE while the relay stayed de-energised (**bug #28**), and the
> key-position red and green LEDs lit *simultaneously* in SAFE. Both were sneak
> paths in the indicator wiring — a return that was not true GND — and neither
> touched the key switch's break of the coil circuit, so the hardware AND gate
> above was never compromised. The table's three meanings now hold as written.
>
> Because these LEDs are the operator's only unpowered-ESP32 view of the fire
> bus, **re-verify the AND gate at the node (not from the LEDs) after any
> indicator rework**: key SAFE + GPIO 47 driven → relay out, ARM SENSE 0; key ON
> + GPIO 47 low → relay out, ARM SENSE 0; key ON + GPIO 47 driven → relay in,
> ARM SENSE 1.

Both this switch AND the remote arm switch must be in the armed position for any channel to be armed.

#### 5.4.5 Circuit Topology

The following diagram shows the relationship between the arm key switch, arm relay, firing circuit, continuity sensing circuit, and SPDT channel relays:

```
    BATTERY +
        │
        ├── ARM RELAY DRIVE PATH:
        │   VBAT+ ── Key SW COM ── Key SW NO ── Arm Relay Coil(+) ── Coil(−) ── IRLZ44N ── GND
        │                                                       │
        │                                                 Flyback diode
        │                                                 Red LED + R (across coil)
        │
        ├── ARM RELAY (SPDT, 12V automotive, 20A):
        │   VBAT+ ── Arm Relay NO
        │            Arm Relay NC ── (unused)
        │            Arm Relay COM ── ARM SENSE node
        │
        │   RELAY DE-ENERGISED (default/safe state):
        │   ARM SENSE node disconnected from VBAT. No fire path.
        │   Continuity sense active on all channels.
        │
        │   RELAY ENERGISED (ARMED/PRE_FIRE/FIRING state):
        │   ARM SENSE node connected to VBAT. Fire path enabled.
        │
        ├── ARM SENSE NODE ───┬── CH1 Relay NO ── CH1 COM ── igniter ── GND
        │                     ├── CH2 Relay NO ── CH2 COM ── igniter ── GND
        │                     ├── ...
        │                     └── CH8 Relay NO ── CH8 COM ── igniter ── GND
        │
        │            ┌──────┴──────┐
        │            │  CHANNEL     │
        │            │  SPDT RELAY  │
        │            │   (1-8)      │
        │            │              │
        │      NO ───┤              ├─── NC
        │ (fire path)│     COM      │(continuity)
        │            └──────┬──────┘
        │                   │
        │              IGNITER HIGH-SIDE
        │                   │
        │              IGNITER LOW-SIDE
        │                   │
    BATTERY − ◄─────────────┘

    KEY SWITCH FEEDBACK:
    VBAT+ ── Key SW COM ──┬── Key SW NC ── Green LED + R ── GND  (SAFE indicator)
                          └── Key SW NO ── Red LED + R ── GND    (ARM position indicator)

    Key switch NO output also connects to GPIO 42 through voltage divider + zener (§5.4.3b).

    ARM RELAY COIL FEEDBACK:
    Red LED + series R across arm relay coil terminals (indicates relay energised)

    ARM SENSE CIRCUIT (GPIO 21) — arm relay contact feedback:
    ARM SENSE node ── 27 kΩ ──┬── GPIO 21 (arm relay feedback)
                               │
                          10 kΩ ── GND
                               │
                        3.3 V zener ── GND   [NOT FITTED as-built 2026-08-17]

    ARM RELAY CONTACT SNUBBER: [NOT FITTED as-built 2026-08-17]
    The bug #18 relay-order fix makes the arm relay the sole contact breaking
    6 A DC. Required: 47 Ω/100 nF RC across the contact + SMBJ18A-class TVS
    from arm relay COM to GND.

    CONTINUITY CIRCUIT (per channel, via channel relay NC contact):

    3.3V ── R_ref1 (1.5kΩ, fusible) ── R_ref2 (1.8kΩ, fusible) ──┬── [220Ω] ── CH RELAY NC ── (COM) ── igniter ── GND
                                                                    │
                                                                    ├── R_pull (100kΩ) ── GND
                                                                    │
                                                                    ├── 1N5819 ── GND          [FITTED all 8 ch, 2026-08-21]
                                                                    ├── 1N5819 ── +3.3V        [FITTED all 8 ch, 2026-08-21]
                                                                    │
                                                                    └── ADC1 pin (GPIO 2–9)

    [220Ω] = sense-branch series resistor, NOT FITTED as-built. Trialled on CH1 and removed
    (Development_Progress.md, 2026-08-21). Required as protection: it is the only thing limiting
    the current the 3V3-side clamp injects into the 3.3 V rail during a fault. Must sit between
    the relay NC contact and the pin/clamp junction — on the pin side of the clamps it protects
    nothing and adds a leakage offset instead. Changes CONT_MARGINAL_UV to ~264000 µV and
    CONT_OPEN_UV to ~588000 µV; recalibration required.

    Two series fusible resistors (1.5kΩ + 1.8kΩ = 3.3kΩ total) for defence-in-depth.
    No MOSFET switch required — SPDT relay NC/NO switching provides inherent isolation.
    Continuity always active when channel relay is de-energised (NC position).
    During FIRING, channel relay NC contact disconnects — ADC reads OPEN (expected).

    FIRE PATH (per channel):

    VBAT+ ── ARM RELAY NO ── ARM RELAY COM (ARM SENSE) ── CH RELAY NO ── CH COM ── igniter ── GND

    Two independent break points in the fire path:
    1. ARM RELAY (requires physical key switch ON AND software MOSFET drive — hardware AND gate)
    2. CHANNEL SPDT RELAY (software-controlled — must be energised to NO position)

    The arm relay coil requires BOTH the physical key switch closed AND the IRLZ44N
    MOSFET driven by firmware. Neither condition alone can energise the arm relay.
```

The arm relay provides the primary fire path interlock. Its coil drive requires both the physical key switch (hardware) and the IRLZ44N MOSFET (software), forming a hardware AND gate. The channel relay provides a second, independent break point. This gives three independent safety barriers:

1. **Physical key switch** — in the arm relay coil drive path. Cannot be actuated by software. Prevents arm relay from energising when OFF.
2. **Arm relay contacts** — in the fire path. Controlled by the AND gate of key switch + software MOSFET. Breaks fire path for all 8 channels simultaneously when de-energised.
3. **Channel relay contacts** — per-channel, in the fire path. Software-controlled. Breaks the fire path for one channel when de-energised.

The continuity circuit uses the channel SPDT relay NC contact for its sense path (unchanged from v1.12). R_ref = 3.3 kΩ limits the maximum current to 1 mA at 3.3V. R_pull = 100 kΩ provides a defined ADC reading for open circuits. Each of the 8 channels has an independent sensing circuit connected to a dedicated ADC1 pin.

The arm sense circuit (GPIO 21, §5.4.3) monitors the ARM SENSE node (fire bus), providing definitive feedback that VBAT is present on the fire path — verifying that the arm relay contacts are actually closed. The key sense circuit (GPIO 42, §5.4.3b) monitors the key switch position independently, enabling the firmware to detect key switch OFF before or after arm relay energisation.

#### 5.4.6 Post-Fire Igniter Status Detection

After a fire pulse completes and the channel relay returns to its NC position, the continuity sensing circuit automatically reconnects to the igniter. The arm relay is also de-energised on transition to POST_FIRE, breaking the fire path for all channels. The continuity task resumes meaningful ADC sampling for that channel (continuity sense uses its own 3.3V supply through R_ref, independent of the arm relay state). An igniter that has fired will read as OPEN (high resistance / open circuit). An igniter that failed to fire will still read as GOOD or MARGINAL.

This provides automatic post-fire status without any additional hardware — the SPDT relay's return to NC inherently reconnects the sense path.

**Relay dropout delay:** The first continuity reading after FIRING→POST_FIRE SHALL be delayed by at least 50 ms to allow for relay mechanical dropout time and contact settling.

#### 5.4.7 Battery Voltage Input

| Parameter | Value |
|---|---|
| Signal type | Analogue input (ADC1) |
| Quantity | 1 |
| Pin | GPIO 1 (ADC1_CH0) |
| Input range | 0–3.3 V (via external voltage divider from battery) |
| ADC resolution | 12-bit |
| Sampling interval | 1000 ms |
| Averaging | Median of a 33-sample burst per reading, then an 8-deep moving average (see §5.6.3) |
| Conversion | The firmware SHALL use the ESP-IDF v5.x ADC calibration API (`adc_cali_raw_to_voltage()`) for voltage conversion. This uses per-chip calibration data burned into eFuse at the factory. The calibrated millivolt reading is then multiplied by `DIVIDER_RATIO` to obtain the battery voltage. |

The DIVIDER_RATIO constant must be defined in configuration to match the external resistor divider.

**ADC1 allocation summary (base unit):** GPIO 1 = battery voltage, GPIO 2 = ch1 continuity, GPIO 10 = ch2 continuity, GPIO 4–9 = ch3–ch8 continuity. This uses all 10 available ADC1 pins (GPIO 1–10). No spare ADC1 pins remain. ADC2 (GPIO 11–20) remains unavailable due to ESP-NOW/Wi-Fi conflict.

#### 5.4.8 Siren Output (GPIO 40)

**Base unit only.** The siren is the *pad* warning — loud, next to the rocket,
on the unit that actually closes the fire path. The remote has a separate
buzzer (§5.5.7, GPIO 16) for operator feedback; the two are different devices
with different drivers and **opposite polarity**, which is easy to get
backwards when wiring both.

| Parameter | Value |
|---|---|
| GPIO | **40** (`PIN_SIREN`) |
| Unit | Base only — `PIN_SIREN` is defined inside `#ifdef CONFIG_RLC_UNIT_BASE` |
| Signal type | Digital output, active HIGH (GPIO HIGH = MOSFET on = siren activated) |
| Quantity | 1 |
| Function | Loud alarm: **continuous throughout ARMED, PRE_FIRE and FIRING**, pulsed during LINK_LOST (500/500, 4 cycles), 3 short blasts on ERROR |
| Driver | Via dedicated IRLZ44N MOSFET (same low-side switch topology as relay drivers, see §5.4.10) |
| Gate network | 150 Ω series (GPIO→gate) + **10 kΩ gate pull-down (gate→GND)** |
| Flyback diode | Required — cathode to VBAT+, anode to MOSFET drain (1N4007, or 1N5819/SS14) |

**The 10 kΩ gate pull-down is not optional on this output.** ESP32-S3 GPIOs are
high-impedance from power-on until `siren_init()` configures them. Without the
pull-down, capacitive coupling through Cgd can partially turn the MOSFET on
during the power-on transient — and a siren that sounds by itself at boot is
the one output where that behaviour is genuinely dangerous at a pad, because it
trains operators to disbelieve it.

**Note (GPIO 40 is also MTDO).** GPIO 39–42 are the ESP32-S3's pin-based JTAG
signals; GPIO 42 is already committed to key sense (§5.4.3b). The board uses
USB Serial/JTAG, so this is harmless — but pin-based JTAG debugging is not
available on this hardware.

> **AS-BUILT (2026-08-26): the siren driver IS FITTED.** IRLZ44N on GPIO 40 with
> the 150 Ω gate series resistor, the 10 kΩ gate pull-down, and a **1N5819**
> Schottky flyback diode across the siren (cathode VBAT+, anode drain).
> Supersedes the 2026-08-21 "NOT CONNECTED" note. Two items remain open:
>
> 1. ~~**Diode current rating.**~~ **CLOSED 2026-08-26: siren measured at under
>    200 mA steady**, so the 1 A 1N5819 has a 5x margin. The v1.35 removal of
>    the ARMED pulse also dropped this diode from switching at 1 Hz throughout
>    ARMED to once per sequence, retiring the repetitive-avalanche concern as
>    well. No diode change needed.
> 2. ~~**Retest not yet run.**~~ **COMPLETE 2026-08-26 — six checks, all PASS;
>    review finding N2 (§12.3) is closed by measurement.** Silent at power-on;
>    continuous across ARMED→PRE_FIRE; stops and stays stopped after all three
>    disarm routes; LINK_LOST = 4 cycles of 500/500; ERROR = 3 blasts at 200 ms;
>    and link recovery **mid-pattern** silences the siren immediately and
>    permanently. That last check is the one that matters going forward: N2's
>    stuck-on mode needed the infinite ARMED pulse, which v1.35 removed, so the
>    surviving risk lives in the still-periodic LINK_LOST and ERROR patterns.
>    See `Development_Progress.md`, bug #27.

#### 5.4.9 Arm Relay Output (GPIO 47)

| Parameter | Value |
|---|---|
| Signal type | Digital output, active HIGH (GPIO HIGH = MOSFET on = arm relay coil energised) |
| Quantity | 1 |
| Pin | GPIO 47 |
| Relay type | 12V automotive SPDT relay, 20A contact rating (same as channel relays) |
| Driver | Via IRLZ44N MOSFET in low-side switch configuration (see §5.4.10), with arm relay coil positive terminal connected through physical key switch (see §5.4.4) |
| Function | Primary fire path interlock. SPDT relay in series with all channel relay fire paths. NC contact = disconnected (safe), NO contact = VBAT+ connected to fire path. Provides a software-controllable break point that requires BOTH physical key switch ON AND software drive to energise (hardware AND gate). |
| Default state at boot | Inactive (relay de-energised, NC position — fire path broken) |

The arm relay is the primary fire path interlock, positioned between the battery and all 8 channel relay fire paths. When de-energised (default/safe state), the arm relay COM output (ARM SENSE node) is disconnected from VBAT, breaking the fire path for all channels simultaneously. When energised, VBAT is connected to the ARM SENSE node, enabling the fire path for whichever channel relay is subsequently energised.

**Hardware AND gate:** The arm relay coil drive path requires two independent conditions to be met simultaneously:
1. **Physical key switch ON** — VBAT connected to arm relay coil positive terminal via key switch COM→NO (§5.4.4)
2. **Software MOSFET drive** — GPIO 47 HIGH → IRLZ44N MOSFET on → arm relay coil negative terminal sinks to GND

Neither condition alone can energise the arm relay. A software bug cannot energise the arm relay without the physical key being turned. A physical key being turned cannot energise the arm relay without software actively driving GPIO 47 HIGH.

**Arm relay coil feedback:** A red LED with series resistor is connected across the arm relay coil terminals. This LED illuminates only when current flows through the coil (both key switch ON AND MOSFET driven), providing a passive visual indicator of the arm relay state.

**State machine control:**
- **ARMED, PRE_FIRE, FIRING:** arm relay energised (GPIO 47 HIGH)
- **IDLE, POST_FIRE, LINK_LOST, ERROR:** arm relay de-energised (GPIO 47 LOW)
- **Boot:** arm relay de-energised (GPIO 47 is high-impedance, 10 kΩ gate pull-down holds MOSFET off)

Two sense circuits monitor the arm relay system:
- **Arm relay feedback** (§5.4.3, GPIO 21): monitors the arm relay COM output (ARM SENSE node / fire bus), providing definitive feedback that the arm relay contacts are closed and VBAT is present on the fire path.
- **Key switch sense** (§5.4.3b, GPIO 42): reads the key switch NO output directly, detecting key switch position independently of arm relay state. Used in arming guards and FSM disarm triggers.

#### 5.4.10 IRLZ44N MOSFET Low-Side Drivers

| Parameter | Value |
|---|---|
| MOSFET type | IRLZ44N — N-channel logic-level power MOSFET (Vds = 55 V, Id = 47 A, Rds(on) ≈ 22 mΩ @ Vgs = 5 V) |
| Quantity | 10 (one per relay coil × 8 channels + 1 siren + 1 arm relay) |
| Configuration | Low-side switch: drain to relay coil low-side, source to GND, gate from ESP32-S3 GPIO |
| Gate series resistor (R_g) | 150 Ω per MOSFET (standard E24 value). Limits peak gate inrush to 22 mA (3.3V / 150Ω), damps gate ringing from lead inductance. Turn-on time ≈ 270 ns (R_g × Ciss), negligible for relay switching. |
| Gate pull-down resistor (R_pd) | 10 kΩ per MOSFET, gate to GND. Ensures MOSFET is OFF when GPIO is high-impedance at boot. **Critical for boot safety.** |
| Flyback diode | 1 per relay coil (1N4007 or Schottky e.g. 1N5819/SS14), cathode to VBAT+, anode to drain. Clamps inductive kick on coil de-energisation. |
| Rds(on) at 3.3 V Vgs | ~30–40 mΩ (higher than 5 V spec but negligible for relay coil currents of 80–150 mA; P_diss < 1 mW per channel) |
| Logic | Active HIGH: ESP32-S3 GPIO HIGH → MOSFET gate driven via R_g → MOSFET on (drain-source conducts) → relay coil energised from VBAT+ through coil to GND via MOSFET. ESP32-S3 GPIO LOW → MOSFET off → relay de-energised. |

**Per-channel circuit:**

```
        VBAT+ (12V)
            │
      ┌─────┴─────┐
      │  Relay     │
      │  coil      │◄── flyback diode (cathode to VBAT+, anode to drain)
      └─────┬─────┘
            │ drain
    ┌───────┴───────┐
    │   IRLZ44N     │
    │               │
    │  gate  source │
    └──┬───────┬────┘
       │       │
  R_g  │       │
(150Ω) │       │
       │       │
ESP32 ─┘  R_pd │
GPIO      (10k)│
           │   │
          GND GND
```

**Flyback diodes:** Each relay coil and the siren MUST have an external flyback (freewheel) diode. The diode cathode connects to VBAT+ (12 V) and the anode connects to the MOSFET drain (relay coil low-side). This clamps the inductive voltage spike when the coil de-energises, protecting the MOSFET. A 1N4007 (general purpose) or 1N5819/SS14 (Schottky, faster recovery) is suitable.

**Boot safety:** At power-on, the ESP32-S3 GPIOs default to input mode (high-impedance). The 10 kΩ gate pull-down resistor on each MOSFET holds the gate at 0 V, ensuring all MOSFETs are OFF and all relay outputs are de-energised before the firmware configures GPIOs. This is a hardware fail-safe. Without the pull-down, stray capacitive coupling from the drain (via Cgd) could partially turn on the MOSFET during power-on transients.

**Arm relay MOSFET special case:** The arm relay MOSFET (GPIO 47) differs from the channel relay MOSFETs in that VBAT reaches the arm relay coil through the physical key switch (§5.4.4), not directly. This creates a hardware AND gate: the arm relay can only energise when both the key switch is ON (hardware) and the MOSFET is driven (software). The MOSFET gate circuit (150 Ω series + 10 kΩ pull-down) is identical to the channel relay MOSFETs.

**Component count:** 10× IRLZ44N MOSFETs + 10× gate series resistors (150 Ω) + 10× gate pull-down resistors (10 kΩ) + 10× flyback diodes = **40 components total** for relay, siren, and arm relay drive.

#### 5.4.11 RGB LED (Status Indicator)

| Parameter | Value |
|---|---|
| Type | WS2812 (NeoPixel) addressable RGB LED strip (8 external pixels) + on-board LED on GPIO 48 |
| Pin | GPIO 48 (fixed, on-board) |
| Pixels | 8 addressable external pixels, one per igniter channel. Pixel 0 also drives the on-board LED in parallel. |
| Pixel order | **Data-in at the channel-1 end** — channel N is pixel `N-1` (`RLC_STRIP_REVERSED = 0`). Pixel 0, and therefore the on-board LED, is channel 1. |
| Driver | ESP32-S3 RMT peripheral |
| Function | Igniter continuity display with status modulation (see §11) |

### 5.5 Remote Unit I/O

#### 5.5.1 Rotary Encoder (Channel Selector)

| Parameter | Value |
|---|---|
| Signal type | 2× digital input (A/B quadrature) + 1× digital input (push button) |
| Quantity | 1 encoder |
| Inputs | CLK (A), DT (B), SW (push button) — all with internal pull-ups |
| A/B debounce | Interrupt-driven cycle-position quadrature decoder with 2 ms lockout (not shift-register). Each quadrature state (00, 01, 10, 11) maps to a position in the CW rotation cycle; moving forward (+1) in the cycle is CW, backward (−1) is CCW. This gives consistent direction on every transition, unlike Gray code lookups which alternate on half-step encoders (e.g. KY-040). A configurable pulse divider (`ENC_DIVIDER`, **4** as of 2026-08-20; the spec previously said 3) requires that many raw transitions **in the same direction** before one counted step is emitted; a direction reversal resets the accumulator, so incidental jitter nets to nothing. Both A and B interrupt on **both edges**, since the cycle-position decoder needs the full quadrature sequence. `ENC_REVERSED` selects the rotation sense: which way a KY-040 counts depends on how A and B are wired to the MCU, so it is a board property rather than a decoder property (same reasoning as `RLC_STRIP_REVERSED`). Set to 1 as built — without it the channel selection moved opposite to the knob. |
| Push button debounce | Shift-register, 10 ms polling, 160 ms debounce |
| Rotation function | Select active channel (1–8), wrapping around |
| Push button function | Context-dependent: ARM confirm via **500 ms long-press** (in IDLE with arm switch ON), DISARM (in ARMED). Short press in IDLE with arm switch ON shows "Hold to ARM" prompt. **The 500 ms long-press timer starts from the debounced stable-pressed transition (0xFFFF→0x0000), not from the raw physical press. Total operator hold time is approximately 660 ms (160 ms debounce + 500 ms long-press).** |

The direction of rotation determines increment (+1) or decrement (−1) of the selected channel. Channel selection wraps: incrementing past 8 returns to 1; decrementing past 1 returns to 8. **At boot, the selected channel SHALL default to channel 1.**

**Behaviour while ARMED:** rotating the encoder (channel change) triggers an immediate CMD_DISARM, return to IDLE, and the newly selected channel becomes the cursor position. The operator must re-arm deliberately.

> **AS-BUILT NOTE (2026-08-20).** Until this date the firmware implemented **neither** mechanism specified above. The decoder was a Gray-code level comparison (`if (A != B) CW else CCW`) sampling B at an edge on A — the approach this section explicitly rejects — and `ENC_DIVIDER` did not exist anywhere in the codebase, so every accepted edge became a channel change. Additionally B was configured to interrupt but had no handler attached, so three of every four transitions were lost. Together these produced the reported oversensitivity, and made the input vulnerable to electrical noise: a glitch on A yielded a channel step whose direction depended on whatever B happened to read. Now implemented as specified, with `ENC_DIVIDER` raised to 4. Covered by host tests T-Q01…T-Q06 (§15.5).

#### 5.5.2 Manual Arm/Disarm Switch Input

| Parameter | Value |
|---|---|
| Signal type | Digital input with internal pull-up |
| Quantity | 1 |
| Logic | LOW = ARMED, HIGH = DISARMED (fail-safe) |
| Debounce | Shift-register, 10 ms polling, 160 ms debounce |

#### 5.5.3 Fire Button Input

| Parameter | Value |
|---|---|
| Signal type | Digital input with internal pull-up |
| Quantity | 1 |
| Logic | LOW = pressed, HIGH = released (fail-safe: disconnected wire = not pressed) |
| Debounce | **8-bit shift-register**, 10 ms polling, **80 ms debounce** |
| Stable values | 0x00 = pressed, 0xFF = released |
| Behaviour | **Press-and-hold**: fire command is sent only while button is held AND channel is armed. Releasing the button sends an immediate CMD_CEASE_FIRE. |

The fire button uses an 8-bit shift register (80 ms debounce) rather than 16-bit to minimise release detection latency, which is safety-critical for the dead-man switch.

The software shall enforce **fresh press detection**: the button must transition from stably released (0xFF) to stably pressed (0x00) to initiate fire. A button that reads pressed at boot or at state entry does not count as a press.

#### 5.5.4 Indicator LEDs

Three indicator LEDs are driven directly from GPIO outputs. All three LEDs have **built-in series resistors** — no external current-limiting resistors are required.

| LED | GPIO | Colour | Function |
|---|---|---|---|
| Arm switch LED | 8 | Red | Illuminates when arm switch is in ARMED position |
| Fire button LED (red) | 17 | Red | Illuminates while the fire button is **live** — the remote is in ARMED / PRE_FIRE / FIRING **and** a fresh `STATUS_UPDATE` confirms the base has the same channel armed (v1.41). Both halves are required: the remote can hold ARMED while the base has already disarmed underneath it (arm timeout, key off, an unreported continuity loss), and a red ring in that window would promise a fire path that no longer exists. |
| Fire button LED (green) | 18 | Green | Illuminates whenever the button is not live — safe / IDLE / LINK_LOST / ERROR, and any armed state the base has not confirmed. **The LED does not respond to the button itself**: a press in IDLE is ignored (§8.2.3), so lighting red for it would report an action that will not happen (v1.41). |

| Parameter | Value |
|---|---|
| Signal type | Digital output (fire LEDs: active HIGH; arm LED: active LOW — LED wired 3.3V→resistor→GPIO) |
| Quantity | 3 |
| Drive | Direct GPIO — built-in series resistors on illuminated push-button modules |
| Default state at boot | All off (fire LEDs: GPIO LOW; arm LED: GPIO HIGH) |

#### 5.5.5 Battery Voltage Input

| Parameter | Value |
|---|---|
| Signal type | Analogue input (ADC1) |
| Quantity | 1 |
| Pin | GPIO 1 (ADC1_CH0) — same as base unit for code reuse |
| Conversion | Same as Base Unit §5.4.7 — use `adc_cali_raw_to_voltage()` calibration API. DIVIDER_RATIO may differ. |

#### 5.5.6 ILI9488 LCD Display

| Parameter | Value |
|---|---|
| Controller IC | ILI9488 |
| Resolution | 480 × 320 pixels |
| Interface | 4-wire SPI |
| Colour depth | 18-bit (262k colours); driver transmits RGB666 (3 bytes per pixel). Internal colour constants use RGB888 notation for readability. |
| Touch | Not used in this design (pins 10–14 of module left unconnected) |
| Pin count | 14-pin module (only pins 1–9 connected) |
| SPI clock | Start at 20 MHz write / 6.67 MHz read; increase if stable |
| Backlight | Always on at 100% — GPIO driven digital HIGH. No brightness control. |
| Refresh strategy | Partial updates (dirty-rectangle); full refresh only at state transitions |

**Module pin usage:**

| Module Pin | Label | Function | Connection |
|---|---|---|---|
| 1 | VCC | Power supply (3.3 V – 5 V) | 3V3 or 5V rail |
| 2 | GND | Ground | GND |
| 3 | CS | LCD chip select (active low) | ESP32-S3 GPIO |
| 4 | RESET | LCD hardware reset (active low) | ESP32-S3 GPIO |
| 5 | DC | Data/command select | ESP32-S3 GPIO |
| 6 | SDI (MOSI) | SPI data in | ESP32-S3 GPIO |
| 7 | SCK | SPI clock | ESP32-S3 GPIO |
| 8 | LED | Backlight enable (active high) | ESP32-S3 GPIO (digital output, always HIGH) |
| 9 | SDO (MISO) | SPI data out | ESP32-S3 GPIO |
| 10–14 | Touch | Not connected | N/C |

SPI bus shall use SPI2_HOST on the ESP32-S3.

**Display health check:** at boot, the firmware SHALL read back the ILI9488 display ID register (command 0x04, "Read Display Identification Information") via SPI. If the read-back ID is invalid (e.g., 0x00000000 indicating no panel response) or the SPI transaction fails, the remote SHALL transition to ERROR state (the operator cannot safely control the system without visual feedback). ILI9488-class clone panels may report a non-standard ID (observed 0x2A403300 on this hardware), so the check SHALL NOT require a specific value. It SHALL reject **both undriven signatures — 0x00000000 and 0xFFFFFFFF** — and SHALL check the SPI transaction status. (v1.45 correction: this sentence previously said "any non-zero read-back is considered valid" while the same sentence called a *garbage* read-back a fault; all-ones is garbage and non-zero, so the two clauses disagreed. The firmware implemented the weaker one, and a broken MOSI leaving MISO floating high would have booted as healthy with a dead panel. The boot read also discarded the SPI status, which the next sentence requires — that requirement had been implemented for the periodic check in 1.1.9 but not for the boot read. Both fixed in firmware 1.1.28.) A periodic display health check (every 5000 ms) SHALL re-read the display ID register and compare it with the value latched at boot. **The check runs in every state, not only IDLE** (v1.44 correction: the previous "during IDLE state" wording contradicted the ARMED/PRE_FIRE/FIRING requirement in the next sentence — a check that only runs in IDLE can never detect the failure it is required to react to). **The display health check SHALL be performed within `display_task`, serialised with normal display writes.** It SHALL NOT be performed from a separate task or timer callback. A single bad read SHALL NOT be treated as failure: two consecutive bad reads are required, because one read can be lost to noise on a long flex and disarming the pad on a glitch is itself a hazard. SPI transaction return codes SHALL be checked and counted; a health check that succeeds only because the SPI layer swallowed an error is not a health check. Display failure during ARMED, PRE_FIRE, or FIRING SHALL trigger an immediate CMD_DISARM and transition to ERROR. Implemented in firmware 1.1.9 (`display_health_check()` in `rlc_display.c`, `EVT_DISPLAY_FAULT` to the remote FSM); prior to that only the boot-time read existed and every SPI return code was discarded.

#### 5.5.7 Buzzer Output

| Parameter | Value |
|---|---|
| Signal type | Digital output, configurable polarity (default: active LOW — BC547 NPN transistor inverts GPIO signal) |
| Quantity | 1 |
| Function | Audible feedback (beeps for state changes, ping failures, warnings, alarms) |
| Drive | Active buzzer driven through BC547 NPN transistor (low-side switch) |

#### 5.5.8 RGB LED (Status Indicator)

Same as Base Unit §5.4.11: an 8-pixel WS2812 strip on GPIO 48, one pixel per igniter channel, with the on-board WS2812 in parallel mirroring pixel 0. **Unlike the base, data-in is at the channel-8 end**, so channel N is pixel `7-(N-1)` (`RLC_STRIP_REVERSED = 1`) and the on-board LED mirrors channel 8.

The remote's map is driven from the continuity bands in the cached STATUS_UPDATE rather than from local sensing, so it can go stale; §11.1 layer 4 dims it when it does. Brightness is set independently of the base via `RGB_LED_BRIGHTNESS_REMOTE`, since the remote runs from the smaller 2S pack.

### 5.6 Power Supply

#### 5.6.1 Base Unit Power Supply

| Parameter | Value |
|---|---|
| Battery chemistry | 3S LiPo (11.1V nominal) |
| Specified battery | Turnigy Rapid 3S 5000mAh 11.1V 100C LiPo (SKU 9067160560, 55.5 Wh) |
| Nominal voltage | 11.1V (fully charged: 12.6V) |
| Minimum operating voltage | 9.0V (`BASE_VBAT_CRITICAL_MV`) |
| Minimum arm voltage | 10.5V (`BASE_VBAT_MIN_ARM_MV`) |
| Voltage divider ratio | 4.3:1 (33 kΩ + 10 kΩ, giving 0–3.3V ADC range for 0–14.19V battery) |
| Capacity | 5000 mAh (specified battery) |
| Discharge rating | 100C continuous (500 A) — far exceeds relay + igniter demand, ensuring stable voltage under all loads |
| Connector | XT90 (on battery); PCB must mate with XT90 or use XT90 pigtail. |
| Regulation | 3.3V LDO or DC-DC buck converter for ESP32-S3 and logic. Relay coils driven from battery via IRLZ44N low-side MOSFET switches (§5.4.10). |

#### 5.6.2 Remote Unit Power Supply

| Parameter | Value |
|---|---|
| Battery chemistry | 2S LiPo (7.4V nominal) |
| Specified battery | Turnigy 2200mAh 2S 7.4V 30C Shorty LiPo (SKU 9067110009, 16.28 Wh) |
| Nominal voltage | 7.4V (fully charged: 8.4V) |
| Minimum operating voltage | 6.4V (`REMOTE_VBAT_CRITICAL_MV`, 3.2V/cell) |
| Minimum arm voltage | 7.0V (`REMOTE_VBAT_MIN_ARM_MV`, 3.5V/cell) |
| Voltage divider ratio | 2.8:1 (18 kΩ + 10 kΩ, giving 0–3.0V ADC range for 0–8.4V battery) |

> **AS-BUILT DEVIATION (2026-08-19) — an undocumented 3.3 V zener to ground is fitted on this ADC node, and it breaks the measurement.** Its reverse leakage rises from 27 µA to 144 µA across the 2S range; into the divider's 6429 Ω Thevenin impedance that is 174 mV to 925 mV of droop, making the sense non-linear and under-reading by 9 % at 5.3 V rising to 30 % at 8.6 V. A fully charged pack reads below CRITICAL. See bug #21. Fix: replace with a BAT54 Schottky to the 3.3 V rail, and preferably rescale to 3.0 kΩ / 1.2 kΩ (ratio 3.5) — the 0–3.0 V range specified above already puts full charge at 95 % of the ADC's usable ceiling.

| Capacity | 2200 mAh (specified battery) |
| Discharge rating | 30C continuous (66 A) — far exceeds remote unit demand (~300 mA peak) |
| Connector | T-plug (Deans) (on battery); PCB must mate with T-plug or use T-plug pigtail. |
| Regulation | 3.3V DC-DC buck converter (e.g., MP1584EN module). LDO not recommended due to large voltage differential (7.4–8.4V → 3.3V) and associated thermal waste. |

#### 5.6.2a Battery Undervoltage Protection — NOT SPECIFIED, NOT FITTED

> **OPEN — bug #25.** Neither unit has a hardware undervoltage cut-off, and this specification does not define one. §5.6.1 and §5.6.2 above give chemistry, capacity, connector and regulation, but pack protection rests entirely on the firmware thresholds in §5.6.3 and §7.3.3.
>
> That is insufficient for a LiPo system, for three reasons:
>
> 1. **The ERROR state does not disconnect the load.** Crossing `*_VBAT_CRITICAL_MV` latches the FSM into ERROR, which halts *operation*. Regulators, display backlight, status LEDs, siren driver and MCU continue to draw from the pack.
> 2. **It only protects while firmware runs.** A brownout, a halt, or a unit left switched on after a launch day discharges the pack unobserved.
> 3. **There is no margin.** `BASE_VBAT_CRITICAL_MV` (9000 mV) is exactly 3.00 V/cell — the level at which permanent capacity loss begins. Below roughly 2.5 V/cell a LiPo becomes unsafe to recharge.
>
> **Required:** a hardware cut-off between each pack and the load, set **above** the firmware threshold so the operator still sees the ERROR screen before power is removed. Suggested trip points: base ~9.6 V (3.2 V/cell), remote ~6.8 V (3.4 V/cell). Hysteresis or a latch is required, since an unloaded LiPo recovers above the threshold after disconnect and would otherwise oscillate.

#### 5.6.3 Battery ADC Sampling

Both units read their pack through a resistive divider into ADC1 (12-bit, 12 dB attenuation), then apply the per-unit `*_VBAT_DIVIDER_RATIO`. Each reading is produced as follows:

1. Take a burst of `VBAT_BURST_SAMPLES` (33) raw samples, spaced `VBAT_BURST_GAP_MS` (1 ms) apart.
2. Keep the **median** of the burst.
3. Convert through the ESP-IDF calibration curve (`adc_cali_raw_to_voltage`).
4. Feed an 8-deep moving average, which is the value reported to the FSM and protocol.

**Why a median, not a mean.** A sample clipped at ADC full scale can only bias a mean **upward**, which makes a flat pack read as healthy — the single direction in which a battery guard must never fail. A median discards clipping and spikes outright. This is not hypothetical: during divider calibration (2026-08-19) a noisy bench supply produced 600–1500 counts of sample spread with individual samples clipping, and in a burst where 9 of 33 samples clipped, the mean read 571 counts high — roughly **+2 V of phantom battery voltage** through the base's divider ratio, while the median was exact.

The burst count is odd so the median is a real sample rather than an interpolation between two. The 1 ms spacing spreads the burst over ~33 ms so samples decorrelate from supply ripple instead of landing in the same part of every cycle. Sampling runs at 1 Hz in dedicated tasks that feed the 5 s task watchdog, so the ~33 ms cost is immaterial.

If more than a quarter of a burst clips, the driver logs a warning: the median has already discarded them, but persistent clipping indicates supply noise or an input above the divider's design range and must not pass silently.

---

## 6. Communication Protocol Specification

### 6.1 Physical Layer

| Parameter | Value |
|---|---|
| Protocol | ESP-NOW over Wi-Fi (2.4 GHz) |
| Data rate | 1 Mbps (ESP-NOW default) |
| Max payload | 250 bytes per ESP-NOW frame |
| Range | **Measured 2026-08-25: 200 m holding at −93 dBm** with external antennas on both units, base on the ground. Range near the ground is two-ray (d⁴) limited, not free-space: that geometry runs out at ~250–260 m, while raising both ends to ~1.5 m is worth ~30 dB and pushes it past 1 km. Expected drop-out **−96 to −99 dBm** (1 Mbps DSSS sensitivity ≈ −98 dBm, extended 1–3 dB by the 3-consecutive-miss rule); `ERR_COMM_DEGRADED` refuses arming a couple of dB before that. |
| Antenna | **External on both units** (0 Ω link moved to the U.FL position, 2026-08-23). No firmware configuration — the DevKitC-1 has no RF switch. |
| Channel | Fixed Wi-Fi channel (configurable, default: **channel 11** — avoids the heavily congested channel 1 at launch events). **Note:** channels 1, 6, and 11 are the three non-overlapping 2.4 GHz channels and all three may be congested at well-attended events. The Wi-Fi channel SHOULD be surveyed at the field using an external Wi-Fi analyser tool and adjusted via `WIFI_CHANNEL` before the launch event. |

### 6.2 Security

#### 6.2.1 ESP-NOW Encryption (Security Boundary)

ESP-NOW provides built-in **AES-128-CCM** encryption per peer. This is the system's primary security boundary against external adversaries. The implementation shall:

1. Define a shared 16-byte Primary Master Key (PMK) at compile time (stored in `rlc_config.h`).
2. Derive or define a 16-byte Local Master Key (LMK) per peer.
3. Register each peer with encryption enabled (`esp_now_peer_info_t.encrypt = true`).
4. The PMK and LMK shall be identical on both units (symmetric).

**Note:** PMK and LMK are compile-time constants. Changing keys requires recompilation and reflashing both units.

> **OPEN — bug #20 (as-built deviation).** The keys named above, and the `CMD_INTEGRITY_KEY` of §6.2.2, are committed to a **public** repository. Against an adversary who has read the source, this security boundary does not hold and the keyed integrity check does not detect forgery — only the §6.2.2 replay protection, whose session token is random per link-up, remains effective. The keys must be rotated **and moved out of tracked files** before any field use where an adversary is in the threat model; rotating alone is insufficient because git history preserves superseded values. See Development_Progress.md bug #20.

#### 6.2.2 Application-Layer Integrity and Replay Protection

In addition to ESP-NOW encryption, the application protocol shall implement:

1. **Sequence numbers** — every message includes a monotonically increasing 32-bit sequence number. The receiver shall reject any message with a sequence number equal to or less than the last accepted sequence number from that sender (replay protection). **Upon session establishment (LINK_ACK accepted), both units SHALL reset their per-peer sequence counters to 0.** Sequence numbers are per-sender. **Overflow handling:** if a sender's sequence number reaches `0xFFFFFFFF`, the sender SHALL NOT wrap to 0. Instead, the sender SHALL cease transmitting commands and initiate a session re-establishment (re-link) by sending LINK_REQUEST. This forces both sides to reset counters to 0 via the normal link handshake. At typical transmission rates (~5 increments/second), overflow would not occur for ~27 years, but the behaviour must be defined for correctness.
2. **Session token** — at link establishment, the base generates a random 32-bit session token and sends it to the remote in the `LINK_ACK` message. All subsequent messages must include this session token. Messages with an invalid token are silently discarded. **Upon receiving a LINK_REQUEST, the base SHALL atomically invalidate the previous session token before generating a new one.** This prevents delayed packets from a previous session being accepted during the handover window.
3. **Command integrity check** — ARM, DISARM, FIRE, and CEASE_FIRE commands include a 32-bit CRC32 computed over the **full message (header + payload, excluding the CRC field itself)** appended with a pre-shared 16-byte key. The base shall verify this CRC before executing any command. **Note:** CRC32 is an integrity check, not a cryptographic authentication function. It protects against software bugs and accidental corruption. The actual security boundary is ESP-NOW's AES-128-CCM encryption (§6.2.1).

   **CRC32 specification:**
   - **Polynomial:** CRC32-C (Castagnoli), polynomial 0x1EDC6F41. The ESP32-S3 provides hardware acceleration for CRC32-C via the ROM CRC functions.
   - **CRC input:** `header_bytes || payload_bytes_excluding_crc || integrity_key_bytes` (concatenated in that order). The header fields (protocol_version, msg_type, payload_length, sequence_number, session_token) are included in the CRC input to prevent a corruption in msg_type from causing misinterpretation (e.g., CMD_DISARM interpreted as CMD_FIRE).
   - **Byte order:** all fields in their native little-endian wire encoding. The 16-byte integrity key is appended as-is (byte 0 first).
   - **Initial value:** 0xFFFFFFFF. **Final XOR:** 0xFFFFFFFF (standard CRC32-C).
   - **Test vector:** CRC32-C of the ASCII string `"123456789"` = `0xE3069283`. Implementations SHALL verify this at boot.

#### 6.2.3 Peer Addressing

Both units must be compiled with the other unit's MAC address. MAC addresses shall be defined in `protocol_config.h`. The base shall only accept link requests from the pre-configured remote MAC address. The remote shall only accept responses from the pre-configured base MAC address.

### 6.3 Message Format

All messages use a common header followed by a type-specific payload. All multi-byte integers are little-endian (native ESP32-S3).

#### 6.3.1 Common Message Header (12 bytes)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 1 | `protocol_version` | Protocol version (currently `0x01`) |
| 1 | 1 | `msg_type` | Message type enum (see §6.3.2) |
| 2 | 2 | `payload_length` | Length of payload following this header, in bytes |
| 4 | 4 | `sequence_number` | Monotonically increasing per-sender counter |
| 8 | 4 | `session_token` | Session token (0x00000000 during link establishment) |

Total header size: 12 bytes.

#### 6.3.2 Message Types

| Value | Name | Direction | Description |
|---|---|---|---|
| `0x01` | `LINK_REQUEST` | Remote → Base | Initial link establishment request |
| `0x02` | `LINK_ACK` | Base → Remote | Link establishment acknowledgement (contains session token) |
| `0x10` | `PING` | Remote → Base | Heartbeat ping |
| `0x11` | `PONG` | Base → Remote | Heartbeat pong (response to ping) |
| `0x20` | `CMD_ARM` | Remote → Base | Arm a specific channel |
| `0x21` | `CMD_DISARM` | Remote → Base | Disarm a specific channel (or all) |
| `0x22` | `CMD_FIRE` | Remote → Base | Fire a specific channel |
| `0x23` | `CMD_CEASE_FIRE` | Remote → Base | Immediately cease fire and disarm all |
| `0x30` | `STATUS_UPDATE` | Base → Remote | Periodic or event-driven status report |
| `0x31` | `CMD_ACK` | Base → Remote | Acknowledgement of a received command |
| `0x32` | `CMD_NACK` | Base → Remote | Negative acknowledgement (command rejected, with reason) |

#### 6.3.3 Message Payloads

##### LINK_REQUEST (0x01) — 9 bytes

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 3 | `remote_firmware_version` | Remote firmware version (byte 0 = major, byte 1 = minor, byte 2 = patch) |
| 3 | 6 | `remote_mac` | Remote unit MAC address |

##### LINK_ACK (0x02) — 8 bytes

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 | `session_token` | Randomly generated session token for this session |
| 4 | 3 | `base_firmware_version` | Base firmware version (byte 0 = major, byte 1 = minor, byte 2 = patch) |
| 7 | 1 | `num_channels` | Number of supported channels (8) |

**Note:** During link establishment, the `session_token` field in the common message header (§6.3.1) is `0x00000000`. The authoritative new session token is in the LINK_ACK **payload** `session_token` field above. All subsequent messages use the payload token value in the header field.

**Firmware version enforcement:** the remote shall compare all three components (major, minor, patch) of the received `base_firmware_version` with its own version. If any component differs, the link shall be rejected. The remote shall display "FIRMWARE MISMATCH — Base vX.Y.Z / Remote vX.Y.Z — Reflash required" and shall NOT transition to IDLE. The remote stays in LINKING state but stops retrying, displaying the mismatch error until power cycle.

**Immediately after sending LINK_ACK**, the base SHALL send a **full** STATUS_UPDATE message to provide the remote with initial system state.

> **"Full" is a requirement, not a description (v1.31, review finding m8).** Until firmware 1.1.1 the link layer sent a Phase-1 placeholder here: an all-zero payload with `base_state` hardcoded to `STATE_IDLE`. The app-state guard above rejects LINK_REQUESTs only for the *busy* states (ARMED/PRE_FIRE/FIRING/POST_FIRE), so a base sitting in **ERROR** or **LINK_LOST** answered every handshake by asserting IDLE with no error flags and no continuity data — a false "safe" on the remote's status line and channel strip until the real 2 s update arrived. The link layer no longer fabricates this frame; it invokes an application hook that asks the base's status task to sample and push the true state, which it does within its 100 ms tick.

##### PING (0x10) — 6 bytes

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 | `ping_timestamp` | Sender's `esp_timer_get_time() / 1000` (ms since boot, truncated to 32-bit) |
| 4 | 2 | `remote_battery_voltage_mv` | Remote battery voltage in millivolts (uint16) |

##### PONG (0x11)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 | `ping_timestamp` | Echoed from the PING message |
| 4 | 4 | `pong_timestamp` | Base's own timestamp at time of reply |

##### CMD_ARM (0x20)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 | `integrity_crc` | CRC32 integrity check (see §6.2.2) |
| 4 | 1 | `channel` | Channel number to arm (1–8) |

**Alignment note:** the `uint32_t integrity_crc` field is placed first to ensure natural 4-byte alignment on the ESP32-S3 (Xtensa architecture). All command structs follow this convention. The `__attribute__((packed))` directive SHALL be used alongside `#pragma pack(push, 1)` for compiler portability. A runtime self-test at boot SHALL verify field offsets using `offsetof()` for all packed structs (see §9.9).

##### CMD_DISARM (0x21)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 | `integrity_crc` | CRC32 integrity check |
| 4 | 1 | `channel` | Channel number to disarm (1–8), or `0xFF` for all channels |

##### CMD_FIRE (0x22)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 | `integrity_crc` | CRC32 integrity check |
| 4 | 1 | `channel` | Channel number to fire (1–8) |

##### CMD_CEASE_FIRE (0x23)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 | `integrity_crc` | CRC32 integrity check |

##### STATUS_UPDATE (0x30)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 2 | `continuity_bands` | 2 bits per channel (ch1 in bits 1:0, ch2 in bits 3:2, ... ch8 in bits 15:14). Values: 00 = OPEN, 01 = CONNECTED, 10 = MARGINAL, 11 = *(deprecated SHORT, never emitted since 2026-08-21 — see §5.4.2)*. Enum values match wire encoding directly (CONT_OPEN=0, CONT_CONNECTED=1, CONT_MARGINAL=2, CONT_SHORT=3). The encoding is unchanged by the three-band merge, so no protocol version bump was required. |
| 2 | 2 | `channel_armed_bitmask` | Bits 0–7: armed state per channel (1 = armed). Bits 8–15: reserved. |
| 4 | 2 | `channel_firing_bitmask` | Bits 0–7: currently firing per channel (1 = firing). Bits 8–15: reserved. |
| 6 | 1 | `base_key_switch` | 0 = key SAFE, 1 = key ARM. Debounced key switch position (GPIO 42, §5.4.3b). A **precondition** only — the fire path may still be dead. |
| 7 | 1 | `base_arm_sense` | 0 = arm relay open / no VBAT on the fire path, 1 = arm relay contacts closed **and VBAT live on the ARM SENSE node**. Debounced reading of GPIO 21 (§5.4.3). This is the hazard signal and the welded-contact evidence; it is what drives the remote's ARMED and WELD! states (§10.2.2). Prior to firmware 1.1.0 this field wrongly carried a raw copy of the key switch. |
| 8 | 2 | `battery_voltage_mv` | Base battery voltage in millivolts (uint16) |
| 10 | 1 | `base_state` | Current base FSM state enum |
| 11 | 1 | `error_flags` | Bit field of active errors (see §13) |
| 12 | 2 | `update_sequence` | Monotonically increasing per status update (uint16). Remote can detect gaps. **Wrap-around from 65535 to 0 is expected and SHALL NOT be treated as a gap.** Gap detection SHALL use modular arithmetic: a gap is detected when `(received_seq - last_seq) > 2` using unsigned subtraction modulo 65536. |

**Channel numbering convention:** Channels are numbered 1–8 in the user interface and wire protocol. In bitmask fields (`channel_armed_bitmask`, `channel_firing_bitmask`), channel N maps to bit (N-1). In the `continuity_bands` field, channel N occupies bits [(2N-1):(2N-2)]. For example: channel 1 = bits [1:0], channel 4 = bits [7:6], channel 8 = bits [15:14].

**Multi-arm detection:** The remote SHALL verify that at most one bit is set in `channel_armed_bitmask` (since only single-channel arming is permitted per §9.3). If multiple bits are set, the remote SHALL send CMD_DISARM (channel 0xFF), display "MULTI-ARM ERROR", and transition to IDLE. This defends against a base firmware bug that could arm multiple channels simultaneously.

##### CMD_ACK (0x31) — 6 bytes

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 1 | `acked_msg_type` | Message type being acknowledged |
| 1 | 4 | `acked_sequence_number` | Sequence number of the acknowledged message |
| 5 | 1 | `channel` | Channel the command applied to (1–8), or 0x00 for commands without a channel (CMD_CEASE_FIRE) |

The remote SHALL verify that the `channel` in CMD_ACK matches the channel it requested before acting on the acknowledgement.

**Alignment note:** Unlike command structs (which place `uint32_t integrity_crc` first for natural alignment), ACK/NACK structs do not carry an integrity CRC and are not alignment-optimised. The `__attribute__((packed))` directive handles unaligned field access.

##### CMD_NACK (0x32)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 1 | `nacked_msg_type` | Message type being rejected |
| 1 | 4 | `nacked_sequence_number` | Sequence number of the rejected message |
| 5 | 1 | `reason_code` | Rejection reason (see below) |

**NACK reason codes:**

| Code | Meaning | Human-readable display text |
|---|---|---|
| `0x01` | Base arm switch not in ARMED position | "BASE KEY OFF" |
| `0x02` | Remote arm switch state mismatch | "REMOTE KEY MISMATCH" |
| `0x03` | Channel out of range | "INVALID CHANNEL" |
| `0x04` | Channel has no continuity (OPEN band) | "NO CONTINUITY ON CH N" |
| `0x05` | System not in correct state for this command | "WRONG STATE" |
| `0x06` | Integrity CRC mismatch | "INTEGRITY ERROR" |
| `0x07` | Invalid session token | "SESSION ERROR" |
| `0x08` | Sequence number replay detected | "REPLAY DETECTED" |
| `0x09` | Low battery — command refused | "LOW BATTERY" |
| `0x0A` | Another channel already armed | "CH N ALREADY ARMED" |
| `0x0B` | Arm switch sense fault — arm sense does not confirm arm relay closed / VBAT on fire path | "ARM SENSE FAULT" |
| `0x0C` | Remote battery below operate threshold | "REMOTE BATTERY LOW" |
| `0x0D` | Link quality degraded (>30 % ping loss over the 10-slot window) | "COMM DEGRADED" |
| `0x0E` | Base is in the terminal ERROR state (v1.39) | "BASE IN ERROR" |

The remote shall display the human-readable text on screen for at least 3 seconds when a NACK is received.

**`0x0E` and the specific fault.** The NACK payload is a fixed 6 bytes and carries only a reason code, so the base cannot say *which* error it is in. It does not need to: `error_flags` arrives in every `STATUS_UPDATE`, so the remote already holds it. On `NACK_BASE_ERROR` the remote SHALL name the fault from its cached flags — e.g. **"BASE ERROR: VBAT CRITICAL"** — falling back to the bare "BASE IN ERROR" when no flags are cached. "BASE IN ERROR" alone sends an operator hunting; naming the flag points at the thing to inspect.

### 6.4 Link Management

#### 6.4.1 Link Establishment Sequence

```
Remote                              Base
  │                                   │
  │  ── LINK_REQUEST ──────────────►  │   Remote sends request
  │     (version, MAC)                │   (retries every 2s, max 15 attempts)
  │                                   │
  │                                   │   Base invalidates old session,
  │                                   │   resets sequence counters,
  │                                   │   generates new session token
  │                                   │
  │  ◄─────────────── LINK_ACK ────  │   Base responds with session token
  │     (token, version, channels)    │
  │                                   │
  │     [Remote checks MAJOR.MINOR    │   Base immediately sends
  │      .PATCH version match]        │   full STATUS_UPDATE
  │                                   │
  │  ◄──────────── STATUS_UPDATE ──  │
  │                                   │
  │  ── PING ──────────────────────►  │   Remote sends first ping
  │  ◄─────────────────────── PONG ─  │
  │                                   │
  │        Link established           │
  │   Heartbeat timer starts (1s)     │
```

The remote shall retry `LINK_REQUEST` every 2000 ms. If no `LINK_ACK` is received after 5 attempts (10 seconds), the remote shall display "NO LINK" and continue retrying every 2000 ms indefinitely.

If the base receives a `LINK_REQUEST` while already linked to the same remote MAC (e.g., after a remote reboot), it shall **atomically invalidate the previous session token**, reset both per-peer sequence counters to 0, generate a new session token, and respond normally. **However, the base SHALL reject LINK_REQUEST (silently ignore it) while in ARMED, PRE_FIRE, or FIRING state.** A session reset during these states would invalidate the active session and disrupt a safety-critical operation. **The base SHALL also silently ignore LINK_REQUEST while in POST_FIRE** — the remote's retry mechanism (every 2000 ms) will deliver a subsequent request after the base returns to IDLE. The base remains in its current state and continues processing the existing session. Once the base returns to IDLE (via disarm, fire completion, link-loss timeout, or POST_FIRE cooldown), it will accept LINK_REQUEST normally.

The base shall only accept link requests from the pre-configured remote MAC address. Requests from any other MAC shall be silently ignored.

#### 6.4.1a ESP-NOW Transmission Failure Handling

ESP-NOW's `esp_now_send()` can fail at the MAC layer (no ACK from peer at the Wi-Fi level), independently of application-level ACK/NACK timeouts. The send callback (`esp_now_send_cb_t`) reports delivery status.

**Handling rules:**
- ESP-NOW send callback failures (delivery not confirmed) SHALL be treated identically to application-level ACK timeouts: increment failure counters, trigger retries per existing retry rules (§6.4.4).
- A burst of **5 consecutive ESP-NOW send failures** SHALL be treated as immediate link loss, without waiting for 3 missed heartbeats. This provides faster detection of complete radio loss (e.g., hardware failure, severe interference).
- The send failure counter SHALL be reset on any successful send callback.
- Send failures during repeated CMD_FIRE (fire-and-forget mode) SHALL increment the failure counter but do not individually trigger abort — the dead-man timeout (500 ms without CMD_FIRE receipt at the base) handles this case.

#### 6.4.1b ESP-NOW Receive Processing

The ESP-NOW receive callback (`esp_now_recv_cb_t`) SHALL post received frames to a FreeRTOS queue (depth >= 16) for processing by the appropriate task. The callback SHALL NOT perform message parsing or state machine operations directly — it SHALL only copy the frame data, sender MAC, and RSSI into a queue entry. If the queue is full, the frame is dropped and a warning is logged.

**Exception:** the dead-man timestamp (last CMD_FIRE received time) SHALL be updated directly in the receive callback after matching `msg_type == CMD_FIRE` from the header (see §7.2.4). This is a single atomic timestamp write and does not require queue processing.

#### 6.4.2 Heartbeat Protocol

Once linked, the remote sends a `PING` message every 500 ms. The base responds with `PONG`.

| Parameter | Value |
|---|---|
| Ping interval | 500 ms |
| Pong timeout | 500 ms (if no PONG received within 500 ms of PING send, that ping is a failure) |
| Link quality window | Last 10 pings |
| Link loss threshold | 3 consecutive failed pings (link loss detected within 1.5 seconds) |
| RSSI source | Captured from the ESP-NOW receive callback on each received frame |

**RSSI tracking:** the remote shall record the RSSI from each received frame (PONG, STATUS_UPDATE, ACK, NACK). The display shall show the average RSSI of the 3 most recently received frames.

**Missed ping action (remote):** on each individual ping failure, the remote buzzer shall emit a single short beep (80 ms). There is no strip indication per miss — the beep is the indicator, RSSI and ping RTT are on the display, and sustained failures raise the amber link alarm wink (§11.2).

**PONG validation:** the remote shall verify that the `ping_timestamp` echoed in the PONG matches the timestamp sent in the corresponding PING. A PONG with a mismatched timestamp is discarded silently and does NOT count as a successful ping. The failure counter continues.

**Link loss action:** if 3 consecutive pings fail:
- Remote: display "LINK LOST" warning, buzzer alarm pattern (400 ms on / 400 ms off, repeating), amber link alarm wink over the channel map, transition to LINK_LOST state.
- Base: immediately disarm all channels, de-energise all channel relays, activate siren for 4000 ms (500 on / 500 off, 4 cycles), amber link alarm wink over the channel map, transition to LINK_LOST state.

**Remote battery at base:** the base receives the remote's battery voltage via the PING message. If the remote battery is below `REMOTE_VBAT_MIN_OPERATE_MV`, the base should log an advisory warning. **Additionally, if the remote battery (as reported in PING) is below `REMOTE_VBAT_MIN_ARM_MV`, the base SHALL refuse ARM commands with NACK reason 0x0C ("REMOTE BATTERY LOW").** This provides defence-in-depth against a remote firmware bug that ignores its own battery threshold.

**Link recovery:** when a valid PONG is received after link loss, both units transition to IDLE (not armed — arming must be re-initiated by the operator).

**Base reboot detection:** if the base reboots mid-session (e.g., watchdog reset, brown-out recovery), the remote's existing session token becomes invalid. The base silently discards PINGs with invalid session tokens. The remote detects this via 3 consecutive heartbeat failures (1.5 seconds) and transitions to LINK_LOST, then re-links via LINK_REQUEST. This latency is acceptable — no special mechanism is required.

#### 6.4.3 Status Update Transmission

The base shall send a `STATUS_UPDATE` message:
- **Event-driven:** immediately upon any debounced change in continuity, arm switch state, channel armed state, or channel firing state.
- **Periodic:** every 2000 ms regardless of changes.
- **After link establishment:** immediately after sending LINK_ACK.

Each STATUS_UPDATE includes a monotonically increasing `update_sequence` (uint16). The remote SHALL track this sequence and display a "DATA GAP" warning if more than 2 consecutive sequence numbers are missed. Status updates do not require acknowledgement.

#### 6.4.4 Command Acknowledgement

All commands (`CMD_ARM`, `CMD_DISARM`, `CMD_FIRE`, `CMD_CEASE_FIRE`) require acknowledgement from the base, **with the following exception:**

**Repeated CMD_FIRE during firing (finding 2.3):** once the initial CMD_FIRE has been ACK'd and the remote is in FIRING state, subsequent CMD_FIRE messages sent at 200 ms intervals are **fire-and-forget** — the remote does not expect or process ACK/NACK for these messages. The base SHALL NOT send ACK/NACK for CMD_FIRE received while already in PRE_FIRE or FIRING state. The remote monitors STATUS_UPDATE to detect base-side state changes.

For the initial command:
- Base sends `CMD_ACK` on successful acceptance and execution of the command.
- Base sends `CMD_NACK` with a reason code if the command is rejected.
- Remote waits up to 500 ms for ACK/NACK.

**Retry rules:**
- `CMD_ARM`: retry once on timeout. If retry also times out, display "ARM FAILED — NO RESPONSE" and remain in IDLE.
- `CMD_DISARM`: retry once on timeout. If retry also times out, remote transitions to IDLE locally and displays "DISARM SENT — NO CONFIRMATION". Base will also disarm via link-loss or arm-switch timeout.
- `CMD_FIRE`: **NO RETRY.** If not acknowledged within 500 ms, abort fire attempt, display "FIRE FAILED — NO RESPONSE", send CMD_DISARM, transition to IDLE. Operator must release and re-press fire button to try again.
- `CMD_CEASE_FIRE`: retry once on timeout. Remote transitions to IDLE regardless of response. Base will self-disarm when CMD_FIRE stops arriving (500 ms authorization timeout).

---

## 7. Base Unit Functional Specification

### 7.1 State Machine

```
                    ┌─────────────────────────────────────────┐
                    │           Any state                      │
                    │  [comm loss / base switch disarmed /     │
                    │   anomaly detected]                      │
                    └──────────────┬──────────────────────────┘
                                   │ Disarm all, de-energise
                                   │ all channel relays
                                   ▼
┌──────────┐    Link established   ┌──────────┐
│  BOOT    ├──────────────────────►│  IDLE    │◄──── (always returns here)
└──────────┘                       └────┬─────┘
                                        │
                              CMD_ARM received,
                              both arm switches ON,
                              continuity not OPEN
                                        │
                                        ▼
                                  ┌──────────┐
                                  │  ARMED   │  (siren continuous)   
                                  └────┬─────┘
                                       │
                              CMD_FIRE received,
                              channel is armed
                                       │
                                       ▼
                                  ┌──────────┐
                                  │ PRE_FIRE │  (siren continuous,
                                  │          │   countdown timer)
                                  └────┬─────┘
                                       │
                              Pre-fire delay elapsed
                              + fire authorization current
                                       │
                                       ▼
                                  ┌──────────┐
                                  │ FIRING   │  (igniter relay ON,
                                  │          │   siren continuous)
                                  └────┬─────┘
                                       │
                              Pulse complete
                                       │
                                       ▼
                                  ┌──────────┐
                                  │POST_FIRE │  (auto-disarm,
                                  │          │   de-energise relays)
                                  └────┬─────┘
                                       │
                              Cooldown elapsed
                                       │
                                       ▼
                                  Back to IDLE
```

**States:**

| State | Description | RGB LED | Siren |
|---|---|---|---|
| `BOOT` | Initialisation. All outputs inactive. Waiting for link. | Channel map (cyan chase until continuity valid) | Off |
| `IDLE` | Linked. All channel relays de-energised (NC position). | Channel map; whole map breathes while the key switch is ON | Off |
| `ARMED` | One channel selected for firing. All channel relays remain de-energised (NC). | Red slow blink (500/500) | Pulsing (500 on / 500 off) |
| `PRE_FIRE` | FIRE accepted. Countdown running. Dead-man active. | Red fast blink (100/100) | Continuous |
| `FIRING` | Igniter relay active. Fire pulse timer running. | Red solid | Continuous |
| `POST_FIRE` | Fire complete. All channel relays de-energised. Cooldown. | Channel map — the fired channel flips to OPEN as it is resampled | Off |
| `LINK_LOST` | Comms lost. All channel relays de-energised. | Channel map with the amber link alarm wink | 500 on / 500 off, 4 cycles on entry |
| `ERROR` | Unrecoverable error. All channel relays de-energised. Requires power cycle. | Red triple flash, map dimmed in the gap | 3 blasts on entry |

### 7.2 State Transition Rules

#### 7.2.1 BOOT → IDLE

- Trigger: `LINK_ACK` sent successfully to remote, followed by initial STATUS_UPDATE.
- Guard:
  - ESP-NOW initialised successfully.
  - **ESP-NOW peer registered successfully** (`esp_now_add_peer()` returns `ESP_OK`). If registration fails, retry 3 times, then transition to ERROR.
  - All GPIOs configured, all outputs in safe state.
  - Brown-out detector configured.
- Actions: start heartbeat response handling, begin polling all inputs, send initial `STATUS_UPDATE`.
- Exceptions:
  - ESP-NOW init fails → retry 3 times, then ERROR.
  - Peer registration fails → retry 3 times, then ERROR.

#### 7.2.2 IDLE → ARMED

- Trigger: `CMD_ARM` received for channel N.
- Guard conditions (ALL must be true):
  1. Base key switch is in ARMED position (key switch sense input §5.4.3b reads HIGH, confirming VBAT present at key switch NO output, debounced stable). This check uses the dedicated key sense GPIO 42 — NOT the arm relay feedback GPIO 21 (which can only read HIGH after the arm relay is already energised).
  2. Channel N has continuity (band is GOOD, MARGINAL, or SHORT — only OPEN blocks arming).
  3. Channel N is in range (1–8).
  4. No other channel is currently armed (single-channel arming only).
  5. Message integrity CRC is valid.
  6. Session token is valid.
  7. Sequence number is valid (not a replay).
  8. Base battery voltage is above `VBAT_MIN_ARM_MV` threshold.
  9. **Link quality is acceptable** — `ERR_COMM_DEGRADED` is NOT set (ping failure rate ≤ 30% in last 10 pings). Arming on a degraded link risks dead-man timeout false aborts during firing.
- Actions on successful transition:
  1. Record armed channel number.
  2. Energise arm relay (GPIO 47 HIGH). Verify arm sense GPIO 21 reads HIGH within `ARM_SENSE_VERIFY_TIMEOUT_MS` (200 ms — §14.1) (M1 arm relay verify — confirms contacts actually closed and VBAT on fire bus. If not HIGH, immediately disarm and set `ERR_RELAY_FAULT`).
     The wait is **non-blocking**: the FSM remains in IDLE with a pending-verify flag, so safety events are still processed inside the window. A `CMD_DISARM`, `CMD_CEASE_FIRE`, `CMD_FIRE`, key-switch-OFF, battery-critical, weld-fault or link-loss arriving during the window SHALL cancel the pending ARM, de-energise the relay and NACK with the true reason — never complete the arm behind the operator (review findings 2.1 and 4.6).
  3. Start siren, continuous (see §7.4.1 — the 500 ms ARMED pulse was removed in v1.35).
  4. Start arm timeout timer (`ARM_TIMEOUT_MS`, default: 10000 ms). If no CMD_FIRE is received before this timer expires, auto-disarm and return to IDLE.
  5. Send `CMD_ACK` (with channel field) to remote.
  6. Send `STATUS_UPDATE` with updated bitmasks.
  7. RGB LED → red slow blink.
- **Note:** The channel SPDT relay remains de-energised (NC position) in ARMED state. The relay is only energised (switched to NO/fire position) in the FIRING state (§7.4.2). No current path to the igniter exists until fire command execution, because the relay NO contact is disconnected from COM. Continuity sensing remains active on all channels (no MOSFET switch to disable).
- If any guard fails: send `CMD_NACK` with appropriate reason code (including 0x01 for base key switch OFF, 0x0B for arm sense fault). Remain in IDLE.
- Exceptions:
  - CMD_ARM for wrong channel (e.g., 0 or 9+) → NACK reason 0x03.
  - CMD_ARM while another channel armed → NACK reason 0x0A.
  - CMD_FIRE received while in IDLE → NACK reason 0x05.
  - CMD_DISARM received while in IDLE → ACK (idempotent, already safe).
  - CMD_CEASE_FIRE received while in IDLE → ACK (idempotent, already safe).
  - Multiple rapid CMD_ARM for different channels → first is processed, subsequent NACK'd.

#### 7.2.3 ARMED → PRE_FIRE

- Trigger: `CMD_FIRE` received for the armed channel.
- Guard conditions:
  1. Channel in the FIRE command matches the currently armed channel.
  2. Message integrity CRC is valid.
  3. **Key switch still ON** (key switch sense §5.4.3b reads HIGH) **AND arm sense confirms arm relay still closed** (arm sense §5.4.3 reads HIGH — defence-in-depth re-verification).
- Actions on transition:
  1. Siren remains continuous (already continuous since ARMED; the FSM re-asserts it so PRE_FIRE does not depend on how ARMED was entered).
  2. Start pre-fire countdown timer (`PRE_FIRE_DELAY_MS`, **5000 ms** as of v1.36).
  3. Cancel arm timeout timer.
  4. Send `CMD_ACK` (with channel field) to remote.
  5. Send `STATUS_UPDATE`.
  6. RGB LED → red fast blink.
- **Note:** Continuity is NOT re-checked at this transition as a guard condition. Continuity was verified at arm time (§7.2.2 guard 2). The window between arming and firing is expected to be short (≤ ARM_TIMEOUT_MS). Continuity sensing remains active (the relay is still in NC position during ARMED/PRE_FIRE), but continuity band changes do not block the ARMED→PRE_FIRE transition.
- Exceptions:
  - CMD_FIRE for wrong channel → NACK reason 0x05. Remain ARMED.
  - CMD_ARM for a different channel while armed → NACK reason 0x0A. Remain ARMED.
  - Key switch OFF (§5.4.3b) → immediate disarm (§7.2.7).
  - Arm sense lost (§5.4.3) → arm relay feedback lost, immediate disarm (§7.2.7).

**Wrong-channel CMD_FIRE during PRE_FIRE or FIRING:** CMD_FIRE received during PRE_FIRE or FIRING for a channel other than the armed channel SHALL be silently discarded (not NACK'd, since repeated CMD_FIRE messages are fire-and-forget during these states). This prevents a remote firmware bug from disrupting an active firing sequence.

#### 7.2.4 PRE_FIRE → FIRING

- Trigger: Pre-fire countdown timer elapsed.
- Guard (ALL must be true):
  1. The base must have received at least one `CMD_FIRE` message within the last `FIRE_AUTHORIZATION_TIMEOUT_MS` (500 ms). **Implementation note:** The last-CMD_FIRE-received timestamp SHALL be updated in the ESP-NOW receive callback (see §6.4.1b), not deferred to the state machine task. This ensures the timestamp is not delayed by lower-priority task scheduling.
  2. **Link health: the last frame from the peer was received within `HEARTBEAT_INTERVAL_MS + HEARTBEAT_TIMEOUT_MS` (1000 ms).** This uses the sum of the ping interval and pong timeout to allow for scheduling jitter while ensuring the link has not missed a full heartbeat cycle. This prevents energising the igniter at the exact moment the link dies. **This is a freshness test and SHALL NOT be folded into guard 4's failure-rate test** (v1.44): a rate of 2 misses in 10 is 20 %, which passes guard 4, and still means roughly 1.5 s of silence. Note the base is the PONG *sender*, so its equivalent of "last PONG received" is the last well-formed frame received from the remote — the PING each PONG answers (`rlc_link_ms_since_contact()`). Implemented in firmware 1.1.9; before that this guard was treated as implicitly covered by the failure-rate check and did not exist.
  3. **Key switch still ON** (key switch sense §5.4.3b reads HIGH) **AND arm relay still closed** (arm sense §5.4.3 reads HIGH — defence-in-depth re-verification of relay contact integrity).
  4. **Link quality is acceptable** — `ERR_COMM_DEGRADED` is NOT set (ping failure rate ≤ 30% in last 10 pings). A degraded link risks dead-man timeout false aborts during firing.
- Actions on transition:
  1. Drive the armed channel's SPDT relay output active (switch from NC/continuity to NO/fire path).
  2. Start fire pulse timer (`FIRE_PULSE_DURATION_MS`, default: 1000 ms). **The start SHALL be stopped-first and its return value SHALL be checked** (v1.44): an expired one-shot GPTimer alarm disables the alarm but leaves the driver in RUN state, so a subsequent start on the same power cycle can fail. A failure SHALL make the fire path safe and latch ERROR. It SHALL NOT abort/panic — a panic at this point leaves the arm relay and the channel relay energised for the whole panic-print and reboot interval, with the igniter carrying full current. **The channel number SHALL be passed to the timer callback as a context argument**, not read from a global variable inside the ISR. The callback SHALL only signal the state machine task via `xTaskNotifyFromISR()` — it SHALL NOT drive any GPIO or acquire any mutex. The state machine task, upon receiving the notification, SHALL call `relay_all_safe()` and transition to POST_FIRE.
  3. Keep siren continuous.
  4. Send `STATUS_UPDATE` with firing bitmask set.
  5. RGB LED → red solid.
- Exceptions:
  - **Pre-fire timer expires but no CMD_FIRE received within last 500 ms (dead-man timeout):** abort. `relay_all_safe()`, siren off, return to IDLE. Send STATUS_UPDATE.
  - **Pre-fire timer expires but link health check fails:** abort. `relay_all_safe()`, siren off, transition to LINK_LOST.
  - **Pre-fire timer expires but ERR_COMM_DEGRADED is set:** abort. `relay_all_safe()`, siren off, return to IDLE. Send STATUS_UPDATE.
  - CMD_CEASE_FIRE during PRE_FIRE → immediate abort. `relay_all_safe()`, siren off, return to IDLE. ACK the command.
  - Key switch OFF (§5.4.3b) during PRE_FIRE → immediate abort (key switch OFF breaks arm relay coil current, arm relay de-energises). `relay_all_safe()`, siren off, return to IDLE.
  - Arm sense lost (§5.4.3) during PRE_FIRE → arm relay feedback lost, immediate abort. `relay_all_safe()`, siren off, return to IDLE.
  - Link lost during PRE_FIRE → immediate abort (igniter not yet energised, safe to abort). `relay_all_safe()`, siren for 4000 ms, LINK_LOST.
  - Battery drops critical during PRE_FIRE → immediate abort → ERROR.

#### 7.2.5 FIRING → POST_FIRE

- Trigger: Fire pulse timer elapsed (signalled to state machine task via `xTaskNotifyFromISR()` from the hardware timer callback).
- Actions on transition (executed by state machine task):
  0. Call `fire_timer_stop()`. **Every exit from FIRING SHALL stop the fire timer, including this successful one** (v1.44). An expired one-shot alarm auto-disables only the alarm; the GPTimer driver remains in RUN state, so skipping the stop here left the timer running between pulses and broke the next fire cycle of the same power cycle.
  1. Call `relay_all_safe()` (de-energises channel relay, returning to NC/continuity position, and de-energises arm relay).
  2. Deactivate siren.
  4. Clear armed channel.
  5. Start post-fire cooldown timer (`POST_FIRE_COOLDOWN_MS`, default: 2000 ms).
  6. Send `STATUS_UPDATE` (all bitmasks cleared).
  7. RGB LED → channel map (fired channel flips to OPEN as it is resampled).
- Exceptions:
  - CMD_CEASE_FIRE during FIRING → `relay_all_safe()` immediately. Siren off. Return to IDLE. ACK the command. The igniter has received partial energy but the operator explicitly asked to stop.
  - Base arm switch → DISARM during FIRING → same as CEASE_FIRE. Key switch OFF breaks arm relay coil current, de-energising arm relay. Immediate cutoff.
  - **Link lost during FIRING → SPECIAL CASE.** The igniter is actively energised. Cutting it mid-pulse could leave a partially initiated igniter in an unstable state. **Behaviour is controlled by the `COMPLETE_PULSE_ON_LINK_LOSS` constant (default: `true`).** When `true`, the base shall complete the fire pulse, then transition to POST_FIRE, then to LINK_LOST with full disarm. When `false`, the base shall immediately cut the fire pulse, call `relay_all_safe()`, and transition to LINK_LOST. Remaining pulse time (when completing) is at most FIRE_PULSE_DURATION_MS. **This is a safety-relevant parameter — the RSO/operator should choose the appropriate setting. The rationale: completing the pulse avoids a partially fired igniter in an uncertain state, but contradicts the dead-man principle that operator loss-of-control should stop all operations. For solid-propellant rocket igniters, a partially fired e-match is generally not dangerous (it simply fails to ignite), so `false` is defensible. The `FIRE_PULSE_DURATION_MS` default of 1000 ms should be kept short to limit exposure in either mode.**
  - Continuity band change during FIRING → **EXPECTED.** The igniter is burning/consumed. Do NOT treat as error during FIRING state. Ignore continuity changes on the armed channel while in FIRING.
  - **Battery drops critical during FIRING** → complete the fire pulse (same reasoning as link loss), then → ERROR.
  - CMD_ARM received during FIRING → NACK reason 0x05.

#### 7.2.6 POST_FIRE → IDLE

- Trigger: Cooldown timer elapsed.
- Actions: state change only. RGB LED → channel map.
- Exceptions:
  - CMD_ARM received during cooldown → NACK reason 0x05. Must wait for IDLE.
  - Link lost during POST_FIRE → relays are already safe. Transition to LINK_LOST immediately (safe either way).
  - **LINK_REQUEST received during POST_FIRE:** the base SHALL silently ignore LINK_REQUEST while in POST_FIRE. The remote's retry mechanism (every 2000 ms) will deliver a subsequent request after the base returns to IDLE. The cooldown is not interrupted — relays are already safe and the timer ensures a minimum delay before the next arming sequence.

#### 7.2.7 Any State → IDLE (Disarm)

This transition can be triggered from ARMED, PRE_FIRE, or FIRING (with caveats for FIRING per §7.2.5) by any of:

- `CMD_DISARM` or `CMD_CEASE_FIRE` received.
- Key switch moved to OFF position (key sense §5.4.3b detects transition, arm relay coil current broken).
- Arm relay feedback lost (arm sense §5.4.3 reads LOW while arm relay should be energised — indicates relay dropout or contact fault).
- Repeated `CMD_FIRE` not received for 500 ms during PRE_FIRE (dead-man timeout).
- Base battery voltage drops below `VBAT_CRITICAL_MV`.
- Arm timeout elapsed (`ARM_TIMEOUT_MS`, default: 10000 ms) — no CMD_FIRE received within the timeout period after entering ARMED state.

- **Continuity on the armed channel transitions to OPEN** while in ARMED or PRE_FIRE (added v1.35, 2026-08-26 — see below).

**Continuity-loss disarm (v1.35).** Up to v1.34 this specification stated the opposite: band changes during ARMED/PRE_FIRE were informational only, and §7.3.1 called mid-arm igniter disconnection "an accepted low-probability risk" bounded by `ARM_TIMEOUT_MS`. On-target testing on 2026-08-26 showed that risk is not the one that was accepted — disconnecting an igniter is precisely what happens when someone is working at the pad, which is the moment being armed matters most. The base stayed ARMED with the siren sounding and the fire path live, and neither unit indicated the igniter was gone.

Requirements:

1. Only the **OPEN** band SHALL trigger disarm, matching the arming guard (§7.2.2 guard 2), which likewise refuses only on OPEN. MARGINAL and SHORT remain informational (§7.3.1 step 2).
2. Only the **currently armed channel** SHALL trigger it. Band changes on other channels remain informational.
3. It SHALL apply in **ARMED and PRE_FIRE only**. It SHALL NOT apply in FIRING — during the pulse the channel relay is on NO and its NC sense line is physically disconnected, so the armed channel reads OPEN *by design*, and acting on it would abort every fire pulse the instant it began. It SHALL NOT apply in POST_FIRE, where OPEN is the **success** indicator (§7.3.1, post-fire igniter status).
4. Detection latency is bounded by the round-robin sample period: one channel per `CONT_SAMPLE_INTERVAL_MS` (100 ms) over 8 channels, so up to **~800 ms** plus hysteresis. This is well inside `ARM_TIMEOUT_MS` and is accepted; the mechanism is a safety backstop, not a real-time interlock.
5. The remote learns of the disarm through the resulting `STATUS_UPDATE`, whose `continuity_bands` field shows the channel as OPEN. No new NACK reason is defined — there is no command to NACK, since the trigger is a spontaneous hardware event.

Actions:
1. All relay outputs → inactive (call `relay_all_safe()`). De-energise arm relay (GPIO 47 LOW). All channel SPDT relays return to NC position.
2. Deactivate siren.
3. Clear armed channel.
4. Cancel arm timeout timer (if running).
5. Send `CMD_ACK` (if triggered by command) or `STATUS_UPDATE`.
6. RGB LED → channel map.

#### 7.2.8 Any State → LINK_LOST

- Trigger: 3 consecutive heartbeat failures (no PING received for 1.5 seconds).
- Actions:
  1. Execute full disarm (§7.2.7 actions 1–3).
  2. Activate siren for 4000 ms (500 on / 500 off, 4 cycles).
  3. Send STATUS_UPDATE (if possible — link may be partially functional).
  4. RGB LED → channel map with the amber link alarm wink.
- Recovery: when a valid PING is received, respond with PONG, transition to IDLE (not ARMED). **Siren is silenced immediately on transition to IDLE**, even if the SIREN_LINK_LOST pattern has not completed its 4-cycle duration.
- Exceptions:
  - LINK_LOST persists for extended time → base stays in LINK_LOST. System is safe (all relays off). No automatic shutdown. Operator must physically intervene.
  - Key switch toggled while in LINK_LOST → no effect on relays (arm relay already off). State is updated for when link recovers.

#### 7.2.9 Any State → ERROR

- Trigger: `VBAT_CRITICAL_MV` exceeded (except during FIRING — see §7.2.5), assertion failure, arm relay contact welding detected, or unrecoverable internal error.
- Actions:
  1. `relay_all_safe()` (de-energises arm relay + all channel relays).
  2. Set `ERR_INTERNAL` or `ERR_VBAT_CRITICAL` flag.
  3. Send one final `STATUS_UPDATE` if possible.
  4. RGB LED → red triple flash pattern.
  5. Activate siren: 3 short blasts (200 ms on / 200 ms off), then silence (`SIREN_ERROR`).
  6. **ERROR state is intentionally unrecoverable** to prevent operation with a potentially compromised unit. System halted. Requires power cycle.

### 7.2.9a No silent refusals (v1.39)

**Every refusal, abort or failure that an operator can trigger SHALL produce both an audible indication and a message on the remote display.** A log line is not operator feedback.

This is a requirement rather than a style note because the failure mode is specific and was hit repeatedly on target: a refusal that only beeps — or does nothing at all — is indistinguishable from an input that never registered. An operator cannot tell a flat battery from a dead link from a base that needs a power cycle, and the natural response to apparent non-response is to try again, which is the wrong instinct at a launch pad.

Three consequences, all applied in firmware 1.1.4–1.1.6:

1. **The base answers commands in ERROR** (§7.2.9) rather than discarding them, so the remote gets a reason instead of a timeout.
2. **Timeouts are reported.** "No response" is itself a finding worth showing.
3. **The FIRE guard family reports.** Arm key off, base not armed, stale status, degraded link, send failure, key-off-after-ACK and no-response each drop the remote to IDLE — previously in silence, with the operator holding a fire button and nothing happening.

### 7.3 Input Processing

#### 7.3.1 Continuity Monitoring

A dedicated FreeRTOS task (`continuity_task`) shall sample all 8 continuity ADC inputs in round-robin fashion. Each channel is sampled every 100 ms using the `adc_cali_raw_to_voltage()` calibration API, with 64-sample oversampling per reading for noise reduction. The calibrated millivolt value is classified into a continuity band (CONNECTED, MARGINAL, OPEN — the former SHORT band was folded into CONNECTED in v1.29) using the threshold constants from §14.5 with hysteresis.

When a band change is detected on any channel:
1. Update the internal `continuity_bands` field.
2. If a channel that is currently armed transitions to MARGINAL: log an advisory warning (informational only — does not trigger disarm). Only OPEN disarms, matching the arming guard. (This step named the SHORT band until v1.44; that band was folded into CONNECTED in v1.29 and is never produced.)
3. Trigger an event-driven `STATUS_UPDATE` to the remote.
4. Post the change — channel number **and** new band — to the base FSM event queue as `EVT_CONTINUITY_CHANGED`, so the state machine can apply §7.2.7's continuity-loss disarm. The band is carried in the event rather than re-read by the FSM, because the round-robin sampler may have moved on by the time the event is dequeued. The post SHALL use a short blocking send rather than a zero-timeout one: dropping this event on a transient queue burst would silently leave the base armed on an open igniter.

**Relay settling (v1.44, §5.4.6).** After a channel relay is de-energised the sampler SHALL skip that channel for at least `CONT_RELAY_DROPOUT_MS` (50 ms) so no reading is taken while the NO→NC contacts are still bouncing. The channel's band is left unchanged across the window — this is a known-invalid measurement window, not a fault — and the round-robin re-reads it on the next sweep. Implemented by `continuity_note_relay_released()`, called from every de-energise path in `rlc_relay.c`.

**Note:** Continuity sensing remains active at all times — there is no MOSFET switch to disable. The SPDT relay provides inherent isolation: when the relay is de-energised (NC position), the continuity sense circuit is connected to the igniter; when the relay is energised for firing (NO position), the NC contact physically disconnects. During FIRING, the armed channel's ADC reads OPEN (expected — NC disconnected). All other channels remain readable.

**Continuity-loss disarm IS implemented as of v1.35 (2026-08-26).** A transition to OPEN on the currently armed channel while in ARMED or PRE_FIRE SHALL trigger the disarm sequence of §7.2.7, which see for the full requirements and for the exclusions that keep FIRING and POST_FIRE unaffected. This replaces the v1.8–v1.34 position that mid-arm igniter disconnection was an accepted low-probability risk.

Since the continuity circuit uses the SPDT relay NC contact, continuity readings are valid whenever the relay is de-energised, regardless of arm switch position. During FIRING, the fired channel reads OPEN (NC disconnected) while all other channels continue to provide live readings. After the fire pulse completes and the relay returns to NC, the fired channel's continuity reading automatically updates — OPEN indicates the igniter has fired, GOOD/MARGINAL indicates a misfire.

**Post-fire igniter status:** After a fire pulse completes and the system returns to POST_FIRE/IDLE, the channel relay returns to NC position, reconnecting the igniter to the continuity sense circuit. The first valid continuity reading after POST_FIRE indicates whether the igniter has fired (OPEN) or failed to fire (GOOD/MARGINAL). A delay of at least 50 ms after relay de-energisation SHALL be observed before the first ADC sample (relay dropout settling time).

#### 7.3.2 Arm Sense Monitoring

A dedicated FreeRTOS task or timer callback (`arm_sense_task`) shall poll the arm sense input (GPIO 21, §5.4.3) using the shift-register debounce engine at 10 ms intervals. This input reads the ARM SENSE node (arm relay COM output). On debounced change:
1. If arm sense moved to LOW (0x0000 = arm relay de-energised or contacts opened) and base is in ARMED/PRE_FIRE/FIRING: execute immediate disarm (key switch turned OFF, or arm relay lost).
2. Trigger `STATUS_UPDATE`.
3. Periodically during IDLE: verify arm sense reads LOW when arm relay is known to be de-energised (contact welding detection).

#### 7.3.3 Battery Monitoring

Sampled every 1000 ms: the median of a 33-sample burst, then an 8-deep moving average (§5.6.3). Two thresholds:
- `VBAT_MIN_ARM_MV`: minimum voltage to allow arming. Below this, ARM commands are NACK'd with reason 0x09.
- `VBAT_CRITICAL_MV`: critical low voltage. Below this, immediate disarm and transition to ERROR state (unless in FIRING — complete pulse first).

### 7.4 Output Control

#### 7.4.1 Relay Drive Functions

Relays shall only be driven using the following encapsulated functions, which apply the configured polarity:

```c
void relay_fire_set(uint8_t channel, bool state);    // Energise/de-energise one channel SPDT relay
void relay_fire_all_off(void);                        // De-energise all channel SPDT relays (return to NC)
void arm_relay_set(bool state);                       // Energise/de-energise arm relay (GPIO 47)
void relay_all_safe(void);                            // De-energise arm relay + all channel relays (full safe state)
```

`relay_all_safe()` shall be called:
- At boot (before any other operation).
- On any disarm event.
- On any error.
- On entry to LINK_LOST.

#### 7.4.2 Firing Sequence Detail

The exact sequence within the FIRING state:

1. Assert: exactly one channel SPDT relay is about to be energised. Assert: arm relay is already energised (ARM SENSE node should read HIGH).
2. Energise channel SPDT relay (switch from NC/continuity to NO/fire path).
3. Start **hardware timer** for `FIRE_PULSE_DURATION_MS`. **The channel number SHALL be passed to the timer callback as a context argument**, not read from a global variable inside the ISR. The callback SHALL:
   - Signal the state machine task via `xTaskNotifyFromISR()`. The callback SHALL NOT drive any GPIO, call any state machine function, or acquire any mutex.
4. The state machine task, upon receiving the notification:
   - Asserts that the passed channel matches the currently armed channel.
   - Calls `relay_all_safe()` (de-energises channel relay, returning to NC/continuity position).
   - Transitions to POST_FIRE.

**Design rationale:** All relay control is performed in task context, never in ISR context. This keeps safety-critical GPIO operations in a single execution context, simplifying safety reasoning and avoiding concurrent relay access between ISR and task. The fire pulse duration may exceed `FIRE_PULSE_DURATION_MS` by a few hundred microseconds (task scheduling latency), which is negligible against the 1000 ms default pulse.

The fire pulse shall be driven by a hardware timer interrupt, NOT a software delay, to guarantee precise timing even under software load.

---

## 8. Remote Unit Functional Specification

### 8.1 State Machine

```
┌──────────┐                        ┌──────────┐
│  BOOT    ├───────────────────────►│ LINKING  │
└──────────┘                        └────┬─────┘
                                         │ LINK_ACK received
                                         │ + version match
                                         ▼
                                   ┌──────────┐
                              ┌───►│  IDLE    │◄────────────────────┐
                              │    └────┬─────┘                     │
                              │         │                           │
                              │  Arm switch ON +                    │
                              │  encoder press                      │
                              │         │                           │
                              │         ▼                           │
                              │   ┌──────────┐  disarm/timeout/    │
                              │   │  ARMED   ├─────────────────────┘
                              │   └────┬─────┘  encoder rotate/    │
                              │        │        switch off          │
                              │  Fire button                       │
                              │  pressed (fresh)                   │
                              │        │                           │
                              │        ▼                           │
                              │   ┌──────────┐                     │
                              │   │PRE_FIRE  │  button released/   │
                              │   └────┬─────┘  abort → IDLE ─────┘
                              │        │                           │
                              │  Local countdown                   │
                              │  elapsed                           │
                              │        │                           │
                              │        ▼                           │
                              │   ┌──────────┐                     │
                              │   │ FIRING   ├─────────────────────┘
                              │   └──────────┘  button released/
                              │                  fire complete
                              │
                              │   ┌──────────┐
                              └───┤LINK_LOST │ (recovery → IDLE)
                                  └──────────┘
```

**States:**

| State | Description | RGB LED |
|---|---|---|
| `BOOT` | Initialisation. Display splash screen. | Cyan chase (no continuity data yet) |
| `LINKING` | Sending `LINK_REQUEST`, waiting for `LINK_ACK`. Display "Connecting..." with retry counter. | Cyan chase, then the channel map once STATUS_UPDATE arrives |
| `IDLE` | Linked. Display status overview. Operator can select channels. | Channel map, selected channel pulsing; dimmed if the data is stale |
| `ARMED` | ARM command ACK'd. Fire button active. | Red slow blink (500/500) |
| `PRE_FIRE` | Fire button held. Pre-fire countdown active. CMD_FIRE sent at 200 ms intervals. | Red fast blink (100/100) |
| `FIRING` | Local countdown elapsed. Ignition assumed active. CMD_FIRE continues at 200 ms intervals. | Red solid |
| `LINK_LOST` | Heartbeat lost. Buzzer alarm. All commands disabled. | Dimmed channel map with the amber link alarm wink |
| `ERROR` | Unrecoverable error. Intentionally unrecoverable to prevent operation with a compromised unit. Requires power cycle. | Red triple flash |

### 8.2 State Transition Rules

#### 8.2.1 BOOT → LINKING

- Trigger: Initialisation complete (display, ESP-NOW, inputs all configured).
- Guard: **ESP-NOW peer registered successfully** (`esp_now_add_peer()` returns `ESP_OK`). If registration fails, retry 3 times, then transition to ERROR.
- Actions: display splash screen with version number, begin sending `LINK_REQUEST` every 2000 ms.

#### 8.2.2 LINKING → IDLE

- Trigger: `LINK_ACK` received from base with matching MAJOR.MINOR.PATCH firmware version.
- Actions: store session token, reset sequence counters, start heartbeat timer, update display to main status view.
- Exceptions:
  - Firmware version mismatch (any of major/minor/patch) → display "FIRMWARE MISMATCH — Base vX.Y.Z / Remote vX.Y.Z — Reflash required". Stop retrying. Stay in LINKING. Require power cycle.
  - Unexpected `num_channels` in LINK_ACK → store and adapt. If `num_channels` > 8, cap at 8 (protocol limitation); if it is 0, treat as 8 rather than leaving the remote with nothing selectable. If `num_channels` < 8, channels above the reported count SHALL be hidden on the display (rendered as an explicit "N/A" slot — never as an OPEN igniter, which would read as a disconnected lead), the encoder SHALL wrap at the reported count, and ARM commands for those channels SHALL be blocked locally with a named refusal rather than left to the base's NACK. Implemented in firmware 1.1.9 (`rlc_link_get_peer_num_channels()`, `encoder_set_max_channel()`); before that the field was received and discarded.

#### 8.2.3 IDLE → ARMED

- Trigger: operator action sequence:
  1. Arm switch is in ARMED position (local check, debounced, stable).
  2. Operator has selected a channel via the rotary encoder.
  3. Operator presses and holds encoder button for **500 ms** (long-press) to confirm arming. A short press (< 500 ms) displays "Hold to ARM" prompt without sending a command.
- Local guard conditions (ALL must be true before sending CMD_ARM):
  1. Local arm switch is ARMED.
  2. **Remote battery voltage is above `REMOTE_VBAT_MIN_ARM_MV`.** If below, display "REMOTE BATTERY LOW — CANNOT ARM". Do not send command.
  3. **Most recent STATUS_UPDATE was received within 2× STATUS_UPDATE_INTERVAL_MS (default: 4000 ms).** If stale, display "NO BASE STATUS DATA". Do not send command.
  4. Link is healthy (last ping succeeded).
- Actions:
  1. Send `CMD_ARM` for the selected channel.
  2. Wait for `CMD_ACK` (up to 500 ms, one retry on timeout).
  3. If ACK'd: **verify that the `channel` field in CMD_ACK matches the requested channel.** If match: transition to ARMED. Update display. Buzzer: two short beeps. If mismatch: treat as error, send CMD_DISARM, display "CHANNEL MISMATCH ERROR".
  4. If NACK'd: display NACK reason text (from §6.3.3 table) for 3 seconds. Remain in IDLE. Buzzer: three rapid beeps.
  5. If no response after retry: display "ARM FAILED — NO RESPONSE". Remain in IDLE.
- Exceptions:
  - Encoder press with arm switch OFF → display "Turn ARM key first". No command sent.
  - **Encoder press during pending ACK wait → cancel pending command, send CMD_DISARM, return to IDLE.**
  - Fire button pressed while in IDLE → ignored (no buzzer, no display change).
  - STATUS_UPDATE shows base in ERROR → display "BASE ERROR" prominently. Refuse ARM commands.

#### 8.2.4 ARMED → PRE_FIRE

- Trigger: fire button pressed (fresh press: transition from 0xFF to 0x00 for 8-bit debounce).
- Guard:
  1. Most recent `STATUS_UPDATE` from base confirms the channel is still armed.
  2. Link is healthy (last ping succeeded).
- Actions:
  1. Send `CMD_FIRE` for the armed channel.
  2. Wait for `CMD_ACK` (up to 500 ms, **no retry** for fire commands).
  3. If ACK'd with matching channel: transition to PRE_FIRE. **Start a local countdown timer initialised to `PRE_FIRE_DELAY_MS`.** The display shows this countdown decrementing at 100 ms resolution. Begin sending repeated CMD_FIRE at 200 ms intervals (fire-and-forget — no ACK expected for subsequent messages).
  4. If NACK'd or no response or channel mismatch: abort. Display error. Send `CMD_DISARM`. Transition to IDLE.
- **During the CMD_FIRE ACK wait (up to 500 ms), the remote remains in ARMED state.** If the fire button is released during this wait, the remote abandons the fire attempt, sends CMD_DISARM, and transitions to IDLE. If the arm switch is moved to DISARM, same behaviour. If the encoder is pressed, same behaviour.
- **Note:** the local countdown is a display-only timer — the base's PRE_FIRE timer is authoritative for actual ignition timing.

#### 8.2.5 PRE_FIRE → FIRING

- Trigger: local countdown timer elapsed.
- Actions: update display from countdown to "IGNITION ACTIVE". Continue sending CMD_FIRE at 200 ms.

#### 8.2.6 FIRING → IDLE

- Trigger (any of):
  1. Fire button released → send `CMD_CEASE_FIRE`, toast `CH n PULSE CUT SHORT`
     with the attention beep, transition to IDLE.
  2. `STATUS_UPDATE` showing fire complete (base in POST_FIRE or IDLE) → display
     "FIRE COMPLETE" for `FIRE_COMPLETE_SCREEN_MS` (10000 ms, §10.2.4a), then
     transition to IDLE.
  3. Arm switch moved to DISARM → send `CMD_CEASE_FIRE`, toast
     `CH n CUT SHORT - ARM OFF` with the attention beep, transition to IDLE.
  4. Link lost → transition to LINK_LOST.
- Actions: update display, clear firing indicators, stop sending repeated CMD_FIRE.

**Cease-fire feedback (added 2026-08-27, fw 1.1.24).** Triggers 1 and 3 returned
to IDLE silently — logged, but with nothing shown or sounded. That loses the one
fact the operator most needs to carry to the pad: **the channel was energised,
just for less than the full pulse.** A silent return to IDLE is
indistinguishable from an abort during the pre-fire countdown (§8.2.5), where no
current ever reached the igniter — and those two situations call for very
different behaviour when someone walks out to the rail.

The wording states what happened without asserting what it means. Whether the
igniter took is not knowable from the remote; the §10.2.2 continuity grid
answers that, live, as soon as the toast clears.

Note this was **not** a §7.2.9a violation, and the v1.39 audit that found "only
five log-without-display sites, all legitimate" was correct by its own terms.
§7.2.9a covers refusals, aborts and failures; a cease-fire is a *successful*
operator action. The gap was in a neighbouring category the requirement did not
reach: operator-initiated state changes whose **consequences** the operator
needs to know about.
- Exceptions:
  - STATUS_UPDATE shows base no longer in PRE_FIRE or FIRING unexpectedly → sync to base state. Return to IDLE. Stop sending CMD_FIRE.

#### 8.2.7 ARMED / PRE_FIRE → IDLE (Disarm without firing)

- Trigger (any of):
  1. Arm switch moved to DISARM position.
  2. Encoder button pressed (context: "disarm" in ARMED state).
  3. Encoder rotated (channel change while armed).
  4. `STATUS_UPDATE` showing base no longer armed.
  5. Link lost.
  6. No `STATUS_UPDATE` for 5000 ms (stale data safety timeout).
- Actions:
  1. Send `CMD_DISARM` (unless triggered by link loss).
  2. Update display.
  3. Buzzer: one long beep (500 ms).

#### 8.2.8 Any State → LINK_LOST

- Trigger: 3 consecutive ping failures.
- Actions:
  1. If in ARMED or FIRING: mark as disarmed locally (base will also disarm).
  2. Display "LINK LOST" prominently.
  3. Buzzer: continuous alarm pattern (200 ms on / 200 ms off).
  4. Continue sending PINGs at 500 ms.
  5. RGB LED → channel map with the amber link alarm wink.
- Recovery: on first successful PONG, transition to IDLE (never directly to ARMED).

### 8.3 Input Processing

#### 8.3.1 Rotary Encoder

- A/B pins: read via interrupts with cycle-position quadrature decoder and 2 ms lockout (§5.5.1).
- Push button: shift-register debounce, 10 ms polling.
- Rotation: increment/decrement selected channel (1–8, wrapping).
- Push: context-dependent:
  - In IDLE with arm switch OFF: no action.
  - In IDLE with arm switch ON: send ARM for selected channel.
  - In ARMED: send DISARM.
- Rotation while ARMED: triggers immediate DISARM → IDLE, channel selection updates.

#### 8.3.2 Fire Button

- Shift-register debounce, 10 ms polling.
- **Fresh press detection:** must transition from 0xFF (released) to 0x00 (pressed). A button that is 0x00 at boot or at state entry does not count.
- While held in FIRING state: `CMD_FIRE` sent every 200 ms with fresh sequence numbers.
- On release (0x00 → 0xFF): `CMD_CEASE_FIRE` sent immediately.

#### 8.3.3 Arm Switch

- Shift-register debounce, 10 ms polling.
- Moving to DISARMED (0xFFFF) at any time triggers immediate disarm sequence.
- Moving to ARMED (0x0000) does NOT automatically arm — it is a precondition only.

#### 8.3.4 Battery Monitoring

Same calibrated ADC approach as base (§7.3.3). Three thresholds:
- `REMOTE_VBAT_MIN_ARM_MV`: minimum voltage to allow arming. Below this, the remote SHALL NOT send CMD_ARM. Display "REMOTE BATTERY LOW — CANNOT ARM".
- `REMOTE_VBAT_MIN_OPERATE_MV`: minimum voltage for normal operation. Below this: display warning, continue operating.
- `REMOTE_VBAT_CRITICAL_MV`: critical low. Display warning, refuse to arm, buzzer alarm, RGB LED error. **If remote battery drops critical during FIRING, the remote transitions to ERROR, ceasing all CMD_FIRE transmissions. The base will abort via dead-man timeout within 500 ms.**

### 8.4 Repeated FIRE Transmission

While the remote is in the PRE_FIRE or FIRING state and the fire button is held:
- A `CMD_FIRE` message is sent every 200 ms.
- Each message has a fresh sequence number and valid integrity CRC.
- **These repeated messages are fire-and-forget — the remote does not expect or process ACK/NACK.** The base SHALL NOT send ACK/NACK for CMD_FIRE received while in PRE_FIRE or FIRING.
- The remote monitors STATUS_UPDATE to detect base-side state changes.
- The base uses the "last CMD_FIRE received within 500 ms" rule for dead-man authorization.

---

## 9. Safety Requirements

These requirements are non-negotiable and shall take precedence over all other functional requirements.

### 9.1 Fail-Safe Defaults

| Condition | Required behaviour |
|---|---|
| Power-on (either unit) | All channel SPDT relays de-energised (NC position), arm relay de-energised, no channel armed |
| Communication lost | Immediate disarm all, de-energise all relays (including arm relay), siren (base), buzzer alarm (remote) |
| Base key switch → OFF | Arm relay coil current broken — arm relay de-energises regardless of software. Fire path broken for all channels. |
| Remote arm switch → DISARM | Send DISARM ALL to base |
| Fire button released | CMD_CEASE_FIRE sent, base stops firing at next 500 ms authorization check |
| Battery critical (either unit) | Refuse to arm; if armed, disarm (complete fire pulse first if FIRING) |
| Software crash / watchdog reset | ESP32-S3 GPIO default state ensures all MOSFETs off (hardware fail-safe via gate pull-downs). Arm relay also de-energised (key switch provides additional hardware break). |
| Unknown / corrupt message received | Silently discard (never act on unvalidated data) |
| Channel change while armed (encoder rotation) | Immediate disarm, return to IDLE |
| Arm switch sense fault at arm time | Refuse to arm (NACK 0x0B) |
| Arm relay contact welding detected | Set ERR_RELAY_FAULT, refuse all arming |

### 9.2 Dual-Key Arming

No channel can be armed unless ALL of the following are simultaneously true:
1. Base physical key switch is in ARMED position (provides VBAT to arm relay coil).
2. Remote physical arm switch is in ARMED position.
3. Operator has explicitly pressed the encoder button to send an ARM command (deliberate action).
4. Communication link is healthy.
5. Selected channel has igniter continuity (band is GOOD, MARGINAL, or SHORT — only OPEN blocks arming).
6. Base battery voltage is above `BASE_VBAT_MIN_ARM_MV`.
7. Remote battery voltage is above `REMOTE_VBAT_MIN_ARM_MV`.
8. Arm sense confirms arm relay contacts closed and VBAT present on fire path (§5.4.3).
9. STATUS_UPDATE data is fresh (received within 2× STATUS_UPDATE_INTERVAL_MS).
10. Arm relay contact welding check passes (arm sense reads LOW when arm relay is de-energised).

### 9.3 Single-Channel Arming

Only one channel may be armed at a time. An ARM command for a new channel while another is armed shall be NACK'd with reason 0x0A. The operator must disarm the current channel first. Rotating the encoder (changing channel selection) while armed triggers automatic disarm.

### 9.4 Fire Button Dead-Man Switch

The fire button acts as a dead-man switch: the operator must continuously hold it throughout the entire pre-fire delay and fire pulse. Releasing the button at any point aborts the sequence (except during FIRING where link loss allows pulse completion — see §7.2.5).

### 9.5 Auto-Disarm After Fire

After a fire pulse completes, the base automatically disarms the channel and de-energises the channel relay (returning to NC/continuity position). The operator must go through the full arm sequence again to fire another channel.

### 9.6 Watchdog Timer

Both units shall enable the ESP32-S3 hardware watchdog timer with a 5-second timeout. The main loop and all critical tasks must feed the watchdog. A watchdog reset results in a clean boot with all outputs in safe state.

**Task Watchdog Timer (TWDT):** Each critical task (arm switch, fire button, continuity, heartbeat, state machine) SHALL register with the ESP-IDF Task Watchdog Timer via `esp_task_wdt_add()`. The TWDT timeout SHALL be 5 seconds. Unlike the hardware watchdog (which detects total system lock), TWDT detects individual task starvation — if any registered task fails to call `esp_task_wdt_reset()` within the timeout, the TWDT triggers a panic or reset.

### 9.7 GPIO Initialisation Order

At boot, before any other peripheral is initialised, the firmware shall:
1. Configure all channel SPDT relay output GPIOs and the arm relay output GPIO (GPIO 47) as outputs, driven to inactive state (all relays de-energised, NC/safe position).
2. Only then proceed to initialise ESP-NOW, display, and other peripherals.

This ensures that even if initialisation of a later peripheral crashes, the relay outputs are already in the safe state (de-energised, NC/continuity position).

### 9.8 Brown-Out Detection

The ESP32-S3 hardware brown-out detector (BOD) SHALL be configured with a threshold above the minimum operating voltage of the relay driver circuitry. The expected inrush current from relay coil activation SHALL be documented in the hardware design. The battery SHALL be sized to supply this current without dipping below the BOD threshold. If a brown-out reset occurs, the system boots to safe state per §9.7.

### 9.9 Struct Packing Verification at Boot

At boot, before any communication is attempted, the firmware SHALL execute a runtime self-test that verifies field offsets of all packed message structs using `offsetof()`. For each struct, the test SHALL confirm that every field is at its expected byte offset (as defined in §6.3.3 and Appendix A). If any offset is incorrect, the firmware SHALL transition to ERROR state. This catches toolchain or compiler-configuration issues that could silently corrupt message parsing. Both `#pragma pack(push, 1)` and `__attribute__((packed))` SHALL be used on all message structs for compiler portability.

### 9.10 FreeRTOS Task Priorities

All FreeRTOS tasks SHALL be assigned priorities according to the following table. Safety-critical tasks run at higher priority than UI tasks to prevent display refresh or buzzer patterns from starving relay control or heartbeat processing. The watchdog SHALL be fed from the highest-priority safety task.

**Base unit tasks:**

| Task | Priority | Core | Stack (bytes) | Description |
|---|---|---|---|---|
| `espnow_rx` | 8 | any | 4096 | ESP-NOW receive worker — drains the radio queue and hands frames to `rlc_link`. Above every other task so the receive path can never be starved by one of its own consumers. Deliberately NOT TWDT-registered (§9.6): it blocks on `portMAX_DELAY` with nothing to do between frames, so it cannot feed a watchdog on a quiet link. |
| `arm_switch_task` | 7 (highest safety) | 0 | 4096 | Arm relay feedback (GPIO 21) + key switch sense (GPIO 42) debounce polling (10 ms). Both inputs use independent 16-bit debounce engines. |
| `rlc_link` | 6 | 0 | 4096 | Link manager: PING/PONG response, link-loss detection, frame validation. (Named `heartbeat_task` in this table before v1.44; priority is 6, not 5, so that it drains ahead of `continuity_task`.) |
| `continuity_task` | 5 | 0 | 4096 | ADC continuity sampling, band classification |
| `state_machine_task` | 4 | 0 | 8192 | Base FSM, command processing, relay control |
| `battery_task` | 3 | 0 | 2048 | Battery ADC sampling (1000 ms) |
| `status_update_task` | 3 | 0 | 4096 | Periodic and event-driven STATUS_UPDATE |
| `siren_task` | 2 | 1 | 2048 | Siren pattern generation |
| `rgb_led_task` | 1 (lowest) | 1 | 2048 | RGB LED pattern engine |

**Remote unit tasks:**

| Task | Priority | Core | Stack (bytes) | Description |
|---|---|---|---|---|
| `espnow_rx` | 8 | any | 4096 | ESP-NOW receive worker — see the base table. |
| `fire_button_task` | 7 (highest safety) | 0 | 2048 | Fire button debounce, fresh-press detection |
| `arm_switch_task` | 6 | 0 | 2048 | Arm switch debounce polling (10 ms) |
| `rlc_link` | 6 | 0 | 4096 | Link manager: PING send, PONG validation, link-loss detection. (Named `heartbeat_task` before v1.44; priority is 6, not 5.) |
| `state_machine_task` | 4 | 0 | 8192 | Remote FSM, command sending, ACK handling |
| `cmd_fire_repeat_task` | 4 | 0 | 2048 | Repeated CMD_FIRE at 200 ms during PRE_FIRE/FIRING |
| `battery_task` | 3 | 0 | 2048 | Battery ADC sampling |
| `encoder_task` | 3 | 0 | 4096 | Rotary encoder button polling (the quadrature decode itself is in the GPIO ISR) |
| `display_task` | 2 | 1 | 8192 | Display refresh, partial updates |
| `buzzer_task` | 1 | 1 | 2048 | Buzzer pattern player |
| `rgb_led_task` | 1 (lowest) | 1 | 2048 | RGB LED pattern engine |

Priority values are relative (FreeRTOS: higher number = higher priority). Exact values may be adjusted during implementation, but the relative ordering SHALL be preserved. Safety-critical tasks (arm switch, fire button, link) SHALL always run at higher priority than UI tasks (display, buzzer, LED).

**v1.44:** these tables were audited against the built firmware and corrected — the `espnow_rx` worker was missing entirely, the link manager was listed under an old name at the wrong priority, and the remote's encoder task stack was understated. In firmware, `buzzer_task` had drifted to an unpinned priority 5, i.e. a UI task above the safety FSM, in direct violation of the SHALL above; it was moved back to 1 / core 1 in 1.1.9 rather than the spec being relaxed to match.

### 9.11 Runtime Logging

Runtime logging is **disabled by default** and SHALL be enabled via a compile-time Kconfig option (`CONFIG_RLC_SERIAL_DEBUG_LOGGING`). When enabled, both units SHALL output structured log messages on UART0 at **115200 baud** (8N1) during operation. When disabled, no UART logging output is produced, reducing CPU overhead and UART buffer usage in field operation.

When enabled, log messages SHALL use the following format:

```
[timestamp_ms] [LEVEL] [module] message
```

Where `timestamp_ms` is `esp_timer_get_time() / 1000`, `LEVEL` is one of `E` (error), `W` (warning), `I` (info), `D` (debug), and `module` is the component name (e.g., `FSM`, `COMMS`, `RELAY`, `CONT`, `ADC`).

Log levels SHALL be compile-time configurable via Kconfig (`CONFIG_RLC_LOG_LEVEL`). Default: `I` (info) for release builds, `D` (debug) for development builds. Safety-critical events (state transitions, relay operations, disarm events, errors) SHALL always be logged at `I` or higher and cannot be suppressed below `W`.

### 9.12 ISR Safety Rules

All hardware timer callbacks and interrupt service routines SHALL use only ISR-safe FreeRTOS API variants (e.g., `xTaskNotifyFromISR()`, `xQueueSendFromISR()`, `xSemaphoreGiveFromISR()`). No mutexes, blocking calls, or direct function calls to state machine logic are permitted in ISR context. GPIO reads and writes are ISR-safe on ESP32-S3.

### 9.13 Boot Sequence

Both units SHALL execute the following initialisation sequence in order. If any step marked as mandatory fails (after retries where specified), the unit transitions to ERROR state.

| Step | Action | Mandatory | Notes |
|------|--------|-----------|-------|
| 0 | **Reconfigure the TWDT to `WATCHDOG_TIMEOUT_S`** — before any task is created | Yes | §9.6. See the ordering requirement below. |
| 1 | Configure all channel SPDT relay output GPIOs and arm relay output GPIO (GPIO 47) to safe (inactive / de-energised) state | Yes | §9.7 — before any other peripheral |
| 2 | Verify packed struct field offsets (`offsetof()` checks) | Yes | §9.9 |
| 3 | Verify CRC32-C test vector | Yes | §6.2.2 |
| 4 | Initialise ADC calibration | Yes | §5.4.7, §5.4.2 |
| 5 | Initialise ESP-NOW, set PMK, register peer | Yes | §6.2.1, §6.2.3 — retry 3× on failure |
| 6 | Initialise display, read-back display ID | Yes (remote only) | §5.5.6 |
| 7 | Configure all input GPIOs (including arm relay feedback §5.4.3 and key switch sense §5.4.3b), start debounce engine | Yes | §5.3 |
| 8 | *(was: configure watchdog — moved to step 0, see below)* | — | §9.6 |
| 9 | Start FreeRTOS tasks | Yes | §9.10 |
| 10 | Begin link establishment (LINK_REQUEST / wait for link) | Yes | §6.4.1 |
| 11 | **Subscribe `app_main` to the TWDT**, immediately before entering its housekeeping loop | Yes | §9.6 |

**TWDT ordering requirement (v1.31, review finding N3 — Critical).**

`esp_task_wdt_reconfigure()` **rebuilds the watchdog's subscriber list.** Every task that has already called `esp_task_wdt_add(NULL)` loses its subscription. The reconfigure therefore SHALL happen at **step 0**, before any task exists — not at the old step 8.

Getting this wrong is not a degraded-diagnostics problem, it is a reboot loop. The remote called it at step 8, after `display_start_task()`:

- the display task's subscription was silently dropped;
- its `esp_task_wdt_reset()` then logged `task not found` at the full 50 ms frame rate;
- the now-unfed watchdog triggered at 11.4 s and **panicked inside its own report path** (`LoadProhibited`) while walking stale entries;
- the unit rebooted, and did so on every boot.

It also silently voided the §9.6 coverage the display and buzzer tasks were given specifically so a hung SPI transaction could not freeze an "ARMED"/"PRE-FIRE" screen forever.

Subscribing `app_main` is deliberately **split out to step 11** rather than done at step 0: the initialisation between them (SPI display bring-up, NVS, Wi-Fi start, up to three 500 ms peer-registration retries) can exceed `WATCHDOG_TIMEOUT_S` on its own, so subscribing early would trade one spurious panic for another.

Spawned tasks continue to self-register at task entry (§9.6). The two rules compose: **reconfigure first, then start tasks, and let each task add itself.**

---

## 10. Display Specification

### 10.1 Display Hardware

See §5.5.6 for hardware details.

### 10.2 Screen Layouts

#### 10.2.0 Display Colour Constants

All display colours shall use the following RGB888 values (adjustable during implementation). The driver transmits these as RGB666 to the ILI9488.

| Name | RGB888 | Usage |
|---|---|---|
| Blue (continuity GOOD) | (0, 120, 255) | Continuity GOOD filled circle (●). Blue is used instead of green for red-green colour-blind accessibility (~8% of males). |
| Red (continuity OPEN / error) | (255, 0, 0) | Continuity OPEN circle (○), error text |
| ~~Orange (continuity SHORT)~~ | ~~(255, 140, 0)~~ | **Retired 2026-08-21** with the SHORT band and its diamond glyph |
| Yellow (continuity MARGINAL / warning) | (255, 220, 0) | Continuity MARGINAL triangle (▲), warning text |
| Cyan (selected) | (0, 220, 255) | Selected channel highlight |
| Red background (armed) | (180, 0, 0) | Armed channel background |
| White (default text) | (255, 255, 255) | Normal text |
| Dark background | (0, 0, 0) | Screen background |

**As-built deviation (recorded 2026-08-27):** the implemented continuity palette is a green/yellow family, not the blue-based table above — dark green CONNECTED (●), light green MARGINAL (▲), yellow OPEN (○) — matching `RLC_COLOR_CONT_*` in `rlc_config.h` and the README. Colour-blind accessibility is carried by the shape coding (●/▲/○) rather than by the blue-for-CONNECTED substitution.

The display shall support the following screens, determined by the remote FSM state.

#### 10.2.1 Splash Screen (BOOT / LINKING)

```
┌──────────────────────────────────────────────────┐
│                                                  │
│         ESP32 WIRELESS ROCKET LAUNCH             │
│              CONTROLLER  v1.0.0                  │
│                                                  │
│              Connecting to base...               │
│              Attempt 3 / 5                       │
│                                                  │
│              ████████░░░░░░░░░░░░  40%           │
│                                                  │
└──────────────────────────────────────────────────┘
```

If a firmware version mismatch is detected:

```
┌──────────────────────────────────────────────────┐
│                                                  │
│         ESP32 WIRELESS ROCKET LAUNCH             │
│              CONTROLLER  v1.0.0                  │
│                                                  │
│          ⚠  FIRMWARE MISMATCH  ⚠                 │
│                                                  │
│          Base:   v1.1.0                          │
│          Remote: v1.0.0                          │
│                                                  │
│          Reflash both units with                 │
│          matching firmware.                      │
│                                                  │
└──────────────────────────────────────────────────┘
```

#### 10.2.2 Main Status Screen (IDLE)

```
┌──────────────────────────────────────────────────┐
│ RSSI: -45 dBm  ████▌    BATT: 12.3V  ████▌     │
│ Link: OK  Ping: 12ms    Base: 11.8V  ████       │
├──────────────────────────────────────────────────┤
│                                                  │
│   1 ● ── 2 ● ── 3 ▲ ── 4 ○                     │
│                  MARGINAL                        │
│   5 ○ ── 6 ○ ── 7 ● ── 8 ◆                     │
│                            SHORT                 │
│   ● = good  ▲ = marginal  ○ = open  ◆ = short   │
│                                                  │
│  SEL CH 1   BASE SAFE   REMOTE ARMED             │
│                                                  │
│        TURN ARM KEY TO ARM CH 1                  │
└──────────────────────────────────────────────────┘
```

Key elements:
- **Top bar:** RSSI with graphical bar (averaged over last 3 frames), ping round-trip time, remote battery voltage with bar, base battery voltage with bar, link status indicator.
- **Channel grid:** all 8 channels displayed with continuity band indicators. Filled circle (●) = CONNECTED. Triangle (▲) = MARGINAL (with label). Empty circle (○) = OPEN. The selected channel is highlighted with a `►[ CH N ]◄` cursor. Shape carries the meaning as well as colour, so the grid stays readable regardless of colour vision. **Three bands since 2026-08-21** — the diamond (◆) that marked SHORT is retired with that band (§5.4.2). As-built colours are dark green / light green / yellow, not the blue-based palette above; see the §10.2.0 deviation note.
- **Status area:** base key switch state (§5.4.3b), arm relay feedback (§5.4.3), remote arm switch state.
- **Instruction text:** context-sensitive prompt guiding the operator.

Colour coding:
- Continuity GOOD: blue (colour-blind accessible — avoids red-green ambiguity).
- Continuity MARGINAL: yellow (with "MARGINAL" label below the channel indicator).
- Continuity OPEN: red.
- Continuity SHORT: orange (with "SHORT" label below the channel indicator).
- Selected channel: cyan highlight.
- Armed channel: red background.
- Warning text: yellow.
- Error text: red.

**Base arm state (four states).** The status line's `BASE` field is derived from *two* distinct signals — the key switch (§5.4.3b, GPIO 42) and the arm sense (§5.4.3, GPIO 21) — because they answer different questions and collapsing them loses the one that matters.

| Key sense | Arm sense | Shown | Colour | Meaning |
|---|---|---|---|---|
| OFF | LOW | `SAFE` | green | Key off, fire path dead |
| ON | LOW | `READY` | amber | Key turned, path still dead — arming permitted |
| any | HIGH, in ARMED/PRE_FIRE/FIRING | `ARMED` | red | Arm relay closed, VBAT live on the fire path |
| any | HIGH, in any other state, **or** `ERR_RELAY_FAULT` | `WELD!` | flashing red/yellow | Contacts closed when they should not be |
| — | no fresh STATUS_UPDATE | `?` | grey | Unknown — never displayed as SAFE |

**`ARMED` and `WELD!` are driven by the arm sense, never by the key switch.** A welded arm relay leaves the fire path live with the key turned OFF; a display keyed off the key switch would print `SAFE` over an energised igniter circuit. The `WELD!` case is additionally checked against `base_state` rather than waiting for the base's own weld confirm count, so the warning appears earlier than `ERR_RELAY_FAULT`.

`REMOTE` reflects the remote's own arm switch and is local, so it stays valid even with the link down.

Covered by host tests T-M01…T-M07 (§15.5).

#### 10.2.3 Armed Screen (ARMED)

```
┌──────────────────────────────────────────────────┐
│ RSSI: -48 dBm  ████       BATT: 12.2V  ████    │
│ Link: OK                   Base: 11.7V  ████    │
├──────────────────────────────────────────────────┤
│                                                  │
│            ╔══════════════════════╗               │
│            ║   CHANNEL 3 ARMED   ║               │
│            ║                     ║               │
│            ║  CONTINUITY GOOD    ║               │
│            ║                     ║               │
│            ║                     ║               │
│            ║ HOLD FIRE TO LAUNCH ║               │
│            ╚══════════════════════╝               │
│                                                  │
│     ARM SENSE OK   REMOTE ARMED                  │
│                                                  │
└──────────────────────────────────────────────────┘
```

Red border, pulsing/flashing. Large channel number.

#### 10.2.4 Firing Screen (PRE_FIRE / FIRING)

```
┌──────────────────────────────────────────────────┐
│ RSSI: -50 dBm  ████       BATT: 12.1V  ███     │
├──────────────────────────────────────────────────┤
│                                                  │
│            ██████████████████████████             │
│            █                        █             │
│            █    FIRING CH 3         █             │
│            █                        █             │
│            █   Pre-fire: 3.2s       █             │
│            █     — or —             █             │
│            █   IGNITION ACTIVE      █             │
│            █                        █             │
│            ██████████████████████████             │
│                                                  │
│         HOLD FIRE BUTTON — RELEASE TO ABORT      │
│                                                  │
└──────────────────────────────────────────────────┘
```

Full red background or large red indicator. Shows pre-fire countdown (decrementing) or "IGNITION ACTIVE" during fire pulse.

#### 10.2.4a Fire Complete Screen

```
┌──────────────────────────────────────────────────┐
│ RSSI: -48 dBm  ████       BATT: 12.2V  ████    │
├──────────────────────────────────────────────────┤
│                                                  │
│            ╔══════════════════════╗               │
│            ║   FIRE COMPLETE     ║               │
│            ║   CHANNEL 3         ║               │
│            ║                     ║               │
│            ║ ○ OPEN - LIKELY     ║               │
│            ║      FIRED          ║               │
│            ║   CLEARS IN 8.2s    ║               │
│            ╚══════════════════════╝               │
│                                                  │
└──────────────────────────────────────────────────┘
```

Green/yellow border. Shown after STATUS_UPDATE confirms the base in POST_FIRE or
IDLE, then auto-transitions to the main status screen.

**Duration — `FIRE_COMPLETE_SCREEN_MS` (10000 ms), amended 2026-08-27 (fw
1.1.22, raised again to 10 s in 1.1.23).** This was `POST_FIRE_COOLDOWN_MS`
(2000 ms), which the operator found too brief to read; 5 s was still not long
enough to read the igniter status and act on it. The two are now separate constants **deliberately**:
`POST_FIRE_COOLDOWN_MS` is a fire-path parameter governing how long the *base*
holds POST_FIRE before accepting another arm, and it remains at 2000 ms.
Lengthening the screen by reusing it would have extended the base's cooldown as
a side effect — fire-path constants are changed here by explicit decision, not
incidentally.

**The screen is cancelled early if the remote FSM enters ARMED, PRE_FIRE or
FIRING.** Because the screen now outlives the base's cooldown, the operator can
re-arm while it is still displayed; a summary of the last shot must never be
shown over a live pad. The `fire_done` branch outranks the FSM-derived screen,
so without this the display would have shown FIRE COMPLETE during a live ARMED
state.

**Countdown wording is "CLEARS IN", not "Returning to IDLE in".** After ~2000 ms
the base genuinely is IDLE while the screen is still up, so an "IDLE IN"
countdown would state something untrue. The screen can only promise when it
will clear itself.

**Igniter status line (added 2026-08-27, fw 1.1.22).** The screen continuously
shows the continuity band of the channel just fired, with the same shape-plus-
colour coding as the §10.2.2 channel grid, refreshed every frame:

| Band | Shown | Colour | Meaning |
|---|---|---|---|
| OPEN | `○ OPEN - LIKELY FIRED` | green | Circuit opened — the expected outcome of a good igniter burning through |
| MARGINAL | `▲ MARGINAL - CHECK` | yellow | Partial continuity remains |
| CONNECTED | `● STILL CONNECTED` | red | Current can still flow — usually a misfire |
| no fresh status | `IGNITER ?` | grey | Not known; never guessed |

The wording describes the **measurement**, not a verdict on the igniter — the
same reason the band is labelled CONNECTED rather than GOOD (§5.4.2). OPEN says
the circuit opened; it cannot distinguish a burned igniter from a lead that
fell off, so "LIKELY FIRED" is as far as the reading supports. This is the
operator-facing half of T-S19.

#### 10.2.5 Link Lost Screen

```
┌──────────────────────────────────────────────────┐
│                                                  │
│          !  LINK LOST  !                         │
│                                                  │
│     No response from base unit                   │
│                                                  │
│     All channels disarmed (assumed)              │
│                                                  │
│     Last contact: 4 s ago                        │
│                                                  │
│     Attempting to reconnect...                   │
│                                                  │
│     Attempts 7   RSSI -45 dBm                    │
└──────────────────────────────────────────────────┘
```

Layout and wording as built; strings are abbreviated to fit 40 characters at
the §10.3 scale-2 font floor.

Yellow/amber background.

**Field sources (corrected 2026-08-19).** Both dynamic fields must come from
counters that keep advancing *after* the link has dropped:

| Field | Source | Note |
|---|---|---|
| "Last contact: N s ago" | `rlc_link_status_t.ms_since_contact` | Real elapsed time since the last well-formed frame from the peer. Switches to minutes past 600 s. |
| "Attempts N" | `rlc_link_status_t.linkreq_attempts` | LINK_REQUEST retries, which advance while reconnecting. |

`missed_pings` must **not** be used for either. It is a consecutive-miss
counter whose update path is gated behind `s_state == RLC_LINK_STATE_LINKED`,
so it stops the instant the link is declared lost and freezes at
`HEARTBEAT_FAIL_THRESHOLD`. Deriving elapsed time from it produced a display
permanently stuck at "1 s ago" (3 misses x 500 ms), and an attempt count stuck
at 3. Verified on target: across a 50 s induced outage `missed_pings` held at 3
while `ms_since_contact` advanced 2354 → 47354 ms and `linkreq_attempts` went
1 → 23.

#### 10.2.6 Error Screen

```
┌──────────────────────────────────────────────────┐
│                                                  │
│            ✖  ERROR  ✖                            │
│                                                  │
│     [Error description text]                     │
│                                                  │
│     System halted. Power cycle required.         │
│                                                  │
└──────────────────────────────────────────────────┘
```

#### 10.2.7 NACK Display

When a NACK is received, the human-readable text from the NACK reason code table (§6.3.3) shall be displayed as an overlay or toast notification on the current screen for 3 seconds, with a red background. Example: "ARM FAILED: BASE KEY OFF".

### 10.3 Display Update Strategy

- Use partial refresh (dirty-rectangle) for dynamic elements (RSSI, voltages, ping time, continuity indicators).
- Full screen redraw only on state transitions (IDLE → ARMED, etc.).
- Target display refresh rate: ≥ 5 Hz for dynamic elements, immediate for state transitions.
- Pre-fire countdown shall update every 100 ms (smooth countdown display).

---

## 11. RGB LED Status Specification

Both units carry a WS2812 (NeoPixel) 8-pixel addressable strip on GPIO 48, driven via the ESP32-S3 RMT peripheral. Each DevKit's on-board WS2812 sits **in parallel** on the same data line and therefore mirrors pixel 0; it carries no independent meaning.

### 11.0 Principle

The strip is an **igniter continuity display**: one pixel per channel, showing the same continuity bands as the remote's channel grid, in the same colours (§10.2.0, `RLC_COLOR_CONT_*`). System status **modulates** that map rather than replacing it, so the operator never loses sight of the pad. The only exceptions are the firing path and ERROR, which take the whole strip so those signals stay unmistakable.

**Pixel order.** The two strips are wired data-in at **opposite ends**, so `RLC_STRIP_REVERSED` is set per unit (verified 2026-08-19 with `tools/strip-diag`):

| Unit | Data-in end | Mapping | `RLC_STRIP_REVERSED` | On-board LED shows |
|---|---|---|---|---|
| Base | channel 1 | channel N → pixel `N-1` | 0 | channel 1 |
| Remote | channel 8 | channel N → pixel `7-(N-1)` | 1 | channel 8 |

### 11.1 Rendering layers

Highest active layer wins. Identical on both units except where noted.

| # | Layer | Rendering | Condition |
|---|---|---|---|
| 1 | Firing path | Whole strip red — ARMED slow blink (500 ms on/off), PRE_FIRE fast blink (100 ms on/off), FIRING solid | FSM in ARMED / PRE_FIRE / FIRING |
| 2 | ERROR | Red triple flash (100on/100off/100on/100off/100on/700off); the channel map is shown dimmed to `RLC_STRIP_ERROR_DIM_PCT` during the 700 ms gap so pad state stays readable during a fault | FSM in ERROR, or firmware version mismatch |
| 3 | Alarm wink | Full-strip flash of `RLC_STRIP_ALARM_WINK_MS` every `RLC_STRIP_ALARM_PERIOD_MS`, over a map that otherwise runs normally. Several concurrent alarms **alternate colours on successive winks** | any alarm class raised (§11.2) |
| 4 | Stale data | Whole map dimmed to `RLC_STRIP_STALE_DIM_PCT` — last known, not live | **Remote only:** cached STATUS_UPDATE older than 2 × `STATUS_UPDATE_INTERVAL_MS` |
| 5 | Breathing | Map alternates full / `RLC_STRIP_BREATHE_LOW_PCT` every `RLC_STRIP_BREATHE_MS`. **Base:** whole map. **Remote:** selected channel only | **Base:** key switch in ARM with nothing armed. **Remote:** arm switch ON |
| 6 | Channel map | One pixel per channel in the continuity colours; the channel of interest pulses full / `RLC_STRIP_CURSOR_LOW_PCT` every `RLC_STRIP_CURSOR_MS` (**base:** armed or firing channel; **remote:** encoder cursor) | default — includes BOOT, LINKING, IDLE, LINK_LOST and POST_FIRE |

Layers 3 and 4 compose: STATUS_UPDATE can be late while the link is healthy, so dim means "the data is old" and a wink means "something is wrong".

Before the first continuity data arrives, a **cyan left-to-right chase** (`RLC_COLOR_STRIP_BOOT`, `RLC_STRIP_CHASE_MS` per step) runs in place of the map — clearly "not ready", clearly not a channel state.

POST_FIRE deliberately renders as the plain map: the fired channel flips GOOD → OPEN within one continuity sweep (~800 ms), inside the 2000 ms cooldown, making the post-fire igniter check (T-S19) readable at a glance.

### 11.2 Alarm classes

Colours are chosen to be unmistakable for any continuity colour, so a wink can never be read as a channel state.

| Class | Colour | Base raises when | Remote raises when |
|---|---|---|---|
| `RLC_ALARM_LINK_LOST` | Amber `#FFB400` | link state != LINKED | link state != LINKED |
| `RLC_ALARM_BATTERY` | Magenta `#FF00FF` | own pack below `BASE_VBAT_MIN_ARM_MV` | own pack below `REMOTE_VBAT_MIN_ARM_MV`, **or** base pack below `BASE_VBAT_MIN_ARM_MV` as reported in a fresh STATUS_UPDATE |
| `RLC_ALARM_ARM_FAULT` | White `#FFFFFF` | `ERR_RELAY_FAULT` set | `ERR_RELAY_FAULT` in a fresh STATUS_UPDATE |

A 0 mV reading means the ADC has not yet produced a sample (or, on the remote, that the battery divider is unfed) and does **not** raise the battery alarm.

There is no per-missed-ping strip indication. The 80 ms buzzer beep (§6.4.2) remains the per-miss indicator, and the remote display carries RSSI and ping RTT; sustained failures raise the link alarm above.

### 11.3 Implementation

- Driver: `rlc_common/src/rlc_rgb_led.c`, WS2812 via the ESP32-S3 RMT peripheral. The renderer is **unit-agnostic** — both units run the same layer resolver and differ only in what they feed it.
- GPIO: 48 (fixed, on-board, `RGB_LED_GPIO`).
- Brightness: per unit, `RGB_LED_BRIGHTNESS_BASE` / `RGB_LED_BRIGHTNESS_REMOTE` (default 30 of 255). Eight pixels draw roughly 55 mA at that setting, scaling linearly.
- Pattern engine: a FreeRTOS task at priority 1 (§9.10), ticking every `RLC_STRIP_FRAME_MS`. All animation phase is derived from `esp_timer_get_time()`, so patterns are stable across frame jitter.
- **Feeds:** `set_channel_bands()`, `set_active_channel()`, `set_alarms()`, `set_stale()`, `set_key_warning()` are published from each unit's **housekeeping loop**, never from an FSM, so the fire path is untouched. The FSMs set only the firing-path and ERROR patterns.
- Torn reads on the feed variables are tolerated by design: the worst case is one frame of stale colour on a status display.
- Renderer behaviour is covered by host-compiled tests (§15.5, `tests/host/run.sh`, T-L01…T-L09) that assert every emitted pixel against a mock strip backend.

---

## 12. Audio Feedback Specification

### 12.1 Buzzer Patterns (Remote only)

The remote uses an active buzzer for audible feedback. Patterns are implemented as a pattern player that accepts a sequence of on/off durations.

| Pattern Name | Sequence (ms) | Usage |
|---|---|---|
| `BEEP_SHORT` | 100 on | Single confirmation beep |
| `BEEP_DOUBLE` | 100 on, 100 off, 100 on | Arm confirmed |
| `BEEP_TRIPLE` | 100 on, 80 off, 100 on, 80 off, 100 on | Error / NACK received |
| `BEEP_LONG` | 500 on | Disarm event |
| `BEEP_PING_FAIL` | 80 on | Link quality degraded — played once on the transition into `ERR_COMM_DEGRADED` (rising edge), NOT once per missed ping. At a 500 ms heartbeat a per-ping beep would be a continuous rattle during exactly the condition the operator needs to hear other alerts through. (Semantics pinned v1.44; the pattern existed but was never played before firmware 1.1.9.) |
| `BEEP_CONTINUITY_LOST` | 200 on, 100 off, 200 on, 100 off, 200 on | Continuity → OPEN disarm (distinctive pattern) |
| `ALARM_LINK_LOST` | 200 on, 200 off, repeating | Link lost alarm |
| `ALARM_CRITICAL` | 100 on, 100 off, repeating | Critical error alarm |
| `ALARM_ARMED` | 80 on, 1120 off, repeating | **State tone: ARMED.** ~0.8 Hz heartbeat. Added 2026-08-27 (fw 1.1.27) |
| `ALARM_FIRING` | 90 on, 160 off, repeating | **State tone: PRE_FIRE and FIRING.** ~4 Hz. Added 2026-08-27 (fw 1.1.27) |

**State tones (added 2026-08-27, fw 1.1.27).** Before this the remote was silent
throughout ARMED and FIRING — the operator had only the display and the ring
LED, and the base siren covers the pad rather than the operator, who may not be
looking at the panel.

The tempo gap between the two is the point: the step into the firing sequence
must be unmistakable by ear without looking. `ALARM_ARMED` is deliberately
sparse rather than urgent — it may sound for the full 10 s arm window, and both
pre-existing repeating patterns (`ALARM_LINK_LOST`, `ALARM_CRITICAL`) are ~2.5 Hz
*fault* patterns, so an urgent armed tone would read as "something is wrong"
when nothing is. PRE_FIRE shares `ALARM_FIRING` because the countdown is the
part the operator must hear starting, and it is where an abort is still free.

**These are background patterns, not played patterns.** A repeating pattern
sounds only until the next `buzzer_play()` replaces it, and ARMED is full of
one-shots — the arm-confirm `BEEP_DOUBLE` and a `BEEP_TRIPLE` from every §8.2.4
FIRE guard refusal. A played tone would be silenced by the first refusal and
never return. `buzzer_set_background()` is re-entered whenever nothing else is
sounding, and is idempotent so it can be driven from the FSM tick rather than
on transitions — necessary because the base dropping out underneath an ARMED
remote arrives as a `STATUS_UPDATE`, not a local state change, and a missed
transition would leave the remote sounding armed when it is not.
`buzzer_stop()` clears the background as well as silencing.

### 12.2 Siren Patterns (Base only)

| Pattern Name | Behaviour | Usage |
|---|---|---|
| `SIREN_ARMED` | Continuous ON | Channel armed — audible pad warning (pulse removed v1.35, see §12.3) |
| `SIREN_PRE_FIRE` | Continuous ON | Pre-fire countdown — clear the pad |
| `SIREN_FIRING` | Continuous ON | Active ignition |
| `SIREN_LINK_LOST` | 500 ms on, 500 ms off, 4 cycles | Link lost alert |
| `SIREN_ERROR` | 200 ms on, 200 ms off, 3 cycles, then silence | ERROR state entry — audible pad alert |
| `SIREN_CONTINUITY_LOST` | 200 ms on, 200 ms off, 3 cycles, then silence | Continuity-loss disarm — distinctive alert |

### 12.3 Implementation

Buzzer patterns shall be driven by a dedicated FreeRTOS task (`buzzer_task`) in `rlc_remote` that consumes pattern descriptors from a queue. New patterns preempt active ones.

Siren control is managed directly by the base state machine. The siren SHALL be driven **continuously ON from entry to ARMED, through PRE_FIRE and FIRING, without interruption**. On disarm or transition to POST_FIRE/IDLE, the siren is set OFF.

> **The 500 ms ARMED pulse was removed in v1.35 (2026-08-26), on-target finding.** Chopping the siren's supply at 1 Hz interferes with the siren's *own* internal modulation: the device restarts its sweep on every power-up edge and never reaches the loud part of its cycle, so the pulsed ARMED warning was **less** audible than a steady tone, not more. The pattern was inherited from v1.1, when the base still had a simple buzzer for which on/off gating was the only available modulation. Continuous is now the ARMED warning, and the siren's own sweep provides the attention-getting variation.
>
> Consequences to keep in mind:
> - ARMED is bounded by `ARM_TIMEOUT_MS` (10 s), so the continuous burst is short-lived by construction.
> - Base battery draw during ARMED roughly doubles versus the old 50 % duty cycle. Immaterial over 10 s.
> - The siren flyback diode (§5.4.8) now switches once per sequence instead of once per second, which removes the repetitive-avalanche concern the 1 Hz pattern raised.
>
> LINK_LOST and ERROR remain **patterned** (500/500 × 4 cycles, and 3 × 200 ms blasts). Those patterns carry meaning — they distinguish a fault from an armed pad — and they are short enough that the modulation interference does not matter.

**Pattern cancellation (added v1.31, review finding N2).** The pulsing patterns run on an `esp_timer` callback while every pattern change is made from task context. `esp_timer_stop()` does **not** cancel a callback that has already been dispatched — only `esp_timer_delete()` waits, and that cannot be called from a path holding the lock the callback wants. Serialising the pattern state under a mutex is therefore **not sufficient**: a callback can be parked on the lock while a task reconfigures the pattern underneath it, and then act on the new state as if it were the old one.

The implementation SHALL therefore mark whether a periodic pattern is genuinely running, and a callback whose pattern has been superseded SHALL return without touching the siren output. `siren_off()` SHALL additionally clear the pattern's remaining cycle count.

Both failures this prevents were reachable in firmware 1.1.0 and neither is self-correcting:

| Failure | Mechanism |
|---|---|
| **Siren stuck ON after disarm** | `siren_off()` left the cycle count at −1 (infinite, from the ARMED pattern). The stale callback toggled the output back ON — with the timer now stopped, nothing ever turned it off again. |
| **Siren silent through the PRE_FIRE countdown** | `SIREN_PRE_FIRE` sets the cycle count to 0 (steady ON, no timer). A callback left over from the ARMED pattern read 0 as "pattern finished" and drove the siren OFF — no audible pad warning for the full 2 s countdown. ARMED→PRE_FIRE is *always* preceded by a running 500 ms timer, so this window opens on every launch. |

---

## 13. Error Handling

### 13.1 Error Flags

The `error_flags` field in `STATUS_UPDATE` is a bitmask:

| Bit | Flag | Description |
|---|---|---|
| 0 | `ERR_VBAT_LOW` | Base battery below `VBAT_MIN_ARM_MV` |
| 1 | `ERR_VBAT_CRITICAL` | Base battery below `VBAT_CRITICAL_MV` |
| 2 | `ERR_RELAY_FAULT` | Arm relay fault: either (a) arm sense input does not confirm VBAT on fire path when arm relay is commanded ON (possible wiring fault, blown fuse, or relay failure), or (b) arm sense reads HIGH when arm relay is known to be de-energised (contact welding — critical hardware fault). |
| 3 | Reserved (was `ERR_CONTINUITY_LOST_WHILE_ARMED`) | Previously: continuity band transitioned to OPEN on the armed channel. Removed in v1.8; the v1.35 continuity-loss disarm is carried as an FSM event (§7.3.1), not as this flag. |
| 4 | `ERR_COMM_DEGRADED` | > 30% ping failure rate in last 10 pings |
| 5 | `ERR_WATCHDOG_RESET` | Set if last boot was from watchdog reset |
| 6 | `ERR_INTERNAL` | Software assertion failure |
| 7 | Reserved | |

### 13.2 Error Handling Behaviour

| Error | Severity | Action |
|---|---|---|
| `ERR_VBAT_LOW` | Warning | Display warning on remote. NACK arm commands (reason 0x09). |
| `ERR_VBAT_CRITICAL` | Critical | Immediate disarm (complete fire pulse if FIRING). Transition to ERROR. |
| `ERR_RELAY_FAULT` | Critical | Immediate disarm. Transition to ERROR. |
| *(Bit 3 reserved — see §13.1)* | — | — |
| `ERR_COMM_DEGRADED` | Warning | Display warning on remote (yellow indicator). |
| `ERR_WATCHDOG_RESET` | Info | Set flag in first STATUS_UPDATE so remote can display "Base rebooted". |
| `ERR_INTERNAL` | Critical | Immediate disarm. Transition to ERROR. |

**Multiple simultaneous errors:** When multiple error flags are active simultaneously, the most severe error determines the system state transition (Critical > Warning > Info). All active error flags SHALL be displayed on the remote and logged to UART.

### 13.2a Error Flag Presentation

A raw bitmask is not actionable in the field, so every flag has a canonical short name. The remote shows `BASE ERROR 0x<hex>: <NAME>`, and the base logs the same names alongside the hex value.

| Bit | Flag | Displayed name |
|---|---|---|
| 0 | `ERR_VBAT_LOW` | `VBAT LOW` |
| 1 | `ERR_VBAT_CRITICAL` | `VBAT CRITICAL` |
| 2 | `ERR_RELAY_FAULT` | `RELAY FAULT` |
| 3 | *(reserved)* | `RESERVED BIT3` |
| 4 | `ERR_COMM_DEGRADED` | `COMM DEGRADED` |
| 5 | `ERR_WATCHDOG_RESET` | `WATCHDOG RESET` |
| 6 | `ERR_INTERNAL` | `INTERNAL FAULT` |
| 7 | *(undefined)* | `UNDEFINED BIT7` |

Every bit 0–7 resolves to a name, including the two that carry no meaning today, so an unexpected flag is reported rather than silently dropped.

**Deviation from §13.2's "stacked" wording.** The main status screen has one line for this, and §10.3's scale-2 minimum font allows 40 characters — enough for one named flag, not several. When more than one flag is set the remote **cycles** them at 2 s intervals with an `(n/total)` counter, e.g. `BASE ERROR 0x06: VBAT CRITICAL (1/2)`. Every active flag is therefore shown, in full, without truncation and without dropping below the legibility floor. The complete comma-separated list is available in the UART log.

The names are defined once in `rlc_protocol.h` (`rlc_error_flag_str()`, `rlc_error_flag_nth()`, `rlc_error_flags_str()`) and covered by host tests T-E01…T-E07 (§15.5).

### 13.3 Assertions

The firmware shall use a custom `RLC_ASSERT(condition)` macro. On assertion failure:
1. Call `relay_all_safe()`.
2. Set `ERR_INTERNAL` flag.
3. If possible, send one final `STATUS_UPDATE`.
4. Transition to ERROR state.
5. RGB LED → red triple flash.

---

## 14. Configuration and Constants

All tuneable parameters shall be defined in a single header file (`rlc_config.h`) for easy adjustment without modifying logic code.

### 14.1 Timing Constants

| Constant | Default Value | Description |
|---|---|---|
| `HEARTBEAT_INTERVAL_MS` | 500 | Ping send interval |
| `HEARTBEAT_TIMEOUT_MS` | 500 | Time to wait for PONG |
| `HEARTBEAT_FAIL_THRESHOLD` | 3 | Consecutive failures before link loss |
| `HEARTBEAT_WINDOW_SIZE` | 10 | Rolling window for link quality calculation |
| `RSSI_AVERAGE_WINDOW` | 3 | Number of frames to average for RSSI display |
| `STATUS_UPDATE_INTERVAL_MS` | 2000 | Periodic status broadcast interval |
| `STATUS_STALE_TIMEOUT_MS` | 5000 | Max time without STATUS_UPDATE before remote disarms |
| `LINK_REQUEST_INTERVAL_MS` | 2000 | Interval between link request retries |
| `LINK_REQUEST_MAX_RETRIES` | 5 | Max retries before "NO LINK" display |
| `LINK_REQUEST_SLOW_INTERVAL_MS` | 2000 | Retry interval after max retries |
| `CMD_ACK_TIMEOUT_MS` | 500 | Timeout waiting for command ACK/NACK |
| `CMD_RETRY_COUNT` | 1 | Number of retries for non-fire commands |
| `FIRE_REPEAT_INTERVAL_MS` | 200 | Interval for repeated CMD_FIRE while button held |
| `FIRE_AUTHORIZATION_TIMEOUT_MS` | 500 | Max time without CMD_FIRE before aborting fire (base). **Rationale:** the 300 ms margin (500 − 200) means two consecutive packet losses (400 ms gap) would trigger a dead-man abort. This is conservative (fail-safe) but may frustrate operators at long range under poor RF conditions. If field experience shows excessive false aborts, consider increasing to 700 ms (tolerates two consecutive losses) or reducing `FIRE_REPEAT_INTERVAL_MS` to 150 ms. |
| `PRE_FIRE_DELAY_MS` | **5000** | Siren warning before ignition. **This value is configurable and must be agreed with the RSO.** Raised from 2000 to 5000 on 2026-08-26 by operator decision, after T-A17 showed 2 s is too short to act inside: the operator could not disconnect an igniter within the countdown and one fired. 5 s gives a usable abort window while staying short enough that holding the fire button does not invite fatigue-release, which is what the original 2000 ms was protecting against. **Both units must be flashed together when this changes** — the remote runs its own local countdown against the same constant. |
| `FIRE_PULSE_DURATION_MS` | 1000 | Igniter current duration. **Safety-relevant parameter** — keep as short as practical. |
| `COMPLETE_PULSE_ON_LINK_LOSS` | true | If true, base completes fire pulse on link loss during FIRING. If false, base immediately cuts fire pulse. See §7.2.5. **Safety-relevant parameter — RSO/operator should choose.** |
| `POST_FIRE_COOLDOWN_MS` | 2000 | Cooldown before returning to IDLE |
| `FIRE_PULSE_BACKSTOP_MARGIN_MS` | 250 | Grace beyond `FIRE_PULSE_DURATION_MS` before the FSM's max-duration backstop synthesises pulse completion. Defence in depth: if the GPTimer notification is ever lost the FSM would otherwise sit in FIRING with the relay energised, and the task watchdog would not catch it (the task keeps feeding). |
| `SIREN_LINK_LOST_DURATION_MS` | 4000 | Siren duration on link loss. Since v1.31 the cycle count is **derived** from this and the 500 ms half-period (4 cycles); it was previously a bare literal, so editing this constant had no effect. |
| `NACK_DISPLAY_DURATION_MS` | 3000 | How long NACK reason text is shown on display |
| `WATCHDOG_TIMEOUT_S` | 5 | Hardware watchdog timeout |
| `ARM_TIMEOUT_MS` | 10000 | Maximum time in ARMED state without CMD_FIRE before auto-disarm |
| `ARM_SENSE_VERIFY_TIMEOUT_MS` | 200 | Time allowed for arm sense (GPIO 21) to read HIGH after the arm relay is energised, before NACK `ARM_SENSE_FAULT` (§7.2.2 step 2). The wait is non-blocking — safety events are still processed inside it. Named in v1.31; previously a bare literal in the FSM. |
| `DEBOUNCE_POLL_INTERVAL_MS` | 10 | Default shift-register poll interval |
| `SPLASH_MIN_DURATION_MS` | 10000 | Minimum time the splash screen stays up, even if the link comes up sooner (linking typically completes in well under a second) |
| `FIRE_PROTECTED_CHANNEL_MASK` | `0xFF` | Bitmask of channels whose continuity ADC input has the bug #18 hardware protection fitted (bit 0 = ch1 … bit 7 = ch8). ARM is NACKed and the channel relay refuses to close for any channel not in this mask. Widened to all 8 channels on 2026-08-23, once the protection BOM was complete (snubber + 2x 1N5819 clamps + 217 Ω sense resistor per channel) |

### 14.2 Voltage Thresholds

| Constant | Default Value | Description |
|---|---|---|
| `BASE_VBAT_DIVIDER_RATIO` | 4.3 | Voltage divider ratio for base battery ADC (33 kΩ + 10 kΩ). Calibrated as-built: 4.3148 (2026-08-19) |
| `BASE_VBAT_MIN_ARM_MV` | 10500 | Minimum base battery to allow arming (mV) |
| `BASE_VBAT_CRITICAL_MV` | 9000 | Critical base battery threshold (mV) |
| `BASE_VBAT_FULL_MV` | 12600 | Full-charge endpoint for the base display battery gauge (3S LiPo) |
| `REMOTE_VBAT_DIVIDER_RATIO` | 2.8 | Voltage divider ratio for remote battery ADC (18 kΩ + 10 kΩ). Calibrated as-built: 2.8211 (2026-08-19) |
| `REMOTE_VBAT_MIN_ARM_MV` | 7000 | Minimum remote battery to allow arming (mV, 3.5V/cell) |
| `REMOTE_VBAT_MIN_OPERATE_MV` | 6600 | Minimum remote battery for operation (mV, 3.3V/cell) |
| `REMOTE_VBAT_CRITICAL_MV` | 6400 | Critical remote battery threshold (mV, 3.2V/cell). Set above 6.0V (3.0V/cell) to protect LiPo cell health and to maintain margin above the regulation stage input requirements. |
| `REMOTE_VBAT_FULL_MV` | 8400 | Full-charge endpoint for the remote display battery gauge (2S LiPo) |

### 14.3 Hardware Configuration

| Constant | Description |
|---|---|
| `NUM_CHANNELS` | 8 |
| `WIFI_CHANNEL` | ESP-NOW Wi-Fi channel (1–13, default: 11) |
| `ESPNOW_PMK` | 16-byte Primary Master Key (hex array) |
| `ESPNOW_LMK` | 16-byte Local Master Key (hex array) |
| `CMD_INTEGRITY_KEY` | 16-byte pre-shared key for CRC32 integrity check |
| `BASE_MAC_ADDR` | 6-byte MAC of the base unit |
| `REMOTE_MAC_ADDR` | 6-byte MAC of the remote unit |
| `ENC_DIVIDER` | 4 — raw quadrature transitions per channel step |
| `ENC_REVERSED` | 1 — rotation sense, set by how A/B are wired on this board |
| `ENC_LOCKOUT_US` | 2000 — lockout after an accepted edge |
| `VBAT_BURST_SAMPLES` | 33 — raw samples per reading, odd so the median is exact |
| `VBAT_BURST_GAP_MS` | 1 — spacing, to decorrelate from supply ripple |
| `VBAT_RAIL_COUNTS` | 4093 — at or above this a sample counts as clipped |
| `RGB_LED_BRIGHTNESS_BASE` / `_REMOTE` | 0–255, default: 30 each |
| `RLC_STRIP_REVERSED` | per unit — 0 (base, data-in at channel 1), 1 (remote, data-in at channel 8) |
| `RLC_STRIP_ALARM_WINK_MS` / `_PERIOD_MS` | 300 / 3000 |
| `RLC_STRIP_STALE_DIM_PCT` / `_ERROR_DIM_PCT` | 10 / 20 |
| `RLC_STRIP_BREATHE_LOW_PCT` / `_CURSOR_LOW_PCT` | 25 / 40 |
| `RLC_STRIP_BREATHE_MS` / `_CURSOR_MS` / `_CHASE_MS` / `_FRAME_MS` | 250 / 500 / 120 / 50 |
| `RLC_COLOR_ALARM_LINK` / `_BATT` / `_FAULT` | `#FFB400` / `#FF00FF` / `#FFFFFF` |
| `RLC_COLOR_STRIP_BOOT` | `#00FFFF` |

### 14.4 Display Configuration

| Constant | Default Value | Description |
|---|---|---|
| `DISPLAY_SPI_HOST` | `SPI2_HOST` | SPI peripheral to use |
| `DISPLAY_SPI_CLOCK_HZ` | 20000000 | SPI clock frequency (20 MHz) |
| `DISPLAY_WIDTH` | 480 | Pixels |
| `DISPLAY_HEIGHT` | 320 | Pixels |
| `DISPLAY_ROTATION` | 1 | Landscape |

### 14.5 Continuity Sensing Configuration

| Constant | Default Value | Description |
|---|---|---|
| `CONT_R_REF_OHM` | 3300 | Total series current-limiting resistance (1.5 kΩ + 1.8 kΩ = 3.3 kΩ). Two fusible resistors in series for defence-in-depth. Limits test current to ≤ 1 mA. |
| `CONT_R_PULL_OHM` | 100000 | Pull-down resistor per channel (Ω). Defines open-circuit ADC voltage. |
| `CONT_SAMPLE_INTERVAL_MS` | 100 | ADC sampling interval per channel (ms) |
| `CONT_OVERSAMPLE_COUNT` | 64 | Number of ADC samples averaged per reading |
| `CONT_ADC_ATTEN` | `ADC_ATTEN_DB_0` | Continuity ADC attenuation, 0 dB (changed from 11 dB in v1.29 when calibration was re-enabled) |
| `CONT_ADC_FULLSCALE_MV` | 950 | ADC full-scale at 0 dB attenuation. An open channel rests at ~3.19 V and saturates below this ceiling — saturated means OPEN, and `CONT_OPEN_UV` must stay below full scale |
| `CONT_RELAY_DROPOUT_MS` | 50 | Relay dropout settling time enforced before the first ADC sample after a channel relay is de-energised (§5.4.6, §7.3.1). The sampler skips the channel for this long; its band is left unchanged across the window. Implemented in firmware 1.1.9 — the constant existed from the start but nothing referenced it, so no settling delay was applied at all. |
| `CONT_TRACE_INTERVAL_MS` | **0** (off) | Bench diagnostic: interval for the compact per-channel raw ADC log line; 0 disables it. Default changed 1000 → 0 in v1.44: it shipped at 1000 while its own comment said "set to 0 for field use", so every production build wrote one trace line per second into the log an operator has to read a real fault out of. Set it to 1000 while working at the bench. |
| `CONT_SHORT_UV` | 500 | **DEPRECATED (v1.29)** — the SHORT band was folded into CONNECTED and this threshold is no longer referenced. Kept as a record of the boundary that was attempted. |
| `CONT_R_SENSE_OHM` | 217 | Sense-branch series resistor per channel (Ω), between the relay NC contact and the ADC pin/clamp junction. Fitted 2026-08-23. In the sense current path, so it offsets every reading by ~204 mV — the two thresholds below are derived from it. |
| `CONT_MARGINAL_UV` | 261000 | Threshold: above this = MARGINAL band (µV). Corresponds to ~67 Ω through the 217 Ω sense branch. With hysteresis ±5000 µV. |
| `CONT_OPEN_UV` | 586000 | Threshold: above this = OPEN band (µV). Corresponds to ~500 Ω through the 217 Ω sense branch. With hysteresis ±50000 µV. |
| `CONT_HYSTERESIS_SHORT_UV` | 200 | Hysteresis band for SHORT/GOOD boundary (µV) |
| `CONT_HYSTERESIS_MARGINAL_UV` | 5000 | Hysteresis band for GOOD/MARGINAL boundary (µV) |
| `CONT_HYSTERESIS_OPEN_UV` | 50000 | Hysteresis band for MARGINAL/OPEN boundary (µV) |

**Units note:** All continuity thresholds use microvolts (µV) as integer constants to avoid floating-point comparison and sub-millivolt precision issues. The ADC calibration API returns millivolts — multiply by 1000 before threshold comparison.

**ADC noise floor note (DEPRECATED with the SHORT band, v1.29):** `CONT_SHORT_UV` (500 µV) and `CONT_HYSTERESIS_SHORT_UV` (200 µV) are near the ESP32-S3 ADC noise floor (~760 µV per LSB at 12-bit), which is why the SHORT band was folded into CONNECTED — see the bench evidence in §5.4.2. Retained as a record only; no code reads these constants.

---

## 15. Test Requirements

The developer shall implement and document tests for the following scenarios. Tests can be executed in a bench setup with both units powered but without live igniters (use LEDs or resistors on channel outputs, and jumper wires for continuity simulation).

### 15.1 Communication Tests

| ID | Test | Expected Result |
|---|---|---|
| T-C01 | Power on remote with base off | Remote displays "Connecting..." and retries. No crash. RGB strip runs the cyan boot chase (§11.1) until continuity data exists. |
| T-C02 | Power on both units | Link established within 10 seconds. RSSI displayed. RGB strips show the per-channel continuity map (§11.0), not a whole-strip link colour. |
| T-C03 | Separate units beyond range | Link lost detected within 1.5 seconds. Both units disarm. Siren/buzzer. RGB LEDs yellow. |
| T-C04 | Return units to range after T-C03 | Link re-established. Both units in IDLE (not armed). RGB strips back to the per-channel continuity map (§11.0); link alarm wink clears. |
| T-C05 | Send 1000 pings, measure loss rate | < 1% loss at 10 m LOS, < 5% loss at 100 m LOS. |
| T-C06 | Replay a captured ARM command | Base rejects (sequence number or session token invalid). NACK reason 0x08. **Partially discharged (v1.44):** the rule itself is now host-tested against the production code — `tests/host/test_seqgap.c` T-U04 pins `rlc_seq_validate()`, which is the rule `rlc_link.c`'s `seq_is_replay()` mirrors, including the rejection of seq 0 (a replay window that existed until firmware 1.1.9). From 1.1.9 the base also emits the App D.3 NACK 0x08 instead of dropping the frame silently, so the expected result is now observable on the remote. **Still outstanding on target:** a tool that captures a real frame off the air and re-transmits it. |
| T-C07 | Flash base with different firmware version | Remote displays "FIRMWARE MISMATCH" and refuses to link. |
| T-C08 | Verify RSSI averaging | RSSI display is stable (averaged over 3 frames), not jumping per-frame. |

### 15.2 Arming Tests

| ID | Test | Expected Result |
|---|---|---|
| T-A01 | ARM with both switches armed, continuity GOOD | Channel arms. ACK received. Display updates. **Siren sounds continuously.** RGB LEDs red blink. |
| T-A02 | ARM with base switch disarmed | NACK with reason 0x01 ("BASE KEY OFF"). Channel not armed. |
| T-A03 | ARM with remote switch disarmed | Remote does not send ARM (local guard). Display shows "Turn ARM key first". |
| T-A04 | ARM channel with OPEN continuity | NACK with reason 0x04. |
| T-A05 | ARM second channel while one is armed | **NOT REACHABLE THROUGH THE OPERATOR UI — verify by host test / code review, not on target.** Selecting a second channel requires rotating the encoder, and T-A08 requires that rotating the encoder while armed disarms immediately. Satisfying T-A08 destroys T-A05's precondition, so no sequence of operator actions can produce a second `CMD_ARM` while a channel is armed. Confirmed on target 2026-08-26: the remote sent `CMD_DISARM` (0x21), never a second ARM. The `guard_arm()` guard 4 that returns NACK 0x0A remains **correct and required** — it is defence-in-depth against a remote-side bug, a replayed frame or a malformed command, i.e. exactly the cases where the base must not assume the remote disarmed first. **Host-test half now met (v1.44):** `tests/host/test_base_fsm.c` T-FSM01 injects a second `EVT_CMD_ARM` while armed and asserts `NACK_CHANNEL_ALREADY_ARMED` with the first channel still armed. |
| T-A06 | Turn base key switch to OFF while armed | Immediate disarm. Arm relay de-energised (coil current broken). All channel relays de-energised (NC). Siren off. |
| T-A07 | Turn remote arm switch to DISARM while armed | DISARM sent. Base disarms. |
| T-A08 | Rotate encoder while armed | Immediate disarm. Channel selection updates. Operator must re-arm. |
| T-A09 | Verify continuity bands visible with arm switch OFF | All 8 channels show the correct continuity band (CONNECTED / MARGINAL / OPEN) on the remote display regardless of base key switch position — continuity runs from its own 3.3 V supply through the relay NC contacts and does not depend on the arm relay. Use known resistors: **< ~67 Ω → CONNECTED, ~67–500 Ω → MARGINAL, > ~500 Ω or nothing → OPEN**. (Band list corrected v1.37: SHORT was merged into CONNECTED on 2026-08-21, so a 0 Ω load reads CONNECTED, not SHORT.) |
| T-A10 | ARM with arm relay sense fault (disconnect arm relay feedback wire) | NACK 0x0B ("ARM SENSE FAULT"). Channel not armed. |
| T-A11 | ARM with stale STATUS_UPDATE | Remote blocks locally, triple beep, display **"NO BASE STATUS DATA"**, and **nothing is transmitted**. Requires a fault-injection build (`./build_base.sh --inject`, key `s`): the condition is not reachable otherwise, because link loss trips at 3 missed pings (1.5 s) long before the staleness timeout, so jamming the radio produces LINK_LOST rather than "linked but stale". Note the guard is `is_status_fresh()` at **2 × STATUS_UPDATE_INTERVAL_MS = 4000 ms**; the separate stale-data safety timeout is `STATUS_STALE_TIMEOUT_MS` = 5000 ms. (Wording corrected v1.38 — the spec previously said "DATA STALE — CANNOT ARM", which no build has ever displayed.) |
| T-A12 | ARM with low remote battery | Remote blocks locally ("REMOTE BATTERY LOW — CANNOT ARM"). |
| T-A13 | Verify channel in CMD_ACK | Base ACKs an ARM with a channel the operator did not select → remote refuses to enter ARMED, triple beep, display **"CHANNEL MISMATCH ERROR"**, and `CMD_DISARM` sent so the base does not stay armed on a channel nobody chose. Requires a fault-injection build (`./build_base.sh --inject`, key `a`); the injection is one-shot so the follow-up DISARM ACK is clean. |
| T-A14 | ARM channel with MARGINAL continuity | Arming succeeds (MARGINAL does not block arming). Display shows yellow warning. |
| T-A15 | ARM channel with SHORT continuity | **SUPERSEDED — not runnable.** The SHORT band was merged into CONNECTED on 2026-08-21 (bug #26): the distinction sits below the measurement floor at the specified 1 mA test current, so reporting it would be guessing. `rlc_continuity_class.c` classifies into three bands only and `CONT_SHORT_UV` is unused. A 0 Ω load arms normally and displays as CONNECTED. No orange SHORT warning exists to observe. Retained for numbering continuity; the arming-with-a-very-low-resistance case is covered by T-A01. |
| T-A16 | **Disconnect the igniter on the armed channel while ARMED** | Base disarms within ~1 s of the band reaching OPEN: arm relay de-energised, all channel relays NC, siren off, state → IDLE, `STATUS_UPDATE` shows that channel OPEN and nothing armed. Base log shows `Continuity OPEN on armed ch N during ARMED — disarm`. (v1.35) |
| T-A17 | **Disconnect the igniter on the armed channel during the PRE_FIRE countdown** | Countdown aborts, no fire pulse, siren off, state → IDLE. Base log shows `Continuity OPEN on armed ch N during PRE_FIRE — abort`. (v1.35) |
| T-A19 | **ARM while the base is in terminal ERROR** (v1.40) | Base answers with NACK `0x0E`, remote displays the *specific* fault from its cached `error_flags` — e.g. "BASE ERROR: VBAT CRITICAL" — not a bare reason code and not a silent timeout. Requires a fault-injection build (`./build_base.sh --inject`, key `e`): the remote's own ERROR guard normally refuses to send the command, so the base never gets to answer, and the real-world gap (base fails between status updates) is under 100 ms. The `e` injection falsifies `base_state` to IDLE while keeping `error_flags` truthful, which removes the race and still exercises the enrichment. |
| T-A20 | **Operator-visible reporting of refusals** (v1.40, §7.2.9a) | Every refusal, abort or failure produces an audible indication **and** a display message. Spot-checked on target: stale-status timeout → "BASE STATUS LOST"; base disarming under the remote → "BASE DISARMED"; ARM refused in ERROR → "BASE ERROR: <flag>". |
| T-A18 | **Disconnect an igniter on a NON-armed channel while another channel is ARMED** | Base stays ARMED — only the armed channel's band triggers disarm. `STATUS_UPDATE` reflects the other channel as OPEN. (v1.35 regression guard) |

### 15.3 Fire Tests

| ID | Test | Expected Result |
|---|---|---|
| T-F01 | Full fire sequence (arm → fire → complete) | Siren continuous from ARMED through PRE_FIRE and FIRING, with no gap at either transition. Channel SPDT relay energises (NO/fire path) for FIRE_PULSE_DURATION_MS. Auto-disarm after (relay returns to NC). |
| T-F02 | Release fire button during pre-fire delay | Fire aborted. Return to IDLE. Siren off. |
| T-F03 | Release fire button during active fire | Cease fire. Channel relay deactivates. Return to IDLE. |
| T-F04 | Fire command on non-armed channel | NACK with reason 0x05. |
| T-F05 | Continuity remains readable during ARMED | Verify that continuity readings on the armed channel remain live during ARMED state (relay is still in NC position). Band changes are live during ARMED: removing the igniter jumper while ARMED causes the band to reach OPEN and **disarms the base** (v1.35; covered by T-A16). |
| T-F06 | Link lost during firing | **NOT REACHABLE with the current constants (v1.42).** `FIRE_PULSE_DURATION_MS` is 1000 ms; link loss needs 3 missed pings = **1500 ms**. If the remote stops before the PRE_FIRE→FIRING transition, the dead-man guard aborts instead of firing; if it stops at or after the transition, the base needs 1500 ms to notice and the 1000 ms pulse has already completed. **There is no window in which link loss can be detected during FIRING**, which also makes `COMPLETE_PULSE_ON_LINK_LOSS` and the C1 `s_link_lost_pending` logic unreachable config. Operator decision 2026-08-26: leave the pulse at 1 s (it suits the igniters — do not change a fire-path constant to suit a test) and verify the link-loss-during-FIRING logic by code review instead. Reachable only if the pulse is ever raised above 1500 ms, or via a remote-side fault injection that stops pings without stopping `CMD_FIRE`. **Discharged by host harness (v1.44):** `tests/host/test_base_fsm.c` drives the production `rlc_base_fsm.c` with injected `rlc_fsm_event_t` sequences and asserts the relay, siren and fire-timer outcomes. This is the agreed "verify by code review" substitute, now as an executable artifact that runs on every build (`build_base.sh` / `build_remote.sh` invoke `tests/host/run.sh` and refuse to build on failure). Covered by T-FSM06 (`COMPLETE_PULSE_ON_LINK_LOSS` both ways). |
| T-F07 | Pre-fire timer expires without fire button held | **NOT OPERATOR-INDUCIBLE (v1.42).** The dead-man is checked only at the PRE_FIRE→FIRING transition and requires `CMD_FIRE` to have stopped **while the link is still up**. Releasing the button sends `CMD_CEASE_FIRE` (that is T-F02); killing the remote stops its pings too, so link loss trips at 1500 ms and PRE_FIRE takes its `EVT_LINK_LOST` branch first. Reaching this guard needs a remote that stops authorising without disconnecting — a remote-side fault. The guard is correct defence-in-depth against exactly that; it simply cannot be reached from outside the firmware. Needs a remote-side injection harness. **Discharged by host harness (v1.44):** `tests/host/test_base_fsm.c` drives the production `rlc_base_fsm.c` with injected `rlc_fsm_event_t` sequences and asserts the relay, siren and fire-timer outcomes. This is the agreed "verify by code review" substitute, now as an executable artifact that runs on every build (`build_base.sh` / `build_remote.sh` invoke `tests/host/run.sh` and refuse to build on failure). Covered by T-FSM04 (dead-man timeout, including the rule that a wrong-channel CMD_FIRE does not refresh it). |
| T-F08 | Verify fire pulse timing | Measure relay ON duration with oscilloscope or logic analyser. Must match FIRE_PULSE_DURATION_MS within +500 µs (task scheduling latency for relay deactivation in task context). |
| T-F09 | Verify link-health guard at PRE_FIRE→FIRING | If PONG missed at transition boundary, base aborts instead of firing. **Not inducible from outside the firmware (v1.42)** — it requires a PONG to be missed in a specific sub-second window. Needs a remote-side injection harness, same as T-F06 and T-F07. **Discharged by host harness (v1.44):** `tests/host/test_base_fsm.c` drives the production `rlc_base_fsm.c` with injected `rlc_fsm_event_t` sequences and asserts the relay, siren and fire-timer outcomes. This is the agreed "verify by code review" substitute, now as an executable artifact that runs on every build (`build_base.sh` / `build_remote.sh` invoke `tests/host/run.sh` and refuse to build on failure). Covered by T-FSM04, which also pins the v1.44 separation of guard 2 (heartbeat freshness → LINK_LOST) from guard 4 (failure rate → IDLE). |

### 15.4 Safety Tests

| ID | Test | Expected Result |
|---|---|---|
| T-S01 | Power cycle base while armed | Boots to safe state. All SPDT relays de-energised (NC position). |
| T-S02 | Power cycle remote while base armed | Base detects link loss within 1.5 seconds. Disarms. |
| T-S03 | Reduce base battery below VBAT_CRITICAL_MV | Base disarms and enters ERROR state. |
| T-S04 | Hold fire button at boot, then arm | Fire does not trigger (fresh press required). |
| T-S05 | Corrupt a message (bit flip simulation) | Message rejected (CRC integrity check or ESP-NOW decrypt failure). |
| T-S06 | Verify GPIO init order | Measure SPDT relay outputs with logic analyser during boot. Must be inactive (de-energised) before ESP-NOW init. |
| T-S07 | Watchdog test: infinite loop in main task | Unit reboots within 5 seconds. All SPDT relays de-energised after reboot. |
| T-S08 | Hold fire button, then arm (button already pressed at ARMED entry) | Fire does not trigger — fresh press detection requires 0xFF→0x00 transition after entering ARMED state. |
| T-S09 | LINK_REQUEST while ARMED | Send LINK_REQUEST while base is ARMED. **Base rejects it with `LINK_REJECT_BUSY` (0x02)** — amended fw 1.1.17, which replaced the silent drop; the original "silently ignores" wording predates it. Session and armed state are unaffected: no LINK_ACK, no new session token, no dropped arm. **PASS 2026-08-27.** |
| T-S10 | Display SPI failure at boot | Disconnect display MOSI. Remote fails display ID read-back and transitions to ERROR. |
| T-S11 | ESP-NOW 5 consecutive send failures | Simulate 5 consecutive send callback failures. System transitions to LINK_LOST immediately without waiting for 3 missed heartbeats. |
| T-S12 | Fire pulse on link loss (COMPLETE_PULSE_ON_LINK_LOSS=true) | Lose link during FIRING. Base completes fire pulse, then transitions POST_FIRE → LINK_LOST. **Physically unreachable as written (v1.43):** the 1 s fire pulse is shorter than the 1.5 s link-loss detection — see the T-F06 note. **Discharged by host harness (v1.44):** `tests/host/test_base_fsm.c` drives the production `rlc_base_fsm.c` with injected `rlc_fsm_event_t` sequences and asserts the relay, siren and fire-timer outcomes. This is the agreed "verify by code review" substitute, now as an executable artifact that runs on every build (`build_base.sh` / `build_remote.sh` invoke `tests/host/run.sh` and refuse to build on failure). Covered by T-FSM06. |
| T-S13 | Fire pulse on link loss (COMPLETE_PULSE_ON_LINK_LOSS=false) | Set constant to false. Lose link during FIRING. Base immediately cuts fire pulse and transitions to LINK_LOST. **Physically unreachable as written (v1.43)** for the same reason as T-S12 (1 s fire pulse < 1.5 s link-loss detection, see T-F06); additionally requires rebuilding with `COMPLETE_PULSE_ON_LINK_LOSS=0`. **Discharged by host harness (v1.44):** `tests/host/test_base_fsm.c` drives the production `rlc_base_fsm.c` with injected `rlc_fsm_event_t` sequences and asserts the relay, siren and fire-timer outcomes. This is the agreed "verify by code review" substitute, now as an executable artifact that runs on every build (`build_base.sh` / `build_remote.sh` invoke `tests/host/run.sh` and refuse to build on failure). Covered by T-FSM06, which asserts whichever branch the constant selects, so no rebuild is needed to exercise the logic. |
| T-S14 | Arm timeout | Arm a channel, do not press fire. After 10 seconds, base auto-disarms and returns to IDLE. |
| T-S15 | ERR_COMM_DEGRADED blocks arming | Induce >30% ping failure rate. Attempt to arm. Base NACK's due to degraded link. |
| T-S16 | ERR_COMM_DEGRADED blocks firing | Arm channel, press fire, induce >30% ping failure during PRE_FIRE. Base aborts instead of transitioning to FIRING. |
| T-S17 | Key switch sense verifies key switch | With key switch OFF: verify key sense GPIO 42 reads LOW. Turn key switch ON: verify key sense reads HIGH. Attempt ARM: verify guard 1 passes. |
| T-S18 | Key switch sense fault detection | Disconnect key switch sense wire. Turn key switch ON (debounced input reads key OFF due to lost signal). Attempt ARM: verify NACK 0x01 ("BASE KEY OFF"). |
| T-S19 | Post-fire igniter status via continuity | Fire a channel with test resistor (2 Ω). After fire pulse completes and relay returns to NC: verify continuity reads GOOD (resistor intact). Repeat with a fuse wire that burns: verify continuity reads OPEN after fire. **PASS 2026-08-27** — burn-through verified with a real igniter during earlier fire testing (operator attestation; T-A17 corroborates that igniters have been fired on this rig). Intact-load half re-confirmed 2026-08-27 with a 12 V 50 W halogen reading CONNECTED after a pulse. |

### 15.5 Unit Tests (Host-Compilable)

| ID | Module | Test |
|---|---|---|
| T-U01 | Message serialisation | Serialise and deserialise all message types. Verify byte-for-byte correctness. `_Static_assert` on all struct sizes. |
| T-U02 | Integrity CRC | Compute CRC for known inputs. Verify against expected output. Verify rejection of wrong CRC. |
| T-U03 | Sequence number | Verify acceptance of increasing sequence numbers. Verify rejection of equal/lower. Verify reset on session establishment. |
| T-U04 | Session token | Verify acceptance of correct token. Verify rejection of wrong token. Verify atomic invalidation on re-link. **Anti-replay half covered (v1.44)** by `tests/host/test_seqgap.c`; token acceptance/rejection still needs the link layer on target. |
| T-U05 | Debounce (8-bit) | Feed 0/1 sequence. Verify 0x00/0xFF detection. Verify 80 ms timing. |
| T-U06 | Debounce (16-bit) | Feed 0/1 sequence. Verify 0x0000/0xFFFF detection. Verify 160 ms timing. |
| T-U07 | Battery threshold | Feed ADC values. Verify all three remote thresholds (MIN_ARM, MIN_OPERATE, CRITICAL). **Base gating covered (v1.44)** by `tests/host/test_base_fsm.c` T-FSM01, which drives the arming guard below `BASE_VBAT_MIN_ARM_MV` and asserts `NACK_LOW_BATTERY` — the safety *behaviour*, as opposed to the sampling maths already covered by `test_battery.c`. |
| T-U08 | Version comparison | Verify strict MAJOR.MINOR.PATCH matching logic. |
| T-U09 | Update sequence gap | Feed update_sequence numbers with gaps. Verify warning at gap > 2. **COVERED (v1.44)** — `tests/host/test_seqgap.c` T-U09 against the production `rlc_update_seq_lost()`. |
| T-L01 | LED strip renderer | Channel → pixel mapping with `RLC_STRIP_REVERSED`: channel 1 at pixel 7, channel 8 at pixel 0. |
| T-L02 | LED strip renderer | Continuity map colours: GOOD/MARGINAL/OPEN/SHORT resolve to `RLC_COLOR_CONT_*` on the correct pixels. |
| T-L03 | LED strip renderer | Cyan boot chase runs while no continuity data has been published, and advances in channel order. |
| T-L04 | LED strip renderer | Alarm wink timing: full-strip alarm colour inside the wink window, map restored after it, repeating each period. |
| T-L05 | LED strip renderer | Concurrent alarms alternate colours across successive winks. |
| T-L06 | LED strip renderer | Stale flag dims the whole map to `RLC_STRIP_STALE_DIM_PCT`; clearing it restores full brightness. |
| T-L07 | LED strip renderer | Channel-of-interest cursor pulses to `RLC_STRIP_CURSOR_LOW_PCT` while other channels stay steady. |
| T-L08 | LED strip renderer | Key warning breathes the whole map with no active channel (base) and only the cursor with one (remote). |
| T-L09 | LED strip renderer | Stale dim and cursor pulse compose without either overriding the other. |

| T-Q01 | Encoder decoder | `ENC_DIVIDER` raw transitions are required per emitted channel step, in both directions. |
| T-Q02 | Encoder decoder | Illegal transitions (a jump of two positions, or no movement) are discarded and not counted valid. |
| T-Q03 | Encoder decoder | Twenty contact-bounce cycles on one line emit no channel change. |
| T-Q04 | Encoder decoder | A direction reversal restarts the accumulator, so partial rotation either way nets to nothing. |
| T-Q05 | Encoder decoder | The first sample after init only seeds the state; it never emits a step. |
| T-Q06 | Encoder decoder | One full detent (four transitions) moves exactly one channel, never two. |
| T-Q07 | Encoder decoder | Rotation sense matches `ENC_REVERSED`, pinned explicitly in both directions. |
| T-M01 | Base arm state | Normal progression SAFE → READY → ARMED across the firing-path states. |
| T-M02 | Base arm state | Welded relay: arm sense HIGH outside the firing path yields WELD!, including with the key OFF — the case where a key-driven display would wrongly read SAFE. |
| T-M03 | Base arm state | `ERR_RELAY_FAULT` yields WELD! even when the sense bit is low. |
| T-M04 | Base arm state | Stale STATUS_UPDATE yields UNKNOWN, never SAFE. |
| T-M05 | Base arm state | The key switch alone can never produce ARMED or WELD!, in any base state. |
| T-M06 | Base arm state | The rendered status line fits the 40-character scale-2 budget for every state. |
| T-M07 | Base arm state | STATUS_UPDATE remains 14 bytes — a size change would require reflashing both units. |
| T-B01 | Battery sampling | Insertion-sort helper orders correctly. |
| T-B02 | Battery sampling | Median of a constant burst returns that value. |
| T-B03 | Battery sampling | Samples clipped at full scale do not move the median, and are counted. Contrasted against the mean, which is biased upward by the same data. |
| T-B04 | Battery sampling | Zero-valued dropouts do not move the median. |
| T-B05 | Battery sampling | Full sample path applies the divider ratio and caches the result, unaffected by clipping. |
| T-B06 | Battery sampling | Total ADC read failure retains the previous good reading. |
| T-B07 | Battery sampling | Burst size is odd. |
| T-E01 | Error flag naming | Every defined flag maps to its documented display name. |
| T-E02 | Error flag naming | Every bit 0-7 resolves to a usable name, including the reserved and undefined bits. |
| T-E03 | Error flag naming | Flag counting for empty, single, multiple and full masks. |
| T-E04 | Error flag naming | N-th set flag lookup (drives the remote's multi-flag cycling); NULL past the end. |
| T-E05 | Error flag naming | Comma-separated list formatting, including the empty mask. |
| T-E06 | Error flag naming | Truncation into an undersized buffer stays in bounds and NUL-terminated. |
| T-E07 | Error flag naming | The reported field case: 0x02 names the critical battery flag. |
| T-D01 | Debounce engine | Settle width: an 8-bit register latches after 8 consecutive readings, a 16-bit register after 16, and neither latches early. |
| T-D02 | Debounce engine | The **first stable reading fires no callback** — in either direction. This is the fire button's fresh-press interlock (§5.3.1 rule 1, §5.5.3): a button held at power-on must not read as a press. |
| T-D03 | Debounce engine | The first stable reading **is** visible via `rlc_debounce_get_state()` / `rlc_debounce_is_stable()` (§5.3.1 rule 2). This is the path the remote arm switch must poll; omitting it was review finding N1. |
| T-D04 | Debounce engine | Genuine transitions after the initial determination fire exactly one callback each, with the correct state, and a held input fires no repeats. |
| T-D05 | Debounce engine | Bounce shorter than the register width never latches; a clean run afterwards does. |
| T-D06 | Debounce engine | NULL-safe API: init/update/get_state/is_stable on a NULL handle do not crash and report inactive. |
| T-U10 | Continuity band classification | Feed known microvolt values: 0, 300, 500, 1000, 30000, 66000, 100000, 500000, 1500000, 2000000, 3190000. Verify correct band assignment (**CONNECTED, MARGINAL, OPEN** — three bands since v1.29) at each threshold. |
| T-U11 | Continuity hysteresis | Feed voltage sequence oscillating near each threshold boundary. Verify no spurious band transitions within the hysteresis band. |
| T-U12 | Continuity bands encoding | Verify 2-bit-per-channel packing into uint16: ch1 in bits 1:0 through ch8 in bits 15:14. Verify extraction for all band combinations. Verify that enum values (`CONT_OPEN`=0, `CONT_CONNECTED`=1, `CONT_MARGINAL`=2) match wire encoding directly with no mapping required. Value 3 (`CONT_SHORT`) is deprecated and never emitted; verify it folds to the current scheme on decode. |
| T-U13 | Struct field offset verification | Verify `offsetof()` for all packed message structs matches expected byte offsets from §6.3.3. Specifically: `rlc_payload_cmd_arm_t.integrity_crc` at offset 0, `.channel` at offset 4. Same for cmd_disarm_t and cmd_fire_t. Verify with both `#pragma pack` and `__attribute__((packed))`. |

**Runner:** `./tests/host/run.sh` — compiles each `tests/host/test_*.c` against the mock headers in `tests/host/stubs/` and runs it, once per unit (`BASE` and `REMOTE`), because `RLC_STRIP_REVERSED` and other per-unit config differ. As of v1.44: **16 binaries, 418 checks**.

`build_base.sh` and `build_remote.sh` run the suite before every firmware build and **refuse to build on failure** (set `RLC_SKIP_HOST_TESTS=1` to bypass). Until v1.44 the runner existed but nothing ever invoked it, so a regression could reach a board without anyone running the tests.

Tests include the real production sources directly rather than mirroring their logic — `rlc_base_fsm.c` (T-FSM), `rlc_rgb_led.c` (T-L), `rlc_arm_state.c` (T-M), `rlc_message.c` (T-U04/09/16), `rlc_continuity_class.c`, `rlc_debounce.c` (T-D) and `rlc_encoder.c` (T-Q) — so a divergence between what is tested and what runs on the target is not possible. The debounce engine was stubbed out in the harness until v1.31; that stub is removed.

**Unit-specific tests declare themselves SKIPPED** rather than printing "0 checks, 0 failures", which read identically to a passing test in the runner output (`test_encoder.c` is remote-only; `test_base_fsm.c` is base-only).

**Base FSM event-injection harness (v1.44, §4.5).** `tests/host/test_base_fsm.c` compiles the production `rlc_base_fsm.c` against recording fakes for the relays, siren, fire timer, arm/key sense, continuity, battery and link, then calls its event handler and timer tick directly with synthesised `rlc_fsm_event_t` sequences. It asserts outcomes — which relay moved, which siren pattern sounded, which NACK reason went out — not merely that nothing crashed.

| ID | Coverage |
|---|---|
| T-FSM01 | All ten §7.2.2 arming guards, each with its specific NACK reason (discharges the host half of T-A05 and the base gating half of T-U07) |
| T-FSM02 | The §7.2.2 arm-verify window: completion, timeout, key-off, DISARM, and the bug #30 entry re-check |
| T-FSM03 | §7.2.7 continuity-loss disarm — event path in ARMED and PRE_FIRE, the bug #30 level backstop with no event at all, correct scoping (other channels and MARGINAL do not disarm; FIRING is excluded) |
| T-FSM04 | §7.2.4 PRE_FIRE→FIRING guards: dead-man (including that a wrong-channel CMD_FIRE does not refresh it), heartbeat freshness → LINK_LOST, failure rate → IDLE, key, arm sense, and the successful ignition (discharges T-F07, T-F09) |
| T-FSM05 | **Two complete fire cycles on one power-on** — the BF-01 regression — plus the fire-timer-start-failure path latching ERROR with the igniter de-energised |
| T-FSM06 | Every §7.2.5 exit from FIRING: CEASE_FIRE, key off, arm sense lost, link loss under both `COMPLETE_PULSE_ON_LINK_LOSS` settings, battery critical (complete-then-ERROR), and the max-duration backstop (discharges T-F06, T-S12, T-S13) |
| T-FSM07 | §7.3.2 arm-relay weld fault from IDLE and from FIRING |
| T-FSM08 | §7.2.9a: ERROR answers all four commands with `NACK_BASE_ERROR` and acts on none; link recovery does not clear it |
| T-FSM09 | `ARM_TIMEOUT_MS` auto-disarm; link loss while ARMED and recovery never re-entering ARMED |
| T-U14 | CRC32-C test vector | Verify CRC32-C (Castagnoli) of ASCII `"123456789"` = `0xE3069283`. Verify that CRC input includes header + payload (excluding CRC field) + integrity key. |
| T-U15 | Sequence number overflow | Verify that when sender sequence reaches `0xFFFFFFFF`, system initiates re-link rather than wrapping to 0. |
| T-U16 | update_sequence wrap-around | Verify that `update_sequence` wrap from 65535 to 0 is not treated as a gap. Verify modular gap detection. **COVERED (v1.44)** — `tests/host/test_seqgap.c` T-U16. |

---

## Appendix A — Message Format Reference

### A.1 Complete Message Structure

```
┌─────────────────────────────── ESP-NOW Frame (max 250 bytes) ──────────────┐
│                                                                            │
│  ┌─── Common Header (12 bytes) ───┐  ┌─── Payload (variable) ──────────┐  │
│  │ version    : u8                │  │                                  │  │
│  │ msg_type   : u8                │  │  Type-specific data              │  │
│  │ payload_len: u16               │  │  (see §6.3.3 for each type)     │  │
│  │ seq_number : u32               │  │                                  │  │
│  │ session_tok: u32               │  │                                  │  │
│  └────────────────────────────────┘  └──────────────────────────────────┘  │
│                                                                            │
└────────────────────────────────────────────────────────────────────────────┘
```

### A.2 C Struct Definitions (Reference)

```c
#pragma pack(push, 1)

typedef struct __attribute__((packed)) {
    uint8_t  protocol_version;
    uint8_t  msg_type;
    uint16_t payload_length;
    uint32_t sequence_number;
    uint32_t session_token;
} rlc_msg_header_t;
_Static_assert(sizeof(rlc_msg_header_t) == 12, "Header size mismatch");

typedef struct __attribute__((packed)) {
    uint8_t  remote_firmware_version[3];  // [0]=major, [1]=minor, [2]=patch
    uint8_t  remote_mac[6];
} rlc_payload_link_request_t;
_Static_assert(sizeof(rlc_payload_link_request_t) == 9, "LINK_REQUEST size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t session_token;
    uint8_t  base_firmware_version[3];    // [0]=major, [1]=minor, [2]=patch
    uint8_t  num_channels;
} rlc_payload_link_ack_t;
_Static_assert(sizeof(rlc_payload_link_ack_t) == 8, "LINK_ACK size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t ping_timestamp;
    uint16_t remote_battery_voltage_mv;
} rlc_payload_ping_t;
_Static_assert(sizeof(rlc_payload_ping_t) == 6, "PING size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t ping_timestamp;
    uint32_t pong_timestamp;
} rlc_payload_pong_t;
_Static_assert(sizeof(rlc_payload_pong_t) == 8, "PONG size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t integrity_crc;
    uint8_t  channel;
} rlc_payload_cmd_arm_t;
_Static_assert(sizeof(rlc_payload_cmd_arm_t) == 5, "CMD_ARM size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t integrity_crc;
    uint8_t  channel;
} rlc_payload_cmd_disarm_t;
_Static_assert(sizeof(rlc_payload_cmd_disarm_t) == 5, "CMD_DISARM size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t integrity_crc;
    uint8_t  channel;
} rlc_payload_cmd_fire_t;
_Static_assert(sizeof(rlc_payload_cmd_fire_t) == 5, "CMD_FIRE size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t integrity_crc;
} rlc_payload_cmd_cease_fire_t;
_Static_assert(sizeof(rlc_payload_cmd_cease_fire_t) == 4, "CMD_CEASE_FIRE size mismatch");

typedef struct __attribute__((packed)) {
    uint16_t continuity_bands;          // 2 bits per channel: ch1=bits 1:0, ch2=bits 3:2, ...
                                        // ch8=bits 15:14. Values: 00=OPEN, 01=GOOD, 10=MARGINAL, 11=SHORT
    uint16_t channel_armed_bitmask;    // bits 0-7: channels 1-8, bits 8-15: reserved
    uint16_t channel_firing_bitmask;   // bits 0-7: channels 1-8, bits 8-15: reserved
    uint8_t  base_key_switch;         // key switch position, debounced (GPIO 42, §5.4.3b)
    uint8_t  base_arm_sense;          // arm relay COM output, debounced (GPIO 21, §5.4.3)
    uint16_t battery_voltage_mv;
    uint8_t  base_state;
    uint8_t  error_flags;
    uint16_t update_sequence;
} rlc_payload_status_update_t;
_Static_assert(sizeof(rlc_payload_status_update_t) == 14, "STATUS_UPDATE size mismatch");

typedef struct __attribute__((packed)) {
    uint8_t  acked_msg_type;
    uint32_t acked_sequence_number;
    uint8_t  channel;                  // channel the command applied to, or 0x00
} rlc_payload_cmd_ack_t;
_Static_assert(sizeof(rlc_payload_cmd_ack_t) == 6, "CMD_ACK size mismatch");

typedef struct __attribute__((packed)) {
    uint8_t  nacked_msg_type;
    uint32_t nacked_sequence_number;
    uint8_t  reason_code;
} rlc_payload_cmd_nack_t;
_Static_assert(sizeof(rlc_payload_cmd_nack_t) == 6, "CMD_NACK size mismatch");

#pragma pack(pop)
```

---

## Appendix B — State Transition Tables

### B.1 Base Unit State Transitions

| Current State | Event | Guard Conditions | Next State | Actions |
|---|---|---|---|---|
| BOOT | Link established | — | IDLE | Start I/O polling, send initial STATUS_UPDATE |
| BOOT | ESP-NOW init fails (3 retries) | — | ERROR | RGB LED red triple flash |
| IDLE | CMD_ARM(ch) | All §7.2.2 guards (incl. key switch sense) | ARMED | Record channel, arm relay ON, siren continuous, ACK(ch), STATUS_UPDATE |
| IDLE | CMD_ARM(ch) | Any guard fails | IDLE | NACK with reason |
| IDLE | CMD_FIRE(ch) | — | IDLE | NACK reason 0x05 (wrong state) |
| IDLE | CMD_DISARM | — | IDLE | ACK (idempotent, already safe) |
| IDLE | CMD_CEASE_FIRE | — | IDLE | ACK (idempotent) |
| ARMED | CMD_FIRE(ch) | ch == armed_ch, CRC OK, arm switch ARMED | PRE_FIRE | Siren continuous, start pre-fire timer, cancel arm timeout, ACK, STATUS_UPDATE |
| ARMED | CMD_FIRE(ch) | ch != armed_ch | ARMED | NACK reason 0x05 |
| ARMED | CMD_ARM(ch2) | ch2 != armed_ch | ARMED | NACK reason 0x0A |
| ARMED | CMD_DISARM | — | IDLE | relay_all_safe() (arm relay OFF + all ch relays off), siren off, ACK, STATUS_UPDATE |
| ARMED | Arm switch → DISARM | — | IDLE | relay_all_safe() (arm relay OFF + all ch relays off), siren off, STATUS_UPDATE |
| ARMED | Arm timeout (10s) | — | IDLE | relay_all_safe() (arm relay OFF), siren off, STATUS_UPDATE |
| ARMED | Link lost | — | LINK_LOST | relay_all_safe() (arm relay OFF), siren link-lost pattern |
| ARMED | Continuity → OPEN on armed ch | ch == armed_ch | IDLE | relay_all_safe() (arm relay OFF), siren off, STATUS_UPDATE (v1.35) |
| ARMED | VBAT < critical | — | ERROR | relay_all_safe() (arm relay OFF), siren off |
| PRE_FIRE | Timer elapsed | CMD_FIRE in last 500 ms AND last PONG within (HEARTBEAT_INTERVAL_MS + HEARTBEAT_TIMEOUT_MS) AND arm switch ARMED AND ERR_COMM_DEGRADED not set | FIRING | Channel SPDT relay energised (NO/fire), arm relay stays ON, start fire pulse timer, STATUS_UPDATE |
| PRE_FIRE | Timer elapsed | No CMD_FIRE in last 500 ms | IDLE | relay_all_safe() (arm relay OFF), siren off, STATUS_UPDATE (dead-man abort) |
| PRE_FIRE | Continuity → OPEN on armed ch | ch == armed_ch | IDLE | relay_all_safe() (arm relay OFF), siren off, STATUS_UPDATE (v1.35) |
| PRE_FIRE | Timer elapsed | ERR_COMM_DEGRADED set | IDLE | relay_all_safe() (arm relay OFF), siren off, STATUS_UPDATE (link quality abort) |
| PRE_FIRE | CMD_CEASE_FIRE | — | IDLE | relay_all_safe() (arm relay OFF), siren off, ACK, STATUS_UPDATE |
| PRE_FIRE | Arm switch → DISARM | — | IDLE | relay_all_safe() (arm relay OFF), siren off, STATUS_UPDATE |
| PRE_FIRE | Link lost | — | LINK_LOST | relay_all_safe() (arm relay OFF), siren link-lost pattern |
| PRE_FIRE | VBAT < critical | — | ERROR | relay_all_safe() (arm relay OFF), siren off |
| FIRING | Fire pulse timer elapsed | — | POST_FIRE | relay_all_safe() (arm relay OFF + all ch relays off), siren off, STATUS_UPDATE (relay control in task context) |
| FIRING | CMD_CEASE_FIRE | — | IDLE | relay_all_safe() (arm relay OFF + all ch relays off), siren off, ACK, STATUS_UPDATE |
| FIRING | Arm switch → DISARM | — | IDLE | relay_all_safe() (arm relay OFF + all ch relays off), siren off, STATUS_UPDATE |
| FIRING | Link lost | — | *(special)* | Complete fire pulse → POST_FIRE → LINK_LOST |
| FIRING | VBAT < critical | — | *(special)* | Complete fire pulse → POST_FIRE → ERROR |
| FIRING | Continuity band change (armed ch) | — | FIRING | Ignored (expected — relay NC disconnected during firing) |
| POST_FIRE | Cooldown elapsed | — | IDLE | — |
| POST_FIRE | CMD_ARM | — | POST_FIRE | NACK reason 0x05 |
| POST_FIRE | Link lost | — | LINK_LOST | Relays already safe (de-energised) |
| LINK_LOST | Valid PING received | — | IDLE | Respond PONG, resume heartbeat, silence siren immediately |
| ANY | Assertion failure | — | ERROR | relay_all_safe() |

### B.2 Remote Unit State Transitions

| Current State | Event | Guard Conditions | Next State | Actions |
|---|---|---|---|---|
| BOOT | Init + peer reg OK | — | LINKING | Start LINK_REQUEST, splash |
| LINKING | LINK_ACK | Version match | IDLE | Store token, reset seq, heartbeat, display |
| LINKING | LINK_ACK | Version mismatch | LINKING | "FIRMWARE MISMATCH", stop |
| IDLE | Encoder press (500ms hold) | Arm ON, batt OK, data fresh, link OK, not degraded | *(wait ACK)* | CMD_ARM, wait ACK |
| IDLE | ACK'd (ch match) | — | ARMED | Display, buzzer double |
| IDLE | ACK'd (ch mismatch) | — | IDLE | "CHANNEL MISMATCH", CMD_DISARM |
| IDLE | NACK'd | — | IDLE | Display reason 3s, buzzer triple |
| IDLE | ACK timeout (after retry) | — | IDLE | "ARM FAILED — NO RESPONSE" |
| IDLE | Encoder press | Arm OFF | IDLE | "Turn ARM key first" |
| IDLE | Encoder press | Batt < MIN_ARM | IDLE | "REMOTE BATTERY LOW" |
| IDLE | Encoder press | Data stale (> 4s) | IDLE | "NO BASE STATUS DATA" |
| IDLE | Encoder press during ACK wait | — | IDLE | Cancel, CMD_DISARM |
| IDLE | Fire button | — | IDLE | Ignored |
| IDLE | STATUS: base ERROR | — | IDLE | "BASE ERROR", refuse ARM |
| ARMED | Fire press (fresh) | STATUS armed, link OK | *(wait ACK)* | CMD_FIRE, wait ACK |
| ARMED | ACK'd (ch match) | — | PRE_FIRE | Local countdown, start CMD_FIRE repeat |
| ARMED | NACK'd/timeout/mismatch | — | IDLE | CMD_DISARM, display error |
| ARMED | Fire released during ACK wait | — | IDLE | Cancel, CMD_DISARM |
| ARMED | Arm switch → DISARM | — | IDLE | CMD_DISARM, buzzer long |
| ARMED | Encoder press | — | IDLE | CMD_DISARM |
| ARMED | Encoder rotate | — | IDLE | CMD_DISARM, update channel |
| ARMED | STATUS: base disarmed | — | IDLE | Buzzer long |
| ARMED | Stale data (5s) | — | IDLE | CMD_DISARM, "STALE DATA" |
| ARMED | Link lost | — | LINK_LOST | Alarm |
| PRE_FIRE | Local countdown elapsed | — | FIRING | Display "IGNITION ACTIVE" |
| PRE_FIRE | Fire released | — | IDLE | CMD_CEASE_FIRE |
| PRE_FIRE | Arm switch / encoder | — | IDLE | CMD_DISARM |
| PRE_FIRE | Link lost | — | LINK_LOST | Alarm |
| FIRING | Fire released | — | IDLE | CMD_CEASE_FIRE |
| FIRING | Arm switch → DISARM | — | IDLE | CMD_CEASE_FIRE |
| FIRING | STATUS: fire complete | — | IDLE | "FIRE COMPLETE" |
| FIRING | STATUS: base not firing | — | IDLE | Sync, stop CMD_FIRE |
| FIRING | Link lost | — | LINK_LOST | Alarm |
| LINK_LOST | PONG received | — | IDLE | Stop alarm |
| ANY | VBAT critical | — | ERROR | Alarm, refuse all |

---

## Appendix C — Pin Assignments

### C.1 Base Unit Pin Assignment

Based on ESP32-S3-DevKitC-1 with N16R8 module. 8 channels with SPDT relays + arm relay + arm relay feedback (GPIO 21) + key switch sense (GPIO 42). All outputs use configurable polarity. Continuity inputs use ADC1.

| Function | GPIO | Notes |
|---|---|---|
| Battery voltage ADC | 1 | ADC1_CH0 — analogue input |
| Channel 1 continuity ADC | 2 | ADC1_CH1 — analogue input + 3.3kΩ series + 100kΩ pull-down |
| Channel 2 continuity ADC | 10 | ADC1_CH9 — analogue input + 3.3kΩ series + 100kΩ pull-down. Moved from GPIO 3 (strapping pin, JTAG select) to avoid boot-time interference. |
| Channel 3 continuity ADC | 4 | ADC1_CH3 — analogue input + 3.3kΩ series + 100kΩ pull-down |
| Channel 4 continuity ADC | 5 | ADC1_CH4 — analogue input + 3.3kΩ series + 100kΩ pull-down |
| Channel 5 continuity ADC | 6 | ADC1_CH5 — analogue input + 3.3kΩ series + 100kΩ pull-down |
| Channel 6 continuity ADC | 7 | ADC1_CH6 — analogue input + 3.3kΩ series + 100kΩ pull-down |
| Channel 7 continuity ADC | 8 | ADC1_CH7 — analogue input + 3.3kΩ series + 100kΩ pull-down |
| Channel 8 continuity ADC | 9 | ADC1_CH8 — analogue input + 3.3kΩ series + 100kΩ pull-down |
| Channel 1 SPDT relay output | 11 | Digital output |
| Channel 2 SPDT relay output | 12 | Digital output |
| Channel 3 SPDT relay output | 13 | Digital output |
| Channel 4 SPDT relay output | 14 | Digital output |
| Channel 5 SPDT relay output | 15 | Digital output |
| Channel 6 SPDT relay output | 16 | Digital output |
| Channel 7 SPDT relay output | 17 | Digital output |
| Channel 8 SPDT relay output | 18 | Digital output |
| Arm switch sense input | 21 | Digital input with voltage divider (27kΩ/10kΩ) + 3.3V zener clamp (§5.4.3). Senses ARM SENSE node (arm relay COM output). Sole method of arm relay state detection. |
| Key switch sense input | 42 | Digital input with voltage divider (27kΩ/10kΩ) + 3.3V zener clamp (§5.4.3b). Reads the key switch NO output directly, detecting key switch position independently of arm relay state. Used in arming guards, PRE_FIRE checks, and FSM disarm triggers. |
| Arm relay output | 47 | Digital output, via IRLZ44N MOSFET (§5.4.9). Arm relay coil driven through physical key switch AND MOSFET (hardware AND gate). Primary fire path interlock. |
| Siren output | 40 | Digital output, via IRLZ44N MOSFET (§5.4.10) |
| RGB LED strip (status) | 48 | WS2812 8-pixel LED strip via RMT |

**Total: 22 GPIOs used. 3 spare GPIOs (38, 39, 41). GPIO 3 deliberately unused (strapping pin).**

**Notes:**
- GPIO 1–9 plus GPIO 10 are allocated to ADC1 (1 battery + 8 continuity). All 10 ADC1 pins are used; no spare ADC1 pins remain.
- GPIO 3 is a strapping pin (JTAG signal select) and is deliberately not used. Channel 2 continuity is assigned to GPIO 10 (ADC1_CH9) instead.
- Continuity inputs use ADC1 GPIOs (2, 10, 4–9) for analogue band classification. Each requires two external series fusible resistors (1.5 kΩ + 1.8 kΩ) and one 100 kΩ pull-down resistor (24 resistors total). Continuity is sensed via the SPDT relay NC contact. No MOSFET switch is required — the SPDT relay provides inherent isolation.
- SPDT relay outputs on GPIO 11–18, each driven via an IRLZ44N low-side MOSFET (§5.4.10). Active HIGH: GPIO HIGH = MOSFET on = relay energised (NO/fire position). GPIO LOW = relay de-energised (NC/continuity position).
- ADC2 (GPIO 11–20) is unreliable with ESP-NOW active, but GPIO 11–18 are used as digital outputs only — ADC2 conflict does not apply to digital I/O.
- GPIO 21 is the arm relay feedback sense input — reads the ARM SENSE node (arm relay COM output / fire bus) via voltage divider (27 kΩ / 10 kΩ) and 3.3 V zener clamp (§5.4.3). Provides post-energise verification that arm relay contacts are closed and VBAT is on the fire bus. Also used for contact-welding detection.
- GPIO 42 is the key switch sense input — reads the key switch NO output directly via voltage divider (27 kΩ / 10 kΩ) and 3.3 V zener clamp (§5.4.3b). Detects key switch position independently of arm relay state. Used in arming guards, PRE_FIRE checks, and FSM disarm triggers.
- GPIO 40 is the siren output, driven via IRLZ44N MOSFET (§5.4.10).
- GPIO 47 is the arm relay output, driven via IRLZ44N MOSFET (§5.4.9). Arm relay coil positive terminal connected through physical key switch (§5.4.4) — hardware AND gate. Active HIGH. Primary fire path interlock.
- GPIO 48 has the WS2812 8-pixel RGB LED strip for status indication.
- Spare GPIOs 38, 39, 41 available for future expansion.

### C.2 Remote Unit Pin Assignment

| Function | GPIO | ADC/Notes |
|---|---|---|
| Encoder CLK (A) | 4 | Digital input, pull-up, interrupt |
| Encoder DT (B) | 5 | Digital input, pull-up, interrupt |
| Encoder SW (push) | 6 | Digital input, pull-up |
| Arm/disarm switch | 7 | Digital input, pull-up |
| Arm switch LED (red) | 8 | Digital output (built-in series resistor) |
| Fire button | 15 | Digital input, pull-up |
| Fire button LED (red) | 17 | Digital output (built-in series resistor) |
| Fire button LED (green) | 18 | Digital output (built-in series resistor) |
| Battery voltage ADC | 1 | ADC1_CH0 — safe with Wi-Fi/ESP-NOW |
| Buzzer | 16 | Digital output |
| Display SPI MOSI | 11 | SPI2 MOSI |
| Display SPI SCLK | 12 | SPI2 CLK |
| Display CS | 10 | Chip select |
| Display DC | 13 | Data/command |
| Display RST | 14 | Reset |
| Display backlight | 21 | Digital output, always HIGH (100% brightness) |
| Display MISO | 9 | SPI2 MISO |
| RGB LED (status) | 48 | WS2812 8-pixel strip + parallel on-board LED via RMT |

**Total: 17 GPIOs + 1 on-board LED = 18 pins used. 7 spare GPIOs.**

**Spare GPIOs available:** 2, 38, 39, 40, 41, 42, 47 — available for future expansion (e.g., SD card, additional buttons, external status LEDs).

**Notes:**
- Battery ADC is on GPIO 1 (ADC1_CH0), same as base unit for code reuse.
- Display SPI uses SPI2_HOST (HSPI). Pins 11 (MOSI), 12 (SCLK) are the default FSPI pins and work well for SPI2 when remapped.
- Touch controller pins on the LCD module are left unconnected.

---

## Appendix D — Protocol Exception Handling

This appendix provides a comprehensive reference of all protocol exceptions and their handling.

### D.1 Link Establishment Exceptions

| Exception | Scenario | Handling |
|---|---|---|
| No LINK_ACK received | Base off, out of range, wrong channel | Remote retries every 2s × 15, then every 5s indefinitely. Display retry count. |
| LINK_ACK from unknown MAC | Different base responds | Remote silently ignores (only accepts configured MAC). |
| LINK_REQUEST from unknown MAC | Rogue remote | Base silently ignores (only accepts configured MAC). |
| Firmware version mismatch | Different code versions | Remote displays "FIRMWARE MISMATCH" with both versions. Stops retrying. Requires power cycle and reflash. |
| LINK_REQUEST while already linked (same MAC) | Remote rebooted | If base is in IDLE or LINK_LOST: base generates new session token, responds normally (session reset). Not an error. **If base is in ARMED, PRE_FIRE, FIRING, or POST_FIRE: LINK_REQUEST is silently ignored.** Session reset during safety-critical or cooldown states is blocked to prevent disruption. The remote's retry mechanism will deliver a subsequent request after the base returns to IDLE. |
| Duplicate LINK_ACK received | Network duplicate | Remote ignores if already in IDLE. If token differs, accept new token (session reset). |

### D.2 Heartbeat Exceptions

| Exception | Scenario | Handling |
|---|---|---|
| PONG not received within 500 ms | Single packet loss | Increment failure counter. Remote buzzer: 80 ms beep (no strip indication, §11.2). |
| 3 consecutive PONG failures | Sustained link loss | Both units → LINK_LOST. Base disarms. Siren. Buzzer alarm. |
| PONG with wrong ping_timestamp | Stale/mismatched pong | Discard silently. Do NOT count as success. Failure counter continues. |
| PING received after link loss | Remote recovering | Base responds with PONG. Both → IDLE. |
| PING before link established | Premature ping | Base ignores (no valid session). |

### D.3 Command Exceptions

| Exception | Scenario | Handling |
|---|---|---|
| CMD_ARM no ACK (timeout) | Packet lost | Retry once. Second timeout: display "ARM FAILED — NO RESPONSE", remain IDLE. |
| CMD_ARM NACK'd | Guard failed | Display human-readable reason (§6.3.3 table) for 3 seconds. Buzzer triple beep. Remain IDLE. |
| CMD_FIRE no ACK (timeout) | Packet lost | **NO RETRY.** Abort fire. Display "FIRE FAILED — NO RESPONSE". Send CMD_DISARM. Return to IDLE. |
| CMD_FIRE NACK'd | Guard failed | Abort fire. Display reason. Send CMD_DISARM. Return to IDLE. |
| CMD_DISARM no ACK (timeout) | Packet lost | Retry once. If still no response: remote → IDLE locally. Display "DISARM SENT — NO CONFIRMATION". |
| CMD_CEASE_FIRE no ACK (timeout) | Packet lost | Retry once. Remote → IDLE regardless. Base will self-disarm via authorization timeout. |
| Invalid session token | Session desync | Base silently discards. Remote detects via heartbeat failure → re-link. |
| Sequence replay | Replay attack or desync | Base NACK reason 0x08. Remote displays error. Persistent: remote should re-link. |
| Invalid integrity CRC | Corruption or key mismatch | Base NACK reason 0x06. Remote displays "INTEGRITY ERROR". |
| Command in wrong state | e.g., CMD_FIRE in IDLE | Base NACK reason 0x05. |
| Repeated CMD_FIRE stops | Operator released button | Base waits 500 ms. No CMD_FIRE → abort fire, relay_all_safe(), → IDLE. |

### D.4 Status Update Exceptions

| Exception | Scenario | Handling |
|---|---|---|
| No STATUS_UPDATE for 5000 ms | Link degraded or base stuck | Remote: display "STALE DATA". If ARMED: send CMD_DISARM → IDLE. If IDLE: warning only. |
| STATUS_UPDATE shows unexpected state | State desync | Remote syncs to base state. Base is authoritative. |
| STATUS_UPDATE shows continuity → OPEN (armed ch) | Igniter disconnected | During ARMED/PRE_FIRE: the band change is real (relay on NC) and **disarms the base** (§7.2.7, v1.35). During FIRING: the armed channel reads OPEN because the relay NC contact is physically disconnected (expected, ignored). |
| STATUS_UPDATE shows continuity SHORT or MARGINAL | Connection quality change | Remote updates display indicators. Informational only — does not trigger disarm. |
| STATUS_UPDATE shows base in ERROR | Base fault | Remote displays "BASE ERROR". Refuses ARM commands. |

---

*End of Functional Specification — RLC-FSPEC-001 v1.44*
