/* Host test for the RLC LED strip renderer.
 * Includes the driver source directly so the real static rendering
 * functions are exercised against a mock led_strip backend. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int64_t g_mock_us = 0;
uint32_t g_pix[8];
int g_refreshes;

#include "rlc_rgb_led.c"

static int fails = 0, checks = 0;

/* Pixel index holding channel `ch` (1-8), per the unit's strip orientation. */
static int px(int ch) { return pixel_for_channel(ch - 1); }

static void at_ms(int64_t ms) { g_mock_us = ms * 1000; }

static void expect_pix(const char *what, int idx, uint32_t want)
{
    checks++;
    if (g_pix[idx] != want) {
        printf("  FAIL %-46s pixel[%d] = 0x%06X, want 0x%06X\n",
               what, idx, g_pix[idx], want);
        fails++;
    }
}

static void expect_int(const char *what, int got, int want)
{
    checks++;
    if (got != want) { printf("  FAIL %-46s got %d, want %d\n", what, got, want); fails++; }
}

/* Colour after brightness + dim scaling, matching led_set_pixel(). */
static uint32_t scaled(uint32_t colour, int pct)
{
    uint32_t r = RLC_COLOR_R(colour) * s_brightness / 255;
    uint32_t g = RLC_COLOR_G(colour) * s_brightness / 255;
    uint32_t b = RLC_COLOR_B(colour) * s_brightness / 255;
    if (pct != 100) { r = r*pct/100; g = g*pct/100; b = b*pct/100; }
    return (r<<16)|(g<<8)|b;
}

static void reset(void)
{
    memset(g_pix, 0, sizeof(g_pix));
    /* The driver skips re-sending pixels that already match its shadow of the
     * last transmitted frame. Zeroing the mock's output without clearing that
     * shadow would leave the driver believing the strip is already correct, so
     * it must be reset alongside. Firmware never hits this: nothing clears the
     * strip behind the driver's back. */
    memset((void *)s_shadow, 0, sizeof(s_shadow));
    s_shadow_valid = false;
    s_dirty = false;
    s_led_strip = (void*)1;
    s_pixel_count = NUM_CHANNELS;
    s_brightness = RGB_LED_BRIGHTNESS_BASE;
    s_channel_bands = 0; s_channel_bands_valid = false;
    s_active_channel = 0; s_alarms = 0; s_stale = false; s_key_warning = false;
}

int main(void)
{
    printf("RLC LED strip renderer — host tests\n\n");

    /* ── T-L01: pixel mapping for this unit's strip orientation ── */
#if RLC_STRIP_REVERSED
    printf("T-L01 channel -> pixel mapping (REVERSED: DIN at channel-8 end)\n");
    reset();
    expect_int("channel 1 lives at pixel 7", pixel_for_channel(0), 7);
    expect_int("channel 8 lives at pixel 0", pixel_for_channel(7), 0);
    expect_int("channel 4 lives at pixel 4", pixel_for_channel(3), 4);
#else
    printf("T-L01 channel -> pixel mapping (STRAIGHT: DIN at channel-1 end)\n");
    reset();
    expect_int("channel 1 lives at pixel 0", pixel_for_channel(0), 0);
    expect_int("channel 8 lives at pixel 7", pixel_for_channel(7), 7);
    expect_int("channel 4 lives at pixel 3", pixel_for_channel(3), 3);
#endif

    /* ── T-L02: continuity map colours, per channel ── */
    printf("T-L02 continuity map colours\n");
    reset();
    /* ch1=GOOD(01) ch2=MARGINAL(10) ch3=SHORT(11) ch4=OPEN(00), rest OPEN */
    rlc_rgb_led_set_channel_bands(0x0000 | (CONT_GOOD<<0) | (CONT_MARGINAL<<2) | (CONT_SHORT<<4));
    at_ms(0);
    led_render_status(0);
    expect_pix("ch1 GOOD -> darkgreen",  px(1), scaled(RLC_COLOR_CONT_GOOD, 100));
    expect_pix("ch2 MARGINAL -> lightgreen", px(2), scaled(RLC_COLOR_CONT_MARGINAL, 100));
    expect_pix("ch3 SHORT -> red",       px(3), scaled(RLC_COLOR_CONT_SHORT, 100));
    expect_pix("ch4 OPEN -> yellow",     px(4), scaled(RLC_COLOR_CONT_OPEN, 100));
    expect_pix("ch8 OPEN -> yellow",     px(8), scaled(RLC_COLOR_CONT_OPEN, 100));

    /* ── T-L03: boot chase before any continuity data ── */
    printf("T-L03 boot chase while no continuity data\n");
    reset();
    led_render_status(0);
    expect_pix("chase step 0 lights channel 1", px(1), scaled(RLC_COLOR_STRIP_BOOT, 100));
    expect_pix("chase step 0 leaves channel 2 dark",      px(2), 0);
    led_render_status(RLC_STRIP_CHASE_MS);
    expect_pix("chase step 1 moves to channel 2", px(2), scaled(RLC_COLOR_STRIP_BOOT, 100));
    expect_pix("chase step 1 clears channel 1",             px(1), 0);

    /* ── T-L04: alarm wink over the map ── */
    printf("T-L04 alarm wink timing and colour\n");
    reset();
    rlc_rgb_led_set_channel_bands(0);
    rlc_rgb_led_set_alarms(RLC_ALARM_LINK_LOST);
    led_render_status(0);
    expect_pix("inside wink window -> amber, whole strip", px(8), scaled(RLC_COLOR_ALARM_LINK, 100));
    expect_pix("inside wink window -> amber, whole strip", px(1), scaled(RLC_COLOR_ALARM_LINK, 100));
    led_render_status(RLC_STRIP_ALARM_WINK_MS);
    expect_pix("after wink -> map returns", px(8), scaled(RLC_COLOR_CONT_OPEN, 100));
    led_render_status(RLC_STRIP_ALARM_PERIOD_MS);
    expect_pix("next period -> winks again", px(8), scaled(RLC_COLOR_ALARM_LINK, 100));

    /* ── T-L05: multiple alarms alternate across winks ── */
    printf("T-L05 multiple alarms alternate\n");
    reset();
    rlc_rgb_led_set_channel_bands(0);
    rlc_rgb_led_set_alarms(RLC_ALARM_LINK_LOST | RLC_ALARM_BATTERY);
    led_render_status(0);
    expect_pix("wink 0 -> magenta (battery first)", px(8), scaled(RLC_COLOR_ALARM_BATT, 100));
    led_render_status(RLC_STRIP_ALARM_PERIOD_MS);
    expect_pix("wink 1 -> amber (link)",            px(8), scaled(RLC_COLOR_ALARM_LINK, 100));
    led_render_status(2 * RLC_STRIP_ALARM_PERIOD_MS);
    expect_pix("wink 2 -> back to magenta",         px(8), scaled(RLC_COLOR_ALARM_BATT, 100));

    /* ── T-L06: stale data dims the whole map ── */
    printf("T-L06 stale map dimming\n");
    reset();
    rlc_rgb_led_set_channel_bands((CONT_GOOD<<0));
    rlc_rgb_led_set_stale(true);
    led_render_status(0);
    expect_pix("stale ch1 dimmed to STALE_DIM_PCT", px(1),
               scaled(RLC_COLOR_CONT_GOOD, RLC_STRIP_STALE_DIM_PCT));
    rlc_rgb_led_set_stale(false);
    led_render_status(0);
    expect_pix("fresh ch1 at full brightness", px(1), scaled(RLC_COLOR_CONT_GOOD, 100));

    /* ── T-L07: cursor pulse on the selected channel only ── */
    printf("T-L07 channel-of-interest cursor pulse\n");
    reset();
    rlc_rgb_led_set_channel_bands(0);
    rlc_rgb_led_set_active_channel(3);
    led_render_status(0);
    expect_pix("cursor peak = full",     px(3), scaled(RLC_COLOR_CONT_OPEN, 100));
    led_render_status(RLC_STRIP_CURSOR_MS);
    expect_pix("cursor trough dimmed",   px(3), scaled(RLC_COLOR_CONT_OPEN, RLC_STRIP_CURSOR_LOW_PCT));
    expect_pix("other channels steady",  px(4), scaled(RLC_COLOR_CONT_OPEN, 100));

    /* ── T-L08: key warning — base (no cursor) vs remote (cursor) ── */
    printf("T-L08 key-ON breathing\n");
    reset();
    rlc_rgb_led_set_channel_bands(0);
    rlc_rgb_led_set_key_warning(true);
    led_render_status(RLC_STRIP_BREATHE_MS);   /* trough */
    expect_pix("base: whole map breathes (ch1)", px(1),
               scaled(RLC_COLOR_CONT_OPEN, RLC_STRIP_BREATHE_LOW_PCT));
    expect_pix("base: whole map breathes (ch8)", px(8),
               scaled(RLC_COLOR_CONT_OPEN, RLC_STRIP_BREATHE_LOW_PCT));
    rlc_rgb_led_set_active_channel(2);
    led_render_status(RLC_STRIP_BREATHE_MS);   /* trough */
    expect_pix("remote: only cursor breathes",   px(2),
               scaled(RLC_COLOR_CONT_OPEN, RLC_STRIP_BREATHE_LOW_PCT));
    expect_pix("remote: others stay steady",     px(1), scaled(RLC_COLOR_CONT_OPEN, 100));

    /* ── T-L09: stale + cursor compose (dim applies under the pulse) ── */
    printf("T-L09 stale and cursor compose\n");
    reset();
    rlc_rgb_led_set_channel_bands(0);
    rlc_rgb_led_set_active_channel(1);
    rlc_rgb_led_set_stale(true);
    led_render_status(0);
    expect_pix("stale cursor peak = stale level", px(1),
               scaled(RLC_COLOR_CONT_OPEN, RLC_STRIP_STALE_DIM_PCT));
    expect_pix("stale non-cursor = stale level",  px(2),
               scaled(RLC_COLOR_CONT_OPEN, RLC_STRIP_STALE_DIM_PCT));

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
