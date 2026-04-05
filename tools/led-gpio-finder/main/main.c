/* LED GPIO Finder — tests WS2812 on candidate GPIO pins one at a time.
 * Outputs serial debug on UART0 (115200 baud) so you can see which pin
 * is being driven when the LED lights up.
 *
 * Candidate pins: 38 (DevKitC-1 v1.1), 47 (FSD), 48 (DevKitC-1 v1.0)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "led_strip.h"
#include "driver/gpio.h"

static const char *TAG = "led-finder";

/* Pins to test, in order */
static const int PINS[] = { 38, 47, 48 };
#define NUM_PINS 3

/* Seconds to hold each colour */
#define HOLD_SEC 3

static led_strip_handle_t make_strip(int gpio)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num      = gpio,
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

    led_strip_handle_t strip = NULL;
    esp_err_t ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "  led_strip_new_rmt_device on GPIO %d FAILED: %s", gpio, esp_err_to_name(ret));
        return NULL;
    }
    return strip;
}

static void show_colour(led_strip_handle_t strip, const char *name, uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(strip, 0, r, g, b);
    led_strip_refresh(strip);
    printf("    %s  (R=%d G=%d B=%d)\r\n", name, r, g, b);
    vTaskDelay(pdMS_TO_TICKS(HOLD_SEC * 1000));
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== WS2812 GPIO Finder ===");
    ESP_LOGI(TAG, "Testing pins: 38, 47, 48");
    ESP_LOGI(TAG, "Each pin: RED %ds -> GREEN %ds -> BLUE %ds -> WHITE %ds -> OFF",
             HOLD_SEC, HOLD_SEC, HOLD_SEC, HOLD_SEC);
    ESP_LOGI(TAG, "Watch the LED and note which GPIO lights it up.\n");

    for (int i = 0; i < NUM_PINS; i++) {
        int gpio = PINS[i];

        printf("\r\n========================================\r\n");
        printf("  TESTING GPIO %d  (%d of %d)\r\n", gpio, i + 1, NUM_PINS);
        printf("========================================\r\n");
        ESP_LOGI(TAG, "GPIO %d — creating LED strip...", gpio);

        led_strip_handle_t strip = make_strip(gpio);
        if (!strip) {
            ESP_LOGE(TAG, "GPIO %d — SKIPPED (init failed)", gpio);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "GPIO %d — strip created OK, driving LED...", gpio);

        /* Cycle through distinctive colours so there's no ambiguity */
        show_colour(strip, "RED",   255, 0,   0);
        show_colour(strip, "GREEN", 0,   255, 0);
        show_colour(strip, "BLUE",  0,   0,   255);
        show_colour(strip, "WHITE", 255, 255, 255);

        led_strip_clear(strip);
        led_strip_refresh(strip);
        ESP_LOGI(TAG, "GPIO %d — OFF, cleaning up.", gpio);
        led_strip_del(strip);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "\n=== Test complete ===");
    ESP_LOGI(TAG, "Whichever GPIO number was printed when the LED lit up is your pin.");
    ESP_LOGI(TAG, "Update PIN_RGB_LED in pin_config.h accordingly.");

    /* Blink the correct pin's LED forever so the last state isn't ambiguous */
    int correct_gpio = -1;
    printf("\r\nEnter the working GPIO number via serial (e.g. 38) then press Enter: ");
    fflush(stdout);

    /* Simple blocking read from stdin */
    char buf[8] = {0};
    int pos = 0;
    while (pos < (int)sizeof(buf) - 1) {
        int ch = fgetc(stdin);
        if (ch == '\r' || ch == '\n') {
            buf[pos] = '\0';
            break;
        } else if (ch >= '0' && ch <= '9') {
            buf[pos++] = (char)ch;
            fputc(ch, stdout);
            fflush(stdout);
        }
    }
    printf("\r\n");
    correct_gpio = atoi(buf);
    ESP_LOGI(TAG, "You entered GPIO %d — blinking it forever.", correct_gpio);

    led_strip_handle_t strip = make_strip(correct_gpio);
    if (strip) {
        while (1) {
            led_strip_set_pixel(strip, 0, 0, 255, 0);
            led_strip_refresh(strip);
            vTaskDelay(pdMS_TO_TICKS(500));
            led_strip_clear(strip);
            led_strip_refresh(strip);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    ESP_LOGE(TAG, "Could not create strip on GPIO %d — halting.", correct_gpio);
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
