#pragma once
#include <stdint.h>
#include "rlc_protocol.h"

/* Band classification is the PRODUCTION enum (FSD §5.4.2 wire encoding,
 * 2 bits per channel): CONT_OPEN=0, CONT_CONNECTED=1, CONT_MARGINAL=2,
 * CONT_SUSPECT=3 (since v1.71; value 3 was SHORT until that band was merged
 * into CONNECTED on 2026-08-21).
 *
 * Until 2026-09-15 this header carried its own CONT_BAND_* enum and the .c
 * classified with thresholds two rebases stale (66 mV / 1500 mV at 12 dB —
 * pre-v1.29 values, before the 0 dB rework, the 217 Ω sense branch and the
 * SUSPECT split). The tool now compiles the production classifier in
 * directly (see main/CMakeLists.txt), so bench readings and field behaviour
 * are the same code, not merely similar numbers. */
typedef rlc_continuity_band_t cont_band_t;

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
