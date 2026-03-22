/**
 * RLC RGB LED Status Driver
 *
 * WS2812 (NeoPixel) on GPIO 47 via RMT peripheral.
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
    LED_PATTERN_PING_FAIL,      /* Orange 50ms flash (overlay) */
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
 * Set pattern based on FSM state (convenience).
 */
void rlc_rgb_led_set_state(rlc_state_t state);
