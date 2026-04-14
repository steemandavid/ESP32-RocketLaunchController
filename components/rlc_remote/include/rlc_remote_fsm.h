/**
 * RLC Remote Unit State Machine
 *
 * Full FSM per FSD §8. Single-task-owner model: all state is owned by
 * remote_fsm_task. External readers use getter functions.
 *
 * State transitions:
 *   BOOT → LINKING → IDLE → ARMED → PRE_FIRE → FIRING → IDLE
 *   Any → LINK_LOST, Any → ERROR
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "rlc_protocol.h"

/**
 * Initialise the remote FSM. Creates event queue and registers
 * with link manager. Must be called AFTER rlc_link_init().
 */
int remote_fsm_init(void);

/**
 * Start the state machine task and fire-repeat task.
 */
int remote_fsm_start(void);

/**
 * Get the event queue (for input callbacks to post events).
 */
QueueHandle_t remote_fsm_get_queue(void);

/**
 * Get the FSM task handle (for task notifications).
 */
TaskHandle_t remote_fsm_get_task(void);

/**
 * Get current FSM state.
 */
rlc_state_t remote_fsm_get_state(void);

/**
 * Get the currently selected channel (1-8).
 */
uint8_t remote_fsm_get_selected_channel(void);

/**
 * Get the currently armed channel (0=none, 1-8).
 */
uint8_t remote_fsm_get_armed_channel(void);

/**
 * Get the armed channel as volatile read (for fire-repeat task).
 */
volatile uint8_t *remote_fsm_get_armed_channel_ptr(void);

/**
 * Check if fire-repeat should be active (PRE_FIRE or FIRING state).
 */
bool remote_fsm_is_fire_repeat_active(void);

/**
 * Notify the FSM to stop fire repeat.
 */
void remote_fsm_stop_fire_repeat(void);
