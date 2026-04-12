#include "pin_config.h"
#include "hw_encoder.h"
#include "hw_buttons.h"
#include "hw_display.h"
#include "hw_buzzer.h"
#include "hw_battery.h"
#include "hw_rgb_led.h"
#include "hw_leds.h"
#include "cli.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

void app_main(void)
{
    /* ---------------------------------------------------------------
     * Boot safety: drive all outputs to safe state FIRST.
     * Matches FSD §9.7 boot safety requirement.
     * --------------------------------------------------------------- */
    ESP_LOGI(TAG, "GPIO init — driving all outputs to safe state");

    /* Outputs off */
    hw_buzzer_init();   /* buzzer off */
    hw_leds_init();     /* all indicator LEDs off */

    /* Inputs with pull-ups */
    hw_buttons_init();  /* fire button, arm switch, encoder SW */

    /* Encoder (installs GPIO ISR) */
    hw_encoder_init();

    /* ADC init — battery only */
    ESP_LOGI(TAG, "ADC init");
    hw_battery_init();

    /* RGB LED */
    ESP_LOGI(TAG, "RGB LED init");
    hw_rgb_led_init();

    /* CLI */
    ESP_LOGI(TAG, "CLI init");
    cli_init();

    ESP_LOGI(TAG, "Boot complete — starting CLI task");
    xTaskCreate(cli_task, "cli", 8192, NULL, 5, NULL);
}
