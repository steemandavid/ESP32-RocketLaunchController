#include "hw_fire_timer.h"
#include "hw_relay.h"
#include "pin_config.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "hw_fire";

/* Notification bit used by timer ISR → fire task */
#define FIRE_NOTIFY_BIT  0x01U
#define FIRE_ABORT_BIT   0x02U

static gptimer_handle_t  s_timer      = NULL;
static TaskHandle_t      s_fire_task  = NULL;

static bool IRAM_ATTR fire_timer_isr(gptimer_handle_t timer,
                                      const gptimer_alarm_event_data_t *edata,
                                      void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    ESP_DRAM_LOGI(TAG, "Timer ISR: signalling task");
    xTaskNotifyFromISR(s_fire_task, FIRE_NOTIFY_BIT, eSetBits, &woken);
    return woken == pdTRUE;
}

void hw_fire_timer_init(void)
{
    gptimer_config_t cfg = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, /* 1 µs per tick */
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &s_timer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = fire_timer_isr,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(s_timer));

    ESP_LOGI(TAG, "Fire hardware timer initialised");
}

int fire_pulse(int ch, uint32_t duration_ms, int safe_after)
{
    if (ch < 1 || ch > 8) return -1;

    s_fire_task = xTaskGetCurrentTaskHandle();

    /* Clear any stale notification */
    xTaskNotifyStateClear(NULL);
    ulTaskNotifyValueClear(NULL, 0xFFFFFFFF);

    /* Arm timer alarm */
    gptimer_alarm_config_t alarm = {
        .alarm_count        = (uint64_t)duration_ms * 1000ULL, /* µs */
        .reload_count       = 0,
        .flags.auto_reload_on_alarm = false,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(s_timer, &alarm));
    ESP_ERROR_CHECK(gptimer_set_raw_count(s_timer, 0));

    int64_t t_start = esp_timer_get_time();

    /* Activate relays */
    lowside_set(1);
    relay_set(ch, 1);

    /* Start timer */
    ESP_ERROR_CHECK(gptimer_start(s_timer));

    /* Wait for ISR notification (or abort) — block indefinitely */
    uint32_t bits = 0;
    xTaskNotifyWait(0, 0xFFFFFFFF, &bits, portMAX_DELAY);

    /* Stop timer */
    gptimer_stop(s_timer);

    int64_t t_end     = esp_timer_get_time();
    int     elapsed_ms = (int)((t_end - t_start) / 1000);

    if (bits & FIRE_ABORT_BIT) {
        ESP_LOGI(TAG, "Fire aborted by safe command");
        return elapsed_ms;
    }

    ESP_LOGI(TAG, "Task: relay_all_safe() called");
    if (safe_after) {
        relay_all_safe();
    }

    return elapsed_ms;
}

/* Called from CLI 'safe' command to abort an in-progress fire pulse */
void fire_abort(void)
{
    if (s_fire_task) {
        xTaskNotify(s_fire_task, FIRE_ABORT_BIT, eSetBits);
    }
}
