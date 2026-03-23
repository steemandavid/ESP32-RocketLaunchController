#pragma once

void hw_inputs_init(void);
int  arm_switch_read_raw(void);      /* raw GPIO level */
int  arm_switch_read_debounced(void);/* 1 = armed (LOW), 0 = disarmed (HIGH) */
int  feedback_read_raw(void);        /* raw GPIO level */
