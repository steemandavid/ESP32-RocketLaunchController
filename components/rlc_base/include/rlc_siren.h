/**
 * RLC Siren Control (Base Unit)
 */

#pragma once

#include <stdbool.h>

/**
 * Initialise siren GPIO.
 */
void siren_init(void);

/**
 * Set siren to continuous ON.
 *
 * Used from ARMED all the way through PRE_FIRE and FIRING. The siren sounds
 * without interruption for the whole armed period.
 *
 * The 500 ms ARMED pulse pattern was removed on 2026-08-26: gating the supply
 * at 1 Hz interferes with the siren's own internal modulation (the sweep never
 * gets to run), so the ARMED warning was less audible than a steady tone
 * rather than more. See FSD 5.4.8 / 7.4.1.
 */
void siren_start_continuous(void);

/**
 * Set siren to link-lost pattern (500ms on/off, 4 cycles).
 */
void siren_start_link_lost(void);

/**
 * Turn siren off.
 */
void siren_off(void);

/**
 * Set siren to error pattern (3 short blasts, 200ms on/200ms off).
 * Used on entry to ERROR state.
 */
void siren_start_error(void);

/**
 * Set siren to the continuity-loss pattern (FSD §12.2 SIREN_CONTINUITY_LOST:
 * 200 ms on / 200 ms off, 3 cycles, then silence).
 *
 * Sounded when the armed channel goes OPEN during ARMED or PRE_FIRE and the
 * base auto-disarms, so that disarm is audibly distinct from a key-off disarm
 * (which is silent).
 */
void siren_start_continuity_lost(void);

/**
 * One short blast (200 ms on, then silence) — FSD §12.2 SIREN_BOOT_TEST.
 *
 * Sounded once at the end of a successful boot, so the operator knows the
 * unit is up and the siren itself has just been exercised. A single chirp is
 * deliberately distinct from every alert pattern: SIREN_ERROR and
 * SIREN_CONTINUITY_LOST are three blasts, SIREN_LINK_LOST is four long ones.
 * A base that fails to boot sounds SIREN_ERROR instead and never chirps, so
 * the chirp is a positive claim that boot completed.
 */
void siren_boot_pulse(void);

/**
 * One short blip (SIREN_CONNECT_CHIRP_MS, then silence) — FSD §12.2
 * SIREN_IGNITER_CONNECTED.
 *
 * Sounded when a channel's igniter appears on the continuity sense, so the
 * operator at the pad hears the connection being made instead of walking back
 * to read the LEDs on the base or the remote.
 *
 * Unlike every other entry point here, this one REFUSES rather than takes
 * over: if the siren is already sounding — a continuous ARMED/PRE_FIRE/FIRING
 * tone, or a running LINK_LOST/ERROR/CONTINUITY_LOST pattern — the call is a
 * no-op. Every other pattern here means something the operator must hear, and
 * the one-shot's "drive on, drive off at the first tick" mechanism would end
 * by silencing it. The FSM already gates the call to BOOT and IDLE (§7.3.1);
 * this is the belt-and-braces half, because the cost of getting it wrong is a
 * silenced pad warning.
 */
void siren_chirp_connect(void);

/**
 * Two short blips — FSD §12.2 SIREN_IGNITER_MARGINAL.
 *
 * Sounded when a channel's igniter appears on the continuity sense but reads
 * MARGINAL: a connection with enough resistance that it may not fire. One blip
 * means good, two means look at it — the operator hears the difference without
 * walking back to the LEDs, which is the case that costs a launch window.
 *
 * Same gate and same refusal-while-busy behaviour as siren_chirp_connect(),
 * which see.
 */
void siren_chirp_marginal(void);
