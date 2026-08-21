/**
 * RLC RGB LED Status Driver
 *
 * WS2812 on GPIO 48 via RMT, driven by a FreeRTOS task.
 * See rlc_rgb_led.h for the rendering layer model.
 */

#include "rlc_rgb_led.h"
#include "rlc_config.h"
#include "pin_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rlc_led";

static led_strip_handle_t s_led_strip = NULL;
static rlc_led_pattern_t s_current_pattern = LED_PATTERN_OFF;
static SemaphoreHandle_t s_pattern_mutex = NULL;
static TaskHandle_t s_led_task = NULL;
static int s_pixel_count = 1;  /* Default: single pixel until configured */
static uint8_t s_brightness = RGB_LED_BRIGHTNESS_BASE;

/* Shadow of what was last transmitted, so identical frames are not re-sent.
 * The renderer runs at RLC_STRIP_FRAME_MS but most frames are unchanged — a
 * steady map sends nothing, and a pulsing cursor sends twice a second instead
 * of twenty times. That cuts WS2812 data-line activity and its current pulses
 * sharply, which matters because the strip shares a ground with the encoder
 * inputs on the remote (see the 2026-08-20 encoder sensitivity work). */
static uint32_t s_shadow[NUM_CHANNELS];
static bool     s_shadow_valid = false;
static bool     s_dirty = false;

/* Status feeds — published by the units' housekeeping loops. Torn reads are
 * harmless here: the worst case is one frame of stale colour. */
static volatile uint16_t s_channel_bands = 0;
static volatile bool     s_channel_bands_valid = false;
static volatile uint8_t  s_active_channel = 0;
static volatile uint32_t s_alarms = 0;
static volatile bool     s_stale = false;
static volatile bool     s_key_warning = false;

/* ── Pixel helpers ────────────────────────────────────────────── */

/* Channel index 0..7 → pixel index on the strip. The two units are wired
 * data-in at opposite ends (RLC_STRIP_REVERSED, set per unit in rlc_config.h),
 * so pixel 0 — which the parallel on-board LED mirrors — is channel 1 on the
 * base and channel 8 on the remote. */
static inline int pixel_for_channel(int ch_idx)
{
#if RLC_STRIP_REVERSED
    return s_pixel_count - 1 - ch_idx;
#else
    return ch_idx;
#endif
}

/* Write one pixel with brightness and an optional dim, in percent. */
static void led_set_pixel(int idx, uint32_t colour, int scale_pct)
{
    if (!s_led_strip || idx < 0 || idx >= s_pixel_count) return;
    uint32_t r = RLC_COLOR_R(colour) * s_brightness / 255;
    uint32_t g = RLC_COLOR_G(colour) * s_brightness / 255;
    uint32_t b = RLC_COLOR_B(colour) * s_brightness / 255;
    if (scale_pct != 100) {
        r = r * scale_pct / 100;
        g = g * scale_pct / 100;
        b = b * scale_pct / 100;
    }
    uint32_t packed = (r << 16) | (g << 8) | b;
    if (s_shadow_valid && s_shadow[idx] == packed) return;   /* unchanged */
    s_shadow[idx] = packed;
    s_dirty = true;
    led_strip_set_pixel(s_led_strip, idx, r, g, b);
}

/* Transmit only if something actually changed since the last frame. */
static void led_commit(void)
{
    if (!s_led_strip) return;
    if (!s_shadow_valid) { s_shadow_valid = true; s_dirty = true; }
    if (!s_dirty) return;
    led_strip_refresh(s_led_strip);
    s_dirty = false;
}

/* Paint every pixel one colour (whole-strip patterns). */
static void led_fill(uint32_t colour, int scale_pct)
{
    if (!s_led_strip) return;
    for (int i = 0; i < s_pixel_count; i++) {
        led_set_pixel(i, colour, scale_pct);
    }
    led_commit();
}

static void led_off(void)
{
    if (!s_led_strip) return;
    bool was_lit = !s_shadow_valid;
    for (int i = 0; i < s_pixel_count; i++) {
        if (!s_shadow_valid || s_shadow[i] != 0) { s_shadow[i] = 0; was_lit = true; }
    }
    s_shadow_valid = true;
    if (!was_lit) return;                 /* already dark — nothing to send */
    led_strip_clear(s_led_strip);
    led_strip_refresh(s_led_strip);
    s_dirty = false;
}

static uint32_t band_colour(uint8_t band)
{
    switch (band) {
        case CONT_CONNECTED:     return RLC_COLOR_CONT_CONNECTED;
        case CONT_MARGINAL: return RLC_COLOR_CONT_MARGINAL;
        case CONT_SHORT:    return RLC_COLOR_CONT_SHORT;
        default:            return RLC_COLOR_CONT_OPEN;
    }
}

/* ── Layer 6: the channel map ─────────────────────────────────── */

/* One pixel per igniter channel. `base_pct` dims the whole map (stale data,
 * ERROR gap); `active_pct` overrides it for the channel of interest. */
static void led_show_channel_map(int base_pct, int active_pct)
{
    if (!s_led_strip) return;
    uint16_t bands = s_channel_bands;
    uint8_t  active = s_active_channel;

    for (int i = 0; i < s_pixel_count; i++) {
        uint8_t band = (uint8_t)((bands >> (2 * i)) & 0x3);
        int pct = (active && (i == active - 1)) ? active_pct : base_pct;
        led_set_pixel(pixel_for_channel(i), band_colour(band), pct);
    }
    led_commit();
}

/* Shown until the first continuity data arrives: a cyan chase in channel
 * order, clearly "not ready" and clearly not a channel state. */
static void led_show_boot_chase(int64_t now_ms)
{
    if (!s_led_strip) return;
    int lit = (int)((now_ms / RLC_STRIP_CHASE_MS) % s_pixel_count);
    for (int i = 0; i < s_pixel_count; i++) {
        led_set_pixel(pixel_for_channel(i),
                      (i == lit) ? RLC_COLOR_STRIP_BOOT : 0x000000, 100);
    }
    led_commit();
}

/* ── Layer 3: the alarm wink ──────────────────────────────────── */

/* Colour for the Nth wink. Several alarms may be raised at once; successive
 * winks cycle through them so every active alarm is eventually seen. */
static uint32_t alarm_wink_colour(uint32_t alarms, int64_t wink_index)
{
    uint32_t active[3];
    int n = 0;
    if (alarms & RLC_ALARM_BATTERY)   active[n++] = RLC_COLOR_ALARM_BATT;
    if (alarms & RLC_ALARM_ARM_FAULT) active[n++] = RLC_COLOR_ALARM_FAULT;
    if (alarms & RLC_ALARM_LINK_LOST) active[n++] = RLC_COLOR_ALARM_LINK;
    if (n == 0) return 0x000000;
    return active[wink_index % n];
}

/* ── Status rendering: layers 3-6 ─────────────────────────────── */

static void led_render_status(int64_t now_ms)
{
    /* Layer 3 — alarm wink over everything below it. */
    uint32_t alarms = s_alarms;
    if (alarms) {
        int64_t phase = now_ms % RLC_STRIP_ALARM_PERIOD_MS;
        if (phase < RLC_STRIP_ALARM_WINK_MS) {
            int64_t idx = now_ms / RLC_STRIP_ALARM_PERIOD_MS;
            led_fill(alarm_wink_colour(alarms, idx), 100);
            return;
        }
    }

    /* No continuity data yet — nothing meaningful to map. */
    if (!s_channel_bands_valid) {
        led_show_boot_chase(now_ms);
        return;
    }

    /* Layer 4 — stale cached data (remote): last known, not live. */
    int base_pct = s_stale ? RLC_STRIP_STALE_DIM_PCT : 100;
    int active_pct = base_pct;

    if (s_key_warning) {
        /* Layer 5 — key-ON / arm-ready breathing. The base has no cursor
         * (it never learns the remote's selection), so its whole map
         * breathes; the remote knows its cursor and breathes only that. */
        bool trough = ((now_ms / RLC_STRIP_BREATHE_MS) % 2) != 0;
        int low = base_pct * RLC_STRIP_BREATHE_LOW_PCT / 100;
        if (s_active_channel) {
            active_pct = trough ? low : base_pct;
        } else {
            base_pct = active_pct = trough ? low : base_pct;
        }
    } else if (s_active_channel) {
        /* Layer 6 — highlight the channel of interest with a gentle pulse. */
        bool trough = ((now_ms / RLC_STRIP_CURSOR_MS) % 2) != 0;
        active_pct = trough ? (base_pct * RLC_STRIP_CURSOR_LOW_PCT / 100)
                            : base_pct;
    }

    led_show_channel_map(base_pct, active_pct);
}

/* ── Pattern engine ───────────────────────────────────────────── */

static void led_task(void *arg)
{
    (void)arg;

    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000;

        rlc_led_pattern_t pat;
        xSemaphoreTake(s_pattern_mutex, portMAX_DELAY);
        pat = s_current_pattern;
        xSemaphoreGive(s_pattern_mutex);

        switch (pat) {
            case LED_PATTERN_STATUS:
                led_render_status(now_ms);
                break;

            case LED_PATTERN_ARMED:
                /* Red slow blink 500ms on/off */
                if ((now_ms / 500) % 2 == 0) led_fill(0xFF0000, 100);
                else led_off();
                break;

            case LED_PATTERN_PRE_FIRE:
                /* Red fast blink 100ms on/off */
                if ((now_ms / 100) % 2 == 0) led_fill(0xFF0000, 100);
                else led_off();
                break;

            case LED_PATTERN_FIRING:
                led_fill(0xFF0000, 100);
                break;

            case LED_PATTERN_ERROR: {
                /* Red triple flash: 100on/100off/100on/100off/100on/700off.
                 * The long gap keeps igniter status visible (dimmed) so the
                 * operator can still read the pad state during a fault. */
                int64_t phase = now_ms % 1200;
                if (phase < 500) {
                    if ((phase / 100) % 2 == 0) led_fill(0xFF0000, 100);
                    else led_off();
                } else if (s_channel_bands_valid) {
                    led_show_channel_map(RLC_STRIP_ERROR_DIM_PCT,
                                         RLC_STRIP_ERROR_DIM_PCT);
                } else {
                    led_off();
                }
                break;
            }

            case LED_PATTERN_OFF:
            default:
                led_off();
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(RLC_STRIP_FRAME_MS));
    }
}

int rlc_rgb_led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = PIN_RGB_LED,
        .max_leds         = NUM_CHANNELS,  /* 8-pixel strip on both units */
        .led_model        = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src    = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,  /* 10 MHz */
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED strip init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    led_strip_clear(s_led_strip);
    led_strip_refresh(s_led_strip);

    s_pattern_mutex = xSemaphoreCreateMutex();

    /* FSD §9.10: rgb_led_task runs at priority 1 (lowest) */
    xTaskCreate(led_task, "led_task", 2048, NULL, 1, &s_led_task);

    ESP_LOGI(TAG, "RGB LED initialised on GPIO %d (%d pixels)", PIN_RGB_LED, s_pixel_count);
    return 0;
}

void rlc_rgb_led_set_pixel_count(int count)
{
    if (count < 1) count = 1;
    if (count > NUM_CHANNELS) count = NUM_CHANNELS;
    s_pixel_count = count;
}

void rlc_rgb_led_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;
}

void rlc_rgb_led_set_pattern(rlc_led_pattern_t pattern)
{
    /* Safe before rlc_rgb_led_init(): the boot self-test failure path signals
     * ERROR and halts, and on 2026-08-21 that path ran before the driver was
     * initialised — xSemaphoreTake(NULL) asserted and the unit reboot-looped
     * instead of halting with an error indication. A fail-safe halt that
     * cannot itself fail matters more than the mutex here. */
    if (!s_pattern_mutex) {
        s_current_pattern = pattern;
        return;
    }
    xSemaphoreTake(s_pattern_mutex, portMAX_DELAY);
    s_current_pattern = pattern;
    xSemaphoreGive(s_pattern_mutex);
}

void rlc_rgb_led_set_channel_bands(uint16_t bands)
{
    s_channel_bands = bands;
    s_channel_bands_valid = true;
}

void rlc_rgb_led_set_active_channel(uint8_t channel)
{
    s_active_channel = (channel <= NUM_CHANNELS) ? channel : 0;
}

void rlc_rgb_led_set_alarms(uint32_t alarms)
{
    s_alarms = alarms;
}

void rlc_rgb_led_set_stale(bool stale)
{
    s_stale = stale;
}

void rlc_rgb_led_set_key_warning(bool warn)
{
    s_key_warning = warn;
}
