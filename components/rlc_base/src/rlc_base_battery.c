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

/* 5.13: priority boost during the ADC read (see battery_task). Must stay
 * below configMAX_PRIORITIES — enforced at compile time. */
#define BATTERY_ADC_BOOST_PRIO  24
_Static_assert(BATTERY_ADC_BOOST_PRIO < configMAX_PRIORITIES,
               "ADC boost priority exceeds configMAX_PRIORITIES");

static void battery_task(void *arg)
{
    (void)arg;
    /* 5.11: self-register at task entry. Registering from the creator after
     * xTaskCreate races this task's first esp_task_wdt_reset() (spawned at a
     * higher priority than the creator) and produces the "task not found"
     * TWDT error bursts seen at boot. */
    esp_task_wdt_add(NULL);
    bool critical_posted = false;  /* edge-trigger so we only post once per crossing */

    /* Delay first read until WiFi/ESP-NOW init completes. The ADC driver
     * shares a lock between ADC1 and ADC2; WiFi holds it during ADC2
     * calibration which can take several hundred milliseconds at startup.
     * Feed the watchdog during the delay to avoid a WDT timeout. */
    for (int i = 0; i < 3; i++) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    while (1) {
        /* Boost priority during ADC read to prevent priority inversion
         * with WiFi driver (prio 23) over the shared ADC hardware lock. */
        int orig_prio = uxTaskPriorityGet(NULL);
        vTaskPrioritySet(NULL, BATTERY_ADC_BOOST_PRIO);
        rlc_battery_sample();
        vTaskPrioritySet(NULL, orig_prio);
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
    /* m9: checked. Without this task rlc_battery_get_voltage_mv() stays 0,
     * so guard 8 refuses every ARM (safe) but EVT_BATTERY_CRITICAL is never
     * posted either — worth a loud line rather than silence. */
    if (xTaskCreatePinnedToCore(battery_task, "battery_task", 3072, NULL, 3,
                                NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "battery task create FAILED — no VBAT monitoring");
        return;
    }
    ESP_LOGI(TAG, "battery task started (prio 3, core 0)");
}
