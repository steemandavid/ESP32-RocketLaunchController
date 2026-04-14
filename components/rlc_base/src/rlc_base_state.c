/**
 * RLC Base Unit State Machine — Public API
 *
 * Delegates to rlc_base_fsm.c for actual state management.
 * This file provides the getter functions used by status_update_task
 * and other readers.
 */

#include "rlc_base_state.h"
#include "rlc_base_fsm.h"

rlc_state_t base_state_get(void)
{
    return base_fsm_get_state();
}

uint8_t base_state_get_armed_channel(void)
{
    return base_fsm_get_armed_channel();
}

uint16_t base_state_get_continuity(void)
{
    /* Continuity is managed by rlc_continuity module, not FSM */
    return 0;  /* Caller should use continuity_get_bands() directly */
}

uint8_t base_state_get_error_flags(void)
{
    return base_fsm_get_error_flags();
}

uint8_t base_state_get_firing_channel(void)
{
    return base_fsm_get_firing_channel();
}

bool base_state_is_busy(void)
{
    return base_fsm_is_busy();
}
