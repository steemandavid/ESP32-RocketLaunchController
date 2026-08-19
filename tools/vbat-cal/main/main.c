/**
 * RLC battery-divider calibration capture.
 *
 * Reads the VBAT sense pin (GPIO 1, ADC1, 12-bit, 12 dB) using exactly the
 * same configuration as rlc_battery.c, and streams averaged readings as CSV
 * so a host can fit the divider ratio against a known reference voltage.
 *
 * Reports BOTH the raw ADC count and the calibrated millivolts. That
 * separation matters: a wrong divider ratio shows up as a constant
 * proportional error, whereas poor ADC calibration shows up as curvature in
 * the residuals. With only calibrated mV the two are indistinguishable.
 *
 * Procedure: feed the unit's battery input from a bench supply, hold each
 * setpoint ~5 s, and record this stream. Plateaus are then matched against
 * the reference voltages measured at the board terminals.
 *
 *   ==> DO NOT EXCEED the per-unit input limits printed in the banner. <==
 *   The dividers differ per unit; feeding base voltages into the remote puts
 *   >4 V on a 3.3 V pin and destroys the chip (bug #18 failure class).
 *
 * Build/flash:  idf.py -C tools/vbat-cal -p <by-id> flash monitor
 * Console is USB-Serial/JTAG (native USB port).
 */

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "vbat_cal";

/* Must match rlc_battery.c / pin_config.h */
#define VBAT_GPIO        1
#define BURST            64      /* samples averaged per emitted record */
#define PERIOD_MS        500

/* Nominal ratios from rlc_config.h, printed for orientation only — the
 * calibration exists precisely because these are the suspect numbers. */
#define RATIO_BASE       4.3f
#define RATIO_REMOTE     2.8f

static adc_oneshot_unit_handle_t s_adc  = NULL;
static adc_cali_handle_t         s_cali = NULL;
static adc_channel_t             s_chan;

static void adc_setup(void)
{
    adc_unit_t unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(VBAT_GPIO, &unit, &s_chan));

    adc_oneshot_unit_init_cfg_t init_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_chan, &chan_cfg));

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cc = {
        .unit_id = ADC_UNIT_1, .chan = s_chan,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cc, &s_cali) == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration: CURVE FITTING (eFuse)");
        return;
    }
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t lc = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_line_fitting(&lc, &s_cali) == ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration: LINE FITTING (less accurate)");
        return;
    }
#endif
    ESP_LOGE(TAG, "ADC calibration UNAVAILABLE — mV column is an estimate only");
    s_cali = NULL;
}

void app_main(void)
{
    adc_setup();

    printf("\n");
    printf("=== RLC VBAT divider calibration ===\n");
    printf("Pin GPIO %d, ADC1, 12-bit, 12 dB attenuation (same as rlc_battery.c)\n",
           VBAT_GPIO);
    printf("\n");
    printf("INPUT LIMITS — exceeding these destroys the ESP32:\n");
    printf("  BASE   (3S, nominal divider %.2f:1): sweep 8.0-12.6 V, NEVER above 13.0 V\n",
           (double)RATIO_BASE);
    printf("  REMOTE (2S, nominal divider %.2f:1): sweep 5.5-8.4 V,  NEVER above 8.6 V\n",
           (double)RATIO_REMOTE);
    printf("  Feeding BASE voltages into the REMOTE puts >4 V on a 3.3 V pin.\n");
    printf("\n");
    printf("Hold each setpoint ~5 s. Measure the reference at the board terminals.\n");
    printf("Columns: seq, raw_avg, raw_min, raw_max, adc_mv, vbat_if_base, vbat_if_remote\n");
    printf("CSV_HEADER,seq,raw_avg,raw_min,raw_max,adc_mv,vbat_base_mv,vbat_remote_mv\n");

    uint32_t seq = 0;
    while (1) {
        uint32_t sum = 0;
        int rmin = 1 << 30, rmax = -1;

        for (int i = 0; i < BURST; i++) {
            int raw = 0;
            if (adc_oneshot_read(s_adc, s_chan, &raw) != ESP_OK) continue;
            sum += (uint32_t)raw;
            if (raw < rmin) rmin = raw;
            if (raw > rmax) rmax = raw;
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        int raw_avg = (int)(sum / BURST);
        int adc_mv  = 0;
        if (s_cali) adc_cali_raw_to_voltage(s_cali, raw_avg, &adc_mv);
        else        adc_mv = (raw_avg * 3300) / 4095;

        /* Both interpretations, so one binary serves either unit. */
        printf("CSV,%lu,%d,%d,%d,%d,%d,%d\n",
               (unsigned long)seq++, raw_avg, rmin, rmax, adc_mv,
               (int)(adc_mv * RATIO_BASE), (int)(adc_mv * RATIO_REMOTE));
        fflush(stdout);

        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}
