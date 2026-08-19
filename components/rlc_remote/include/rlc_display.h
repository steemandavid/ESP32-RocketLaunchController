/**
 * RLC Display Driver (Remote Unit) — Phase 4
 *
 * ILI9488 480x320 SPI LCD (SPI2_HOST, 20 MHz, RGB666).
 *
 * Architecture:
 *   - A PSRAM framebuffer (480x320x3 bytes, RGB666) is rendered into by a
 *     dedicated `display_task` (priority 2, core 1 — FSD §9.10).
 *   - Only the dirty bounding box is flushed over SPI (FSD §10.3 partial
 *     refresh); full redraw happens on screen changes only.
 *   - The task derives the screen from the remote FSM state; the functions
 *     below are non-blocking overrides/hints posted from other tasks. No
 *     caller ever blocks on SPI.
 *
 * FSD §10.2.0: colour constants use RGB888 notation for readability.
 * Driver transmits as RGB666 to ILI9488 (top 6 bits of each byte).
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
 * Initialise the ILI9488 display: SPI bus, panel init sequence, framebuffer,
 * and the boot health check (ID read-back — FSD §9.13 step 6).
 *
 * @return 0 on success, -1 on SPI/panel/allocation failure
 */
int display_init(void);

/**
 * True when display_init() succeeded and the panel answered its ID read-back.
 * FSD §15.4 T-S10: a false return at boot must drive the unit to ERROR.
 */
bool display_is_healthy(void);

/**
 * Panel ID read back at init (0 if the read failed).
 */
uint32_t display_get_id(void);

/**
 * Start the display task (priority 2, core 1, 8192 stack — FSD §9.10).
 * Call after display_init(); safe to call before the FSM is started.
 *
 * @return 0 on success
 */
int display_start_task(void);

/**
 * Display the splash screen with version and connection status.
 * The task keeps this screen up for BOOT/LINKING; the arguments only
 * refresh the attempt counter/progress bar.
 *
 * @param attempt       Current link attempt number
 * @param max_attempts  Maximum attempts
 */
void display_splash(int attempt, int max_attempts);

/**
 * Display firmware version mismatch error (latched until reboot).
 */
void display_firmware_mismatch(const uint8_t *base_ver, const uint8_t *remote_ver);

/**
 * Request the main IDLE status screen (normally selected automatically).
 */
void display_main_status(void);

/**
 * Request the ARMED screen (normally selected automatically).
 */
void display_armed(uint8_t channel);

/**
 * Request the firing/pre-fire screen (normally selected automatically).
 *
 * @param channel        Firing channel
 * @param countdown_ms   Remaining pre-fire time (0 = ignition active)
 */
void display_firing(uint8_t channel, uint32_t countdown_ms);

/**
 * Request the link-lost screen (normally selected automatically).
 */
void display_link_lost(uint32_t seconds_since_contact, int ping_attempts);

/**
 * Show the fire-complete screen for POST_FIRE_COOLDOWN_MS (FSD §10.2.4a).
 */
void display_fire_complete(uint8_t channel);

/**
 * Latch the error screen (FSD §10.2.6). Stays up until reboot.
 */
void display_error(const char *error_text);

/**
 * Show a NACK reason overlay for NACK_DISPLAY_DURATION_MS (FSD §10.2.7).
 */
void display_nack(const char *reason_text);

/**
 * Show a short informational toast (same overlay mechanism as NACK,
 * amber instead of red) — e.g. "TURN ARM KEY FIRST".
 */
void display_toast(const char *text);

/**
 * Backlight control.
 */
void display_backlight(bool on);
