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
 * Set siren to pulsing mode (500ms on / 500ms off).
 * Used during ARMED state.
 */
void siren_start_pulse(void);

/**
 * Set siren to continuous ON.
 * Used during PRE_FIRE and FIRING.
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
