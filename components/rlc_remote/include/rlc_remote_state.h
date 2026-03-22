/**
 * RLC Remote Unit State Machine
 */

#pragma once

#include <stdint.h>
#include "rlc_protocol.h"

/**
 * Get the current remote FSM state.
 */
rlc_state_t remote_state_get(void);

/**
 * Get the currently selected channel (1–8).
 */
uint8_t remote_state_get_selected_channel(void);
