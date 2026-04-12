#include "hw_inputs.h"
#include "pin_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "hw_inputs";

void hw_inputs_init(void)
{
    /* Arm switch sense input — GPIO 21 (§5.4.3)
     * External circuit: 10 kΩ series + 3.3V zener clamp + 100 kΩ pull-down.
     * No internal pull needed — external 100 kΩ pull-down handles it. */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_ARM_SENSE),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    ESP_LOGI(TAG, "Input GPIOs initialised (arm switch sense on GPIO %d)", PIN_ARM_SENSE);
}

int arm_sense_read_raw(void)
{
    return gpio_get_level(PIN_ARM_SENSE);
}

/* Simple 3-sample debounce — reads three times 5 ms apart */
int arm_sense_read_debounced(void)
{
    int s0 = gpio_get_level(PIN_ARM_SENSE);
    vTaskDelay(pdMS_TO_TICKS(5));
    int s1 = gpio_get_level(PIN_ARM_SENSE);
    vTaskDelay(pdMS_TO_TICKS(5));
    int s2 = gpio_get_level(PIN_ARM_SENSE);

    /* Majority vote — HIGH = armed (VBAT present on fire path) */
    return (s0 + s1 + s2) >= 2 ? 1 : 0;
}

/* --- Arm relay output (GPIO 47, IRLZ44N MOSFET) ----------------------- */

void arm_sim_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_ARM_SIM_RELAY),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    /* Start de-energised (relay off = arm switch disarmed) */
    gpio_set_level(PIN_ARM_SIM_RELAY, !PIN_ARM_SIM_RELAY_ACTIVE);
    ESP_LOGI(TAG, "Arm sim relay initialised on GPIO %d (OFF)", PIN_ARM_SIM_RELAY);
}

void arm_sim_set(int on)
{
    gpio_set_level(PIN_ARM_SIM_RELAY, on ? PIN_ARM_SIM_RELAY_ACTIVE : !PIN_ARM_SIM_RELAY_ACTIVE);
    ESP_LOGI(TAG, "Arm sim relay: %s (GPIO %d)", on ? "ON (sim ARMED)" : "OFF (sim DISARMED)", PIN_ARM_SIM_RELAY);
}
