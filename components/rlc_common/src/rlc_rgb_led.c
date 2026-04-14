/**
 * RLC RGB LED Status Driver
 *
 * WS2812 on GPIO 48 via RMT, driven by a FreeRTOS task.
 * Supports single pixel (remote) or 8-pixel strip (base unit).
 */

#include "rlc_rgb_led.h"
#include "rlc_config.h"
#include "pin_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "rlc_led";

static led_strip_handle_t s_led_strip = NULL;
static rlc_led_pattern_t s_current_pattern = LED_PATTERN_OFF;
static SemaphoreHandle_t s_pattern_mutex = NULL;
static TaskHandle_t s_led_task = NULL;
static int s_pixel_count = 1;  /* Default: single pixel (remote) */

/* Overlay flash state — protected by s_overlay_mutex */
static SemaphoreHandle_t s_overlay_mutex = NULL;
static bool     s_overlay_active = false;
static uint8_t  s_overlay_r, s_overlay_g, s_overlay_b;
static uint16_t s_overlay_duration_ms;

static void led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_led_strip) return;
    /* Apply brightness scaling */
    r = (uint8_t)((r * RGB_LED_BRIGHTNESS) / 255);
    g = (uint8_t)((g * RGB_LED_BRIGHTNESS) / 255);
    b = (uint8_t)((b * RGB_LED_BRIGHTNESS) / 255);
    for (int i = 0; i < s_pixel_count; i++) {
        led_strip_set_pixel(s_led_strip, i, r, g, b);
    }
    led_strip_refresh(s_led_strip);
}

static void led_off(void)
{
    if (!s_led_strip) return;
    led_strip_clear(s_led_strip);
    led_strip_refresh(s_led_strip);
}

static void led_task(void *arg)
{
    (void)arg;
    int step = 0;

    while (1) {
        /* Handle overlay flash — copy under mutex */
        bool do_overlay = false;
        uint8_t ov_r, ov_g, ov_b;
        uint16_t ov_ms;

        if (xSemaphoreTake(s_overlay_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (s_overlay_active) {
                do_overlay = true;
                ov_r = s_overlay_r;
                ov_g = s_overlay_g;
                ov_b = s_overlay_b;
                ov_ms = s_overlay_duration_ms;
                s_overlay_active = false;
            }
            xSemaphoreGive(s_overlay_mutex);
        }

        if (do_overlay) {
            led_set_rgb(ov_r, ov_g, ov_b);
            vTaskDelay(pdMS_TO_TICKS(ov_ms));
            step = 0;
            continue;
        }

        rlc_led_pattern_t pat;
        xSemaphoreTake(s_pattern_mutex, portMAX_DELAY);
        pat = s_current_pattern;
        xSemaphoreGive(s_pattern_mutex);

        switch (pat) {
            case LED_PATTERN_OFF:
                led_off();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case LED_PATTERN_BOOT: {
                /* Blue slow pulse (2s cycle) */
                int phase = step % 20;  /* 20 steps × 100ms = 2s */
                int brightness = (phase < 10) ? (phase * 25) : ((20 - phase) * 25);
                led_set_rgb(0, 0, (uint8_t)((brightness * 255) / 250));
                vTaskDelay(pdMS_TO_TICKS(100));
                step++;
                break;
            }

            case LED_PATTERN_IDLE:
                led_set_rgb(0, 255, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                break;

            case LED_PATTERN_IDLE_ARM_ON:
                /* Green fast blink 250ms on/off */
                if (step % 2 == 0) led_set_rgb(0, 255, 0);
                else led_off();
                vTaskDelay(pdMS_TO_TICKS(250));
                step++;
                break;

            case LED_PATTERN_ARMED:
                /* Red slow blink 500ms on/off */
                if (step % 2 == 0) led_set_rgb(255, 0, 0);
                else led_off();
                vTaskDelay(pdMS_TO_TICKS(500));
                step++;
                break;

            case LED_PATTERN_PRE_FIRE:
                /* Red fast blink 100ms on/off */
                if (step % 2 == 0) led_set_rgb(255, 0, 0);
                else led_off();
                vTaskDelay(pdMS_TO_TICKS(100));
                step++;
                break;

            case LED_PATTERN_FIRING:
                led_set_rgb(255, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                break;

            case LED_PATTERN_POST_FIRE:
                led_set_rgb(255, 180, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                break;

            case LED_PATTERN_LINK_LOST:
                /* Yellow fast blink 200ms on/off */
                if (step % 2 == 0) led_set_rgb(255, 180, 0);
                else led_off();
                vTaskDelay(pdMS_TO_TICKS(200));
                step++;
                break;

            case LED_PATTERN_ERROR:
                /* Red triple flash: 100on/100off/100on/100off/100on/700off */
                switch (step % 6) {
                    case 0: led_set_rgb(255, 0, 0); vTaskDelay(pdMS_TO_TICKS(100)); break;
                    case 1: led_off(); vTaskDelay(pdMS_TO_TICKS(100)); break;
                    case 2: led_set_rgb(255, 0, 0); vTaskDelay(pdMS_TO_TICKS(100)); break;
                    case 3: led_off(); vTaskDelay(pdMS_TO_TICKS(100)); break;
                    case 4: led_set_rgb(255, 0, 0); vTaskDelay(pdMS_TO_TICKS(100)); break;
                    case 5: led_off(); vTaskDelay(pdMS_TO_TICKS(700)); break;
                }
                step++;
                break;

            default:
                led_off();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
}

int rlc_rgb_led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = PIN_RGB_LED,
        .max_leds         = 8,  /* Allocate for max (base unit 8-pixel strip) */
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
    s_overlay_mutex = xSemaphoreCreateMutex();

    /* FSD §9.10: rgb_led_task runs at priority 1 (lowest) */
    xTaskCreate(led_task, "led_task", 2048, NULL, 1, &s_led_task);

    ESP_LOGI(TAG, "RGB LED initialised on GPIO %d (%d pixels)", PIN_RGB_LED, s_pixel_count);
    return 0;
}

void rlc_rgb_led_set_pixel_count(int count)
{
    if (count < 1) count = 1;
    if (count > 8) count = 8;
    s_pixel_count = count;
}

void rlc_rgb_led_set_pattern(rlc_led_pattern_t pattern)
{
    xSemaphoreTake(s_pattern_mutex, portMAX_DELAY);
    s_current_pattern = pattern;
    xSemaphoreGive(s_pattern_mutex);
}

void rlc_rgb_led_flash_overlay(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms)
{
    if (xSemaphoreTake(s_overlay_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_overlay_r = r;
        s_overlay_g = g;
        s_overlay_b = b;
        s_overlay_duration_ms = duration_ms;
        s_overlay_active = true;
        xSemaphoreGive(s_overlay_mutex);
    }
}

void rlc_rgb_led_set_state(rlc_state_t state)
{
    switch (state) {
        case STATE_BOOT:
        case STATE_LINKING:
            rlc_rgb_led_set_pattern(LED_PATTERN_BOOT);
            break;
        case STATE_IDLE:
            rlc_rgb_led_set_pattern(LED_PATTERN_IDLE);
            break;
        case STATE_ARMED:
            rlc_rgb_led_set_pattern(LED_PATTERN_ARMED);
            break;
        case STATE_PRE_FIRE:
            rlc_rgb_led_set_pattern(LED_PATTERN_PRE_FIRE);
            break;
        case STATE_FIRING:
            rlc_rgb_led_set_pattern(LED_PATTERN_FIRING);
            break;
        case STATE_POST_FIRE:
            rlc_rgb_led_set_pattern(LED_PATTERN_POST_FIRE);
            break;
        case STATE_LINK_LOST:
            rlc_rgb_led_set_pattern(LED_PATTERN_LINK_LOST);
            break;
        case STATE_ERROR:
            rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
            break;
        default:
            rlc_rgb_led_set_pattern(LED_PATTERN_OFF);
            break;
    }
}
