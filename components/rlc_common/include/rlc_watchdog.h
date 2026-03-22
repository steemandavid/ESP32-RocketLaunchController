/**
 * RLC Watchdog Timer
 *
 * Hardware watchdog with 2-second timeout.
 * Main loop and critical tasks must feed regularly.
 */

#pragma once

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
