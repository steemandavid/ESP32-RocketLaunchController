/**
 * RLC Continuity Sensing Module (Base Unit)
 *
 * ADC1-based 8-channel continuity monitoring with 64-sample oversampling,
 * three-band classification (CONNECTED/MARGINAL/OPEN) with hysteresis.
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
 * Called from continuity_task context, so it must stay short.
 *
 * @param cb  receives the channel (1-8) and its new band. The arguments were
 *            added on 2026-08-26: the FSM needs to know *which* channel moved
 *            and where to, so that an armed igniter going OPEN can disarm the
 *            base (FSD 7.2.7). A bare "something changed" ping was enough for
 *            the STATUS_UPDATE trigger but not for a safety decision.
 */
void continuity_register_change_cb(void (*cb)(uint8_t ch, rlc_continuity_band_t band));

/**
 * Last sampled sense voltage for a channel, in microvolts.
 *
 * The band alone does not say how close a channel sits to a threshold, which
 * is what matters when judging a marginal igniter or validating the band
 * boundaries themselves.
 *
 * @param ch  channel 1-8
 * @return    microvolts, or 0 for an invalid channel
 */
int32_t continuity_get_uv(uint8_t ch);

/** Last averaged raw ADC count for a channel (1-8), before calibration. */
int32_t continuity_get_raw(uint8_t ch);

/**
 * Tell the sampler that a channel relay has just been de-energised
 * (FSD §5.4.6, CI-01).
 *
 * The channel is skipped in the round-robin for CONT_RELAY_DROPOUT_MS so no
 * reading is taken while the NO→NC contacts are still bouncing. Its band is
 * left unchanged during the window; the next sweep re-reads it.
 *
 * Called from rlc_relay.c on every de-energise. Cheap and ISR-safe-ish (one
 * timestamp write), safe to call for a channel that was already off.
 */
void continuity_note_relay_released(uint8_t ch);
