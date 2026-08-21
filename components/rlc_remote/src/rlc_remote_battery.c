/**
 * RLC Remote Unit Battery Monitoring Task
 *
 * Samples battery voltage at 1000 ms intervals with three thresholds.
 * FSD §8.3.4: MIN_ARM, MIN_OPERATE, CRITICAL.
 * Updates the link manager with battery voltage for PING payload.
 */

#include "rlc_remote_battery.h"
#include "rlc_battery.h"
#include "rlc_link.h"
#include "rlc_config.h"
#include "rlc_watchdog.h"
#include "rlc_remote_fsm.h"
#include "rlc_fsm_events.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "rlc_rbat";

/* 5.13: priority boost during the ADC read (see battery_task). Must stay
 * below configMAX_PRIORITIES — enforced at compile time. */
#define BATTERY_ADC_BOOST_PRIO  24
_Static_assert(BATTERY_ADC_BOOST_PRIO < configMAX_PRIORITIES,
               "ADC boost priority exceeds configMAX_PRIORITIES");

static void battery_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);   /* 5.11: self-register (see rlc_base_battery.c) */
    bool critical_posted = false;  /* edge-trigger */

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
         * with WiFi driver (prio 23) over the shared ADC hardware lock.
         * The ESP-IDF ADC driver uses a newlib lock (semaphore without
         * priority inheritance) shared between ADC1 and ADC2. */
        int orig_prio = uxTaskPriorityGet(NULL);
        vTaskPrioritySet(NULL, BATTERY_ADC_BOOST_PRIO);
        uint16_t mv = rlc_battery_sample();
        vTaskPrioritySet(NULL, orig_prio);

        /* Three-threshold check (FSD §8.3.4) */
        rlc_battery_status_t st = rlc_battery_check(REMOTE_VBAT_MIN_ARM_MV,
                                                    REMOTE_VBAT_MIN_OPERATE_MV,
                                                    REMOTE_VBAT_CRITICAL_MV);

        /* Update link manager so battery goes out in PING payload */
        rlc_link_set_remote_battery_mv(mv);

        if (st == BATTERY_CRITICAL) {
            ESP_LOGW(TAG, "CRITICAL battery: %u mV (< %u)", mv, REMOTE_VBAT_CRITICAL_MV);
            /* R8: post EVT_BATTERY_CRITICAL on first entry into critical band
             * (FSD §8.3.4). Edge-triggered to avoid queue spam. */
            if (!critical_posted && remote_fsm_get_queue()) {
                rlc_fsm_event_t evt = {0};
                evt.type = EVT_BATTERY_CRITICAL;
                if (xQueueSend(remote_fsm_get_queue(), &evt, pdMS_TO_TICKS(10)) == pdTRUE) {
                    critical_posted = true;
                }
            }
        } else if (st == BATTERY_LOW) {
            ESP_LOGW(TAG, "LOW battery: %u mV (< %u)", mv, REMOTE_VBAT_MIN_ARM_MV);
            critical_posted = false;
        } else if (st == BATTERY_WARNING) {
            ESP_LOGW(TAG, "WARNING battery: %u mV (< %u)", mv, REMOTE_VBAT_MIN_OPERATE_MV);
            critical_posted = false;
        } else {
            critical_posted = false;
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void remote_battery_start_task(void)
{
    if (xTaskCreatePinnedToCore(battery_task, "battery_task", 4096, NULL, 3,
                                NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "battery task create failed");
        return;
    }
    ESP_LOGI(TAG, "remote battery task started (prio 3, core 0)");
}
