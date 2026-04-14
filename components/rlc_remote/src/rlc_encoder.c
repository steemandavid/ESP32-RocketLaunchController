/**
 * RLC Rotary Encoder Driver
 *
 * Interrupt-driven quadrature decoder with push button.
 * Long-press detection (500 ms) for ARM confirm (FSD §5.5.1).
 */

#include "rlc_encoder.h"
#include "rlc_debounce.h"
#include "rlc_config.h"
#include "pin_config.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "rlc_enc";

#define ENCODER_LONG_PRESS_MS  500

static uint8_t s_channel = 1;
static rlc_encoder_rotate_cb_t s_rotate_cb = NULL;
static rlc_encoder_press_cb_t s_press_cb = NULL;
static rlc_encoder_long_press_cb_t s_long_press_cb = NULL;
static rlc_debounce_t s_button_db;
static int64_t s_last_rotate_us = 0;

/* Long-press state tracking */
static bool s_button_debounced_pressed = false;
static int64_t s_press_start_us = 0;
static bool s_long_press_fired = false;

#define ENCODER_LOCKOUT_US  5000  /* 5 ms lockout for A/B */

static void IRAM_ATTR encoder_isr(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_rotate_us < ENCODER_LOCKOUT_US) return;
    s_last_rotate_us = now;

    int clk = gpio_get_level(PIN_ENCODER_CLK);
    int dt  = gpio_get_level(PIN_ENCODER_DT);

    if (clk != dt) {
        /* CW rotation — increment */
        s_channel = (s_channel % NUM_CHANNELS) + 1;
    } else {
        /* CCW rotation — decrement */
        s_channel = (s_channel == 1) ? NUM_CHANNELS : (s_channel - 1);
    }

    if (s_rotate_cb) {
        s_rotate_cb(s_channel);
    }
}

static void button_change_cb(int gpio_num, bool new_state, void *user_data)
{
    if (new_state) {
        /* Button pressed (LOW = active) — start long-press timer */
        s_button_debounced_pressed = true;
        s_press_start_us = esp_timer_get_time();
        s_long_press_fired = false;
    } else {
        /* Button released */
        if (s_button_debounced_pressed && !s_long_press_fired && s_press_cb) {
            /* Short press — button released before 500 ms */
            s_press_cb();
        }
        s_button_debounced_pressed = false;
        s_long_press_fired = false;
    }
}

void encoder_init(void)
{
    /* Configure CLK and DT as interrupt inputs with pull-up */
    gpio_config_t ab_cfg = {
        .pin_bit_mask = (1ULL << PIN_ENCODER_CLK) | (1ULL << PIN_ENCODER_DT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&ab_cfg);

    /* Configure push button as input with pull-up (debounced) */
    gpio_config_t sw_cfg = {
        .pin_bit_mask = (1ULL << PIN_ENCODER_SW),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&sw_cfg);

    /* Init button debounce (16-bit, 160ms) */
    rlc_debounce_init(&s_button_db, PIN_ENCODER_SW, DEBOUNCE_16BIT);

    /* Install ISR for CLK pin */
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_ENCODER_CLK, encoder_isr, NULL);

    ESP_LOGI(TAG, "Encoder initialised (CLK=%d, DT=%d, SW=%d)",
             PIN_ENCODER_CLK, PIN_ENCODER_DT, PIN_ENCODER_SW);
}

void encoder_register_rotate_cb(rlc_encoder_rotate_cb_t cb)
{
    s_rotate_cb = cb;
}

void encoder_register_press_cb(rlc_encoder_press_cb_t cb)
{
    s_press_cb = cb;
}

void encoder_register_long_press_cb(rlc_encoder_long_press_cb_t cb)
{
    s_long_press_cb = cb;
}

uint8_t encoder_get_channel(void)
{
    return s_channel;
}

void encoder_poll_button(void)
{
    int level = gpio_get_level(PIN_ENCODER_SW);
    rlc_debounce_update(&s_button_db, level, button_change_cb, NULL);

    /* Check for long-press timeout */
    if (s_button_debounced_pressed && !s_long_press_fired && s_long_press_cb) {
        int64_t elapsed_us = esp_timer_get_time() - s_press_start_us;
        if (elapsed_us >= (int64_t)ENCODER_LONG_PRESS_MS * 1000) {
            s_long_press_fired = true;
            s_long_press_cb();
        }
    }
}
