#pragma once
#include "freertos/FreeRTOS.h"

/* Host-test stub. The FSM harness drives process_event()/check_timers()
 * directly rather than running the task loop, so queues only need to exist
 * and never actually carry anything. */
typedef void* QueueHandle_t;
#define pdFALSE 0

static inline QueueHandle_t xQueueCreate(int len, int item)
{ (void)len; (void)item; return (void*)1; }
static inline int xQueueSend(QueueHandle_t q, const void *item, uint32_t t)
{ (void)q; (void)item; (void)t; return pdTRUE; }
static inline int xQueueSendToFront(QueueHandle_t q, const void *item, uint32_t t)
{ (void)q; (void)item; (void)t; return pdTRUE; }
static inline int xQueueOverwrite(QueueHandle_t q, const void *item)
{ (void)q; (void)item; return pdTRUE; }
static inline int xQueueReceive(QueueHandle_t q, void *item, uint32_t t)
{ (void)q; (void)item; (void)t; return 0; }
static inline void xQueueReset(QueueHandle_t q) { (void)q; }
