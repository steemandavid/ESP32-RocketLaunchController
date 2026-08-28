/**
 * RLC Rotary Encoder Driver
 *
 * Interrupt-driven CYCLE-POSITION quadrature decoder with push button.
 * Long-press detection (500 ms) for ARM confirm (FSD §5.5.1).
 *
 * Decoding (FSD §5.5.1). The four quadrature states map to positions in the
 * CW rotation cycle:
 *
 *      state (A<<1)|B :  00    01    11    10
 *      cycle position :   0     1     2     3
 *
 * A transition is accepted only if the position advances by exactly one, in
 * either direction. Anything else — no movement, or a jump of two — is
 * discarded as a missed step or noise.
 *
 * This replaced a Gray-code level comparison (`if (clk != dt) CW else CCW`)
 * which sampled B at the instant of an edge on A. That gave two problems the
 * field reported as oversensitive selection: it has no notion of a legal
 * transition, so an electrical glitch produced a channel change in a
 * direction decided by whatever B happened to read; and every accepted edge
 * became a channel change, because the ENC_DIVIDER the spec requires was
 * never implemented. Contact bounce is now rejected inherently — bouncing one
 * line toggles between two adjacent states, so the accumulator oscillates
 * about zero and nets to nothing.
 *
 * ENC_REVERSED flips the sense to match how A and B are wired on this board.
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

/* INF-05: volatile — written from the quadrature ISR, read from the FSM and
 * display tasks, exactly like s_max_channel below. Xtensa reloads it in
 * practice today; under LTO it need not. */
static volatile uint8_t s_channel = 1;
/* RM-02: highest selectable channel. Narrowed to the base's advertised
 * num_channels once LINK_ACK arrives (FSD §8.2.2). */
static volatile uint8_t s_max_channel = NUM_CHANNELS;
static rlc_encoder_rotate_cb_t s_rotate_cb = NULL;
static rlc_encoder_press_cb_t s_press_cb = NULL;
static rlc_encoder_long_press_cb_t s_long_press_cb = NULL;
static rlc_debounce_t s_button_db;
static int64_t s_last_rotate_us = 0;

/* Long-press state tracking */
static bool s_button_debounced_pressed = false;
static int64_t s_press_start_us = 0;
static bool s_long_press_fired = false;

/* Cycle position for each quadrature state, indexed by (A<<1)|B. */
static const uint8_t s_cycle_pos[4] = { 0, 1, 3, 2 };

static uint8_t s_last_pos     = 0;      /* previous cycle position */
static int8_t  s_accum        = 0;      /* raw steps toward ENC_DIVIDER */
static bool    s_pos_valid    = false;  /* first edge only seeds the state */

/* Diagnostic counters — surfaced by encoder_get_stats() so a noisy input is
 * visible in the log rather than only felt as bad feel at the knob. */
static volatile uint32_t s_isr_count   = 0;   /* ISR entries after lockout */
static volatile uint32_t s_valid_count = 0;   /* legal single-step transitions */
static volatile uint32_t s_step_count  = 0;   /* channel changes emitted */

/**
 * Feed one quadrature sample. Returns +1 / -1 when a channel step should be
 * emitted, 0 otherwise. Pure apart from the module statics, so the host tests
 * can drive it directly.
 *
 * 5.12: IRAM_ATTR — called from encoder_isr. With CONFIG_GPIO_CTRL_FUNC_IN_IRAM
 * set, the whole decode path (this function, the ISR, gpio_get_level's inline
 * register read) stays IRAM-resident and survives flash cache misses.
 */
static int8_t IRAM_ATTR encoder_feed(uint8_t state)
{
    uint8_t pos = s_cycle_pos[state & 0x3];

    if (!s_pos_valid) {           /* seed on the first sample; emit nothing */
        s_last_pos = pos;
        s_pos_valid = true;
        return 0;
    }

    uint8_t delta = (uint8_t)((pos - s_last_pos) & 0x3);
    s_last_pos = pos;

    int8_t dir;
    if (delta == 1)      dir = +1;
    else if (delta == 3) dir = -1;
    else return 0;                /* 0 = no movement, 2 = illegal — discard */

#if ENC_REVERSED
    dir = (int8_t)-dir;           /* board wiring, see ENC_REVERSED */
#endif

    s_valid_count++;

    /* Reversal restarts the count, so a step only follows ENC_DIVIDER raw
     * pulses in the SAME direction (FSD §5.5.1). */
    if ((s_accum > 0 && dir < 0) || (s_accum < 0 && dir > 0)) s_accum = 0;
    s_accum += dir;

    if (s_accum >= ENC_DIVIDER)  { s_accum = 0; return +1; }
    if (s_accum <= -ENC_DIVIDER) { s_accum = 0; return -1; }
    return 0;
}

static void IRAM_ATTR encoder_isr(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    if (now - s_last_rotate_us < ENC_LOCKOUT_US) return;
    s_last_rotate_us = now;
    s_isr_count++;

    uint8_t state = (uint8_t)((gpio_get_level(PIN_ENCODER_CLK) << 1) |
                               gpio_get_level(PIN_ENCODER_DT));
    int8_t step = encoder_feed(state);
    if (step == 0) return;

    s_step_count++;
    /* RM-02: wrap at the channel count the base advertised in LINK_ACK, not
     * at this build's NUM_CHANNELS. Single-byte volatile read, so no lock is
     * needed in ISR context; the value only ever shrinks the usable range. */
    uint8_t max_ch = s_max_channel;
    if (s_channel > max_ch) s_channel = max_ch;
    if (step > 0) s_channel = (s_channel % max_ch) + 1;
    else          s_channel = (s_channel == 1) ? max_ch : (s_channel - 1);

    if (s_rotate_cb) s_rotate_cb(s_channel);
}

void encoder_set_max_channel(uint8_t max_channel)
{
    if (max_channel < 1 || max_channel > NUM_CHANNELS) return;
    s_max_channel = max_channel;
    if (s_channel > max_channel) s_channel = max_channel;
}

void encoder_get_stats(uint32_t *isr, uint32_t *valid, uint32_t *steps)
{
    if (isr)   *isr   = s_isr_count;
    if (valid) *valid = s_valid_count;
    if (steps) *steps = s_step_count;
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
        .intr_type    = GPIO_INTR_ANYEDGE,
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
    /* BOTH lines, both edges: the cycle-position decoder needs the full
     * quadrature sequence. Previously DT was configured to interrupt but had
     * no handler registered, so three of every four transitions were lost. */
    gpio_isr_handler_add(PIN_ENCODER_CLK, encoder_isr, NULL);
    gpio_isr_handler_add(PIN_ENCODER_DT,  encoder_isr, NULL);

    ESP_LOGI(TAG, "Encoder initialised (CLK=%d, DT=%d, SW=%d, divider=%d)",
             PIN_ENCODER_CLK, PIN_ENCODER_DT, PIN_ENCODER_SW, ENC_DIVIDER);
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

bool encoder_button_is_pressed(void)
{
    return s_button_debounced_pressed;
}

void encoder_poll_button(void)
{
    int level = gpio_get_level(PIN_ENCODER_SW);
    rlc_debounce_update(&s_button_db, level, button_change_cb, NULL);

    /* Check for long-press timeout — detect regardless of callback registration */
    if (s_button_debounced_pressed && !s_long_press_fired) {
        int64_t elapsed_us = esp_timer_get_time() - s_press_start_us;
        if (elapsed_us >= (int64_t)ENCODER_LONG_PRESS_MS * 1000) {
            s_long_press_fired = true;
            ESP_LOGI(TAG, "LONG PRESS detected (ch=%d)", s_channel);
            if (s_long_press_cb) {
                s_long_press_cb();
            }
        }
    }
}
