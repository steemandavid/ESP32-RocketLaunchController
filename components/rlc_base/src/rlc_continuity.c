/**
 * RLC Continuity Sensing Module (Base Unit)
 *
 * 8-channel ADC1 continuity monitoring with 64-sample burst oversampling,
 * three-band classification (CONNECTED/MARGINAL/OPEN) with hysteresis.
 *
 * FSD §5.4.2: Each channel uses the SPDT relay NC contact to route the
 * igniter to the sense circuit when de-energised. ADC pins: GPIO 2,10,4-9.
 *
 * FSD §7.3.1: Round-robin sampling, one channel per 100 ms, 64 samples
 * averaged per reading. Band changes trigger event-driven STATUS_UPDATE.
 */

#include "rlc_continuity.h"
#include "rlc_continuity_class.h"
#include "rlc_battery.h"
#include "rlc_config.h"
#include "rlc_watchdog.h"
#include "pin_config.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "rlc_cont";

/* ── Channel GPIO-to-ADC mapping (FSD Appendix C.1) ────────────── */

static const int s_gpio[NUM_CHANNELS] = {
    PIN_CONT_CH1, PIN_CONT_CH2, PIN_CONT_CH3, PIN_CONT_CH4,
    PIN_CONT_CH5, PIN_CONT_CH6, PIN_CONT_CH7, PIN_CONT_CH8,
};

static adc_channel_t s_adc_chan[NUM_CHANNELS];
static adc_cali_handle_t s_cali_handles[NUM_CHANNELS];
/* 2.3: true only for channels whose GPIO mapped to ADC1 and whose channel
 * config succeeded in continuity_init(). Unconfigured channels are never
 * sampled — adc_oneshot_read on a stale s_adc_chan[] entry would silently
 * sample the wrong ADC input. */
static bool s_chan_configured[NUM_CHANNELS] = { false };

/* ── Per-channel state ─────────────────────────────────────────── */

/* m11: volatile, like its s_uv/s_raw siblings below. Written by
 * continuity_task, read by the FSM task (guard 2 in guard_arm), the status
 * task and the housekeeping loop. Benign in practice, but this is a value
 * that gates arming — it should not be a compiler's to cache. */
static volatile rlc_continuity_band_t s_bands[NUM_CHANNELS] = {
    CONT_OPEN, CONT_OPEN, CONT_OPEN, CONT_OPEN,
    CONT_OPEN, CONT_OPEN, CONT_OPEN, CONT_OPEN,
};
static bool s_band_initialized[NUM_CHANNELS] = { false };

/* Last sampled sense voltage per channel. Retained so the raw measurement can
 * be inspected without waiting for a band change — the band alone hides how
 * close a channel sits to a threshold, which is what matters when judging a
 * marginal igniter or checking the thresholds themselves. */
static volatile int32_t s_uv[NUM_CHANNELS] = { 0 };
static volatile int32_t s_raw[NUM_CHANNELS] = { 0 };   /* pre-calibration counts */

/* Callback on band change */
static void (*s_on_change_cb)(void) = NULL;

/* ── Band classification ───────────────────────────────────────── */

/* The classifier lives in rlc_common (rlc_continuity_class.c) so the boot
 * self-test exercises the production functions rather than a copy
 * (Phase-2 M2 / review 2.5). */

/* ── ADC burst sampling ────────────────────────────────────────── */

/* 2.3: sentinel returned by sample_channel() when the channel is not
 * configurable or the ADC read fails. A real reading is always >= 0, so -1
 * is unambiguous. The task fails safe to CONT_OPEN on this value — uv = 0
 * would classify as CONNECTED, the only arming-permitting band, i.e. the
 * exact opposite of the safe direction. */
#define CONT_SAMPLE_FAILED (-1)

static int32_t sample_channel(int ch_idx)
{
    if (!s_chan_configured[ch_idx]) {
        return CONT_SAMPLE_FAILED;
    }

    adc_oneshot_unit_handle_t adc_handle = rlc_battery_get_adc_handle();
    if (adc_handle == NULL) {
        return CONT_SAMPLE_FAILED;
    }

    int32_t sum = 0;
    for (int i = 0; i < CONT_OVERSAMPLE_COUNT; i++) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(adc_handle, s_adc_chan[ch_idx], &raw);
        if (ret != ESP_OK) {
            return CONT_SAMPLE_FAILED;
        }
        sum += raw;
    }

    int32_t avg_raw = sum / CONT_OVERSAMPLE_COUNT;

    /* Convert to millivolts using calibration (if available and valid).
     * Use raw conversion as fallback — band classification has wide margins
     * and does not require calibrated voltages. */
    int voltage_mv = 0;
    if (s_cali_handles[ch_idx] != NULL) {
        esp_err_t ret = adc_cali_raw_to_voltage(s_cali_handles[ch_idx], avg_raw, &voltage_mv);
        if (ret != ESP_OK) {
            voltage_mv = (avg_raw * CONT_ADC_FULLSCALE_MV) / 4095;
        }
    } else {
        voltage_mv = (avg_raw * CONT_ADC_FULLSCALE_MV) / 4095;
    }

    s_raw[ch_idx] = avg_raw;

    /* Convert millivolts to microvolts for threshold comparison */
    return (int32_t)voltage_mv * 1000;
}

/* ── Continuity task ───────────────────────────────────────────── */

/* 2.3: latched per-channel ADC-failure state. Log once on entering failure
 * and once on recovery — a persistent failure fires every 100 ms per
 * channel and would otherwise drown the log. */
static bool s_adc_failed[NUM_CHANNELS] = { false };

static void continuity_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);   /* 5.11: self-register (see rlc_base_battery.c) */
    int current_ch = 0;

    ESP_LOGI(TAG, "continuity task started — sampling %d channels", NUM_CHANNELS);

    while (1) {
        int32_t uv = sample_channel(current_ch);

        if (uv == CONT_SAMPLE_FAILED) {
            /* 2.3: fail safe. Classify OPEN directly — never run the
             * classifier on a fabricated reading, and never let hysteresis
             * hold a stale CONNECTED band across an ADC failure (guard 2
             * blocks arming on OPEN only). */
            if (!s_adc_failed[current_ch]) {
                ESP_LOGE(TAG, "ch%d (GPIO %d): ADC read failed — failing safe to OPEN",
                         current_ch + 1, s_gpio[current_ch]);
                s_adc_failed[current_ch] = true;
            }
            s_uv[current_ch] = 0;
            if (s_bands[current_ch] != CONT_OPEN) {
                s_bands[current_ch] = CONT_OPEN;
                if (s_on_change_cb) {
                    s_on_change_cb();
                }
            }
        } else {
            rlc_continuity_band_t new_band;

            if (s_adc_failed[current_ch]) {
                ESP_LOGW(TAG, "ch%d: ADC recovered — re-initialising band",
                         current_ch + 1);
                s_adc_failed[current_ch] = false;
                /* Restart classification from a clean read; a band held at
                 * OPEN through the failure must not need hysteresis margins
                 * to leave OPEN once readings are real again. */
                s_band_initialized[current_ch] = false;
            }

            s_uv[current_ch] = uv;

            if (!s_band_initialized[current_ch]) {
                new_band = rlc_continuity_classify_initial(uv);
                s_band_initialized[current_ch] = true;
            } else {
                new_band = rlc_continuity_classify_hysteresis(uv, s_bands[current_ch]);
            }

            if (new_band != s_bands[current_ch]) {
                ESP_LOGI(TAG, "ch%d: band %d -> %d (%ld uV)", current_ch + 1,
                         s_bands[current_ch], new_band, (long)uv);
                s_bands[current_ch] = new_band;

                /* Notify status update task */
                if (s_on_change_cb) {
                    s_on_change_cb();
                }
            }
        }

        /* Advance to next channel (round-robin) */
        current_ch = (current_ch + 1) % NUM_CHANNELS;

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(CONT_SAMPLE_INTERVAL_MS));
    }
}

/* ── Public API ────────────────────────────────────────────────── */

void continuity_init(void)
{
    adc_oneshot_unit_handle_t adc_handle = rlc_battery_get_adc_handle();
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "Battery ADC not initialised — cannot configure continuity");
        return;
    }

    /* Map each GPIO to its ADC1 channel and configure it */
    for (int i = 0; i < NUM_CHANNELS; i++) {
        adc_unit_t unit;
        esp_err_t ret = adc_oneshot_io_to_channel(s_gpio[i], &unit, &s_adc_chan[i]);
        if (ret != ESP_OK || unit != ADC_UNIT_1) {
            ESP_LOGE(TAG, "GPIO %d is not a valid ADC1 pin", s_gpio[i]);
            continue;
        }

        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten    = CONT_ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ret = adc_oneshot_config_channel(adc_handle, s_adc_chan[i], &chan_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to config ADC channel for GPIO %d", s_gpio[i]);
            continue;
        }

        /* 2.3: only now is s_adc_chan[i] guaranteed to mean what it says. */
        s_chan_configured[i] = true;

        /* Calibration re-enabled 2026-08-21 (bug #26). It had been disabled
         * with a note that curve fitting produced corrupted handles and
         * LoadProhibited panics, and that "raw conversion is sufficient
         * because the thresholds have wide margins". Bench measurement
         * disproved that second claim: uncalibrated, a 0 V input reads raw
         * ~458, i.e. +369 mV of offset, which is 5.6x the entire GOOD band.
         * Real igniters were misclassified as a result.
         *
         * Guarded defensively — a non-OK return or a NULL handle leaves the
         * channel on the raw fallback rather than trusting a bad handle. */
        adc_cali_handle_t h = NULL;
        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id  = ADC_UNIT_1,
            .chan     = s_adc_chan[i],
            .atten    = CONT_ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_12,
        };
        esp_err_t cret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &h);
        if (cret == ESP_OK && h != NULL) {
            s_cali_handles[i] = h;
        } else {
            ESP_LOGW(TAG, "ch%d: ADC calibration unavailable (%s) — raw fallback",
                     i + 1, esp_err_to_name(cret));
            s_cali_handles[i] = NULL;
        }
    }

    ESP_LOGI(TAG, "continuity ADC initialised (%d channels)", NUM_CHANNELS);
}

void continuity_start_task(void)
{
    /* m9: checked. A silent failure here means every channel keeps its
     * initial CONT_OPEN and arming is refused forever with NO_CONTINUITY —
     * safe, but indistinguishable from a wiring fault without this log. */
    if (xTaskCreatePinnedToCore(
            continuity_task,
            "continuity_task",
            4096,
            NULL,
            5,              /* Priority 5 (FSD §9.10) */
            NULL,
            0               /* Core 0 */
        ) != pdPASS) {
        ESP_LOGE(TAG, "continuity task create FAILED — no continuity sensing");
        return;
    }
    ESP_LOGI(TAG, "task started (prio 5, core 0, 4096 stack)");
}

uint16_t continuity_get_bands(void)
{
    uint16_t packed = 0;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        packed |= ((uint16_t)s_bands[i] << (i * 2));
    }
    return packed;
}

rlc_continuity_band_t continuity_get_channel(uint8_t ch)
{
    if (ch < 1 || ch > NUM_CHANNELS) return CONT_OPEN;
    return s_bands[ch - 1];
}

void continuity_register_change_cb(void (*cb)(void))
{
    s_on_change_cb = cb;
}

int32_t continuity_get_uv(uint8_t ch)
{
    if (ch < 1 || ch > NUM_CHANNELS) return 0;
    return s_uv[ch - 1];
}

int32_t continuity_get_raw(uint8_t ch)
{
    if (ch < 1 || ch > NUM_CHANNELS) return 0;
    return s_raw[ch - 1];
}
