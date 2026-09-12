/**
 * RLC Firmware Version
 *
 * Both units must match on all three components (MAJOR.MINOR.PATCH)
 * to establish a link.
 */

#pragma once

/* 1.2.10 (2026-09-12): splash layout — the band gets margins, the version
 * gets off its own row, and the cut finally contains the landing.
 *
 * Three operator-driven corrections to 1.2.9, all cosmetic, no protocol
 * change. Flash both units.
 *
 *   1. THE CUT MISSED THE TOUCHDOWN. 1.2.9's crop panned to anchor 0.62
 *      settling at 65% of the clip, which put the band's lower edge at ~71%
 *      of frame height — and the pads sat right on that edge, so the boosters
 *      descended out of the bottom of the strip instead of visibly landing in
 *      it. The source camera tracks: the ground line runs ~63-65% of frame
 *      height at t=226-227 s and rises to ~55% by t=228 s as it zooms, so the
 *      band has to sit low enough for the earlier, lower ground line and
 *      still hold the later, higher one. Re-cut at 222.0 s with the pan
 *      0.50 -> 0.60 settling at 50% (the touchdown itself, rather than after
 *      it): the band now spans ~42-72% through the landing, with ground
 *      visible beneath the pads. Exact recipe in assets/README.md.
 *
 *   2. THE BAND HAD NO MARGINS. It butted straight against the club credit
 *      above and the "Connecting to base" line below, which read as a
 *      rendering fault rather than a frame — a photograph dropped into a text
 *      layout needs to be seen to have been placed there. VBAND_Y is 101, so
 *      there are now 15 blank rows above the band (the credit ends at y85)
 *      and 15 below it (the headline starts at y196). Both gaps are load
 *      bearing; anything moving the header, the band or the headline must
 *      keep them.
 *
 *   3. THE VERSION LEFT ITS OWN ROW, which is what paid for those margins.
 *      It now rides the copyright line at the foot of the screen —
 *      "(C) 2026 David Steeman  v1.2.10" — as the same kind of small print.
 *      It is emphatically still ON the boot screen: the strict version check
 *      makes "which firmware is this unit running" a question an operator has
 *      to be able to answer without a serial cable, and the firmware-mismatch
 *      screen it backstops is only reachable once a base answers.
 *
 * The fault-injection banner is keyed off VBAND_Y/VBAND_H now instead of
 * carrying its own literals, so it keeps occupying exactly the band's rows
 * when the band moves again.
 *
 * 1.2.9 (2026-09-12): the boot splash plays real footage, not a drawing.
 *
 * 1.2.8's procedurally-drawn boosters were rejected on sight — they looked
 * like what they were, nine pixels of rectangles pretending to be a rocket.
 * They are gone entirely: the sky gradient, starfield, pads, sprites, plumes,
 * legs and dust are all deleted, along with sky_fill(), rgb_lerp(),
 * draw_booster(), booster_state() and draw_splash_scene().
 *
 * In their place, a real 480x80 letterboxed band of two side boosters landing,
 * played from a JPEG frame sequence at 10 Hz.
 *
 * WHY A BAND AND NOT THE WHOLE PANEL, recorded again because it is the fact
 * that shapes every other decision here: the ILI9488 is 18-bit-only over SPI,
 * so a full 480x320 frame is 460,800 B = 184 ms at DISPLAY_SPI_CLOCK_HZ,
 * against a 100 ms frame period. Full-panel playback is not slow, it is
 * impossible — and it would saturate the panel for exactly the window the link
 * handshake runs in. The band is 115,200 B = 46 ms and fits with room spare.
 * Decode cost and flash space were never the constraint. The wire is.
 *
 * THE ASSET LIVES IN ITS OWN PARTITION. The remote leaves
 * CONFIG_PARTITION_TABLE_SINGLE_APP behind for partitions_remote.csv: 3 MB
 * factory (the 1 MB single-app partition held an 863 KB app with no room for
 * this) plus a 2 MB `splash` data partition, read memory-mapped, no
 * filesystem. Not embedded in the binary, deliberately — footage can then be
 * re-cut and reflashed without rebuilding or reflashing the image that runs
 * the fire path, and iterating on a decoration never touches it.
 *
 * The base keeps the single-app table. It has no splash asset, and leaving its
 * layout alone keeps a pad-side unit off the list of things this can break.
 * Note for later: that leaves the base app at 829 KB in a 1 MB partition,
 * ~19% headroom — worth watching, but not worth a flash-layout change today.
 *
 * FLASHING CHANGES. `./build_remote.sh flash` now writes bootloader +
 * partition table + app, not app alone: a device still carrying the old 1 MB
 * table would run the new image against the wrong map. The asset is written
 * separately with `./build_remote.sh splash <file>` and survives app
 * reflashes. Build one with tools/mkvideoband.py.
 *
 * GRADING IS PART OF THE FORMAT, NOT TASTE. mkvideoband.py desaturates,
 * darkens and then hard-caps every channel at 0x9A — the same bound 1.2.8
 * applied to its palette, for the same reason. This band sits behind white
 * title text on the boot screen of a launch controller and the text has to
 * win. Footage that looks right on a monitor will scream here.
 *
 * EVERY FAILURE IS NON-FATAL. No partition, no asset, a blank or corrupt one,
 * wrong dimensions, a frame that will not decode: each ends with a plain dark
 * band and a remote that boots normally. The container is validated whole at
 * init — magic, format, frame count, dimensions, interval, and every frame
 * offset and length against the partition size — so the 10 Hz path can index
 * the table without re-checking, and an erased all-0xFF partition is rejected
 * as cleanly as a corrupt one. A boot screen decoration must never be able to
 * stop the unit coming up.
 *
 * The clip HOLDS on its last frame rather than looping. STATE_LINKING maps to
 * the splash screen, so an unlinked remote sits here indefinitely (see the
 * 1.2.8 note) — and a landing clip restarting every ten seconds forever is a
 * worse thing to leave switched on in a case than a still of two landed
 * boosters. Once held, the decode is skipped and the blit writes identical
 * pixels, which flush()'s per-row memcmp rejects: an ended clip costs nothing
 * on the wire at all.
 *
 * The shipped asset is assets/splash_falconheavy.bin — the side boosters
 * landing at LZ-1/LZ-2 on 6 February 2018, cut from NASA imagery that is
 * public domain in the US. Provenance and the exact recipe are in
 * assets/README.md, so the 10 s band can be rebuilt without guessing at
 * which ten seconds of a four-minute source it came from.
 *
 * Remote-only: display, partition layout and flash procedure. No protocol
 * change, but the version moves because the binary did. Flash both units.
 *
 * 1.2.8 (2026-09-12): the boot splash has a landing animation behind it.
 *
 * Cosmetic, remote-only, no protocol change — but the version is bumped
 * because a changed binary sharing a version number is exactly what the
 * strict check exists to prevent. Flash both units.
 *
 * The ask was a 10 s video of two Falcon Heavy side boosters landing. A video
 * is not available on this hardware: the ILI9488 is 18-bit-only over SPI, so
 * a full 480x320 frame is 460,800 B = 184 ms at DISPLAY_SPI_CLOCK_HZ, against
 * a 100 ms frame period — decode cost and flash space are not the limit, the
 * wire is. Full-screen playback would also saturate the panel for the whole
 * splash, which is the window in which the link handshake runs.
 *
 * So the scene is drawn procedurally with the existing primitives: a graded
 * night sky band (y 116..190), a fixed starfield, two pads, and two boosters
 * descending on an ease-out profile with flickering plumes, deploying legs,
 * staggered touchdowns and settling dust. ~18 KB/frame on the wire, ~7 ms of
 * SPI, at the full 10 Hz. The band is repainted whole each frame and the
 * boosters drawn on top; flush()'s shadow diff (and its per-row memcmp fast
 * reject) is what keeps that cheap, so there is no hand-maintained erase list
 * to get wrong.
 *
 * Subdued by construction, per the same request: no channel above 0x9A, most
 * far below, against 0xFFFFFF title text and a C_SELECTED version string.
 * The stars deliberately do not twinkle — scattered single-pixel changes
 * across the full width widen flush()'s per-row runs for no visual gain.
 *
 * The static header block moves up (title y 8/36, version 64, credit 96) to
 * open the band. Dynamic layout is unchanged: headline 196, attempt 228, bar
 * 262, credit line at DH-26, so §10.2.1's live fields are where they were.
 *
 * Also in 1.2.8 — the boot splash stops claiming the handshake has a limit.
 *
 * It read "Attempt N / 5", clamped at 5, bar pinned at 100%. Every part of
 * that was wrong. LINK_REQUEST_MAX_RETRIES was never a give-up count: it is
 * the threshold at which tick_remote() changes cadence, and the remote retries
 * forever either side of it. The splash rendered a backoff threshold as a
 * denominator, promising an end that never came — and then froze the counter
 * there while the firmware was still working, which reads as a hung remote.
 * On the one screen whose job is to show the unit is alive.
 *
 * Now: no denominator, no clamp, "Attempt N" climbing without bound. Past the
 * threshold the headline becomes "No response from base" and the bar becomes
 * an indeterminate sweep — a number that keeps moving is the clearest proof
 * available that the unit has not given up. Below it the bar still measures
 * something real: progress through the fast-retry phase. display_splash()
 * loses its max_attempts argument; it had no callers and could only
 * reintroduce the same lie.
 *
 * And LINK_REQUEST_SLOW_INTERVAL_MS is a real interval at last: 5000, not the
 * 2000 it has held since the initial scaffolding commit, which made the
 * backoff ternary a choice between two identical values. Full reasoning in
 * rlc_config.h.
 *
* And the remote now says out loud that it is still hunting: one 40 ms
 * BUZZER_BEEP_LINK_TRY blip per handshake attempt while the link state is
 * LINKING. This is the battery measure the slow interval was not.
 *
 * A remote switched on with no base in range never links, never times out and
 * never stops retrying. STATE_LINKING maps to the splash screen, so that is
 * its indefinite steady state rather than a boot phase — it will sit there
 * drawing full current until the pack is flat, and the screen saying so is
 * usually in a case or face-down in a bag. The blip is the audible half, and
 * the decision it supports is the operator's: switch it off, or leave it.
 *
 * LINKING only, never LINK_LOST — that state already sounds
 * ALARM_LINK_LOST continuously, a far louder reminder than this, and a
 * one-shot layered under a running alarm only contends for the pattern
 * player (see the CRIT-01 history in 1.1.30).
 *
  * ONE CORRECTION FOR THE RECORD, because the change was requested to save
 * battery and does not: the backoff saves no meaningful power. rlc_espnow.c
 * sets WIFI_PS_NONE with no PM or tickless idle configured, so the receive
 * chain is powered continuously regardless of how often the remote transmits.
 * A ~40-byte LINK_REQUEST every 5 s instead of every 2 s is an order of
 * 0.1 mA against a draw dominated by the always-on radio and the display
 * backlight. The change is still worth having — it restores the documented
 * design and stops the cadence contradicting the spec — but it is a
 * correctness fix, not a power fix, and SHALL NOT be cited as one. What would
 * actually move the number, in order: blanking or dimming the backlight when
 * unlinked and idle; and, far more invasively, stopping the radio between
 * attempts on the never-linked path, which trades away the base's ability to
 * reach a remote that is not currently transmitting.
 *
 * A CONFIG_RLC_REMOTE_FAULT_INJECTION build draws no scene at all. That build
 * lies to its operator by construction and must not look normal — least of
 * all better than normal; the red banner keeps the band to itself.
 *
 * 1.2.7 (2026-09-10): the arm-key fault is now visible where it is needed,
 * not just for three seconds when it appears.
 *
 * 1.2.6 announced ARM KEY SWITCH FAULT with a triple beep and a 3 s amber
 * toast, fired once on the edge. The condition it reports is not transient —
 * a broken wire stays broken — and the announcement was landing at the wrong
 * moment: the operator turns the key, looks at the pad rather than the screen,
 * long-presses to arm, and gets refused. Under 1.2.6 that refusal read
 * "TURN ARM KEY FIRST" — telling them to turn a key they are looking at,
 * already turned, at the exact moment they are asking why it will not arm,
 * with the toast that would have explained it already expired.
 *
 *   1. The refusals are state-aware. The ARM guard and the FIRE key-off guard
 *      now say "ARM KEY FAULT - CHECK SWITCH" / "ARM KEY FAULT - FIRE
 *      REFUSED" when the contacts are in disagreement. The refusal itself is
 *      unchanged and still correct — a NO contact that fails open SHOULD read
 *      as SAFE and fail safe — only the explanation improves.
 *
 *   2. The indication persists. The status band's REMOTE field reads
 *      "KEY FAULT" instead of ARMED/SAFE for as long as the fault stands, on
 *      both the main and ARMED screens, and the main screen's prompt line
 *      reads "ARM KEY FAULT - CHECK SWITCH". The field change is the
 *      important half: that field's whole job is to report the key position,
 *      and while the two contacts disagree the remote cannot honestly claim
 *      one. Ranked below a base error on the prompt line (that one is about
 *      the fire path) but above the next-step prompt — which is precisely
 *      what this fault makes untrustworthy.
 *
 * Still not an interlock, still not a latched error screen. `display_error()`
 * would hold the screen until reboot, which for a maintenance warning on a
 * path that is not the fire path would be more disruptive than the fault.
 *
 * Remote-only, display and wording, no protocol change. Flash both units.
 *
 * 1.2.6 (2026-09-10): the remote's arm key switch has a second contact, and
 * the firmware now knows about it.
 *
 * Found by the operator, not by the code: the key is an SPDT with its common
 * at ground — NO to GPIO 7, NC to **GPIO 2** — and only the NO leg had ever
 * been documented. GPIO 2 was listed in FSD C.2 among the remote's *spare*
 * pins, "available for future expansion". It was not spare: the NC contact
 * hard-shorts it to ground for as long as the key sits at SAFE, which is most
 * of the time. Anyone taking that documented invitation and assigning GPIO 2
 * as an output would have been driving it into a dead short on every turn of
 * the key. Nothing did so — the pin was never configured — but the document
 * was pointing at the one spare pin with a wire on it.
 *
 * Three things follow, in order of importance:
 *
 *   1. The pin is claimed. `arm_switch_init()` configures GPIO 2 as a
 *      pulled-up input, which is safe in both key positions and cannot be
 *      reassigned by accident.
 *   2. FSD C.2 no longer lists it as spare (6 spare GPIOs on the remote, not
 *      7), and §5.5.2 now documents the switch as the SPDT it physically is.
 *   3. The contacts are cross-checked. A healthy SPDT closes exactly one
 *      contact per position, so the two debounced inputs are complementary;
 *      a disagreement lasting longer than ARM_SWITCH_DISAGREE_MS is reported
 *      as ARM KEY SWITCH FAULT (log, triple beep, display toast).
 *
 * Point 3 buys a real diagnosis. With the NO leg alone, a broken wire, a
 * lifted joint or a contact that no longer closes reads exactly like "key at
 * SAFE" — so the remote refuses every long-press with TURN ARM KEY FIRST while
 * the operator is looking at a key they have already turned, with nothing
 * anywhere to say why. Now that failure names itself.
 *
 * Deliberately NOT an interlock: `arm_switch_is_armed()` still follows the NO
 * contact alone, even while the fault is raised. The NC leg was unused until
 * today and its joint has never been exercised; letting it veto arming would
 * let a marginal solder joint on a previously dead pin disable the remote at a
 * launch — trading a silent diagnostic gap for a loud availability failure.
 * The remote's key is not in the fire path in any case (§5.4.4: the two breaks
 * are the base's arm relay and the channel relay), so nothing here touches the
 * fire-path safety argument.
 *
 * Remote-only, no protocol change. Flash both units together.
 *
 * 1.2.5 (2026-09-10): a marginal igniter connection gets its own signal —
 * two blips instead of one.
 *
 * The other half of 1.2.4's feature, deliberately deferred until the single
 * blip had been heard at the pad (T-A21, PASS 2026-09-10). A high-resistance
 * crimp is the fault the operator most wants to know about while still
 * standing at the motor — it is the one that costs a launch window — and until
 * now MARGINAL was the one band that made no sound at all, so a bad connection
 * was indistinguishable from no connection by ear.
 *
 *   CONNECTED → one 100 ms blip    (SIREN_IGNITER_CONNECTED)
 *   MARGINAL  → two 100 ms blips   (SIREN_IGNITER_MARGINAL)
 *   OPEN      → silence, unchanged
 *
 * Two rather than three, and 100 ms rather than 200 ms, so it cannot be heard
 * as `SIREN_ERROR` or `SIREN_CONTINUITY_LOST` (both three 200 ms blasts, both
 * meaning "stop"). Counting one against two is the easiest discrimination
 * available to someone who is deliberately not looking at anything, and the
 * shared pitch and length make the pair read as two values of one message.
 *
 * The rate limit changed with it, and this is the interesting part: it is now
 * per channel AND per band. A repeat of the same signal inside the window is
 * still suppressed as chatter, but a *change* of signal always sounds —
 * because "two blips, re-seat the crimp, one blip" is the exact loop this
 * feature exists to close, and the old shared window would have swallowed the
 * confirming blip and left the operator believing the crimp was still bad.
 * The cost is that a connection oscillating across the CONNECTED/MARGINAL
 * boundary can blip on each change; the round-robin sampler caps that at one
 * change per ~800 ms per channel, and a connection that cannot decide which
 * band it is in is itself something the operator needs to hear.
 *
 * Every gate and suppression from 1.2.4 applies unchanged to both patterns:
 * BOOT and IDLE only, never on an `initial` classification, and not inside the
 * post-fire inhibit window.
 *
 * Base-only, audible-only, no protocol change. Flash both units together.
 *
 * 1.2.4 (2026-09-10): the base blips its siren each time an igniter is
 * connected.
 *
 * Operator request: the person wiring up at the pad had no way to know a
 * connection had been made without walking back to read the LEDs on the base
 * or the remote. A 100 ms blip (SIREN_IGNITER_CONNECTED, §12.2) sounds when a
 * channel's continuity band moves to CONNECTED.
 *
 * Everything interesting about this change is what it refuses to do:
 *
 *   - It sounds in BOOT and IDLE only. In ARMED/PRE_FIRE/FIRING the siren is
 *     the pad's continuous warning that the fire path is live, and the blip's
 *     "drive on, drive off at the first tick" mechanism would silence it; in
 *     LINK_LOST and ERROR a patterned alert is running and carries meaning.
 *     The FSM gates it (§7.3.1) AND siren_chirp_connect() declines whenever
 *     the siren is already sounding — one gate is a property of a switch
 *     statement, two make it structural.
 *   - CONNECTED only. MARGINAL is a connection to look at rather than trust;
 *     giving it the same blip would teach the wrong reflex, and giving it a
 *     different one is a second pattern to keep distinct from the 3-blast
 *     alerts. Deferred until this blip has been heard in the field.
 *   - Not on the sampler's first classification of a channel (new `initial`
 *     flag on EVT_CONTINUITY_CHANGED), so igniters already connected at
 *     power-on do not blip their way through the first round-robin sweep.
 *   - Not within 2 s of the same channel's last blip (chatter from a
 *     half-seated connector), and not within 2 s of POST_FIRE → IDLE, where an
 *     unfired igniter reappears as CONNECTED with nobody having touched it.
 *
 * 100 ms rather than the 200 ms of every other pattern because the operator is
 * standing next to a siren built to be heard across a launch site. Long enough
 * that its internal sweep makes a tone rather than a click — v1.35's lesson
 * from the removed ARMED pulse; SIREN_CONNECT_CHIRP_MS is the knob if the
 * bench says otherwise.
 *
 * Base-only, audible-only, no protocol change. Version bumped anyway: the
 * binary differs. Flash both units together.
 *
 * 1.2.3 (2026-09-01): the base chirps its siren once at the end of a
 * successful boot.
 *
 * Operator request: a single 200 ms blast, so the operator at the pad hears
 * that the unit came up AND knows the siren itself has just been exercised —
 * its only previous sounds were fault and armed states, so a dead siren driver
 * could otherwise stay undetected until the moment a pad warning was needed.
 *
 * Placement is the whole design: the chirp sounds only after every init step
 * has passed. boot_fail() already sounds SIREN_ERROR (3 blasts) — so the
 * outcomes stay unambiguous: one chirp = booted, three blasts = halted, plus
 * the error LED either way. A single chirp is distinct from every operational
 * pattern (ERROR/CONTINUITY_LOST are 3 blasts, LINK_LOST is 4 long ones).
 *
 * This amends the bug #27 retest property "silent at power-on": that check
 * verified no UNCOMMANDED sound during the power-on transient (the gate
 * pull-down's job, untouched). A deliberate, firmware-commanded chirp after
 * boot is a different thing and is now specified in FSD v1.53 §12.2.
 *
 * Base-only, audible-only, no protocol change. Version bumped anyway: the
 * binary differs. Flash both units together.
 *
 * 1.2.2 (2026-09-01): the main screen's continuity legend is gone; the status
 * band takes its space.
 *
 * Operator request: the legend row under the channel grid ("CONNECTED /
 * MARGINAL / OPEN" with their glyphs) restated what every cell already shows —
 * each cell draws the glyph AND the band's name in the band's colour. The row
 * carried no information the grid did not, and it occupied 22 px of the one
 * screen element whose whole purpose is to be legible from across a launch
 * site. The row is removed; the band on the main screen now starts where the
 * legend sat (y=230 instead of 252), 90 px tall against 68.
 *
 * Main screen only. Every other screen keeps BAND_Y=252 because its centre box
 * (ARMED / FIRING / FIRE COMPLETE) ends at y=250 and is pinned there by a
 * _Static_assert — moving the band up there would mean shrinking the box the
 * operator stares at during a live sequence. The main screen's two status rows
 * are re-centred in the taller band (19/20/19 px) instead of hugging its
 * bottom edge.
 *
 * Remote-only, display-only, no protocol change. Version bumped anyway: the
 * binary differs, and a changed binary sharing a version number is exactly
 * what the strict version check exists to prevent. Flash both units together.
 *
 * 1.2.1 (2026-08-29): the remote's boot splash now carries an unmissable
 * FAULT INJECTION BUILD banner when CONFIG_RLC_REMOTE_FAULT_INJECTION is set —
 * a red frame around the whole screen and a red block displacing the club
 * credit. The build already announced itself four ways (compile #warning, boot
 * banner, flash-time warning, and a build failure if the option did not reach
 * the built config), but all four are on the developer's terminal. None is
 * visible to someone who picks the remote up at a firing point, which is
 * precisely the person who must not be misled by a build that lies to its
 * operator by construction.
 *
 * Remote-only, display-only, no protocol change. Version bumped anyway: the
 * binary differs, and a changed binary sharing a version number is exactly
 * what the strict version check exists to prevent. Flash both units together.
 *
 * NOT covered: a *base* built with --inject cannot be signalled on the
 * remote's splash. The remote knows only its own build, and the base does not
 * advertise its fault-injection state on the wire. Closing that gap needs a
 * protocol field and an explicit decision. */

/* 1.2.0 (2026-08-28): FINAL — Phase 5 release.
 *
 * A version-only bump: the code is byte-for-byte 1.1.35's, and 1.1.35 is the
 * build both units ended the second on-target campaign on (stock, non-
 * injection) with every defect it surfaced fixed and re-verified. The MINOR
 * bump marks the release rather than another patch: the Phase 5 review round
 * (RLC-REVIEW-ALL-009) and the two on-target campaigns that closed it out
 * are done, and the bug #29 regression suite is complete — cleared for live
 * fire.
 *
 * Final-build audit recorded with this tag: both fault-injection consoles
 * compile to nothing (options default n, absent from every sdkconfig, zero
 * injection symbols in both stock ELFs); the display-profile harness was
 * removed at 1.1.11; CONT_TRACE_INTERVAL_MS is 0 for field builds; the
 * rlc-hw-test-* bring-up projects are outside the main build.
 *
 * Deferred past this release, tracked in Development_Progress Phase 5:
 * T-S12/S13 and T-S18 (needs physical access), T-C06 replay tool, range
 * 10-100 m and power-consumption measurements (field), remote FSM host
 * harness, CI runner.
 *
 * Flash both units together, as always.
 *
 * 1.1.35 (2026-08-28): a critical pack no longer leaves the base armed.

 * Found live during the CRIT-01 on-target retest (fw 1.1.34, injection key
 * 'b'): battery-critical injected from ARMED sounded the alarm and latched
 * the remote's ERROR exactly as 1.1.30 intended — but the ARMED
 * EVT_BATTERY_CRITICAL handler entered ERROR without telling the base, and
 * the operator watched the base arm relay stay engaged for the full 10 s
 * ARM TIMEOUT. The remote was already in terminal ERROR, so it could not
 * disarm afterwards either; the pad was live with no way to command it safe
 * from the remote. The EVT_DISPLAY_FAULT handler (any-state, §5.5.6) sends
 * CMD_DISARM before entering ERROR for exactly this reason — the battery
 * path simply never had it.
 *
 * The ARMED handler now mirrors the display-fault behaviour: stop the fire
 * repeats, CMD_DISARM the armed channel (0xFF belt-and-braces if the channel
 * is unknown), then enter ERROR. The remote's own battery dying is no longer
 * a reason the base should stay armed.
 *
 * PRE_FIRE and FIRING already send CEASE_FIRE on this event, and the base's
 * ARMED handler disarms on CEASE_FIRE, so those paths were never exposed —
 * the gap was ARMED only.
 *
 * Remote-only change; flash both units for the version check.
 *
 * 1.1.34 (2026-08-28): a base-aborted countdown names its cause.
 *
 * Found live during the Bug #29 regression (T-A17, fw 1.1.33): with the
 * channel armed, the countdown running and the operator holding the fire
 * button, pulling the igniter lead produced the base-side abort exactly as
 * specified — but the remote toasted "[NACK] WRONG STATE".
 *
 * The race: the base's continuity-loss disarm pushed its STATUS_UPDATE, but
 * the operator's still-flowing CMD_FIRE repeats drew a NACK 0x05 first
 * (measured 7 ms apart at the base; the NACK reached the remote's FSM before
 * the status did). The NACK path showed the raw reason, and once the remote
 * was in IDLE the arriving status was reconciled silently — so the frame
 * that knew the cause (ch 1 OPEN, base disarmed) passed through without
 * ever being shown. "WRONG STATE" is true about the repeat and useless
 * about the pad; at the pad the difference between "igniter left the
 * circuit" and anything else is the thing worth saying. Same defect class
 * as the MAJ-06 minors: right safety behaviour, wrong operator sentence.
 *
 * Three changes, all display-layer:
 *   - the PRE_FIRE fire-repeat NACK path never shows the raw NACK reason;
 *     it says "BASE ENDED SEQUENCE" (the status-exit path's wording) and
 *     latches the armed channel;
 *   - the first STATUS_UPDATE in IDLE settles the latch one-shot, no
 *     timer: channel not armed and band OPEN names the continuity-loss
 *     disarm exactly as the ARMED handler has since RM-07, with
 *     BEEP_CONTINUITY_LOST. Any other frame consumes the latch silently;
 *   - the PRE_FIRE status-exit path gains the same RM-07 discrimination,
 *     so the cause is named immediately when the status frame wins the
 *     race instead.
 *
 * The latch is cleared on a new ARM attempt and on link loss, so it cannot
 * name a continuity loss belonging to a previous sequence. The safety
 * behaviour is untouched: the sequence ended at the base in 58 ms measured,
 * and it still does.
 *
 * Remote-only change; flash both units for the version check.
 *
 * 1.1.33 (2026-08-28): no false ERROR on the first shot of a power cycle.
 *
 * Cosmetic finding from the bug #31 two-cycle regression: cycle 1 logged
 * "E gptimer: gptimer_stop(418): timer is not running" immediately before
 * the timer started — the BF-01 defensive stop-before-every-start firing on
 * the first pulse of a power cycle, when there is genuinely nothing to stop.
 * Harmless (it does not appear on cycle 2, because the completion handler
 * has left the timer in a stoppable state), but it prints at ERROR level on
 * every first shot, which will send someone hunting a fault that is not
 * there.
 *
 * The log line comes from inside the gptimer driver, so it cannot be demoted
 * from app code — the call itself must be skipped. New s_running flag in
 * rlc_fire_timer.c mirrors the driver's RUN state: set by the only
 * gptimer_start() in the unit, cleared on every stop path, and both the
 * defensive stop in fire_timer_start() and fire_timer_stop() now only call
 * gptimer_stop() when it is set. The stop itself is unchanged when the timer
 * IS running, so BF-01's protection is not weakened: the flag cannot go
 * stale in the dangerous direction, because nothing else calls gptimer_start.
 *
 * Base-only change; flash both units for the version check.
 *
 * 1.1.32 (2026-08-28): a failed ARM now undoes itself.
 *
 * Operator-reported during on-target testing: a fire press answered with
 * "NOT ARMED - ARM FIRST" while the base was in fact armed, followed a moment
 * later by "BASE STATE MISMATCH - DISARMED". Both messages are diagnostic —
 * the first comes only from the remote's IDLE fire-press handler, the second
 * only from a STATUS_UPDATE carrying an armed bitmask while the remote is in
 * IDLE — so the base really was armed, relay closed and siren running, with
 * this unit unaware and its status band reading READY TO ARM.
 *
 * Reviewing the ARM failure branches found the asymmetry: the ACK-timeout
 * branch was the ONLY one that did not send CMD_DISARM. ACK-after-key-off,
 * operator interruption, channel mismatch and the key-off retry abort all undo
 * the arm; the timeout — which is exactly what a lost ACK looks like, and so
 * the case most likely to have left the base armed — did not. The §8.2.3
 * reconciliation still caught it within one status interval, which is why the
 * operator saw the mismatch toast, but that is up to 2 s of live pad described
 * to the operator as safe, and a fire press inside that window is refused with
 * the opposite of the truth.
 *
 * The timeout branch now sends CMD_DISARM like its siblings. Harmless when the
 * base never armed: §7.2.7 makes DISARM idempotent, answered with an ACK. The
 * reconciliation remains the backstop for the two mechanisms this cannot cover
 * (a remote restart while the base is armed, and a lost DISARM).
 *
 * Remote-only change; flash both units for the version check.
 *
 * 1.1.31 (2026-08-28): the unconfirmed-outcome toast says what to do about it.
 *
 * On-target testing of 1.1.30 (Test_Report_Phase5_Review_Fixes.md) showed how
 * often MAJ-02's evidence gate lands on the unknown case: the base pushes its
 * FIRING STATUS_UPDATE on entering the state, so a fire-button release within
 * roughly one link latency of ignition — measured at >190 ms — reaches the
 * classifier before that frame does. The channel had genuinely carried current
 * for ~200 ms and the remote said "CH 1 ENDED - NOT CONFIRMED".
 *
 * That is accurate about the remote's knowledge and wrong about the operator's
 * next move: at the pad "NOT CONFIRMED" reads as "nothing happened", when the
 * honest instruction is that the igniter may have had current. Now
 * "CH n OUTCOME UNKNOWN - TREAT AS LIVE", at all three sites that report an
 * unprovable outcome. Wording only — no logic change, and the evidence gate
 * itself is untouched.
 *
 * Remote-only change, but the strict version check covers both units, so flash
 * them together.
 *
 * 1.1.30 (2026-08-28): Phase 5 review fixes (RLC-REVIEW-ALL-009) — the
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
#define RLC_VERSION_MINOR  2
#define RLC_VERSION_PATCH  10
#define RLC_VERSION_STRING "1.2.10"
