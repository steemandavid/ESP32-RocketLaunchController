/**
 * RLC Display Driver — Stub (Phase 4)
 *
 * ILI9488 SPI driver and screen layouts will be implemented in Phase 4.
 * For now, all display functions log to console.
 */

#include "rlc_display.h"
#include "rlc_version.h"

#include "esp_log.h"

static const char *TAG = "rlc_disp";

int display_init(void)
{
    ESP_LOGI(TAG, "Display init (stub — Phase 4)");
    return 0;
}

void display_splash(int attempt, int max_attempts)
{
    ESP_LOGI(TAG, "=== RLC v%s === Connecting... attempt %d/%d",
             RLC_VERSION_STRING, attempt, max_attempts);
}

void display_firmware_mismatch(const uint8_t *base_ver, const uint8_t *remote_ver)
{
    ESP_LOGW(TAG, "FIRMWARE MISMATCH — Base v%d.%d.%d / Remote v%d.%d.%d",
             base_ver[0], base_ver[1], base_ver[2],
             remote_ver[0], remote_ver[1], remote_ver[2]);
}

void display_main_status(void)
{
    ESP_LOGI(TAG, "[IDLE] Main status screen");
}

void display_armed(uint8_t channel)
{
    ESP_LOGI(TAG, "[ARMED] Channel %d armed — press FIRE to launch", channel);
}

void display_firing(uint8_t channel, uint32_t countdown_ms)
{
    if (countdown_ms > 0) {
        ESP_LOGI(TAG, "[PRE_FIRE] Channel %d — countdown: %lu ms", channel,
                 (unsigned long)countdown_ms);
    } else {
        ESP_LOGI(TAG, "[FIRING] Channel %d — IGNITION ACTIVE", channel);
    }
}

void display_link_lost(uint32_t seconds_since_contact, int ping_attempts)
{
    ESP_LOGW(TAG, "[LINK LOST] Last contact: %lu s ago, ping attempts: %d",
             (unsigned long)seconds_since_contact, ping_attempts);
}

void display_error(const char *error_text)
{
    ESP_LOGE(TAG, "[ERROR] %s", error_text);
}

void display_nack(const char *reason_text)
{
    ESP_LOGW(TAG, "[NACK] %s", reason_text);
}
