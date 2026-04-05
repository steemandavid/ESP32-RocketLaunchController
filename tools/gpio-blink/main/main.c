/* Minimal GPIO blink — toggles GPIO 11 at 1Hz, nothing else.
 * If this doesn't drive GPIO 11, something fundamental is broken. */

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const int PINS[] = { 2, 4, 11, 12, 13, 14, 17, 18, 21, 42, 47 };
#define NUM_PINS (int)(sizeof(PINS)/sizeof(PINS[0]))

static const char *TAG = "blink";

void app_main(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < NUM_PINS; i++) mask |= (1ULL << PINS[i]);

    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    ESP_LOGI(TAG, "Toggling %d GPIOs at 1 Hz in sync", NUM_PINS);
    for (int i = 0; i < NUM_PINS; i++) {
        ESP_LOGI(TAG, "  GPIO %d", PINS[i]);
    }

    int level = 0;
    while (1) {
        level = !level;
        char line[160]; int off = 0;
        for (int i = 0; i < NUM_PINS; i++) {
            gpio_set_level(PINS[i], level);
        }
        off += snprintf(line + off, sizeof(line) - off, "drive=%d  readback:", level);
        for (int i = 0; i < NUM_PINS; i++) {
            off += snprintf(line + off, sizeof(line) - off, " %d=%d", PINS[i], gpio_get_level(PINS[i]));
        }
        ESP_LOGI(TAG, "%s", line);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
