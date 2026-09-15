#include "hw_continuity.h"
#include "pin_config.h"
#include "rlc_config.h"
#include "rlc_continuity_class.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "hw_cont";

static const adc_channel_t s_cont_channels[8] = {
    ADC_CH_CONT1, ADC_CH_CONT2, ADC_CH_CONT3, ADC_CH_CONT4,
    ADC_CH_CONT5, ADC_CH_CONT6, ADC_CH_CONT7, ADC_CH_CONT8
};

/* Per-channel calibration handles at the PRODUCTION attenuation (0 dB).
 *
 * Not the shared g_adc1_cali_handle: that one was created for the battery
 * channel at 12 dB, and a curve-fitting handle is only valid for the
 * (channel, atten) it was calibrated for — running continuity raws through
 * it would produce quietly wrong millivolts. The production firmware keeps
 * its own per-channel set for the same reason (rlc_continuity.c). */
static adc_cali_handle_t s_cali_handles[8] = { 0 };

/* Shared ADC1 unit — battery init creates it, we share. */
extern adc_oneshot_unit_handle_t g_adc1_handle;

void hw_continuity_init(void)
{
    /* 0 dB (0-950 mV full scale), matching CONT_ADC_ATTEN in rlc_config.h —
     * the production thresholds the classifier below applies are derived for
     * this range and make no sense at another attenuation. Until 2026-09-15
     * this tool configured 12 dB and classified with pre-v1.29 thresholds:
     * every band boundary it printed was wrong at the bench. */
    adc_oneshot_chan_cfg_t cfg = {
        .atten    = CONT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    for (int i = 0; i < 8; i++) {
        if (adc_oneshot_config_channel(g_adc1_handle, s_cont_channels[i], &cfg) != ESP_OK) {
            ESP_LOGE(TAG, "ch%d: ADC channel config failed", i + 1);
            continue;
        }

        /* Same guarded calibration as production (bug #26 note in
         * rlc_continuity.c): uncalibrated raws carry +369 mV of offset,
         * which is larger than the whole CONNECTED band. On failure the
         * channel falls back to the linear raw conversion, not to zero. */
        adc_cali_handle_t h = NULL;
        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id  = ADC_UNIT_1,
            .chan     = s_cont_channels[i],
            .atten    = CONT_ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_12,
        };
        if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &h) == ESP_OK && h != NULL) {
            s_cali_handles[i] = h;
        } else {
            ESP_LOGW(TAG, "ch%d: calibration unavailable — raw fallback", i + 1);
            s_cali_handles[i] = NULL;
        }
    }

    ESP_LOGI(TAG, "Continuity ADC initialised: production classifier "
                  "(0 dB, 4 bands incl. SUSPECT)");
}

static int32_t read_uv_with_raw(adc_channel_t ch, int cali_idx, int *out_raw)
{
    int raw_sum = 0;
    int32_t total_mv = 0;

    for (int i = 0; i < CONT_ADC_SAMPLES; i++) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(g_adc1_handle, ch, &raw));
        raw_sum += raw;

        int mv = 0;
        if (s_cali_handles[cali_idx] != NULL) {
            adc_cali_raw_to_voltage(s_cali_handles[cali_idx], raw, &mv);
        } else {
            /* Same fallback conversion the production sampler uses */
            mv = (raw * CONT_ADC_FULLSCALE_MV) / 4095;
        }
        total_mv += mv;
    }
    *out_raw = raw_sum / CONT_ADC_SAMPLES;
    return (total_mv / CONT_ADC_SAMPLES) * 1000;
}

/* The PRODUCTION classifier — rlc_continuity_class.c is compiled into this
 * firmware directly (main/CMakeLists.txt), so there is no local copy to
 * drift. Bench diagnostic reads take the initial (hysteresis-free)
 * classification: a person watching `cont 1` wants what the reading says
 * now, not what a stateful hysteresis filter would settle on. */
static cont_band_t classify(int32_t uv)
{
    return rlc_continuity_classify_initial(uv);
}

cont_reading_t cont_read(int ch)
{
    cont_reading_t r = {0};
    if (ch < 1 || ch > 8) return r;
    r.uv   = read_uv_with_raw(s_cont_channels[ch - 1], ch - 1, &r.raw);
    r.band = classify(r.uv);
    return r;
}

void cont_read_raw_stats(int ch, int n_samples,
                         int *out_mean, int *out_min,
                         int *out_max, int *out_stddev)
{
    if (ch < 1 || ch > 8 || n_samples <= 0) return;
    adc_channel_t adc_ch = s_cont_channels[ch - 1];

    long long sum = 0;
    int mn = INT32_MAX, mx = INT32_MIN;
    int *samples = malloc(n_samples * sizeof(int));
    if (!samples) return;

    for (int i = 0; i < n_samples; i++) {
        int raw = 0;
        adc_oneshot_read(g_adc1_handle, adc_ch, &raw);
        samples[i] = raw;
        sum += raw;
        if (raw < mn) mn = raw;
        if (raw > mx) mx = raw;
    }
    int mean = (int)(sum / n_samples);
    long long var = 0;
    for (int i = 0; i < n_samples; i++) {
        long long d = samples[i] - mean;
        var += d * d;
    }
    free(samples);

    *out_mean   = mean;
    *out_min    = mn;
    *out_max    = mx;
    *out_stddev = (int)sqrt((double)var / n_samples);
}

const char *cont_band_str(cont_band_t band)
{
    /* Production names: CONNECTED, not GOOD — the band means current can
     * flow, not that the igniter is sound (FSD §5.4.2 naming note). */
    switch (band) {
        case CONT_CONNECTED: return "CONNECTED";
        case CONT_MARGINAL:  return "MARGINAL";
        case CONT_SUSPECT:   return "SUSPECT";
        case CONT_OPEN:      return "OPEN";
        default:             return "UNKNOWN";
    }
}
