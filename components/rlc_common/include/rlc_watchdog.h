/**
 * RLC Watchdog Timer
 *
 * Hardware watchdog with 2-second timeout.
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

/**
 * Register an arbitrary task with the TWDT.
 * Use for non-main tasks (e.g. link_task) that also need watchdog coverage.
 *
 * @param task  Task handle, or NULL for the calling task
 * @return      0 on success
 */
int rlc_watchdog_add_task(TaskHandle_t task);
