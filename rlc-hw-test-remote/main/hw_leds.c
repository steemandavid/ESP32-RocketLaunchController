#include "hw_leds.h"
#include "pin_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "hw_leds";

void hw_leds_init(void)
{
    uint64_t mask = (1ULL << PIN_ARM_LED)
                  | (1ULL << PIN_FIRE_LED_RED)
                  | (1ULL << PIN_FIRE_LED_GREEN);
    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    all_leds_off();
    ESP_LOGI(TAG, "Indicator LEDs initialised (arm=%d, fire_r=%d, fire_g=%d)",
             PIN_ARM_LED, PIN_FIRE_LED_RED, PIN_FIRE_LED_GREEN);
}

void arm_led_set(int on)
{
    gpio_set_level(PIN_ARM_LED, on ? 1 : 0);
}

void fire_led_red(int on)
{
    gpio_set_level(PIN_FIRE_LED_RED, on ? 1 : 0);
}

void fire_led_green(int on)
{
    gpio_set_level(PIN_FIRE_LED_GREEN, on ? 1 : 0);
}

void all_leds_off(void)
{
    gpio_set_level(PIN_ARM_LED, 0);
    gpio_set_level(PIN_FIRE_LED_RED, 0);
    gpio_set_level(PIN_FIRE_LED_GREEN, 0);
}
