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
