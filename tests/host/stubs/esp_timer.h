#pragma once
#include <stdint.h>
#include <stdbool.h>
extern int64_t g_mock_us;
static inline int64_t esp_timer_get_time(void) { return g_mock_us; }

/* CRIG module (test_cam_trig.c): declarations only. test_cam_trig.c supplies a
 * simulated timer wheel; no other test calls these, so nothing else needs to
 * define them. */
#include "esp_err.h"
typedef struct esp_timer *esp_timer_handle_t;
typedef enum { ESP_TIMER_TASK = 0, ESP_TIMER_ISR = 1 } esp_timer_dispatch_t;
typedef struct {
    void (*callback)(void *arg);
    void *arg;
    esp_timer_dispatch_t dispatch_method;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out);
esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t timeout_us);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t period_us);
esp_err_t esp_timer_stop(esp_timer_handle_t t);
