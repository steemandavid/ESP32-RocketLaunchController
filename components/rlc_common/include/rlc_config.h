/**
 * RLC Configuration Constants
 *
 * All tuneable parameters in one place. Adjust values here
 * without modifying logic code.
 */

#pragma once

#include <stdint.h>

/* ── Timing Constants ─────────────────────────────────────────── */

#define HEARTBEAT_INTERVAL_MS          1000
#define HEARTBEAT_TIMEOUT_MS           500
#define HEARTBEAT_FAIL_THRESHOLD       3
#define HEARTBEAT_WINDOW_SIZE          10
#define RSSI_AVERAGE_WINDOW            3

#define STATUS_UPDATE_INTERVAL_MS      2000
#define STATUS_STALE_TIMEOUT_MS        5000

#define LINK_REQUEST_INTERVAL_MS       2000
#define LINK_REQUEST_MAX_RETRIES       15
#define LINK_REQUEST_SLOW_INTERVAL_MS  5000

#define CMD_ACK_TIMEOUT_MS             500
#define CMD_RETRY_COUNT                1

#define FIRE_REPEAT_INTERVAL_MS        200
#define FIRE_AUTHORIZATION_TIMEOUT_MS  500

#define PRE_FIRE_DELAY_MS              5000
#define FIRE_PULSE_DURATION_MS         2000
#define POST_FIRE_COOLDOWN_MS          2000

#define SIREN_LINK_LOST_DURATION_MS    4000
#define NACK_DISPLAY_DURATION_MS       3000

#define WATCHDOG_TIMEOUT_S             2
#define DEBOUNCE_POLL_INTERVAL_MS      10

/* ── Voltage Thresholds (millivolts) ──────────────────────────── */

#define BASE_VBAT_DIVIDER_RATIO        4.0f
#define BASE_VBAT_MIN_ARM_MV           10500
#define BASE_VBAT_CRITICAL_MV          9000

#define REMOTE_VBAT_DIVIDER_RATIO      2.0f
#define REMOTE_VBAT_MIN_ARM_MV         3500
#define REMOTE_VBAT_MIN_OPERATE_MV     3300
#define REMOTE_VBAT_CRITICAL_MV        3000

/* ── Hardware Configuration ───────────────────────────────────── */

#define NUM_CHANNELS                   8
#define WIFI_CHANNEL                   11

/* ESP-NOW encryption keys (16 bytes each) — CHANGE BEFORE DEPLOYMENT */
#define ESPNOW_PMK  { \
    0x52, 0x4C, 0x43, 0x5F, 0x50, 0x4D, 0x4B, 0x5F, \
    0x44, 0x45, 0x46, 0x41, 0x55, 0x4C, 0x54, 0x21  \
}

#define ESPNOW_LMK  { \
    0x52, 0x4C, 0x43, 0x5F, 0x4C, 0x4D, 0x4B, 0x5F, \
    0x44, 0x45, 0x46, 0x41, 0x55, 0x4C, 0x54, 0x21  \
}

/* Pre-shared key for CRC32 integrity check (16 bytes) */
#define CMD_INTEGRITY_KEY  { \
    0x52, 0x4C, 0x43, 0x5F, 0x43, 0x52, 0x43, 0x5F, \
    0x49, 0x4E, 0x54, 0x45, 0x47, 0x52, 0x49, 0x54  \
}

/* Peer MAC addresses — MUST be set to actual hardware MACs */
#define BASE_MAC_ADDR    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 }
#define REMOTE_MAC_ADDR  { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02 }

/* ── RGB LED ──────────────────────────────────────────────────── */

#define RGB_LED_GPIO               47
#define RGB_LED_BRIGHTNESS         30   /* 0–255 */

/* ── Display Configuration (Remote only) ──────────────────────── */

#define DISPLAY_SPI_HOST           SPI2_HOST
#define DISPLAY_SPI_CLOCK_HZ      20000000
#define DISPLAY_WIDTH              480
#define DISPLAY_HEIGHT             320
#define DISPLAY_ROTATION           1
