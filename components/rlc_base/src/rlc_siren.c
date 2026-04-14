/**
 * RLC Siren Control Implementation
 */

#include "rlc_siren.h"
#include "pin_config.h"
#include "rlc_config.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "rlc_siren";

static esp_timer_handle_t s_siren_timer = NULL;
static bool s_siren_on = false;
static int s_pulse_count = 0;   /* -1 = infinite, >0 = remaining cycles */

static inline void siren_drive(bool on)
{
    int level = on ? PIN_SIREN_ACTIVE : !PIN_SIREN_ACTIVE;
    gpio_set_level(PIN_SIREN, level);
    s_siren_on = on;
}

static void siren_timer_cb(void *arg)
{
    if (s_pulse_count == 0) {
        siren_drive(false);
        esp_timer_stop(s_siren_timer);
        return;
    }

    /* Toggle */
    siren_drive(!s_siren_on);

    if (s_siren_on == false && s_pulse_count > 0) {
        /* Just turned off — count one complete cycle */
        s_pulse_count--;
    }
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

    esp_timer_create_args_t timer_args = {
        .callback = siren_timer_cb,
        .name     = "siren_pulse",
    };
    esp_timer_create(&timer_args, &s_siren_timer);

    ESP_LOGI(TAG, "Siren initialised on GPIO %d", PIN_SIREN);
}

void siren_start_pulse(void)
{
    esp_timer_stop(s_siren_timer);
    s_pulse_count = -1;  /* Infinite */
    siren_drive(true);
    esp_timer_start_periodic(s_siren_timer, 500 * 1000);  /* 500 ms */
}

void siren_start_continuous(void)
{
    esp_timer_stop(s_siren_timer);
    s_pulse_count = 0;
    siren_drive(true);
}

void siren_start_link_lost(void)
{
    esp_timer_stop(s_siren_timer);
    s_pulse_count = 4;  /* 4 cycles */
    siren_drive(true);
    esp_timer_start_periodic(s_siren_timer, 500 * 1000);  /* 500 ms on/off */
}

void siren_off(void)
{
    esp_timer_stop(s_siren_timer);
    siren_drive(false);
}

void siren_start_error(void)
{
    esp_timer_stop(s_siren_timer);
    s_pulse_count = 3;  /* 3 short blasts */
    siren_drive(true);
    esp_timer_start_periodic(s_siren_timer, 200 * 1000);  /* 200 ms on/off */
}
