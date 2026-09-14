/**
 * RLC Continuity Band Classifier (shared, pure)
 *
 * The production four-band classification with hysteresis (FSD §5.4.2,
 * §14.5; CONNECTED / MARGINAL / SUSPECT / OPEN, the last two split apart in
 * FSD v1.71). Lives in rlc_common — not inside rlc_continuity.c — so the boot
 * self-test (rlc_selftest.c) exercises the *production* functions instead
 * of a copy that silently drifts when the real logic is edited (Phase-2
 * finding M2 / all-phases review finding 2.5). Pure functions: no ADC, no
 * state, safe to call from any context.
 */

#pragma once

#include <stdint.h>
#include "rlc_protocol.h"

/**
 * First-reading classification — simple thresholds, no hysteresis.
 * Also the fallback for an out-of-range band value.
 */
rlc_continuity_band_t rlc_continuity_classify_initial(int32_t uv);

/**
 * Classification with hysteresis relative to the channel's current band.
 * A reading inside the hysteresis margin of a boundary keeps the current
 * band.
 */
rlc_continuity_band_t rlc_continuity_classify_hysteresis(
    int32_t uv, rlc_continuity_band_t current);
