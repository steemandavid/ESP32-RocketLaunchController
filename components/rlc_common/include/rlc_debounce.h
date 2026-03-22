/**
 * RLC Shift-Register Debounce Engine
 *
 * Generic debounce for digital inputs using a shift-register method.
 * Supports 8-bit (80 ms) and 16-bit (160 ms) register widths.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * Debounce register width.
 */
typedef enum {
    DEBOUNCE_8BIT  = 8,
    DEBOUNCE_16BIT = 16,
} rlc_debounce_width_t;

/**
 * Debounce state for a single input.
 */
typedef struct {
    uint16_t             shift_reg;     /* Shift register (8 or 16 bits used) */
    rlc_debounce_width_t width;         /* Register width */
    int                  gpio_num;      /* GPIO number */
    bool                 stable_state;  /* Last stable state (true = active/LOW) */
    bool                 initialised;   /* Has a stable state been established? */
} rlc_debounce_t;

/**
 * Callback when a debounced input changes state.
 *
 * @param gpio_num     GPIO number
 * @param new_state    New stable state (true = active/pressed/LOW)
 * @param user_data    User-provided context
 */
typedef void (*rlc_debounce_cb_t)(int gpio_num, bool new_state, void *user_data);

/**
 * Initialise a debounce instance.
 *
 * @param db       Debounce state to initialise
 * @param gpio_num GPIO pin number
 * @param width    Register width (8 or 16 bit)
 */
void rlc_debounce_init(rlc_debounce_t *db, int gpio_num, rlc_debounce_width_t width);

/**
 * Feed a new sample into the debounce shift register.
 * Call this at the configured polling interval (typically 10 ms).
 *
 * @param db          Debounce state
 * @param raw_level   Current GPIO reading (0 or 1)
 * @param cb          Callback on state change (may be NULL)
 * @param user_data   Passed to callback
 * @return            true if a state change occurred
 */
bool rlc_debounce_update(rlc_debounce_t *db, int raw_level,
                         rlc_debounce_cb_t cb, void *user_data);

/**
 * Get the current stable state.
 *
 * @param db  Debounce state
 * @return    true if input is stably active (LOW), false if stably inactive (HIGH)
 */
bool rlc_debounce_get_state(const rlc_debounce_t *db);

/**
 * Check if the debounce has established an initial stable reading.
 */
bool rlc_debounce_is_stable(const rlc_debounce_t *db);
