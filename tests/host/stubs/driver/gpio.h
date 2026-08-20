#pragma once
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_NEGEDGE 2
#define GPIO_INTR_ANYEDGE 3
typedef struct { uint64_t pin_bit_mask; int mode; int pull_up_en;
                 int pull_down_en; int intr_type; } gpio_config_t;
extern int g_gpio_level[64];
static inline esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return ESP_OK; }
static inline int gpio_get_level(int pin) { return g_gpio_level[pin & 63]; }
static inline esp_err_t gpio_install_isr_service(int f) { (void)f; return ESP_OK; }
static inline esp_err_t gpio_isr_handler_add(int p, void (*fn)(void*), void *a)
{ (void)p;(void)fn;(void)a; return ESP_OK; }
