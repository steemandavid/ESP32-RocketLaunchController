#include "hw_rgb_led.h"
#include "pin_config.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "hw_rgb_led";

static led_strip_handle_t s_strip    = NULL;
static uint8_t            s_brightness = 255;

void hw_rgb_led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num      = PIN_RGB_LED,
        .max_leds            = NUM_RGB_LEDS,
        .led_model           = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out    = false,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src        = RMT_CLK_SRC_DEFAULT,
        .resolution_hz  = 10000000, /* 10 MHz */
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    led_off();
    ESP_LOGI(TAG, "RGB LED strip (WS2812, %d pixels) initialised on GPIO %d", NUM_RGB_LEDS, PIN_RGB_LED);
}

/* Helpers — apply brightness scaling */
static uint8_t scale(uint8_t v)
{
    return (uint8_t)((v * s_brightness) / 255);
}

static void set_pixel(int idx, uint8_t r, uint8_t g, uint8_t b)
{
    esp_err_t ret = led_strip_set_pixel(s_strip, idx, scale(r), scale(g), scale(b));
    if (ret != ESP_OK) ESP_LOGE(TAG, "set_pixel %d failed: %s", idx, esp_err_to_name(ret));
}

static void refresh(void)
{
    esp_err_t ret = led_strip_refresh(s_strip);
    if (ret != ESP_OK) ESP_LOGE(TAG, "refresh failed: %s", esp_err_to_name(ret));
}

/* Set pixel 0 only (single-pixel backwards-compatible) */
void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    set_pixel(0, r, g, b);
    refresh();
}

/* Set all pixels to the same colour */
void led_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < NUM_RGB_LEDS; i++) {
        set_pixel(i, r, g, b);
    }
    refresh();
}

void led_off(void)
{
    esp_err_t ret = led_strip_clear(s_strip);
    if (ret != ESP_OK) ESP_LOGE(TAG, "clear failed: %s", esp_err_to_name(ret));
    ret = led_strip_refresh(s_strip);
    if (ret != ESP_OK) ESP_LOGE(TAG, "refresh failed: %s", esp_err_to_name(ret));
}

void led_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;
}

void led_test(void)
{
    /* FSD v1.10 §11.1 — all base unit status patterns, 2s each */

    printf("BOOT: Blue slow pulse (2s cycle)...\r\n");
    for (int cycle = 0; cycle < 2; cycle++) {
        for (int i = 0; i <= 255; i += 5) {
            led_set_all(0, 0, (uint8_t)i);
            vTaskDelay(pdMS_TO_TICKS(4));
        }
        for (int i = 255; i >= 0; i -= 5) {
            led_set_all(0, 0, (uint8_t)i);
            vTaskDelay(pdMS_TO_TICKS(4));
        }
    }

    printf("IDLE (arm OFF): Green solid...\r\n");
    led_set_all(0, 255, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("IDLE (arm ON): Green fast blink (250ms)...\r\n");
    for (int i = 0; i < 4; i++) {
        led_set_all(0, 255, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    printf("ARMED: Red slow blink (500ms)...\r\n");
    for (int i = 0; i < 2; i++) {
        led_set_all(255, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    printf("PRE_FIRE: Red fast blink (100ms)...\r\n");
    for (int i = 0; i < 10; i++) {
        led_set_all(255, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("FIRING: Red solid...\r\n");
    led_set_all(255, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("POST_FIRE: Yellow solid...\r\n");
    led_set_all(255, 180, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("LINK_LOST: Yellow fast blink (200ms)...\r\n");
    for (int i = 0; i < 5; i++) {
        led_set_all(255, 180, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    printf("ERROR: Red triple flash (100on/100off x3, 700off)...\r\n");
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < 3; i++) {
            led_set_all(255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            led_off();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(700));
    }

    led_off();
    printf("LED test complete.\r\n");
}

void led_strip_test(void)
{
    /* Pixel walk — lights each pixel red one at a time */
    printf("Strip test: pixel walk (red)...\r\n");
    printf("  (pixel 0 also drives on-board LED in parallel)\r\n");
    for (int i = 0; i < NUM_RGB_LEDS; i++) {
        led_off();
        set_pixel(i, 255, 0, 0);
        refresh();
        printf("  Pixel %d\r\n", i);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    led_off();

    /* Colour sequence — all pixels R, G, B, white */
    printf("Strip test: all-pixel colours...\r\n");
    uint8_t colours[][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}
    };
    const char *names[] = {"Red", "Green", "Blue", "White"};
    for (int c = 0; c < 4; c++) {
        printf("  %s\r\n", names[c]);
        led_set_all(colours[c][0], colours[c][1], colours[c][2]);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* Chase — single pixel running along the strip */
    printf("Strip test: chase (x3)...\r\n");
    for (int rep = 0; rep < 3; rep++) {
        for (int i = 0; i < NUM_RGB_LEDS; i++) {
            led_off();
            set_pixel(i, 0, 255, 0);
            refresh();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    /* Rainbow — each pixel a different hue */
    printf("Strip test: rainbow...\r\n");
    for (int i = 0; i < NUM_RGB_LEDS; i++) {
        int phase = i * (256 * 3 / NUM_RGB_LEDS);
        uint8_t r, g, b;
        if (phase < 256)      { r = 255 - (uint8_t)phase; g = (uint8_t)phase; b = 0; }
        else if (phase < 512) { r = 0; g = 255 - (uint8_t)(phase - 256); b = (uint8_t)(phase - 256); }
        else                  { r = (uint8_t)(phase - 512); g = 0; b = 255 - (uint8_t)(phase - 512); }
        set_pixel(i, r, g, b);
    }
    refresh();
    vTaskDelay(pdMS_TO_TICKS(3000));

    led_off();
    printf("Strip test complete.\r\n");
}
