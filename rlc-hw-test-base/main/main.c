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
     * Boot safety: drive all SPDT relay outputs inactive FIRST.
     * Matches FSD §9.7 boot safety requirement.
     * --------------------------------------------------------------- */
    ESP_LOGI(TAG, "GPIO init — driving all SPDT relay outputs inactive (NC)");
    hw_relay_init();   /* all 8 channel SPDT relays de-energised */

    /* Siren inactive */
    hw_siren_init();

    /* ADC init — battery creates shared ADC1 unit, continuity shares it */
    ESP_LOGI(TAG, "ADC init");
    hw_battery_init();
    hw_continuity_init();

    /* Digital inputs — arm switch sense (GPIO 21) */
    hw_inputs_init();

    /* Arm relay output — GPIO 47 (IRLZ44N MOSFET) */
    arm_sim_init();

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
