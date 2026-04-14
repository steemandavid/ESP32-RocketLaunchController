/**
 * RLC Base Unit State Machine — Public Getters
 *
 * Thread-safe reads of FSM state. Actual FSM logic is in rlc_base_fsm.c/h.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "rlc_protocol.h"

/**
 * Get the current base FSM state.
 */
rlc_state_t base_state_get(void);

/**
 * Get the currently armed channel (0 if none).
 */
uint8_t base_state_get_armed_channel(void);

/**
 * Get current continuity bands (deprecated — use continuity_get_bands()).
 */
uint16_t base_state_get_continuity(void);

/**
 * Get current error flags.
 */
uint8_t base_state_get_error_flags(void);

/**
 * Get the currently firing channel (0 if none).
 */
uint8_t base_state_get_firing_channel(void);

/**
 * Check if the FSM is in a busy state (ARMED/PRE_FIRE/FIRING/POST_FIRE).
 * Used by the link guard callback to reject LINK_REQUEST.
 */
bool base_state_is_busy(void);
