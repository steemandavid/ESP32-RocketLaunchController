#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0xFFFFFFFF
#define pdTRUE 1
#define pdPASS 1
