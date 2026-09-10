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
#include "rlc_config.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

static const char *TAG = "arm_sw";

/* Debounce state — one engine per contact. The NO leg (s_db) is
 * authoritative for the arm state; the NC leg (s_db_nc) is the cross-check. */
static rlc_debounce_t s_db;
static rlc_debounce_t s_db_nc;

/* User callback on debounced state change */
static void (*s_on_change_cb)(bool armed) = NULL;

/* Current debounced arm state (protected by atomic reads on ESP32) */
static volatile bool s_armed = false;

/* Task handle */
static TaskHandle_t s_task_handle = NULL;

/* §5.5.2 contact cross-check. s_disagree_since_ms is the timestamp at which
 * the two contacts stopped being complementary, or 0 while they agree. */
static void (*s_on_fault_cb)(bool fault) = NULL;
static volatile bool s_fault = false;
static int64_t s_disagree_since_ms = 0;

static inline int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* Both engines report "true" for an ACTIVE (LOW, contact closed) input. On a
 * healthy SPDT exactly one contact is closed, so agreement means the two
 * debounced states differ. Both-closed and both-open are the two impossible
 * combinations — see arm_switch_get_fault(). */
static void check_contacts(bool no_closed, bool nc_closed)
{
    bool complementary = (no_closed != nc_closed);
    int64_t t = now_ms();

    if (complementary) {
        s_disagree_since_ms = 0;
        if (s_fault) {
            s_fault = false;
            ESP_LOGW(TAG, "arm key contacts agree again — fault cleared");
            if (s_on_fault_cb) s_on_fault_cb(false);
        }
        return;
    }

    /* Break-before-make: both contacts are open for the moment it takes to
     * turn the key. Only a disagreement that persists is a fault. */
    if (s_disagree_since_ms == 0) {
        s_disagree_since_ms = t;
        return;
    }
    if (!s_fault && (t - s_disagree_since_ms) >= ARM_SWITCH_DISAGREE_MS) {
        s_fault = true;
        ESP_LOGE(TAG, "ARM KEY SWITCH FAULT — NO(GPIO %d)=%s NC(GPIO %d)=%s "
                      "for %d ms. Arm state still follows the NO contact.",
                 PIN_ARM_SWITCH, no_closed ? "closed" : "open",
                 PIN_ARM_SWITCH_NC, nc_closed ? "closed" : "open",
                 ARM_SWITCH_DISAGREE_MS);
        if (s_on_fault_cb) s_on_fault_cb(true);
    }
}

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

        /* The NC leg is polled and debounced identically, but deliberately
         * with no callback: it drives nothing, it only corroborates. */
        rlc_debounce_update(&s_db_nc, gpio_get_level(PIN_ARM_SWITCH_NC),
                            NULL, NULL);

        /* N1: the debouncer deliberately fires NO callback on its very first
         * stable determination (rlc_debounce.c) — that suppression exists for
         * the fire button, whose fresh-press interlock must not see a
         * held-at-boot button as a press. The arm key is the opposite case:
         * a key already turned to ARM at power-up is a state we must adopt,
         * not an edge we must ignore. Without this sync s_armed stayed false
         * until the operator physically toggled the key off and back on, so
         * every long-press was refused with "TURN ARM KEY FIRST" and the arm
         * LED never lit. Syncing from the debouncer every poll picks the
         * initial determination up and is a no-op once the callback runs. */
        if (rlc_debounce_is_stable(&s_db)) {
            bool st = rlc_debounce_get_state(&s_db);
            if (st != s_armed) {
                s_armed = st;
                set_arm_led(st);
                ESP_LOGI(TAG, "arm state adopted from debouncer: %s",
                         st ? "ARMED" : "DISARMED");
            }
        }

        /* Cross-check only once both engines have a stable determination —
         * before that, "disagreement" would just be the two 160 ms windows
         * filling at different rates. */
        if (rlc_debounce_is_stable(&s_db) && rlc_debounce_is_stable(&s_db_nc)) {
            check_contacts(rlc_debounce_get_state(&s_db),
                           rlc_debounce_get_state(&s_db_nc));
        }

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

    /* NC contact — same configuration. Claiming the pin matters even beyond
     * the cross-check: its common is at ground, so leaving GPIO 2 unclaimed
     * left a pin that FSD C.2 advertised as spare while it was hard-shorted to
     * GND for as long as the key sat at SAFE. Configured as a pulled-up input
     * it is safe in both key positions and can never be assigned as an output
     * by mistake. */
    gpio_config_t nc_cfg = {
        .pin_bit_mask   = (1ULL << PIN_ARM_SWITCH_NC),
        .mode           = GPIO_MODE_INPUT,
        .pull_up_en     = GPIO_PULLUP_ENABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE,
    };
    gpio_config(&nc_cfg);

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
    rlc_debounce_init(&s_db_nc, PIN_ARM_SWITCH_NC, DEBOUNCE_16BIT);

    /* Start disarmed. The initial determination fires no callback (see
     * rlc_debounce.c), so arm_switch_task adopts it explicitly from
     * rlc_debounce_get_state() within the first ~160 ms — including the case
     * where the key is already turned to ARM at power-up (N1). */
    s_armed = false;
    set_arm_led(false);

    s_fault = false;
    s_disagree_since_ms = 0;

    ESP_LOGI(TAG, "initialised (NO=%d, NC=%d, LED=%d)",
             PIN_ARM_SWITCH, PIN_ARM_SWITCH_NC, PIN_ARM_LED);
}

void arm_switch_start_task(void)
{
    /* m9: checked. Without this task arm_switch_is_armed() is frozen at
     * false, so the arm key can never be seen — every long-press refused,
     * with no explanation. Halt rather than present a dead arm path. */
    if (xTaskCreatePinnedToCore(
            arm_switch_task,
            "arm_sw_task",
            3072,
            NULL,
            6,
            &s_task_handle,
            0   /* Core 0 */
        ) != pdPASS) {
        ESP_LOGE(TAG, "arm switch task create FAILED — arm key cannot be read. HALTING.");
        while (1) { vTaskDelay(portMAX_DELAY); }
    }
}

bool arm_switch_is_armed(void)
{
    return s_armed;
}

void arm_switch_register_cb(void (*cb)(bool armed))
{
    s_on_change_cb = cb;
}

bool arm_switch_get_fault(void)
{
    return s_fault;
}

void arm_switch_register_fault_cb(void (*cb)(bool fault))
{
    s_on_fault_cb = cb;
}
