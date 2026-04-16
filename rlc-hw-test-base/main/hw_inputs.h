#pragma once

void hw_inputs_init(void);
int  arm_sense_read_raw(void);       /* raw GPIO level: 1=armed, 0=disarmed */
int  arm_sense_read_debounced(void); /* debounced: 1=armed (HIGH), 0=disarmed (LOW) */

int  key_sense_read_raw(void);       /* raw GPIO level: 1=key ON, 0=key OFF */
int  key_sense_read_debounced(void); /* debounced: 1=key ON (HIGH), 0=key OFF (LOW) */

void arm_sim_init(void);             /* configure GPIO 47 arm relay (off) */
void arm_sim_set(int on);            /* 1 = energise relay (simulate armed) */
