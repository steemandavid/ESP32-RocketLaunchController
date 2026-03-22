/**
 * RLC Base Unit State Machine
 */

#pragma once

#include <stdint.h>
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
 * Get the current continuity bitmask.
 */
uint16_t base_state_get_continuity(void);

/**
 * Get current error flags.
 */
uint8_t base_state_get_error_flags(void);
