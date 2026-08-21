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
 * Initialise the hardware watchdog timer.
 * @return 0 on success
 */
int rlc_watchdog_init(void);

/**
 * Feed (reset) the watchdog timer.
 * Must be called from the main task at least every WATCHDOG_TIMEOUT_S.
 */
void rlc_watchdog_feed(void);

/* 5.11: spawned tasks register THEMSELVES with esp_task_wdt_add(NULL) at
 * task entry — never from the creator after xTaskCreate. A spawned task at
 * a higher priority than its creator can call esp_task_wdt_reset() before
 * the creator's add, producing "task not found" TWDT error bursts at boot. */
