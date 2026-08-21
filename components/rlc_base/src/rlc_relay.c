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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rlc_relay";

/* Track intended arm relay state for contact-weld detection */
static bool s_arm_relay_on = false;

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
    /* 4.13: an unchecked failure here would leave the pin floating — for
     * a relay drive that means an undefined fire path. Fatal at boot. */
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed for GPIO %d: %s — HALTING",
                 gpio, esp_err_to_name(ret));
        while (1) { vTaskDelay(portMAX_DELAY); }
    }
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

    if (FIRE_PROTECTED_CHANNEL_MASK != 0xFF) {
        ESP_LOGW(TAG, "bug #18 gate ACTIVE — firing allowed on mask 0x%02X only; "
                      "all other channels lack ADC clamp/snubber",
                 FIRE_PROTECTED_CHANNEL_MASK);
    }
}

void relay_fire_set(uint8_t channel, bool state)
{
    if (channel < 1 || channel > NUM_CHANNELS) {
        ESP_LOGE(TAG, "invalid channel %u", channel);
        return;
    }
    /* Bug #18 hardware gate — last line of defence. Closing an unprotected
     * channel relay onto a live fire bus can couple VBAT to its unclamped
     * continuity ADC input. De-energising is always allowed. */
    if (state && !CHANNEL_IS_PROTECTED(channel)) {
        ESP_LOGE(TAG, "REFUSED to energise ch %u relay — no ADC clamp/snubber "
                      "fitted (protected mask 0x%02X), see bug #18",
                 channel, FIRE_PROTECTED_CHANNEL_MASK);
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
    s_arm_relay_on = state;
    drive_output(PIN_ARM_RELAY, state, PIN_ARM_RELAY_ACTIVE);
}

bool arm_relay_get_intended(void)
{
    return s_arm_relay_on;
}

/**
 * Emergency safe: de-energise arm relay + all channel relays.
 *
 * IMPORTANT: The arm relay must be de-energised FIRST to remove VBAT from
 * the fire bus before channel relay contacts transition from NO back to NC.
 * If channel relays open while the arm relay still carries fire current,
 * the relay arc couples VBAT to the NC contact and destroys the continuity
 * ADC inputs (GPIO 2-10) which have no high-voltage clamping.
 *
 * A 20 ms delay after arm relay OFF ensures the contacts are fully open
 * and the fire current has decayed to zero before channel relays release.
 */
#define RELAY_ARM_RELEASE_MS  20

void relay_all_safe(void)
{
    arm_relay_set(false);
    vTaskDelay(pdMS_TO_TICKS(RELAY_ARM_RELEASE_MS));
    relay_fire_all_off();
}
