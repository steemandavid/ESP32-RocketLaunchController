/**
 * RLC Remote Unit — Application Entry Point
 *
 * Phase 3: Full state machine with command processing.
 * All I/O tasks running — fire button, arm switch, encoder,
 * battery monitoring, and link manager driving communication.
 *
 * Boot sequence follows FSD §9.13.
 */

#include "rlc_remote.h"
#include "rlc_remote_state.h"
#include "rlc_remote_fsm.h"
#include "rlc_remote_faultinject.h"
#include "rlc_encoder.h"
#include "rlc_buzzer.h"
#include "rlc_display.h"
#include "rlc_espnow.h"
#include "rlc_link.h"
#include "rlc_message.h"
#include "rlc_battery.h"
#include "rlc_rgb_led.h"
#include "rlc_watchdog.h"
#include "rlc_selftest.h"
#include "rlc_config.h"
#include "rlc_version.h"
#include "pin_config.h"

/* Phase 2 headers */
#include "rlc_fire_button.h"
#include "rlc_arm_switch.h"
#include "rlc_remote_battery.h"

/* Phase 3 headers */
#include "rlc_fsm_events.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"

static const char *TAG = "rlc_remote";

/* ── m4: Dedicated encoder task (FSD §9.10 — priority 3, core 0, 4096 stack) ── */

static void encoder_task_fn(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "encoder task started");

    while (1) {
        encoder_poll_button();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ── Input Callbacks → FSM Events ─────────────────────────────── */

static void on_fire_press(void)
{
    if (!remote_fsm_get_queue()) return;
    rlc_fsm_event_t evt = {0};
    evt.type = EVT_FIRE_BUTTON_PRESSED;
    (void)xQueueSend(remote_fsm_get_queue(), &evt, 0);
}

static void on_fire_release(void)
{
    if (!remote_fsm_get_queue()) return;
    rlc_fsm_event_t evt = {0};
    evt.type = EVT_FIRE_BUTTON_RELEASED;
    (void)xQueueSend(remote_fsm_get_queue(), &evt, 0);
}

static void on_arm_switch_change(bool armed)
{
    if (!remote_fsm_get_queue()) return;
    rlc_fsm_event_t evt = {0};
    evt.type = EVT_ARM_SWITCH_CHANGED;
    evt.data.arm_state.armed = armed;
    (void)xQueueSend(remote_fsm_get_queue(), &evt, 0);
}

static void on_encoder_rotate(uint8_t channel)
{
    if (!remote_fsm_get_queue()) return;
    rlc_fsm_event_t evt = {0};
    evt.type = EVT_ENCODER_ROTATE;
    evt.data.encoder.channel = channel;
    BaseType_t higher_prio_woken = pdFALSE;
    (void)xQueueSendFromISR(remote_fsm_get_queue(), &evt, &higher_prio_woken);
    if (higher_prio_woken) portYIELD_FROM_ISR();
}

static void on_encoder_press(void)
{
    if (!remote_fsm_get_queue()) return;
    rlc_fsm_event_t evt = {0};
    evt.type = EVT_ENCODER_SHORT_PRESS;
    (void)xQueueSend(remote_fsm_get_queue(), &evt, 0);
}

static void on_encoder_long_press(void)
{
    if (!remote_fsm_get_queue()) return;
    rlc_fsm_event_t evt = {0};
    evt.type = EVT_ENCODER_LONG_PRESS;
    (void)xQueueSend(remote_fsm_get_queue(), &evt, 0);
}

/* ── Application Entry Point ──────────────────────────────────── */

/**
 * RM-09 / CI-05: terminal boot failure.
 *
 * These paths used to `return` out of remote_app_main(), which left the FSM in
 * STATE_BOOT — not ERROR — with the housekeeping loop gone and app_main's
 * watchdog subscription never taken. The handheld looked merely idle. Latch a
 * visible, audible, unambiguous halt instead; the display message (when the
 * panel is up) names the failing step.
 */
static void boot_fail(const char *what)
{
    ESP_LOGE(TAG, "BOOT FAILED: %s — halting (power cycle required)", what);
    rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
    display_error(what);
    buzzer_play(BUZZER_ALARM_CRITICAL);
    vTaskDelay(portMAX_DELAY);
}

void remote_app_main(void)
{
    ESP_LOGI(TAG, "=== RLC Remote Unit v%s ===", RLC_VERSION_STRING);

    /* N3: reconfigure the TWDT before ANY task exists. It rebuilds the
     * subscriber list, so doing it later — as this unit used to, after
     * display_start_task() — silently unsubscribes every task that has
     * already self-registered. The remote then rebooted 11.4 s into every
     * boot: the display task logged "task not found" at 20 Hz, the watchdog
     * eventually triggered unfed, and the trigger handler panicked
     * (LoadProhibited) walking its stale entries. app_main subscribes itself
     * later, via rlc_watchdog_register_self(), once the slow init is done. */
    rlc_watchdog_init();

    /* Visual feedback first (remote has no relays/safety GPIOs). */
    /* CI-10: the return was discarded on both units. A failed strip means no
     * igniter status and no ARMED indication at all — not fatal (the unit is
     * still safe and the console still reports), but it must not be silent. */
    if (rlc_rgb_led_init() != 0) {
        ESP_LOGE(TAG, "RGB LED init FAILED — no strip indication this session");
    }
    rlc_rgb_led_set_pixel_count(NUM_CHANNELS);  /* 8-pixel igniter strip */
    rlc_rgb_led_set_brightness(RGB_LED_BRIGHTNESS_REMOTE);
    rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);

    /* §9.13: Boot self-tests (CRC32-C, struct offsets) */
    if (rlc_selftest_run() != 0) {
        /* Display is not up yet — LED + log only, so no display_error(). */
        ESP_LOGE(TAG, "self-tests FAILED — halting");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        vTaskDelay(portMAX_DELAY);
    }

    /* §9.13 step 6: display init + health check (ID read-back).
     * FSD §15.4 T-S10: a display failure at boot must halt in ERROR. */
    if (display_init() != 0 || !display_is_healthy()) {
        ESP_LOGE(TAG, "display init/health check FAILED (id=0x%08lX) — halting",
                 (unsigned long)display_get_id());
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        vTaskDelay(portMAX_DELAY);
    }
    display_start_task();

    /* CI-06: encoder_init() MUST run before rlc_battery_init().
     *
     * The encoder sits on GPIO 4/5, which are ADC1_CH3/ADC1_CH4. Bringing up
     * the ADC1 oneshot unit first reconfigures those pads and kills the
     * quadrature inputs — the knob then does nothing at all. The ordering has
     * been correct since the bug was found, but only by accident of layout:
     * nothing said so, and "sort these calls into FSD §9.13 step order" would
     * silently reintroduce it. Do not move rlc_battery_init() above this line.
     */
    encoder_init();
    buzzer_init();

    /* §9.13 Step 4: Initialise ADC calibration + battery (see CI-06 above) */
    if (rlc_battery_init(PIN_VBAT_ADC, REMOTE_VBAT_DIVIDER_RATIO) != 0) {
        boot_fail("BATTERY ADC INIT FAILED");
    }

    /* §9.13 Step 5: Initialise ESP-NOW */
    if (rlc_espnow_init() != 0) {
        boot_fail("ESP-NOW INIT FAILED");
    }

    uint8_t base_mac[] = BASE_MAC_ADDR;
    int retries = 3;
    while (rlc_espnow_add_peer(base_mac) != 0 && retries-- > 0) {
        ESP_LOGW(TAG, "peer registration failed, retrying (%d left)", retries);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (retries < 0) {
        boot_fail("PEER REGISTRATION FAILED");
    }

    /* §9.13 Step 7: Configure input GPIOs */
    fire_button_init();
    arm_switch_init();

    /* (§9.13 Step 8: the TWDT was reconfigured at the top of this function —
     * see the N3 note there. Nothing to do here.) */

    /* 5.7: register the input callbacks BEFORE starting the tasks that drive
     * them. They used to be wired up at the very end of init, after the FSM
     * came up — so any button press, key turn or encoder detent during the
     * first couple of hundred milliseconds was silently dropped. The
     * callbacks themselves are safe to install early: each one returns
     * immediately if remote_fsm_get_queue() is still NULL. */
    fire_button_register_cb(on_fire_press, on_fire_release);
    arm_switch_register_cb(on_arm_switch_change);
    encoder_register_rotate_cb(on_encoder_rotate);
    encoder_register_press_cb(on_encoder_press);
    encoder_register_long_press_cb(on_encoder_long_press);

    /* §9.13 Step 9: Start FreeRTOS tasks */
    /* Priority 7 — fire button (highest safety) */
    fire_button_start_task();
    /* Priority 6 — arm switch */
    arm_switch_start_task();
    /* Priority 3 — battery monitoring */
    remote_battery_start_task();
    /* m4: Dedicated encoder task (FSD §9.10 — priority 3, core 0, 4096 stack) */
    if (xTaskCreatePinnedToCore(encoder_task_fn, "encoder_task", 4096, NULL, 3,
                                NULL, 0) != pdPASS) {
        boot_fail("ENCODER TASK FAILED");
    }
    ESP_LOGI(TAG, "encoder task started (prio 3, core 0)");

    /* §9.13 Step 10: Begin link establishment */
    if (rlc_link_init(RLC_LINK_ROLE_REMOTE, base_mac) != 0) {
        boot_fail("LINK MANAGER INIT FAILED");
    }

    /* Phase 3: Initialise the remote FSM (creates event queue).
     * M8: Queue is registered AFTER both init calls to avoid race. */
    if (remote_fsm_init() != 0) {
        boot_fail("FSM INIT FAILED");
    }

    /* M8: Register FSM queue with link manager now that both are initialised. */
    rlc_link_register_cmd_queue(remote_fsm_get_queue());

    if (remote_fsm_start() != 0) {
        boot_fail("FSM TASK START FAILED");
    }

    /* (Input callbacks were registered before their tasks started — see 5.7
     * above.) */

    /* Test builds only: the injection console. Started after the FSM task so
     * remote_fsm_get_queue() is up and a keystroke cannot be dropped. Compiles
     * to nothing in a normal build. */
    remote_fault_inject_init();

    ESP_LOGI(TAG, "remote ready — Phase 3 FSM active, waiting for link");

    /* N3: subscribe app_main only now. All the slow init (SPI display,
     * NVS/Wi-Fi bring-up, peer registration retries) is behind us, so the
     * 10 ms housekeeping loop below can feed comfortably. */
    rlc_watchdog_register_self();

    /* Housekeeping loop — watchdog + LED status feeds + status log */
    int64_t last_log_ms = 0;
    int64_t last_led_ms = 0;
    while (1) {
        rlc_watchdog_feed();

        int64_t now = esp_timer_get_time() / 1000;

        /* Feed the 8-pixel igniter strip at 10 Hz. The continuity map comes
         * from the cached STATUS_UPDATE, so unlike the base it can go stale —
         * dim it rather than let old data read as live. Fed from here, never
         * from the FSM, to keep the fire path untouched. */
        if (now - last_led_ms >= 100) {
            rlc_payload_status_update_t st;
            bool fresh = remote_fsm_get_status(&st);
            if (fresh || st.update_sequence) {
                rlc_rgb_led_set_channel_bands(st.continuity_bands);
            }
            rlc_rgb_led_set_stale(!fresh);
            rlc_rgb_led_set_active_channel(remote_fsm_get_selected_channel());

            rlc_link_status_t led_ls;
            rlc_link_get_status(&led_ls);
            uint16_t vbat_mv = rlc_battery_get_voltage_mv();

            uint32_t alarms = 0;
            if (led_ls.state != RLC_LINK_STATE_LINKED)   alarms |= RLC_ALARM_LINK_LOST;
            if (vbat_mv > 0 && vbat_mv < REMOTE_VBAT_MIN_ARM_MV)
                                                        alarms |= RLC_ALARM_BATTERY;
            if (fresh && st.battery_voltage_mv > 0 &&
                st.battery_voltage_mv < BASE_VBAT_MIN_ARM_MV)
                                                        alarms |= RLC_ALARM_BATTERY;
            if (fresh && (st.error_flags & ERR_RELAY_FAULT))
                                                        alarms |= RLC_ALARM_ARM_FAULT;
            rlc_rgb_led_set_alarms(alarms);

            /* Arm switch on: the selected channel breathes, so the operator
             * sees exactly which igniter the next long-press would arm. */
            rlc_rgb_led_set_key_warning(arm_switch_is_armed());

            last_led_ms = now;
        }
        if (now - last_log_ms >= 5000) {
            rlc_link_status_t ls;
            rlc_link_get_status(&ls);
            uint32_t enc_isr = 0, enc_valid = 0, enc_steps = 0;
            encoder_get_stats(&enc_isr, &enc_valid, &enc_steps);
            ESP_LOGI(TAG, "state=%d armed=%u sel=%u rssi=%d missed=%u txfail=%lu contact=%lums "
                          "attempts=%u vbat=%u mv arm=%d fire=%d "
                          "enc[isr=%lu valid=%lu step=%lu]",
                     remote_fsm_get_state(), remote_fsm_get_armed_channel(),
                     remote_fsm_get_selected_channel(),
                     ls.rssi_avg_dbm, ls.missed_pings,
                     (unsigned long)rlc_espnow_get_send_failure_total(),
                     (unsigned long)ls.ms_since_contact, ls.linkreq_attempts,
                     rlc_battery_get_voltage_mv(), arm_switch_is_armed(),
                     fire_button_is_pressed(),
                     (unsigned long)enc_isr, (unsigned long)enc_valid,
                     (unsigned long)enc_steps);
            last_log_ms = now;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
