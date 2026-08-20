/**
 * RLC Base Unit STATUS_UPDATE Generation Task
 *
 * Generates STATUS_UPDATE messages with real I/O data.
 * FSD §6.4.3: periodic (2000 ms) + event-driven (on change).
 */

#include "rlc_status_update.h"
#include "rlc_continuity.h"
#include "rlc_arm_sense.h"
#include "rlc_battery.h"
#include "rlc_base_state.h"
#include "rlc_link.h"
#include "rlc_config.h"
#include "rlc_watchdog.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

static const char *TAG = "rlc_stupd";

static volatile bool s_trigger = false;

void status_update_trigger(void)
{
    s_trigger = true;
}

static void send_update(void)
{
    if (!rlc_link_is_linked()) {
        return;
    }

    rlc_payload_status_update_t p = {0};

    /* Real continuity bands from ADC module */
    p.continuity_bands = continuity_get_bands();

    /* Armed/firing bitmasks from FSM state */
    uint8_t armed_ch = base_state_get_armed_channel();
    uint8_t firing_ch = base_state_get_firing_channel();
    p.channel_armed_bitmask  = (armed_ch > 0)  ? (1U << (armed_ch - 1))  : 0;
    p.channel_firing_bitmask = (firing_ch > 0) ? (1U << (firing_ch - 1)) : 0;

    /* Two distinct signals. Previously both carried the key switch (debounced
     * and raw), so the remote could never see the arm relay — and the ARMED
     * screen's "SENSE CONFIRMED" was derived from the key, not the sense. */
    p.base_key_switch = key_sense_get_debounced() ? 1 : 0;   /* GPIO 42 */
    p.base_arm_sense  = arm_sense_get_debounced() ? 1 : 0;   /* GPIO 21 */

    /* Battery voltage */
    p.battery_voltage_mv = rlc_battery_get_voltage_mv();

    /* State machine state */
    p.base_state = base_state_get();

    /* Error flags from the FSM */
    p.error_flags = base_state_get_error_flags();

    /* update_sequence is managed by the link manager */
    rlc_link_send_status_update(&p);
}

static void status_update_task(void *arg)
{
    (void)arg;
    int64_t last_send_ms = 0;

    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000;

        bool periodic = (now_ms - last_send_ms) >= STATUS_UPDATE_INTERVAL_MS;
        /* m7: Read and clear in one step to narrow the race window. */
        bool event = s_trigger;
        if (event) s_trigger = false;

        if ((periodic || event) && rlc_link_is_linked()) {
            send_update();
            last_send_ms = now_ms;
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void status_update_init(void)
{
    /* Nothing to init yet — continuity and arm sense must be started first */
}

void status_update_start_task(void)
{
    TaskHandle_t handle;
    xTaskCreatePinnedToCore(status_update_task, "stupd_task", 4096, NULL, 3, &handle, 0);
    rlc_watchdog_add_task(handle);
    ESP_LOGI(TAG, "status update task started (prio 3, core 0)");
}
