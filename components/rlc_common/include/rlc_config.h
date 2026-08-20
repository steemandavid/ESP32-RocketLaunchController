/**
 * RLC Configuration Constants
 *
 * All tuneable parameters in one place. Adjust values here
 * without modifying logic code.
 */

#pragma once

#include <stdint.h>
#include "sdkconfig.h"

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

/* Minimum time the splash screen stays up, even if the link comes up sooner
 * (linking typically completes in well under a second). FSD §10.2.1. */
#define SPLASH_MIN_DURATION_MS         10000

#define WATCHDOG_TIMEOUT_S             5
#define DEBOUNCE_POLL_INTERVAL_MS      10
#define CONT_RELAY_DROPOUT_MS          50    /* Relay settling before first ADC sample (§5.4.6) */

/* ── Fire Safety Configuration ─────────────────────────────────── */

#define COMPLETE_PULSE_ON_LINK_LOSS    1     /* 1 = complete fire pulse on link loss, 0 = immediate abort */

/* Bitmask of channels whose continuity ADC input has the bug #18 hardware
 * protection fitted (Schottky clamp diodes on the ADC pin + snubber across
 * the relay contact). Bit 0 = ch1 ... bit 7 = ch8.
 *
 * Firing a channel without that protection couples VBAT onto an unclamped
 * ADC input (GPIO 2-10) and destroys the ESP32 — this already happened twice
 * (Dev-Progress bug #18). ARM is NACKed and the channel relay refuses to
 * close for any channel not in this mask.
 *
 * Set to 0xFF once all eight channels are protected. */
#define FIRE_PROTECTED_CHANNEL_MASK    0x01  /* channel 1 only (2026-07-21) */

#define CHANNEL_IS_PROTECTED(ch) \
    (((ch) >= 1) && ((ch) <= NUM_CHANNELS) && \
     ((FIRE_PROTECTED_CHANNEL_MASK >> ((ch) - 1)) & 1u))

/* ── Voltage Thresholds (millivolts) ──────────────────────────── */

/* Calibrated 2026-08-19 against a DVM at the board terminals across
 * 4.56-12.92 V, mapping raw counts through the chip's own adc_cali curve.
 * Fitted 4.3148 vs the 4.3 nominal — a 0.34 % difference, i.e. the divider
 * resistors are within tolerance and were never the error source.
 * Residual error in the 8.7-12.9 V operating band is under 90 mV (0.7 %),
 * dominated by ADC compression near full scale, not by this ratio.
 * See Development_Progress "Battery Divider Calibration". */
#define BASE_VBAT_DIVIDER_RATIO        4.3148f
#define BASE_VBAT_MIN_ARM_MV           10500
#define BASE_VBAT_CRITICAL_MV          9000

/* Calibrated 2026-08-19 against a DVM at the board terminals across
 * 4.94-8.56 V, after the bug #21 zener was removed (with it fitted the
 * implied ratio drifted 30 % and no calibration was possible). Gain-only fit
 * over the operating band, 57 mV worst case. Fitted 2.8211 vs 2.8 nominal —
 * 0.75 %, i.e. the divider resistors are fine.
 * Residual error is ADC compression: a full 2S pack sits at 97 % of the ADC's
 * usable ceiling. See docs/calibration/remote_vbat_2026-08-19.md. */
#define REMOTE_VBAT_DIVIDER_RATIO      2.8211f
/* FSD §5.6.2 production values for the specified 2S pack, restored
 * 2026-08-19 once the sense path was trustworthy (bug #21 zener removed and
 * the divider calibrated). Verified against the calibration data: the
 * firmware under-reads each threshold slightly — 6400→6356, 6600→6561,
 * 7000→6971 — so protection trips early rather than late.
 *
 * NOTE these are 2S-pack values. The remote reads 0 mV on USB power alone
 * (the divider is unfed, not a flat pack) and will sit in ERROR until a pack
 * is connected. That is correct behaviour, not a fault. */
#define REMOTE_VBAT_MIN_ARM_MV         7000
#define REMOTE_VBAT_MIN_OPERATE_MV     6600
#define REMOTE_VBAT_CRITICAL_MV        6400

/* Battery ADC sampling. Each rlc_battery_sample() call takes a burst and
 * keeps its MEDIAN, rather than a single read.
 *
 * Rationale (2026-08-19): during divider calibration a noisy bench supply
 * produced 600-1500 counts of sample spread with individual samples clipping
 * at ADC full scale. A clipped sample can only bias a mean UPWARD — the
 * dangerous direction for a battery threshold, because it makes a flat pack
 * look healthy. A median discards spikes and clipping outright; measured
 * side by side on that supply it was over twice as stable as the mean.
 *
 * Odd count so the median is an exact sample, not an interpolation. The 1 ms
 * spacing spreads the burst across ~33 ms so samples decorrelate from supply
 * ripple instead of all landing in the same part of the cycle. Called at 1 Hz
 * from tasks that feed a 5 s watchdog, so the cost is immaterial. */
#define VBAT_BURST_SAMPLES             33
#define VBAT_BURST_GAP_MS              1
#define VBAT_RAIL_COUNTS               4093   /* at/above this = clipped */

/* Full-charge endpoints for the display battery gauges (FSD §10.2.2),
 * production values: REMOTE 8400 (2S LiPo), BASE 12600 (3S LiPo). */
#define REMOTE_VBAT_FULL_MV            8400
#define BASE_VBAT_FULL_MV              12600

/* ── Status Colours (HTML #RRGGBB) ────────────────────────────── */

/* Igniter continuity status. These drive the 8-pixel NeoPixel strip on BOTH
 * units (one pixel per channel) and the remote display's channel grid, so
 * pad and handheld always agree. Edit here to restyle all three.
 *
 * NOTE: this palette (green/red) deviates from FSD §10.2.0, which specifies
 * blue for GOOD to avoid red-green ambiguity for colour-blind operators.
 * Shape coding on the display (circle/triangle/ring/diamond) still carries
 * the meaning independently of colour. */
#define RLC_COLOR_CONT_GOOD        0x006400   /* darkgreen  — 0.5-20 Ω, normal igniter */
#define RLC_COLOR_CONT_MARGINAL    0x90EE90   /* lightgreen — 20-500 Ω, high resistance */
#define RLC_COLOR_CONT_OPEN        0xFFFF00   /* yellow     — >500 Ω or no igniter */
#define RLC_COLOR_CONT_SHORT       0xFF0000   /* red        — <0.5 Ω, wiring fault */

/* Alarm-wink colours. Deliberately chosen to be unmistakable for any
 * continuity colour above, so a wink can never be read as a channel state. */
#define RLC_COLOR_ALARM_LINK       0xFFB400   /* amber   — link lost */
#define RLC_COLOR_ALARM_BATT       0xFF00FF   /* magenta — battery low/critical */
#define RLC_COLOR_ALARM_FAULT      0xFFFFFF   /* white   — arm-sense fault */

/* Shown as a left-to-right chase before the first continuity sweep is valid */
#define RLC_COLOR_STRIP_BOOT       0x00FFFF   /* cyan */

/* Unpack an 0xRRGGBB constant */
#define RLC_COLOR_R(c)  ((uint8_t)(((c) >> 16) & 0xFF))
#define RLC_COLOR_G(c)  ((uint8_t)(((c) >>  8) & 0xFF))
#define RLC_COLOR_B(c)  ((uint8_t)( (c)        & 0xFF))

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
#define BASE_MAC_ADDR    { 0x44, 0x1B, 0xF6, 0x81, 0xF1, 0x70 }  /* chip #4 (2026-08-20, ex-remote #1 board); #3 44:1B:F6:D4:0D:68 (3.68 V rail), #2 44:1B:F6:81:FA:F8 & #1 94:A9:90:31:18:38 destroyed */
#define REMOTE_MAC_ADDR  { 0xAC, 0xA7, 0x04, 0xE2, 0xF2, 0x8C }  /* remote chip #2 (2026-07-22) */

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

/* Master brightness, 0–255. Split per unit: the pad strip may need to be
 * brighter for daylight visibility, while the handheld runs off a smaller
 * 2S pack. 8 pixels draw roughly 55 mA at 30/255, scaling linearly. */
#define RGB_LED_BRIGHTNESS_BASE    30
#define RGB_LED_BRIGHTNESS_REMOTE  30

/* Strip orientation, per unit — the two strips are wired data-in at opposite
 * ends (verified 2026-08-19 with tools/strip-diag, single-pixel walk):
 *
 *   Base   — DIN at the CHANNEL-1 end: channel N is pixel N-1, on-board LED
 *            (parallel on the same data line) mirrors channel 1.
 *   Remote — DIN at the CHANNEL-8 end: channel N is pixel 7-(N-1), on-board
 *            LED mirrors channel 8.
 *
 * Set to 1 when data-in is at the channel-8 end. */
#ifdef CONFIG_RLC_UNIT_BASE
#define RLC_STRIP_REVERSED         0
#else
#define RLC_STRIP_REVERSED         1
#endif

/* Alarm wink: a brief full-strip flash over the continuity map, so igniter
 * status stays readable while the alarm stays unmissable. */
#define RLC_STRIP_ALARM_WINK_MS    300
#define RLC_STRIP_ALARM_PERIOD_MS  3000

/* Dim/pulse depths, percent of RGB_LED_BRIGHTNESS_* */
#define RLC_STRIP_STALE_DIM_PCT    10   /* remote: cached STATUS_UPDATE is stale */
#define RLC_STRIP_ERROR_DIM_PCT    20   /* map during the ERROR flash gap */
#define RLC_STRIP_BREATHE_LOW_PCT  25   /* key-ON / arm-ready breathing trough */
#define RLC_STRIP_CURSOR_LOW_PCT   40   /* remote channel-cursor pulse trough */

/* Animation periods */
#define RLC_STRIP_BREATHE_MS       250
#define RLC_STRIP_CURSOR_MS        500
#define RLC_STRIP_CHASE_MS         120  /* boot chase step */
#define RLC_STRIP_FRAME_MS         50   /* led_task tick */

/* ── Display Configuration (Remote only) ──────────────────────── */

#define DISPLAY_SPI_HOST           SPI2_HOST
#define DISPLAY_SPI_CLOCK_HZ      20000000
#define DISPLAY_WIDTH              480
#define DISPLAY_HEIGHT             320
#define DISPLAY_ROTATION           1
