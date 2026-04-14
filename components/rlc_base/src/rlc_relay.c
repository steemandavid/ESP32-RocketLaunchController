/**
 * RLC Relay Control — FSD v1.10+ topology.
 *
 * Eight channel SPDT relays driven via IRLZ44N low-side MOSFETs.
 * One arm relay (GPIO 47) provides the primary fire-path interlock in
 * series with the physical key switch (hardware AND gate).
 */

#include "rlc_relay.h"
#include "pin_config.h"
#include "rlc_config.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "rlc_relay";

static const int s_channel_pins[NUM_CHANNELS] = {
    PIN_RELAY_CH1, PIN_RELAY_CH2, PIN_RELAY_CH3, PIN_RELAY_CH4,
    PIN_RELAY_CH5, PIN_RELAY_CH6, PIN_RELAY_CH7, PIN_RELAY_CH8,
};

static inline void drive_output(int gpio, bool active, int active_level)
{
    int level = active ? active_level : !active_level;
    gpio_set_level(gpio, level);
}

static void configure_output(int gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << gpio),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

void relay_init(void)
{
    /* §9.7 — outputs come up in their safe (inactive) state first. */
    for (int i = 0; i < NUM_CHANNELS; i++) {
        configure_output(s_channel_pins[i]);
        drive_output(s_channel_pins[i], false, PIN_RELAY_CH_ACTIVE);
    }

    configure_output(PIN_ARM_RELAY);
    drive_output(PIN_ARM_RELAY, false, PIN_ARM_RELAY_ACTIVE);

    ESP_LOGI(TAG, "relay GPIOs initialised — channels + arm relay safe");
}

void relay_fire_set(uint8_t channel, bool state)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        ESP_LOGE(TAG, "invalid channel %u", channel);
        return;
    }
    drive_output(s_channel_pins[channel - 1], state, PIN_RELAY_CH_ACTIVE);
}

void relay_fire_all_off(void)
{
    for (int i = 0; i < NUM_CHANNELS; i++) {
        drive_output(s_channel_pins[i], false, PIN_RELAY_CH_ACTIVE);
    }
}

void arm_relay_set(bool state)
{
    drive_output(PIN_ARM_RELAY, state, PIN_ARM_RELAY_ACTIVE);
}

void relay_all_safe(void)
{
    relay_fire_all_off();
    arm_relay_set(false);
}
