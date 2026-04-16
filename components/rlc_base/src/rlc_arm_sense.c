/**
 * RLC Arm Sense Monitor (Base Unit)
 *
 * Monitors the arm sense input (GPIO 21 / PIN_ARM_SENSE) which samples the
 * arm relay COM output via a 27k/10k voltage divider with 3.3V zener clamp.
 *
 * Polarity (opposite to the remote arm switch):
 *   HIGH = arm relay closed / VBAT present on fire path = ARMED
 *   LOW  = arm relay de-energised / no VBAT             = DISARMED
 *
 * The debounce engine treats LOW = active and HIGH = inactive, so we invert
 * in the callback: stable_state == false (stably HIGH) => armed.
 *
 * Contact-welding detection (FSD sec 5.4.3, 7.3.2):
 *   When the arm relay GPIO (PIN_ARM_RELAY) is known LOW (de-energised),
 *   the arm sense GPIO must also read LOW.  A HIGH reading indicates the
 *   arm relay contacts are welded shut -- a critical fault.
 */

#include "rlc_arm_sense.h"
#include "rlc_debounce.h"
#include "rlc_watchdog.h"
#include "rlc_relay.h"
#include "pin_config.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "arm_sense";

/* ── Debounce state ──────────────────────────────────────────────── */

static rlc_debounce_t s_db;

/* Current debounced arm sense state (true = armed / VBAT present) */
static volatile bool s_armed = false;

/* User callback on debounced state change */
static void (*s_on_change_cb)(bool armed) = NULL;

/* User callback on contact-welding fault */
static void (*s_on_fault_cb)(void) = NULL;

/* ── Key switch debounce state ─────────────────────────────────── */

static rlc_debounce_t s_key_db;

/* Current debounced key switch state (true = key ON / VBAT present) */
static volatile bool s_key_on = false;

/* User callback on key switch state change */
static void (*s_on_key_change_cb)(bool on) = NULL;

/* Task handle */
static TaskHandle_t s_task_handle = NULL;

/* Tick counter for periodic welding check (runs every 500 ms) */
static uint32_t s_weld_check_ticks = 0;

#define WELD_CHECK_INTERVAL_MS  500
#define POLL_INTERVAL_MS        10
#define WELD_CHECK_DIVISOR      (WELD_CHECK_INTERVAL_MS / POLL_INTERVAL_MS)

/* ── Internal helpers ────────────────────────────────────────────── */

/**
 * Debounce engine callback.
 *
 * The debounce engine convention:
 *   new_state == true  => stably LOW  => arm sense LOW  => disarmed
 *   new_state == false => stably HIGH => arm sense HIGH => armed
 *
 * So the arm sense armed state is the logical inverse of new_state.
 */
static void on_debounce_change(int gpio_num, bool new_state, void *user_data)
{
    (void)gpio_num;
    (void)user_data;

    bool armed = !new_state;  /* Invert: LOW=disarmed, HIGH=armed */
    s_armed = armed;

    ESP_LOGI(TAG, "arm sense changed: %s", armed ? "ARMED (VBAT present)" : "DISARMED");

    if (s_on_change_cb) {
        s_on_change_cb(armed);
    }
}

/**
 * Key switch debounce callback.
 *
 * Same polarity as arm sense: the debounce engine convention has
 * new_state == true => stably LOW, new_state == false => stably HIGH.
 * Key sense HIGH = key ON, so key_on = !new_state.
 */
static void on_key_debounce_change(int gpio_num, bool new_state, void *user_data)
{
    (void)gpio_num;
    (void)user_data;

    bool key_on = !new_state;
    s_key_on = key_on;

    ESP_LOGI(TAG, "key switch changed: %s", key_on ? "ON" : "OFF");

    if (s_on_key_change_cb) {
        s_on_key_change_cb(key_on);
    }
}

/**
 * Contact-welding detection (FSD sec 5.4.3, 7.3.2).
 *
 * Uses the intended arm relay state (from arm_relay_set) and the debounced
 * arm_sense state (from the 16-bit debounce engine).  A welded contact is
 * declared when the relay was intentionally de-energised but arm_sense
 * remains debounced HIGH for WELD_CONFIRM_COUNT consecutive checks.
 *
 * This avoids false positives from:
 *   - GPIO readback glitches on the relay drive pin
 *   - Residual voltage transients on the sense divider after relay toggle
 */
#define WELD_CONFIRM_COUNT  3
static int s_weld_high_count = 0;

static void weld_check(void)
{
    /* Only check when arm relay is intentionally OFF */
    if (arm_relay_get_intended()) {
        s_weld_high_count = 0;
        return;
    }

    /* Use debounced arm_sense state (160ms stable = 16-bit debounce) */
    if (arm_sense_get_debounced()) {
        s_weld_high_count++;
        if (s_weld_high_count >= WELD_CONFIRM_COUNT) {
            ESP_LOGE(TAG, "CONTACT WELD DETECTED: relay OFF but sense debounced HIGH (%d consecutive)",
                     s_weld_high_count);
            if (s_on_fault_cb) {
                s_on_fault_cb();
            }
        }
    } else {
        s_weld_high_count = 0;
    }
}

/* ── FreeRTOS task ───────────────────────────────────────────────── */

static void arm_sense_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "task started");

    while (1) {
        /* Read the arm sense GPIO */
        int level = gpio_get_level(PIN_ARM_SENSE);

        /* Feed the arm sense debounce engine */
        rlc_debounce_update(&s_db, level, on_debounce_change, NULL);

        /* Read the key sense GPIO */
        int key_level = gpio_get_level(PIN_KEY_SENSE);

        /* Feed the key sense debounce engine */
        rlc_debounce_update(&s_key_db, key_level, on_key_debounce_change, NULL);

        /* Periodic contact-welding detection */
        s_weld_check_ticks++;
        if (s_weld_check_ticks >= WELD_CHECK_DIVISOR) {
            s_weld_check_ticks = 0;
            weld_check();
        }

        /* Feed the task watchdog */
        esp_task_wdt_reset();

        /* 10 ms poll interval */
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

void arm_sense_init(void)
{
    /* Configure arm sense GPIO -- input, no pulls (external divider + zener) */
    gpio_config_t cfg = {
        .pin_bit_mask   = (1ULL << PIN_ARM_SENSE),
        .mode           = GPIO_MODE_INPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    /* Initialise arm sense debounce engine -- 16-bit (160 ms at 10 ms polling) */
    rlc_debounce_init(&s_db, PIN_ARM_SENSE, DEBOUNCE_16BIT);

    /* Pre-read the raw GPIO to seed the initial state */
    int raw = gpio_get_level(PIN_ARM_SENSE);
    s_armed = (raw != 0);

    ESP_LOGI(TAG, "arm sense initialised (GPIO %d, raw=%d, armed=%d)",
             PIN_ARM_SENSE, raw, (int)s_armed);

    /* Configure key sense GPIO -- input, no pulls (external divider + zener) */
    gpio_config_t key_cfg = {
        .pin_bit_mask   = (1ULL << PIN_KEY_SENSE),
        .mode           = GPIO_MODE_INPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE,
    };
    gpio_config(&key_cfg);

    /* Initialise key sense debounce engine -- 16-bit (160 ms at 10 ms polling) */
    rlc_debounce_init(&s_key_db, PIN_KEY_SENSE, DEBOUNCE_16BIT);

    /* Pre-read the raw GPIO to seed the initial state */
    int key_raw = gpio_get_level(PIN_KEY_SENSE);
    s_key_on = (key_raw != 0);

    ESP_LOGI(TAG, "key sense initialised (GPIO %d, raw=%d, key_on=%d)",
             PIN_KEY_SENSE, key_raw, (int)s_key_on);
}

void arm_sense_start_task(void)
{
    xTaskCreatePinnedToCore(
        arm_sense_task,
        "arm_switch_task",
        4096,
        NULL,
        7,              /* Priority 7 -- highest base unit task */
        &s_task_handle,
        0               /* Core 0 */
    );

    /* Register with the task watchdog */
    rlc_watchdog_add_task(s_task_handle);
}

bool arm_sense_get_debounced(void)
{
    return s_armed;
}

bool arm_sense_get_raw(void)
{
    return gpio_get_level(PIN_ARM_SENSE) != 0;
}

void arm_sense_register_cb(void (*cb)(bool armed))
{
    s_on_change_cb = cb;
}

void arm_sense_register_fault_cb(void (*cb)(void))
{
    s_on_fault_cb = cb;
}

/* ── Key Sense Public API ──────────────────────────────────────── */

bool key_sense_get_debounced(void)
{
    return s_key_on;
}

bool key_sense_get_raw(void)
{
    return gpio_get_level(PIN_KEY_SENSE) != 0;
}

void key_sense_register_cb(void (*cb)(bool on))
{
    s_on_key_change_cb = cb;
}
