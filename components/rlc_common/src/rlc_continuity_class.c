/**
 * RLC Continuity Band Classifier (shared, pure) — see rlc_continuity_class.h.
 *
 * Three bands only. SHORT was merged into CONNECTED on 2026-08-21 — the
 * distinction is below the measurement floor at the specified 1 mA test
 * current, so reporting it would be guessing. CONT_SHORT_UV is consequently
 * unused.
 */

#include "rlc_continuity_class.h"
#include "rlc_config.h"

rlc_continuity_band_t rlc_continuity_classify_initial(int32_t uv)
{
    /* First reading — simple thresholds, no hysteresis */
    if (uv < CONT_MARGINAL_UV)   return CONT_CONNECTED;
    if (uv < CONT_OPEN_UV)       return CONT_MARGINAL;
    return CONT_OPEN;
}

rlc_continuity_band_t rlc_continuity_classify_hysteresis(
    int32_t uv, rlc_continuity_band_t current)
{
    switch (current) {
    case CONT_CONNECTED:
        /* Up to MARGINAL? */
        if (uv > CONT_MARGINAL_UV + CONT_HYSTERESIS_MARGINAL_UV) {
            if (uv < CONT_OPEN_UV) return CONT_MARGINAL;
            return CONT_OPEN;
        }
        return CONT_CONNECTED;

    case CONT_MARGINAL:
        /* Down to CONNECTED? */
        if (uv < CONT_MARGINAL_UV - CONT_HYSTERESIS_MARGINAL_UV)
            return CONT_CONNECTED;
        /* Up to OPEN? */
        if (uv > CONT_OPEN_UV + CONT_HYSTERESIS_OPEN_UV)
            return CONT_OPEN;
        return CONT_MARGINAL;

    case CONT_OPEN:
        /* Stay OPEN unless the reading drops below boundary - hysteresis */
        if (uv < CONT_OPEN_UV - CONT_HYSTERESIS_OPEN_UV) {
            if (uv < CONT_MARGINAL_UV) return CONT_CONNECTED;
            return CONT_MARGINAL;
        }
        return CONT_OPEN;

    case CONT_SHORT:
        /* Deprecated band — a unit that booted before the merge, or a stale
         * cached value, is folded into the current scheme on first update. */
        return rlc_continuity_classify_initial(uv);
    }
    return rlc_continuity_classify_initial(uv);
}
