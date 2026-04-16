/**
 * RLC Arm Sense and Key Sense Monitor (Base Unit)
 *
 * Monitors two inputs:
 *
 * 1. Key sense (GPIO 42) — direct read of the physical key switch position
 *    through a voltage divider (27k/10k + 3.3V zener clamp).
 *    HIGH = key switch ON / VBAT present at switch output
 *    LOW  = key switch OFF
 *
 * 2. Arm sense (GPIO 21) — reads the arm relay COM output through a voltage
 *    divider (27k/10k + 3.3V zener clamp). Used for post-energize verification
 *    and contact-welding detection.
 *    HIGH = arm relay closed / VBAT present on fire path
 *    LOW  = arm relay de-energised / no VBAT
 *
 * Includes contact-welding detection (FSD sec 5.4.3, 7.3.2): verifies that
 * arm sense reads LOW when the arm relay GPIO is known to be de-energised.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Initialise the arm sense GPIO and debounce engine.
 * Must be called before arm_sense_start_task().
 */
void arm_sense_init(void);

/**
 * Start the arm sense monitoring FreeRTOS task.
 * Registers with the task watchdog.
 */
void arm_sense_start_task(void);

/**
 * Get the current debounced arm sense state.
 *
 * @return true if arm relay is closed (VBAT present), false otherwise
 */
bool arm_sense_get_debounced(void);

/**
 * Get the raw (undebounced) GPIO level.
 * Used for STATUS_UPDATE arm_switch_hw field.
 *
 * @return true if GPIO reads HIGH, false if LOW
 */
bool arm_sense_get_raw(void);

/**
 * Register a callback invoked on debounced state transitions.
 *
 * @param cb  Callback: cb(armed) where armed=true means arm relay closed
 */
void arm_sense_register_cb(void (*cb)(bool armed));

/**
 * Register a callback for contact-welding fault detection.
 * Called when arm sense reads HIGH while the arm relay GPIO is known LOW
 * (de-energised) — indicating welded arm relay contacts.
 *
 * @param cb  Callback invoked on fault detection (no parameters)
 */
void arm_sense_register_fault_cb(void (*cb)(void));

/* ── Key Sense (direct key switch position) ───────────────────── */

/**
 * Get the current debounced key switch state.
 *
 * @return true if key switch is ON (VBAT present at key output), false otherwise
 */
bool key_sense_get_debounced(void);

/**
 * Get the raw (undebounced) key switch GPIO level.
 *
 * @return true if GPIO reads HIGH, false if LOW
 */
bool key_sense_get_raw(void);

/**
 * Register a callback invoked on debounced key switch state transitions.
 *
 * @param cb  Callback: cb(on) where on=true means key switch ON
 */
void key_sense_register_cb(void (*cb)(bool on));
