# ESP32 Wireless Rocket Launch Controller — Functional Specification

**Document ID:** RLC-FSPEC-001
**Version:** 1.3
**Date:** 2026-03-22
**Author:** David (System Analyst)
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

---

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
│  │Arm/     │ │ VBAT   │ │ Siren   ││ RGB LED     │            │
│  │Disarm   │ │ ADC    │ │         ││ (status)    │            │
│  │Switch   │ └────────┘ └─────────┘└─────────────┘            │
│  └─────────┘                                                    │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │              LOW-SIDE RELAY (master safety)              │    │
│  └─────────────────────────┬───────────────────────────────┘    │
│                            │                                    │
│  ┌─────────┬─────────┬─────┴───┬─────────────────┐             │
│  │ CH 1    │ CH 2    │ CH 3   │  ...  │ CH 8    │             │
│  │ Relay + │ Relay + │ Relay +│       │ Relay + │             │
│  │ Cont.   │ Cont.   │ Cont.  │       │ Cont.   │             │
│  └─────────┴─────────┴────────┴───────┴─────────┘             │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │           RELAY FEEDBACK INPUT (safety verification)      │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Operational Concept

The launch sequence follows a strict multi-step procedure:

1. The base unit is placed at the launch pad and powered on. It enters IDLE state with all relays disengaged and the low-side relay open (no current can reach any igniter, except for a low-current continuity sensing circuit that bypasses the arm switch).
2. The operator retreats to a safe distance with the remote unit.
3. The remote unit is powered on, discovers the base unit, and establishes a communication link.
4. The operator selects a channel on the remote using the rotary encoder.
5. The operator turns the physical arm key/switch on the remote, then presses the encoder button to confirm. This sends an ARM command for the selected channel.
6. The base unit validates that its own physical arm switch is also in the ARMED position. Only if both arm conditions are met does it close the low-side relay and arm the selected channel relay. The siren begins pulsing (500 ms on / 500 ms off).
7. The operator presses and holds the fire button. The remote sends a FIRE command. The siren switches to continuous.
8. The base unit fires the selected channel for a defined pulse duration, then automatically disarms. The siren is switched off.
9. Disarming any switch, losing communication, selecting a different channel, or any anomaly results in immediate disarming of all channels and opening of the low-side relay.

---

## 3. Definitions and Abbreviations

| Term | Definition |
|---|---|
| **Base** | The launch pad unit that controls igniter hardware |
| **Remote** | The handheld operator unit with display and controls |
| **Channel** | One igniter circuit (relay + continuity sense), numbered 1–8 |
| **Continuity** | Electrical measurement confirming an igniter is connected and has a continuous circuit. The continuity sensing circuit bypasses the arm switch, uses ≤ 1 mA test current, and is always active. |
| **Low-Side Relay** | A master safety relay in the ground return path; when open, no current can flow through any igniter regardless of individual channel relay state |
| **Arm** | The act of enabling a channel for firing (closing the low-side relay and the channel relay) |
| **Fire** | The act of applying current to an igniter to initiate combustion |
| **ESP-NOW** | Espressif connectionless Wi-Fi communication protocol, operates on 2.4 GHz |
| **RSSI** | Received Signal Strength Indicator (dBm) |
| **LMK** | Local Master Key (ESP-NOW encryption) |
| **PMK** | Primary Master Key (ESP-NOW encryption) |
| **VBAT** | Battery voltage |
| **FSM** | Finite State Machine |
| **ADC** | Analogue-to-Digital Converter |
| **BOD** | Brown-Out Detector — hardware voltage monitoring on ESP32-S3 |
| **Relay Feedback** | A dedicated input that senses whether relay outputs are actually de-energised, used to detect stuck relays before arming |
| **Rotary Encoder** | Incremental encoder with quadrature A/B outputs and push-button |
| **Heartbeat** | Periodic ping/pong message pair used to assess link quality |
| **LOS** | Line of Sight |
| **WS2812** | Addressable RGB LED (NeoPixel), driven via RMT peripheral on GPIO47 |

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

The firmware version transmitted during link establishment uses MAJOR, MINOR, and PATCH (3 bytes: one byte each). **Both units must match on all three components** (major, minor, AND patch) to establish a link. If any component differs, the link is rejected. The version string shall be displayed on the remote's splash screen and is incremented with every code change committed.

### 4.3 Development Phases

Development shall proceed in phases. Each phase produces a testable deliverable. The user will test each phase and provide feedback before the next phase begins.

#### Phase 1 — Foundation and Communication

**Goal:** Both units boot, establish a link, and exchange heartbeats. RGB LED shows status.

Deliverables:
- Project scaffolding (CMake, Kconfig, component structure).
- `rlc_common`: ESP-NOW driver wrapper, message serialisation/deserialisation, protocol header and struct definitions, encryption setup, sequence number management, session token generation.
- `rlc_common`: RGB LED driver (WS2812 via RMT on GPIO47), status colour patterns.
- `rlc_common`: Watchdog setup.
- `rlc_common`: Version header.
- Link establishment: LINK_REQUEST / LINK_ACK handshake with firmware version check.
- Heartbeat: PING / PONG at 1-second intervals with RSSI capture.
- Link loss detection (3 missed pings).
- Base: boots, waits for link, responds to pings, RGB LED shows BOOT → IDLE → LINK_LOST.
- Remote: boots, sends link requests, sends pings, tracks RSSI, detects link loss, RGB LED shows BOOT → LINKING → IDLE → LINK_LOST.
- Unit tests for message serialisation, integrity CRC, sequence number validation.

**Test criteria:** Both units power on, link within 10 seconds, display stable RSSI, detect link loss within 3 seconds when separated, recover when returned to range.

#### Phase 2 — Input/Output and Debouncing

**Goal:** All hardware I/O is functional and debounced.

Deliverables:
- `rlc_common`: Shift-register debounce engine (generic, configurable polling rate).
- `rlc_common`: Battery voltage ADC driver (ADC1, 8-sample averaging).
- `rlc_base`: GPIO configuration for all 8 channel relays, 8 continuity inputs, relay feedback input, low-side relay, arm switch, siren. All outputs with configurable polarity.
- `rlc_base`: `relay_channel_set()`, `relay_channel_all_off()`, `relay_lowside_set()`, `relay_all_safe()` functions.
- `rlc_base`: Continuity monitoring task (8 channels, debounced).
- `rlc_base`: Arm switch monitoring (debounced).
- `rlc_base`: Battery monitoring with threshold detection.
- `rlc_base`: STATUS_UPDATE message generation (periodic + event-driven).
- `rlc_remote`: Rotary encoder driver (interrupt-driven, channel 1–8 wrapping).
- `rlc_remote`: Fire button driver (debounced, fresh-press detection).
- `rlc_remote`: Arm switch monitoring (debounced).
- `rlc_remote`: Battery monitoring.
- `rlc_remote`: Buzzer pattern player task.
- Unit tests for debounce engine, battery threshold logic.

**Test criteria:** Base correctly reads continuity on all 8 channels (use jumper wires), arm switch state, and battery voltage. Remote encoder selects channels 1–8, fire button and arm switch debounce correctly. Buzzer plays patterns. Status updates arrive at remote with correct bitmasks.

#### Phase 3 — State Machines and Command Processing

**Goal:** Complete arming and firing sequence works end-to-end.

Deliverables:
- `rlc_base`: Full base state machine (BOOT → IDLE → ARMED → PRE_FIRE → FIRING → POST_FIRE, plus LINK_LOST and ERROR).
- `rlc_base`: Command handler (CMD_ARM, CMD_DISARM, CMD_FIRE, CMD_CEASE_FIRE) with all guard conditions, ACK/NACK responses.
- `rlc_base`: Siren control (pulsing in ARMED, continuous in PRE_FIRE/FIRING).
- `rlc_base`: Fire pulse via hardware timer.
- `rlc_remote`: Full remote state machine (BOOT → LINKING → IDLE → ARMED → FIRING, plus LINK_LOST and ERROR).
- `rlc_remote`: Command sender with ACK/NACK timeout handling and retry logic.
- `rlc_remote`: Repeated CMD_FIRE transmission at 200 ms while fire button held.
- `rlc_remote`: Dead-man switch logic (CMD_FIRE authorization timeout on base).
- All safety interlocks from §9.
- Integration tests: full arm → fire → auto-disarm sequence.

**Test criteria:** Complete fire sequence with LED or resistor load on channel output. All disarm triggers work (switch, command, link loss, continuity loss, battery). NACK reasons displayed correctly. Channel change while armed triggers disarm.

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

Each sub-agent should produce code that is independently compilable and testable where possible.

### 4.5 Testability Requirements

The code shall be designed for testability:

- The `rlc_common` component shall compile on a host machine (ESP-IDF CMake host build or plain GCC) for unit testing without hardware. This requires abstracting hardware calls behind a HAL interface that can be mocked.
- All state machine transitions shall be testable by injecting events programmatically.
- The protocol layer shall be testable by feeding raw byte buffers and verifying parsed output.
- The debounce engine shall be testable by providing a sequence of simulated readings and verifying output.
- Integration tests shall use a two-unit bench setup with LEDs or resistors substituting for igniters, and jumper wires for continuity simulation.

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
| On-board RGB LED | GPIO 47 | WS2812 addressable LED — used for status indication |

**Available GPIOs (24 general-purpose + GPIO47 for RGB LED):**
GPIO 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 48

**ADC constraint:** ESP-NOW uses the Wi-Fi subsystem. On ESP32-S3, ADC2 (GPIO 11–20) is unreliable when Wi-Fi is active. All ADC readings **must** use ADC1 pins (GPIO 1–10).

### 5.2 Configurable GPIO Polarity

For all digital outputs (relays, siren, buzzer), whether the active state is HIGH or LOW shall be configurable as a compile-time constant in `pin_config.h`. Changing polarity requires only adjusting the polarity constants in `pin_config.h` and recompiling — no changes to logic code are needed. Example:

```c
#define PIN_RELAY_CH1          4
#define PIN_RELAY_CH1_ACTIVE   1    // 1 = active HIGH, 0 = active LOW

#define PIN_SIREN              48
#define PIN_SIREN_ACTIVE       1    // 1 = active HIGH, 0 = active LOW
```

The relay and siren driver functions shall use these polarity constants to translate logical state (on/off) to physical GPIO level. This allows the hardware design to use either active-HIGH or active-LOW relay drivers without changes to logic code.

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
| Arm switch (both units) | 16-bit | 10 ms | 160 ms | 0x0000 = armed, 0xFFFF = disarmed |
| Continuity inputs (8×) | 16-bit | 10 ms | 160 ms | 0x0000 = continuity OK, 0xFFFF = no continuity |
| Encoder push button | 16-bit | 10 ms | 160 ms | 0x0000 = pressed, 0xFFFF = released |

The fire button uses an 8-bit register (80 ms debounce) to minimise latency on release detection, which is safety-critical for the dead-man switch function. All other inputs use 16-bit registers (160 ms debounce).

The rotary encoder A/B pins remain interrupt-driven with a 5 ms lockout (shift-register debounce is not suitable for quadrature decoding). The encoder push button uses the shift-register method at 10 ms polling.

The debounce engine shall be implemented as a generic, reusable module in `rlc_common` that accepts a GPIO number, polling interval, register width (8-bit or 16-bit), and callback for state changes.

### 5.4 Base Unit I/O

All GPIO pin numbers and polarities shall be defined in `pin_config.h`.

#### 5.4.1 Igniter Channel Outputs (8×)

| Parameter | Value |
|---|---|
| Signal type | Digital output, configurable polarity (default: active HIGH) |
| Quantity | 8 (channels 1–8) |
| Load | Drives relay coil via N-channel MOSFET or NPN transistor (external circuit) |
| Default state at boot | Inactive (relay disengaged) |
| Drive requirement | 3.3 V logic level, max 20 mA per pin |

Each output, when driven active, closes a relay that connects battery power through the igniter and through the low-side relay to ground. The output must be held active for the configured fire pulse duration, then returned to inactive.

#### 5.4.2 Igniter Continuity Inputs (8×)

| Parameter | Value |
|---|---|
| Signal type | Digital input with internal pull-up |
| Quantity | 8 (one per channel, mapped 1:1 to channel outputs) |
| Logic | LOW = continuity OK (igniter connected), HIGH = no continuity (open circuit) |
| Debounce | 16-bit shift-register, 10 ms polling, 160 ms debounce |

The continuity sensing circuit is external and **bypasses the arm switch**: an isolated low-current source with a current-limiting resistor sized to limit test current to **≤ 1 mA** at maximum battery voltage. This provides a massive safety margin against all commercial igniter types (most e-matches have no-fire thresholds around 50 mA). The continuity circuit produces a logic-level signal via a voltage divider. It operates independently of both the firing circuit and the arm switch, allowing the operator to verify igniter connections at all times.

**Implication:** continuity readings are valid and meaningful in all states, regardless of arm switch position. The display can always show which igniters are connected.

#### 5.4.3 Low-Side Relay Output

| Parameter | Value |
|---|---|
| Signal type | Digital output, configurable polarity (default: active HIGH) |
| Quantity | 1 |
| Function | Closes the master ground return relay; must be active for any igniter to fire |
| Default state at boot | Inactive (relay open — no ground return path) |

This relay is in the ground return shared by all 8 igniter circuits. When inactive (open), no igniter can fire even if a channel relay is inadvertently closed.

#### 5.4.4 Manual Arm/Disarm Switch Input

| Parameter | Value |
|---|---|
| Signal type | Digital input with internal pull-up |
| Quantity | 1 |
| Logic | LOW = ARMED, HIGH = DISARMED (fail-safe: disconnected wire = disarmed) |
| Debounce | Shift-register, 10 ms polling, 160 ms debounce |

This is a physical key switch or toggle on the base unit. It serves dual purpose: it is in the high-side of the igniter current path (hardware safety — physically interrupts firing current), and its position is sensed by a GPIO input (software safety — firmware checks switch state before arming).

Both this switch AND the remote arm switch must be in the armed position for any channel to be armed.

**Note:** Because the continuity sensing circuit bypasses this switch, continuity is always readable regardless of switch position.

#### 5.4.5 Circuit Topology

The following diagram shows the relationship between the arm switch, firing circuit, continuity sensing circuit, and low-side relay:

```
    BATTERY +
        │
        ├──── ARM SWITCH (high-side, physical) ────┐
        │                                           │
        │                                    ┌──────┴──────┐
        │                                    │  CHANNEL     │
        │                                    │  RELAY (1-8) │
        │                                    └──────┬──────┘
        │                                           │
        │                                      IGNITER
        │                                           │
        │                                    ┌──────┴──────┐
        │                                    │  LOW-SIDE    │
        │                                    │  RELAY       │
        │                                    └──────┬──────┘
        │                                           │
    BATTERY − ◄─────────────────────────────────────┘

    CONTINUITY CIRCUIT (isolated, bypasses arm switch):

    3.3V ── R_limit (≤ 1 mA) ──┬── IGNITER ──┬── GND
                                │              │
                           voltage divider → GPIO input
                           (logic level)
```

The continuity circuit has its own isolated low-current supply wired around the arm switch. The current-limiting resistor `R_limit` shall be sized such that at maximum battery voltage, the current through the igniter does not exceed 1 mA. For a 3.3 V source: R_limit ≥ 3.3 kΩ (use 10 kΩ for additional margin).

The firing current path requires BOTH the arm switch (hardware, high-side) AND the low-side relay (software-controlled, ground return) to be closed, in addition to the individual channel relay. This provides three independent break points.

#### 5.4.6 Relay Feedback Input

| Parameter | Value |
|---|---|
| Signal type | Digital input with internal pull-up |
| Quantity | 1 |
| Function | Verifies that relay outputs are actually de-energised before arming |
| Logic | Sensed from the common firing bus (downstream of channel relays, upstream of igniters). LOW = current detected (relay stuck closed), HIGH = no current (relays open, safe) |
| Debounce | 16-bit shift-register, 10 ms polling, 160 ms debounce |

This input detects whether any channel relay is stuck closed (welded contacts, shorted MOSFET). The base SHALL check this input before arming any channel. If the feedback indicates current on the firing bus when all relays should be open, arming is refused and `ERR_RELAY_FAULT` is set.

The external sensing circuit should detect current flow on the common bus shared by all channel relays (e.g., a low-value sense resistor with a comparator, or an optocoupler). The exact circuit design is a hardware concern; the firmware requires only a digital logic-level input.

#### 5.4.7 Battery Voltage Input

| Parameter | Value |
|---|---|
| Signal type | Analogue input (ADC1) |
| Quantity | 1 |
| Pin | Must be GPIO 1–10 (ADC1 only — ADC2 is unreliable with ESP-NOW active) |
| Input range | 0–3.3 V (via external voltage divider from battery) |
| ADC resolution | 12-bit |
| Sampling interval | 1000 ms |
| Averaging | 8-sample moving average to reduce noise |
| Conversion | The firmware SHALL use the ESP-IDF v5.x ADC calibration API (`adc_cali_raw_to_voltage()`) for voltage conversion. This uses per-chip calibration data burned into eFuse at the factory. The calibrated millivolt reading is then multiplied by `DIVIDER_RATIO` to obtain the battery voltage. |

The DIVIDER_RATIO constant must be defined in configuration to match the external resistor divider.

#### 5.4.8 Siren Output

| Parameter | Value |
|---|---|
| Signal type | Digital output, configurable polarity (default: active HIGH) |
| Quantity | 1 |
| Function | Loud alarm: pulsing during ARMED, continuous during PRE_FIRE and FIRING, pulsed during LINK_LOST |
| Drive | Drives external siren/horn via MOSFET or transistor |

#### 5.4.9 RGB LED (Status Indicator)

| Parameter | Value |
|---|---|
| Type | WS2812 (NeoPixel) addressable RGB LED, on-board |
| Pin | GPIO 47 (fixed, on-board) |
| Driver | ESP32-S3 RMT peripheral |
| Function | Visual status indication (see §11) |

### 5.5 Remote Unit I/O

#### 5.5.1 Rotary Encoder (Channel Selector)

| Parameter | Value |
|---|---|
| Signal type | 2× digital input (A/B quadrature) + 1× digital input (push button) |
| Quantity | 1 encoder |
| Inputs | CLK (A), DT (B), SW (push button) — all with internal pull-ups |
| A/B debounce | Interrupt-driven with 5 ms lockout (not shift-register) |
| Push button debounce | Shift-register, 10 ms polling, 160 ms debounce |
| Rotation function | Select active channel (1–8), wrapping around |
| Push button function | Context-dependent: ARM confirm (in IDLE with arm switch ON), DISARM (in ARMED) |

The direction of rotation determines increment (+1) or decrement (−1) of the selected channel. Channel selection wraps: incrementing past 8 returns to 1; decrementing past 1 returns to 8.

**Behaviour while ARMED:** rotating the encoder (channel change) triggers an immediate CMD_DISARM, return to IDLE, and the newly selected channel becomes the cursor position. The operator must re-arm deliberately.

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

#### 5.5.4 Battery Voltage Input

| Parameter | Value |
|---|---|
| Signal type | Analogue input (ADC1) |
| Quantity | 1 |
| Pin | Must be GPIO 1–10 (ADC1 only) |
| Conversion | Same as Base Unit §5.4.7 — use `adc_cali_raw_to_voltage()` calibration API. DIVIDER_RATIO may differ. |

#### 5.5.5 ILI9488 LCD Display

| Parameter | Value |
|---|---|
| Controller IC | ILI9488 |
| Resolution | 480 × 320 pixels |
| Interface | 4-wire SPI |
| Colour depth | 18-bit (262k colours); driver shall use RGB666 or RGB565 |
| Touch | Not used in this design (pins 10–14 of module left unconnected) |
| Pin count | 14-pin module (only pins 1–9 connected) |
| SPI clock | Start at 20 MHz write / 6.67 MHz read; increase if stable |
| Backlight | Controlled via GPIO (PWM or digital on/off) |
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
| 8 | LED | Backlight enable (active high) | ESP32-S3 GPIO (PWM capable) |
| 9 | SDO (MISO) | SPI data out | ESP32-S3 GPIO |
| 10–14 | Touch | Not connected | N/C |

SPI bus shall use SPI2_HOST (HSPI) on the ESP32-S3.

#### 5.5.6 Buzzer Output

| Parameter | Value |
|---|---|
| Signal type | Digital output, configurable polarity (default: active HIGH) |
| Quantity | 1 |
| Function | Audible feedback (beeps for state changes, ping failures, warnings, alarms) |
| Drive | Active buzzer driven through MOSFET |

#### 5.5.7 RGB LED (Status Indicator)

Same as Base Unit §5.4.7. On-board WS2812 on GPIO 47.

---

## 6. Communication Protocol Specification

### 6.1 Physical Layer

| Parameter | Value |
|---|---|
| Protocol | ESP-NOW over Wi-Fi (2.4 GHz) |
| Data rate | 1 Mbps (ESP-NOW default) |
| Max payload | 250 bytes per ESP-NOW frame |
| Range | ~200 m LOS (subject to antenna and environment) |
| Channel | Fixed Wi-Fi channel (configurable, default: **channel 11** — avoids the heavily congested channel 1 at launch events) |

### 6.2 Security

#### 6.2.1 ESP-NOW Encryption (Security Boundary)

ESP-NOW provides built-in **AES-128-CCM** encryption per peer. This is the system's primary security boundary against external adversaries. The implementation shall:

1. Define a shared 16-byte Primary Master Key (PMK) at compile time (stored in `protocol_config.h`).
2. Derive or define a 16-byte Local Master Key (LMK) per peer.
3. Register each peer with encryption enabled (`esp_now_peer_info_t.encrypt = true`).
4. The PMK and LMK shall be identical on both units (symmetric).

#### 6.2.2 Application-Layer Integrity and Replay Protection

In addition to ESP-NOW encryption, the application protocol shall implement:

1. **Sequence numbers** — every message includes a monotonically increasing 32-bit sequence number. The receiver shall reject any message with a sequence number equal to or less than the last accepted sequence number from that sender (replay protection). **Upon session establishment (LINK_ACK accepted), both units SHALL reset their per-peer sequence counters to 0.** Sequence numbers are per-sender.
2. **Session token** — at link establishment, the base generates a random 32-bit session token and sends it to the remote in the `LINK_ACK` message. All subsequent messages must include this session token. Messages with an invalid token are silently discarded. **Upon receiving a LINK_REQUEST, the base SHALL atomically invalidate the previous session token before generating a new one.** This prevents delayed packets from a previous session being accepted during the handover window.
3. **Command integrity check** — ARM, DISARM, FIRE, and CEASE_FIRE commands include a 32-bit CRC32 computed over the message payload (excluding the CRC field itself) appended with a pre-shared 16-byte key. The base shall verify this CRC before executing any command. **Note:** CRC32 is an integrity check, not a cryptographic authentication function. It protects against software bugs and accidental corruption. The actual security boundary is ESP-NOW's AES-128-CCM encryption (§6.2.1).

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

**Firmware version enforcement:** the remote shall compare all three components (major, minor, patch) of the received `base_firmware_version` with its own version. If any component differs, the link shall be rejected. The remote shall display "FIRMWARE MISMATCH — Base vX.Y.Z / Remote vX.Y.Z — Reflash required" and shall NOT transition to IDLE. The remote stays in LINKING state but stops retrying, displaying the mismatch error until power cycle.

**Immediately after sending LINK_ACK**, the base SHALL send a full STATUS_UPDATE message to provide the remote with initial system state.

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
| 0 | 1 | `channel` | Channel number to arm (1–8) |
| 1 | 4 | `integrity_crc` | CRC32 integrity check (see §6.2.2) |

##### CMD_DISARM (0x21)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 1 | `channel` | Channel number to disarm (1–8), or `0xFF` for all channels |
| 1 | 4 | `integrity_crc` | CRC32 integrity check |

##### CMD_FIRE (0x22)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 1 | `channel` | Channel number to fire (1–8) |
| 1 | 4 | `integrity_crc` | CRC32 integrity check |

##### CMD_CEASE_FIRE (0x23)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 | `integrity_crc` | CRC32 integrity check |

##### STATUS_UPDATE (0x30)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 2 | `continuity_bitmask` | Bits 0–7: continuity per channel (1 = OK, 0 = open). Bits 8–15: reserved. |
| 2 | 2 | `channel_armed_bitmask` | Bits 0–7: armed state per channel (1 = armed). Bits 8–15: reserved. |
| 4 | 2 | `channel_firing_bitmask` | Bits 0–7: currently firing per channel (1 = firing). Bits 8–15: reserved. |
| 6 | 1 | `base_arm_switch` | 0 = disarmed, 1 = armed |
| 7 | 1 | `low_side_relay` | 0 = open (safe), 1 = closed |
| 8 | 2 | `battery_voltage_mv` | Base battery voltage in millivolts (uint16) |
| 10 | 1 | `base_state` | Current base FSM state enum |
| 11 | 1 | `error_flags` | Bit field of active errors (see §13) |
| 12 | 2 | `update_sequence` | Monotonically increasing per status update (uint16). Remote can detect gaps. |

##### CMD_ACK (0x31) — 6 bytes

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 1 | `acked_msg_type` | Message type being acknowledged |
| 1 | 4 | `acked_sequence_number` | Sequence number of the acknowledged message |
| 5 | 1 | `channel` | Channel the command applied to (1–8), or 0x00 for commands without a channel (CMD_CEASE_FIRE) |

The remote SHALL verify that the `channel` in CMD_ACK matches the channel it requested before acting on the acknowledgement.

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
| `0x04` | Channel has no continuity | "NO CONTINUITY ON CH N" |
| `0x05` | System not in correct state for this command | "WRONG STATE" |
| `0x06` | Integrity CRC mismatch | "INTEGRITY ERROR" |
| `0x07` | Invalid session token | "SESSION ERROR" |
| `0x08` | Sequence number replay detected | "REPLAY DETECTED" |
| `0x09` | Low battery — command refused | "LOW BATTERY" |
| `0x0A` | Another channel already armed | "CH N ALREADY ARMED" |
| `0x0B` | Relay feedback fault — relay stuck | "RELAY FAULT" |

The remote shall display the human-readable text on screen for at least 3 seconds when a NACK is received.

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

The remote shall retry `LINK_REQUEST` every 2000 ms. If no `LINK_ACK` is received after 15 attempts (30 seconds), the remote shall display "NO LINK" and continue retrying indefinitely at a 5-second interval.

If the base receives a `LINK_REQUEST` while already linked to the same remote MAC (e.g., after a remote reboot), it shall **atomically invalidate the previous session token**, reset both per-peer sequence counters to 0, generate a new session token, and respond normally.

The base shall only accept link requests from the pre-configured remote MAC address. Requests from any other MAC shall be silently ignored.

#### 6.4.2 Heartbeat Protocol

Once linked, the remote sends a `PING` message every 1000 ms. The base responds with `PONG`.

| Parameter | Value |
|---|---|
| Ping interval | 1000 ms |
| Pong timeout | 500 ms (if no PONG received within 500 ms of PING send, that ping is a failure) |
| Link quality window | Last 10 pings |
| Link loss threshold | 3 consecutive failed pings |
| RSSI source | Captured from the ESP-NOW receive callback on each received frame |

**RSSI tracking:** the remote shall record the RSSI from each received frame (PONG, STATUS_UPDATE, ACK, NACK). The display shall show the average RSSI of the 3 most recently received frames.

**Missed ping action (remote):** on each individual ping failure, the remote buzzer shall emit a single short beep (80 ms) and the RGB LED shall flash orange (50 ms) overlaid on the current state colour.

**PONG validation:** the remote shall verify that the `ping_timestamp` echoed in the PONG matches the timestamp sent in the corresponding PING. A PONG with a mismatched timestamp is discarded silently and does NOT count as a successful ping. The failure counter continues.

**Link loss action:** if 3 consecutive pings fail:
- Remote: display "LINK LOST" warning, buzzer alarm pattern (200 ms on / 200 ms off, repeating), RGB LED yellow fast blink, transition to LINK_LOST state.
- Base: immediately disarm all channels, open low-side relay, activate siren for 4000 ms (500 on / 500 off, 4 cycles), RGB LED yellow fast blink, transition to LINK_LOST state.

**Remote battery at base:** the base receives the remote's battery voltage via the PING message. If the remote battery is below `REMOTE_VBAT_MIN_OPERATE_MV`, the base should log an advisory warning.

**Link recovery:** when a valid PONG is received after link loss, both units transition to IDLE (not armed — arming must be re-initiated by the operator).

#### 6.4.3 Status Update Transmission

The base shall send a `STATUS_UPDATE` message:
- **Event-driven:** immediately upon any debounced change in continuity, arm switch state, low-side relay state, channel armed state, or channel firing state.
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
                                   │ Disarm all, open
                                   │ low-side relay
                                   ▼
┌──────────┐    Link established   ┌──────────┐
│  BOOT    ├──────────────────────►│  IDLE    │◄──── (always returns here)
└──────────┘                       └────┬─────┘
                                        │
                              CMD_ARM received,
                              both arm switches ON,
                              continuity OK on channel
                                        │
                                        ▼
                                  ┌──────────┐
                                  │  ARMED   │  (siren pulses 500/500)
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
                                  │          │   open all relays)
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
| `BOOT` | Initialisation. All outputs inactive. Waiting for link. | Blue slow pulse | Off |
| `IDLE` | Linked. All relays disengaged. Low-side relay open. | Green solid (arm switch OFF) or green fast blink (arm switch ON) | Off |
| `ARMED` | One channel armed. Low-side relay closed. | Red slow blink (500/500) | Pulsing (500 on / 500 off) |
| `PRE_FIRE` | FIRE accepted. Countdown running. Dead-man active. | Red fast blink (100/100) | Continuous |
| `FIRING` | Igniter relay active. Fire pulse timer running. | Red solid | Continuous |
| `POST_FIRE` | Fire complete. All relays off. Cooldown. | Yellow solid | Off |
| `LINK_LOST` | Comms lost. All relays off. | Yellow fast blink (200/200) | 500 on / 500 off, 4 cycles on entry |
| `ERROR` | Unrecoverable error. All relays off. Requires power cycle. | Red triple flash | Off |

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
  1. Base arm switch is in ARMED position (debounced, stable).
  2. Channel N has continuity (debounced, stable).
  3. Channel N is in range (1–8).
  4. No other channel is currently armed (single-channel arming only).
  5. Message integrity CRC is valid.
  6. Session token is valid.
  7. Sequence number is valid (not a replay).
  8. Base battery voltage is above `VBAT_MIN_ARM_MV` threshold.
  9. **Relay feedback check passes** — the relay feedback input confirms no current on the firing bus (all relays verified open).
- Actions on successful transition:
  1. Close the low-side relay (set active).
  2. Record armed channel number.
  3. Start siren pulsing (500 ms on / 500 ms off).
  4. Send `CMD_ACK` (with channel field) to remote.
  5. Send `STATUS_UPDATE` with updated bitmasks.
  6. RGB LED → red slow blink.
- If any guard fails: send `CMD_NACK` with appropriate reason code (including 0x0B for relay fault). Remain in IDLE.
- Exceptions:
  - CMD_ARM for wrong channel (e.g., 0 or 9+) → NACK reason 0x03.
  - CMD_ARM while another channel armed → NACK reason 0x0A.
  - CMD_FIRE received while in IDLE → NACK reason 0x05.
  - CMD_CEASE_FIRE received while in IDLE → ACK (idempotent, already safe).
  - Multiple rapid CMD_ARM for different channels → first is processed, subsequent NACK'd.

#### 7.2.3 ARMED → PRE_FIRE

- Trigger: `CMD_FIRE` received for the armed channel.
- Guard conditions:
  1. Channel in the FIRE command matches the currently armed channel.
  2. Message integrity CRC is valid.
  3. Continuity is still present on the armed channel.
- Actions on transition:
  1. Switch siren from pulsing to continuous.
  2. Start pre-fire countdown timer (`PRE_FIRE_DELAY_MS`, default: 5000 ms).
  3. Send `CMD_ACK` (with channel field) to remote.
  4. Send `STATUS_UPDATE`.
  5. RGB LED → red fast blink.
- Exceptions:
  - CMD_FIRE for wrong channel → NACK reason 0x05. Remain ARMED.
  - CMD_ARM for a different channel while armed → NACK reason 0x0A. Remain ARMED.
  - Continuity lost on armed channel → immediate disarm (§7.2.7).
  - Base arm switch → DISARM → immediate disarm (§7.2.7).
  - Continuity lost on a NON-armed channel → update bitmask, send STATUS_UPDATE, remain ARMED (not a safety issue).

#### 7.2.4 PRE_FIRE → FIRING

- Trigger: Pre-fire countdown timer elapsed.
- Guard (ALL must be true):
  1. The base must have received at least one `CMD_FIRE` message within the last `FIRE_AUTHORIZATION_TIMEOUT_MS` (500 ms).
  2. **Link health: the last PONG was received within `HEARTBEAT_TIMEOUT_MS` (500 ms).** This prevents energising the igniter at the exact moment the link dies.
- Actions on transition:
  1. Drive the armed channel's igniter relay output active.
  2. Start fire pulse timer (`FIRE_PULSE_DURATION_MS`, default: 2000 ms). **The channel number SHALL be passed to the timer callback as a context argument**, not read from a global variable inside the ISR. The callback SHALL assert that the passed channel matches the currently armed channel before deactivating the relay.
  3. Keep siren continuous.
  4. Send `STATUS_UPDATE` with firing bitmask set.
  5. RGB LED → red solid.
- Exceptions:
  - **Pre-fire timer expires but no CMD_FIRE received within last 500 ms (dead-man timeout):** abort. `relay_all_safe()`, siren off, return to IDLE. Send STATUS_UPDATE.
  - **Pre-fire timer expires but link health check fails:** abort. `relay_all_safe()`, siren off, transition to LINK_LOST.
  - CMD_CEASE_FIRE during PRE_FIRE → immediate abort. `relay_all_safe()`, siren off, return to IDLE. ACK the command.
  - Base arm switch → DISARM during PRE_FIRE → immediate abort. `relay_all_safe()`, siren off, return to IDLE.
  - Continuity lost during PRE_FIRE → immediate abort. `relay_all_safe()`, siren off, return to IDLE. Set ERR_CONTINUITY_LOST_WHILE_ARMED.
  - Link lost during PRE_FIRE → immediate abort (igniter not yet energised, safe to abort). `relay_all_safe()`, siren for 4000 ms, LINK_LOST.
  - Battery drops critical during PRE_FIRE → immediate abort → ERROR.

#### 7.2.5 FIRING → POST_FIRE

- Trigger: Fire pulse timer elapsed.
- Actions on transition:
  1. Drive the channel's igniter relay output inactive.
  2. Open low-side relay (set inactive).
  3. Deactivate siren.
  4. Clear armed channel.
  5. Start post-fire cooldown timer (`POST_FIRE_COOLDOWN_MS`, default: 2000 ms).
  6. Send `STATUS_UPDATE` (all bitmasks cleared).
  7. RGB LED → yellow solid.
- Exceptions:
  - CMD_CEASE_FIRE during FIRING → `relay_all_safe()` immediately. Siren off. Return to IDLE. ACK the command. The igniter has received partial energy but the operator explicitly asked to stop.
  - Base arm switch → DISARM during FIRING → same as CEASE_FIRE. Immediate cutoff.
  - **Link lost during FIRING → SPECIAL CASE.** The igniter is actively energised. Cutting it mid-pulse could leave a partially initiated igniter in an unstable state. **The base shall complete the fire pulse**, then transition to POST_FIRE, then to LINK_LOST with full disarm. Remaining pulse time is at most FIRE_PULSE_DURATION_MS.
  - Continuity lost during FIRING → **EXPECTED.** The igniter is burning/consumed. Do NOT treat as error during FIRING state. Ignore continuity changes on the armed channel while in FIRING.
  - **Battery drops critical during FIRING** → complete the fire pulse (same reasoning as link loss), then → ERROR.
  - CMD_ARM received during FIRING → NACK reason 0x05.

#### 7.2.6 POST_FIRE → IDLE

- Trigger: Cooldown timer elapsed.
- Actions: state change only. RGB LED → green solid.
- Exceptions:
  - CMD_ARM received during cooldown → NACK reason 0x05. Must wait for IDLE.
  - Link lost during POST_FIRE → relays are already safe. Transition to LINK_LOST immediately (safe either way).

#### 7.2.7 Any State → IDLE (Disarm)

This transition can be triggered from ARMED, PRE_FIRE, or FIRING (with caveats for FIRING per §7.2.5) by any of:

- `CMD_DISARM` or `CMD_CEASE_FIRE` received.
- Base arm switch moved to DISARM position.
- Repeated `CMD_FIRE` not received for 500 ms during PRE_FIRE (dead-man timeout).
- Base battery voltage drops below `VBAT_CRITICAL_MV`.
- Continuity lost on the armed channel (except during FIRING).

Actions:
1. All channel relay outputs → inactive.
2. Low-side relay output → inactive.
3. Deactivate siren.
4. Clear armed channel.
5. Send `CMD_ACK` (if triggered by command) or `STATUS_UPDATE`.
6. RGB LED → green solid.

#### 7.2.8 Any State → LINK_LOST

- Trigger: 3 consecutive heartbeat failures (no PING received for 3 seconds).
- Actions:
  1. Execute full disarm (§7.2.7 actions 1–4).
  2. Activate siren for 4000 ms (500 on / 500 off, 4 cycles).
  3. Send STATUS_UPDATE (if possible — link may be partially functional).
  4. RGB LED → yellow fast blink.
- Recovery: when a valid PING is received, respond with PONG, transition to IDLE (not ARMED).
- Exceptions:
  - LINK_LOST persists for extended time → base stays in LINK_LOST. System is safe (all relays off). No automatic shutdown. Operator must physically intervene.
  - Arm switch toggled while in LINK_LOST → no effect on relays (already off). State is updated for when link recovers.

#### 7.2.9 Any State → ERROR

- Trigger: `VBAT_CRITICAL_MV` exceeded (except during FIRING — see §7.2.5), assertion failure, or unrecoverable internal error.
- Actions:
  1. `relay_all_safe()`.
  2. Set `ERR_INTERNAL` or `ERR_VBAT_CRITICAL` flag.
  3. Send one final `STATUS_UPDATE` if possible.
  4. RGB LED → red triple flash pattern.
  5. System halted. Requires power cycle.

### 7.3 Input Processing

#### 7.3.1 Continuity Monitoring

A dedicated FreeRTOS task (`continuity_task`) shall poll all 8 continuity inputs using the shift-register debounce engine at 10 ms intervals. When a debounced change is detected:
1. Update the internal `continuity_bitmask`.
2. If a channel that is currently armed loses continuity (and state is not FIRING): immediately trigger disarm (§7.2.7).
3. Trigger an event-driven `STATUS_UPDATE` to the remote.

Since the continuity circuit bypasses the arm switch, continuity readings are always valid and meaningful regardless of arm switch position.

#### 7.3.2 Arm Switch Monitoring

A dedicated FreeRTOS task or timer callback (`arm_switch_task`) shall poll the arm switch input using the shift-register debounce engine at 10 ms intervals. On debounced change:
1. If switch moved to DISARMED (0xFFFF) and base is in ARMED/PRE_FIRE/FIRING: execute immediate disarm.
2. Trigger `STATUS_UPDATE`.

#### 7.3.3 Battery Monitoring

Sampled every 1000 ms with 8-sample moving average. Two thresholds:
- `VBAT_MIN_ARM_MV`: minimum voltage to allow arming. Below this, ARM commands are NACK'd with reason 0x09.
- `VBAT_CRITICAL_MV`: critical low voltage. Below this, immediate disarm and transition to ERROR state (unless in FIRING — complete pulse first).

### 7.4 Output Control

#### 7.4.1 Relay Drive Functions

Relays shall only be driven using the following encapsulated functions, which apply the configured polarity:

```c
void relay_channel_set(uint8_t channel, bool state);   // Set one channel relay
void relay_channel_all_off(void);                       // All channel relays inactive
void relay_lowside_set(bool state);                     // Low-side relay
void relay_all_safe(void);                              // All relays inactive + low-side inactive
```

`relay_all_safe()` shall be called:
- At boot (before any other operation).
- On any disarm event.
- On any error.
- On entry to LINK_LOST.

#### 7.4.2 Firing Sequence Detail

The exact sequence within the FIRING state:

1. Assert: low-side relay is active (closed).
2. Assert: exactly one channel relay is about to be driven.
3. Drive channel relay active.
4. Start **hardware timer** for `FIRE_PULSE_DURATION_MS`. **The channel number SHALL be passed to the timer callback as a context argument**, not read from a global variable inside the ISR. The callback SHALL:
   - Assert that the passed channel matches the currently armed channel.
   - Drive channel relay inactive.
   - Set low-side relay inactive.
   - Signal the state machine to transition to POST_FIRE.
5. Transition to POST_FIRE on callback completion.

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
| `BOOT` | Initialisation. Display splash screen. | Blue slow pulse |
| `LINKING` | Sending `LINK_REQUEST`, waiting for `LINK_ACK`. Display "Connecting..." with retry counter. | Blue slow pulse |
| `IDLE` | Linked. Display status overview. Operator can select channels. | Green solid |
| `ARMED` | ARM command ACK'd. Fire button active. | Red slow blink (500/500) |
| `FIRING` | Fire button held. CMD_FIRE being sent at 200 ms intervals. | Red solid |
| `LINK_LOST` | Heartbeat lost. Buzzer alarm. All commands disabled. | Yellow fast blink (200/200) |
| `ERROR` | Unrecoverable error. Requires power cycle. | Red triple flash |

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
  - Unexpected `num_channels` in LINK_ACK → store and adapt. Display only the reported number of channels.

#### 8.2.3 IDLE → ARMED

- Trigger: operator action sequence:
  1. Arm switch is in ARMED position (local check, debounced, stable).
  2. Operator has selected a channel via the rotary encoder.
  3. Operator presses encoder button to confirm arming.
- Local guard conditions (ALL must be true before sending CMD_ARM):
  1. Local arm switch is ARMED.
  2. **Remote battery voltage is above `REMOTE_VBAT_MIN_ARM_MV`.** If below, display "REMOTE BATTERY LOW — CANNOT ARM". Do not send command.
  3. **Most recent STATUS_UPDATE was received within 2× STATUS_UPDATE_INTERVAL_MS (default: 4000 ms).** If stale, display "DATA STALE — CANNOT ARM". Do not send command.
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
  1. Fire button released → send `CMD_CEASE_FIRE`, transition to IDLE.
  2. `STATUS_UPDATE` showing fire complete (base in POST_FIRE or IDLE) → transition to IDLE, display "FIRE COMPLETE".
  3. Arm switch moved to DISARM → send `CMD_CEASE_FIRE`, transition to IDLE.
  4. Link lost → transition to LINK_LOST.
- Actions: update display, clear firing indicators, stop sending repeated CMD_FIRE.
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

#### 8.2.7 Any State → LINK_LOST

- Trigger: 3 consecutive ping failures.
- Actions:
  1. If in ARMED or FIRING: mark as disarmed locally (base will also disarm).
  2. Display "LINK LOST" prominently.
  3. Buzzer: continuous alarm pattern (200 ms on / 200 ms off).
  4. Continue sending PINGs at 1000 ms.
  5. RGB LED → yellow fast blink.
- Recovery: on first successful PONG, transition to IDLE (never directly to ARMED).

### 8.3 Input Processing

#### 8.3.1 Rotary Encoder

- A/B pins: read via interrupts with 5 ms lockout.
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
- On release (0x0000 → 0xFFFF): `CMD_CEASE_FIRE` sent immediately.

#### 8.3.3 Arm Switch

- Shift-register debounce, 10 ms polling.
- Moving to DISARMED (0xFFFF) at any time triggers immediate disarm sequence.
- Moving to ARMED (0x0000) does NOT automatically arm — it is a precondition only.

#### 8.3.4 Battery Monitoring

Same calibrated ADC approach as base (§7.3.3). Three thresholds:
- `REMOTE_VBAT_MIN_ARM_MV`: minimum voltage to allow arming. Below this, the remote SHALL NOT send CMD_ARM. Display "REMOTE BATTERY LOW — CANNOT ARM".
- `REMOTE_VBAT_MIN_OPERATE_MV`: minimum voltage for normal operation. Below this: display warning, continue operating.
- `REMOTE_VBAT_CRITICAL_MV`: critical low. Display warning, refuse to arm, buzzer alarm, RGB LED error.

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
| Power-on (either unit) | All relays inactive, low-side relay open, no channel armed |
| Communication lost | Immediate disarm all, low-side relay open, siren (base), buzzer alarm (remote) |
| Base arm switch → DISARM | Immediate disarm all, low-side relay open |
| Remote arm switch → DISARM | Send DISARM ALL to base |
| Fire button released | CMD_CEASE_FIRE sent, base stops firing at next 500 ms authorization check |
| Battery critical (either unit) | Refuse to arm; if armed, disarm (complete fire pulse first if FIRING) |
| Software crash / watchdog reset | ESP32-S3 GPIO default state ensures relays inactive (hardware fail-safe) |
| Unknown / corrupt message received | Silently discard (never act on unvalidated data) |
| Channel change while armed (encoder rotation) | Immediate disarm, return to IDLE |
| Relay feedback fault detected | Refuse to arm (NACK 0x0B); if detected in IDLE, transition to ERROR |

### 9.2 Dual-Key Arming

No channel can be armed unless ALL of the following are simultaneously true:
1. Base physical arm switch is in ARMED position.
2. Remote physical arm switch is in ARMED position.
3. Operator has explicitly pressed the encoder button to send an ARM command (deliberate action).
4. Communication link is healthy.
5. Selected channel has igniter continuity.
6. Base battery voltage is above `BASE_VBAT_MIN_ARM_MV`.
7. Remote battery voltage is above `REMOTE_VBAT_MIN_ARM_MV`.
8. Relay feedback confirms no stuck relays.
9. STATUS_UPDATE data is fresh (received within 2× STATUS_UPDATE_INTERVAL_MS).

### 9.3 Single-Channel Arming

Only one channel may be armed at a time. An ARM command for a new channel while another is armed shall be NACK'd with reason 0x0A. The operator must disarm the current channel first. Rotating the encoder (changing channel selection) while armed triggers automatic disarm.

### 9.4 Fire Button Dead-Man Switch

The fire button acts as a dead-man switch: the operator must continuously hold it throughout the entire pre-fire delay and fire pulse. Releasing the button at any point aborts the sequence (except during FIRING where link loss allows pulse completion — see §7.2.5).

### 9.5 Auto-Disarm After Fire

After a fire pulse completes, the base automatically disarms the channel and opens the low-side relay. The operator must go through the full arm sequence again to fire another channel.

### 9.6 Watchdog Timer

Both units shall enable the ESP32-S3 hardware watchdog timer with a 2-second timeout. The main loop and all critical tasks must feed the watchdog. A watchdog reset results in a clean boot with all outputs in safe state.

### 9.7 GPIO Initialisation Order

At boot, before any other peripheral is initialised, the firmware shall:
1. Configure the low-side relay GPIO as output, driven to inactive state.
2. Configure all channel relay output GPIOs as outputs, driven to inactive state.
3. Only then proceed to initialise ESP-NOW, display, and other peripherals.

This ensures that even if initialisation of a later peripheral crashes, the relay outputs are already in the safe state.

### 9.8 Brown-Out Detection

The ESP32-S3 hardware brown-out detector (BOD) SHALL be configured with a threshold above the minimum operating voltage of the relay driver circuitry. The expected inrush current from relay coil activation SHALL be documented in the hardware design. The battery SHALL be sized to supply this current without dipping below the BOD threshold. If a brown-out reset occurs, the system boots to safe state per §9.7.

---

## 10. Display Specification

### 10.1 Display Hardware

See §5.5.5 for hardware details.

### 10.2 Screen Layouts

#### 10.2.0 Display Colour Constants

All display colours shall use the following RGB565 values (adjustable during implementation):

| Name | RGB888 | RGB565 | Usage |
|---|---|---|---|
| Green (continuity OK) | (0, 200, 0) | 0x0640 | Continuity filled circle |
| Red (no continuity / error) | (255, 0, 0) | 0xF800 | No continuity, error text |
| Cyan (selected) | (0, 220, 255) | 0x06DF | Selected channel highlight |
| Red background (armed) | (180, 0, 0) | 0xB000 | Armed channel background |
| Yellow (warning) | (255, 220, 0) | 0xFEE0 | Warning text |
| White (default text) | (255, 255, 255) | 0xFFFF | Normal text |
| Dark background | (0, 0, 0) | 0x0000 | Screen background |

The display shall support the following screens, determined by the remote FSM state.

#### 10.2.1 Splash Screen (BOOT / LINKING)

```
┌──────────────────────────────────────────────────┐
│                                                  │
│         ESP32 WIRELESS ROCKET LAUNCH             │
│              CONTROLLER  v1.0.0                  │
│                                                  │
│              Connecting to base...               │
│              Attempt 3 / 15                      │
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
│          Base:   v1.1                            │
│          Remote: v1.0                            │
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
│   1 ● ── 2 ● ── 3 ● ── 4 ○                     │
│                                                  │
│   5 ○ ── 6 ○ ── 7 ● ── 8 ○                     │
│                                                  │
│          ● = continuity OK    ○ = open           │
│                                                  │
│  ►[ CH 3 ]◄     Base switch: SAFE               │
│                  Remote switch: SAFE             │
│                  Low-side relay: OPEN            │
│                                                  │
│         Turn ARM key to arm channel 3            │
└──────────────────────────────────────────────────┘
```

Key elements:
- **Top bar:** RSSI with graphical bar (averaged over last 3 frames), ping round-trip time, remote battery voltage with bar, base battery voltage with bar, link status indicator.
- **Channel grid:** all 8 channels displayed with continuity indicators. Green filled circle (●) = continuity OK. Red empty circle (○) = no continuity. The selected channel is highlighted with `►[ CH N ]◄` cursor.
- **Status area:** base arm switch state, remote arm switch state, low-side relay state.
- **Instruction text:** context-sensitive prompt guiding the operator.

Colour coding:
- Continuity OK: green.
- No continuity: red.
- Selected channel: cyan highlight.
- Armed channel: red background.
- Warning text: yellow.
- Error text: red.

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
│            ║   Continuity: OK    ║               │
│            ║                     ║               │
│            ║   PRESS AND HOLD    ║               │
│            ║   FIRE TO LAUNCH    ║               │
│            ╚══════════════════════╝               │
│                                                  │
│     Base switch: ARMED   Remote switch: ARMED    │
│     Low-side relay: CLOSED                       │
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

#### 10.2.5 Link Lost Screen

```
┌──────────────────────────────────────────────────┐
│                                                  │
│          ⚠  LINK LOST  ⚠                         │
│                                                  │
│     No response from base unit                   │
│     Last contact: 4 seconds ago                  │
│                                                  │
│     All channels disarmed (assumed)              │
│                                                  │
│     Attempting to reconnect...                   │
│     Ping attempts: 7                             │
│                                                  │
└──────────────────────────────────────────────────┘
```

Yellow/amber background.

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

Both units have an on-board WS2812 (NeoPixel) addressable RGB LED on GPIO 47, driven via the ESP32-S3 RMT peripheral.

### 11.1 Base Unit LED Patterns

| State | Colour | Pattern | Description |
|---|---|---|---|
| BOOT | Blue (0,0,255) | Slow pulse (2s cycle, fade in/out) | Initialising |
| IDLE (arm switch OFF) | Green (0,255,0) | Solid | Safe, linked, ready |
| IDLE (arm switch ON) | Green (0,255,0) | Fast blink (250ms on/250ms off) | Arm switch active, waiting for remote ARM |
| ARMED | Red (255,0,0) | Slow blink (500ms on/500ms off) | Channel armed, danger |
| PRE_FIRE | Red (255,0,0) | Fast blink (100ms on/100ms off) | Imminent ignition |
| FIRING | Red (255,0,0) | Solid | Active ignition |
| POST_FIRE | Yellow (255,180,0) | Solid | Cooldown |
| LINK_LOST | Yellow (255,180,0) | Fast blink (200ms on/200ms off) | No communication |
| ERROR | Red (255,0,0) | Triple flash (100on/100off/100on/100off/100on/700off) | Fault, power cycle required |

### 11.2 Remote Unit LED Patterns

| State | Colour | Pattern | Description |
|---|---|---|---|
| BOOT / LINKING | Blue (0,0,255) | Slow pulse (2s cycle) | Searching for base |
| IDLE | Green (0,255,0) | Solid | Linked, safe |
| ARMED | Red (255,0,0) | Slow blink (500ms on/500ms off) | Channel armed |
| FIRING | Red (255,0,0) | Solid | Fire command active |
| LINK_LOST | Yellow (255,180,0) | Fast blink (200ms on/200ms off) | Lost contact |
| ERROR | Red (255,0,0) | Triple flash | Fault |
| Ping failure | Orange (255,100,0) | Single flash (50ms) overlaid on current pattern | Brief indicator |

### 11.3 Implementation

- Driver: single-pixel WS2812 driver using ESP32-S3 RMT peripheral.
- GPIO: 47 (fixed, on-board, defined as `RGB_LED = 47`).
- Brightness: configurable in `rlc_config.h` (`RGB_LED_BRIGHTNESS`, default: 30 out of 255).
- Pattern engine: implemented in `rlc_common` as a FreeRTOS task that accepts state changes and drives the LED accordingly. Patterns are defined as arrays of (colour, duration_ms) pairs with repeat flags.
- The ping-failure orange flash is implemented as a brief override that temporarily replaces the current pattern for 50 ms, then restores it.

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
| `BEEP_PING_FAIL` | 80 on | Ping failure |
| `BEEP_CONTINUITY_LOST` | 200 on, 100 off, 200 on, 100 off, 200 on | Continuity loss disarm (distinctive pattern) |
| `ALARM_LINK_LOST` | 200 on, 200 off, repeating | Link lost alarm |
| `ALARM_CRITICAL` | 100 on, 100 off, repeating | Critical error alarm |

### 12.2 Siren Patterns (Base only)

| Pattern Name | Behaviour | Usage |
|---|---|---|
| `SIREN_ARMED` | 500 ms on, 500 ms off, repeating | Channel armed — audible pad warning |
| `SIREN_PRE_FIRE` | Continuous ON | Pre-fire countdown — clear the pad |
| `SIREN_FIRING` | Continuous ON | Active ignition |
| `SIREN_LINK_LOST` | 500 ms on, 500 ms off, 4 cycles | Link lost alert |

### 12.3 Implementation

Buzzer patterns shall be driven by a dedicated FreeRTOS task (`buzzer_task`) in `rlc_remote` that consumes pattern descriptors from a queue. New patterns preempt active ones.

Siren control is managed directly by the base state machine. The siren pulsing in ARMED state shall be driven by a timer that toggles the siren GPIO every 500 ms. On transition to PRE_FIRE, the siren is set to continuous ON. On disarm or transition to POST_FIRE/IDLE, the siren is set OFF.

---

## 13. Error Handling

### 13.1 Error Flags

The `error_flags` field in `STATUS_UPDATE` is a bitmask:

| Bit | Flag | Description |
|---|---|---|
| 0 | `ERR_VBAT_LOW` | Base battery below `VBAT_MIN_ARM_MV` |
| 1 | `ERR_VBAT_CRITICAL` | Base battery below `VBAT_CRITICAL_MV` |
| 2 | `ERR_RELAY_FAULT` | Relay feedback input detects current on firing bus when all relays should be open (§5.4.6) |
| 3 | `ERR_CONTINUITY_LOST_WHILE_ARMED` | Continuity was lost on the armed channel |
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
| `ERR_CONTINUITY_LOST_WHILE_ARMED` | Critical | Immediate disarm of affected channel. Return to IDLE. |
| `ERR_COMM_DEGRADED` | Warning | Display warning on remote (yellow indicator). |
| `ERR_WATCHDOG_RESET` | Info | Set flag in first STATUS_UPDATE so remote can display "Base rebooted". |
| `ERR_INTERNAL` | Critical | Immediate disarm. Transition to ERROR. |

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
| `HEARTBEAT_INTERVAL_MS` | 1000 | Ping send interval |
| `HEARTBEAT_TIMEOUT_MS` | 500 | Time to wait for PONG |
| `HEARTBEAT_FAIL_THRESHOLD` | 3 | Consecutive failures before link loss |
| `HEARTBEAT_WINDOW_SIZE` | 10 | Rolling window for link quality calculation |
| `RSSI_AVERAGE_WINDOW` | 3 | Number of frames to average for RSSI display |
| `STATUS_UPDATE_INTERVAL_MS` | 2000 | Periodic status broadcast interval |
| `STATUS_STALE_TIMEOUT_MS` | 5000 | Max time without STATUS_UPDATE before remote disarms |
| `LINK_REQUEST_INTERVAL_MS` | 2000 | Interval between link request retries |
| `LINK_REQUEST_MAX_RETRIES` | 15 | Max retries before fallback to slow retry |
| `LINK_REQUEST_SLOW_INTERVAL_MS` | 5000 | Retry interval after max retries |
| `CMD_ACK_TIMEOUT_MS` | 500 | Timeout waiting for command ACK/NACK |
| `CMD_RETRY_COUNT` | 1 | Number of retries for non-fire commands |
| `FIRE_REPEAT_INTERVAL_MS` | 200 | Interval for repeated CMD_FIRE while button held |
| `FIRE_AUTHORIZATION_TIMEOUT_MS` | 500 | Max time without CMD_FIRE before aborting fire (base) |
| `PRE_FIRE_DELAY_MS` | 5000 | Siren warning before ignition |
| `FIRE_PULSE_DURATION_MS` | 2000 | Igniter current duration |
| `POST_FIRE_COOLDOWN_MS` | 2000 | Cooldown before returning to IDLE |
| `SIREN_LINK_LOST_DURATION_MS` | 4000 | Siren duration on link loss (4 × 500on/500off) |
| `NACK_DISPLAY_DURATION_MS` | 3000 | How long NACK reason text is shown on display |
| `WATCHDOG_TIMEOUT_S` | 2 | Hardware watchdog timeout |
| `DEBOUNCE_POLL_INTERVAL_MS` | 10 | Default shift-register poll interval |

### 14.2 Voltage Thresholds

| Constant | Default Value | Description |
|---|---|---|
| `BASE_VBAT_DIVIDER_RATIO` | 4.0 | Voltage divider ratio for base battery ADC |
| `BASE_VBAT_MIN_ARM_MV` | 10500 | Minimum base battery to allow arming (mV) |
| `BASE_VBAT_CRITICAL_MV` | 9000 | Critical base battery threshold (mV) |
| `REMOTE_VBAT_DIVIDER_RATIO` | 2.0 | Voltage divider ratio for remote battery ADC |
| `REMOTE_VBAT_MIN_ARM_MV` | 3500 | Minimum remote battery to allow arming (mV) |
| `REMOTE_VBAT_MIN_OPERATE_MV` | 3300 | Minimum remote battery for operation (mV) |
| `REMOTE_VBAT_CRITICAL_MV` | 3000 | Critical remote battery threshold (mV) |

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
| `RGB_LED_BRIGHTNESS` | 0–255, default: 30 |

### 14.4 Display Configuration

| Constant | Default Value | Description |
|---|---|---|
| `DISPLAY_SPI_HOST` | `SPI2_HOST` | SPI peripheral to use |
| `DISPLAY_SPI_CLOCK_HZ` | 20000000 | SPI clock frequency (20 MHz) |
| `DISPLAY_WIDTH` | 480 | Pixels |
| `DISPLAY_HEIGHT` | 320 | Pixels |
| `DISPLAY_ROTATION` | 1 | Landscape |

---

## 15. Test Requirements

The developer shall implement and document tests for the following scenarios. Tests can be executed in a bench setup with both units powered but without live igniters (use LEDs or resistors on channel outputs, and jumper wires for continuity simulation).

### 15.1 Communication Tests

| ID | Test | Expected Result |
|---|---|---|
| T-C01 | Power on remote with base off | Remote displays "Connecting..." and retries. No crash. RGB LED blue pulse. |
| T-C02 | Power on both units | Link established within 10 seconds. RSSI displayed. RGB LEDs green. |
| T-C03 | Separate units beyond range | Link lost detected within 3 seconds. Both units disarm. Siren/buzzer. RGB LEDs yellow. |
| T-C04 | Return units to range after T-C03 | Link re-established. Both units in IDLE (not armed). RGB LEDs green. |
| T-C05 | Send 1000 pings, measure loss rate | < 1% loss at 10 m LOS, < 5% loss at 100 m LOS. |
| T-C06 | Replay a captured ARM command | Base rejects (sequence number or session token invalid). NACK reason 0x08. |
| T-C07 | Flash base with different firmware version | Remote displays "FIRMWARE MISMATCH" and refuses to link. |
| T-C08 | Verify RSSI averaging | RSSI display is stable (averaged over 3 frames), not jumping per-frame. |

### 15.2 Arming Tests

| ID | Test | Expected Result |
|---|---|---|
| T-A01 | ARM with both switches armed, continuity OK | Channel arms. ACK received. Display updates. Siren pulses. RGB LEDs red blink. |
| T-A02 | ARM with base switch disarmed | NACK with reason 0x01 ("BASE KEY OFF"). Channel not armed. |
| T-A03 | ARM with remote switch disarmed | Remote does not send ARM (local guard). Display shows "Turn ARM key first". |
| T-A04 | ARM channel with no continuity | NACK with reason 0x04. |
| T-A05 | ARM second channel while one is armed | NACK with reason 0x0A. |
| T-A06 | Turn base arm switch to DISARM while armed | Immediate disarm. Low-side relay opens. Siren off. |
| T-A07 | Turn remote arm switch to DISARM while armed | DISARM sent. Base disarms. |
| T-A08 | Rotate encoder while armed | Immediate disarm. Channel selection updates. Operator must re-arm. |
| T-A09 | Verify continuity visible with arm switch OFF | All 8 channels show correct continuity on remote display regardless of base arm switch position. |
| T-A10 | ARM with relay feedback fault | NACK 0x0B ("RELAY FAULT"). Channel not armed. |
| T-A11 | ARM with stale STATUS_UPDATE (> 4s old) | Remote blocks locally ("DATA STALE — CANNOT ARM"). |
| T-A12 | ARM with low remote battery | Remote blocks locally ("REMOTE BATTERY LOW — CANNOT ARM"). |
| T-A13 | Verify channel in CMD_ACK | Simulate wrong channel in ACK → "CHANNEL MISMATCH ERROR", CMD_DISARM sent. |

### 15.3 Fire Tests

| ID | Test | Expected Result |
|---|---|---|
| T-F01 | Full fire sequence (arm → fire → complete) | Siren pulses in ARMED, continuous in PRE_FIRE/FIRING. Channel relay activates for FIRE_PULSE_DURATION_MS. Auto-disarm after. |
| T-F02 | Release fire button during pre-fire delay | Fire aborted. Return to IDLE. Siren off. |
| T-F03 | Release fire button during active fire | Cease fire. Channel relay deactivates. Return to IDLE. |
| T-F04 | Fire command on non-armed channel | NACK with reason 0x05. |
| T-F05 | Continuity lost during armed state | Immediate disarm. Error displayed. BEEP_CONTINUITY_LOST buzzer. |
| T-F06 | Link lost during firing | Base completes current fire pulse, then disarms. |
| T-F07 | Pre-fire timer expires without fire button held | Base aborts to IDLE (dead-man timeout). |
| T-F08 | Verify fire pulse timing | Measure relay ON duration with oscilloscope or logic analyser. Must match FIRE_PULSE_DURATION_MS ± 10 ms. |
| T-F09 | Verify link-health guard at PRE_FIRE→FIRING | If PONG missed at transition boundary, base aborts instead of firing. |

### 15.4 Safety Tests

| ID | Test | Expected Result |
|---|---|---|
| T-S01 | Power cycle base while armed | Boots to safe state. All relays inactive. |
| T-S02 | Power cycle remote while base armed | Base detects link loss within 3 seconds. Disarms. |
| T-S03 | Reduce base battery below VBAT_CRITICAL_MV | Base disarms and enters ERROR state. |
| T-S04 | Hold fire button at boot, then arm | Fire does not trigger (fresh press required). |
| T-S05 | Corrupt a message (bit flip simulation) | Message rejected (CRC integrity check or ESP-NOW decrypt failure). |
| T-S06 | Verify GPIO init order | Measure relay outputs with logic analyser during boot. Must be inactive before ESP-NOW init. |
| T-S07 | Watchdog test: infinite loop in main task | Unit reboots within 2 seconds. All relays inactive after reboot. |
| T-S08 | Hold fire button, then arm (button already pressed at ARMED entry) | Fire does not trigger — fresh press detection requires 0xFF→0x00 transition after entering ARMED state. |

### 15.5 Unit Tests (Host-Compilable)

| ID | Module | Test |
|---|---|---|
| T-U01 | Message serialisation | Serialise and deserialise all message types. Verify byte-for-byte correctness. `_Static_assert` on all struct sizes. |
| T-U02 | Integrity CRC | Compute CRC for known inputs. Verify against expected output. Verify rejection of wrong CRC. |
| T-U03 | Sequence number | Verify acceptance of increasing sequence numbers. Verify rejection of equal/lower. Verify reset on session establishment. |
| T-U04 | Session token | Verify acceptance of correct token. Verify rejection of wrong token. Verify atomic invalidation on re-link. |
| T-U05 | Debounce (8-bit) | Feed 0/1 sequence. Verify 0x00/0xFF detection. Verify 80 ms timing. |
| T-U06 | Debounce (16-bit) | Feed 0/1 sequence. Verify 0x0000/0xFFFF detection. Verify 160 ms timing. |
| T-U07 | Battery threshold | Feed ADC values. Verify all three remote thresholds (MIN_ARM, MIN_OPERATE, CRITICAL). |
| T-U08 | Version comparison | Verify strict MAJOR.MINOR.PATCH matching logic. |
| T-U09 | Update sequence gap | Feed update_sequence numbers with gaps. Verify warning at gap > 2. |

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

typedef struct {
    uint8_t  protocol_version;
    uint8_t  msg_type;
    uint16_t payload_length;
    uint32_t sequence_number;
    uint32_t session_token;
} rlc_msg_header_t;
_Static_assert(sizeof(rlc_msg_header_t) == 12, "Header size mismatch");

typedef struct {
    uint8_t  remote_firmware_version[3];  // [0]=major, [1]=minor, [2]=patch
    uint8_t  remote_mac[6];
} rlc_payload_link_request_t;
_Static_assert(sizeof(rlc_payload_link_request_t) == 9, "LINK_REQUEST size mismatch");

typedef struct {
    uint32_t session_token;
    uint8_t  base_firmware_version[3];    // [0]=major, [1]=minor, [2]=patch
    uint8_t  num_channels;
} rlc_payload_link_ack_t;
_Static_assert(sizeof(rlc_payload_link_ack_t) == 8, "LINK_ACK size mismatch");

typedef struct {
    uint32_t ping_timestamp;
    uint16_t remote_battery_voltage_mv;
} rlc_payload_ping_t;
_Static_assert(sizeof(rlc_payload_ping_t) == 6, "PING size mismatch");

typedef struct {
    uint32_t ping_timestamp;
    uint32_t pong_timestamp;
} rlc_payload_pong_t;
_Static_assert(sizeof(rlc_payload_pong_t) == 8, "PONG size mismatch");

typedef struct {
    uint8_t  channel;
    uint32_t integrity_crc;
} rlc_payload_cmd_arm_t;
_Static_assert(sizeof(rlc_payload_cmd_arm_t) == 5, "CMD_ARM size mismatch");

typedef struct {
    uint8_t  channel;
    uint32_t integrity_crc;
} rlc_payload_cmd_disarm_t;
_Static_assert(sizeof(rlc_payload_cmd_disarm_t) == 5, "CMD_DISARM size mismatch");

typedef struct {
    uint8_t  channel;
    uint32_t integrity_crc;
} rlc_payload_cmd_fire_t;
_Static_assert(sizeof(rlc_payload_cmd_fire_t) == 5, "CMD_FIRE size mismatch");

typedef struct {
    uint32_t integrity_crc;
} rlc_payload_cmd_cease_fire_t;
_Static_assert(sizeof(rlc_payload_cmd_cease_fire_t) == 4, "CMD_CEASE_FIRE size mismatch");

typedef struct {
    uint16_t continuity_bitmask;       // bits 0-7: channels 1-8, bits 8-15: reserved
    uint16_t channel_armed_bitmask;    // bits 0-7: channels 1-8, bits 8-15: reserved
    uint16_t channel_firing_bitmask;   // bits 0-7: channels 1-8, bits 8-15: reserved
    uint8_t  base_arm_switch;
    uint8_t  low_side_relay;
    uint16_t battery_voltage_mv;
    uint8_t  base_state;
    uint8_t  error_flags;
    uint16_t update_sequence;
} rlc_payload_status_update_t;
_Static_assert(sizeof(rlc_payload_status_update_t) == 14, "STATUS_UPDATE size mismatch");

typedef struct {
    uint8_t  acked_msg_type;
    uint32_t acked_sequence_number;
    uint8_t  channel;                  // channel the command applied to, or 0x00
} rlc_payload_cmd_ack_t;
_Static_assert(sizeof(rlc_payload_cmd_ack_t) == 6, "CMD_ACK size mismatch");

typedef struct {
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
| IDLE | CMD_ARM(ch) | All §7.2.2 guards (incl. relay feedback) | ARMED | Close low-side relay, record channel, siren pulse, ACK(ch), STATUS_UPDATE |
| IDLE | CMD_ARM(ch) | Any guard fails | IDLE | NACK with reason |
| IDLE | CMD_FIRE(ch) | — | IDLE | NACK reason 0x05 (wrong state) |
| IDLE | CMD_CEASE_FIRE | — | IDLE | ACK (idempotent) |
| ARMED | CMD_FIRE(ch) | ch == armed_ch, CRC OK, continuity OK | PRE_FIRE | Siren continuous, start pre-fire timer, ACK, STATUS_UPDATE |
| ARMED | CMD_FIRE(ch) | ch != armed_ch | ARMED | NACK reason 0x05 |
| ARMED | CMD_ARM(ch2) | ch2 != armed_ch | ARMED | NACK reason 0x0A |
| ARMED | CMD_DISARM | — | IDLE | relay_all_safe(), siren off, ACK, STATUS_UPDATE |
| ARMED | Arm switch → DISARM | — | IDLE | relay_all_safe(), siren off, STATUS_UPDATE |
| ARMED | Continuity lost on armed ch | — | IDLE | relay_all_safe(), siren off, STATUS_UPDATE, set ERR flag |
| ARMED | Continuity lost on other ch | — | ARMED | Update bitmask, STATUS_UPDATE (not a safety issue) |
| ARMED | Link lost | — | LINK_LOST | relay_all_safe(), siren link-lost pattern |
| ARMED | VBAT < critical | — | ERROR | relay_all_safe(), siren off |
| PRE_FIRE | Timer elapsed | CMD_FIRE in last 500 ms AND last PONG within HEARTBEAT_TIMEOUT_MS | FIRING | Channel relay active, start fire pulse timer, STATUS_UPDATE |
| PRE_FIRE | Timer elapsed | No CMD_FIRE in last 500 ms | IDLE | relay_all_safe(), siren off, STATUS_UPDATE (dead-man abort) |
| PRE_FIRE | CMD_CEASE_FIRE | — | IDLE | relay_all_safe(), siren off, ACK, STATUS_UPDATE |
| PRE_FIRE | Arm switch → DISARM | — | IDLE | relay_all_safe(), siren off, STATUS_UPDATE |
| PRE_FIRE | Continuity lost (armed ch) | — | IDLE | relay_all_safe(), siren off, STATUS_UPDATE, ERR flag |
| PRE_FIRE | Link lost | — | LINK_LOST | relay_all_safe(), siren link-lost pattern |
| PRE_FIRE | VBAT < critical | — | ERROR | relay_all_safe(), siren off |
| FIRING | Fire pulse timer elapsed | — | POST_FIRE | Channel relay inactive, low-side inactive, siren off, STATUS_UPDATE |
| FIRING | CMD_CEASE_FIRE | — | IDLE | relay_all_safe(), siren off, ACK, STATUS_UPDATE |
| FIRING | Arm switch → DISARM | — | IDLE | relay_all_safe(), siren off, STATUS_UPDATE |
| FIRING | Link lost | — | *(special)* | Complete fire pulse → POST_FIRE → LINK_LOST |
| FIRING | VBAT < critical | — | *(special)* | Complete fire pulse → POST_FIRE → ERROR |
| FIRING | Continuity lost (armed ch) | — | FIRING | Ignored (expected — igniter burning) |
| POST_FIRE | Cooldown elapsed | — | IDLE | — |
| POST_FIRE | CMD_ARM | — | POST_FIRE | NACK reason 0x05 |
| POST_FIRE | Link lost | — | LINK_LOST | Relays already safe |
| LINK_LOST | Valid PING received | — | IDLE | Respond PONG, resume heartbeat |
| ANY | Assertion failure | — | ERROR | relay_all_safe() |

### B.2 Remote Unit State Transitions

| Current State | Event | Guard Conditions | Next State | Actions |
|---|---|---|---|---|
| BOOT | Init + peer reg OK | — | LINKING | Start LINK_REQUEST, splash |
| LINKING | LINK_ACK | Version match | IDLE | Store token, reset seq, heartbeat, display |
| LINKING | LINK_ACK | Version mismatch | LINKING | "FIRMWARE MISMATCH", stop |
| IDLE | Encoder press | Arm ON, batt OK, data fresh, link OK | *(wait ACK)* | CMD_ARM, wait ACK |
| IDLE | ACK'd (ch match) | — | ARMED | Display, buzzer double |
| IDLE | ACK'd (ch mismatch) | — | IDLE | "CHANNEL MISMATCH", CMD_DISARM |
| IDLE | NACK'd | — | IDLE | Display reason 3s, buzzer triple |
| IDLE | ACK timeout (after retry) | — | IDLE | "ARM FAILED — NO RESPONSE" |
| IDLE | Encoder press | Arm OFF | IDLE | "Turn ARM key first" |
| IDLE | Encoder press | Batt < MIN_ARM | IDLE | "REMOTE BATTERY LOW" |
| IDLE | Encoder press | Data stale (> 4s) | IDLE | "DATA STALE — CANNOT ARM" |
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
| ARMED | STATUS: continuity lost | — | IDLE | Buzzer CONTINUITY_LOST |
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

Based on ESP32-S3-DevKitC-1 with N16R8 module. 8 channels + relay feedback. All outputs use configurable polarity.

| Function | GPIO | Notes |
|---|---|---|
| Channel 1 relay output | 4 | Digital output |
| Channel 2 relay output | 5 | Digital output |
| Channel 3 relay output | 6 | Digital output |
| Channel 4 relay output | 7 | Digital output |
| Channel 5 relay output | 15 | Digital output |
| Channel 6 relay output | 16 | Digital output |
| Channel 7 relay output | 17 | Digital output |
| Channel 8 relay output | 18 | Digital output |
| Channel 1 continuity input | 11 | Digital input, pull-up |
| Channel 2 continuity input | 12 | Digital input, pull-up |
| Channel 3 continuity input | 13 | Digital input, pull-up |
| Channel 4 continuity input | 14 | Digital input, pull-up |
| Channel 5 continuity input | 21 | Digital input, pull-up |
| Channel 6 continuity input | 38 | Digital input, pull-up |
| Channel 7 continuity input | 39 | Digital input, pull-up |
| Channel 8 continuity input | 40 | Digital input, pull-up |
| Low-side relay output | 48 | Digital output |
| Relay feedback input | 41 | Digital input, pull-up (safety verification) |
| Arm/disarm switch input | 42 | Digital input, pull-up |
| Battery voltage ADC | 1 | ADC1_CH0 — safe with Wi-Fi/ESP-NOW |
| Siren output | 2 | Digital output |
| RGB LED (status) | 47 | WS2812 via RMT (on-board, fixed) |

**Total: 22 GPIOs + 1 on-board LED = 23 pins used. 3 spare GPIOs (8, 9, 10).**

**Notes:**
- Reduced from 10 to 8 channels to free GPIOs for relay feedback input and spare capacity.
- Battery ADC is on GPIO 1 (ADC1_CH0) to avoid ADC2/Wi-Fi conflict.
- GPIO 47 has the on-board WS2812 RGB LED — no external wiring needed.
- Relay outputs are grouped on one side of the board header for easier PCB routing.
- Spare GPIOs 8, 9, 10 available for future expansion.

### C.2 Remote Unit Pin Assignment

| Function | GPIO | ADC/Notes |
|---|---|---|
| Encoder CLK (A) | 4 | Digital input, pull-up, interrupt |
| Encoder DT (B) | 5 | Digital input, pull-up, interrupt |
| Encoder SW (push) | 6 | Digital input, pull-up |
| Arm/disarm switch | 7 | Digital input, pull-up |
| Fire button | 15 | Digital input, pull-up |
| Battery voltage ADC | 1 | ADC1_CH0 — safe with Wi-Fi/ESP-NOW |
| Buzzer | 16 | Digital output |
| Display SPI MOSI | 11 | SPI2 MOSI |
| Display SPI SCLK | 12 | SPI2 CLK |
| Display CS | 10 | Chip select |
| Display DC | 13 | Data/command |
| Display RST | 14 | Reset |
| Display backlight | 21 | PWM capable |
| Display MISO | 9 | SPI2 MISO |
| RGB LED (status) | 47 | WS2812 via RMT (on-board, fixed) |

**Total: 14 GPIOs + 1 on-board LED = 15 pins used. 10 spare GPIOs.**

**Spare GPIOs available:** 2, 8, 17, 18, 38, 39, 40, 41, 42, 48 — available for future expansion (e.g., SD card, additional buttons, external status LEDs).

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
| LINK_REQUEST while already linked (same MAC) | Remote rebooted | Base generates new session token, responds normally (session reset). Not an error. |
| Duplicate LINK_ACK received | Network duplicate | Remote ignores if already in IDLE. If token differs, accept new token (session reset). |

### D.2 Heartbeat Exceptions

| Exception | Scenario | Handling |
|---|---|---|
| PONG not received within 500 ms | Single packet loss | Increment failure counter. Remote buzzer: 80 ms beep. RGB LED: orange flash. |
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
| STATUS_UPDATE shows continuity loss (armed ch) | Igniter disconnected | Remote follows base (base will have already disarmed). |
| STATUS_UPDATE shows base in ERROR | Base fault | Remote displays "BASE ERROR". Refuses ARM commands. |

---

*End of Functional Specification — RLC-FSPEC-001 v1.3*
