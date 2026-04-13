#include "hw_buzzer.h"
#include "pin_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "hw_buzz";

void hw_buzzer_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BUZZER),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(PIN_BUZZER, PIN_BUZZER_ACTIVE ? 0 : 1);
    ESP_LOGI(TAG, "Buzzer GPIO initialised");
}

void buzzer_set(int active)
{
    gpio_set_level(PIN_BUZZER, active ? PIN_BUZZER_ACTIVE : !PIN_BUZZER_ACTIVE);
}

void buzzer_beep(uint32_t ms)
{
    buzzer_set(1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    buzzer_set(0);
}

void buzzer_pattern(uint32_t on_ms, uint32_t off_ms, int count)
{
    for (int i = 0; i < count; i++) {
        buzzer_set(1);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        buzzer_set(0);
        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}

void buzzer_test(void)
{
    printf("BEEP_SHORT (200ms)...\r\n");
    buzzer_beep(200);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("BEEP_DOUBLE (250/300/250)...\r\n");
    buzzer_pattern(250, 300, 2);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("BEEP_TRIPLE (250/250 x3)...\r\n");
    buzzer_pattern(250, 250, 3);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("BEEP_LONG (500ms)...\r\n");
    buzzer_beep(500);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("BEEP_PING_FAIL (150ms)...\r\n");
    buzzer_beep(150);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("BEEP_CONTINUITY_LOST (300/300 x3)...\r\n");
    buzzer_pattern(300, 300, 3);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("ALARM_LINK_LOST (400/400 x3)...\r\n");
    buzzer_pattern(400, 400, 3);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("ALARM_CRITICAL (250/250 x5)...\r\n");
    buzzer_pattern(250, 250, 5);

    printf("Buzzer test complete.\r\n");
}
