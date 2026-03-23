#pragma once
#include <stdint.h>

void hw_siren_init(void);
void siren_set(int active);
void siren_pulse(uint32_t on_ms, uint32_t off_ms, int count);
void siren_test(void);   /* ARMED / PRE_FIRE / LINK_LOST / ERROR patterns */
