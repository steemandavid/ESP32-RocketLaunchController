/**
 * RLC Base Unit STATUS_UPDATE Generation Task
 *
 * Periodic (2000 ms) and event-driven STATUS_UPDATE messages
 * populated with real continuity bands, arm sense, and battery data.
 * FSD §6.4.3, §7.3.
 */

#pragma once

void status_update_init(void);
void status_update_start_task(void);

/**
 * Request an immediate (event-driven) STATUS_UPDATE.
 * Called by continuity and arm sense modules on state changes.
 */
void status_update_trigger(void);
