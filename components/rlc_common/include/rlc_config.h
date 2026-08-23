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
/* 4.5: grace beyond the pulse duration before the FSM's max-duration
 * backstop fires (GPTimer notification normally arrives within ms). */
#define FIRE_PULSE_BACKSTOP_MARGIN_MS  250
#define POST_FIRE_COOLDOWN_MS          2000

#define ARM_TIMEOUT_MS                 10000

/* FSD §7.2.2: how long the base waits for the arm relay's sense feedback to
 * go HIGH after energising the coil, before NACKing ARM_SENSE_FAULT. The
 * wait is non-blocking (M1) — safety events are still processed inside it.
 * m12: was a bare 200 in rlc_base_fsm.c while every sibling timeout lived
 * here. */
#define ARM_SENSE_VERIFY_TIMEOUT_MS    200

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
 * Widened to 0xFF on 2026-08-23 by explicit operator decision, once the
 * protection BOM was complete on all eight channels: RC snubber across every
 * channel relay contact and the arm relay, 2x 1N5819 (to GND and to +3V3) on
 * every sense pin, and a 217 Ω sense-branch series resistor per channel that
 * limits an arc into the 3V3-side clamp to ~41 mA and holds the pin at ~3.55 V,
 * inside the ESP32-S3's 3.6 V absolute maximum. The TL431 rail clamp is still
 * unfitted (bug #24); with the 217 Ω in place it protects against a
 * multi-channel fault rather than a single-channel one. */
#define FIRE_PROTECTED_CHANNEL_MASK    0xFF  /* all 8 channels (2026-08-23) */

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
#define RLC_COLOR_CONT_CONNECTED   0x006400   /* darkgreen  — low-resistance path present */
#define RLC_COLOR_CONT_MARGINAL    0x90EE90   /* lightgreen — high resistance, may not fire */
#define RLC_COLOR_CONT_OPEN        0xFFFF00   /* yellow     — no path, blocks arming */
/* DEPRECATED with the CONT_SHORT band (2026-08-21). Retained only so a
 * value 3 from a pre-merge peer still resolves to a colour. */
#define RLC_COLOR_CONT_SHORT       0xFF0000   /* red — no longer produced */

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
/* Sense-branch series resistor, fitted on all 8 channels 2026-08-23 (bug #18
 * gap 1/gap 2 — it limits an arc into the 3V3-side 1N5819 to ~41 mA and keeps
 * the pin inside 3.6 V during a fault). It sits between the ADC pin/clamp
 * junction and the relay NC contact, so it is *in the sense current path* and
 * offsets every reading by ~204 mV. The thresholds below are re-derived for it;
 * changing this part means re-deriving them again. Marked 217 Ω; measured
 * 216-219 Ω back-calculated from three known loads. */
#define CONT_R_SENSE_OHM               217
/* ADC attenuation for the continuity inputs.
 *
 * 0 dB (0-950 mV full scale, ~0.23 mV/LSB) rather than the 12 dB used
 * elsewhere. An earlier 0 dB trial appeared to change nothing, but that test
 * was invalid — a ~64 ohm fault in the continuity return was pinning every
 * channel at the time. With the ground repaired, 12 dB measured 73.7 mV as
 * raw 70 but collapsed 15.1 mV to raw 1 and 2 mV to raw 0, which is where
 * real igniters live. 0 dB gives ~3.5x the counts per millivolt.
 *
 * An open channel rests at ~3.19 V, far above this range, so the ADC
 * saturates at 4095. That is harmless (well inside the pin's absolute
 * maximum) and unambiguous: saturated means OPEN. CONT_OPEN_UV must
 * therefore stay below full scale. */
#define CONT_ADC_ATTEN                 ADC_ATTEN_DB_0
#define CONT_ADC_FULLSCALE_MV          950

#define CONT_SAMPLE_INTERVAL_MS        100     /* Per-channel ADC interval */

/* Bench diagnostic: interval in ms for the compact per-channel raw ADC line.
 * 0 disables it. A full round-robin sweep takes NUM_CHANNELS x
 * CONT_SAMPLE_INTERVAL_MS (800 ms), so there is nothing to gain below that.
 * Set to 0 for field use — it is noise in the log. */
#define CONT_TRACE_INTERVAL_MS         1000
#define CONT_OVERSAMPLE_COUNT          64      /* ADC samples averaged per reading */

/* Thresholds in microvolts (µV) — multiply ADC millivolts by 1000 */
/* DEPRECATED 2026-08-21 — the SHORT band was merged into CONNECTED and this
 * threshold is no longer referenced. Kept only as a record of the boundary
 * that was attempted. Measured evidence for the merge: at 1 mA a dead short
 * and a 1.5-1.9 ohm igniter differ by 1-1.6 mV, and three experiments on one
 * fixed igniter returned 0.77 / 1.15 / 1.77 ohm. */
#define CONT_SHORT_UV                  500     /* unused */
/* Re-derived 2026-08-23 for CONT_R_SENSE_OHM. V = 3.3 * Rx / (R_REF + Rx),
 * Rx = (R_SENSE + R_ign) ∥ R_PULL. At R_ign = 67 Ω that is 261 mV, up from the
 * 66 mV this threshold held when the sense branch was a plain wire. */
#define CONT_MARGINAL_UV               261000  /* Above = MARGINAL (> ~67 Ω) */
/* 432000 uV is what 500 ohm produces through the 3.3k/100k divider, so this is
 * the ">500 ohm" boundary FSD §5.4.2 documents. It previously sat at 1500000
 * uV — ~2828 ohm, not 500 — and is unreachable at 0 dB attenuation anyway.
 * Adopted 2026-08-21 once the return-path fault was repaired and the readings
 * became trustworthy. OPEN is the only band that blocks arming, so this
 * tightens a safety guard: a 1 kohm connection that will not fire is now
 * refused rather than merely flagged. */
/* 2026-08-23: 432000 -> 586000 µV, the same ~500 Ω boundary shifted by the
 * 217 Ω sense resistor. Full scale (950 mV) is now reached at ~1117 Ω rather
 * than ~1670 Ω; everything above the boundary is OPEN, so that costs nothing. */
#define CONT_OPEN_UV                   586000  /* Above = OPEN (> ~500 Ω) */

/* Hysteresis bands (µV) — prevents oscillation at boundaries */
#define CONT_HYSTERESIS_SHORT_UV       200     /* unused, see CONT_SHORT_UV */
#define CONT_HYSTERESIS_MARGINAL_UV    5000
#define CONT_HYSTERESIS_OPEN_UV        50000

/* ── Rotary Encoder (Remote only, FSD §5.5.1) ─────────────────── */

/* Raw quadrature steps required in the same direction before one channel
 * change is emitted. The spec calls for 3; raised to 4 on 2026-08-20 because
 * selection was overshooting in the field. Direction reversal resets the
 * accumulator, so incidental jitter nets to nothing. */
#define ENC_DIVIDER                    4

/* Lockout after an accepted edge. Short, because the cycle-position decoder
 * already rejects contact bounce (it produces illegal transitions), so this
 * only caps the ISR rate under sustained noise. Even a fast 20 detents/s puts
 * real transitions ~12 ms apart, far outside this window. */
#define ENC_LOCKOUT_US                 2000

/* Rotation sense. Which way a KY-040 counts depends on how A and B are wired
 * to the MCU, so this is a board property, not a decoder property — same
 * reasoning as RLC_STRIP_REVERSED. Set to 1 as built (2026-08-20): without it
 * the channel selection moved opposite to the knob. */
#define ENC_REVERSED                   1

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
