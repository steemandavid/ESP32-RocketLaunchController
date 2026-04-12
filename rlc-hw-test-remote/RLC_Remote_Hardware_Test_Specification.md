# ESP32 Rocket Launch Controller — Remote Unit Hardware Test Specification

**Document ID:** RLC-HWTEST-REMOTE-001
**Version:** 1.0
**Date:** 2026-03-23
**Author:** David (System Analyst)
**Status:** Draft for Development
**Target Platform:** ESP32-S3 (ESP-IDF framework)
**Board:** ESP32-S3-DevKitC-1 with ESP32-S3-WROOM-1 N16R8 module

---

## 1. Purpose

This document specifies a standalone hardware test firmware for the RLC remote unit. The firmware exercises every hardware peripheral in isolation, using a UART serial command interface for manual testing. It contains no ESP-NOW communication, no state machine, and no protocol logic.

**This is an independent ESP-IDF project** — it shares no code with the main RLC codebase. Its sole purpose is to validate that all remote unit hardware is correctly wired and functioning before main system development begins.

### 1.1 Relationship to Main Project

| Aspect | Hardware Test | Main RLC Project |
|--------|--------------|-------------------|
| Codebase | Independent, standalone | `rlc/` with `rlc_common`, `rlc_base`, `rlc_remote` |
| Location | `rlc-hw-test-remote/` | `rlc/` (main project root) |
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
| On-board RGB LED | GPIO 48 | WS2812 — used for status |

---

## 3. Pin Assignments Under Test

All pin assignments match the main RLC FSD (RLC-FSPEC-001 v1.8, Appendix C.2).

| Function | GPIO | Type | Notes |
|---|---|---|---|
| Encoder CLK (A) | 4 | Digital input | Internal pull-up, interrupt-driven |
| Encoder DT (B) | 5 | Digital input | Internal pull-up, interrupt-driven |
| Encoder SW (push) | 6 | Digital input | Internal pull-up |
| Arm/disarm switch | 7 | Digital input | Internal pull-up |
| Fire button | 15 | Digital input | Internal pull-up |
| Battery voltage ADC | 1 | ADC1_CH0 | Analogue input, voltage divider |
| Buzzer | 16 | Digital output | Configurable polarity |
| Display SPI MOSI | 11 | SPI2 MOSI | ILI9341 data in |
| Display SPI SCLK | 12 | SPI2 CLK | ILI9341 clock |
| Display CS | 10 | Digital output | ILI9341 chip select |
| Display DC | 13 | Digital output | ILI9341 data/command |
| Display RST | 14 | Digital output | ILI9341 hardware reset |
| Display backlight | 21 | Digital output | Always HIGH (100% brightness) |
| Display MISO | 9 | SPI2 MISO | ILI9341 data out (read-back) |
| RGB LED (status) | 48 | WS2812 | On-board, RMT peripheral |

---

## 4. Project Structure

```
rlc-hw-test-remote/
├── main/
│   ├── main.c                # Entry point, CLI dispatcher
│   ├── cli.c                 # UART command parser and handlers
│   ├── cli.h
│   ├── hw_encoder.c          # Rotary encoder driver (A/B + push button)
│   ├── hw_encoder.h
│   ├── hw_buttons.c          # Fire button, arm switch
│   ├── hw_buttons.h
│   ├── hw_display.c          # ILI9341 SPI display driver
│   ├── hw_display.h
│   ├── hw_buzzer.c           # Buzzer output control
│   ├── hw_buzzer.h
│   ├── hw_battery.c          # Battery voltage ADC
│   ├── hw_battery.h
│   ├── hw_rgb_led.c          # WS2812 RGB LED driver
│   ├── hw_rgb_led.h
│   └── pin_config.h          # All GPIO numbers and polarities
├── CMakeLists.txt
└── README.md
```

---

## 5. Serial Command Interface

The firmware SHALL provide a UART0 command-line interface at **115200 baud** (8N1). Commands are newline-terminated. The prompt SHALL be `remote> `. Unknown commands print `Unknown command. Type 'help' for usage.`

### 5.1 General Commands

| Command | Description |
|---|---|
| `help` | Print all available commands with brief descriptions |
| `status` | Print complete system status: all input states, ADC readings, display status, buzzer state |
| `pins` | Print all pin assignments and their current logical/physical states |

### 5.2 Encoder Commands

| Command | Description |
|---|---|
| `enc monitor` | Continuously display encoder events: rotation direction (CW/CCW), step count, push button press/release. Press any key to stop. |
| `enc count` | Display cumulative rotation count since last reset (signed: positive = CW, negative = CCW). |
| `enc reset` | Reset rotation counter to 0. |
| `enc channel` | Simulate channel selection: display current channel (1–8), update on rotation (wrapping). Press any key to stop. |
| `enc button` | Monitor push button only: display raw GPIO, debounced state (pressed/released), and press duration (ms). Press any key to stop. |
| `enc longpress` | Test long-press detection: display "SHORT PRESS" for < 500 ms, "LONG PRESS" for >= 500 ms (measured from debounced transition). Press any key to stop. |

### 5.3 Button and Switch Commands

| Command | Description |
|---|---|
| `fire monitor` | Monitor fire button: raw GPIO, 8-bit shift register value (hex), debounced state (0x00=pressed, 0xFF=released), fresh-press detection. Press any key to stop. |
| `fire fresh` | Test fresh-press detection: hold button before starting command, then release and re-press. Only the re-press should register as "FRESH PRESS". Press any key to stop. |
| `arm monitor` | Monitor arm switch: raw GPIO, 16-bit shift register value (hex), debounced state (ARMED/DISARMED). Toggle switch to verify. Press any key to stop. |

### 5.4 Display Commands

| Command | Description |
|---|---|
| `disp init` | Initialise ILI9341 display: SPI bus setup, hardware reset sequence, read display ID register. Report success/failure and display ID. |
| `disp id` | Read and display the ILI9341 identification register (command 0x04). |
| `disp fill <r> <g> <b>` | Fill entire screen with specified RGB888 colour. E.g., `disp fill 255 0 0` for red. |
| `disp test` | Run display test pattern: red, green, blue, white, black fills (1 second each), then horizontal colour bars, then vertical colour bars. |
| `disp text <string>` | Display text string at centre of screen (white on black). Basic font rendering. |
| `disp grid` | Draw 8-cell grid simulating channel layout from main FSD §10.2.2. Numbered 1–8 with colour-coded symbols: blue circle, yellow triangle, red circle, orange diamond. |
| `disp gradient` | Display horizontal gradient from black to white. Tests colour depth and SPI data integrity. |
| `disp backlight on` | Turn backlight on (GPIO 21 HIGH) |
| `disp backlight off` | Turn backlight off (GPIO 21 LOW) |
| `disp speed` | Measure full-screen fill time at current SPI clock. Report ms and effective pixel rate. |
| `disp pixel <x> <y> <r> <g> <b>` | Set a single pixel. For verifying coordinate system and orientation. |
| `disp rect <x> <y> <w> <h> <r> <g> <b>` | Draw filled rectangle. For testing partial update (dirty-rectangle) capability. |

### 5.5 Buzzer Commands

| Command | Description |
|---|---|
| `buzz on` | Activate buzzer output |
| `buzz off` | Deactivate buzzer output |
| `buzz beep <ms>` | Single beep of specified duration. E.g., `buzz beep 100`. |
| `buzz pattern <on_ms> <off_ms> <count>` | Play pattern: on_ms active, off_ms inactive, repeated count times. E.g., `buzz pattern 100 100 3` for triple beep. |
| `buzz test` | Run all predefined buzzer patterns from main FSD §12.1: BEEP_SHORT, BEEP_DOUBLE, BEEP_TRIPLE, BEEP_LONG, BEEP_PING_FAIL, BEEP_CONTINUITY_LOST, ALARM_LINK_LOST (3 cycles), ALARM_CRITICAL (3 cycles). 1 second gap between patterns. |

### 5.6 Battery Commands

| Command | Description |
|---|---|
| `batt` | Read battery voltage: raw ADC, calibrated mV, scaled voltage (using DIVIDER_RATIO) |
| `batt raw <N>` | Take N raw ADC samples and display statistics (mean, min, max, std deviation). Default N=8. |

### 5.7 RGB LED Commands

| Command | Description |
|---|---|
| `led <r> <g> <b>` | Set RGB LED to specified colour (0–255 each) |
| `led off` | Turn off RGB LED |
| `led test` | Cycle through all remote status colours from main FSD §11.2: blue pulse, green solid, red blink, red fast blink, red solid, yellow fast blink, red triple flash, orange flash. 2 seconds each. |
| `led brightness <0-255>` | Set LED brightness scaling factor |

### 5.8 Debounce Visualisation Commands

| Command | Description |
|---|---|
| `debounce fire` | Display fire button's 8-bit shift register in real-time (binary and hex), updated every 10 ms poll. Shows debounce convergence visually. Press any key to stop. |
| `debounce arm` | Display arm switch's 16-bit shift register in real-time, updated every 10 ms poll. Press any key to stop. |
| `debounce encoder` | Display encoder push button's 16-bit shift register in real-time. Press any key to stop. |

---

## 6. Implementation Requirements

### 6.1 GPIO Initialisation

At boot:
1. Configure buzzer GPIO (16) as output, drive inactive.
2. Configure display backlight GPIO (21) as output, drive HIGH (always on).
3. Configure all input GPIOs (4, 5, 6, 7, 15) with internal pull-ups enabled.
4. Only then initialise SPI bus, display, ADC, RGB LED, UART CLI.

### 6.2 Configurable Polarity

Digital outputs SHALL use configurable polarity defined in `pin_config.h`:

```c
#define PIN_BUZZER              16
#define PIN_BUZZER_ACTIVE       1    // 1 = active HIGH, 0 = active LOW

#define PIN_DISPLAY_BACKLIGHT   21
#define PIN_BACKLIGHT_ACTIVE    1    // Always HIGH for 100% brightness
```

### 6.3 ADC Configuration

- Battery ADC on GPIO 1 (ADC1_CH0) SHALL use the ESP-IDF v5.x calibration API (`adc_cali_raw_to_voltage()`).
- 12-bit resolution, 8-sample averaging.
- `DIVIDER_RATIO` for battery voltage SHALL be configurable in `pin_config.h` (default: 2.0 for single-cell LiPo).

### 6.4 Rotary Encoder

- A/B pins (GPIO 4, 5): interrupt-driven with Gray code quadrature state machine (preferred) or edge detection with 2 ms lockout.
- Push button (GPIO 6): 16-bit shift-register debounce, 10 ms polling, 160 ms debounce time.
- The encoder driver SHALL track direction (CW/CCW) and step count.

### 6.5 Fire Button Debounce

- GPIO 15 with internal pull-up.
- **8-bit** shift-register debounce, 10 ms polling, 80 ms debounce time.
- Stable values: 0x00 = pressed, 0xFF = released.
- Fresh-press detection: must transition from 0xFF to 0x00 to register. A button held at boot does not count.

### 6.6 Arm Switch Debounce

- GPIO 7 with internal pull-up.
- 16-bit shift-register debounce, 10 ms polling, 160 ms debounce time.
- Stable values: 0x0000 = ARMED, 0xFFFF = DISARMED.
- Fail-safe: disconnected wire = HIGH = DISARMED.

### 6.7 ILI9341 Display

- Controller: ILI9341, 240 × 320 pixels, 4-wire SPI.
- SPI bus: SPI2_HOST on ESP32-S3.
- Pins: MOSI=11, SCLK=12, CS=10, DC=13, RST=14, MISO=9, backlight=21.
- Colour depth: 16-bit (RGB565). The driver accepts RGB888 and converts to RGB565 for transmission.
- SPI clock: start at 20 MHz. Report actual clock achieved.
- Hardware reset sequence: RST LOW for 10 ms, then HIGH, then wait 120 ms before sending commands.
- Display ID read-back: command 0x04 ("Read Display Identification Information") via SPI. Expected response identifies ILI9341. If read-back fails, report error but continue (allow testing of other peripherals).
- Rotation: landscape mode (rotation=1).

### 6.8 RGB LED

WS2812 single-pixel driver using ESP32-S3 RMT peripheral on GPIO 48. Same implementation as base unit test.

---

## 7. Test Procedures

Each test validates one hardware subsystem. Tests are performed manually using the serial CLI. Pass/fail is determined by the operator.

### 7.1 Encoder Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| R-E01 | Rotation detection | `enc monitor`. Rotate encoder CW and CCW slowly (1 detent/second). | CW increments, CCW decrements. Every detent registers exactly one step. No double-counts or missed steps. |
| R-E02 | Fast rotation | `enc monitor`. Rotate encoder rapidly (~5 detents/second). | All steps detected, correct direction. No missed or extra steps. |
| R-E03 | Direction reversal | `enc monitor`. Rotate CW 5 steps, then CCW 5 steps. | Net count returns to starting value. No direction errors at reversal point. |
| R-E04 | Channel wrapping | `enc channel`. Start at CH 1, rotate CW through CH 8 and beyond. Then CCW past CH 1. | Wraps 8→1 (CW) and 1→8 (CCW) correctly. |
| R-E05 | Push button debounce | `enc button`. Press and release button rapidly 10 times. | Each press/release detected cleanly. No spurious events. Duration accurate ±20 ms. |
| R-E06 | Long-press detection | `enc longpress`. Short press (~200 ms), then long press (~800 ms). | Short press reports "SHORT PRESS". Long press reports "LONG PRESS". Threshold at 500 ms from debounced transition (~660 ms total hold). |
| R-E07 | Simultaneous rotation + press | `enc monitor`. Rotate while pressing button. | Both rotation and button events reported independently without interference. |

### 7.2 Fire Button Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| R-F01 | Basic press/release | `fire monitor`. Press and release fire button. | Shift register converges to 0x00 on press (80 ms), 0xFF on release (80 ms). |
| R-F02 | Debounce visualisation | `debounce fire`. Press button slowly. | Register fills with 0s from LSB. Stable at 0x00 after 8 consecutive LOW samples. |
| R-F03 | Fresh-press — held at start | `fire fresh`. Hold button before entering command. | No "FRESH PRESS" event. Release, then re-press → "FRESH PRESS" detected. |
| R-F04 | Fresh-press — boot held | Hold fire button, power cycle, then `fire fresh`. | Button held at boot does not register. Release and re-press → "FRESH PRESS". |
| R-F05 | Rapid press/release | `fire monitor`. Tap button rapidly (< 80 ms presses). | Short taps (< 80 ms) may not register (debounce filtering). This is expected and correct. |

### 7.3 Arm Switch Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| R-A01 | Switch states | `arm monitor`. Toggle switch to ARMED and DISARMED. | ARMED = 0x0000 (GPIO LOW), DISARMED = 0xFFFF (GPIO HIGH). |
| R-A02 | Debounce timing | `debounce arm`. Toggle switch slowly. | Register converges over 160 ms (16 samples × 10 ms). |
| R-A03 | Disconnected wire | `arm monitor`. Disconnect switch wire. | Reports DISARMED (fail-safe: pull-up → HIGH). |

### 7.4 Display Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| R-D01 | Initialisation and ID | `disp init`. | SPI initialised, display ID read successfully, matches expected ILI9341 ID. |
| R-D02 | Backlight | `disp backlight off`. Wait 2s. `disp backlight on`. | Screen visibly turns off and on. |
| R-D03 | Colour fills | `disp fill 255 0 0`, `disp fill 0 255 0`, `disp fill 0 0 255`, `disp fill 255 255 255`, `disp fill 0 0 0`. | Each fill covers entire 240×320 area with correct colour. No artefacts. |
| R-D04 | Test pattern | `disp test`. | Colour fills + horizontal and vertical bars render correctly. No corruption. |
| R-D05 | Text rendering | `disp text "Hello RLC"`. | Text displayed at centre, readable, correct font rendering. |
| R-D06 | Channel grid | `disp grid`. | 8-cell grid with numbered channels and colour-coded symbols renders correctly. Layout matches main FSD §10.2.2 design intent. |
| R-D07 | Gradient | `disp gradient`. | Smooth gradient from black to white. No banding or colour steps (validates colour depth). |
| R-D08 | Pixel accuracy | `disp pixel 0 0 255 0 0`, `disp pixel 239 0 0 255 0`, `disp pixel 0 319 0 0 255`, `disp pixel 239 319 255 255 0`. | Pixels appear at exact corners. Confirms coordinate system and display orientation. |
| R-D09 | Partial update | `disp fill 0 0 0`. Then `disp rect 100 100 200 100 255 0 0`. | Black background with red rectangle at correct position and size. Surrounding pixels undisturbed. |
| R-D10 | SPI speed | `disp speed`. | Full-screen fill completes. Reports time in ms. At 20 MHz: expect ~30–60 ms for 240×320×2 bytes. |
| R-D11 | Display ID re-read | After display is initialised: `disp id`. | Returns valid ILI9341 ID. Confirms SPI read-back works during operation (not just at init). |

### 7.5 Buzzer Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| R-B01 | On/off | `buzz on`. Verify audible. `buzz off`. Verify silent. | Buzzer activates and deactivates. |
| R-B02 | Single beep | `buzz beep 100`. | Single 100 ms beep. |
| R-B03 | Pattern | `buzz pattern 100 100 3`. | Triple beep with 100 ms on/off. |
| R-B04 | All patterns | `buzz test`. | All 8 predefined patterns play distinctly with 1 second gaps. |

### 7.6 Battery Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| R-BT01 | Battery voltage | `batt` with known supply. | Scaled voltage matches supply ±100 mV. |
| R-BT02 | ADC stability | `batt raw 64`. | Standard deviation < 20 mV. |

### 7.7 RGB LED Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| R-L01 | Colour accuracy | `led 255 0 0`, `led 0 255 0`, `led 0 0 255`. | Red, green, blue displayed correctly. |
| R-L02 | Pattern test | `led test`. | All 8 remote status patterns cycle correctly (2s each). |
| R-L03 | Brightness | `led brightness 10`, `led 255 255 255`. Then `led brightness 255`. | Visible brightness difference. |

### 7.8 Integration Tests

| ID | Test | Procedure | Pass Criteria |
|---|---|---|---|
| R-INT01 | All inputs simultaneous | `enc monitor` (or `status` in loop). While monitoring: rotate encoder, toggle arm switch, press fire button. | All events detected independently. No interference between inputs. |
| R-INT02 | Display + inputs | `disp grid` (shows channel grid). Then `enc channel` overlaid — rotate encoder to highlight channels on display. | Encoder rotation updates display highlight. Both peripherals work concurrently. |
| R-INT03 | Buzzer + display | `buzz test` while display shows test pattern. | Both buzzer patterns and display render concurrently without artefacts or missed buzzer timing. |

---

## 8. Build and Flash

```bash
cd rlc-hw-test-remote
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The serial monitor serves as both the test CLI and the log output.

---

*End of Remote Unit Hardware Test Specification — RLC-HWTEST-REMOTE-001 v1.0*
