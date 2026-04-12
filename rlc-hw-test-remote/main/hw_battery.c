#include "hw_battery.h"
#include "pin_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

static const char *TAG = "hw_batt";

static adc_oneshot_unit_handle_t s_adc1_handle  = NULL;
static adc_cali_handle_t         s_adc1_cali     = NULL;
static bool                      s_adc1_cali_ok  = false;

void hw_battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc1_handle));

    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1_handle, ADC_CH_BATT, &ch_cfg));

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = ADC_CH_BATT,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc1_cali) == ESP_OK) {
        s_adc1_cali_ok = true;
        ESP_LOGI(TAG, "ADC calibration: curve fitting");
    }
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!s_adc1_cali_ok) {
        adc_cali_line_fitting_config_t lf_cfg = {
            .unit_id  = ADC_UNIT_1,
            .atten    = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        if (adc_cali_create_scheme_line_fitting(&lf_cfg, &s_adc1_cali) == ESP_OK) {
            s_adc1_cali_ok = true;
            ESP_LOGI(TAG, "ADC calibration: line fitting");
        }
    }
#endif
    if (!s_adc1_cali_ok) {
        ESP_LOGW(TAG, "ADC calibration not available — raw values only");
    }
    ESP_LOGI(TAG, "Battery ADC initialised");
}

batt_reading_t batt_read(void)
{
    batt_reading_t r = {0};
    int sum_raw = 0, sum_mv = 0;
    for (int i = 0; i < BATT_ADC_SAMPLES; i++) {
        int raw = 0;
        adc_oneshot_read(s_adc1_handle, ADC_CH_BATT, &raw);
        sum_raw += raw;
        int mv = 0;
        if (s_adc1_cali_ok) {
            adc_cali_raw_to_voltage(s_adc1_cali, raw, &mv);
        }
        sum_mv += mv;
    }
    r.raw           = sum_raw / BATT_ADC_SAMPLES;
    r.mv_calibrated = sum_mv  / BATT_ADC_SAMPLES;
    r.mv_scaled     = (int32_t)(r.mv_calibrated * BATT_DIVIDER_RATIO);
    return r;
}

void batt_read_raw_stats(int n_samples, int *out_mean, int *out_min, int *out_max, int *out_stddev)
{
    if (n_samples <= 0) return;
    long long sum = 0;
    int mn = INT_MAX, mx = INT_MIN;
    int *buf = malloc(n_samples * sizeof(int));
    if (!buf) return;
    for (int i = 0; i < n_samples; i++) {
        int raw = 0;
        adc_oneshot_read(s_adc1_handle, ADC_CH_BATT, &raw);
        buf[i] = raw;
        sum   += raw;
        if (raw < mn) mn = raw;
        if (raw > mx) mx = raw;
    }
    int mean = (int)(sum / n_samples);
    long long var = 0;
    for (int i = 0; i < n_samples; i++) {
        long long d = buf[i] - mean;
        var += d * d;
    }
    free(buf);
    *out_mean   = mean;
    *out_min    = mn;
    *out_max    = mx;
    *out_stddev = (int)sqrt((double)var / n_samples);
}
