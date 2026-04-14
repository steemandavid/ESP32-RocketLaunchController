#pragma once

#include <stdbool.h>

void arm_switch_init(void);
void arm_switch_start_task(void);
bool arm_switch_is_armed(void);
void arm_switch_register_cb(void (*cb)(bool armed));
