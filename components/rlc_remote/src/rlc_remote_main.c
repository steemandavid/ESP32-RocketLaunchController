/**
 * RLC Remote Unit — Application Entry Point
 *
 * Phase 1: Boot, init display, init ESP-NOW,
 * send link requests, handle heartbeats.
 */

#include "rlc_remote.h"
#include "rlc_remote_state.h"
#include "rlc_encoder.h"
#include "rlc_buzzer.h"
#include "rlc_display.h"
#include "rlc_espnow.h"
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

static const char *TAG = "rlc_remote";

void remote_app_main(void)
{
    ESP_LOGI(TAG, "=== RLC Remote Unit v%s ===", RLC_VERSION_STRING);

    /* Initialise RGB LED */
    rlc_rgb_led_init();
    rlc_rgb_led_set_state(STATE_BOOT);

    /* Initialise display (stub for Phase 4) */
    display_init();

    /* Initialise inputs */
    encoder_init();
    buzzer_init();

    /* Initialise battery ADC */
    rlc_battery_init(PIN_VBAT_ADC, REMOTE_VBAT_DIVIDER_RATIO);

    /* Initialise ESP-NOW */
    if (rlc_espnow_init() != 0) {
        ESP_LOGE(TAG, "ESP-NOW init failed");
        rlc_rgb_led_set_state(STATE_ERROR);
        display_error("ESP-NOW INIT FAILED");
        return;
    }

    /* Register peer (base unit) */
    uint8_t base_mac[] = BASE_MAC_ADDR;
    int retries = 3;
    while (rlc_espnow_add_peer(base_mac) != 0 && retries-- > 0) {
        ESP_LOGW(TAG, "Peer registration failed, retrying... (%d left)", retries);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (retries < 0) {
        ESP_LOGE(TAG, "Peer registration failed after 3 retries — ERROR");
        rlc_rgb_led_set_state(STATE_ERROR);
        display_error("PEER REGISTRATION FAILED");
        return;
    }

    /* Initialise watchdog */
    rlc_watchdog_init();

    ESP_LOGI(TAG, "Remote unit initialised — sending link requests");

    /* Main loop — Phase 1 placeholder */
    int attempt = 0;
    while (1) {
        rlc_watchdog_feed();
        rlc_battery_sample();

        attempt++;
        display_splash(attempt, LINK_REQUEST_MAX_RETRIES);

        /* Poll encoder button */
        encoder_poll_button();

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
