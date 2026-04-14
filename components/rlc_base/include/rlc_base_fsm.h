/**
 * RLC Base Unit State Machine
 *
 * Full FSM implementation per FSD §7. Manages all state transitions,
 * guard conditions, relay control, siren patterns, and timer management.
 *
 * Thread model: single-task-owner — all state is owned by state_machine_task.
 * External readers use getter functions.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "rlc_protocol.h"

/**
 * Initialise the base FSM. Creates the event queue and registers it
 * with the link manager. Must be called AFTER rlc_link_init().
 *
 * @return 0 on success, -1 on error
 */
int base_fsm_init(void);

/**
 * Start the state machine task (priority 4, core 0, 8192 stack).
 * Must be called AFTER base_fsm_init().
 *
 * @return 0 on success, -1 on error
 */
int base_fsm_start(void);

/**
 * Get the event queue handle (for registering with input callbacks).
 */
QueueHandle_t base_fsm_get_queue(void);

/**
 * Get the state machine task handle (for fire timer notifications).
 */
TaskHandle_t base_fsm_get_task(void);

/**
 * Post an event to the FSM queue (non-blocking, drops if full).
 */
void base_fsm_post_event(uint32_t event_type, bool armed);

/**
 * Get current FSM state (thread-safe read).
 */
rlc_state_t base_fsm_get_state(void);

/**
 * Get the currently armed channel (0 = none, 1-8).
 */
uint8_t base_fsm_get_armed_channel(void);

/**
 * Get the currently firing channel (0 = none, 1-8).
 */
uint8_t base_fsm_get_firing_channel(void);

/**
 * Get current error flags.
 */
uint8_t base_fsm_get_error_flags(void);

/**
 * Check if the FSM is in a busy state (for link guard callback).
 * Returns true if in ARMED/PRE_FIRE/FIRING/POST_FIRE.
 */
bool base_fsm_is_busy(void);
