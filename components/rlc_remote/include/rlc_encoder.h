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
 * Called in INTERRUPT context from the quadrature ISR (encoder_isr in
 * rlc_encoder.c): keep it short, IRAM-safe, and never block. The registered
 * application handler posts to the FSM queue with xQueueSendFromISR.
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
 * True while the encoder push button is held (debounced).
 *
 * Exposed so the FSM can refuse to start the arming sequence with an input
 * already held — see the §7.2.9a sequence guards in rlc_remote_fsm.c.
 */
bool encoder_button_is_pressed(void);

/**
 * Narrow the selectable channel range to what the base actually has
 * (FSD §8.2.2 — num_channels from LINK_ACK). Values outside 1..NUM_CHANNELS
 * are ignored; the current selection is clamped down if needed.
 */
void encoder_set_max_channel(uint8_t max_channel);

/**
 * Poll the encoder push button debounce.
 * Call at DEBOUNCE_POLL_INTERVAL_MS (10 ms).
 */
void encoder_poll_button(void);

/**
 * Diagnostic counters since boot.
 *
 * @param isr    ISR entries that passed the lockout
 * @param valid  legal single-step quadrature transitions
 * @param steps  channel changes actually emitted
 *
 * valid should track the detents turned (x4, one per transition); a much
 * larger isr count means edges are arriving that are not real rotation —
 * electrical noise on CLK/DT. steps should be valid / ENC_DIVIDER.
 */
void encoder_get_stats(uint32_t *isr, uint32_t *valid, uint32_t *steps);
