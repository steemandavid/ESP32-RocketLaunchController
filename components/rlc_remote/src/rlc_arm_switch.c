/**
 * RLC Arm Switch Monitor
 *
 * Monitors the arm/disarm toggle switch on the remote unit (FSD §5.5.2, §8.3.3).
 * Uses the shift-register debounce engine with 16-bit width (160 ms at 10 ms
 * polling) and drives the arm indicator LED based on the debounced state.
 */

#include "rlc_arm_switch.h"
#include "rlc_debounce.h"
#include "rlc_watchdog.h"
#include "pin_config.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "arm_sw";

/* Debounce state */
static rlc_debounce_t s_db;

/* User callback on debounced state change */
static void (*s_on_change_cb)(bool armed) = NULL;

/* Current debounced arm state (protected by atomic reads on ESP32) */
static volatile bool s_armed = false;

/* Task handle */
static TaskHandle_t s_task_handle = NULL;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static void set_arm_led(bool on)
{
    /* Active-LOW LED: ON → drive LOW, OFF → drive HIGH */
    int level = on ? PIN_ARM_LED_ACTIVE : !PIN_ARM_LED_ACTIVE;
    gpio_set_level(PIN_ARM_LED, level);
}

static void on_debounce_change(int gpio_num, bool new_state, void *user_data)
{
    (void)gpio_num;
    (void)user_data;

    /* new_state == true  → active/LOW → armed
     * new_state == false → inactive/HIGH → disarmed
     */
    s_armed = new_state;
    set_arm_led(new_state);

    ESP_LOGI(TAG, "arm state changed: %s", new_state ? "ARMED" : "DISARMED");

    if (s_on_change_cb) {
        s_on_change_cb(new_state);
    }
}

/* ------------------------------------------------------------------ */
/* FreeRTOS task                                                      */
/* ------------------------------------------------------------------ */

static void arm_switch_task(void *arg)
{
    (void)arg;

    esp_task_wdt_add(NULL);   /* 5.11: self-register (see rlc_base_battery.c) */

    ESP_LOGI(TAG, "task started");

    while (1) {
        int level = gpio_get_level(PIN_ARM_SWITCH);
        rlc_debounce_update(&s_db, level, on_debounce_change, NULL);
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void arm_switch_init(void)
{
    /* Configure arm switch GPIO — input with pull-up (fail-safe: disconnected = HIGH = disarmed) */
    gpio_config_t sw_cfg = {
        .pin_bit_mask   = (1ULL << PIN_ARM_SWITCH),
        .mode           = GPIO_MODE_INPUT,
        .pull_up_en     = GPIO_PULLUP_ENABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE,
    };
    gpio_config(&sw_cfg);

    /* Configure arm LED GPIO — output, start OFF (drive HIGH for active-LOW LED) */
    gpio_config_t led_cfg = {
        .pin_bit_mask   = (1ULL << PIN_ARM_LED),
        .mode           = GPIO_MODE_INPUT_OUTPUT,  /* need gpio_set_level */
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);
    gpio_set_level(PIN_ARM_LED, !PIN_ARM_LED_ACTIVE);  /* LED OFF initially */

    /* Initialise debounce engine — 16-bit (160 ms at 10 ms polling) */
    rlc_debounce_init(&s_db, PIN_ARM_SWITCH, DEBOUNCE_16BIT);

    /* LED off until the debouncer establishes the initial state (first poll);
     * the initial determination fires no callback (see rlc_debounce.c). */
    s_armed = false;
    set_arm_led(false);

    ESP_LOGI(TAG, "initialised (SW=%d, LED=%d)", PIN_ARM_SWITCH, PIN_ARM_LED);
}

void arm_switch_start_task(void)
{
    xTaskCreatePinnedToCore(
        arm_switch_task,
        "arm_sw_task",
        3072,
        NULL,
        6,
        &s_task_handle,
        0   /* Core 0 */
    );

}

bool arm_switch_is_armed(void)
{
    return s_armed;
}

void arm_switch_register_cb(void (*cb)(bool armed))
{
    s_on_change_cb = cb;
}
