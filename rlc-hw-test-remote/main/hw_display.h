#pragma once
#include <stdint.h>
#include <stdbool.h>

void    hw_display_init(void);
bool    display_read_id(uint32_t *out_id);
void    display_fill(uint8_t r, uint8_t g, uint8_t b);
void    display_test(void);
void    display_text(const char *str);
void    display_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void    display_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
void    display_gradient(void);
void    display_speed(void);
void    display_backlight(int on);
