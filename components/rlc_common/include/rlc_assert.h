/**
 * RLC Custom Assertion Macro
 *
 * On failure: calls relay_all_safe(), sets ERR_INTERNAL,
 * sends final STATUS_UPDATE if possible, transitions to ERROR.
 */

#pragma once

#include "esp_log.h"

/**
 * Assertion failure handler — implemented per unit type.
 * Base: calls relay_all_safe() and transitions to ERROR.
 * Remote: transitions to ERROR.
 */
void rlc_assert_fail(const char *file, int line, const char *expr);

#define RLC_ASSERT(cond)  do { \
    if (!(cond)) { \
        ESP_LOGE("RLC_ASSERT", "ASSERTION FAILED: %s at %s:%d", #cond, __FILE__, __LINE__); \
        rlc_assert_fail(__FILE__, __LINE__, #cond); \
    } \
} while (0)
