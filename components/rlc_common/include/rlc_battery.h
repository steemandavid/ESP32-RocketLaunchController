/**
 * RLC Battery Voltage Monitor
 *
 * ADC1-based battery voltage measurement with calibration,
 * 8-sample moving average, and threshold detection.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * Battery threshold status.
 */
typedef enum {
    BATTERY_OK,
    BATTERY_LOW,        /* Below MIN_ARM threshold */
    BATTERY_WARNING,    /* Below MIN_OPERATE threshold (remote only) */
    BATTERY_CRITICAL,   /* Below CRITICAL threshold */
} rlc_battery_status_t;

/**
 * Initialise the battery ADC on the specified GPIO (must be ADC1, GPIO 1-10).
 *
 * @param gpio_num      ADC GPIO pin
 * @param divider_ratio Voltage divider ratio (e.g. 4.3 for base, 2.8 for remote)
 * @return              0 on success
 */
int rlc_battery_init(int gpio_num, float divider_ratio);

/**
 * Take a new ADC sample and update the moving average.
 * Call at 1000 ms intervals.
 *
 * @return Current averaged battery voltage in millivolts
 */
uint16_t rlc_battery_sample(void);

/**
 * Get the last computed battery voltage in millivolts.
 */
uint16_t rlc_battery_get_voltage_mv(void);

/**
 * Check battery status against configured thresholds.
 *
 * @param min_arm_mv     Minimum voltage to allow arming
 * @param min_operate_mv Minimum voltage for safe operation (remote only;
 *                       triggers BATTERY_WARNING when below this but above critical)
 * @param critical_mv    Critical low voltage
 * @return               Battery status
 */
rlc_battery_status_t rlc_battery_check(uint16_t min_arm_mv,
                                        uint16_t min_operate_mv,
                                        uint16_t critical_mv);

/**
 * Get the shared ADC1 oneshot handle.
 * Used by the continuity module to configure additional ADC1 channels
 * without creating a second unit handle (ESP-IDF only allows one per unit).
 */
#include "esp_adc/adc_oneshot.h"
adc_oneshot_unit_handle_t rlc_battery_get_adc_handle(void);
