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
    printf("BEEP_SHORT (100ms)...\r\n");
    buzzer_beep(100);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("BEEP_DOUBLE (100/100/100)...\r\n");
    buzzer_pattern(100, 100, 2);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("BEEP_TRIPLE (100/80/100/80/100)...\r\n");
    buzzer_pattern(100, 80, 3);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("BEEP_LONG (500ms)...\r\n");
    buzzer_beep(500);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("BEEP_PING_FAIL (80ms)...\r\n");
    buzzer_beep(80);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("BEEP_CONTINUITY_LOST (200/100 x3)...\r\n");
    buzzer_pattern(200, 100, 3);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("ALARM_LINK_LOST (200/200 x3)...\r\n");
    buzzer_pattern(200, 200, 3);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("ALARM_CRITICAL (100/100 x3)...\r\n");
    buzzer_pattern(100, 100, 3);

    printf("Buzzer test complete.\r\n");
}
