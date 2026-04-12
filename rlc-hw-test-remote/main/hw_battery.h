#pragma once
#include <stdint.h>

typedef struct {
    int     raw;
    int32_t mv_calibrated;   /* ADC pin voltage in mV */
    int32_t mv_scaled;       /* After BATT_DIVIDER_RATIO */
} batt_reading_t;

void            hw_battery_init(void);
batt_reading_t  batt_read(void);
void            batt_read_raw_stats(int n_samples,
                                    int *out_mean, int *out_min,
                                    int *out_max, int *out_stddev);
