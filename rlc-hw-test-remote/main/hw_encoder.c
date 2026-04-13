#include "hw_encoder.h"
#include "pin_config.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "hw_enc";

static volatile int s_count     = 0;
static volatile int s_direction = 0;
static volatile uint8_t s_prev_state = 0;
static volatile int64_t s_last_step_us = 0;
static volatile int s_accum = 0;        /* raw pulse accumulator */

/* 2 ms lockout between counted steps (filters contact bounce) */
#define ENC_LOCKOUT_US  2000

/* Divider: require this many raw pulses in the same direction before
   outputting one count.  Increase to make the encoder less sensitive. */
#define ENC_DIVIDER    3

/*
 * Cycle-position decoder for half-step and full-step encoders.
 *
 * Each quadrature state (00, 01, 10, 11) maps to a position in the
 * CW rotation cycle.  Moving forward (+1) in the cycle is CW;
 * moving backward (−1, i.e. +3 mod 4) is CCW.
 *
 * CW  sequence:  3 → 2 → 0 → 1 → 3   (positions 0→1→2→3→0)
 * CCW sequence:  3 → 1 → 0 → 2 → 3   (positions 0→3→2→1→0)
 *
 * This gives the same direction for EVERY transition within a rotation,
 * unlike a Gray code lookup table which alternates on half-step encoders.
 *
 * State → cycle position mapping:
 *   state 0 (A=0 B=0) → position 2
 *   state 1 (A=0 B=1) → position 3
 *   state 2 (A=1 B=0) → position 1
 *   state 3 (A=1 B=1) → position 0
 */
static const uint8_t DRAM_ATTR s_cycle_pos[4] = { 2, 3, 1, 0 };

static void IRAM_ATTR enc_isr(void *arg)
{
    uint8_t a = gpio_get_level(PIN_ENCODER_A);
    uint8_t b = gpio_get_level(PIN_ENCODER_B);
    uint8_t cur = (a << 1) | b;

    if (cur == s_prev_state) return;

    int64_t now = esp_timer_get_time();
    uint8_t pos     = s_cycle_pos[cur];
    uint8_t prev_pos = s_cycle_pos[s_prev_state];

    /* Diff in the cycle: 1 = forward (CW), 3 = backward (CCW) */
    int diff = (pos - prev_pos + 4) & 3;

    if (diff == 1 || diff == 3) {
        if (now - s_last_step_us >= ENC_LOCKOUT_US) {
            int raw_dir = (diff == 3) ? 1 : -1;
            s_last_step_us = now;

            /* Accumulate raw pulses; only output on divider boundary */
            if (s_accum == 0 || (raw_dir > 0 && s_accum > 0)
                             || (raw_dir < 0 && s_accum < 0)) {
                s_accum += raw_dir;
            } else {
                /* Direction reversed — reset accumulator */
                s_accum = raw_dir;
            }

            if (s_accum >= ENC_DIVIDER) {
                s_count++;
                s_direction = 1;
                s_accum = 0;
            } else if (s_accum <= -ENC_DIVIDER) {
                s_count--;
                s_direction = -1;
                s_accum = 0;
            }
        }
    }

    s_prev_state = cur;
}

void hw_encoder_init(void)
{
    /* Configure A/B as inputs with pull-ups and interrupts */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_ENCODER_A) | (1ULL << PIN_ENCODER_B),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io);

    /* Sample initial state */
    s_prev_state = (gpio_get_level(PIN_ENCODER_A) << 1)
                 | gpio_get_level(PIN_ENCODER_B);

    /* Install GPIO ISR on both pins */
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_ENCODER_A, enc_isr, NULL);
    gpio_isr_handler_add(PIN_ENCODER_B, enc_isr, NULL);

    ESP_LOGI(TAG, "Encoder initialised (A=GPIO%d, B=GPIO%d)", PIN_ENCODER_A, PIN_ENCODER_B);
}

int enc_get_count(void)
{
    return s_count;
}

void enc_reset_count(void)
{
    s_count = 0;
}

int enc_get_direction(void)
{
    int d = s_direction;
    s_direction = 0;
    return d;
}
