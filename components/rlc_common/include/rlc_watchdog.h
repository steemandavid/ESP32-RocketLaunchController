/**
 * RLC Watchdog Timer
 *
 * Task Watchdog (TWDT) with WATCHDOG_TIMEOUT_S (5 s) timeout.
 * Main loop and critical tasks must feed regularly.
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * Reconfigure the Task Watchdog to the project timeout.
 *
 * N3 — CALL THIS BEFORE STARTING ANY TASK.
 *
 * esp_task_wdt_reconfigure() rebuilds the TWDT's subscriber list. Any task
 * that has already called esp_task_wdt_add(NULL) loses its subscription, and
 * every esp_task_wdt_reset() it makes afterwards logs
 * "esp_task_wdt_reset(): task not found" — at that task's full loop rate.
 * Worse, the unfed watchdog eventually triggers and panics
 * (LoadProhibited) while walking the stale entries in its report path.
 *
 * That is not hypothetical: on firmware 1.1.1 the remote called this after
 * display_start_task(), and rebooted 11.4 s into every boot. See
 * Development_Progress "Post-Fix Code Review Round".
 *
 * Does NOT subscribe the calling task — see rlc_watchdog_register_self().
 * The split exists because app_main must reconfigure early (before tasks)
 * but must not be subscribed until it reaches its housekeeping loop; the
 * init work in between (SPI, Wi-Fi, NVS, peer retries) can exceed the
 * timeout on its own.
 *
 * @return 0 on success
 */
int rlc_watchdog_init(void);

/**
 * Subscribe the calling task to the TWDT.
 * Call from app_main immediately before entering its housekeeping loop,
 * after all slow initialisation is done.
 * @return 0 on success
 */
int rlc_watchdog_register_self(void);

/**
 * Feed (reset) the watchdog timer.
 * Must be called from the main task at least every WATCHDOG_TIMEOUT_S.
 */
void rlc_watchdog_feed(void);

/* 5.11: spawned tasks register THEMSELVES with esp_task_wdt_add(NULL) at
 * task entry — never from the creator after xTaskCreate. A spawned task at
 * a higher priority than its creator can call esp_task_wdt_reset() before
 * the creator's add, producing "task not found" TWDT error bursts at boot.
 * That rule and the N3 ordering rule work together: reconfigure first, then
 * start tasks, and each task adds itself. */
