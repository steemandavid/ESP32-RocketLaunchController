/**
 * RLC Remote Unit — Application Entry Point
 *
 * Phase 1: Boot, bring up ESP-NOW, start the link manager, and let it
 * drive LINK_REQUEST/heartbeat transmission on its own task. This
 * function is a housekeeping loop (watchdog, battery sample, status log).
 */

#include "rlc_remote.h"
#include "rlc_remote_state.h"
#include "rlc_encoder.h"
#include "rlc_buzzer.h"
#include "rlc_display.h"
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

static const char *TAG = "rlc_remote";

void remote_app_main(void)
{
    ESP_LOGI(TAG, "=== RLC Remote Unit v%s ===", RLC_VERSION_STRING);

    rlc_rgb_led_init();
    rlc_rgb_led_set_pattern(LED_PATTERN_BOOT);

    display_init();
    encoder_init();
    buzzer_init();

    rlc_battery_init(PIN_VBAT_ADC, REMOTE_VBAT_DIVIDER_RATIO);

    if (rlc_espnow_init() != 0) {
        ESP_LOGE(TAG, "ESP-NOW init failed");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        display_error("ESP-NOW INIT FAILED");
        return;
    }

    uint8_t base_mac[] = BASE_MAC_ADDR;
    int retries = 3;
    while (rlc_espnow_add_peer(base_mac) != 0 && retries-- > 0) {
        ESP_LOGW(TAG, "peer registration failed, retrying (%d left)", retries);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (retries < 0) {
        ESP_LOGE(TAG, "peer registration failed — ERROR");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        display_error("PEER REGISTRATION FAILED");
        return;
    }

    if (rlc_link_init(RLC_LINK_ROLE_REMOTE, base_mac) != 0) {
        ESP_LOGE(TAG, "link manager init failed");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        return;
    }

    rlc_watchdog_init();
    ESP_LOGI(TAG, "remote ready — link manager driving handshake");

    int64_t last_log_ms = 0;
    while (1) {
        rlc_watchdog_feed();

        uint16_t vbat = rlc_battery_sample();
        rlc_link_set_remote_battery_mv(vbat);

        int64_t now = esp_timer_get_time() / 1000;
        if (now - last_log_ms >= 5000) {
            rlc_link_status_t ls;
            rlc_link_get_status(&ls);
            ESP_LOGI(TAG, "state=%d rssi=%d missed=%u vbat=%u mv",
                     ls.state, ls.rssi_avg_dbm, ls.missed_pings, vbat);
            last_log_ms = now;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
