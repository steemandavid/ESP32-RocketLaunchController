/**
 * RLC Firmware Version
 *
 * Both units must match on all three components (MAJOR.MINOR.PATCH)
 * to establish a link.
 */

#pragma once

/* 1.1.9 (2026-08-27): full-codebase review fix round (RLC-REVIEW-ALL-008).
 *
 * CRITICAL — BF-01: the fire GPTimer was never stopped on the *successful*
 * pulse-completion path. An expired one-shot alarm disables the alarm but
 * leaves the driver in RUN state, so the second fire_timer_start() of a power
 * cycle hit ESP_ERROR_CHECK -> abort() -> panic-reboot with the arm relay and
 * the channel relay still energised. The igniter carried full current for the
 * whole panic+reboot interval. Fixed three ways: stop on completion, stop
 * defensively at the top of every start, and a checked return that makes the
 * hardware safe and latches ERROR instead of aborting.
 *
 * Also on the fire path: BF-02 (PRE_FIRE heartbeat-freshness is now its own
 * guard routing to LINK_LOST, no longer folded into the 30% failure-rate
 * check), BF-03 (SIREN_CONTINUITY_LOST now sounds on a continuity-loss
 * disarm), BF-04/CI-05 (boot failures latch a halt with siren and LED instead
 * of returning from app_main), BF-07 (FSM queue created before the arm-sense
 * task, so a weld present at power-on is not dropped).
 *
 * Comms: CM-01 (rlc_link_send_status_update ran unlocked against link_task —
 * duplicate sequence numbers), CM-02 (replay/CRC refusals now NACK per App
 * D.3 instead of dropping silently), CM-03 (STATUS_UPDATE data-gap
 * detection), CM-04 (truncated ACK/NACK dropped, not forwarded zeroed),
 * CM-05 (seq 0 rejected for commands), CM-06 (unused espressif/esp-now
 * dependency removed).
 *
 * Remote: DS-01 (FSD §5.5.6 runtime display health check — 5 s panel-ID
 * re-read; failure while armed disarms and latches ERROR), RM-01/02/03/05/06/
 * 07/09/11, DS-02/03. Buzzer task moved to priority 1 / core 1 per §9.10.
 *
 * Both units are affected and the version check is strict — flash together.
 */
/* 1.1.1 (2026-08-21): post-review fix round — arm-key state adopted at boot
 * (N1), siren stale-callback race (N2), and 11 minors. Arm-path behaviour
 * changed on BOTH units, so the bump is deliberate: the strict version check
 * makes a half-flashed pair refuse to link rather than run mismatched safety
 * logic. Flash base and remote together. */
/* 1.1.8 (2026-08-26): bug #30 — the continuity-loss disarm was edge-triggered
 * with no level-triggered backstop, so an igniter going OPEN inside the
 * arm-verify window (FSM still in STATE_IDLE, which does not handle the event)
 * left the base ARMED on an open igniter with no edge left to report it. Two
 * fixes: refuse the ARM at verify completion if the band has gone OPEN, and a
 * periodic level check in check_timers() covering ARMED/PRE_FIRE that also
 * catches an event dropped by a full FSM queue. Base-only.
 *
 * 1.1.7 (2026-08-26): fire button ring LED reports state, not the button.
 * It had driven red-while-held / green-while-released since Phase 2, so it
 * showed the operator's finger rather than whether a press would do anything.
 * Red now means the button is live: remote ARMED/PRE_FIRE/FIRING AND a fresh
 * STATUS_UPDATE confirming the same channel armed at the base. Remote-only.
 *
 * 1.1.6 (2026-08-26): no silent refusals left. The base now ANSWERS commands
 * while in ERROR with the new NACK_BASE_ERROR (0x0E) instead of discarding
 * them — a timeout carried no reason, so an operator could not tell a dead
 * link from a base needing a power cycle. The remote names the specific fault
 * from the error_flags it already caches, so the NACK payload is unchanged.
 * Every remaining operator-facing branch that logged a refusal without saying
 * so now beeps and toasts: the whole FIRE guard family (arm key off, base not
 * armed, stale status, degraded link, send failure, key-off-after-ACK,
 * no-response), the ARM send/retry failures and cancellation, base/remote
 * state mismatch, stale-status timeout, and base-ended-sequence.
 * New NACK code = both units must be flashed together.
 *
 * 1.1.5 (2026-08-26): remote now displays "CHANNEL MISMATCH ERROR" when the
 * base ACKs an ARM for a channel the operator did not select. The disarm and
 * the triple beep were already correct; only the message was missing. Found by
 * T-A13 once the fault-injection harness could produce a malformed ACK.
 *
 * 1.1.4 (2026-08-26): remote no longer fails silently when an ARM cannot be
 * granted. New guard refuses locally (naming the flag) when the cached status
 * shows the base in ERROR — the base's ERROR handler is inert and NACKs
 * nothing, so the remote had been timing out into the one failure path that
 * gave no operator feedback. That timeout path now beeps and toasts too.
 * Remote-only, no protocol change.
 *
 * 1.1.3 (2026-08-26): PRE_FIRE_DELAY_MS 2000 -> 5000 by operator decision
 * after on-target testing (T-A17: 2 s was too short to act inside — an
 * igniter fired because the abort could not be made in time). Both units run
 * a countdown against this constant, so flash them together.
 *
 * 1.1.2 (2026-08-26): base siren sounds continuously from ARMED through
 * PRE_FIRE and FIRING (the 500 ms ARMED pulse fought the siren's own internal
 * modulation), and continuity loss on the armed channel now disarms the base
 * from ARMED or PRE_FIRE instead of being informational. Base-only changes,
 * no wire-protocol change — but the strict version check is on all three
 * components, so flash base and remote together or they will refuse to
 * link. */
#define RLC_VERSION_MAJOR  1
#define RLC_VERSION_MINOR  1
#define RLC_VERSION_PATCH  9
#define RLC_VERSION_STRING "1.1.9"
