/**
 * RLC Display Driver (Remote Unit)
 *
 * ILI9488 480x320 SPI LCD — Phase 4 implementation.
 * FSD §10.2.0: colour constants use RGB888 notation for readability.
 * Driver transmits as RGB666 to ILI9488.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * Display colour constants (RGB888 — FSD §10.2.0).
 * Blue used instead of green for continuity GOOD (colour-blind accessibility).
 */
#define DISP_COLOR_CONT_GOOD      { 0, 120, 255 }   /* Blue — continuity GOOD */
#define DISP_COLOR_CONT_OPEN      { 255, 0, 0 }     /* Red — continuity OPEN / error */
#define DISP_COLOR_CONT_SHORT     { 255, 140, 0 }   /* Orange — continuity SHORT */
#define DISP_COLOR_CONT_MARGINAL  { 255, 220, 0 }   /* Yellow — continuity MARGINAL */
#define DISP_COLOR_SELECTED       { 0, 220, 255 }   /* Cyan — selected channel */
#define DISP_COLOR_ARMED_BG       { 180, 0, 0 }     /* Red — armed channel background */
#define DISP_COLOR_WHITE          { 255, 255, 255 }
#define DISP_COLOR_BLACK          { 0, 0, 0 }

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
