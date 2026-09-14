#pragma once
/* Host-test stub for the one esp_system.h API the base FSM uses
 * (RLC-REVIEW-ALL-010 B-MIN1: ERR_WATCHDOG_RESET latching).
 * A clean power-on keeps the flag clear in every host test. */

typedef enum {
    ESP_RST_UNKNOWN = 0,
    ESP_RST_POWERON,
    ESP_RST_BROWNOUT,
    ESP_RST_SW,
    ESP_RST_PANIC,
    ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT,
    ESP_RST_WDT,
    ESP_RST_DEEPSLEEP,
    ESP_RST_SDIO,
} esp_reset_reason_t;

static inline esp_reset_reason_t esp_reset_reason(void)
{
    return ESP_RST_POWERON;
}
