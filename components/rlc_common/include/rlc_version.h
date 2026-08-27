/**
 * RLC Firmware Version
 *
 * Both units must match on all three components (MAJOR.MINOR.PATCH)
 * to establish a link.
 */

#pragma once

/* 1.1.20 (2026-08-27): bug #20 — crypto keys rotated and taken out of the repo.
 *
 * The ESP-NOW PMK/LMK and the CRC integrity pre-shared key were defined in
 * rlc_config.h as literal ASCII placeholders — "RLC_PMK_DEFAULT!",
 * "RLC_LMK_DEFAULT!", "RLC_CRC_INTEGRIT" — and committed to a PUBLIC
 * repository. Two of the three link-security layers therefore offered no
 * protection against anyone who had read the source, and the values were
 * guessable even without reading it. Only the replay protection, whose session
 * token is random per link-up, ever held.
 *
 * Keys now live in rlc_secrets.h: gitignored, mode 600, generated locally by
 * ./tools/gen-secrets.sh from /dev/urandom. rlc_config.h has NO fallback — a
 * build without real keys fails with an instructive #error rather than quietly
 * linking a default, because a silent fallback is exactly how the placeholders
 * survived to ship.
 *
 * Leak prevention is enforced, not merely intended: tools/git-hooks/pre-commit
 * refuses any commit that stages rlc_secrets.h under any path, or that defines
 * a key macro with non-zero bytes in any file. Install with
 * `git config core.hooksPath tools/git-hooks`. Verified by attempting both
 * leaks — a `git add -f` of the real file, and a key macro pasted into an
 * unrelated tracked file — and confirming both were refused.
 *
 * THE OLD KEYS REMAIN PERMANENTLY PUBLIC. They are in git history across many
 * commits on a public repo, very likely already cloned, forked and cached.
 * Rewriting history would not reliably retract them. Rotation does not
 * un-publish the old values; it makes them irrelevant. Never reuse them.
 *
 * Both units must be flashed from the SAME tree. With mismatched keys ESP-NOW
 * decryption fails before the firmware version check runs, so a half-flashed
 * pair does not refuse to link with a diagnostic — it simply goes silent.
 *
 * 1.1.19 (2026-08-27): the arming sequence must be walked in order, and says
 * so when it is not.
 *
 * Raised by the operator during T-S04/T-S08. Both tests PASSED — a fire button
 * held through ARMED entry cannot fire, because authorisation needs a
 * 0xFF->0x00 transition *after* entry — but the refusals were silent. A press
 * that does nothing and says nothing is indistinguishable from a dead button,
 * and the natural response to apparent non-response is to try again, which is
 * the wrong instinct at a pad. This is the third place §7.2.9a was never
 * applied, after commands (1.1.6) and the link handshake (1.1.17).
 *
 *   FIRE pressed in IDLE          was a bare "ignored" comment; now beeps and
 *                                 toasts "NOT ARMED - ARM FIRST"
 *   ARM attempted with fire held  now REFUSED outright, "RELEASE FIRE BUTTON
 *                                 FIRST". Arming into a state where the
 *                                 operator is already pressing fire and
 *                                 nothing happens is the confusing case: the
 *                                 most obvious input in the most critical
 *                                 state, silently inert. Refusing is honest.
 *   arm switch ON with an input   was not handled at all in IDLE; now toasts
 *   already held                  "RELEASE FIRE BUTTON FIRST" / "RELEASE
 *                                 ENCODER FIRST"
 *
 * The order enforced is: arm key ON -> encoder held then released -> fire
 * held. The arm switch is deliberately NOT forced off on a bad sequence — it
 * is a physical switch the firmware cannot move, and pretending otherwise
 * would put the display out of step with the panel. The refusal that carries
 * the safety weight is on the ARM.
 *
 * Adds encoder_button_is_pressed() so the FSM can see a held encoder; the
 * state was already tracked, just not exposed.
 *
 * Remote-only, but the version check is strict — flash both units.
 *
 * 1.1.18 (2026-08-27): a mismatch also reaches a remote on OLD firmware.
 *
 * 1.1.17's LINK_REJECT only helps once both units carry it: an older remote
 * has no handler for message type 0x03 and drops it at the dispatch switch's
 * default case. Since a version mismatch means one unit IS on older firmware,
 * that left the case it was written for uncovered.
 *
 * On a mismatch the base now also sends a LINK_ACK carrying its version, with
 * the session token zeroed. Every version of handle_link_ack() has checked the
 * peer version before touching anything else, so an old remote latches
 * VERSION_MISMATCH and shows the §10.2.1 screen from that. No reset_session(),
 * no LINKED — the base's own lock-out is unchanged and no session is created.
 *
 * Both frames go out: new remotes latch on the REJECT (its log names the
 * reason), old ones on the ACK. Whichever lands first wins; the dispatch guard
 * on VERSION_MISMATCH drops the second.
 *
 * 1.1.17 (2026-08-27): the base says WHY it refused a handshake.
 *
 * PROTOCOL CHANGE — new MSG_LINK_REJECT (0x03). Both units must be flashed
 * together, which the strict version check already enforces.
 *
 * handle_link_request() refused a handshake with a bare `return` on two
 * paths: a firmware mismatch, and the app-state guard when the base is armed
 * or firing. The remote cannot tell a refusal from a base that is switched
 * off or out of range, so it retried every 2 s forever behind a splash frozen
 * at "Attempt 5 / 5" with the progress bar at 100% — which reads as a hung
 * boot, not a diagnosis. Meanwhile the base knew exactly what was wrong and
 * said so only on its own LED strip, 200 m away at the pad. Both paths
 * contradicted the no-silent-refusals rule (§7.2.9a), which had been applied
 * to commands in 1.1.6 but never to the handshake.
 *
 * The base now sends LINK_REJECT with a reason code and its own version:
 *   LINK_REJECT_VERSION_MISMATCH  terminal; the remote latches
 *                                 VERSION_MISMATCH and the §10.2.1 mismatch
 *                                 screen can finally render, naming both
 *                                 versions
 *   LINK_REJECT_BUSY              not terminal; the remote keeps retrying but
 *                                 the splash now says "Base busy - armed or
 *                                 firing" instead of counting in silence
 *
 * Note on the mismatch case specifically: the remote has always had its own
 * check in handle_link_ack(), but it reads the version out of a LINK_ACK the
 * base never sent on a mismatch, so that path — and the screen behind it —
 * was unreachable. The base-side check added in 5.7 to make mismatches
 * *clearer* is what pre-empted it.
 *
 * rlc_link_status_t gains last_reject so the display can surface the reason.
 *
 * 1.1.16 (2026-08-27): no more false RELAY WELDED on a normal disarm.
 *
 * Measured on target: the status band flashed RELAY WELDED for 180 ms and
 * 220 ms across two ordinary disarms. On ARMED -> IDLE the base reports
 * base_state = IDLE before base_arm_sense has fallen (relay release plus
 * debounce), so rlc_base_arm_state() sees the sense HIGH with the FSM outside
 * the firing path — exactly its weld condition. Pre-existing: the main
 * screen's BASE field had been flashing WELD! the same way, but the band turned
 * it into a full-width colour flash, which is what made it worth fixing. An
 * indicator that cries wolf twice a session teaches the operator to ignore it.
 *
 * A weld must now hold for 500 ms before it is believed. During the window the
 * state is reported as ARMED, never anything safer: the arm sense IS high, so
 * on a disarm that is the literal truth (the relay is still releasing) and on
 * a real weld it is the conservative reading. 500 ms still beats the base's
 * own weld confirm count, which is what the early check exists to beat.
 *
 * The hysteresis is in the display, not in rlc_base_arm_state(): that function
 * is pure, shared with the base, and compiled into the host tests
 * (T-M01..T-M07). The same settled state now also feeds the main screen's BASE
 * field, which additionally fixes it reading SAFE on a dead link.
 *
 * Adds the remote fault-injection console (CONFIG_RLC_REMOTE_FAULT_INJECTION,
 * ./build_remote.sh --inject) — keys d and b latch DISPLAY FAULT and REMOTE
 * BATTERY CRITICAL. The REMOTE FAULT band state is latched by only four
 * conditions, none reachable from the base harness or from the air; a
 * wrong-channel ARM ACK does NOT latch it (the remote toasts and reconciles by
 * disarming, which is better behaviour but left the path untested).
 *
 * Remote-only, but the version check is strict — flash both units.
 *
 * 1.1.15 (2026-08-27): status band no longer claims SAFE on a dead link.
 *
 * Observed on target: cutting power to the base left the band GREEN while the
 * LINK LOST screen was up. Link loss is declared at 1500 ms, but a
 * STATUS_UPDATE is only stale after 4000 ms (2 x STATUS_UPDATE_INTERVAL_MS),
 * so for 2.5 s the band kept rendering the last state received before the
 * power was cut. The band now gates on link state as well as staleness: the
 * link being down is itself proof the base state is unknown, whatever the age
 * of the last packet says.
 *
 * Fault states added, all red, named in the band:
 *   BASE FAULT    base reports any error_flags, or base_state == STATE_ERROR
 *   REMOTE FAULT  the remote has latched its own ERROR
 * Red rather than a softer colour because a base that has faulted cannot be
 * trusted to have reported its relay state accurately either, so treating it
 * as possibly live is the honest reading. Priority runs WELD > RELAY LIVE >
 * REMOTE FAULT > UNKNOWN > BASE FAULT > keys > SAFE.
 *
 * The band now logs one line per state transition. It is a safety indicator
 * whose only check was previously to look at the panel, which is how the
 * green-on-dead-link case survived; it is now verifiable from a capture.
 *
 * ARMED screen: "ARM SENSE OK" -> "BASE ARM SENSE OK", so the line names which
 * unit the sense belongs to (36 of 40 columns at the scale-2 floor).
 *
 * Remote-only, but the version check is strict — flash both units.
 *
 * 1.1.14 (2026-08-27): the one-key / both-keys distinction made visible.
 *
 * 1.1.13 used C_WARN (0xFFDC00, 87% green) for one key and 0xFF6000 (38%) for
 * both. Clear separation on paper; on the actual panel both read as orange and
 * the distinction was invisible. Pushed to the extremes the display can put
 * between red and green — 0xFFFF00 (100% green) against 0xFF5000 (31%) — with
 * dedicated band constants so tuning this cannot drag the warning-text colour
 * with it.
 *
 * Hue is no longer the only carrier of the distinction. The main screen's
 * instruction line tested ONLY the remote arm switch, so with the remote armed
 * and the base key still in SAFE it read "HOLD ENCODER TO ARM CH n" — an
 * instruction the base refuses, since arming needs its key too. It now names
 * the step actually outstanding for each of the four combinations:
 *   neither      "TURN ARM KEY TO ARM CH n"
 *   remote only  "TURN BASE KEY TO ARM CH n"
 *   base only    "FLIP REMOTE ARM SWITCH"
 *   both         "HOLD ENCODER TO ARM CH n"
 *
 * Remote-only, but the version check is strict — flash both units.
 *
 * 1.1.13 (2026-08-27): the status band separates one key turned from two.
 *
 * A single amber for "either key armed" hid the only transition in the arming
 * sequence where the risk actually changes. With one key turned the hardware
 * will not act: the base refuses an ARM without its key, and the remote will
 * not send one without its arm switch. With both turned, a single long-press
 * closes the arm relay and puts VBAT on the fire path.
 *
 *   YELLOW  one key   — "BASE KEY ARMED" / "REMOTE ARMED", naming the end
 *                        that is live so the operator knows which remains
 *   ORANGE  both keys — "READY TO ARM" (not "ready to fire": the next step
 *                        arms the relay, it does not fire)
 *
 * Remote-only, but the version check is strict — flash both units.
 *
 * 1.1.12 (2026-08-27): system status band, and a background-fill fix.
 *
 * A coloured field across the bottom of every screen reports the state of the
 * fire path at a glance: GREEN base safe and remote arm switch off, AMBER
 * either key turned with the path still dead, RED the arm relay engaged,
 * flashing red/amber on a welded relay, GREY whenever the state is not known.
 * Grey rather than green for unknown, per the §10.2.2 rule that unknown is
 * never shown as SAFE — green is a positive claim the pad is safe to approach.
 * It carries the status in words as well as colour, matching the rule that hue
 * is never the only carrier of meaning here.
 *
 * The band occupies the area that already held the status and instruction
 * lines, leaving the channel grid untouched: that grid fills the panel width
 * exactly (_Static_assert in rlc_display.c) and had no room to give, so a
 * border would have had to shrink the cells.
 *
 * Fixed alongside: draw_text_centred_bg() cleared the full panel width before
 * writing, so every refresh of a live value punched a notch through the left
 * and right edges of whatever frame the text sat in — visible on the LINK LOST
 * amber border and on the ARMED / FIRING / FIRE COMPLETE box outlines. The fill
 * now takes explicit bounds and callers pass the interior of their enclosure.
 *
 * base_arm_colour() is removed: the band carries that mapping now.
 * Remote-only, but the version check is strict — flash both units.
 *
 * 1.1.11 (2026-08-27): stock build — the T-D09 display profiling harness
 * (CONFIG_RLC_DISPLAY_PROFILE, ./build_remote.sh --profile) is removed now that
 * the measurements are taken. No functional change: the harness was passive and
 * off by default, so 1.1.11 renders identically to 1.1.10. Recover it from git
 * history at 1.1.10 if the display refresh ever needs re-measuring — the
 * numbers in Test_Report_Phase4_Display.md §6 were taken with it and cannot be
 * reproduced on a stock build.
 *
 * 1.1.10 (2026-08-27): display refresh fix — FSD test T-D09 failed on target
 * at 3.3 Hz against the §10.3 >=5 Hz floor, and the pre-fire countdown stepped
 * at ~300 ms rather than the specified 100 ms.
 *
 * Three causes, only the first of which the code review had identified:
 *   - one dirty bounding box, unioned over updates from the top bar at y=0 to
 *     the instruction line at y=DH-30, so it spanned the whole panel;
 *   - draw_field() repaints every field every frame whether or not its text
 *     changed, so the pixels genuinely were all being rewritten and a rect
 *     list alone would not have helped;
 *   - vTaskDelay ran *after* the frame's work, making the period work+100 ms,
 *     so 100 ms was unreachable even with an instantaneous flush.
 *
 * flush() now diffs the dirty box row by row against a shadow copy of what the
 * panel was last sent (second 460800-byte PSRAM buffer) and transmits only the
 * changed spans, coalescing consecutive changed rows into runs; the frame loop
 * is paced with xTaskDelayUntil, re-basing on overrun. Diffing rather than
 * per-field invalidation is deliberate: a missed invalidation leaves a stale
 * pixel, and this display shows ARMED.
 *
 * Retested 100.00 ms / 10.0 Hz steady, 101 ms during PRE_FIRE, ~1200 px sent
 * per frame against a 153600 px panel. Remote-only change, but the version
 * check is strict on all three components — flash both units.
 *
 * Also adds CONFIG_RLC_DISPLAY_PROFILE / ./build_remote.sh --profile, the
 * test-only instrumentation T-D09 needs (passive; off by default).
 *
 * 1.1.9 (2026-08-27): full-codebase review fix round (RLC-REVIEW-ALL-008).
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
#define RLC_VERSION_PATCH  20
#define RLC_VERSION_STRING "1.1.20"
