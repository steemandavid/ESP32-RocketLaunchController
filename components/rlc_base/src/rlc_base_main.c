/**
 * RLC Base Unit — Application Entry Point
 *
 * Phase 2: All I/O tasks running — continuity ADC, arm sense debounce,
 * battery monitoring, and STATUS_UPDATE generation with real data.
 *
 * Boot sequence follows FSD §9.13.
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
#include "rlc_selftest.h"
#include "rlc_config.h"
#include "rlc_version.h"
#include "pin_config.h"

/* Phase 2 headers */
#include "rlc_continuity.h"
#include "rlc_arm_sense.h"
#include "rlc_base_battery.h"
#include "rlc_status_update.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rlc_base";

/**
 * STATUS_UPDATE trigger — called when continuity changes.
 * Runs from continuity_task context, so it must be minimal (just set a flag).
 */
static void on_io_change(void)
{
    status_update_trigger();
}

/**
 * Arm sense callback wrapper — adapts arm_sense_register_cb signature
 * (void (*cb)(bool armed)) to the simple trigger function.
 */
static void on_arm_change_cb(bool armed)
{
    (void)armed;
    status_update_trigger();
}

void base_app_main(void)
{
    ESP_LOGI(TAG, "=== RLC Base Unit v%s ===", RLC_VERSION_STRING);

    /* §9.13 Step 1: GPIO safe state FIRST — before any other init. */
    relay_init();
    siren_init();
    ESP_LOGI(TAG, "safety outputs initialised — all relays safe");

    /* §9.13 Step 2-3: Boot self-tests (CRC32-C, struct offsets) */
    if (rlc_selftest_run() != 0) {
        ESP_LOGE(TAG, "self-tests FAILED — halting");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        vTaskDelay(portMAX_DELAY);
    }

    rlc_rgb_led_init();
    rlc_rgb_led_set_pattern(LED_PATTERN_BOOT);
    rlc_rgb_led_set_pixel_count(8);  /* Base unit has 8-pixel strip */

    /* §9.13 Step 4: Initialise ADC calibration + battery */
    rlc_battery_init(PIN_VBAT_ADC, BASE_VBAT_DIVIDER_RATIO);

    /* §9.13 Step 5: Initialise ESP-NOW */
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

    /* §9.13 Step 7: Configure input GPIOs + start debounce engines */
    arm_sense_init();
    continuity_init();   /* Configures ADC1 for GPIO 2,10,4-9 */

    /* Register I/O change callbacks for event-driven STATUS_UPDATE */
    continuity_register_change_cb(on_io_change);
    arm_sense_register_cb(on_arm_change_cb);

    /* §9.13 Step 8: Configure hardware watchdog + TWDT */
    rlc_watchdog_init();

    /* §9.13 Step 9: Start FreeRTOS tasks */
    /* Priority 7 — arm switch (highest safety) */
    arm_sense_start_task();
    /* Priority 5 — continuity ADC sampling */
    continuity_start_task();
    /* Priority 3 — battery monitoring */
    base_battery_start_task();
    /* Priority 3 — STATUS_UPDATE generation */
    status_update_start_task();

    /* §9.13 Step 10: Begin link establishment */
    if (rlc_link_init(RLC_LINK_ROLE_BASE, remote_mac) != 0) {
        ESP_LOGE(TAG, "link manager init failed");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        return;
    }

    ESP_LOGI(TAG, "base ready — all Phase 2 tasks running, waiting for LINK_REQUEST");

    /* Housekeeping loop — watchdog + status log */
    int64_t last_status_log_ms = 0;
    while (1) {
        rlc_watchdog_feed();

        int64_t now = esp_timer_get_time() / 1000;
        if (now - last_status_log_ms >= 5000) {
            rlc_link_status_t ls;
            rlc_link_get_status(&ls);
            uint16_t bands = continuity_get_bands();
            ESP_LOGI(TAG, "state=%d rssi=%d vbat=%u mv cont=0x%04x arm=%d",
                     ls.state, ls.rssi_avg_dbm, rlc_battery_get_voltage_mv(),
                     bands, arm_sense_get_debounced());
            last_status_log_ms = now;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
