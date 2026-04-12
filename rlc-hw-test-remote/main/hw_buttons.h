#pragma once
#include <stdint.h>

void     hw_buttons_init(void);

/* Fire button (GPIO 15, 8-bit debounce) */
void     fire_poll(void);                 /* shift one sample into register — call at 10 ms */
int      fire_read_raw(void);             /* raw GPIO: 0=pressed, 1=released */
int      fire_read_debounced(void);       /* 1=pressed, 0=released (reads current SR) */
int      fire_get_shift_reg(void);        /* raw 8-bit shift register (no shift-in) */
void     fire_reset_fresh(void);          /* reset fresh-press tracking only */
int      fire_fresh_press(void);          /* 1 if fresh press detected, consumed on read */

/* Arm switch (GPIO 7, 16-bit debounce) */
void     arm_poll(void);                  /* shift one sample into register — call at 10 ms */
int      arm_read_raw(void);              /* raw GPIO: 0=ARMED, 1=DISARMED */
int      arm_read_debounced(void);        /* 1=ARMED, 0=DISARMED (reads current SR) */
uint16_t arm_get_shift_reg(void);         /* raw 16-bit shift register (no shift-in) */

/* Encoder push button (GPIO 6, 16-bit debounce) */
void     enc_sw_poll(void);               /* shift one sample into register — call at 10 ms */
int      enc_sw_read_raw(void);
int      enc_sw_read_debounced(void);     /* 1=pressed, 0=released (reads current SR) */
uint16_t enc_sw_get_shift_reg(void);      /* raw 16-bit shift register (no shift-in) */
