#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0xFFFFFFFF
#define pdTRUE 1
#define pdPASS 1

/* Critical sections (CRIG module). Single-threaded host: no-ops, but the
 * test counts nesting so an unbalanced enter/exit fails loudly. */
typedef struct { int owner; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }
extern int g_crit_depth;
#define taskENTER_CRITICAL(m) do { (void)(m); g_crit_depth++; } while (0)
#define taskEXIT_CRITICAL(m)  do { (void)(m); g_crit_depth--; } while (0)
