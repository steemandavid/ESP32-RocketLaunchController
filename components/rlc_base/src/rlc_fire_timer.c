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

/* INF-02: FSD §7.4.2 describes the channel being carried as the timer
 * callback's context and asserted against the firing channel on completion.
 * This is a channel-less notification instead: the FSM owns s_firing_channel
 * and stops the timer on every FIRING exit, so a completion can only ever
 * refer to the pulse in progress, and there is nothing for the assertion to
 * disagree with. Equivalent, with one fewer piece of state reachable from an
 * ISR. */
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

esp_err_t fire_timer_start(uint32_t duration_ms, uint8_t channel, TaskHandle_t target_task)
{
    esp_err_t err;

    s_target_task = target_task;

    /* BF-01: an expired one-shot alarm auto-disables the *alarm*, not the
     * timer — the GPTimer driver stays in RUN state after a pulse completes
     * normally. gptimer_start() on a running timer returns
     * ESP_ERR_INVALID_STATE on IDF 5.4.x (and is a no-op on 5.5.x, where
     * gptimer_set_raw_count() on a counting timer is then unsynchronised).
     * Either way the second launch of a power cycle is broken, so force the
     * timer back to the ENABLE state before every start. Unconditional, so it
     * does not depend on which FIRING exit path ran last. */
    err = gptimer_stop(s_timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gptimer_stop failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Clear any stale notification */
    xTaskNotifyStateClear(target_task);
    ulTaskNotifyValueClear(target_task, 0xFFFFFFFF);

    gptimer_alarm_config_t alarm = {
        .alarm_count = (uint64_t)duration_ms * 1000ULL,  /* Convert ms to µs */
        .reload_count = 0,
        .flags.auto_reload_on_alarm = false,
    };

    /* BF-01: no ESP_ERROR_CHECK on the fire path. An abort() here panics with
     * the arm relay and the channel relay energised — the igniter would carry
     * full current for the whole panic-print + reboot interval. Report the
     * failure instead and let the FSM make the hardware safe and latch ERROR. */
    err = gptimer_set_alarm_action(s_timer, &alarm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gptimer_set_alarm_action failed: %s", esp_err_to_name(err));
        return err;
    }
    err = gptimer_set_raw_count(s_timer, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gptimer_set_raw_count failed: %s", esp_err_to_name(err));
        return err;
    }
    err = gptimer_start(s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gptimer_start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Fire timer started: ch %u, %lu ms", channel, (unsigned long)duration_ms);
    return ESP_OK;
}

void fire_timer_stop(void)
{
    /* ESP_ERR_INVALID_STATE just means the timer was not running — expected on
     * the paths that stop a timer which already expired (BF-01). */
    esp_err_t err = gptimer_stop(s_timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gptimer_stop failed: %s", esp_err_to_name(err));
    }

    /* m2: Clear any pending FIRE_NOTIFY_BIT that may have been posted by the ISR
     * between gptimer_stop() and this point, to avoid spurious EVT_FIRE_PULSE_DONE. */
    if (s_target_task) {
        ulTaskNotifyValueClear(s_target_task, FIRE_NOTIFY_BIT);
    }

    ESP_LOGI(TAG, "Fire timer stopped");
}
