/**
 * RLC Base Unit Battery Monitoring Task
 *
 * Wraps rlc_battery_sample() in a dedicated FreeRTOS task with
 * threshold detection (FSD §7.3.3).
 */

#pragma once

void base_battery_start_task(void);
