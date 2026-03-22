/**
 * RLC Remote Unit State Machine — Stub
 *
 * Full implementation in Phase 3.
 */

#include "rlc_remote_state.h"

static rlc_state_t s_state = STATE_BOOT;
static uint8_t s_selected_channel = 1;

rlc_state_t remote_state_get(void)
{
    return s_state;
}

uint8_t remote_state_get_selected_channel(void)
{
    return s_selected_channel;
}
