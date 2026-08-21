/**
 * RLC Remote Unit Battery Monitoring Task
 *
 * Wraps rlc_battery_sample() in a dedicated FreeRTOS task with
 * three-threshold detection (FSD §8.3.4).
 */

#pragma once

#include "rlc_battery.h"

void remote_battery_start_task(void);
