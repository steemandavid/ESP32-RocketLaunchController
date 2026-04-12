#include "hw_relay.h"
#include "hw_inputs.h"
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
        .mode         = GPIO_MODE_INPUT_OUTPUT,  /* read-back enabled for diagnostics */
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(pin, inactive_level);
}

void hw_relay_init(void)
{
    /* Drive all SPDT relay outputs inactive (de-energised / NC position)
     * before configuring anything else — FSD §9.7 boot safety. */
    for (int i = 0; i < 8; i++) {
        gpio_out_init(s_relay_pins[i], !s_relay_active[i]);
    }
    ESP_LOGI(TAG, "SPDT relay GPIOs initialised — all de-energised (NC)");
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

void relay_all_safe(void)
{
    relay_all_off();
    ESP_LOGI(TAG, "relay_all_safe() — all SPDT relays de-energised (NC)");
}

void relay_sweep(void)
{
    for (int ch = 1; ch <= 8; ch++) {
        relay_set(ch, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        relay_set(ch, 0);
    }
    /* Arm relay (GPIO 47) */
    arm_sim_set(1);
    vTaskDelay(pdMS_TO_TICKS(500));
    arm_sim_set(0);
}
