#include "hw_relay.h"
#include "pin_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "hw_relay";

/* Tables indexed 0–7 for channels 1–8 */
static const int s_relay_pins[8] = {
    PIN_RELAY_CH1, PIN_RELAY_CH2, PIN_RELAY_CH3, PIN_RELAY_CH4,
    PIN_RELAY_CH5, PIN_RELAY_CH6, PIN_RELAY_CH7, PIN_RELAY_CH8
};
static const int s_relay_active[8] = {
    PIN_RELAY_CH1_ACTIVE, PIN_RELAY_CH2_ACTIVE, PIN_RELAY_CH3_ACTIVE, PIN_RELAY_CH4_ACTIVE,
    PIN_RELAY_CH5_ACTIVE, PIN_RELAY_CH6_ACTIVE, PIN_RELAY_CH7_ACTIVE, PIN_RELAY_CH8_ACTIVE
};

static void gpio_out_init(int pin, int inactive_level)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(pin, inactive_level);
}

void hw_relay_init(void)
{
    /* Drive all outputs inactive before configuring anything else */
    for (int i = 0; i < 8; i++) {
        gpio_out_init(s_relay_pins[i], !s_relay_active[i]);
    }
    gpio_out_init(PIN_LOWSIDE_RELAY, !PIN_LOWSIDE_ACTIVE);
    ESP_LOGI(TAG, "Relay GPIOs initialised — all inactive");
}

void relay_set(int ch, int active)
{
    if (ch < 1 || ch > 8) return;
    int idx   = ch - 1;
    int level = active ? s_relay_active[idx] : !s_relay_active[idx];
    gpio_set_level(s_relay_pins[idx], level);
}

void relay_all_off(void)
{
    for (int i = 0; i < 8; i++) {
        gpio_set_level(s_relay_pins[i], !s_relay_active[i]);
    }
}

void lowside_set(int active)
{
    gpio_set_level(PIN_LOWSIDE_RELAY, active ? PIN_LOWSIDE_ACTIVE : !PIN_LOWSIDE_ACTIVE);
}

void relay_all_safe(void)
{
    relay_all_off();
    lowside_set(0);
    ESP_LOGI(TAG, "relay_all_safe() called — all outputs inactive");
}

void relay_sweep(void)
{
    for (int ch = 1; ch <= 8; ch++) {
        relay_set(ch, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        relay_set(ch, 0);
    }
}

int relay_feedback_read(void)
{
    return gpio_get_level(PIN_RELAY_FEEDBACK);
}
