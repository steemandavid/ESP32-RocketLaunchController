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
        .max_leds            = 1,
        .led_model           = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out    = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src        = RMT_CLK_SRC_DEFAULT,
        .resolution_hz  = 10000000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    led_off();
    ESP_LOGI(TAG, "RGB LED (WS2812) initialised on GPIO %d", PIN_RGB_LED);
}

void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    r = (uint8_t)((r * s_brightness) / 255);
    g = (uint8_t)((g * s_brightness) / 255);
    b = (uint8_t)((b * s_brightness) / 255);
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

void led_off(void)
{
    led_strip_clear(s_strip);
    led_strip_refresh(s_strip);
}

void led_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;
}

void led_test(void)
{
    /* Remote unit status patterns (FSD §11.2), 2s each */

    printf("BOOT/LINKING: Blue slow pulse (2s cycle)...\r\n");
    for (int cycle = 0; cycle < 2; cycle++) {
        for (int i = 0; i <= 255; i += 5) {
            led_set(0, 0, (uint8_t)i);
            vTaskDelay(pdMS_TO_TICKS(4));
        }
        for (int i = 255; i >= 0; i -= 5) {
            led_set(0, 0, (uint8_t)i);
            vTaskDelay(pdMS_TO_TICKS(4));
        }
    }

    printf("IDLE: Green solid...\r\n");
    led_set(0, 255, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("ARMED: Red slow blink (500ms)...\r\n");
    for (int i = 0; i < 2; i++) {
        led_set(255, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    printf("PRE_FIRE: Red fast blink (100ms)...\r\n");
    for (int i = 0; i < 10; i++) {
        led_set(255, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("FIRING: Red solid...\r\n");
    led_set(255, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("LINK_LOST: Yellow fast blink (200ms)...\r\n");
    for (int i = 0; i < 5; i++) {
        led_set(255, 180, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    printf("ERROR: Red triple flash (100on/100off x3, 700off)...\r\n");
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < 3; i++) {
            led_set(255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            led_off();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(700));
    }

    printf("PING FAIL: Orange single flash (50ms)...\r\n");
    led_set(255, 100, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    led_off();

    led_off();
    printf("LED test complete.\r\n");
}
