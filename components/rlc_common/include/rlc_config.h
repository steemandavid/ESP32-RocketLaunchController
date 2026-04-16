/**
 * RLC Configuration Constants
 *
 * All tuneable parameters in one place. Adjust values here
 * without modifying logic code.
 */

#pragma once

#include <stdint.h>

/* ── Timing Constants ─────────────────────────────────────────── */

#define HEARTBEAT_INTERVAL_MS          500
#define HEARTBEAT_TIMEOUT_MS           500
#define HEARTBEAT_FAIL_THRESHOLD       3
#define HEARTBEAT_WINDOW_SIZE          10
#define RSSI_AVERAGE_WINDOW            3

#define STATUS_UPDATE_INTERVAL_MS      2000
#define STATUS_STALE_TIMEOUT_MS        5000

#define LINK_REQUEST_INTERVAL_MS       2000
#define LINK_REQUEST_MAX_RETRIES       5
#define LINK_REQUEST_SLOW_INTERVAL_MS  2000

#define CMD_ACK_TIMEOUT_MS             500
#define CMD_RETRY_COUNT                1

#define FIRE_REPEAT_INTERVAL_MS        200
#define FIRE_AUTHORIZATION_TIMEOUT_MS  500

#define PRE_FIRE_DELAY_MS              2000
#define FIRE_PULSE_DURATION_MS         1000
#define POST_FIRE_COOLDOWN_MS          2000

#define ARM_TIMEOUT_MS                 10000

#define SIREN_LINK_LOST_DURATION_MS    4000
#define NACK_DISPLAY_DURATION_MS       3000

#define WATCHDOG_TIMEOUT_S             5
#define DEBOUNCE_POLL_INTERVAL_MS      10
#define CONT_RELAY_DROPOUT_MS          50    /* Relay settling before first ADC sample (§5.4.6) */

/* ── Fire Safety Configuration ─────────────────────────────────── */

#define COMPLETE_PULSE_ON_LINK_LOSS    1     /* 1 = complete fire pulse on link loss, 0 = immediate abort */

/* ── Voltage Thresholds (millivolts) ──────────────────────────── */

#define BASE_VBAT_DIVIDER_RATIO        4.3f
#define BASE_VBAT_MIN_ARM_MV           10500
#define BASE_VBAT_CRITICAL_MV          9000

#define REMOTE_VBAT_DIVIDER_RATIO      2.8f
/* Bench-test overrides: remote reads ~3290 mV on USB power (3.3V rail).
 * Production values: MIN_ARM=7000, MIN_OPERATE=6600, CRITICAL=6400. */
#define REMOTE_VBAT_MIN_ARM_MV         3200
#define REMOTE_VBAT_MIN_OPERATE_MV     3100
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

/* Peer MAC addresses — actual hardware MACs */
#define BASE_MAC_ADDR    { 0x44, 0x1B, 0xF6, 0x81, 0xFA, 0xF8 }
#define REMOTE_MAC_ADDR  { 0x44, 0x1B, 0xF6, 0x81, 0xF1, 0x70 }

/* ── Continuity Sensing (Base only, §14.5) ───────────────────── */

#define CONT_R_REF_OHM                 3300    /* Total series resistance (1.5k + 1.8k fusible) */
#define CONT_R_PULL_OHM                100000  /* Pull-down per channel (Ω) */
#define CONT_SAMPLE_INTERVAL_MS        100     /* Per-channel ADC interval */
#define CONT_OVERSAMPLE_COUNT          64      /* ADC samples averaged per reading */

/* Thresholds in microvolts (µV) — multiply ADC millivolts by 1000 */
#define CONT_SHORT_UV                  500     /* Below = SHORT (< 0.5 Ω) */
#define CONT_MARGINAL_UV               66000   /* Above = MARGINAL (> ~20 Ω) */
#define CONT_OPEN_UV                   1500000 /* Above = OPEN (> ~500 Ω) */

/* Hysteresis bands (µV) — prevents oscillation at boundaries */
#define CONT_HYSTERESIS_SHORT_UV       200
#define CONT_HYSTERESIS_MARGINAL_UV    5000
#define CONT_HYSTERESIS_OPEN_UV        50000

/* ── RGB LED ──────────────────────────────────────────────────── */

#define RGB_LED_GPIO               48
#define RGB_LED_BRIGHTNESS         30   /* 0–255 */

/* ── Display Configuration (Remote only) ──────────────────────── */

#define DISPLAY_SPI_HOST           SPI2_HOST
#define DISPLAY_SPI_CLOCK_HZ      20000000
#define DISPLAY_WIDTH              480
#define DISPLAY_HEIGHT             320
#define DISPLAY_ROTATION           1
