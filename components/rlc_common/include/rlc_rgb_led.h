/**
 * RLC RGB LED Status Driver
 *
 * WS2812 (NeoPixel) on GPIO 48 via RMT peripheral, driven by a FreeRTOS task.
 *
 * Both units carry an 8-pixel strip: one pixel per igniter channel. The strip
 * is an IGNITER CONTINUITY DISPLAY — system status modulates the channel map
 * rather than replacing it, so the operator never loses sight of the pad. The
 * only exceptions are the firing path (ARMED/PRE_FIRE/FIRING) and ERROR, which
 * take the whole strip so those signals stay unmistakable.
 *
 * The DevKit's on-board LED sits in parallel on the same data line and so
 * mirrors pixel 0. With RLC_STRIP_REVERSED it has no independent meaning.
 *
 * Rendering layers, highest priority first:
 *
 *   1. Firing path   ARMED / PRE_FIRE / FIRING  — whole strip red (FSD §11.1)
 *   2. ERROR         red triple flash, map dimmed in the gap
 *   3. Alarm wink    brief full-strip flash over the map; multiple active
 *                    alarms alternate colours on successive winks
 *   4. Stale         whole map dimmed — remote only, cached data is old
 *   5. Breathing     key-ON / arm-ready warning
 *   6. Channel map   continuity, with the channel of interest highlighted
 *
 * Before the first continuity data arrives, a cyan chase runs instead.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "rlc_protocol.h"

/**
 * LED pattern identifiers.
 *
 * Only the firing path and ERROR are set explicitly by the FSMs; every other
 * system state renders as the channel map plus whatever alarms are raised.
 */
typedef enum {
    LED_PATTERN_OFF,
    LED_PATTERN_STATUS,         /* Channel map + alarm/stale/breathe layers */
    LED_PATTERN_ARMED,          /* Red slow blink (500ms on/off) */
    LED_PATTERN_PRE_FIRE,       /* Red fast blink (100ms on/off) */
    LED_PATTERN_FIRING,         /* Red solid */
    LED_PATTERN_ERROR,          /* Red triple flash */
} rlc_led_pattern_t;

/* Alarm classes for rlc_rgb_led_set_alarms(). Several may be set at once;
 * the wink alternates colours across successive winks. */
#define RLC_ALARM_LINK_LOST   (1u << 0)   /* amber   */
#define RLC_ALARM_BATTERY     (1u << 1)   /* magenta */
#define RLC_ALARM_ARM_FAULT   (1u << 2)   /* white   */

/**
 * Initialise the RGB LED driver (RMT + task).
 *
 * @return 0 on success
 */
int rlc_rgb_led_init(void);

/**
 * Set the current LED pattern.
 */
void rlc_rgb_led_set_pattern(rlc_led_pattern_t pattern);

/**
 * Set the number of active pixels (8 on both units; 1 for a bare DevKit).
 * Must be called after rlc_rgb_led_init().  Default is 1.
 *
 * @param count  Number of pixels (1-8)
 */
void rlc_rgb_led_set_pixel_count(int count);

/**
 * Set master brightness (0-255). Defaults to RGB_LED_BRIGHTNESS_BASE; the
 * remote calls this with RGB_LED_BRIGHTNESS_REMOTE.
 */
void rlc_rgb_led_set_brightness(uint8_t brightness);

/* ── Status feeds ─────────────────────────────────────────────────
 *
 * All published from the units' housekeeping loops, never from an FSM, so
 * the fire path is untouched. Torn reads are harmless: the worst case is one
 * frame of stale colour on a status display.
 */

/**
 * Publish the igniter continuity map: 2 bits per channel, same encoding as
 * STATUS_UPDATE (00=OPEN, 01=GOOD, 10=MARGINAL, 11=SHORT), channel 1 in the
 * least significant pair. Channel N is drawn at pixel 7-(N-1) when
 * RLC_STRIP_REVERSED is set, in the colours configured by RLC_COLOR_CONT_*.
 *
 * Until this is called at least once the strip shows the cyan boot chase.
 */
void rlc_rgb_led_set_channel_bands(uint16_t bands);

/**
 * Highlight the channel of interest (1-8; 0 = none). On the base this is the
 * armed or firing channel; on the remote it is the encoder cursor, which
 * pulses so the selection is visible on the map.
 */
void rlc_rgb_led_set_active_channel(uint8_t channel);

/**
 * Raise or clear alarm classes (bitwise OR of RLC_ALARM_*). Rendered as a
 * brief full-strip wink over the map every RLC_STRIP_ALARM_PERIOD_MS.
 */
void rlc_rgb_led_set_alarms(uint32_t alarms);

/**
 * Mark the channel map as stale (remote only — the cached STATUS_UPDATE has
 * aged out). The map dims to RLC_STRIP_STALE_DIM_PCT: last known, not live.
 */
void rlc_rgb_led_set_stale(bool stale);

/**
 * Key/arm-ready warning. Base: the whole map breathes. Remote: only the
 * cursor channel breathes, since the remote knows which channel is selected.
 */
void rlc_rgb_led_set_key_warning(bool warn);
