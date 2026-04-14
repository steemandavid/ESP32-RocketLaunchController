# Phase 1 Code Review — FSD v1.14 Compliance

**Document ID:** RLC-REVIEW-P1-001
**Reviewer:** Code Review Agent (30+ year experience)
**Date:** 2026-04-14
**Scope:** Phase 1 — Foundation and Communication
**FSD Reference:** RLC_Functional_Specification_v1_14.md
**Commit Reviewed:** `ed62aff` (Phase 1: Foundation and Communication)

---

## Verdict: MAYBE

Core architecture is solid, but several spec deviations and missing deliverables must be addressed before proceeding to Phase 2.

---

## Table of Contents

1. [Coverage Analysis](#1-coverage-analysis--missing-spec-requirements)
2. [Deviation Report](#2-deviation-report)
3. [Edge Cases & Business Rules](#3-edge-cases--business-rules)
4. [Quality Assessment](#4-quality-assessment)
5. [Summary](#5-summary)
6. [Concerns](#6-concerns)
7. [Recommendation](#7-recommendation)

---

## Files Reviewed

| File | Purpose |
|------|---------|
| `components/rlc_common/include/rlc_config.h` | All configuration constants |
| `components/rlc_common/include/rlc_protocol.h` | Protocol message types, structs, NACK codes |
| `components/rlc_common/include/rlc_message.h` | Message serialisation API |
| `components/rlc_common/src/rlc_message.c` | Message serialisation implementation |
| `components/rlc_common/include/rlc_espnow.h` | ESP-NOW driver wrapper API |
| `components/rlc_common/src/rlc_espnow.c` | ESP-NOW driver implementation |
| `components/rlc_common/include/rlc_link.h` | Link manager API |
| `components/rlc_common/src/rlc_link.c` | Link manager implementation |
| `components/rlc_common/include/rlc_version.h` | Firmware version (1.0.0) |
| `components/rlc_common/include/rlc_rgb_led.h` | RGB LED driver API |
| `components/rlc_common/src/rlc_rgb_led.c` | RGB LED driver implementation |
| `components/rlc_common/include/rlc_watchdog.h` | Watchdog API |
| `components/rlc_common/src/rlc_watchdog.c` | Watchdog implementation |
| `components/rlc_common/include/rlc_battery.h` | Battery ADC API |
| `components/rlc_common/src/rlc_battery.c` | Battery ADC implementation |
| `components/rlc_common/include/rlc_debounce.h` | Debounce engine API |
| `components/rlc_common/src/rlc_debounce.c` | Debounce engine implementation |
| `components/rlc_common/include/rlc_assert.h` | Custom assertion macro |
| `components/rlc_common/include/pin_config.h` | GPIO pin assignments |
| `components/rlc_base/src/rlc_base_main.c` | Base unit entry point |
| `components/rlc_base/src/rlc_relay.c` | Relay control |
| `components/rlc_base/src/rlc_siren.c` | Siren control |
| `components/rlc_base/src/rlc_base_state.c` | Base state machine stub |
| `components/rlc_remote/src/rlc_remote_main.c` | Remote unit entry point |
| `components/rlc_remote/src/rlc_display.c` | Display driver stub |
| `main/main.c` | Application entry point |
| `main/Kconfig.projbuild` | Build configuration |

---

## 1. Coverage Analysis — Missing Spec Requirements

### Phase 1 Deliverables Checklist (FSD §4.3)

| # | Deliverable | Status | Evidence |
|---|-------------|--------|----------|
| 1 | Project scaffolding (CMake, Kconfig, component structure) | **DONE** | CMakeLists.txt, Kconfig.projbuild, component directories |
| 2 | ESP-NOW driver wrapper | **DONE** | `rlc_espnow.c/h` |
| 3 | Message serialisation/deserialisation | **DONE** | `rlc_message.c/h` |
| 4 | Protocol header and struct definitions | **DONE** | `rlc_protocol.h` |
| 5 | Encryption setup (PMK + LMK) | **DONE** | `rlc_espnow.c:120-121, 148-153` |
| 6 | Sequence number management | **DONE** | `rlc_link.c:56-57`, `rlc_message.c:80-86` |
| 7 | Session token generation | **DONE** | `rlc_link.c:251-254` |
| 8 | RGB LED driver (WS2812 via RMT on GPIO 48) | **DONE** | `rlc_rgb_led.c/h` |
| 9 | RGB LED status colour patterns | **DONE** | `rlc_rgb_led.c:68-148` |
| 10 | Watchdog setup | **DONE** | `rlc_watchdog.c/h` |
| 11 | Version header | **DONE** | `rlc_version.h` |
| 12 | LINK_REQUEST / LINK_ACK handshake with version check | **DONE** | `rlc_link.c:147-165, 167-182, 235-282` |
| 13 | PING / PONG at 500 ms with RSSI capture | **DONE** | `rlc_link.c:202-231, 284-309` |
| 14 | Link loss detection (3 missed pings) | **DONE** | `rlc_link.c:411-425` (remote), `rlc_link.c:436-446` (base) |
| 15 | Base: boots, waits, responds, RGB LED status | **DONE** | `rlc_base_main.c`, `rlc_link.c:453-458` |
| 16 | Remote: boots, sends link reqs, pings, RSSI, link loss | **DONE** | `rlc_remote_main.c`, `rlc_link.c:393-433` |
| 17 | **Unit tests** for message serialisation, integrity CRC, sequence validation | **MISSING** | No test files exist in the project |

### CRITICAL — Missing Phase 1 Deliverables

#### C1: Unit Tests (FSD §4.3)

**Status:** MISSING

The FSD explicitly lists "Unit tests for message serialisation, integrity CRC, sequence number validation" as a Phase 1 deliverable. No test files exist in the project.

Required tests per FSD §15.5:

| Test ID | Description |
|---------|-------------|
| T-U01 | Serialise and deserialise all message types — byte-for-byte correctness |
| T-U02 | Integrity CRC — known inputs, expected outputs, rejection of wrong CRC |
| T-U03 | Sequence number — acceptance of increasing, rejection of equal/lower, reset on session |
| T-U04 | Session token — acceptance of correct, rejection of wrong, atomic invalidation on re-link |
| T-U08 | Version comparison — strict MAJOR.MINOR.PATCH matching logic |
| T-U13 | Struct field offset verification via `offsetof()` |
| T-U14 | CRC32-C test vector: `"123456789"` = `0xE3069283` |

#### C2: CRC32-C Algorithm (FSD §6.2.2)

**Status:** WRONG ALGORITHM

```
File:  components/rlc_common/src/rlc_message.c
Lines: 69-78

Current code:
    uint32_t crc = esp_crc32_le(0, (const uint8_t *)payload, payload_len);
    crc = esp_crc32_le(crc, key, sizeof(key));

Spec requirement:
    CRC32-C (Castagnoli), polynomial 0x1EDC6F41
    Initial value: 0xFFFFFFFF
    Final XOR: 0xFFFFFFFF
```

`esp_crc32_le()` implements standard CRC32 (IEEE 802.3, polynomial 0x04C11DB7), not CRC32-C (Castagnoli, polynomial 0x1EDC6F41). The ESP32-S3 provides hardware-accelerated CRC32-C via `esp_crc32c_le()`.

Additionally, the initial value is `0` instead of `0xFFFFFFFF` as specified.

**Fix:** Replace `esp_crc32_le()` with `esp_crc32c_le()` and use initial value `0xFFFFFFFF`.

#### C3: CRC Input Must Include Header (FSD §6.2.2)

**Status:** NOT IMPLEMENTED

```
File:  components/rlc_common/src/rlc_message.c
Lines: 69-78

Current:  CRC over payload + integrity_key only
Spec:     CRC over header + payload (excluding CRC field) + integrity_key
```

FSD §6.2.2 states:
> "CRC input: header_bytes || payload_bytes_excluding_crc || integrity_key_bytes (concatenated in that order). The header fields (protocol_version, msg_type, payload_length, sequence_number, session_token) are included in the CRC input to prevent a corruption in msg_type from causing misinterpretation (e.g., CMD_DISARM interpreted as CMD_FIRE)."

**Fix:** The `rlc_compute_integrity_crc()` function must accept the header bytes and compute CRC over `header + payload + key`. Alternatively, the CRC computation must be performed at the call site where both header and payload are available.

#### C4: Struct Field Offset Verification at Boot (FSD §9.9)

**Status:** NOT IMPLEMENTED

FSD §9.9:
> "At boot, before any communication is attempted, the firmware SHALL execute a runtime self-test that verifies field offsets of all packed message structs using offsetof()."

No `offsetof()` checks exist in any file. Required for all structs in `rlc_protocol.h`.

#### C5: CRC32-C Test Vector Verification at Boot (FSD §6.2.2)

**Status:** NOT IMPLEMENTED

FSD §6.2.2:
> "Test vector: CRC32-C of the ASCII string "123456789" = 0xE3069283. Implementations SHALL verify this at boot."

No boot-time CRC verification exists.

#### C6: Boot Sequence Steps Missing (FSD §9.13)

**Status:** PARTIAL

FSD §9.13 defines a strict 10-step boot sequence. Current implementation order in `rlc_base_main.c`:

| Step | Spec Action | Current |
|------|-------------|---------|
| 1 | Configure relay GPIOs to safe state | **DONE** (`relay_init()`, `siren_init()` at line 36-37) |
| 2 | Verify packed struct field offsets (`offsetof()`) | **MISSING** |
| 3 | Verify CRC32-C test vector | **MISSING** |
| 4 | Initialise ADC calibration | **DONE** (inside `rlc_battery_init()` at line 43) |
| 5 | Initialise ESP-NOW, PMK, register peer | **DONE** (lines 45-61) |
| 6 | Initialise display, read-back display ID (remote only) | **STUB** (remote `display_init()` is a no-op) |
| 7 | Configure input GPIOs, start debounce engine | **NOT YET** (Phase 2) |
| 8 | Configure hardware watchdog and TWDT | **DONE** (line 69) — but missing BOD config |
| 9 | Start FreeRTOS tasks | **PARTIAL** (link task + LED task only) |
| 10 | Begin link establishment | **DONE** (`rlc_link_init()` at line 63) |

Missing items: struct offset checks (step 2), CRC test vector (step 3), BOD configuration.

### HIGH — Missing Spec Requirements

#### H1: ESP-NOW Send Failure Tracking (FSD §6.4.1a)

**Status:** NOT IMPLEMENTED

FSD §6.4.1a:
> "A burst of 5 consecutive ESP-NOW send failures SHALL be treated as immediate link loss."

The send callback is registered (`rlc_espnow.c:136`) and invoked (`rlc_espnow.c:69-74`), but failures are not counted or acted upon. The `s_send_cb` callback fires but nothing increments a failure counter.

#### H2: LINK_REQUEST Retry Count (FSD §6.4.1)

**Status:** NOT IMPLEMENTED

FSD §6.4.1:
> "If no LINK_ACK is received after 5 attempts (10 seconds), the remote shall display 'NO LINK' and continue retrying every 2000 ms indefinitely."

`LINK_REQUEST_MAX_RETRIES` is defined as 5 in `rlc_config.h:24` but is never referenced in code. The remote retries indefinitely without tracking attempt count (`rlc_link.c:401-406`).

```c
// rlc_link.c:401-406 — retries forever, no count
if (s_state == RLC_LINK_STATE_LINKING ||
    s_state == RLC_LINK_STATE_LOST) {
    if (t - s_last_linkreq_ms >= LINK_REQUEST_INTERVAL_MS) {
        send_link_request();
    }
    return;
}
```

#### H3: Per-Task TWDT Registration (FSD §9.6)

**Status:** NOT IMPLEMENTED

FSD §9.6:
> "Each critical task (arm switch, fire button, continuity, heartbeat, state machine) SHALL register with the ESP-IDF Task Watchdog Timer via esp_task_wdt_add()."

Only the main task registers (`rlc_watchdog.c:27`). The following tasks do not register:
- `link_task` (priority 6)
- `espnow_rx` (priority 8)
- `led_task` (priority 5)

#### H4: Compile-Time Optional Logging (FSD §9.11)

**Status:** NOT IMPLEMENTED

FSD §9.11:
> "Runtime logging is disabled by default and SHALL be enabled via a compile-time Kconfig option (CONFIG_RLC_SERIAL_DEBUG_LOGGING)."

`main/Kconfig.projbuild` only defines `CONFIG_RLC_UNIT_TYPE`. The `CONFIG_RLC_SERIAL_DEBUG_LOGGING` option does not exist.

#### H5: 8-Pixel LED Strip on Base Unit (FSD §5.4.11)

**Status:** PARTIAL

FSD §5.4.11:
> "8 addressable external pixels. Pixel 0 also drives the on-board LED in parallel."

`rlc_rgb_led.c:158` configures `.max_leds = 1` — only one pixel. The base unit should configure 8 pixels and drive all 8 in unison. For the remote, single pixel is correct (FSD §5.5.8).

---

## 2. Deviation Report

### Critical Deviations

#### D1: CRC32 Algorithm Mismatch

**File:** `components/rlc_common/src/rlc_message.c`, lines 69-78
**Spec:** FSD §6.2.2 — CRC32-C (Castagnoli, polynomial 0x1EDC6F41)
**Code:** Standard CRC32 (IEEE 802.3, polynomial 0x04C11DB7)

```c
// Current (WRONG):
uint32_t crc = esp_crc32_le(0, (const uint8_t *)payload, payload_len);
crc = esp_crc32_le(crc, key, sizeof(key));

// Should be:
uint32_t crc = esp_crc32c_le(0xFFFFFFFF, (const uint8_t *)payload, payload_len);
crc = esp_crc32c_le(crc, key, sizeof(key));
```

Three distinct bugs in this function:
1. Wrong polynomial (CRC32 instead of CRC32-C)
2. Wrong initial value (0 instead of 0xFFFFFFFF)
3. Missing header bytes in CRC input (see D2)

**Impact:** Both units would use the same wrong algorithm, so they'd agree, but the implementation is non-compliant with the security specification.

#### D2: CRC Input Missing Header Bytes

**File:** `components/rlc_common/src/rlc_message.c`, lines 69-78
**Spec:** FSD §6.2.2 — CRC input: `header_bytes || payload_bytes_excluding_crc || integrity_key_bytes`

The current function `rlc_compute_integrity_crc()` only accepts `payload` and `payload_len`. It has no access to the header. The spec explicitly requires the header to be included:

> "The header fields (protocol_version, msg_type, payload_length, sequence_number, session_token) are included in the CRC input to prevent a corruption in msg_type from causing misinterpretation (e.g., CMD_DISARM interpreted as CMD_FIRE)."

**Fix:** Change the function signature to accept the header, or compute the CRC at the call site where the full message is assembled.

#### D3: ESP-NOW Recv Callback Uses ISR API from Task Context

**File:** `components/rlc_common/src/rlc_espnow.c`, lines 62-67
**Spec:** FSD §6.4.1b, §9.12

```c
// espnow_recv_cb() is called from Wi-Fi TASK context, not ISR
BaseType_t hpw = pdFALSE;
if (xQueueSendFromISR(s_rx_queue, &item, &hpw) != pdTRUE) {
    ESP_LOGW(TAG, "rx queue full, dropped");
}
if (hpw) portYIELD_FROM_ISR();  // WRONG: not in ISR context
```

ESP-IDF's `esp_now_recv_cb_t` runs in the **Wi-Fi task** context (a FreeRTOS task), not an interrupt service routine. Using `xQueueSendFromISR()` from a non-ISR context is a FreeRTOS API violation. While it works on ESP32's Xtensa port due to implementation details, it is technically undefined behavior.

Additionally, calling `portYIELD_FROM_ISR()` from task context can corrupt the scheduler state.

**Fix:**
```c
// Replace with:
if (xQueueSend(s_rx_queue, &item, 0) != pdTRUE) {
    ESP_LOGW(TAG, "rx queue full, dropped");
}
// Remove portYIELD_FROM_ISR entirely
```

### Moderate Deviations

#### D4: Overlay Flash Race Condition

**File:** `components/rlc_common/src/rlc_rgb_led.c`, lines 193-200
**Spec:** FSD §4.7 — "Direct global variable sharing between tasks without synchronisation is prohibited."

```c
void rlc_rgb_led_flash_overlay(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms)
{
    s_overlay_r = r;              // Written from link_task context
    s_overlay_g = g;
    s_overlay_b = b;
    s_overlay_duration_ms = duration_ms;
    s_overlay_active = true;      // Read by led_task without sync
}
```

Five variables are written from one task (link_task calls this via `rlc_link.c:419`) and read from another (`led_task` at `rlc_rgb_led.c:55`). No mutex, no atomic, no memory barrier. The `led_task` could read partially-updated values.

**Fix options:**
- Use a mutex (simplest)
- Pack all fields into a struct and use `s_overlay_active` as a flag set **last** with a compiler barrier
- Use a queue to pass overlay requests to the LED task

#### D5: Boot Sequence Not Following FSD §9.13 Order

**File:** `components/rlc_base/src/rlc_base_main.c`, lines 31-70

The boot sequence is missing several mandatory steps from FSD §9.13:

| Missing Step | Spec Reference | Description |
|-------------|----------------|-------------|
| Step 2 | §9.9 | Verify packed struct field offsets via `offsetof()` |
| Step 3 | §6.2.2 | Verify CRC32-C test vector (`"123456789"` = `0xE3069283`) |
| Step 8 (partial) | §9.8 | Brown-out detector configuration |

The remote boot sequence (`rlc_remote_main.c`) additionally misses the display ID read-back check (FSD §9.13, step 6).

#### D6: Sequence Number Overflow Not Handled

**File:** `components/rlc_common/src/rlc_link.c`, lines 196, 209-210, 226-227

```c
int len = rlc_msg_build(buf, MSG_PING, ++s_tx_seq, s_session_token, &p, sizeof(p));
```

`++s_tx_seq` wraps silently from `0xFFFFFFFF` to `0x00000000`.

FSD §6.2.2:
> "if a sender's sequence number reaches 0xFFFFFFFF, the sender SHALL NOT wrap to 0. Instead, the sender SHALL initiate a session re-establishment (re-link) by sending LINK_REQUEST."

**Fix:**
```c
if (s_tx_seq == UINT32_MAX) {
    // Initiate re-link
    set_state(s_role == RLC_LINK_ROLE_REMOTE ? RLC_LINK_STATE_LINKING : RLC_LINK_STATE_WAITING);
    send_link_request();
    return;
}
++s_tx_seq;
```

Note: At ~5 increments/second, overflow would take ~27 years, but the behaviour must be correct per spec.

#### D7: LED Task Priority Too High

**File:** `components/rlc_common/src/rlc_rgb_led.c`, line 180

```c
xTaskCreate(led_task, "led_task", 2048, NULL, 5, &s_led_task);
//                                                  ^ priority 5
```

FSD §9.10 specifies `rgb_led_task` at priority **1 (lowest)**. Priority 5 is equal to the heartbeat task and higher than the state machine task (4). This violates:

> "Safety-critical tasks (arm switch, fire button, heartbeat) SHALL always run at higher priority than UI tasks (display, buzzer, LED)."

**Fix:** Change priority to 1:
```c
xTaskCreate(led_task, "led_task", 2048, NULL, 1, &s_led_task);
```

#### D8: `rlc_battery_check()` Missing BATTERY_WARNING Threshold

**File:** `components/rlc_common/src/rlc_battery.c`, lines 126-131

```c
rlc_battery_status_t rlc_battery_check(uint16_t min_arm_mv, uint16_t critical_mv)
{
    if (s_voltage_mv < critical_mv) return BATTERY_CRITICAL;
    if (s_voltage_mv < min_arm_mv)  return BATTERY_LOW;
    return BATTERY_OK;
    // BATTERY_WARNING never returned!
}
```

The enum defines `BATTERY_WARNING` ("Below MIN_OPERATE threshold, remote only") but the function doesn't accept or use `min_operate_mv`. FSD §8.3.4 requires three thresholds for the remote: `REMOTE_VBAT_MIN_ARM_MV`, `REMOTE_VBAT_MIN_OPERATE_MV`, `REMOTE_VBAT_CRITICAL_MV`.

**Fix:** Add a `min_operate_mv` parameter and return `BATTERY_WARNING` when voltage is between `min_operate_mv` and `min_arm_mv`.

#### D9: Remote Remote Battery Voltage Not Propagated to PING on First Sample

**File:** `components/rlc_remote/src/rlc_remote_main.c`, lines 77-78

```c
uint16_t vbat = rlc_battery_sample();
rlc_link_set_remote_battery_mv(vbat);
```

This works correctly — battery is sampled every 100ms loop iteration and passed to the link manager before the next PING. ✅ Not actually a deviation.

---

## 3. Edge Cases & Business Rules

### Edge Case Analysis

| # | Spec Ref | Edge Case | Status | Evidence |
|---|----------|-----------|--------|----------|
| E1 | §6.4.1 | LINK_REQUEST while base in ARMED/PRE_FIRE/FIRING/POST_FIRE | **NOT GUARDED** | `handle_link_request()` at `rlc_link.c:235-259` has no application-state check. Acceptable for Phase 1 (no state machine yet) but needs a callback mechanism for Phase 3. |
| E2 | §6.4.2 | Base link loss detection — "drought timer" approach | **CORRECT** | `rlc_link.c:442`: `since >= HEARTBEAT_FAIL_THRESHOLD * HEARTBEAT_INTERVAL_MS` (1500ms drought). Spec says "no PING received for 1.5 seconds" for base. |
| E3 | §6.4.2 | PONG with wrong ping_timestamp discarded | **CORRECT** | `rlc_link.c:300-305`: mismatched timestamp silently discarded, failure counter continues. |
| E4 | §6.2.2 | Session token = 0 prevented | **CORRECT** | `rlc_link.c:251-253`: `do { new_token = esp_random(); } while (new_token == 0 \|\| new_token == s_session_token);` |
| E5 | §6.4.1 | Duplicate LINK_ACK — accept new token | **CORRECT** | `handle_link_ack()` processes new token if not in VERSION_MISMATCH state. |
| E6 | §6.4.1 | LINK_REQUEST from unexpected MAC silently ignored | **CORRECT** | `rlc_link.c:314`: `memcmp(it->src_mac, s_peer_mac, 6) != 0` |
| E7 | §6.2.2 | Session token atomically invalidated before new token | **CORRECT** | `rlc_link.c:249-254`: old session invalidated, then `reset_session()` sets new token and resets counters. |
| E8 | §6.3.3 | STATUS_UPDATE sent immediately after LINK_ACK | **CORRECT** | `rlc_link.c:257`: `send_status_update()` called after `send_link_ack()`. |
| E9 | §6.4.1 | Remote re-links after base reboot | **CORRECT** | Remote's PING with old session token is rejected by base (line 353-355), remote detects 3 missed pings, enters LINK_LOST, re-sends LINK_REQUEST. |
| E10 | §6.2.2 | Protocol version check on received messages | **CORRECT** | `rlc_message.c:52-55`: rejects messages with wrong `protocol_version`. |
| E11 | §6.4.2 | RSSI averaged over last 3 frames | **CORRECT** | `rlc_link.c:87-97`: ring buffer of size `RSSI_AVERAGE_WINDOW` (3). |

### Business Rule Issues

#### B1: Remote Must Display "NO LINK" After 5 Failed Attempts

**File:** `components/rlc_common/src/rlc_link.c`, lines 401-406
**Spec:** FSD §6.4.1

The `LINK_REQUEST_MAX_RETRIES` constant (5) exists but is never used. The remote sends LINK_REQUEST every 2s indefinitely in LINKING and LOST states, with no attempt counter or "NO LINK" indication.

**Fix:** Add a retry counter that increments on each LINK_REQUEST send and resets on LINK_ACK receipt. After `LINK_REQUEST_MAX_RETRIES` attempts, transition to a sub-state that signals "NO LINK" to the application.

#### B2: RGB LED Orange Flash Colour Mismatch

**File:** `components/rlc_common/src/rlc_link.c`, line 419
**Spec:** FSD §11.2

```c
// Current:
rlc_rgb_led_flash_overlay(255, 120, 0, 250);   // Orange (255, 120, 0)

// Spec §11.2:
// Orange (255, 100, 0) — Single flash (250ms)
```

Minor colour discrepancy: (255, 120, 0) vs specified (255, 100, 0).

**Fix:** Change to `(255, 100, 0, 250)`.

#### B3: `HEARTBEAT_WINDOW_SIZE` (10) Defined But Unused

**File:** `components/rlc_common/include/rlc_config.h`, line 17

This constant is required for ERR_COMM_DEGRADED calculation (>30% ping failure rate in last 10 pings per FSD §7.2.2 guard 10). It exists but has no implementation. This is not Phase 1 scope but the orphaned constant should be noted.

---

## 4. Quality Assessment

### Strengths

| # | Area | Evidence |
|---|------|----------|
| S1 | Single-task-owner model for link state | `rlc_link.c`: all state mutated only by `link_task` via internal queue — eliminates most race conditions |
| S2 | Proper mutex usage for external reads | `rlc_link.c:84-85`: `lock()`/`unlock()` wrappers protect all `rlc_link_get_status()` reads |
| S3 | Static asserts on packed structs | `rlc_protocol.h:84,90,97,103,109,116,122,128,133,146,153,160`: `_Static_assert` on all struct sizes |
| S4 | Configurable GPIO polarity | `pin_config.h:41,49,53`: `PIN_*_ACTIVE` constants for all outputs |
| S5 | Boot safety ordering | `rlc_base_main.c:36-37`: `relay_init()` + `siren_init()` before ESP-NOW |
| S6 | Decoupled receive path | `rlc_espnow.c:46-87`: recv callback → queue → worker task, matching FSD §6.4.1b |
| S7 | Clean component architecture | Separation of `rlc_common`, `rlc_base`, `rlc_remote` per FSD §4.1 |
| S8 | Session token non-zero guarantee | `rlc_link.c:251-253`: loop excludes 0 and previous token |
| S9 | Peer MAC filtering | `rlc_link.c:314`: frames from unknown MAC silently dropped |

### Issues

#### Q1: ESP-NOW Recv Callback API Misuse (HIGH)

**File:** `components/rlc_common/src/rlc_espnow.c`, lines 62-67

`xQueueSendFromISR()` used from Wi-Fi task context. See Deviation D3 for full details.

#### Q2: LED Overlay Race Condition (HIGH)

**File:** `components/rlc_common/src/rlc_rgb_led.c`, lines 193-200

Five shared variables written from one task, read from another, without synchronisation. See Deviation D4 for full details.

#### Q3: Three CRC Bugs in One Function (HIGH)

**File:** `components/rlc_common/src/rlc_message.c`, lines 69-78

Wrong algorithm, wrong initial value, missing header in input. See Coverage items C2, C3 and Deviation D1.

#### Q4: Sequence Number Overflow (MEDIUM)

**File:** `components/rlc_common/src/rlc_link.c`, lines 196, 209, 226

`++s_tx_seq` wraps silently. See Deviation D6.

#### Q5: Task Priority Discrepancies (MEDIUM)

FSD §9.10 specifies exact task priorities. Current implementation:

| Task | Spec Priority | Code Priority | File:Line |
|------|---------------|---------------|-----------|
| `espnow_rx` | N/A (not in spec) | 8 | `rlc_espnow.c:130` |
| `link_task` | 5 (heartbeat_task) | 6 | `rlc_link.c:504` |
| `led_task` | 1 (lowest) | **5** | `rlc_rgb_led.c:180` |

The LED task at priority 5 is critically wrong — it equals the heartbeat task and exceeds the state machine task (4). This will cause problems in Phase 3 when the state machine is implemented.

**Fix:** Lower LED task to priority 1.

#### Q6: Misleading Link Recovery Log (LOW)

**File:** `components/rlc_common/src/rlc_link.c`, lines 332-337

```c
if (s_state == RLC_LINK_STATE_LOST) {
    if (hdr.msg_type == MSG_PING || hdr.msg_type == MSG_PONG ||
        hdr.msg_type == MSG_LINK_REQUEST || hdr.msg_type == MSG_LINK_ACK) {
        ESP_LOGI(TAG, "link recovery frame 0x%02x", hdr.msg_type);
    }
}
```

This logs "link recovery frame" for any matching frame type, but the actual state recovery only happens for PING (base, line 364) or PONG/LINK_ACK (remote, lines 375-376). A LINK_REQUEST received while LOST on the base would log "link recovery" but not actually recover (it goes to `handle_link_request()` which creates a new session and sets LINKED). A STATUS_UPDATE from a remote in LOST would not log but is also not handled.

The log is misleading — it suggests recovery happened when it may not have yet.

#### Q7: File-Scope Variable Placement (LOW)

**File:** `components/rlc_common/src/rlc_link.c`, line 186

```c
static uint16_t s_status_update_seq = 0;
```

This variable is declared at file scope between function definitions rather than with the other link state variables at lines 44-75. It should be grouped with the other state for clarity.

---

## 5. Summary

### Phase 1 Completeness Score

| Category | Score | Notes |
|----------|-------|-------|
| Architecture | 9/10 | Excellent single-task-owner model, clean separation |
| Protocol Compliance | 5/10 | CRC algorithm wrong, CRC scope wrong, missing retry count |
| Boot Sequence | 6/10 | Safety ordering correct, missing self-tests |
| Concurrency | 7/10 | Mostly good, one race condition in LED overlay |
| Spec Coverage | 7/10 | Core features present, unit tests missing |
| Code Quality | 8/10 | Clean, well-commented, proper static asserts |

### Must-Fix Items (6 items blocking Phase 1 sign-off)

| # | Item | Spec Ref | Severity |
|---|------|----------|----------|
| 1 | **Unit tests** for serialisation, CRC, sequence numbers | §4.3, §15.5 | CRITICAL |
| 2 | **CRC32-C algorithm** — use Castagnoli polynomial | §6.2.2 | CRITICAL |
| 3 | **CRC input includes header** bytes | §6.2.2 | CRITICAL |
| 4 | **Boot self-tests** — struct offsets + CRC test vector | §9.9, §9.13 | HIGH |
| 5 | **ESP-NOW recv callback** — replace `xQueueSendFromISR` with `xQueueSend` | §6.4.1b | HIGH |
| 6 | **LED overlay thread safety** — add synchronisation | §4.7 | HIGH |

### Recommended Fixes (not blockers, but should be addressed)

| # | Item | Spec Ref | Priority |
|---|------|----------|----------|
| R1 | Lower LED task priority from 5 to 1 | §9.10 | HIGH |
| R2 | Add LINK_REQUEST retry count tracking ("NO LINK") | §6.4.1 | MEDIUM |
| R3 | Add sequence number overflow guard | §6.2.2 | MEDIUM |
| R4 | Register all tasks with TWDT | §9.6 | MEDIUM |
| R5 | Add `CONFIG_RLC_SERIAL_DEBUG_LOGGING` Kconfig option | §9.11 | MEDIUM |
| R6 | Configure 8-pixel LED strip for base unit | §5.4.11 | LOW |
| R7 | Fix RGB LED orange colour to (255, 100, 0) | §11.2 | LOW |
| R8 | Add BATTERY_WARNING threshold to `rlc_battery_check()` | §8.3.4 | LOW |
| R9 | Add `rlc_link_set_app_state()` hook for Phase 3 LINK_REQUEST rejection | §6.4.1 | LOW |
| R10 | Add ESP-NOW send failure counter (5 consecutive = link loss) | §6.4.1a | MEDIUM |

---

## 6. Concerns

### 6.1 Security

The CRC algorithm and input scope deviations weaken the integrity protection layer. The spec explicitly designed the CRC to cover the header to prevent msg_type corruption (e.g., CMD_DISARM being interpreted as CMD_FIRE). While ESP-NOW's AES-128-CCM encryption is the primary security boundary (as the spec notes), the application-layer CRC is the second line of defence and must match the specification.

### 6.2 Concurrency

The LED overlay race condition is a code quality concern that could cause garbled LED output under timing-dependent conditions. While this is cosmetic (not safety-critical in Phase 1), it violates FSD §4.7's prohibition on unprotected shared variables and sets a bad precedent for Phase 3 where shared state will be safety-critical.

### 6.3 Maintainability

The link manager has no mechanism to query application-level state. Phase 3 requires rejecting LINK_REQUEST during ARMED/PRE_FIRE/FIRING/POST_FIRE (FSD §6.4.1). The link manager should have a registered callback or a `rlc_link_set_app_state()` function that the state machine can call. Consider adding this hook now to avoid rearchitecting later.

### 6.4 Testing

The complete absence of unit tests is the single biggest gap. The FSD explicitly requires host-compilable tests for serialisation, CRC, and sequence numbers (FSD §4.3, §15.5). These are straightforward to implement — the `rlc_common` component is already designed for hardware independence — and should exist before Phase 2 begins. The spec states (FSD §4.5): "The rlc_common component shall compile on a host machine for unit testing without hardware."

### 6.5 Task Priorities

The LED task at priority 5 could starve the state machine task (spec priority 4). While not safety-critical in Phase 1, it will become critical when the state machine is added in Phase 3. The FSD mandates that safety-critical tasks always run at higher priority than UI tasks (FSD §9.10). This should be corrected now.

---

## 7. Recommendation

Fix the 6 must-fix items listed in Section 5, then Phase 1 is ready for integration testing and progression to Phase 2.

The fixes are well-scoped and can be implemented in a single follow-up commit:

1. Add `tests/` directory with host-compilable unit tests for `rlc_message`, `rlc_debounce`, `rlc_battery`
2. Change `esp_crc32_le()` to `esp_crc32c_le()` with correct initial value
3. Extend `rlc_compute_integrity_crc()` to accept header bytes
4. Add boot self-test functions for struct offsets and CRC test vector
5. Replace `xQueueSendFromISR()` with `xQueueSend()` in `rlc_espnow.c`
6. Add mutex to `rlc_rgb_led_flash_overlay()`

---

*End of Phase 1 Code Review — RLC-REVIEW-P1-001*
