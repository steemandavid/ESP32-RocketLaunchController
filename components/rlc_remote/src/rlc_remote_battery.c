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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "rlc_rbat";

static rlc_battery_status_t s_status = BATTERY_OK;

static void battery_task(void *arg)
{
    (void)arg;

    while (1) {
        uint16_t mv = rlc_battery_sample();

        /* Three-threshold check (FSD §8.3.4) */
        s_status = rlc_battery_check(REMOTE_VBAT_MIN_ARM_MV,
                                     REMOTE_VBAT_MIN_OPERATE_MV,
                                     REMOTE_VBAT_CRITICAL_MV);

        /* Update link manager so battery goes out in PING payload */
        rlc_link_set_remote_battery_mv(mv);

        if (s_status == BATTERY_CRITICAL) {
            ESP_LOGW(TAG, "CRITICAL battery: %u mV (< %u)", mv, REMOTE_VBAT_CRITICAL_MV);
        } else if (s_status == BATTERY_LOW) {
            ESP_LOGW(TAG, "LOW battery: %u mV (< %u)", mv, REMOTE_VBAT_MIN_ARM_MV);
        } else if (s_status == BATTERY_WARNING) {
            ESP_LOGW(TAG, "WARNING battery: %u mV (< %u)", mv, REMOTE_VBAT_MIN_OPERATE_MV);
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void remote_battery_start_task(void)
{
    xTaskCreatePinnedToCore(battery_task, "battery_task", 2048, NULL, 3, NULL, 0);
    ESP_LOGI(TAG, "remote battery task started (prio 3, core 0)");
}

rlc_battery_status_t remote_battery_get_status(void)
{
    return s_status;
}
