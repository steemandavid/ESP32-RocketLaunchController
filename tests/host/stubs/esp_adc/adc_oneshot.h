#pragma once
#include <stdint.h>
#include "esp_err.h"
#define ESP_ERROR_CHECK(x) ((void)(x))
typedef void* adc_oneshot_unit_handle_t;
typedef int adc_channel_t;
typedef enum { ADC_UNIT_1 = 0, ADC_UNIT_2 = 1 } adc_unit_t;
#define ADC_ATTEN_DB_12 3
#define ADC_BITWIDTH_12 12
typedef struct { adc_unit_t unit_id; } adc_oneshot_unit_init_cfg_t;
typedef struct { int atten; int bitwidth; } adc_oneshot_chan_cfg_t;

/* Scripted ADC: the test supplies the raw values the driver will "read". */
extern int  g_adc_script[512];
extern int  g_adc_script_len;
extern int  g_adc_script_pos;
extern int  g_adc_fail_all;

static inline esp_err_t adc_oneshot_io_to_channel(int gpio, adc_unit_t *u, adc_channel_t *c)
{ (void)gpio; *u = ADC_UNIT_1; *c = 0; return ESP_OK; }
static inline esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *c,
                                             adc_oneshot_unit_handle_t *h)
{ (void)c; *h = (void*)1; return ESP_OK; }
static inline esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t h,
                                                   adc_channel_t c,
                                                   const adc_oneshot_chan_cfg_t *cfg)
{ (void)h;(void)c;(void)cfg; return ESP_OK; }
static inline esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t h, adc_channel_t c, int *out)
{
    (void)h; (void)c;
    if (g_adc_fail_all) return -1;
    if (g_adc_script_len == 0) { *out = 0; return ESP_OK; }
    *out = g_adc_script[g_adc_script_pos % g_adc_script_len];
    g_adc_script_pos++;
    return ESP_OK;
}
