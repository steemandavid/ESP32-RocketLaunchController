#include "pin_config.h"
#include "hw_relay.h"
#include "hw_continuity.h"
#include "hw_battery.h"
#include "hw_inputs.h"
#include "hw_siren.h"
#include "hw_rgb_led.h"
#include "hw_fire_timer.h"
#include "cli.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

void app_main(void)
{
    /* ---------------------------------------------------------------
     * Boot safety: drive all relay outputs inactive FIRST.
     * Matches FSD §9.7 boot safety requirement.
     * --------------------------------------------------------------- */
    ESP_LOGI(TAG, "GPIO init — driving all relay outputs inactive");
    hw_relay_init();   /* relay GPIOs + low-side, all inactive */

    /* Siren inactive */
    hw_siren_init();

    /* Continuity MOSFET disabled */
    ESP_LOGI(TAG, "ADC init");
    hw_battery_init();    /* creates shared ADC1 unit + calibration */
    hw_continuity_init(); /* configures continuity channels on shared ADC1 */

    /* Digital inputs */
    hw_inputs_init();

    /* RGB LED */
    ESP_LOGI(TAG, "RGB LED init");
    hw_rgb_led_init();

    /* Fire timer */
    hw_fire_timer_init();

    /* UART CLI */
    ESP_LOGI(TAG, "CLI init");
    cli_init();

    ESP_LOGI(TAG, "Boot complete — starting CLI task");
    xTaskCreate(cli_task, "cli", 8192, NULL, 5, NULL);
}
