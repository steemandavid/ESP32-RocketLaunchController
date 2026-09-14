/**
 * RLC Continuity Band Classifier (shared, pure) — see rlc_continuity_class.h.
 *
 * Four bands. SHORT was merged into CONNECTED on 2026-08-21 — the
 * distinction is below the measurement floor at the specified 1 mA test
 * current, so reporting it would be guessing. CONT_SHORT_UV is consequently
 * unused. SUSPECT was split out of OPEN on 2026-09-14 (FSD v1.71) — the old
 * OPEN territory above 500 Ω contains a measurable window (up to ~1.09 kΩ,
 * where the 0 dB range saturates), and swallowing "connected but badly
 * corroded" into "no igniter" threw away a reading the hardware can make.
 */

#include "rlc_continuity_class.h"
#include "rlc_config.h"

rlc_continuity_band_t rlc_continuity_classify_initial(int32_t uv)
{
    /* First reading — simple thresholds, no hysteresis */
    if (uv < CONT_MARGINAL_UV)   return CONT_CONNECTED;
    if (uv < CONT_OPEN_UV)       return CONT_MARGINAL;
    if (uv < CONT_SUSPECT_UV)    return CONT_SUSPECT;
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
            if (uv < CONT_SUSPECT_UV) return CONT_SUSPECT;
            return CONT_OPEN;
        }
        return CONT_CONNECTED;

    case CONT_MARGINAL:
        /* Down to CONNECTED? */
        if (uv < CONT_MARGINAL_UV - CONT_HYSTERESIS_MARGINAL_UV)
            return CONT_CONNECTED;
        /* Up to SUSPECT? (586 mV is the boundary of the cannot-fire region,
         * whichever band names it this week.) */
        if (uv > CONT_OPEN_UV + CONT_HYSTERESIS_OPEN_UV) {
            if (uv < CONT_SUSPECT_UV) return CONT_SUSPECT;
            return CONT_OPEN;
        }
        return CONT_MARGINAL;

    case CONT_SUSPECT:
        /* Down to MARGINAL? */
        if (uv < CONT_OPEN_UV - CONT_HYSTERESIS_OPEN_UV) {
            if (uv < CONT_MARGINAL_UV) return CONT_CONNECTED;
            return CONT_MARGINAL;
        }
        /* Up to OPEN? */
        if (uv > CONT_SUSPECT_UV + CONT_HYSTERESIS_SUSPECT_UV)
            return CONT_OPEN;
        return CONT_SUSPECT;

    case CONT_OPEN:
        /* Stay OPEN unless the reading drops below boundary - hysteresis */
        if (uv < CONT_SUSPECT_UV - CONT_HYSTERESIS_SUSPECT_UV) {
            if (uv < CONT_OPEN_UV) {
                if (uv < CONT_MARGINAL_UV) return CONT_CONNECTED;
                return CONT_MARGINAL;
            }
            return CONT_SUSPECT;
        }
        return CONT_OPEN;
    }
    return rlc_continuity_classify_initial(uv);
}
