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
#include "rlc_faultinject.h"

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

    /* T-A11 injection. Deliberately AFTER the is_linked() check and inside
     * this function only: heartbeats live in the link task and are untouched,
     * so the link stays healthy while the remote's cached status ages past
     * STATUS_STALE_TIMEOUT_MS. That "linked but stale" state is exactly what
     * T-A11 needs and is unreachable any other way — jamming the radio trips
     * link loss at 1.5 s instead. Compiles to nothing in a normal build. */
    if (fault_inject_suppress_status()) {
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

    /* Test builds only: report IDLE while really in ERROR, so the remote's
     * local ERROR guard stays quiet and an ARM actually reaches the base —
     * the only way to exercise NACK_BASE_ERROR. error_flags below is left
     * truthful on purpose. Compiles to nothing in a normal build. */
    if (fault_inject_lie_state() && p.base_state == STATE_ERROR) {
        p.base_state = STATE_IDLE;
    }

    /* Error flags from the FSM */
    p.error_flags = base_state_get_error_flags();

    /* CI-02 / FSD §13.1 bit 0: ERR_VBAT_LOW. The FSM never raises this — it
     * only ever latches ERR_VBAT_CRITICAL, which is terminal — so the bit was
     * dead and the remote could not show a base "VBAT LOW" warning at all.
     * Arming is still refused from the live reading (guard 8 → NACK 0x09);
     * this is the advisory that tells the operator *before* they try.
     *
     * Derived here rather than latched in the FSM on purpose: it is a live
     * condition that must clear when the pack recovers (or when a bench supply
     * is turned up), unlike the latched fault flags around it. 0 mV means the
     * ADC has not produced a sample yet, not a flat pack. */
    uint16_t vbat_mv = p.battery_voltage_mv;
    if (vbat_mv > 0 && vbat_mv < BASE_VBAT_MIN_ARM_MV) {
        p.error_flags |= ERR_VBAT_LOW;
    }

    /* update_sequence is managed by the link manager */
    rlc_link_send_status_update(&p);
}

static void status_update_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);   /* 5.11: self-register (see rlc_base_battery.c) */
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
    /* m9: checked. No status task means the remote never gets a STATUS_UPDATE,
     * so its ARM guard 3 (fresh status) refuses everything — safe, but the
     * cause would be invisible without this line. */
    if (xTaskCreatePinnedToCore(status_update_task, "stupd_task", 4096, NULL, 3,
                                NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "status update task create FAILED — remote will see no status");
        return;
    }
    ESP_LOGI(TAG, "status update task started (prio 3, core 0)");
}
