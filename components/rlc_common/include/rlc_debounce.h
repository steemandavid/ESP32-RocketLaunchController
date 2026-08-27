/**
 * RLC Shift-Register Debounce Engine
 *
 * Generic debounce for digital inputs using a shift-register method.
 * Supports 8-bit (80 ms) and 16-bit (160 ms) register widths at the 10 ms
 * sample interval every caller uses.
 *
 * Debouncing is symmetric by default: the same number of agreeing samples is
 * required to go active as to go inactive. That is right for a sensor, where
 * both directions are equally trustworthy, and WRONG for a dead-man input,
 * where the two directions have opposite safety consequences. See
 * rlc_debounce_set_fast_release().
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
    uint8_t              release_samples; /* Agreeing HIGH samples needed to go
                                           * inactive; 0 = use the full width
                                           * (symmetric, the default) */
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
 * Require fewer agreeing samples to report INACTIVE (released) than to report
 * ACTIVE (pressed). Opt-in; symmetric behaviour is unchanged for every caller
 * that does not ask for this.
 *
 * For a dead-man input the two directions are not equally safe. A missed
 * release fires an igniter the operator has let go of; a spurious release only
 * aborts, which is the direction that cuts current. Demanding the same 80 ms of
 * evidence for both makes the system exactly as reluctant to stop as to start.
 *
 * Found on target 2026-08-27: mashing the fire button fired the channel.
 * Releases shorter than the full 8-sample window never reached all-high, so the
 * debouncer reported no release, the FSM saw a continuous hold, CMD_FIRE repeats
 * kept flowing, and both dead-man layers stayed satisfied — they sit downstream
 * of this one decision. A worn or chattering contact produces the same signal.
 *
 * @param samples  Agreeing HIGH samples needed to release (1..width). Two at a
 *                 10 ms interval is 20 ms, which sits between switch bounce
 *                 (1-10 ms, rejected) and a real human release (30-80 ms,
 *                 caught). One sample would start reporting bounce as release.
 */
void rlc_debounce_set_fast_release(rlc_debounce_t *db, uint8_t samples);

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
