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

/**
 * Return true once per fresh press transition, then clear.
 *
 * A button that is held at boot does NOT generate a fresh press
 * until it has been released first (safety interlock).
 */
bool fire_button_was_fresh_press(void);

/**
 * Register optional callbacks for press / release events.
 *
 * @param on_press   Called on 0xFF -> 0x00 transition (may be NULL)
 * @param on_release Called on 0x00 -> 0xFF transition (may be NULL)
 */
void fire_button_register_cb(void (*on_press)(void), void (*on_release)(void));
