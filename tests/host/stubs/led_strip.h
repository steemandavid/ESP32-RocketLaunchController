#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef void* led_strip_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define LED_MODEL_WS2812 0
#define LED_STRIP_COLOR_COMPONENT_FMT_GRB 0
#define RMT_CLK_SRC_DEFAULT 0
typedef struct { int strip_gpio_num; int max_leds; int led_model;
                 int color_component_format; struct { bool invert_out; } flags; } led_strip_config_t;
typedef struct { int clk_src; int resolution_hz; struct { bool with_dma; } flags; } led_strip_rmt_config_t;

/* Capture buffer for the test harness */
#define MOCK_MAX_PIX 8
extern uint32_t g_pix[MOCK_MAX_PIX];
extern int g_refreshes;
static inline esp_err_t led_strip_set_pixel(led_strip_handle_t h, uint32_t i, uint32_t r, uint32_t g, uint32_t b)
{ (void)h; if (i < MOCK_MAX_PIX) g_pix[i] = (r<<16)|(g<<8)|b; return ESP_OK; }
static inline esp_err_t led_strip_refresh(led_strip_handle_t h) { (void)h; g_refreshes++; return ESP_OK; }
static inline esp_err_t led_strip_clear(led_strip_handle_t h)
{ (void)h; for (int i=0;i<MOCK_MAX_PIX;i++) g_pix[i]=0; return ESP_OK; }
static inline esp_err_t led_strip_new_rmt_device(const led_strip_config_t *c, const led_strip_rmt_config_t *r, led_strip_handle_t *out)
{ (void)c;(void)r; *out = (void*)1; return ESP_OK; }
