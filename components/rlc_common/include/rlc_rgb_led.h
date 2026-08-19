/**
 * RLC RGB LED Status Driver
 *
 * WS2812 (NeoPixel) on GPIO 48 via RMT peripheral.
 * Pattern engine runs as a FreeRTOS task.
 */

#pragma once

#include <stdint.h>
#include "rlc_protocol.h"

/**
 * LED pattern identifiers.
 */
typedef enum {
    LED_PATTERN_OFF,
    LED_PATTERN_BOOT,           /* Blue slow pulse (2s cycle) */
    LED_PATTERN_IDLE,           /* Green solid */
    LED_PATTERN_IDLE_ARM_ON,    /* Green fast blink (250ms on/off) — base only */
    LED_PATTERN_ARMED,          /* Red slow blink (500ms on/off) */
    LED_PATTERN_PRE_FIRE,       /* Red fast blink (100ms on/off) */
    LED_PATTERN_FIRING,         /* Red solid */
    LED_PATTERN_POST_FIRE,      /* Yellow solid */
    LED_PATTERN_LINK_LOST,      /* Yellow fast blink (200ms on/off) */
    LED_PATTERN_ERROR,          /* Red triple flash */
    LED_PATTERN_PING_FAIL,      /* Orange 250ms flash (overlay) */
    LED_PATTERN_CHANNEL_STATUS, /* Base 8-pixel strip: one pixel per igniter channel */
} rlc_led_pattern_t;

/**
 * Initialise the RGB LED driver (RMT + task).
 *
 * @return 0 on success
 */
int rlc_rgb_led_init(void);

/**
 * Set the current LED pattern based on system state.
 *
 * @param pattern  Pattern to display
 */
void rlc_rgb_led_set_pattern(rlc_led_pattern_t pattern);

/**
 * Trigger a brief overlay flash (e.g., ping failure orange).
 * Temporarily overrides current pattern for duration_ms, then restores.
 *
 * @param r, g, b    Colour
 * @param duration_ms Duration of the flash
 */
void rlc_rgb_led_flash_overlay(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms);

/**
 * Set the number of active pixels (1 for remote, 8 for base unit).
 * Must be called after rlc_rgb_led_init().  Default is 1.
 *
 * @param count  Number of pixels (1-8)
 */
void rlc_rgb_led_set_pixel_count(int count);

/**
 * Set pattern based on FSM state (convenience).
 */
void rlc_rgb_led_set_state(rlc_state_t state);

/* ── Multi-pixel status feeds (base unit 8-pixel strip) ────────── */

/**
 * Publish the igniter continuity map: 2 bits per channel, same encoding as
 * STATUS_UPDATE (00=OPEN, 01=GOOD, 10=MARGINAL, 11=SHORT), channel 1 in the
 * least significant pair. Pixel N shows channel N+1 in the colours configured
 * by RLC_COLOR_CONT_* in rlc_config.h.
 *
 * Once published, LED_PATTERN_IDLE and LED_PATTERN_IDLE_ARM_ON render the
 * channel map instead of plain green on a multi-pixel strip, and the ERROR
 * pattern shows it dimmed between flashes. Firing-path patterns (ARMED,
 * PRE_FIRE, FIRING) are left alone — those signals stay unmistakable.
 *
 * Note the DevKit's built-in NeoPixel shares the strip's data line, so it
 * mirrors pixel 0 (channel 1).
 */
void rlc_rgb_led_set_channel_bands(uint16_t bands);

/**
 * Highlight one channel (1-8) on the strip; 0 = none. The highlighted pixel
 * pulses so the armed/firing channel is identifiable on the map.
 */
void rlc_rgb_led_set_active_channel(uint8_t channel);

/**
 * Publish link RSSI in dBm (0 = unknown). While booting/linking, a multi-pixel
 * strip shows this as a signal-strength bar instead of the blue pulse.
 */
void rlc_rgb_led_set_rssi(int rssi_dbm);
