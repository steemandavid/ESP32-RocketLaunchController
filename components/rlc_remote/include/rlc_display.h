/**
 * RLC Display Driver (Remote Unit)
 *
 * ILI9488 480x320 SPI LCD — Phase 4 implementation.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * Display colour constants (RGB565).
 */
#define DISP_COLOR_GREEN         0x0640
#define DISP_COLOR_RED           0xF800
#define DISP_COLOR_CYAN          0x06DF
#define DISP_COLOR_RED_BG        0xB000
#define DISP_COLOR_YELLOW        0xFEE0
#define DISP_COLOR_WHITE         0xFFFF
#define DISP_COLOR_BLACK         0x0000

/**
 * Initialise the ILI9488 display.
 *
 * @return 0 on success
 */
int display_init(void);

/**
 * Display the splash screen with version and connection status.
 *
 * @param attempt       Current link attempt number
 * @param max_attempts  Maximum attempts
 */
void display_splash(int attempt, int max_attempts);

/**
 * Display firmware version mismatch error.
 */
void display_firmware_mismatch(const uint8_t *base_ver, const uint8_t *remote_ver);

/**
 * Draw the main IDLE status screen.
 */
void display_main_status(void);

/**
 * Draw the ARMED screen.
 */
void display_armed(uint8_t channel);

/**
 * Draw the firing/pre-fire screen.
 *
 * @param channel        Firing channel
 * @param countdown_ms   Remaining pre-fire time (0 = ignition active)
 */
void display_firing(uint8_t channel, uint32_t countdown_ms);

/**
 * Draw the link-lost screen.
 */
void display_link_lost(uint32_t seconds_since_contact, int ping_attempts);

/**
 * Draw the error screen.
 */
void display_error(const char *error_text);

/**
 * Show a NACK reason overlay for NACK_DISPLAY_DURATION_MS.
 */
void display_nack(const char *reason_text);
