#pragma once
#include <stdint.h>

void     hw_relay_init(void);
void     relay_set(int ch, int active);      /* ch: 1–8, SPDT relay */
void     relay_all_off(void);
void     relay_all_safe(void);               /* all channels off */
void     relay_sweep(void);                  /* 500 ms per channel */
