#pragma once

#include <stdbool.h>

/**
 * RLC Remote Arm Key Switch (FSD §5.5.2)
 *
 * The key is an SPDT with its common at ground: the NO contact drives
 * PIN_ARM_SWITCH and the NC contact PIN_ARM_SWITCH_NC, both pulled up. The NO
 * leg alone determines the arm state — that is the tested, authoritative path
 * and it is unchanged. The NC leg is read purely as a cross-check.
 */

void arm_switch_init(void);
void arm_switch_start_task(void);
bool arm_switch_is_armed(void);
void arm_switch_register_cb(void (*cb)(bool armed));

/**
 * True while the two key contacts have disagreed for longer than
 * ARM_SWITCH_DISAGREE_MS.
 *
 * A healthy SPDT presents exactly one closed contact per position, so the two
 * debounced inputs are complementary. Two states are not:
 *
 *   both open   — normal and brief while the key is turning (break-before-make),
 *                 a fault once it persists: a broken wire, a lifted joint, a
 *                 contact that no longer closes, or a key standing between
 *                 positions.
 *   both closed — impossible on a break-before-make switch: a short, or the
 *                 wrong switch fitted.
 *
 * This is a **diagnostic, not an interlock**. `arm_switch_is_armed()` keeps
 * reading the NO leg alone even while this is true, deliberately: the NC leg
 * was undocumented and unused until fw 1.2.6, so letting it veto arming would
 * mean a marginal joint on a previously unused pin could disable the remote at
 * a launch. The value here is diagnosis — without it, a failed NO contact is
 * indistinguishable from "key at SAFE", and the operator is told to turn a key
 * they have already turned. The remote's key is not in the fire path in any
 * case (FSD §5.4.4: the two breaks are the base's arm relay and the channel
 * relay), so this cannot weaken the fire-path argument.
 */
bool arm_switch_get_fault(void);

/**
 * Register a callback fired when the fault state above changes.
 * Runs in arm_switch_task context, so it must stay short.
 */
void arm_switch_register_fault_cb(void (*cb)(bool fault));
