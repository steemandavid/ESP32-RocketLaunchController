# Implementation Plan: Phase 3 Code Review Fixes (Round 2)

**Date:** 2026-04-15
**Status:** Completed

## Context

Phase 3 code review round 3 (merged into Phase3_Code_Review_002.md) downgraded the verdict to **FAIL** after finding a major safety regression (EVT_ARM_SENSE_FAULT handler not consumed in base FSM), three new major remote-side bugs, and a critical dead-code issue where EVT_BATTERY_CRITICAL was handled by both FSMs but never posted by either battery task. Plus an unrelated pre-existing IDF 5.5 callback signature in `rlc_espnow.c` that was blocking all builds on the installed IDF 5.4.1.

All 11 findings from the round-3 review were fixed, plus 1 pre-existing build blocker. Both firmware images now build clean on ESP-IDF v5.4.1.

## Files Modified

| File | Issues Fixed |
|------|-------------|
| `rlc_base_fsm.c` | J1 (EVT_ARM_SENSE_FAULT top-of-handler consumer, clears arm-verify pending, enters ERROR), J2 (`s_last_fire_cmd_ms` cleared in do_disarm / do_enter_link_lost / do_enter_error), J3 (fire_timer_stop + full state reset in do_enter_error), J5 (POST_FIRE idempotent CEASE_FIRE / DISARM ACK), J6 (seed `s_last_fire_cmd_ms` on ARMED→PRE_FIRE) |
| `rlc_base_main.c` | J4 (on_arm_fault_cb: blocking 10 ms xQueueSend so safety event cannot be dropped) |
| `rlc_link.c` | J4 (link safety event queue send switched to blocking 10 ms timeout) |
| `rlc_base_battery.c` | J7 (edge-triggered EVT_BATTERY_CRITICAL post to base FSM queue — handler was dead code before) |
| `rlc_remote_fsm.c` | R1 (WAIT_FOR_ACK_STATE_HANDLED sentinel so LINK_LOST / BATTERY_CRITICAL in wait_for_ack aren't stomped by do_disarm_and_idle), R2 (multi-arm detection via `__builtin_popcount` on IDLE/ARMED STATUS_UPDATE + ARMED FIRE precheck; broadcasts disarm, enters ERROR), N1 (PRE_FIRE and FIRING handle EVT_BATTERY_CRITICAL — send CEASE_FIRE then enter ERROR), N2 (dead stale-status check removed from ARMED EVT_STATUS_UPDATE), R4 (IDLE EVT_ENCODER_ROTATE now stores selected channel), R5 (fire-repeat race window comment) |
| `rlc_remote_battery.c` | R8 (edge-triggered EVT_BATTERY_CRITICAL post to remote FSM queue — handler was dead code before) |
| `rlc_espnow.c` | Pre-existing build blocker: `espnow_send_cb` signature changed from `const wifi_tx_info_t *` (IDF 5.5+) back to `const uint8_t *mac` so the project builds on the installed IDF 5.4.1 |

## Severity Breakdown (Round 3)

| Severity | Count | Fixed |
|----------|-------|-------|
| Major (safety/correctness) | 4 (J1, J7/R8, R1, R2) | 4 |
| Major (robustness) | 2 (J2, J3) | 2 |
| Minor (safety) | 1 (N1) | 1 |
| Minor (correctness/quality) | 4 (J4, J5, J6, N2, R4, R5) | 4 |
| Build blocker (pre-existing) | 1 | 1 |

## Build Verification

- Base: `idf.py -B build_base build` — **PASS**, rlc.bin = 0xc6ed0 bytes, 22% free in app partition.
- Remote: `./build_remote.sh` — **PASS**, `remote_app_main` symbol verified in binary.
- No new warnings introduced.

## Deviations from Plan

- J7 / R8 battery-critical posting was not in the original round-3 findings list — discovered while verifying reviewer-claimed dead code in FSM EVT_BATTERY_CRITICAL handlers. Added to the fix batch because leaving the dead handlers unused would violate FSD §7.3.3 / §8.3.4.
- Build verification succeeded this round after the `rlc_espnow.c` IDF 5.4.1 compatibility fix — superseding the "build not verified" note in the previous implementation plan.
