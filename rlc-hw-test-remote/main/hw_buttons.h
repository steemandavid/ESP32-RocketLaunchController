#pragma once
#include <stdint.h>

void     hw_buttons_init(void);

/* Fire button (GPIO 15, 8-bit debounce) */
int      fire_read_raw(void);             /* raw GPIO: 0=pressed, 1=released */
int      fire_read_debounced(void);       /* 1=pressed, 0=released */
int      fire_get_shift_reg(void);        /* raw 8-bit shift register */
int      fire_fresh_press(void);          /* 1 if fresh press detected, consumed on read */

/* Arm switch (GPIO 7, 16-bit debounce) */
int      arm_read_raw(void);              /* raw GPIO: 0=ARMED, 1=DISARMED */
int      arm_read_debounced(void);        /* 1=ARMED, 0=DISARMED */
uint16_t arm_get_shift_reg(void);         /* raw 16-bit shift register */

/* Encoder push button (GPIO 6, 16-bit debounce) */
int      enc_sw_read_raw(void);
int      enc_sw_read_debounced(void);     /* 1=pressed, 0=released */
uint16_t enc_sw_get_shift_reg(void);
