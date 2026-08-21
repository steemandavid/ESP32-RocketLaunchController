/**
 * RLC Base Arm-State Derivation — see rlc_arm_state.h.
 */

#include "rlc_arm_state.h"

base_arm_state_t rlc_base_arm_state(const rlc_payload_status_update_t *st,
                                    bool fresh)
{
    if (!st || !fresh) return BASE_ARM_UNKNOWN;

    bool sense = st->base_arm_sense != 0;
    bool key   = st->base_key_switch != 0;

    /* The relay is only meant to be energised in the firing path states. Sense
     * HIGH anywhere else means the contacts are closed when they should not be.
     * Checked here as well as via ERR_RELAY_FAULT so the warning appears before
     * the base's own weld confirm count elapses. */
    uint8_t st_state = st->base_state;
    bool relay_expected_on = (st_state == STATE_ARMED || st_state == STATE_PRE_FIRE ||
                              st_state == STATE_FIRING);

    if (st->error_flags & ERR_RELAY_FAULT) return BASE_ARM_WELD;
    if (sense && !relay_expected_on)        return BASE_ARM_WELD;
    if (sense)                              return BASE_ARM_ARMED;
    if (key)                                return BASE_ARM_READY;
    return BASE_ARM_SAFE;
}
