/**
 * RLC battery-divider calibration readout.
 *
 * Reads the VBAT sense pin (GPIO 1, ADC1, 12-bit, 12 dB) using exactly the
 * same configuration as rlc_battery.c and prints a steady, averaged RAW ADC
 * count twice a second.
 *
 * Deliberately dumb: no plateau detection, no gating, no state. The operator
 * sweeps the bench supply across the range, reads the reference voltage from
 * a DVM at the board terminals, and notes the raw count shown here. The
 * pairs are fitted afterwards by tools/vbat_fit.py --pairs.
 *
 * Raw counts are the primary output. The calibrated pin voltage is shown
 * alongside only as a sanity check: it comes from esp_adc_cal, which is one
 * of the things the calibration is meant to check rather than trust.
 *
 *   ==> INPUT LIMITS — exceeding these destroys the ESP32: <==
 *       BASE   (3S, divider ~4.3:1): sweep 8.0-12.6 V, never above 13.0 V
 *       REMOTE (2S, divider ~2.8:1): sweep 5.5-8.4 V,  never above 8.6 V
 *   Feeding BASE voltages into the REMOTE puts >4 V on a 3.3 V pin.
 *
 * A reading pinned at 4095 means the input is over range: either the supply
 * is too high or the divider is not dividing. Stop and measure the pin.
 *
 * Build/flash, board reached over its NATIVE USB port (console on USB-JTAG):
 *   idf.py -C tools/vbat-cal -p <by-id> flash monitor
 *
 * Board reached over a CH340 UART bridge instead (console on UART0):
 *   rm -f tools/vbat-cal/sdkconfig
 *   idf.py -C tools/vbat-cal -DSDKCONFIG_DEFAULTS=sdkconfig.uart -p <by-id> flash monitor
 *
 * The console must come out the port you are actually connected to, or the
 * readout goes nowhere.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "vbat_cal";

/* Must match rlc_battery.c / pin_config.h */
#define VBAT_GPIO     1
#define BURST         64     /* samples averaged per printed line */
#define PERIOD_MS     500

/* Nominal ratios from rlc_config.h — printed for orientation only. These are
 * the suspect numbers the calibration exists to replace. */
#define RATIO_BASE    4.3f
#define RATIO_REMOTE  2.8f

#define ADC_FULL_SCALE 4095

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
    ESP_LOGE(TAG, "ADC calibration UNAVAILABLE — pin mV column is an estimate");
    s_cali = NULL;
}

void app_main(void)
{
    adc_setup();

    printf("\n=== RLC VBAT raw ADC readout ===\n");
    printf("GPIO %d, ADC1, 12-bit, 12 dB attenuation (same config as rlc_battery.c)\n",
           VBAT_GPIO);
    printf("Averaging %d samples, updating every %d ms.\n", BURST, PERIOD_MS);
    /* Dump this chip's ADC linearisation curve before anything else.
     * adc_cali_raw_to_voltage() is a pure function of the raw count for a
     * given chip and attenuation, so the whole table can be emitted with no
     * external voltage applied. Captured alongside a sweep it lets the host
     * separate ADC non-linearity from the divider ratio, which is impossible
     * from raw counts alone. Step 4 is far finer than the curve's curvature. */
    printf("ADCMAP_BEGIN,raw,mv\n");
    for (int r = 0; r <= ADC_FULL_SCALE; r += 4) {
        int mv = 0;
        if (s_cali) adc_cali_raw_to_voltage(s_cali, r, &mv);
        else        mv = (r * 3300) / ADC_FULL_SCALE;
        printf("ADCMAP,%d,%d\n", r, mv);
    }
    printf("ADCMAP_END\n");
    fflush(stdout);

    printf("\nSweep the supply and note (DVM volts -> raw). Raw is the number that matters.\n");
    printf("LIMITS: base never above 13.0 V, remote never above 8.6 V.\n");
    printf("A reading pinned at %d is OVER RANGE, not a measurement.\n\n", ADC_FULL_SCALE);

    while (1) {
        uint32_t sum = 0;
        int rmin = ADC_FULL_SCALE + 1, rmax = -1, got = 0;

        for (int i = 0; i < BURST; i++) {
            int raw = 0;
            if (adc_oneshot_read(s_adc, s_chan, &raw) != ESP_OK) continue;
            sum += (uint32_t)raw;
            if (raw < rmin) rmin = raw;
            if (raw > rmax) rmax = raw;
            got++;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (!got) { ESP_LOGW(TAG, "no ADC samples"); continue; }

        int raw_avg = (int)(sum / (uint32_t)got);
        int pin_mv  = 0;
        if (s_cali) adc_cali_raw_to_voltage(s_cali, raw_avg, &pin_mv);
        else        pin_mv = (raw_avg * 3300) / ADC_FULL_SCALE;

        printf("raw %4d   (min %4d  max %4d)   pin %4d mV   "
               "vbat@%.1f = %5d mV   vbat@%.1f = %5d mV%s\n",
               raw_avg, rmin, rmax, pin_mv,
               (double)RATIO_BASE,   (int)(pin_mv * RATIO_BASE),
               (double)RATIO_REMOTE, (int)(pin_mv * RATIO_REMOTE),
               (raw_avg >= ADC_FULL_SCALE - 5) ? "   ** OVER RANGE **" :
               (raw_avg <= 5)                  ? "   ** near zero **"  : "");
        fflush(stdout);

        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}
