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
 * Fresh-press safety (4.12): press events are edge-triggered — the press
 * callback fires only on a released->pressed transition. A button already
 * held at power-on can therefore never generate EVT_FIRE_BUTTON_PRESSED
 * without a release first, which is the interlock FSD 5.5.3 calls for.
 * The former fire_button_was_fresh_press() polling API was dead code and
 * has been removed.
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

/* ── State LED (FSD line 1110) ─────────────────────────────────── */

/* Latched so the 20 Hz caller only touches GPIO on an actual change. */
static bool s_live = false;

void fire_button_set_live(bool live)
{
    if (live == s_live) return;
    s_live = live;
    led_red(live);
    led_green(!live);
}

/* ── Debounce change callback ──────────────────────────────────── */

static void on_change_cb(int gpio_num, bool new_state, void *user_data)
{
    (void)gpio_num;
    (void)user_data;

    /* new_state: true = active/pressed/LOW, false = released/HIGH */

    /* The LEDs are NOT touched here. Until 2026-08-26 this callback drove them
     * directly — red while held, green while released — so the ring reported
     * the operator's own finger rather than whether the button would do
     * anything. FSD line 1110 specifies red for ARMED/PRE_FIRE/FIRING and
     * green for safe/IDLE; that is state, and only the FSM knows it. Lighting
     * red on a press the FSM ignores (§8.2.3: the fire button does nothing in
     * IDLE) is exactly the misleading-indicator problem §7.2.9a exists to
     * prevent. See fire_button_set_live(). */
    if (new_state) {
        if (s_on_press) {
            s_on_press();
        }
    } else {
        if (s_on_release) {
            s_on_release();
        }
    }
}

/* ── Task ──────────────────────────────────────────────────────── */

static void fire_btn_task(void *arg)
{
    (void)arg;

    esp_task_wdt_add(NULL);   /* 5.11: self-register (see rlc_base_battery.c) */

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

    /* Boot state: not live → green. Must agree with s_live's initialiser
     * (false), or the latch in fire_button_set_live() would swallow the first
     * real transition. */
    led_red(false);
    led_green(true);

    /* Init debounce engine (8-bit = 80 ms at 10 ms poll) */
    rlc_debounce_init(&s_db, PIN_FIRE_BUTTON, DEBOUNCE_8BIT);

    if (gpio_get_level(PIN_FIRE_BUTTON) == 0) {   /* LOW = pressed */
        ESP_LOGW(TAG, "Button held at boot — no press event until released");
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

    ESP_LOGI(TAG, "Task created");
}

bool fire_button_is_pressed(void)
{
    return rlc_debounce_get_state(&s_db);
}

void fire_button_register_cb(void (*on_press)(void), void (*on_release)(void))
{
    s_on_press   = on_press;
    s_on_release = on_release;
}
