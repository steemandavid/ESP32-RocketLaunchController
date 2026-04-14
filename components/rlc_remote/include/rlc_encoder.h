/**
 * RLC Rotary Encoder Driver (Remote Unit)
 *
 * Interrupt-driven quadrature decoder with push button.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * Callback for encoder rotation.
 *
 * @param channel  New selected channel (1–8)
 */
typedef void (*rlc_encoder_rotate_cb_t)(uint8_t channel);

/**
 * Callback for encoder button short press (< 500 ms).
 */
typedef void (*rlc_encoder_press_cb_t)(void);

/**
 * Callback for encoder button long press (>= 500 ms hold).
 * FSD §5.5.1: 500 ms long-press to ARM in IDLE with arm switch ON.
 */
typedef void (*rlc_encoder_long_press_cb_t)(void);

/**
 * Initialise the rotary encoder GPIOs and interrupts.
 */
void encoder_init(void);

/**
 * Register callbacks for rotation, short press, and long press.
 */
void encoder_register_rotate_cb(rlc_encoder_rotate_cb_t cb);
void encoder_register_press_cb(rlc_encoder_press_cb_t cb);
void encoder_register_long_press_cb(rlc_encoder_long_press_cb_t cb);

/**
 * Get the currently selected channel (1–8).
 */
uint8_t encoder_get_channel(void);

/**
 * Poll the encoder push button debounce.
 * Call at DEBOUNCE_POLL_INTERVAL_MS (10 ms).
 */
void encoder_poll_button(void);
