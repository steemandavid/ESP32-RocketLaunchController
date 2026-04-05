# ESP32 Rocket Launch Controller — Base Unit Hardware Test Specification

**Document ID:** RLC-HWTEST-BASE-001
**Version:** 1.1
**Date:** 2026-03-23
**Author:** David (System Analyst)
**Status:** Draft for Development
**Target Platform:** ESP32-S3 (ESP-IDF framework)
**Board:** ESP32-S3-DevKitC-1 with ESP32-S3-WROOM-1 N16R8 module
**Aligned with:** RLC-FSPEC-001 v1.10

---

## Revision History

| Version | Date | Changes |
|---|---|---|
| 1.0 | 2026-03-23 | Initial draft (aligned with FSD v1.8) |
| 1.1 | 2026-03-23 | Aligned with FSD v1.10. Removed low-side relay (GPIO 21 repurposed as arm switch sense), relay feedback (GPIO 38), arm switch digital input (GPIO 39), continuity MOSFET (GPIO 41). SPDT relays via 2x ULN2003A. Arm switch sensed via zener-clamped sense circuit on GPIO 21. Continuity always-on via SPDT NC contact. Updated all tests, commands, and pin table. |

---

## 1. Purpose

This document specifies a standalone hardware test firmware for the RLC base unit. The firmware exercises every hardware peripheral in isolation, using a UART serial command interface for manual testing. It contains no ESP-NOW communication, no state machine, and no protocol logic.

**This is an independent ESP-IDF project** — it shares no code with the main RLC codebase. Its sole purpose is to validate that all base unit hardware is correctly wired and functioning before main system development begins.

### 1.1 Relationship to Main Project

| Aspect | Hardware Test | Main RLC Project |
|--------|--------------|-------------------|
| Codebase | Independent, standalone | `rlc/` with `rlc_common`, `rlc_base`, `rlc_remote` |
| Location | `rlc-hw-test-base/` | `rlc/` (main project root) |
| ESP-NOW | Not used | Core communication |
| State machine | Not used | Full FSM |
| Code reuse | None — test code is disposable | Production quality |
| Purpose | Validate hardware | Production firmware |

---

## 2. Board and Constraints

ESP32-S3-DevKitC-1 with ESP32-S3-WROOM-1 N16R8 (16 MB Flash, 8 MB Octal PSRAM).

| Constraint | Pins affected | Reason |
|---|---|---|
| Strapping pins — do not use | GPIO 0, 3, 45, 46 | Boot mode selection, JTAG |
| Octal PSRAM — not available | GPIO 33, 34, 35, 36, 37 | Internal SPI bus for PSRAM |
| USB — reserved | GPIO 19, 20 | USB D+/D- |
| UART0 — reserved | GPIO 43, 44 | Serial debug/programming (used for test CLI) |
| On-board RGB LED | GPIO 47 | WS2812 — used for status |

---

## 3. Pin Assignments Under Test

All pin assignments match the main RLC FSD (RLC-FSPEC-001 v1.10, Appendix C.1).

| Function | GPIO | Type | Notes |
|---|---|---|---|
| Battery voltage ADC | 1 | ADC1_CH0 | Analogue input, voltage divider |
| Channel 1 continuity ADC | 2 | ADC1_CH1 | Analogue input + 3.3kΩ series (1.5k+1.8k fusible) + 100kΩ pull-down |
| Channel 2 continuity ADC | 10 | ADC1_CH9 | Analogue input + 3.3kΩ series (1.5k+1.8k fusible) + 100kΩ pull-down |
| Channel 3 continuity ADC | 4 | ADC1_CH3 | Analogue input + 3.3kΩ series (1.5k+1.8k fusible) + 100kΩ pull-down |
| Channel 4 continuity ADC | 5 | ADC1_CH4 | Analogue input + 3.3kΩ series (1.5k+1.8k fusible) + 100kΩ pull-down |
| Channel 5 continuity ADC | 6 | ADC1_CH5 | Analogue input + 3.3kΩ series (1.5k+1.8k fusible) + 100kΩ pull-down |
| Channel 6 continuity ADC | 7 | ADC1_CH6 | Analogue input + 3.3kΩ series (1.5k+1.8k fusible) + 100kΩ pull-down |
| Channel 7 continuity ADC | 8 | ADC1_CH7 | Analogue input + 3.3kΩ series (1.5k+1.8k fusible) + 100kΩ pull-down |
| Channel 8 continuity ADC | 9 | ADC1_CH8 | Analogue input + 3.3kΩ series (1.5k+1.8k fusible) + 100kΩ pull-down |
| Channel 1 SPDT relay output | 11 | Digital output | Active HIGH via ULN2003A IC1 |
| Channel 2 SPDT relay output | 12 | Digital output | Active HIGH via ULN2003A IC1 |
| Channel 3 SPDT relay output | 13 | Digital output | Active HIGH via ULN2003A IC1 |
| Channel 4 SPDT relay output | 14 | Digital output | Active HIGH via ULN2003A IC1 |
| Channel 5 SPDT relay output | 15 | Digital output | Active HIGH via ULN2003A IC1 |
| Channel 6 SPDT relay output | 16 | Digital output | Active HIGH via ULN2003A IC1 |
| Channel 7 SPDT relay output | 17 | Digital output | Active HIGH via ULN2003A IC1 |
| Channel 8 SPDT relay output | 18 | Digital output | Active HIGH via ULN2003A IC2 |
| Arm switch sense input | 21 | Digital input | External 10kΩ + 3.3V zener + 100kΩ pull-down. HIGH=armed. |
| Siren output | 40 | Digital output | Active HIGH via ULN2003A IC2 |
| RGB LED (status) | 47 | WS2812 | On-board, RMT peripheral |

**Spare GPIOs:** 38, 39, 41, 42, 48

---

## 4. Project Structure

```
rlc-hw-test-base/
├── main/
│   ├── main.c                # Entry point, boot safety
│   ├── cli.c                 # UART command parser and handlers
│   ├── cli.h
│   ├── hw_relay.c            # SPDT relay output control (8 channels, via ULN2003A)
│   ├── hw_relay.h
│   ├── hw_continuity.c       # ADC continuity sensing (8 channels, always-on via NC)
│   ├── hw_continuity.h
│   ├── hw_battery.c          # Battery voltage ADC
│   ├── hw_battery.h
│   ├── hw_inputs.c           # Arm switch sense input
│   ├── hw_inputs.h
│   ├── hw_siren.c            # Siren output control (via ULN2003A)
│   ├── hw_siren.h
│   ├── hw_rgb_led.c          # WS2812 RGB LED driver
│   ├── hw_rgb_led.h
│   ├── hw_fire_timer.c       # Hardware timer for fire pulse
│   ├── hw_fire_timer.h
│   └── pin_config.h          # All GPIO numbers and polarities
├── CMakeLists.txt
└── README.md
```

---

## 5. Serial Command Interface

The firmware SHALL provide a UART0 command-line interface at **115200 baud** (8N1). Commands are newline-terminated. The prompt SHALL be `base> `. Unknown commands print `Unknown command. Type 'help' for usage.`

### 5.1 General Commands

| Command | Description |
|---|---|
| `help` | Print all available commands with brief descriptions |
| `status` | Print complete system status: arm switch sense, battery voltage, all continuity channels |
| `safe` | Immediately de-energise all SPDT relays. Equivalent to `relay_all_safe()`. |
| `pins` | Print all pin assignments and their current logical/physical states |

### 5.2 Relay Commands

| Command | Description |
|---|---|
| `relay <ch> on` | Energise channel SPDT relay (1–8). Switches from NC (continuity) to NO (fire path). |
| `relay <ch> off` | De-energise channel SPDT relay (1–8). Returns to NC (continuity). |
| `relay all off` | De-energise all 8 channel SPDT relays |
| `relay sweep` | Energise each channel relay in sequence (1→8), 500 ms each. Allows visual/audible verification of each relay clicking. |

### 5.3 Continuity Commands

Continuity sensing is always active when relays are de-energised (NC position). No MOSFET enable/disable required — the SPDT relay NC/NO switching provides inherent isolation.

| Command | Description |
|---|---|
| `cont <ch>` | Read channel (1–8) continuity: raw ADC value, calibrated µV, band classification (SHORT/GOOD/MARGINAL/OPEN) |
| `cont all` | Read all 8 channels sequentially |
| `cont <ch> raw <N>` | Take N raw ADC samples on channel (1–8) and display individual values, mean, min, max, std deviation. For noise floor analysis. Default N=64. |
| `cont monitor` | Continuously sample all 8 channels in round-robin (100 ms per channel) and display band changes. Press any key to stop. |

### 5.4 Battery Commands

| Command | Description |
|---|---|
| `batt` | Read battery voltage: raw ADC, calibrated mV, scaled voltage (using DIVIDER_RATIO) |
| `batt raw <N>` | Take N raw ADC samples and display statistics. Default N=8. |

### 5.5 Input Commands

| Command | Description |
|---|---|
| `arm` | Poll arm switch sense input (GPIO 21). Displays raw GPIO level and debounced state (ARMED=HIGH, DISARMED=LOW). Continuously polls until key press. |

### 5.6 Siren Commands

| Command | Description |
|---|---|
| `siren on` | Activate siren output (via ULN2003A IC2) |
| `siren off` | Deactivate siren output |
| `siren pulse <on_ms> <off_ms> <count>` | Pulse siren: on_ms active, off_ms inactive, repeated count times. E.g., `siren pulse 500 500 4` |
| `siren test` | Run predefined siren patterns: ARMED (500/500 × 3), PRE_FIRE (continuous 2s), FIRING (continuous 2s), LINK_LOST (500/500 × 4), ERROR (200/200 × 3), CONTINUITY_LOST (200/200 × 3) |

### 5.7 RGB LED Commands

| Command | Description |
|---|---|
| `led <r> <g> <b>` | Set RGB LED to specified colour (0–255 each) |
| `led off` | Turn off RGB LED |
| `led test` | Cycle through all FSD §11.1 status patterns: BOOT (blue slow pulse), IDLE arm OFF (green solid), IDLE arm ON (green fast blink 250ms), ARMED (red slow blink 500ms), PRE_FIRE (red fast blink 100ms), FIRING (red solid), POST_FIRE (yellow solid), LINK_LOST (yellow fast blink 200ms), ERROR (red triple flash). |
| `led brightness <0-255>` | Set LED brightness scaling factor |

### 5.8 Fire Timer Commands

| Command | Description |
|---|---|
| `fire <ch> <ms>` | Energise channel SPDT relay (NC→NO) for specified duration (ms) using hardware timer, then auto-safe. Timer callback signals task context for relay deactivation (matching main FSD §7.4.2 design). Reports actual measured duration. |
| `fire <ch> <ms> nosafe` | Same but does NOT call safe after — leaves relay in current state for inspection |

---

## 6. Implementation Requirements

### 6.1 GPIO Initialisation

At boot, before any other operation:
1. Configure all 8 channel SPDT relay GPIOs (11–18) as outputs, drive inactive (de-energised / NC position).
2. Configure siren GPIO (40) as output, drive inactive.
3. Only then initialise ADC, arm switch sense input, RGB LED, UART CLI, and other peripherals.

This mirrors the main FSD §9.7 boot safety requirement.

### 6.2 Configurable Polarity

All digital outputs SHALL use configurable polarity defined in `pin_config.h`:

```c
#define PIN_RELAY_CH1          11
#define PIN_RELAY_CH1_ACTIVE   1    // 1 = active HIGH (ULN2003A input logic)

#define PIN_SIREN              40
#define PIN_SIREN_ACTIVE       1    // Active HIGH (via ULN2003A IC2)
```

### 6.3 ADC Configuration

- All ADC readings SHALL use ADC1 with the ESP-IDF v5.x calibration API (`adc_cali_raw_to_voltage()`).
- Continuity channels: 12-bit resolution, 64-sample oversampling per reading.
- Battery channel: 12-bit resolution, 8-sample averaging.
- `DIVIDER_RATIO` for battery voltage SHALL be configurable in `pin_config.h` (default: 4.3, for 33 kΩ + 10 kΩ divider).

### 6.4 Continuity Band Classification

Thresholds use microvolt integer constants (matching main FSD §14.5):

| Constant | Default (µV) | Band |
|---|---|---|
| `CONT_SHORT_UV` | 500 | Below = SHORT |
| `CONT_MARGINAL_UV` | 66000 | Above = MARGINAL |
| `CONT_OPEN_UV` | 1500000 | Above = OPEN |
| `CONT_HYSTERESIS_SHORT_UV` | 200 | SHORT/GOOD boundary hysteresis |
| `CONT_HYSTERESIS_MARGINAL_UV` | 5000 | GOOD/MARGINAL boundary hysteresis |
| `CONT_HYSTERESIS_OPEN_UV` | 50000 | MARGINAL/OPEN boundary hysteresis |

### 6.5 Fire Timer

The fire timer SHALL use a hardware timer (not `vTaskDelay`). The timer callback SHALL only signal the main task via `xTaskNotifyFromISR()`. The main task SHALL deactivate relays and report timing results. This validates the same ISR-to-task signalling pattern used in the main FSD (§7.4.2).

### 6.6 RGB LED

WS2812 single-pixel driver using ESP32-S3 RMT peripheral on GPIO 47. Brightness scaling configurable. The `led test` command SHALL cycle through all patterns defined in the main FSD §11.1.

### 6.7 Arm Switch Sense

The arm switch sense input on GPIO 21 uses an external circuit (10 kΩ series + 3.3V zener clamp + 100 kΩ pull-down) per FSD §5.4.3. No internal pull-up/pull-down is used. HIGH = VBAT present on fire path (arm switch ON). LOW = no VBAT (arm switch OFF or battery disconnected).

### 6.8 SPDT Relay and ULN2003A

Each channel uses a 12V automotive SPDT relay driven via ULN2003A darlington array:
- IC1 (ULN2003A): channels 1–7
- IC2 (ULN2003A): channel 8 + siren

GPIO HIGH → ULN2003A sinks → relay coil energised (NC→NO). GPIO LOW → relay de-energised (→NC). Internal freewheel diodes in ULN2003A protect against coil inductive kick. At boot, ULN2003A inputs are pulled low by internal base resistors — hardware fail-safe.

---

## 7. Test Procedures

Each test validates one hardware subsystem. Tests are performed manually using the serial CLI. Pass/fail is determined by the operator.

### 7.1 Relay Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| B-R01 | Individual SPDT relay activation | `relay <ch> on` for each channel 1–8. Observe relay click or measure GPIO with multimeter/logic analyser. `relay <ch> off` after each. | Each relay energises (NC→NO) and de-energises (→NC) cleanly. GPIO matches expected active HIGH level. |
| B-R02 | Relay sweep | `relay sweep`. Listen for 8 sequential clicks. | 8 distinct relay activations heard/measured, ~500 ms apart. |
| B-R03 | All-safe | `relay 1 on`, `relay 3 on`. Then `safe`. Verify all relays de-energised. | All relay GPIOs return to inactive state (LOW). |

### 7.2 Continuity Tests

Requires test resistors connected to channel terminals via SPDT relay NC contact: 0 Ω wire, 2 Ω, 100 Ω, open circuit.

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| B-C01 | Always-on continuity | With all relays de-energised (NC), `cont 1`. | Valid reading returned — continuity active via NC contact without any enable step. |
| B-C02 | SHORT classification | Connect 0 Ω wire to channel 1 terminals. `cont 1`. | Band = SHORT, voltage < 500 µV. |
| B-C03 | GOOD classification | Connect 2 Ω resistor. `cont 1`. | Band = GOOD, voltage ~660 µV. |
| B-C04 | MARGINAL classification | Connect 100 Ω resistor. `cont 1`. | Band = MARGINAL, voltage ~97000 µV. |
| B-C05 | OPEN classification | Leave terminals open. `cont 1`. | Band = OPEN, voltage ~3190000 µV (3.19V). |
| B-C06 | All channels | Connect known resistors to all 8 channels. `cont all`. | Each channel reports correct band. |
| B-C07 | Noise floor analysis | `cont 1 raw 256` with 2 Ω resistor. | Standard deviation < 2 mV. Mean matches expected value ±5 mV. |
| B-C08 | Hysteresis stability | `cont monitor` with resistor near a threshold boundary. Observe for 30 seconds. | No spurious band transitions. |
| B-C09 | Continuity isolation during fire | `cont 1` (read GOOD). `relay 1 on` (energise → NC disconnected). `cont 1`. | Reads OPEN (expected — NC contact disconnected). Confirms SPDT isolation. |
| B-C10 | Post-fire reconnection | `relay 1 on`. Wait 500ms. `relay 1 off`. Wait 50ms. `cont 1`. | Reads GOOD again — NC contact reconnected after relay de-energises. |

### 7.3 Battery Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| B-B01 | Battery voltage reading | `batt` with known supply voltage. | Scaled voltage matches supply ±100 mV. |
| B-B02 | Battery ADC stability | `batt raw 64`. | Standard deviation < 20 mV. Mean matches expected. |
| B-B03 | Divider ratio | Measure actual voltage with multimeter. Compare to `batt` output. | Ratio matches `DIVIDER_RATIO` ±2%. |

### 7.4 Arm Switch Sense Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| B-I01 | Arm switch — disarmed | Leave arm switch OFF. `arm`. | Reports DISARMED, GPIO LOW (0). |
| B-I02 | Arm switch — armed | Turn arm switch ON. `arm`. | Reports ARMED, GPIO HIGH (1). VBAT present on fire path. |
| B-I03 | Arm switch — toggle | `arm` (continuous). Toggle switch multiple times. | State changes detected cleanly, no bouncing. |
| B-I04 | Arm sense with battery disconnected | Arm switch ON but battery disconnected. `arm`. | Reports DISARMED (LOW) — correctly detects no VBAT on fire path even with switch ON. |

### 7.5 Siren Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| B-S01 | Siren on/off | `siren on`. Verify audible. `siren off`. Verify silent. | Siren activates and deactivates. |
| B-S02 | Siren pulse | `siren pulse 500 500 4`. | 4 pulses, each ~500 ms on / 500 ms off. |
| B-S03 | Siren patterns | `siren test`. | All six patterns play distinctly (ARMED, PRE_FIRE, FIRING, LINK_LOST, ERROR, CONTINUITY_LOST). |

### 7.6 RGB LED Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| B-L01 | Colour accuracy | `led 255 0 0`, `led 0 255 0`, `led 0 0 255`. | Red, green, blue displayed correctly. |
| B-L02 | Pattern test | `led test`. | All 9 status patterns cycle correctly matching FSD §11.1: BOOT, IDLE (arm OFF), IDLE (arm ON), ARMED, PRE_FIRE, FIRING, POST_FIRE, LINK_LOST, ERROR. |
| B-L03 | Brightness | `led brightness 10`, `led 255 255 255`. Then `led brightness 255`, `led 255 255 255`. | Visible brightness difference. |

### 7.7 Fire Timer Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| B-F01 | Fire pulse timing | `fire 1 2000`. Measure channel 1 relay GPIO with oscilloscope. | SPDT relay ON (energised) duration = 2000 ms ±500 µs. Relay de-energises after. |
| B-F02 | Fire pulse — short | `fire 1 100`. | ON duration = 100 ms ±500 µs. |
| B-F03 | Fire pulse — safe during fire | `fire 1 5000`. During pulse, type `safe`. | Relays de-energise immediately. Timer does not re-activate relay after expiry. |
| B-F04 | Fire all channels | `fire <ch> 1000` for each channel 1–8. | Each channel SPDT relay energises for ~1000 ms. |
| B-F05 | Task-context verification | `fire 1 2000`. Observe log output. | Log shows "Timer ISR: signalling task" followed by "Task: relay_all_safe() called". Confirms ISR does not drive GPIO directly. |

### 7.8 Boot Safety Test

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| B-BS01 | Safe boot state | Power cycle. Measure all SPDT relay GPIOs during boot with logic analyser. | All relay outputs are inactive (LOW) within 1 ms of GPIO init. No glitches. |
| B-BS02 | Boot order | Observe log output during boot. | GPIO init logged before ADC, RGB LED, and UART CLI init. |

---

## 8. Build and Flash

```bash
cd rlc-hw-test-base
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The serial monitor serves as both the test CLI and the log output.

---

*End of Base Unit Hardware Test Specification — RLC-HWTEST-BASE-001 v1.1*
