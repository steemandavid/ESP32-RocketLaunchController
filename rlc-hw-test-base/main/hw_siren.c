#include "hw_siren.h"
#include "pin_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "hw_siren";

void hw_siren_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_SIREN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(PIN_SIREN, PIN_SIREN_ACTIVE ? 0 : 1);  /* inactive */
    ESP_LOGI(TAG, "Siren GPIO initialised (via IRLZ44N MOSFET)");
}

void siren_set(int active)
{
    gpio_set_level(PIN_SIREN, active ? PIN_SIREN_ACTIVE : !PIN_SIREN_ACTIVE);
}

void siren_pulse(uint32_t on_ms, uint32_t off_ms, int count)
{
    for (int i = 0; i < count; i++) {
        siren_set(1);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        siren_set(0);
        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}

void siren_test(void)
{
    printf("ARMED pattern: 500/500 x 3\r\n");
    siren_pulse(500, 500, 3);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("PRE_FIRE pattern: continuous 2s\r\n");
    siren_set(1);
    vTaskDelay(pdMS_TO_TICKS(2000));
    siren_set(0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("FIRING pattern: continuous 2s\r\n");
    siren_set(1);
    vTaskDelay(pdMS_TO_TICKS(2000));
    siren_set(0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("LINK_LOST pattern: 500/500 x 4\r\n");
    siren_pulse(500, 500, 4);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("ERROR pattern: 200/200 x 3\r\n");
    siren_pulse(200, 200, 3);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("CONTINUITY_LOST pattern: 200/200 x 3\r\n");
    siren_pulse(200, 200, 3);

    printf("Siren test complete.\r\n");
}
