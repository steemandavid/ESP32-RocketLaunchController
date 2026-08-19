#pragma once
#include <stdint.h>
extern int64_t g_mock_us;
static inline int64_t esp_timer_get_time(void) { return g_mock_us; }
