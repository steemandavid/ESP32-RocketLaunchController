/**
 * RLC Fire Pulse Timer Implementation (Base Unit)
 *
 * Based on the proven hw_fire_timer pattern from Phase 0 hardware tests.
 * Uses ESP-IDF GPTimer with 1µs resolution, one-shot alarm.
 *
 * ISR signals the state machine task via xTaskNotifyFromISR().
 * All relay control happens in task context (FSD §7.4.2, §9.12).
 */

#include "rlc_fire_timer.h"
#include "rlc_fsm_events.h"

#include "driver/gptimer.h"
#include "esp_log.h"

static const char *TAG = "rlc_fire";

static gptimer_handle_t s_timer = NULL;
static TaskHandle_t     s_target_task = NULL;

static bool IRAM_ATTR fire_timer_isr(gptimer_handle_t timer,
                                      const gptimer_alarm_event_data_t *edata,
                                      void *user_ctx)
{
    (void)timer;
    (void)edata;
    (void)user_ctx;

    BaseType_t woken = pdFALSE;
    xTaskNotifyFromISR(s_target_task, FIRE_NOTIFY_BIT, eSetBits, &woken);
    return woken == pdTRUE;
}

void fire_timer_init(void)
{
    gptimer_config_t cfg = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,  /* 1 µs per tick */
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &s_timer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = fire_timer_isr,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(s_timer));

    ESP_LOGI(TAG, "Fire pulse GPTimer initialised");
}

void fire_timer_start(uint32_t duration_ms, uint8_t channel, TaskHandle_t target_task)
{
    s_target_task = target_task;

    /* Clear any stale notification */
    xTaskNotifyStateClear(target_task);
    ulTaskNotifyValueClear(target_task, 0xFFFFFFFF);

    gptimer_alarm_config_t alarm = {
        .alarm_count = (uint64_t)duration_ms * 1000ULL,  /* Convert ms to µs */
        .reload_count = 0,
        .flags.auto_reload_on_alarm = false,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(s_timer, &alarm));
    ESP_ERROR_CHECK(gptimer_set_raw_count(s_timer, 0));
    ESP_ERROR_CHECK(gptimer_start(s_timer));

    ESP_LOGI(TAG, "Fire timer started: ch %u, %lu ms", channel, (unsigned long)duration_ms);
}

void fire_timer_stop(void)
{
    gptimer_stop(s_timer);

    /* m2: Clear any pending FIRE_NOTIFY_BIT that may have been posted by the ISR
     * between gptimer_stop() and this point, to avoid spurious EVT_FIRE_PULSE_DONE. */
    if (s_target_task) {
        ulTaskNotifyValueClear(s_target_task, FIRE_NOTIFY_BIT);
    }

    ESP_LOGI(TAG, "Fire timer stopped");
}
