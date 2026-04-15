/**
 * RLC Base Unit Battery Monitoring Task
 *
 * Samples battery voltage at 1000 ms intervals with threshold detection.
 * FSD §7.3.3: two thresholds (MIN_ARM, CRITICAL).
 */

#include "rlc_base_battery.h"
#include "rlc_battery.h"
#include "rlc_config.h"
#include "rlc_watchdog.h"
#include "rlc_base_fsm.h"
#include "rlc_fsm_events.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "rlc_bat";

static void battery_task(void *arg)
{
    (void)arg;
    bool critical_posted = false;  /* edge-trigger so we only post once per crossing */

    while (1) {
        rlc_battery_sample();
        uint16_t mv = rlc_battery_get_voltage_mv();

        if (mv < BASE_VBAT_CRITICAL_MV) {
            ESP_LOGW(TAG, "CRITICAL battery: %u mV (< %u)", mv, BASE_VBAT_CRITICAL_MV);
            /* J7: post EVT_BATTERY_CRITICAL on first entry into critical band so
             * the FSM transitions to ERROR (FSD §7.3.3). Edge-triggered to avoid
             * spamming the queue every 1 s. */
            if (!critical_posted && base_fsm_get_queue()) {
                rlc_fsm_event_t evt = {0};
                evt.type = EVT_BATTERY_CRITICAL;
                if (xQueueSend(base_fsm_get_queue(), &evt, pdMS_TO_TICKS(10)) == pdTRUE) {
                    critical_posted = true;
                }
            }
        } else if (mv < BASE_VBAT_MIN_ARM_MV) {
            ESP_LOGW(TAG, "LOW battery: %u mV (< %u)", mv, BASE_VBAT_MIN_ARM_MV);
            critical_posted = false;  /* Recovered above critical — re-arm edge */
        } else {
            critical_posted = false;
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void base_battery_start_task(void)
{
    TaskHandle_t handle;
    xTaskCreatePinnedToCore(battery_task, "battery_task", 3072, NULL, 3, &handle, 0);
    rlc_watchdog_add_task(handle);
    ESP_LOGI(TAG, "battery task started (prio 3, core 0)");
}
