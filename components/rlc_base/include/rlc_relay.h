/**
 * RLC Relay Control
 *
 * Encapsulated relay drive functions with configurable polarity.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialise all relay GPIOs to safe (inactive) state.
 * MUST be called before any other peripheral init (§9.7).
 */
void relay_init(void);

/**
 * Set a single channel relay on or off.
 *
 * @param channel  Channel number (1–8)
 * @param state    true = active (relay closed), false = inactive (relay open)
 */
void relay_channel_set(uint8_t channel, bool state);

/**
 * Set all channel relays to inactive (open).
 */
void relay_channel_all_off(void);

/**
 * Set the low-side relay.
 *
 * @param state  true = closed (current can flow), false = open (safe)
 */
void relay_lowside_set(bool state);

/**
 * Emergency safe: all channel relays off + low-side relay open.
 * Called at boot, on disarm, on error, on link loss.
 */
void relay_all_safe(void);

/**
 * Check relay feedback input.
 *
 * @return true if safe (no current detected on firing bus), false if fault
 */
bool relay_feedback_is_safe(void);
