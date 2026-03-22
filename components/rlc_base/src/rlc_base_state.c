/**
 * RLC Base Unit State Machine — Stub
 *
 * Full implementation in Phase 3.
 */

#include "rlc_base_state.h"

static rlc_state_t s_state = STATE_BOOT;
static uint8_t s_armed_channel = 0;
static uint16_t s_continuity_bitmask = 0;
static uint8_t s_error_flags = 0;

rlc_state_t base_state_get(void)
{
    return s_state;
}

uint8_t base_state_get_armed_channel(void)
{
    return s_armed_channel;
}

uint16_t base_state_get_continuity(void)
{
    return s_continuity_bitmask;
}

uint8_t base_state_get_error_flags(void)
{
    return s_error_flags;
}
