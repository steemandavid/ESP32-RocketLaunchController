/**
 * RLC Firmware Version
 *
 * Both units must match on all three components (MAJOR.MINOR.PATCH)
 * to establish a link.
 */

#pragma once

/* 1.1.30 (2026-08-28): Phase 5 review fixes (RLC-REVIEW-ALL-009) — the
 * operator-information layer on the fire path.
 *
 * CRIT-01: buzzer_set_background() ended with a buzzer_play(BUZZER_OFF)
 * "nudge", and buzzer_play() is an atomic overwrite of a one-deep mailbox. The
 * FSM tick sets the background from the state, so any handler that beeped AND
 * left ARMED/PRE_FIRE/FIRING had its pattern destroyed microseconds later: the
 * link-lost alarm, the critical-error alarm, and every FIRE-guard refusal were
 * silent exactly when they originated from an armed state. The player task now
 * polls the background between pattern slices; nothing is queued for it.
 *
 * MAJ-01: the remote's FIRING status handler was a whitelist (POST_FIRE/IDLE),
 * so a base that latched terminal ERROR mid-pulse left the remote asserting
 * "IGNITION ACTIVE" indefinitely. Now a blacklist, with ERROR/LINK_LOST named
 * as base faults rather than reported as a cut-short pulse.
 *
 * MAJ-02: FIRE COMPLETE required only the remote's own elapsed time, which
 * keeps running through a base-side abort — one lost STATUS_UPDATE could
 * certify a channel that never carried current. Completion (and "cut short")
 * now need a status that actually showed the base in FIRING.
 *
 * MAJ-03: NACKs answering the fire repeats are heeded in PRE_FIRE/FIRING —
 * base-abort detection in ~200 ms instead of up to 2 s.
 *
 * MAJ-04/05: screen precedence — the boot splash and the FIRE COMPLETE hold no
 * longer cover a live (ARMED/PRE_FIRE/FIRING) or alarmed (LINK_LOST) state.
 *
 * MAJ-06 and the minors: missing beeps on three refusal paths, buzzer up
 * before the boot display check, FIRE ACK channel mismatch and key-off ARM
 * abort named instead of blamed on the link, "LINK WEAK" in the top bar, no
 * continuity glyph for an unknown igniter band, first-ARM-wins during the
 * verify window, EVT_LINK_RECOVERED handled in base FIRING, input events
 * posted with a 10 ms block instead of a zero timeout.
 *
 * The three minors the review left to an operator decision were settled the
 * same day and are in this version too:
 *
 * MIN-02: the arm-verify timeout is a refusal on the first strike (NACK 0x0B,
 * stay IDLE, retryable — the 200 ms window leaves only ~40 ms over the sense
 * debounce, so a slow relay is not necessarily a broken one) and a latched
 * ERR_RELAY_FAULT + terminal ERROR on the second consecutive one. Cleared by
 * any successful verify. Weld detection is unchanged: terminal on sight.
 *
 * MIN-04: a validated command dropped because the FSM queue was full now gets
 * NACK 0x0F (BASE_BUSY) instead of only a log line — the last silent refusal
 * on a safety path. The remote deliberately ignores it for repeated CMD_FIRE:
 * one refused frame is not the base leaving the firing path, and aborting a
 * live pulse on it would be a false abort.
 *
 * MIN-12: no code change — FSD §8.2.3/§8.2.4 now describe the ping-failure
 * *rate* test the firmware has always used, and record that the base's own
 * dead-man and 1 s contact-freshness guards are what actually gate ignition.
 *
 * Remote and base both changed; flash them together. New NACK code, so a
 * mismatched pair would also disagree about 0x0F.
 *
 * 1.1.29 (2026-08-27): the dead-man can no longer be defeated by mashing.
 *
 * SAFETY DEFECT found in edge-case testing (Phase 5 task 5). Rapidly mashing
 * the fire button FIRED THE CHANNEL.
 *
 * The fire button used symmetric 8-bit debouncing at a 10 ms poll, so a release
 * was only reported after 80 ms of continuous release. Mash faster than that and
 * the shift register never reaches all-high: no release is ever reported, the
 * FSM sees a continuous hold, CMD_FIRE repeats keep flowing, and BOTH dead-man
 * layers stay satisfied — the remote's release detection and the base's
 * FIRE_AUTHORIZATION_TIMEOUT_MS both sit downstream of that one decision, so
 * neither can catch it. Captured: PRE_FIRE at 655397, full countdown, FIRING at
 * 660437, pulse delivered.
 *
 * This is not only about deliberate mashing. A worn or chattering switch
 * contact produces the identical signal, as would a shaking hand — the operator
 * would believe they were not holding the button while the system fired. The FSD
 * premise is "releasing the button at any point cuts current — a dead-man
 * switch, not a latch".
 *
 * THE UNDERLYING ERROR was symmetry. For a dead-man the two directions have
 * opposite consequences: a missed release fires an igniter the operator has let
 * go of, while a spurious release only aborts, which is the direction that cuts
 * current. Demanding the same evidence for both makes the system exactly as
 * reluctant to stop as to start.
 *
 * New opt-in rlc_debounce_set_fast_release(). The fire button keeps 8 samples
 * (80 ms) to register a PRESS, so noise cannot start a sequence, and needs 2
 * (20 ms) to register a RELEASE. 20 ms sits in a real gap: switch bounce is
 * 1-10 ms (rejected), a human release is 30-80 ms (caught). One sample would
 * report bounce as release.
 *
 * Faster polling was considered and rejected: it narrows the blind window
 * without closing it, and an 8-sample window at 1 ms is 8 ms — inside typical
 * bounce duration — so it would erode the bounce rejection that debouncing
 * exists for, in both directions.
 *
 * Opt-in, so continuity, key sense and arm sense keep symmetric debouncing,
 * which is correct for a sensor. Pinned by tests/host/test_debounce.c T-D07 and
 * T-D08, verified to FAIL against the old symmetric behaviour.
 *
 * Remote-only, but the version check is strict — flash both units.
 *
 * 1.1.28 (2026-08-27): boot display health check actually checks.
 *
 * Found while working out what a fault-injection harness could substitute for
 * FSD T-S10 (disconnect display MOSI at boot), which cannot be run on this
 * hardware without unsoldering a working display. Reading the code the test
 * targets found two defects a real MOSI break would have walked straight
 * through — which is a better outcome than running the test would have been.
 *
 * 1. The boot read DISCARDED the SPI transaction status. §5.5.6 requires it to
 *    be checked — "a health check that succeeds only because the SPI layer
 *    swallowed an error is not a health check". The periodic check has done so
 *    since 1.1.9; the boot read never did. Now snapshots s_spi_errors around
 *    the read, the same pattern the periodic check uses.
 *
 * 2. The test was `s_panel_id != 0`. A broken MOSI leaves the panel with no
 *    command to answer and MISO undriven: that reads 0x00000000 (caught) or
 *    floats to 0xFFFFFFFF (NOT caught), so the remote would boot believing a
 *    dead panel healthy — and every screen after that is a lie, including
 *    ARMED. Both undriven signatures are now rejected.
 *
 * §5.5.6 contradicted itself and the firmware implemented the weaker clause:
 * "any non-zero read-back is considered valid" against "only a zero or GARBAGE
 * read-back ... is treated as a fault", when all-ones is both garbage and
 * non-zero. Spec corrected to require rejecting both signatures and checking
 * the SPI status. A real panel, including this hardware's 0x2A403300 clone,
 * reports neither.
 *
 * Remote-only, but the version check is strict — flash both units.
 *
 * 1.1.27 (2026-08-27): audible state tones for ARMED and the firing sequence.
 *
 * Operator: ARMED and FIRING had no sound on the remote at all — only the
 * display and the ring LED. The base siren covers the pad; this covers the
 * operator, who may not be looking at the panel.
 *
 *   BUZZER_ALARM_ARMED    80on/1120off  (~0.8 Hz)  pad live, standing by
 *   BUZZER_ALARM_FIRING   90on/160off   (~4 Hz)    sequence running
 *
 * The tempo gap is the point: the step into the firing sequence is
 * unmistakable by ear without looking. ARMED is deliberately sparse rather
 * than urgent — it may run the full 10 s arm window, and it must not read like
 * ALARM_CRITICAL or ALARM_LINK_LOST, which are both 2.5 Hz fault patterns.
 * PRE_FIRE shares the firing tone: the countdown is the part that must be
 * heard starting, and it is where an abort is still free.
 *
 * Needed a new mechanism, not just two patterns. A repeating pattern plays
 * until the next buzzer_play() replaces it, and ARMED is full of one-shots —
 * the arm-confirm double beep, and a triple from every FIRE guard refusal — so
 * a plain tone would be killed by the first refusal and never return. New
 * buzzer_set_background(): the player re-enters it whenever nothing else is
 * sounding. Idempotent, so it is driven from the FSM tick rather than on
 * transitions — the same reason fire_button_set_live() is, since the base
 * dropping out underneath an ARMED remote arrives as a STATUS_UPDATE, not as a
 * local state change, and a missed transition would leave the remote sounding
 * armed when it is not.
 *
 * buzzer_stop() clears the background as well: a stop that leaves a tone to
 * resume a moment later is not a stop.
 *
 * 1.1.26 (2026-08-27): remove the slack that let 1.1.25 still miss a base-side
 * cut.
 *
 * 1.1.25 allowed 200 ms of clock-skew slack on the elapsed-time test, so a
 * pulse cut at 802 ms of 1000 was classified as complete — the operator turned
 * the pad key two milliseconds inside the margin and got no toast. Retest
 * caught it.
 *
 * The slack is removed. The skew it guarded against was also unfounded in the
 * wrong direction: a measured completion read 1105 ms on the remote's clock,
 * over rather than under. And POST_FIRE turns out to be authoritative anyway —
 * rlc_base_fsm.c calls status_update_trigger() on entering it, so a completed
 * pulse always pushes a STATUS_UPDATE saying POST_FIRE instead of waiting for
 * the 2 s poll. The observed completion was detected that way (base_state=6),
 * never reaching the elapsed-time branch at all.
 *
 * Elapsed time is now purely a backstop for that one packet being lost over
 * the air, at the full FIRE_PULSE_DURATION_MS with no margin — which is also
 * true on its own terms: a pulse cut at or after 1000 ms had already delivered
 * its whole duration.
 *
 * 1.1.25 (2026-08-27): the remote no longer claims FIRE COMPLETE for a pulse
 * the base cut short.
 *
 * Reported from the bench while testing the 1.1.24 toasts: turning the BASE key
 * to SAFE during a pulse produced no cease-fire toast. Investigating it found
 * something worse than the missing toast.
 *
 * A COMPLETED pulse runs FIRING -> POST_FIRE -> IDLE. A pulse the BASE cuts
 * short — pad key to SAFE, arm sense lost, continuity lost — goes FIRING ->
 * IDLE directly. The remote saw both as base_state == STATE_IDLE and announced
 * "Fire complete detected" for either, putting the FIRE COMPLETE screen up over
 * an interrupted shot. Captured: base key off at 550 ms of a 1000 ms pulse,
 * remote displayed FIRE COMPLETE. Claiming a shot completed when it did not is
 * worse than saying nothing, and since 1.1.23 that screen also carries an
 * igniter-status line, so it was pairing a false headline with a real reading.
 *
 * The remote now timestamps its own entry to FIRING and compares elapsed time
 * against FIRE_PULSE_DURATION_MS, with 200 ms of slack for clock skew between
 * the two units' independent countdowns. POST_FIRE is still accepted as a
 * positive confirmation when it arrives, but is NOT relied on alone:
 * STATUS_UPDATE_INTERVAL_MS and POST_FIRE_COOLDOWN_MS are both 2000 ms, so the
 * remote can miss the POST_FIRE window entirely and see only IDLE. Local
 * elapsed time needs no packet to land in a particular window.
 *
 * A base-side cut now toasts with the attention beep, naming the pad key when
 * the status reports it off — the common cause, and the operator needs to know
 * the pad end acted rather than the remote:
 *
 *   key reported off   "CH n CUT SHORT - BASE KEY"
 *   otherwise          "CH n CUT SHORT AT BASE"
 *
 * 1.1.24 (2026-08-27): a cease-fire tells the operator the channel was live.
 *
 * Operator report: releasing the fire button during the pulse dropped the
 * remote back to the idle screen with no notification. Both cease-fire paths
 * in STATE_FIRING — button released, and arm switch off — logged and returned
 * to IDLE silently.
 *
 * That loses the fact that matters most when someone then walks out to the
 * rail: the channel WAS energised, just for less than the full pulse. A silent
 * return is indistinguishable from an abort during the pre-fire countdown,
 * where no current ever reached the igniter, and the two call for very
 * different behaviour at the pad.
 *
 *   button released   "CH n PULSE CUT SHORT"
 *   arm switch off    "CH n CUT SHORT - ARM OFF"
 *
 * Named separately so the operator knows which input ended it, both with the
 * attention beep. The wording states what happened without asserting what it
 * means — whether the igniter took is not knowable from the remote, and the
 * continuity grid answers that live as soon as the toast clears.
 *
 * Not a §7.2.9a violation: that requirement covers refusals, aborts and
 * failures, and a cease-fire is a successful operator action. The v1.39 audit
 * finding "only five log-without-display sites, all legitimate" was correct by
 * its own terms. The gap was in a neighbouring category — operator-initiated
 * state changes whose consequences the operator needs to know about. FSD
 * §8.2.6 updated.
 *
 * 1.1.23 (2026-08-27): FIRE COMPLETE screen holds for 10 s.
 *
 * Operator: 5 s still was not long enough to read the igniter status and act
 * on it. FIRE_COMPLETE_SCREEN_MS 5000 -> 10000. POST_FIRE_COOLDOWN_MS is
 * untouched at 2000 — the base still accepts another arm two seconds after a
 * shot, and the screen is cancelled the instant the FSM re-arms, so the longer
 * hold costs nothing operationally and never delays the next shot.
 *
 * 1.1.22 (2026-08-27): FIRE COMPLETE screen holds for 5 s, decoupled from the
 * base's post-fire cooldown.
 *
 * Operator request: 2 s was too brief to read. The screen duration was
 * POST_FIRE_COOLDOWN_MS, which is ALSO the fire-path constant governing how
 * long the base sits in POST_FIRE before accepting another arm — so simply
 * raising it would have extended the base's cooldown by 3 s as a side effect.
 * Fire-path constants get changed by explicit decision here, not incidentally.
 * New display-only FIRE_COMPLETE_SCREEN_MS (5000); POST_FIRE_COOLDOWN_MS stays
 * at 2000 and the base FSM is untouched.
 *
 * Decoupling them opened a hazard that is closed in the same change. The
 * fire_done branch outranks the FSM-derived screen, which was harmless while
 * both ended at 2000 ms. With a 5 s screen the operator can re-arm while it is
 * still up, and the display would have shown FIRE COMPLETE over a live ARMED
 * state. It is now cancelled the moment the FSM enters ARMED, PRE_FIRE or
 * FIRING: a summary of the last shot must never cover a live pad.
 *
 * The countdown is relabelled "CLEARS IN" from "IDLE IN". After ~2 s the base
 * really is IDLE, so an "IDLE IN 2.6s" countdown would have been stating
 * something untrue; the screen can only promise when it will clear itself.
 * FSD §10.2.4a updated to match: new duration, the early-cancel rule, the
 * "CLEARS IN" wording, and the igniter status line below.
 *
 * The screen also now shows the fired channel's continuity band continuously,
 * with the same shape-plus-colour coding as the channel grid: OPEN "LIKELY
 * FIRED" (green), MARGINAL "CHECK" (yellow), CONNECTED "STILL CONNECTED"
 * (red), or "IGNITER ?" when the status is not fresh. A good igniter burns
 * through, so OPEN is the expected outcome and the operator's first evidence
 * the shot took; still CONNECTED usually means it did not fire. The wording
 * describes the measurement rather than delivering a verdict — OPEN cannot
 * distinguish a burned igniter from a lead that fell off, which is why it says
 * LIKELY. This is the operator-facing half of T-S19.
 *
 * Remote-only, but the version check is strict — flash both units.
 *
 * 1.1.21 (2026-08-27): status band only where it carries information.
 *
 * Operator report: the band covered the splash progress bar and the LINK LOST
 * reconnect text. It was drawn on every screen, which was the wrong default.
 *
 * Removed from SPLASH, LINK_LOST and FW_MISMATCH. On the latter two the
 * removal is not a judgement call: system_status() gates on link state, and on
 * both screens the link is by definition not LINKED, so the band could only
 * ever return SYS_UNKNOWN. It was grey every single time. A field that can
 * show exactly one value carries no information, and it was displacing text
 * that does. On the splash the operator has not begun a sequence, so it
 * answers a question nobody is asking — and it sat on the progress bar, the
 * one thing that screen exists to show.
 *
 * KEPT on MAIN, ARMED, FIRING, FIRE_COMPLETE and ERROR. ERROR was checked
 * against the same test and passes it: SYS_WELD and SYS_RELAY_LIVE outrank
 * SYS_REMOTE_FAULT, so on a latched remote error the band still reports
 * whether the pad is live — which is the thing most worth knowing when the
 * remote itself has failed.
 *
 * Original layouts restored on the three screens: splash progress bar and
 * credit back on black, LINK LOST reconnect text back to y=250/288, firmware
 * mismatch text back to y=226/250.
 *
 * Remote-only, but the version check is strict — flash both units.
 *
 * 1.1.20 (2026-08-27): bug #20 — crypto keys rotated and taken out of the repo.
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
#define RLC_VERSION_PATCH  30
#define RLC_VERSION_STRING "1.1.30"
