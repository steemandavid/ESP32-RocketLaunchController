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
 * Get the current continuity bands (2 bits per channel, FSD §5.4.2).
 * Ch1 in bits 1:0, Ch2 in bits 3:2, ..., Ch8 in bits 15:14.
 * Values: 0=OPEN, 1=GOOD, 2=MARGINAL, 3=SHORT.
 */
uint16_t base_state_get_continuity_bands(void);

/**
 * Get current error flags.
 */
uint8_t base_state_get_error_flags(void);
