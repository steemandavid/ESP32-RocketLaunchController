#pragma once
#include <stdint.h>

typedef enum {
    CONT_BAND_SHORT    = 0,
    CONT_BAND_GOOD     = 1,
    CONT_BAND_MARGINAL = 2,
    CONT_BAND_OPEN     = 3,
} cont_band_t;

typedef struct {
    int         raw;
    int32_t     uv;
    cont_band_t band;
} cont_reading_t;

void            hw_continuity_init(void);
cont_reading_t  cont_read(int ch);                         /* ch: 1–8 */
void            cont_read_raw_stats(int ch, int n_samples,
                                    int *out_mean, int *out_min,
                                    int *out_max, int *out_stddev);
void            cont_mosfet_set(int active);
const char     *cont_band_str(cont_band_t band);
