#pragma once
#include "freertos/FreeRTOS.h"
static inline void vTaskDelay(uint32_t t) { (void)t; }
static inline int xTaskCreate(void (*f)(void*), const char *n, int s, void *p, int pr, TaskHandle_t *h)
{ (void)f;(void)n;(void)s;(void)p;(void)pr; if (h) *h=(void*)1; return pdPASS; }
