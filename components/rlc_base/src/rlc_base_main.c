/**
 * RLC Base Unit — Application Entry Point
 *
 * Phase 1: Boot, initialise GPIOs (safe state first), bring up ESP-NOW,
 * start the link manager, and respond to heartbeats. The link manager
 * runs on its own task; this function becomes a housekeeping loop that
 * samples the battery, feeds the watchdog, and logs status.
 */

#include "rlc_base.h"
#include "rlc_relay.h"
#include "rlc_siren.h"
#include "rlc_base_state.h"
#include "rlc_espnow.h"
#include "rlc_link.h"
#include "rlc_message.h"
#include "rlc_battery.h"
#include "rlc_rgb_led.h"
#include "rlc_watchdog.h"
#include "rlc_config.h"
#include "rlc_version.h"
#include "pin_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rlc_base";

void base_app_main(void)
{
    ESP_LOGI(TAG, "=== RLC Base Unit v%s ===", RLC_VERSION_STRING);

    /* §9.7: GPIO safe state FIRST — before any other init. */
    relay_init();
    siren_init();
    ESP_LOGI(TAG, "safety outputs initialised — all relays safe");

    rlc_rgb_led_init();
    rlc_rgb_led_set_pattern(LED_PATTERN_BOOT);

    rlc_battery_init(PIN_VBAT_ADC, BASE_VBAT_DIVIDER_RATIO);

    if (rlc_espnow_init() != 0) {
        ESP_LOGE(TAG, "ESP-NOW init failed");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        return;
    }

    uint8_t remote_mac[] = REMOTE_MAC_ADDR;
    int retries = 3;
    while (rlc_espnow_add_peer(remote_mac) != 0 && retries-- > 0) {
        ESP_LOGW(TAG, "peer registration failed, retrying (%d left)", retries);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (retries < 0) {
        ESP_LOGE(TAG, "peer registration failed — ERROR");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        return;
    }

    if (rlc_link_init(RLC_LINK_ROLE_BASE, remote_mac) != 0) {
        ESP_LOGE(TAG, "link manager init failed");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        return;
    }

    rlc_watchdog_init();
    ESP_LOGI(TAG, "base ready — waiting for LINK_REQUEST");

    int64_t last_status_log_ms = 0;
    while (1) {
        rlc_watchdog_feed();
        rlc_battery_sample();

        int64_t now = esp_timer_get_time() / 1000;
        if (now - last_status_log_ms >= 5000) {
            rlc_link_status_t ls;
            rlc_link_get_status(&ls);
            ESP_LOGI(TAG, "state=%d rssi=%d vbat=%u mv",
                     ls.state, ls.rssi_avg_dbm, rlc_battery_get_voltage_mv());
            last_status_log_ms = now;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
