#pragma once

void hw_leds_init(void);
void arm_led_set(int on);      /* GPIO 8 red LED */
void fire_led_red(int on);     /* GPIO 17 red LED */
void fire_led_green(int on);   /* GPIO 18 green LED */
void all_leds_off(void);
