/**
 * RLC Buzzer Pattern Player
 */

#include "rlc_buzzer.h"
#include "pin_config.h"
#include "rlc_config.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "rlc_buzzer";

static QueueHandle_t s_pattern_queue = NULL;
static TaskHandle_t s_buzzer_task = NULL;

static inline void buzzer_drive(bool on)
{
    int level = on ? PIN_BUZZER_ACTIVE : !PIN_BUZZER_ACTIVE;
    gpio_set_level(PIN_BUZZER, level);
}

/* Pattern step: duration_ms, on=true/off=false */
typedef struct {
    uint16_t duration_ms;
    bool     on;
} buzzer_step_t;

static void play_steps(const buzzer_step_t *steps, int count, bool repeat)
{
    do {
        for (int i = 0; i < count; i++) {
            buzzer_drive(steps[i].on);
            TickType_t ticks = pdMS_TO_TICKS(steps[i].duration_ms);
            /* Check for new pattern while waiting */
            rlc_buzzer_pattern_t new_pat;
            if (xQueueReceive(s_pattern_queue, &new_pat, ticks) == pdTRUE) {
                buzzer_drive(false);
                /* Put it back so the main loop picks it up */
                xQueueSendToFront(s_pattern_queue, &new_pat, 0);
                return;
            }
        }
        /* 5.10: repeating alarms (LINK_LOST/CRITICAL) loop here until a new
         * pattern arrives — feed the TWDT each cycle so a hung buzzer task
         * is still caught while a working one is not. */
        esp_task_wdt_reset();
    } while (repeat);

    buzzer_drive(false);
}

static void buzzer_task(void *arg)
{
    (void)arg;
    /* 5.10: TWDT coverage — a hung buzzer task previously went undetected.
     * Timed (not portMAX_DELAY) queue wait so the idle task still feeds. */
    esp_task_wdt_add(NULL);
    rlc_buzzer_pattern_t pattern;

    while (1) {
        if (xQueueReceive(s_pattern_queue, &pattern, pdMS_TO_TICKS(1000)) == pdTRUE) {
            switch (pattern) {
                case BUZZER_BEEP_SHORT: {
                    buzzer_step_t steps[] = {{100, true}};
                    play_steps(steps, 1, false);
                    break;
                }
                case BUZZER_BEEP_DOUBLE: {
                    buzzer_step_t steps[] = {{100, true}, {100, false}, {100, true}};
                    play_steps(steps, 3, false);
                    break;
                }
                case BUZZER_BEEP_TRIPLE: {
                    buzzer_step_t steps[] = {
                        {100, true}, {80, false},
                        {100, true}, {80, false},
                        {100, true}
                    };
                    play_steps(steps, 5, false);
                    break;
                }
                case BUZZER_BEEP_LONG: {
                    buzzer_step_t steps[] = {{500, true}};
                    play_steps(steps, 1, false);
                    break;
                }
                case BUZZER_BEEP_PING_FAIL: {
                    buzzer_step_t steps[] = {{80, true}};
                    play_steps(steps, 1, false);
                    break;
                }
                case BUZZER_BEEP_CONTINUITY_LOST: {
                    buzzer_step_t steps[] = {
                        {200, true}, {100, false},
                        {200, true}, {100, false},
                        {200, true}
                    };
                    play_steps(steps, 5, false);
                    break;
                }
                case BUZZER_ALARM_LINK_LOST: {
                    buzzer_step_t steps[] = {{200, true}, {200, false}};
                    play_steps(steps, 2, true);
                    break;
                }
                case BUZZER_ALARM_CRITICAL: {
                    buzzer_step_t steps[] = {{100, true}, {100, false}};
                    play_steps(steps, 2, true);
                    break;
                }
                case BUZZER_OFF:
                    buzzer_drive(false);
                    break;
            }
        }
        esp_task_wdt_reset();
    }
}

void buzzer_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BUZZER),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    buzzer_drive(false);

    s_pattern_queue = xQueueCreate(4, sizeof(rlc_buzzer_pattern_t));
    if (!s_pattern_queue) {
        ESP_LOGE(TAG, "pattern queue alloc failed");
        return;
    }
    if (xTaskCreate(buzzer_task, "buzzer_task", 2048, NULL, 5,
                    &s_buzzer_task) != pdPASS) {
        ESP_LOGE(TAG, "buzzer task create failed");
    }

    ESP_LOGI(TAG, "Buzzer initialised on GPIO %d", PIN_BUZZER);
}

void buzzer_play(rlc_buzzer_pattern_t pattern)
{
    xQueueReset(s_pattern_queue);
    xQueueSend(s_pattern_queue, &pattern, 0);
}

void buzzer_stop(void)
{
    buzzer_play(BUZZER_OFF);
}
