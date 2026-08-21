/**
 * RLC Base Arm-State Derivation
 *
 * Pure function of a cached STATUS_UPDATE — no hardware or driver deps, so
 * the host tests compile the real source (tests/host/test_armstate.c) and
 * divergence between the display and the tests is impossible.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rlc_protocol.h"

typedef enum {
    BASE_ARM_UNKNOWN = 0,   /* no fresh status — never claim SAFE */
    BASE_ARM_SAFE,          /* key off, fire path dead */
    BASE_ARM_READY,         /* key turned, path still dead — arming permitted */
    BASE_ARM_ARMED,         /* arm relay closed, VBAT live on the fire path */
    BASE_ARM_WELD,          /* sense HIGH while the relay should be de-energised */
} base_arm_state_t;

/**
 * Derive the base arm state shown to the operator.
 *
 * ARMED/WELD are driven by the ARM SENSE (the arm relay COM output), never
 * by the key switch: a welded relay leaves the fire path live with the key
 * OFF, and keying the display off the key switch would print SAFE over an
 * energised igniter circuit.
 *
 * @param st    cached STATUS_UPDATE from the base
 * @param fresh true when the status is within its staleness window
 */
base_arm_state_t rlc_base_arm_state(const rlc_payload_status_update_t *st,
                                    bool fresh);
