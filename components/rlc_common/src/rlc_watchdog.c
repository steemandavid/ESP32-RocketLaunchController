/**
 * RLC Watchdog Timer
 */

#include "rlc_watchdog.h"
#include "rlc_config.h"

#include "esp_task_wdt.h"
#include "esp_log.h"

static const char *TAG = "rlc_wdt";

int rlc_watchdog_init(void)
{
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms       = WATCHDOG_TIMEOUT_S * 1000,
        .idle_core_mask   = 0,
        .trigger_panic    = true,
    };

    esp_err_t ret = esp_task_wdt_reconfigure(&wdt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Watchdog reconfigure failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = esp_task_wdt_add(NULL);  /* Add current task */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Watchdog add task failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGI(TAG, "Watchdog initialised with %d second timeout", WATCHDOG_TIMEOUT_S);
    return 0;
}

void rlc_watchdog_feed(void)
{
    esp_task_wdt_reset();
}
