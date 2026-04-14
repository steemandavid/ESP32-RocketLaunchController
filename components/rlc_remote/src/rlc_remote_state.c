/**
 * RLC Remote Unit State Machine — Public API
 *
 * Delegates to rlc_remote_fsm.c for actual state management.
 */

#include "rlc_remote_state.h"
#include "rlc_remote_fsm.h"

rlc_state_t remote_state_get(void)
{
    return remote_fsm_get_state();
}

uint8_t remote_state_get_selected_channel(void)
{
    return remote_fsm_get_selected_channel();
}
