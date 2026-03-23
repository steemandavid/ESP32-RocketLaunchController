#include "hw_inputs.h"
#include "pin_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "hw_inputs";

void hw_inputs_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_ARM_SWITCH) | (1ULL << PIN_RELAY_FEEDBACK),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    ESP_LOGI(TAG, "Input GPIOs initialised (arm switch, relay feedback)");
}

int arm_switch_read_raw(void)
{
    return gpio_get_level(PIN_ARM_SWITCH);
}

/* Simple 3-sample debounce — reads three times 5 ms apart */
int arm_switch_read_debounced(void)
{
    int s0 = gpio_get_level(PIN_ARM_SWITCH);
    vTaskDelay(pdMS_TO_TICKS(5));
    int s1 = gpio_get_level(PIN_ARM_SWITCH);
    vTaskDelay(pdMS_TO_TICKS(5));
    int s2 = gpio_get_level(PIN_ARM_SWITCH);

    /* Majority vote */
    int level = (s0 + s1 + s2) >= 2 ? 1 : 0;
    /* Armed = switch active = GPIO LOW (pull-up, switch pulls to GND) */
    return (level == 0) ? 1 : 0;
}

int feedback_read_raw(void)
{
    return gpio_get_level(PIN_RELAY_FEEDBACK);
}
