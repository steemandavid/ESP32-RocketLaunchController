/**
 * RLC Arm Sense Monitor (Base Unit)
 *
 * Monitors the arm sense input (GPIO 21) which reads the ARM SENSE node --
 * the output of the arm relay COM contact through a voltage divider
 * (27k/10k + 3.3V zener clamp).
 *
 * HIGH = arm relay closed / VBAT present on fire path
 * LOW  = arm relay de-energised / no VBAT
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
