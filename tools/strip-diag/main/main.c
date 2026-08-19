/**
 * RLC WS2812 strip diagnostic.
 *
 * Standalone bring-up firmware for characterising a misbehaving NeoPixel
 * strip on GPIO 48, independent of the RLC firmware and its FSMs.
 *
 * Symptom this was written for (base unit, 2026-08-19): the first four
 * pixels in the data chain render wrong (dark / blue) while the last three
 * render correctly, using a driver configuration that works on the remote.
 * Both strips are the same type from the same batch and the remote's renders
 * correctly, so chipset timing is ruled out — this characterises the base's
 * data line and supply instead.
 *
 * Each phase paints a KNOWN STATIC frame and holds it, announcing itself on
 * the console (USB-Serial/JTAG — the native USB port). Watch the strip and
 * note which phases render all eight pixels correctly.
 *
 * Build/flash:  cd tools/strip-diag && idf.py -p <by-id> flash monitor
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "strip_diag";

#define STRIP_GPIO   48
#define NUM_PIX      8
#define HOLD_MS      6000

static led_strip_handle_t s_strip = NULL;

static void strip_open(led_model_t model, uint32_t res_hz)
{
    if (s_strip) { led_strip_del(s_strip); s_strip = NULL; }

    led_strip_config_t cfg = {
        .strip_gpio_num = STRIP_GPIO,
        .max_leds       = NUM_PIX,
        .led_model      = model,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt = {
        .clk_src       = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = res_hz,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&cfg, &rmt, &s_strip));
    led_strip_clear(s_strip);
    led_strip_refresh(s_strip);
}

/* Paint all pixels one colour and hold, repainting continuously so a
 * one-off corrupt frame cannot be mistaken for a steady result. */
static void hold_solid(const char *what, uint8_t r, uint8_t g, uint8_t b)
{
    ESP_LOGI(TAG, "%s", what);
    for (int t = 0; t < HOLD_MS / 100; t++) {
        for (int i = 0; i < NUM_PIX; i++) led_strip_set_pixel(s_strip, i, r, g, b);
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* Walk one lit pixel along the chain, announcing each index. This identifies
 * which PHYSICAL led is pixel 0 (the DIN end) and proves the reversed
 * channel mapping independently of the RLC renderer. */
static void walk(const char *what)
{
    ESP_LOGI(TAG, "%s", what);
    for (int i = 0; i < NUM_PIX; i++) {
        ESP_LOGI(TAG, "   pixel %d lit (white) — this is channel %d", i, 8 - i);
        for (int t = 0; t < 15; t++) {
            for (int j = 0; j < NUM_PIX; j++)
                led_strip_set_pixel(s_strip, j, (j == i) ? 60 : 0,
                                                (j == i) ? 60 : 0,
                                                (j == i) ? 60 : 0);
            led_strip_refresh(s_strip);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== WS2812 strip diagnostic, GPIO %d, %d pixels ===", STRIP_GPIO, NUM_PIX);
    ESP_LOGI(TAG, "Watch the strip. Note which phases light ALL EIGHT pixels correctly.");

    while (1) {
        /* ── Group 1: WS2812 at the standard 10 MHz (what RLC ships) ── */
        strip_open(LED_MODEL_WS2812, 10 * 1000 * 1000);
        hold_solid("PHASE 1  WS2812 10MHz  — all RED       (expect 8x red)",   60,  0,  0);
        hold_solid("PHASE 2  WS2812 10MHz  — all GREEN     (expect 8x green)",  0, 60,  0);
        hold_solid("PHASE 3  WS2812 10MHz  — all BLUE      (expect 8x blue)",   0,  0, 60);
        hold_solid("PHASE 4  WS2812 10MHz  — all YELLOW    (expect 8x yellow)",60, 60,  0);
        hold_solid("PHASE 5  WS2812 10MHz  — all WHITE dim (expect 8x white)", 20, 20, 20);

        /* ── Group 2: which physical LED is pixel 0? ── */
        walk      ("PHASE 6  WS2812 10MHz  — single-pixel walk from the DIN end");

        /* ── Group 3: does edge sharpness or supply current change anything?
         * Higher RMT resolution sharpens edges; full brightness raises strip
         * current and so exposes droop on a weak 5 V feed. ── */
        strip_open(LED_MODEL_WS2812, 20 * 1000 * 1000);
        hold_solid("PHASE 7  WS2812 20MHz  — all RED       (expect 8x red)",   60,  0,  0);
        hold_solid("PHASE 8  WS2812 20MHz  — all YELLOW    (expect 8x yellow)",60, 60,  0);

        strip_open(LED_MODEL_WS2812, 10 * 1000 * 1000);
        hold_solid("PHASE 9  WS2812 10MHz  — all RED, FULL brightness",       255, 0,  0);
        hold_solid("PHASE 10 WS2812 10MHz  — all WHITE, FULL brightness",     255,255,255);

        ESP_LOGI(TAG, "--- sequence complete, repeating ---");
    }
}
