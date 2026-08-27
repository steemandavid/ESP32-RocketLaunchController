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

#if CONFIG_RLC_FAULT_INJECTION
/**
 * TEST ONLY (T-S07): make the FSM task spin without feeding the watchdog.
 *
 * Set from the injection console. The spin happens in the FSM task because
 * that is the task the TWDT actually covers — spinning anywhere else would
 * prove nothing about watchdog coverage of the safety state machine. Expect a
 * reboot within WATCHDOG_TIMEOUT_S, and all relays de-energised afterwards.
 */
void base_fsm_inject_wdt_hang(void);
#endif

/**
 * Get the state machine task handle (for fire timer notifications).
 */
TaskHandle_t base_fsm_get_task(void);

/* base_fsm_post_event() was removed 2026-08-27 (BF-05). Post events by
 * xQueueSend() to base_fsm_get_queue() with a short blocking timeout — see
 * rlc_base_main.c. A zero-timeout send can drop a safety event. */

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
