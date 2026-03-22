/**
 * RLC Relay Control Implementation
 */

#include "rlc_relay.h"
#include "pin_config.h"
#include "rlc_config.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "rlc_relay";

/* Channel relay GPIO lookup table */
static const int s_relay_pins[NUM_CHANNELS] = {
    PIN_RELAY_CH1, PIN_RELAY_CH2, PIN_RELAY_CH3, PIN_RELAY_CH4,
    PIN_RELAY_CH5, PIN_RELAY_CH6, PIN_RELAY_CH7, PIN_RELAY_CH8,
};

/**
 * Drive a GPIO output considering polarity.
 * active=true means "relay should be engaged".
 */
static inline void drive_output(int gpio, bool active, int active_level)
{
    int level = active ? active_level : !active_level;
    gpio_set_level(gpio, level);
}

void relay_init(void)
{
    /* Configure all channel relay outputs — inactive state FIRST (§9.7) */
    for (int i = 0; i < NUM_CHANNELS; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << s_relay_pins[i]),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
        drive_output(s_relay_pins[i], false, PIN_RELAY_CH_ACTIVE);
    }

    /* Low-side relay — inactive (open) */
    gpio_config_t ls_cfg = {
        .pin_bit_mask = (1ULL << PIN_LOWSIDE_RELAY),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&ls_cfg);
    drive_output(PIN_LOWSIDE_RELAY, false, PIN_LOWSIDE_RELAY_ACTIVE);

    /* Relay feedback input — pull-up, digital input */
    gpio_config_t fb_cfg = {
        .pin_bit_mask = (1ULL << PIN_RELAY_FEEDBACK),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&fb_cfg);

    ESP_LOGI(TAG, "Relay GPIOs initialised — all safe");
}

void relay_channel_set(uint8_t channel, bool state)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return;
    }
    drive_output(s_relay_pins[channel - 1], state, PIN_RELAY_CH_ACTIVE);
}

void relay_channel_all_off(void)
{
    for (int i = 0; i < NUM_CHANNELS; i++) {
        drive_output(s_relay_pins[i], false, PIN_RELAY_CH_ACTIVE);
    }
}

void relay_lowside_set(bool state)
{
    drive_output(PIN_LOWSIDE_RELAY, state, PIN_LOWSIDE_RELAY_ACTIVE);
}

void relay_all_safe(void)
{
    relay_channel_all_off();
    relay_lowside_set(false);
}

bool relay_feedback_is_safe(void)
{
    /* HIGH = no current (safe), LOW = current detected (fault) */
    return gpio_get_level(PIN_RELAY_FEEDBACK) == 1;
}
