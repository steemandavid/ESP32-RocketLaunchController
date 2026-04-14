/**
 * RLC Relay Control
 *
 * Encapsulated relay drive functions with configurable polarity.
 * FSD §7.4.1: relay_fire_set / relay_fire_all_off / arm_relay_set / relay_all_safe.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialise all relay GPIOs (channel SPDT + arm relay) to safe (inactive) state.
 * MUST be called before any other peripheral init (FSD §9.7).
 */
void relay_init(void);

/**
 * Energise or de-energise one channel SPDT relay.
 *
 * @param channel  Channel number (1-8)
 * @param state    true = energised (NO/fire position), false = de-energised (NC/continuity)
 */
void relay_fire_set(uint8_t channel, bool state);

/**
 * De-energise all channel SPDT relays (return to NC/continuity position).
 */
void relay_fire_all_off(void);

/**
 * Energise or de-energise the arm relay (GPIO 47, via IRLZ44N MOSFET).
 * Primary fire path interlock — hardware AND gate with physical key switch.
 *
 * @param state  true = energised (fire path enabled), false = de-energised (safe)
 */
void arm_relay_set(bool state);

/**
 * Emergency safe: de-energise arm relay + all channel relays.
 * Called at boot, on disarm, on error, on link loss.
 */
void relay_all_safe(void);
