/**
 * RLC Battery Voltage Monitor
 */

#include "rlc_battery.h"
#include "rlc_config.h"

#include <string.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "rlc_battery";

#define ADC_AVG_SAMPLES  8

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static adc_channel_t s_channel;
static float s_divider_ratio = 1.0f;

static uint16_t s_samples[ADC_AVG_SAMPLES];
static int s_sample_idx = 0;
static bool s_buffer_full = false;
static uint16_t s_voltage_mv = 0;

int rlc_battery_init(int gpio_num, float divider_ratio)
{
    s_divider_ratio = divider_ratio;

    /* Map GPIO to ADC1 channel */
    adc_unit_t unit;
    esp_err_t ret = adc_oneshot_io_to_channel(gpio_num, &unit, &s_channel);
    if (ret != ESP_OK || unit != ADC_UNIT_1) {
        ESP_LOGE(TAG, "GPIO %d is not an ADC1 pin", gpio_num);
        return -1;
    }

    /* Configure ADC1 */
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, s_channel, &chan_cfg));

    /* Calibration — use curve fitting or line fitting depending on availability */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = s_channel,
        .atten   = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten   = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali_handle);
#else
    ESP_LOGW(TAG, "No ADC calibration scheme available");
    ret = ESP_ERR_NOT_SUPPORTED;
#endif

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration init failed, using raw values");
        s_cali_handle = NULL;
    }

    memset(s_samples, 0, sizeof(s_samples));
    ESP_LOGI(TAG, "Battery ADC init on GPIO %d, divider ratio %.1f", gpio_num, divider_ratio);
    return 0;
}

uint16_t rlc_battery_sample(void)
{
    int raw = 0;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, s_channel, &raw);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC read failed");
        return s_voltage_mv;
    }

    int voltage_mv = 0;
    if (s_cali_handle) {
        adc_cali_raw_to_voltage(s_cali_handle, raw, &voltage_mv);
    } else {
        /* Fallback: rough estimate for 12-bit, 12dB attenuation (0-3.3V) */
        voltage_mv = (raw * 3300) / 4095;
    }

    /* Store in circular buffer */
    s_samples[s_sample_idx] = (uint16_t)voltage_mv;
    s_sample_idx = (s_sample_idx + 1) % ADC_AVG_SAMPLES;
    if (s_sample_idx == 0) s_buffer_full = true;

    /* Compute average */
    int count = s_buffer_full ? ADC_AVG_SAMPLES : s_sample_idx;
    if (count == 0) count = 1;

    uint32_t sum = 0;
    for (int i = 0; i < count; i++) {
        sum += s_samples[i];
    }
    uint16_t avg_adc_mv = (uint16_t)(sum / count);

    /* Apply divider ratio to get actual battery voltage */
    s_voltage_mv = (uint16_t)(avg_adc_mv * s_divider_ratio);

    return s_voltage_mv;
}

uint16_t rlc_battery_get_voltage_mv(void)
{
    return s_voltage_mv;
}

rlc_battery_status_t rlc_battery_check(uint16_t min_arm_mv,
                                        uint16_t min_operate_mv,
                                        uint16_t critical_mv)
{
    if (s_voltage_mv < critical_mv)    return BATTERY_CRITICAL;
    if (s_voltage_mv < min_operate_mv) return BATTERY_WARNING;
    if (s_voltage_mv < min_arm_mv)     return BATTERY_LOW;
    return BATTERY_OK;
}

adc_oneshot_unit_handle_t rlc_battery_get_adc_handle(void)
{
    return s_adc_handle;
}
