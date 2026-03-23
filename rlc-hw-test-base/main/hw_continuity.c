#include "hw_continuity.h"
#include "pin_config.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "hw_cont";

static const adc_channel_t s_cont_channels[8] = {
    ADC_CH_CONT1, ADC_CH_CONT2, ADC_CH_CONT3, ADC_CH_CONT4,
    ADC_CH_CONT5, ADC_CH_CONT6, ADC_CH_CONT7, ADC_CH_CONT8
};

/* Shared ADC1 handle — battery init may already have created it.
 * We use a global so both modules can share the unit handle. */
extern adc_oneshot_unit_handle_t g_adc1_handle;
extern adc_cali_handle_t         g_adc1_cali_handle;
extern bool                      g_adc1_cali_ok;

void hw_continuity_init(void)
{
    /* Configure each continuity ADC channel on the shared ADC1 unit */
    adc_oneshot_chan_cfg_t cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    for (int i = 0; i < 8; i++) {
        ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc1_handle, s_cont_channels[i], &cfg));
    }

    /* Configure continuity MOSFET GPIO */
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << PIN_CONT_MOSFET),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio_cfg);
    /* Drive inactive (HIGH — active LOW device) */
    gpio_set_level(PIN_CONT_MOSFET, PIN_CONT_MOSFET_ACTIVE ? 0 : 1);

    ESP_LOGI(TAG, "Continuity ADC channels initialised");
}

static int32_t read_uv_with_raw(adc_channel_t ch, int *out_raw)
{
    int raw_sum = 0;
    int32_t total_mv = 0;

    for (int i = 0; i < CONT_ADC_SAMPLES; i++) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(g_adc1_handle, ch, &raw));
        raw_sum += raw;
        int mv = 0;
        if (g_adc1_cali_ok) {
            adc_cali_raw_to_voltage(g_adc1_cali_handle, raw, &mv);
        }
        total_mv += mv;
    }
    *out_raw = raw_sum / CONT_ADC_SAMPLES;
    return (total_mv / CONT_ADC_SAMPLES) * 1000;
}

static cont_band_t classify(int32_t uv)
{
    if (uv < CONT_SHORT_UV)    return CONT_BAND_SHORT;
    if (uv < CONT_MARGINAL_UV) return CONT_BAND_GOOD;
    if (uv < CONT_OPEN_UV)     return CONT_BAND_MARGINAL;
    return CONT_BAND_OPEN;
}

cont_reading_t cont_read(int ch)
{
    cont_reading_t r = {0};
    if (ch < 1 || ch > 8) return r;
    r.uv   = read_uv_with_raw(s_cont_channels[ch - 1], &r.raw);
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

void cont_mosfet_set(int active)
{
    int level = active ? PIN_CONT_MOSFET_ACTIVE : !PIN_CONT_MOSFET_ACTIVE;
    gpio_set_level(PIN_CONT_MOSFET, level);
}

const char *cont_band_str(cont_band_t band)
{
    switch (band) {
        case CONT_BAND_SHORT:    return "SHORT";
        case CONT_BAND_GOOD:     return "GOOD";
        case CONT_BAND_MARGINAL: return "MARGINAL";
        case CONT_BAND_OPEN:     return "OPEN";
        default:                 return "UNKNOWN";
    }
}
