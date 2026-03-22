/**
 * RLC Base Unit — Application Entry Point
 *
 * Phase 1: Boot, initialise GPIOs (safe state first),
 * init ESP-NOW, establish link, handle heartbeats.
 */

#include "rlc_base.h"
#include "rlc_relay.h"
#include "rlc_siren.h"
#include "rlc_base_state.h"
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
#include "esp_timer.h"

static const char *TAG = "rlc_base";

void base_app_main(void)
{
    ESP_LOGI(TAG, "=== RLC Base Unit v%s ===", RLC_VERSION_STRING);

    /* §9.7: GPIO safe state FIRST — before any other init */
    relay_init();
    siren_init();

    ESP_LOGI(TAG, "Safety outputs initialised — all relays safe");

    /* Initialise RGB LED */
    rlc_rgb_led_init();
    rlc_rgb_led_set_state(STATE_BOOT);

    /* Initialise battery ADC */
    rlc_battery_init(PIN_VBAT_ADC, BASE_VBAT_DIVIDER_RATIO);

    /* Initialise ESP-NOW */
    if (rlc_espnow_init() != 0) {
        ESP_LOGE(TAG, "ESP-NOW init failed");
        rlc_rgb_led_set_state(STATE_ERROR);
        return;
    }

    /* Register peer (remote unit) */
    uint8_t remote_mac[] = REMOTE_MAC_ADDR;
    int retries = 3;
    while (rlc_espnow_add_peer(remote_mac) != 0 && retries-- > 0) {
        ESP_LOGW(TAG, "Peer registration failed, retrying... (%d left)", retries);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (retries < 0) {
        ESP_LOGE(TAG, "Peer registration failed after 3 retries — ERROR");
        rlc_rgb_led_set_state(STATE_ERROR);
        return;
    }

    /* Initialise watchdog */
    rlc_watchdog_init();

    ESP_LOGI(TAG, "Base unit initialised — waiting for link request");

    /* Main loop — Phase 1 placeholder */
    while (1) {
        rlc_watchdog_feed();
        rlc_battery_sample();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
