#pragma once
#include <stdint.h>

void     hw_buzzer_init(void);
void     buzzer_set(int active);
void     buzzer_beep(uint32_t ms);
void     buzzer_pattern(uint32_t on_ms, uint32_t off_ms, int count);
void     buzzer_test(void);
