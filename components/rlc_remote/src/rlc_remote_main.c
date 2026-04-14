/**
 * RLC Remote Unit — Application Entry Point
 *
 * Phase 2: All I/O tasks running — fire button, arm switch, encoder,
 * battery monitoring, and link manager driving communication.
 *
 * Boot sequence follows FSD §9.13.
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
#include "rlc_selftest.h"
#include "rlc_config.h"
#include "rlc_version.h"
#include "pin_config.h"

/* Phase 2 headers */
#include "rlc_fire_button.h"
#include "rlc_arm_switch.h"
#include "rlc_remote_battery.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rlc_remote";

void remote_app_main(void)
{
    ESP_LOGI(TAG, "=== RLC Remote Unit v%s ===", RLC_VERSION_STRING);

    /* Visual feedback first (remote has no relays/safety GPIOs). */
    rlc_rgb_led_init();
    rlc_rgb_led_set_pattern(LED_PATTERN_BOOT);
    rlc_rgb_led_set_pixel_count(1);  /* Remote has single pixel */

    /* §9.13: Boot self-tests (CRC32-C, struct offsets) */
    if (rlc_selftest_run() != 0) {
        ESP_LOGE(TAG, "self-tests FAILED — halting");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        vTaskDelay(portMAX_DELAY);
    }

    display_init();
    encoder_init();
    buzzer_init();

    /* §9.13 Step 4: Initialise ADC calibration + battery */
    rlc_battery_init(PIN_VBAT_ADC, REMOTE_VBAT_DIVIDER_RATIO);

    /* §9.13 Step 5: Initialise ESP-NOW */
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

    /* §9.13 Step 7: Configure input GPIOs */
    fire_button_init();
    arm_switch_init();

    /* §9.13 Step 8: Configure hardware watchdog + TWDT */
    rlc_watchdog_init();

    /* §9.13 Step 9: Start FreeRTOS tasks */
    /* Priority 7 — fire button (highest safety) */
    fire_button_start_task();
    /* Priority 6 — arm switch */
    arm_switch_start_task();
    /* Priority 3 — battery monitoring */
    remote_battery_start_task();

    /* §9.13 Step 10: Begin link establishment */
    if (rlc_link_init(RLC_LINK_ROLE_REMOTE, base_mac) != 0) {
        ESP_LOGE(TAG, "link manager init failed");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        return;
    }

    ESP_LOGI(TAG, "remote ready — all Phase 2 tasks running, waiting for link");

    /* Housekeeping loop — watchdog, encoder poll, status log */
    int64_t last_log_ms = 0;
    while (1) {
        rlc_watchdog_feed();

        /* Poll encoder push button debounce + long-press timer (10 ms) */
        encoder_poll_button();

        int64_t now = esp_timer_get_time() / 1000;
        if (now - last_log_ms >= 5000) {
            rlc_link_status_t ls;
            rlc_link_get_status(&ls);
            ESP_LOGI(TAG, "state=%d rssi=%d missed=%u vbat=%u mv arm=%d fire=%d",
                     ls.state, ls.rssi_avg_dbm, ls.missed_pings,
                     rlc_battery_get_voltage_mv(), arm_switch_is_armed(),
                     fire_button_is_pressed());
            last_log_ms = now;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
