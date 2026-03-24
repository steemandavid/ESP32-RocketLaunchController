#pragma once
#include <stdint.h>

/* Enum values match FSD §5.4.2 wire encoding (2-bit per channel):
 * 00=OPEN, 01=GOOD, 10=MARGINAL, 11=SHORT */
typedef enum {
    CONT_BAND_OPEN     = 0,
    CONT_BAND_GOOD     = 1,
    CONT_BAND_MARGINAL = 2,
    CONT_BAND_SHORT    = 3,
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
const char     *cont_band_str(cont_band_t band);
