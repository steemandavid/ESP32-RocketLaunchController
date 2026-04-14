/**
 * RLC Fire Button Driver
 *
 * Debounced fire button with fresh-press detection and dual-LED control.
 * FSD refs: 5.5.3, 8.3.2, 9.10
 *
 * GPIO:
 *   PIN_FIRE_BUTTON   (GPIO 15) — input, pull-up, active LOW
 *   PIN_FIRE_LED_RED  (GPIO 17) — output, active HIGH
 *   PIN_FIRE_LED_GREEN(GPIO 18) — output, active HIGH
 *
 * Debounce: 8-bit shift register, 10 ms poll -> 80 ms settle.
 *   0x00 = pressed (active LOW), 0xFF = released
 *
 * Fresh-press safety: the button must transition from released to
 * pressed AFTER boot to count as a fresh press. A stuck button at
 * power-on is ignored until it is released first.
 */

#include "rlc_fire_button.h"
#include "rlc_debounce.h"
#include "rlc_watchdog.h"
#include "pin_config.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "fire_btn";

/* ── Static state ──────────────────────────────────────────────── */

static rlc_debounce_t s_db;
static volatile bool s_was_released = true;   /* assumes released at boot */
static volatile bool s_fresh_press  = false;

static void (*s_on_press)(void)   = NULL;
static void (*s_on_release)(void) = NULL;

static TaskHandle_t s_task_handle = NULL;

/* ── LED helpers ───────────────────────────────────────────────── */

static inline void led_red(bool on)
{
    gpio_set_level(PIN_FIRE_LED_RED, on ? 1 : 0);
}

static inline void led_green(bool on)
{
    gpio_set_level(PIN_FIRE_LED_GREEN, on ? 1 : 0);
}

/* ── Debounce change callback ──────────────────────────────────── */

static void on_change_cb(int gpio_num, bool new_state, void *user_data)
{
    (void)gpio_num;
    (void)user_data;

    /* new_state: true = active/pressed/LOW, false = released/HIGH */

    if (new_state) {
        /* Button pressed */
        if (s_was_released) {
            s_fresh_press  = true;
            s_was_released = false;
        }
        led_red(true);
        led_green(false);
        if (s_on_press) {
            s_on_press();
        }
    } else {
        /* Button released */
        s_was_released = true;
        led_red(false);
        led_green(true);
        if (s_on_release) {
            s_on_release();
        }
    }
}

/* ── Task ──────────────────────────────────────────────────────── */

static void fire_btn_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Task started on core %d", xPortGetCoreID());

    for (;;) {
        int level = gpio_get_level(PIN_FIRE_BUTTON);
        rlc_debounce_update(&s_db, level, on_change_cb, NULL);
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ── Public API ────────────────────────────────────────────────── */

void fire_button_init(void)
{
    /* Fire button: input with pull-up (LOW = pressed) */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << PIN_FIRE_BUTTON),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    /* Fire LEDs: outputs, active HIGH */
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << PIN_FIRE_LED_RED) | (1ULL << PIN_FIRE_LED_GREEN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);

    /* Default state: released → green LED on */
    led_red(false);
    led_green(true);

    /* Init debounce engine (8-bit = 80 ms at 10 ms poll) */
    rlc_debounce_init(&s_db, PIN_FIRE_BUTTON, DEBOUNCE_8BIT);

    /* Fresh-press safety: if button is already held at init, mark it as
     * not-released so the debounce settling does NOT generate a fresh press. */
    if (gpio_get_level(PIN_FIRE_BUTTON) == 0) {   /* LOW = pressed */
        s_was_released = false;
        ESP_LOGW(TAG, "Button held at boot — fresh-press suppressed");
    }

    ESP_LOGI(TAG, "Initialised (btn=%d, led_r=%d, led_g=%d)",
             PIN_FIRE_BUTTON, PIN_FIRE_LED_RED, PIN_FIRE_LED_GREEN);
}

void fire_button_start_task(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        fire_btn_task,
        "fire_btn_task",
        3072,
        NULL,
        7,                  /* priority — highest remote task */
        &s_task_handle,
        0                   /* core 0 */
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        return;
    }

    rlc_watchdog_add_task(s_task_handle);
    ESP_LOGI(TAG, "Task created, handle=%p", s_task_handle);
}

bool fire_button_is_pressed(void)
{
    return rlc_debounce_get_state(&s_db);
}

bool fire_button_was_fresh_press(void)
{
    if (s_fresh_press) {
        s_fresh_press = false;
        return true;
    }
    return false;
}

void fire_button_register_cb(void (*on_press)(void), void (*on_release)(void))
{
    s_on_press   = on_press;
    s_on_release = on_release;
}
