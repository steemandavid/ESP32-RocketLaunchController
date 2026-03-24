#pragma once

void hw_inputs_init(void);
int  arm_sense_read_raw(void);       /* raw GPIO level: 1=armed, 0=disarmed */
int  arm_sense_read_debounced(void); /* debounced: 1=armed (HIGH), 0=disarmed (LOW) */
