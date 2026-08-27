#pragma once
#include "freertos/FreeRTOS.h"
static inline void vTaskDelay(uint32_t t) { (void)t; }
static inline int xTaskCreate(void (*f)(void*), const char *n, int s, void *p, int pr, TaskHandle_t *h)
{ (void)f;(void)n;(void)s;(void)p;(void)pr; if (h) *h=(void*)1; return pdPASS; }

/* Base FSM harness (TT-04): the FSM task itself is never run under test —
 * process_event()/check_timers() are called directly — but the file that
 * defines it still has to compile and link. */
static inline int xTaskCreatePinnedToCore(void (*f)(void*), const char *n, int s,
                                          void *p, int pr, TaskHandle_t *h, int core)
{ (void)f;(void)n;(void)s;(void)p;(void)pr;(void)core; if (h) *h=(void*)1; return pdPASS; }
static inline int xTaskNotifyWait(uint32_t clr_entry, uint32_t clr_exit,
                                  uint32_t *val, uint32_t wait)
{ (void)clr_entry;(void)clr_exit;(void)wait; if (val) *val = 0; return 0; }
static inline void xTaskNotifyStateClear(TaskHandle_t h) { (void)h; }
static inline void ulTaskNotifyValueClear(TaskHandle_t h, uint32_t bits)
{ (void)h; (void)bits; }
