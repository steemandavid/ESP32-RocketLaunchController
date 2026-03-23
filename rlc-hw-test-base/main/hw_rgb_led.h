#pragma once
#include <stdint.h>

void hw_rgb_led_init(void);
void led_set(uint8_t r, uint8_t g, uint8_t b);
void led_off(void);
void led_set_brightness(uint8_t brightness);
void led_test(void);   /* cycle through all FSD §11.1 status patterns */
