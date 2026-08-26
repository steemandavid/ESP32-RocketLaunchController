/**
 * RLC Fire Button Driver
 *
 * Debounced fire button with fresh-press detection and LED control.
 * FSD refs: 5.5.3, 8.3.2
 */

#pragma once

#include <stdbool.h>

/**
 * Initialise GPIO for the fire button and its LEDs.
 */
void fire_button_init(void);

/**
 * Start the fire-button polling task (10 ms interval).
 */
void fire_button_start_task(void);

/**
 * Return true if the button is currently pressed (debounced).
 */
bool fire_button_is_pressed(void);

/* Fresh-press interlock (FSD 5.5.3): provided by edge-triggered press
 * events — a callback fires only on a released->pressed transition, so a
 * button held at power-on cannot produce a press event until released.
 * The former fire_button_was_fresh_press() polling API was dead code
 * (review 4.12) and has been removed. */

/**
 * Register optional callbacks for press / release events.
 *
 * @param on_press   Called on 0xFF -> 0x00 transition (may be NULL)
 * @param on_release Called on 0x00 -> 0xFF transition (may be NULL)
 */
void fire_button_register_cb(void (*on_press)(void), void (*on_release)(void));

/**
 * Set the fire button ring LED from system state (FSD line 1110):
 * red = the button is live, green = it is not.
 *
 * "Live" means the remote is ARMED/PRE_FIRE/FIRING **and** the base agrees —
 * a fresh STATUS_UPDATE showing the same channel armed. The remote can hold
 * ARMED while the base has already dropped out (arm timeout, key off, a
 * continuity loss it has not yet reported), and a red ring in that window
 * would promise a fire path that no longer exists.
 *
 * Idempotent and cheap: safe to call every FSM tick.
 */
void fire_button_set_live(bool live);
