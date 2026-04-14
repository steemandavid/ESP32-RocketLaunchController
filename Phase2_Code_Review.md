# Phase 2 Code Review — Input/Output and Debouncing

**Document ID:** RLC-REVIEW-P2-001
**Reviewer:** Code Review Agent
**Date:** 2026-04-14
**Scope:** Phase 2 — Input/Output and Debouncing
**FSD Reference:** RLC_Functional_Specification_v1_14.md
**Commit Reviewed:** `aafacd0`

---

## Verdict: PASS WITH NOTES

Phase 2 is a solid, well-structured implementation that correctly delivers all specified hardware I/O modules with proper debouncing, continuity sensing, arm switch monitoring, and STATUS_UPDATE generation. The code builds clean (zero warnings on both targets), follows the established Phase 1 architecture patterns, and adheres to FSD task priorities. Several findings warrant attention before Phase 3, but none are safety-critical blockers.

---

## Table of Contents

1. [Coverage Analysis](#1-coverage-analysis)
2. [Deviation Report](#2-deviation-report)
3. [Edge Cases & Safety](#3-edge-cases--safety)
4. [Concurrency & Platform Issues](#4-concurrency--platform-issues)
5. [Error Handling](#5-error-handling)
6. [Code Quality](#6-code-quality)
7. [Summary](#7-summary)
8. [Recommendation](#8-recommendation)

---

## Files Reviewed

| File | Purpose |
|------|---------|
| `rlc_base/src/rlc_base_main.c` | Base unit boot + Phase 2 task wiring |
| `rlc_base/src/rlc_continuity.c` | 8-channel ADC1 continuity sensing with hysteresis |
| `rlc_base/include/rlc_continuity.h` | Continuity API |
| `rlc_base/src/rlc_arm_sense.c` | Arm sense GPIO debounce + contact welding detection |
| `rlc_base/include/rlc_arm_sense.h` | Arm sense API |
| `rlc_base/src/rlc_base_battery.c` | Base battery monitoring task |
| `rlc_base/include/rlc_base_battery.h` | Base battery API |
| `rlc_base/src/rlc_status_update.c` | STATUS_UPDATE generation (periodic + event) |
| `rlc_base/include/rlc_status_update.h` | STATUS_UPDATE API |
| `rlc_base/CMakeLists.txt` | Base build configuration |
| `rlc_remote/src/rlc_remote_main.c` | Remote unit boot + Phase 2 task wiring |
| `rlc_remote/src/rlc_fire_button.c` | Fire button debounce + fresh-press + LED control |
| `rlc_remote/include/rlc_fire_button.h` | Fire button API |
| `rlc_remote/src/rlc_arm_switch.c` | Remote arm switch debounce + LED |
| `rlc_remote/include/rlc_arm_switch.h` | Remote arm switch API |
| `rlc_remote/src/rlc_remote_battery.c` | Remote battery monitoring (3 thresholds) |
| `rlc_remote/include/rlc_remote_battery.h` | Remote battery API |
| `rlc_remote/src/rlc_encoder.c` | Encoder long-press detection (500 ms) |
| `rlc_remote/include/rlc_encoder.h` | Encoder API with long-press callback |
| `rlc_remote/CMakeLists.txt` | Remote build configuration |
| `rlc_common/include/rlc_config.h` | Continuity constants (thresholds, hysteresis) |
| `rlc_common/include/rlc_protocol.h` | `rlc_continuity_band_t` enum |
| `rlc_common/include/rlc_battery.h` | ADC handle sharing |
| `rlc_common/include/rlc_link.h` | `rlc_link_send_status_update()` |
| `rlc_common/src/rlc_selftest.c` | Boot self-tests (suites 10-11: continuity) |
| `rlc_common/include/pin_config.h` | GPIO pin assignments |

---

## 1. Coverage Analysis

### Phase 2 Deliverables Checklist (FSD §4.3 Phase 2)

| # | Deliverable | Status | Evidence |
|---|-------------|--------|----------|
| 1 | Shift-register debounce engine (generic) | **DONE** (Phase 1) | `rlc_debounce.c/h` — 8-bit and 16-bit |
| 2 | Battery voltage ADC driver | **DONE** (Phase 1) | `rlc_battery.c/h` — ADC1, 8-sample avg |
| 3 | Base: GPIO config for relays, arm sense, siren | **DONE** | `pin_config.h`, `relay_init()`, `siren_init()` |
| 4 | Base: ADC1 config for battery + 8 continuity inputs | **DONE** | `rlc_battery.c` + `rlc_continuity.c` |
| 5 | Base: `relay_fire_set()`, `relay_fire_all_off()`, `relay_all_safe()` | **DONE** (Phase 1) | `rlc_relay.c` stubs |
| 6 | Base: Arm switch sense monitoring | **DONE** | `rlc_arm_sense.c` — GPIO 21, 16-bit debounce |
| 7 | Base: Continuity monitoring task | **DONE** | `rlc_continuity.c` — 8-ch, 64-sample, hysteresis |
| 8 | Base: Arm switch monitoring (debounced) | **DONE** | `rlc_arm_sense.c` — 16-bit shift-register |
| 9 | Base: Battery monitoring with threshold | **DONE** | `rlc_base_battery.c` — 2 thresholds |
| 10 | Base: STATUS_UPDATE generation | **DONE** | `rlc_status_update.c` — periodic + event |
| 11 | Remote: Rotary encoder driver | **DONE** (Phase 1, enhanced) | `rlc_encoder.c` — long-press added |
| 12 | Remote: Fire button driver | **DONE** | `rlc_fire_button.c` — 8-bit debounce, fresh-press |
| 13 | Remote: Arm switch monitoring | **DONE** | `rlc_arm_switch.c` — 16-bit debounce |
| 14 | Remote: Battery monitoring | **DONE** | `rlc_remote_battery.c` — 3 thresholds |
| 15 | Remote: Buzzer pattern player | **DONE** (Phase 1) | `rlc_buzzer.c` |
| 16 | Unit tests for debounce, battery, continuity | **PARTIAL** | Boot self-tests for continuity classification + bands encoding. Debounce + battery already tested in Phase 1 self-tests. |

### FSD §5.4.2 Continuity Band Classification — Verified

| Feature | Spec Requirement | Implementation | Status |
|---------|-----------------|----------------|--------|
| 4-band enum (OPEN/GOOD/MARGINAL/SHORT) | Enum values match wire encoding | `rlc_continuity_band_t`: 0,1,2,3 | **DONE** |
| 8-channel round-robin at 100ms/ch | 800ms full-system update | `continuity_task`: `vTaskDelay(CONT_SAMPLE_INTERVAL_MS)` | **DONE** |
| 64-sample burst oversampling | Noise reduction | `sample_channel()`: loop 64× `adc_oneshot_read()` | **DONE** |
| `adc_cali_raw_to_voltage()` calibration | Per-channel calibration | Per-channel `s_cali_handles[i]` | **DONE** |
| Hysteresis at band boundaries | ±200/±5000/±50000 µV | `classify_with_hysteresis()` | **DONE** |
| Event-driven STATUS_UPDATE on band change | Immediate trigger | `s_on_change_cb` → `status_update_trigger()` | **DONE** |

### FSD §5.3 Debounce — Verified

| Input | Spec Width | Spec Poll | Code | Status |
|-------|-----------|-----------|------|--------|
| Fire button | 8-bit | 10 ms | `DEBOUNCE_8BIT`, 10 ms poll | **MATCH** |
| Base arm sense | 16-bit | 10 ms | `DEBOUNCE_16BIT`, 10 ms poll | **MATCH** |
| Remote arm switch | 16-bit | 10 ms | `DEBOUNCE_16BIT`, 10 ms poll | **MATCH** |
| Encoder push button | 16-bit | 10 ms | `DEBOUNCE_16BIT`, 10 ms poll | **MATCH** |

### FSD §9.10 Task Priorities — Verified

| Task | Spec Priority | Code Priority | Core | Stack | Status |
|------|--------------|---------------|------|-------|--------|
| `arm_switch_task` (base) | 7 | 7 | 0 | 2048 | **MATCH** |
| `continuity_task` | 5 | 5 | 0 | 4096 | **MATCH** |
| `battery_task` (base) | 3 | 3 | 0 | 2048 | **MATCH** |
| `status_update_task` | 3 | 3 | 0 | 4096 | **MATCH** |
| `fire_btn_task` (remote) | 7 | 7 | 0 | 2048 | **MATCH** |
| `arm_sw_task` (remote) | 6 | 6 | 0 | 2048 | **MATCH** |
| `battery_task` (remote) | 3 | 3 | 0 | 2048 | **MATCH** |

---

## 2. Deviation Report

### CRITICAL — None

No safety-critical deviations found.

### MAJOR

#### M1: Contact Welding Callback Semantics Ambiguous

**File:** `rlc_arm_sense.c`, lines 90-110
**Spec:** FSD §5.4.3, §7.3.2

```c
static void weld_check(void)
{
    ...
    if (arm_sense_level != 0) {
        ESP_LOGE(TAG, "CONTACT WELD DETECTED: arm relay OFF but sense reads HIGH");
        if (s_on_change_cb) {
            s_on_change_cb(true);  // Reports "armed" when it should signal fault
        }
    }
}
```

The welding detection calls `s_on_change_cb(true)` — the same callback used for "arm relay closed (legitimate)". The integrator (`on_arm_change_cb` in `rlc_base_main.c`) blindly calls `status_update_trigger()` regardless. This means:

1. The Phase 3 state machine cannot distinguish between a legitimate arm event and a welding fault.
2. No `ERR_RELAY_FAULT` error flag is set (FSD §5.4.3: "The firmware SHALL set `ERR_RELAY_FAULT` and refuse all arming").

**Fix:** Either add a separate `arm_sense_register_fault_cb()` API, or change the callback to include a fault parameter. The base state machine (Phase 3) must be able to react differently to a welding fault vs. a legitimate arm state change.

#### M2: Self-Test Duplicates Classification Logic Instead of Testing Production Code

**File:** `rlc_selftest.c`, lines 475-488
**Spec:** FSD §15.5 (T-U10, T-U11)

```c
/* Replicate the classification logic inline for self-test —
 * must match rlc_continuity.c exactly. */
typedef enum {
    TEST_CONT_OPEN = 0,
    TEST_CONT_GOOD = 1,
    TEST_CONT_MARGINAL = 2,
    TEST_CONT_SHORT = 3,
} test_cont_band_t;

static test_cont_band_t test_classify_initial(int32_t uv) { ... }
```

The self-test re-implements the classification algorithm rather than calling the production function. If someone changes the threshold logic in `rlc_continuity.c`, the test will still pass because it tests its own copy. This defeats the purpose of the test.

**Fix:** Either:
- Make `classify_initial()` a non-static function accessible from the test, or
- Create a test wrapper that includes `rlc_continuity.c` directly, or
- At minimum, add static_asserts that verify the test enum values match `rlc_continuity_band_t`.

Additionally, the hysteresis test (T-U11 in the plan) was not implemented — only the initial classification (no hysteresis) is tested.

#### M3: Encoder Poll Not Called from a Task

**File:** `rlc_remote/src/rlc_remote_main.c`
**Spec:** FSD §5.5.1, §8.2.3

The encoder push button requires `encoder_poll_button()` to be called periodically (it drives the debounce engine and checks the long-press timer). In the current `rlc_remote_main.c`, this function is never called in the housekeeping loop. The `encoder_init()` sets up the ISR for rotation, but **the push button debounce and long-press detection will not work** until `encoder_poll_button()` is called from a loop or task.

**Fix:** Add `encoder_poll_button()` to the remote's housekeeping loop:
```c
while (1) {
    rlc_watchdog_feed();
    encoder_poll_button();  // Required for debounce + long-press
    ...
}
```

Or, if the encoder should have its own task per FSD §9.10 (priority 3), create an `encoder_start_task()` that calls it at 10ms intervals.

### MINOR

#### m1: Base Battery Task Doesn't Register with TWDT

**File:** `rlc_base/src/rlc_base_battery.c`, line 42
**Spec:** FSD §9.6

```c
xTaskCreatePinnedToCore(battery_task, "battery_task", 2048, NULL, 3, NULL, 0);
```

The task handle is not captured (`NULL`), so `rlc_watchdog_add_task()` cannot be called. The remote battery task (`rlc_remote_battery.c`, line 54) has the same issue. While the task does call `esp_task_wdt_reset()` internally, it cannot be individually monitored or reset by the watchdog subsystem.

Compare with `arm_sense_start_task()` (line 180) and `continuity_start_task()` which correctly save the handle and register.

**Fix:** Capture the task handle and call `rlc_watchdog_add_task()`.

#### m2: `s_status_update_seq` Not Incremented Locally

**File:** `rlc_base/src/rlc_status_update.c`, lines 32-62
**Spec:** FSD §6.4.3

The comment says "update_sequence is managed by the link manager" and `p.update_sequence` is left at 0 (default). If `rlc_link_send_status_update()` doesn't populate this field, the remote will receive `update_sequence = 0` on every STATUS_UPDATE, preventing gap detection.

**Investigate:** Check whether `rlc_link_send_status_update()` in `rlc_link.c` fills in the `update_sequence` field. If not, it must be incremented here.

#### m3: Continuity GPIO Pin Array Uses `int` Not `uint8_t`

**File:** `rlc_continuity.c`, line 32

```c
static const int s_gpio[NUM_CHANNELS] = { ... };
```

These are compile-time constants that are GPIO pin numbers (0-48). `uint8_t` would be more appropriate and consistent with ESP-IDF conventions. Trivial — purely informational.

#### m4: Remote Battery Status Not Used in Guard

**File:** `rlc_remote/src/rlc_remote_main.c`

`remote_battery_get_status()` is declared and implemented but never called by the remote main or any guard condition yet. This is expected — the Phase 3 state machine will use it for the arming guard (FSD §8.2.3 guard 2). Noted for Phase 3 integration.

---

## 3. Edge Cases & Safety

### Safety-Critical — All Verified

| # | Scenario | Spec Ref | Assessment | Risk |
|---|----------|----------|------------|------|
| S1 | Fire button held at boot — fresh press prevents false trigger | §5.5.3 | `s_was_released = true` initially; button must transition 0xFF→0x00. Held-at-boot reads 0x00 but `s_was_released` stays true because no 0xFF→0x00 transition occurs. | **SAFE** |
| S2 | Arm switch disconnected wire = disarmed (fail-safe) | §5.5.2 | Pull-up enabled: HIGH = disarmed. Wire break → pulled HIGH → disarmed. | **SAFE** |
| S3 | Fire button disconnected wire = not pressed (fail-safe) | §5.5.3 | Pull-up enabled: HIGH = released. Wire break → pulled HIGH → not pressed. | **SAFE** |
| S4 | Base arm sense disconnected = disarmed | §5.4.3 | No pulls: external 10kΩ pulls to GND → LOW → disarmed. | **SAFE** |
| S5 | Continuity during FIRING reads OPEN (expected) | §5.4.2 | NC contact physically disconnected when relay energised. ADC reads R_pull voltage → OPEN. Noted in log as expected. | **CORRECT** |
| S6 | ADC read failure returns 0 µV | `rlc_continuity.c:130` | Returns 0 on error, which classifies as SHORT. This is conservative — SHORT doesn't block arming, and the 0 reading will persist until ADC recovers. | **ACCEPTABLE** |

### Edge Cases Requiring Attention

| # | Edge Case | File | Assessment |
|---|-----------|------|------------|
| E1 | Welding detection fires callback as "armed=true" | `rlc_arm_sense.c:107` | **Issue M1** — Phase 3 must handle differently |
| E2 | Two STATUS_UPDATE triggers in rapid succession | `rlc_status_update.c:25-30` | `s_trigger` is a simple bool — if continuity and arm sense both change before the task processes, only one update is sent. This is acceptable per spec (updates coalesced). |
| E3 | ADC calibration not supported on some channels | `rlc_continuity.c:218-234` | Falls back to raw conversion (line 142-143). Reasonable degradation. |
| E4 | Encoder button polling missing from remote main | `rlc_remote_main.c` | **Issue M3** — long-press ARM confirmation won't work |

---

## 4. Concurrency & Platform Issues

### Thread Safety Assessment

| Shared Resource | Writer(s) | Reader(s) | Protection | Assessment |
|----------------|-----------|-----------|------------|------------|
| `s_bands[]` (continuity) | `continuity_task` | `status_update_task`, `base_main` housekeeping | None (volatile reads on ESP32) | **ACCEPTABLE** — single writer, ESP32 byte/half-word reads are atomic. 16-bit `continuity_get_bands()` packs into `uint16_t` which is atomic on 32-bit Xtensa. |
| `s_armed` (arm_sense) | `arm_sense_task` | `status_update_task`, `base_main` | `volatile bool` | **ACCEPTABLE** — single writer, atomic on ESP32. |
| `s_trigger` (status_update) | `continuity_task`, `arm_sense_task` | `status_update_task` | `volatile bool` | **ACCEPTABLE** — simple flag, coalescing is correct behaviour. |
| `s_armed` (remote arm_switch) | `arm_switch_task` | `remote_main` housekeeping | `volatile bool` | **ACCEPTABLE** — single writer. |
| `s_fresh_press` (fire_button) | `fire_btn_task` | Any caller of `was_fresh_press()` | `volatile bool` with atomic check-and-clear | **POTENTIAL ISSUE** — see below. |

### Thread Safety Concern — `fire_button_was_fresh_press()`

**File:** `rlc_fire_button.c`, lines 164-171

```c
bool fire_button_was_fresh_press(void)
{
    if (s_fresh_press) {
        s_fresh_press = false;
        return true;
    }
    return false;
}
```

The check-and-clear is not atomic — an interrupt or another task reading this between the `if` and the `false` assignment could see stale state. On ESP32's Xtensa, this is technically a read-modify-write without protection. However:

- There is only one writer (`fire_btn_task`) and one reader (the future Phase 3 state machine task).
- On ESP32, `bool` reads/writes are atomic for single-byte values.
- The worst case is a double-read of the same press event, which is a benign false positive in the Phase 3 context (the state machine would attempt to fire a second time, but the fire command would be rejected because the channel is already firing).

**Assessment:** Minor concern. A `portENTER_CRITICAL` section or `xTaskNotify` would be cleaner, but not a safety risk.

### Platform API Usage

| API | Usage | Assessment |
|-----|-------|------------|
| `adc_oneshot_read()` | Continuity burst sampling | **CORRECT** — called from task context |
| `adc_cali_raw_to_voltage()` | Per-channel calibration | **CORRECT** — handles NULL calibration handle |
| `adc_oneshot_io_to_channel()` | GPIO-to-ADC mapping | **CORRECT** — validates `ADC_UNIT_1` |
| `gpio_config()` | All GPIO setup | **CORRECT** — proper pull-up/pull-down configuration |
| `xTaskCreatePinnedToCore()` | All tasks | **CORRECT** — explicit core pinning |
| `esp_task_wdt_reset()` | All tasks | **CORRECT** — called in every task loop |

### ADC Handle Sharing

The continuity module correctly reuses the battery module's ADC handle via `rlc_battery_get_adc_handle()` — avoiding ESP-IDF's "one unit handle per ADC" restriction. The handle is obtained after `rlc_battery_init()` in the boot sequence, which is the correct order.

---

## 5. Error Handling

| Module | Error Condition | Handling | Assessment |
|--------|----------------|----------|------------|
| Continuity init | ADC handle not available | `ESP_LOGE` + early return | **GOOD** — prevents null pointer dereference |
| Continuity init | GPIO not a valid ADC1 pin | `ESP_LOGE` + `continue` (skips channel) | **GOOD** — graceful degradation |
| Continuity init | ADC channel config fails | `ESP_LOGE` + `continue` | **GOOD** |
| Continuity sample | `adc_oneshot_read()` fails | `ESP_LOGW` + return 0 | **ACCEPTABLE** — classifies as SHORT |
| Arm sense weld | Relay off but sense HIGH | `ESP_LOGE` + callback | **Issue M1** — callback semantics ambiguous |
| Fire button init | N/A (no error paths) | — | **OK** — GPIO config is deterministic |
| Encoder ISR | `gpio_install_isr_service()` | No error check | **MINOR** — could fail if called twice |

### Missing Error Handling

| # | Module | Missing Check | Severity |
|---|--------|--------------|----------|
| 1 | `continuity_start_task()` | `xTaskCreatePinnedToCore` return value not checked | LOW — would crash on stack overflow anyway |
| 2 | `status_update_start_task()` | Same — task creation not checked | LOW |
| 3 | `encoder_init()` | `gpio_install_isr_service()` return value not checked | LOW — only fails if already installed |

---

## 6. Code Quality

### Strengths

| # | Area | Evidence |
|---|------|---------|
| Q1 | Consistent architecture | All modules follow Phase 1 patterns: `*_init()`, `*_start_task()`, callback registration |
| Q2 | Correct polarity handling | Arm sense correctly inverts debounce state for HIGH=armed convention |
| Q3 | Proper TWDT integration | All tasks register with `rlc_watchdog_add_task()` where handle is available |
| Q4 | Clean separation | Base modules don't reference remote modules, and vice versa. Shared code stays in `rlc_common`. |
| Q5 | Pin configuration correctness | All pins match FSD Appendix C.1 — GPIO 10 replaces strapping pin GPIO 3 for CH2 |
| Q6 | Boot sequence ordering | `relay_init()` + `siren_init()` before ESP-NOW init, matching FSD §9.13 |
| Q7 | LED control | Fire button drives red/green LEDs correctly based on press state |

### Issues

| # | File | Issue |
|---|------|-------|
| Q1 | `rlc_arm_sense.c:96-97` | Weld check returns early without logging when relay is energised. This is correct but could be confusing during debugging. |
| Q2 | `rlc_encoder.c:34` | `ENCODER_LOCKOUT_US = 5000` (5ms) but spec says "2 ms lockout". Minor — the longer lockout provides additional noise immunity. |
| Q3 | `rlc_fire_button.c:36` | `s_was_released = true` assumes released at boot. This is correct per FSD ("fail-safe: disconnected = not pressed"), but worth a comment that this is intentional. |
| Q4 | `rlc_status_update.c:86-89` | `status_update_init()` is a no-op but still exposed in the header. Could be removed, or used for future static init. |

---

## 7. Summary

| Category | Critical | Major | Minor | Info |
|----------|----------|-------|-------|------|
| Spec conformance | 0 | 2 (M1, M3) | 2 (m2, m4) | 1 (Q2) |
| Correctness | 0 | 1 (M2) | 1 (m1) | 2 (m3, Q4) |
| Safety | 0 | 0 | 0 | 1 (Q3) |
| Concurrency | 0 | 0 | 0 | 1 (fresh_press) |
| Error handling | 0 | 0 | 0 | 3 |
| Code quality | 0 | 0 | 2 | 4 |

### Phase 1 Review Follow-Up

| P1 Finding | Status in P2 |
|------------|--------------|
| D4: LED overlay race condition | Not re-introduced — no new overlay usage |
| D7: LED task priority too high | Confirmed fixed (priority 1 from P1 fix) |
| R1: TWDT per-task registration | Correctly implemented in all new tasks except battery tasks (m1) |
| R6: 8-pixel LED strip base | Confirmed: `rlc_rgb_led_set_pixel_count(8)` in base_main |

---

## 8. Recommendation

**Proceed to Phase 3** after addressing the two major functional issues:

1. **M3 (encoder polling):** Add `encoder_poll_button()` to the remote housekeeping loop. Without this, the 500ms long-press ARM confirmation will not work. This is a functional gap that would be immediately visible during Phase 3 testing.

2. **M1 (welding callback):** Design the callback interface before Phase 3 starts. The state machine needs to distinguish "arm relay legitimately closed" from "arm relay contacts welded shut." Suggest adding `arm_sense_register_fault_cb()` or changing the existing callback signature to include a fault flag.

The other findings (M2 self-test duplication, m1 TWDT, m2 sequence) are improvements that can be deferred to the hardening phase or addressed alongside Phase 3 work.

---

*End of Phase 2 Code Review — RLC-REVIEW-P2-001*
