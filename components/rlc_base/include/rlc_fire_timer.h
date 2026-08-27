/**
 * RLC Fire Pulse Timer (Base Unit)
 *
 * Hardware GPTimer for precise fire pulse duration.
 * ISR signals state machine task via xTaskNotifyFromISR — never
 * drives GPIOs or calls state machine logic directly (FSD §9.12).
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * Initialise the fire pulse GPTimer. Must be called once at boot.
 */
void fire_timer_init(void);

/**
 * Start a one-shot fire pulse timer.
 *
 * Safe to call regardless of the timer's current run state: the timer is
 * stopped first (BF-01 — a one-shot alarm that expired normally leaves the
 * GPTimer in RUN state).
 *
 * @param duration_ms  Fire pulse duration in milliseconds
 * @param channel      Channel number (1-8), passed as context to ISR
 * @param target_task  Task to notify on expiry (state machine task)
 * @return ESP_OK when the pulse timer is running. On any other value the
 *         caller MUST make the fire path safe and latch ERROR — the pulse has
 *         no timed end. Never aborts: a panic here would leave the igniter
 *         energised for the reboot interval.
 */
esp_err_t fire_timer_start(uint32_t duration_ms, uint8_t channel, TaskHandle_t target_task);

/**
 * Stop the fire timer immediately (for CEASE_FIRE).
 */
void fire_timer_stop(void);
