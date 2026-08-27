/* Host-test stub for esp_task_wdt.h.
 * The TWDT has no meaning off-target; the production sources call these from
 * their task loops (rlc_rgb_led.c's led_task since CI-04), so they only need
 * to compile and do nothing. */
#pragma once

static inline int esp_task_wdt_add(void *h)   { (void)h; return 0; }
static inline int esp_task_wdt_reset(void)    { return 0; }
static inline int esp_task_wdt_delete(void *h){ (void)h; return 0; }
