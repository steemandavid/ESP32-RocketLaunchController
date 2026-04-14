/**
 * RLC Continuity Sensing Module (Base Unit)
 *
 * ADC1-based 8-channel continuity monitoring with 64-sample oversampling,
 * 4-band classification (SHORT/GOOD/MARGINAL/OPEN) with hysteresis.
 * FSD §5.4.2, §7.3.1.
 */

#pragma once

#include <stdint.h>
#include "rlc_protocol.h"

/**
 * Initialise ADC1 for all 8 continuity channels.
 * Must be called AFTER rlc_battery_init() (shares the ADC1 unit handle).
 */
void continuity_init(void);

/**
 * Start the continuity sampling task.
 * Samples one channel per CONT_SAMPLE_INTERVAL_MS (100 ms) in round-robin.
 */
void continuity_start_task(void);

/**
 * Get the current continuity bands for all channels.
 * 2 bits per channel: ch1=bits[1:0], ch2=bits[3:2], ..., ch8=bits[15:14].
 * Values match rlc_continuity_band_t enum directly.
 */
uint16_t continuity_get_bands(void);

/**
 * Get the band for a single channel.
 *
 * @param ch  Channel number (1-8)
 * @return    Continuity band classification
 */
rlc_continuity_band_t continuity_get_channel(uint8_t ch);

/**
 * Register a callback invoked when any channel's band changes.
 * Called from continuity_task context.
 */
void continuity_register_change_cb(void (*cb)(void));
