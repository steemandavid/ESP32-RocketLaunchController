/**
 * RLC Siren Control Implementation
 */

#include "rlc_siren.h"
#include "pin_config.h"
#include "rlc_config.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "rlc_siren";

static esp_timer_handle_t s_siren_timer = NULL;
static bool s_siren_on = false;
static int s_pulse_count = 0;   /* -1 = infinite, >0 = remaining cycles */

/* 5.4: the esp_timer callback (esp_timer task context) races the FSM-task
 * start/stop calls on s_siren_on/s_pulse_count — a toggle interleaved with
 * siren_off() leaves the siren stuck ON. All pattern state is mutated under
 * this mutex. A mutex (not a critical section) because the guarded sections
 * call esp_timer_start/stop, which may not run with interrupts disabled. */
static SemaphoreHandle_t s_siren_mu = NULL;

static void siren_lock(void)
{
    if (s_siren_mu) xSemaphoreTake(s_siren_mu, portMAX_DELAY);
}
static void siren_unlock(void)
{
    if (s_siren_mu) xSemaphoreGive(s_siren_mu);
}

static inline void siren_drive(bool on)
{
    int level = on ? PIN_SIREN_ACTIVE : !PIN_SIREN_ACTIVE;
    gpio_set_level(PIN_SIREN, level);
    s_siren_on = on;
}

static void siren_timer_cb(void *arg)
{
    siren_lock();
    if (s_pulse_count == 0) {
        siren_drive(false);
        esp_timer_stop(s_siren_timer);
        siren_unlock();
        return;
    }

    /* Toggle */
    siren_drive(!s_siren_on);

    if (s_siren_on == false && s_pulse_count > 0) {
        /* Just turned off — count one complete cycle */
        s_pulse_count--;
    }
    siren_unlock();
}

void siren_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_SIREN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    siren_drive(false);

    s_siren_mu = xSemaphoreCreateMutex();

    esp_timer_create_args_t timer_args = {
        .callback = siren_timer_cb,
        .name     = "siren_pulse",
    };
    esp_timer_create(&timer_args, &s_siren_timer);

    ESP_LOGI(TAG, "Siren initialised on GPIO %d", PIN_SIREN);
}

void siren_start_pulse(void)
{
    siren_lock();
    esp_timer_stop(s_siren_timer);
    s_pulse_count = -1;  /* Infinite */
    siren_drive(true);
    esp_timer_start_periodic(s_siren_timer, 500 * 1000);  /* 500 ms */
    siren_unlock();
}

void siren_start_continuous(void)
{
    siren_lock();
    esp_timer_stop(s_siren_timer);
    s_pulse_count = 0;
    siren_drive(true);
    siren_unlock();
}

void siren_start_link_lost(void)
{
    siren_lock();
    esp_timer_stop(s_siren_timer);
    s_pulse_count = 4;  /* 4 cycles */
    siren_drive(true);
    esp_timer_start_periodic(s_siren_timer, 500 * 1000);  /* 500 ms on/off */
    siren_unlock();
}

void siren_off(void)
{
    siren_lock();
    esp_timer_stop(s_siren_timer);
    siren_drive(false);
    siren_unlock();
}

void siren_start_error(void)
{
    siren_lock();
    esp_timer_stop(s_siren_timer);
    s_pulse_count = 3;  /* 3 short blasts */
    siren_drive(true);
    esp_timer_start_periodic(s_siren_timer, 200 * 1000);  /* 200 ms on/off */
    siren_unlock();
}
