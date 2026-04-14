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
 * Contact-welding detection (FSD sec 5.4.3, 7.3.2).
 *
 * When the arm relay drive GPIO is LOW (de-energised), verify that the
 * arm sense input also reads LOW.  If arm sense reads HIGH while the relay
 * is off, the arm relay contacts are welded shut -- a critical fault.
 *
 * We only check the raw GPIO here (not debounced) to catch intermittent
 * welding as early as possible.  A single HIGH reading when the relay is
 * off is sufficient to flag the fault.
 */
static void weld_check(void)
{
    int arm_relay_level = gpio_get_level(PIN_ARM_RELAY);

    /* Only check when arm relay is de-energised (LOW) */
    if (arm_relay_level != 0) {
        return;
    }

    int arm_sense_level = gpio_get_level(PIN_ARM_SENSE);

    if (arm_sense_level != 0) {
        ESP_LOGE(TAG, "CONTACT WELD DETECTED: arm relay OFF but sense reads HIGH");
        if (s_on_change_cb) {
            /* Report fault via callback. The integrator decides the response.
             * We signal this as armed=true when it should not be, so the
             * state machine can trigger a critical fault. */
            s_on_change_cb(true);
        }
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

        /* Feed the debounce engine */
        rlc_debounce_update(&s_db, level, on_debounce_change, NULL);

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

    /* Initialise debounce engine -- 16-bit (160 ms at 10 ms polling) */
    rlc_debounce_init(&s_db, PIN_ARM_SENSE, DEBOUNCE_16BIT);

    /* Pre-read the raw GPIO to seed the initial state */
    int raw = gpio_get_level(PIN_ARM_SENSE);
    s_armed = (raw != 0);

    ESP_LOGI(TAG, "initialised (GPIO %d, raw=%d, armed=%d)",
             PIN_ARM_SENSE, raw, (int)s_armed);
}

void arm_sense_start_task(void)
{
    xTaskCreatePinnedToCore(
        arm_sense_task,
        "arm_switch_task",
        2048,
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
