#pragma once
#include <stdint.h>

void     hw_relay_init(void);
void     relay_set(int ch, int active);      /* ch: 1–8 */
void     relay_all_off(void);
void     lowside_set(int active);
void     relay_all_safe(void);               /* all channels + lowside off */
void     relay_sweep(void);                  /* 500 ms per channel, no lowside */
int      relay_feedback_read(void);          /* raw GPIO level */
