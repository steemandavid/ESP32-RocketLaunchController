/**
 * RLC Continuity Sensing Module (Base Unit)
 *
 * 8-channel ADC1 continuity monitoring with 64-sample burst oversampling,
 * 4-band classification (SHORT/GOOD/MARGINAL/OPEN) with hysteresis.
 *
 * FSD §5.4.2: Each channel uses the SPDT relay NC contact to route the
 * igniter to the sense circuit when de-energised. ADC pins: GPIO 2,10,4-9.
 *
 * FSD §7.3.1: Round-robin sampling, one channel per 100 ms, 64 samples
 * averaged per reading. Band changes trigger event-driven STATUS_UPDATE.
 */

#include "rlc_continuity.h"
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

/* ── Per-channel state ─────────────────────────────────────────── */

static rlc_continuity_band_t s_bands[NUM_CHANNELS] = {
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

/* ── Band classification with hysteresis (FSD §5.4.2, §14.5) ─── */

static rlc_continuity_band_t classify_initial(int32_t uv)
{
    /* First reading — simple thresholds, no hysteresis */
    if (uv < CONT_SHORT_UV)      return CONT_SHORT;
    if (uv < CONT_MARGINAL_UV)   return CONT_GOOD;
    if (uv < CONT_OPEN_UV)       return CONT_MARGINAL;
    return CONT_OPEN;
}

static rlc_continuity_band_t classify_with_hysteresis(int32_t uv,
                                                       rlc_continuity_band_t current)
{
    switch (current) {
    case CONT_SHORT:
        /* Stay SHORT unless voltage rises above boundary + hysteresis */
        if (uv > CONT_SHORT_UV + CONT_HYSTERESIS_SHORT_UV) {
            /* Crossed into GOOD range */
            if (uv < CONT_MARGINAL_UV) return CONT_GOOD;
            if (uv < CONT_OPEN_UV)     return CONT_MARGINAL;
            return CONT_OPEN;
        }
        return CONT_SHORT;

    case CONT_GOOD:
        /* Down to SHORT? */
        if (uv < CONT_SHORT_UV - CONT_HYSTERESIS_SHORT_UV)
            return CONT_SHORT;
        /* Up to MARGINAL? */
        if (uv > CONT_MARGINAL_UV + CONT_HYSTERESIS_MARGINAL_UV) {
            if (uv < CONT_OPEN_UV) return CONT_MARGINAL;
            return CONT_OPEN;
        }
        return CONT_GOOD;

    case CONT_MARGINAL:
        /* Down to GOOD? */
        if (uv < CONT_MARGINAL_UV - CONT_HYSTERESIS_MARGINAL_UV) {
            if (uv < CONT_SHORT_UV) return CONT_SHORT;
            return CONT_GOOD;
        }
        /* Up to OPEN? */
        if (uv > CONT_OPEN_UV + CONT_HYSTERESIS_OPEN_UV)
            return CONT_OPEN;
        return CONT_MARGINAL;

    case CONT_OPEN:
        /* Stay OPEN unless voltage drops below boundary - hysteresis */
        if (uv < CONT_OPEN_UV - CONT_HYSTERESIS_OPEN_UV) {
            if (uv < CONT_MARGINAL_UV) {
                if (uv < CONT_SHORT_UV) return CONT_SHORT;
                return CONT_GOOD;
            }
            return CONT_MARGINAL;
        }
        return CONT_OPEN;
    }

    return CONT_OPEN;  /* Should never reach here */
}

/* ── ADC burst sampling ────────────────────────────────────────── */

static int32_t sample_channel(int ch_idx)
{
    adc_oneshot_unit_handle_t adc_handle = rlc_battery_get_adc_handle();
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "ADC handle not available");
        return 0;
    }

    int32_t sum = 0;
    for (int i = 0; i < CONT_OVERSAMPLE_COUNT; i++) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(adc_handle, s_adc_chan[ch_idx], &raw);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "ADC read failed on ch%d (GPIO %d)", ch_idx + 1, s_gpio[ch_idx]);
            return 0;
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

static void continuity_task(void *arg)
{
    (void)arg;
    int current_ch = 0;

    ESP_LOGI(TAG, "continuity task started — sampling %d channels", NUM_CHANNELS);

    while (1) {
        int32_t uv = sample_channel(current_ch);
        rlc_continuity_band_t new_band;

        s_uv[current_ch] = uv;

        if (!s_band_initialized[current_ch]) {
            new_band = classify_initial(uv);
            s_band_initialized[current_ch] = true;
        } else {
            new_band = classify_with_hysteresis(uv, s_bands[current_ch]);
        }

        if (new_band != s_bands[current_ch]) {
            ESP_LOGI(TAG, "ch%d: band %d -> %d (%ld uV)", current_ch + 1,
                     s_bands[current_ch], new_band, uv);
            s_bands[current_ch] = new_band;

            /* Notify status update task */
            if (s_on_change_cb) {
                s_on_change_cb();
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

        /* Calibration disabled for continuity channels.
         * The ESP-IDF adc_cali_create_scheme_curve_fitting produces corrupted
         * handles for some ADC1 channels on ESP32-S3, causing LoadProhibited
         * panics in adc_cali_raw_to_voltage. Raw conversion is sufficient for
         * band classification (SHORT/GOOD/MARGINAL/OPEN) which has wide
         * threshold margins. */
        s_cali_handles[i] = NULL;
    }

    ESP_LOGI(TAG, "continuity ADC initialised (%d channels)", NUM_CHANNELS);
}

void continuity_start_task(void)
{
    TaskHandle_t handle;
    xTaskCreatePinnedToCore(
        continuity_task,
        "continuity_task",
        4096,
        NULL,
        5,              /* Priority 5 (FSD §9.10) */
        &handle,
        0               /* Core 0 */
    );
    rlc_watchdog_add_task(handle);
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
