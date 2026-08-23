/**
 * RLC Base Unit — Application Entry Point
 *
 * Phase 3: Full state machine with command processing.
 * All I/O tasks running — continuity ADC, arm sense debounce,
 * battery monitoring, and STATUS_UPDATE generation with real data.
 *
 * Boot sequence follows FSD §9.13.
 */

#include "rlc_base.h"
#include "rlc_relay.h"
#include "rlc_siren.h"
#include "rlc_base_state.h"
#include "rlc_base_fsm.h"
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
#include "rlc_continuity.h"
#include "rlc_arm_sense.h"
#include "rlc_base_battery.h"
#include "rlc_status_update.h"

/* Phase 3 headers */
#include "rlc_fsm_events.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rlc_base";

/**
 * STATUS_UPDATE trigger — called when continuity changes.
 * Runs from continuity_task context, so it must be minimal (just set a flag).
 */
static void on_io_change(void)
{
    status_update_trigger();
}

/**
 * Arm sense callback — forward to FSM as EVT_ARM_SENSE_CHANGED.
 * 5.3: J4-style short blocking send (10 ms), matching the key-switch and
 * weld-fault siblings — base_fsm_post_event's zero-timeout send could drop
 * the arm-sense-lost event on a transient queue burst, delaying disarm.
 */
static void on_arm_change_cb(bool armed)
{
    status_update_trigger();
    if (base_fsm_get_queue()) {
        rlc_fsm_event_t evt = {0};
        evt.type = EVT_ARM_SENSE_CHANGED;
        evt.data.arm_state.armed = armed;
        if (xQueueSend(base_fsm_get_queue(), &evt, pdMS_TO_TICKS(10)) != pdTRUE) {
            ESP_LOGE(TAG, "FSM queue full — EVT_ARM_SENSE_CHANGED dropped!");
        }
    }
}

/**
 * Contact welding fault callback — forward to FSM as EVT_ARM_SENSE_FAULT.
 */
static void on_arm_fault_cb(void)
{
    ESP_LOGE(TAG, "ARM RELAY CONTACT WELD FAULT");
    if (base_fsm_get_queue()) {
        rlc_fsm_event_t evt = {0};
        evt.type = EVT_ARM_SENSE_FAULT;
        /* J4: short blocking timeout so a transient queue burst can't drop a
         * safety event. Callback runs in arm_sense_task at priority 7 — a
         * 10 ms wait is acceptable. */
        if (xQueueSend(base_fsm_get_queue(), &evt, pdMS_TO_TICKS(10)) != pdTRUE) {
            ESP_LOGE(TAG, "FSM queue full — ARM_SENSE_FAULT dropped!");
        }
    }
}

/**
 * Key switch callback — forward to FSM as EVT_KEY_SWITCH_CHANGED.
 */
static void on_key_change_cb(bool on)
{
    status_update_trigger();
    if (base_fsm_get_queue()) {
        rlc_fsm_event_t evt = {0};
        evt.type = EVT_KEY_SWITCH_CHANGED;
        evt.data.arm_state.armed = on;
        if (xQueueSend(base_fsm_get_queue(), &evt, pdMS_TO_TICKS(10)) != pdTRUE) {
            ESP_LOGW(TAG, "FSM queue full — EVT_KEY_SWITCH_CHANGED dropped");
        }
    }
}

void base_app_main(void)
{
    ESP_LOGI(TAG, "=== RLC Base Unit v%s ===", RLC_VERSION_STRING);

    /* N3: reconfigure the TWDT before ANY task exists — it rebuilds the
     * subscriber list, so a later call unsubscribes tasks that have already
     * self-registered (this is what rebooted the remote every 11.4 s). The
     * base's ordering was already safe by accident; make it deliberate, and
     * identical on both units. app_main subscribes itself further down. */
    rlc_watchdog_init();

    /* §9.13 Step 1: GPIO safe state FIRST — before any other init. */
    relay_init();
    siren_init();
    ESP_LOGI(TAG, "safety outputs initialised — all relays safe");

    /* The strip comes up before the self-tests so a self-test failure can
     * actually be signalled on it. Previously the halt below set
     * LED_PATTERN_ERROR on an uninitialised strip, so a failing base halted
     * with no visible indication at all — the remote does it in this order. */
    rlc_rgb_led_init();
    rlc_rgb_led_set_pixel_count(NUM_CHANNELS);  /* 8-pixel igniter strip */
    rlc_rgb_led_set_brightness(RGB_LED_BRIGHTNESS_BASE);
    rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);

    /* §9.13 Step 2-3: Boot self-tests (CRC32-C, struct offsets) */
    if (rlc_selftest_run() != 0) {
        ESP_LOGE(TAG, "self-tests FAILED — halting");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        vTaskDelay(portMAX_DELAY);
    }

    /* §9.13 Step 4: Initialise ADC calibration + battery */
    rlc_battery_init(PIN_VBAT_ADC, BASE_VBAT_DIVIDER_RATIO);

    /* §9.13 Step 5: Initialise ESP-NOW */
    if (rlc_espnow_init() != 0) {
        ESP_LOGE(TAG, "ESP-NOW init failed");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        return;
    }

    uint8_t remote_mac[] = REMOTE_MAC_ADDR;
    int retries = 3;
    while (rlc_espnow_add_peer(remote_mac) != 0 && retries-- > 0) {
        ESP_LOGW(TAG, "peer registration failed, retrying (%d left)", retries);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (retries < 0) {
        ESP_LOGE(TAG, "peer registration failed — ERROR");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        return;
    }

    /* §9.13 Step 7: Configure input GPIOs + start debounce engines */
    arm_sense_init();
    continuity_init();   /* Configures ADC1 for GPIO 2,10,4-9 */

    /* Register I/O change callbacks for event-driven STATUS_UPDATE */
    continuity_register_change_cb(on_io_change);
    arm_sense_register_cb(on_arm_change_cb);
    arm_sense_register_fault_cb(on_arm_fault_cb);
    key_sense_register_cb(on_key_change_cb);

    /* (§9.13 Step 8: the TWDT was reconfigured at the top of this function —
     * see the N3 note there. Nothing to do here.) */

    /* §9.13 Step 9: Start FreeRTOS tasks */
    /* Priority 7 — arm switch (highest safety) */
    arm_sense_start_task();
    /* Priority 5 — continuity ADC sampling */
    continuity_start_task();
    /* Priority 3 — battery monitoring */
    base_battery_start_task();
    /* Priority 3 — STATUS_UPDATE generation */
    status_update_start_task();

    /* §9.13 Step 10: Begin link establishment */
    if (rlc_link_init(RLC_LINK_ROLE_BASE, remote_mac) != 0) {
        ESP_LOGE(TAG, "link manager init failed");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        return;
    }

    /* Phase 3: Initialise the base FSM (creates event queue).
     * M8: Queue is registered AFTER both init calls to avoid race
     * where link_task posts events before s_cmd_queue is set. */
    if (base_fsm_init() != 0) {
        ESP_LOGE(TAG, "base FSM init failed");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        return;
    }

    /* M8: Register FSM queue with link manager now that both are initialised. */
    rlc_link_register_cmd_queue(base_fsm_get_queue());

    if (base_fsm_start() != 0) {
        ESP_LOGE(TAG, "base FSM task start failed");
        rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
        return;
    }

    /* Set link guard — reject LINK_REQUEST when FSM is busy */
    rlc_link_set_guard(base_state_is_busy);

    /* m8: push a real STATUS_UPDATE straight after a handshake instead of the
     * placeholder frame the link layer used to fabricate (base_state = IDLE,
     * no error flags — a false "safe" if the base was in ERROR). Just sets a
     * flag; status_update_task builds the frame on its next 100 ms tick. */
    rlc_link_set_status_request_cb(status_update_trigger);

    ESP_LOGI(TAG, "base ready — Phase 3 FSM active, waiting for commands");

    /* N3: subscribe app_main only now, once the slow init (NVS/Wi-Fi
     * bring-up, peer registration retries) is behind us. */
    rlc_watchdog_register_self();

    /* Housekeeping loop — watchdog + status log + LED status feeds */
    int64_t last_status_log_ms = 0;
    while (1) {
        rlc_watchdog_feed();

        /* Feed the 8-pixel igniter strip: one pixel per channel, the armed or
         * firing channel highlighted, plus the alarm and key-warning layers.
         * Done here rather than in the FSM to keep the fire path untouched. */
        rlc_rgb_led_set_channel_bands(continuity_get_bands());
        uint8_t firing_ch = base_fsm_get_firing_channel();
        uint8_t armed_ch  = base_fsm_get_armed_channel();
        rlc_rgb_led_set_active_channel(firing_ch ? firing_ch : armed_ch);

        rlc_link_status_t led_ls;
        rlc_link_get_status(&led_ls);

        /* ERR_VBAT_LOW is never raised by the FSM (only CRITICAL, which goes
         * straight to ERROR), so compare the live reading against the arming
         * floor. 0 mV means the ADC has not produced a sample yet. */
        uint16_t vbat_mv = rlc_battery_get_voltage_mv();
        uint32_t alarms = 0;
        if (led_ls.state != RLC_LINK_STATE_LINKED)             alarms |= RLC_ALARM_LINK_LOST;
        if (vbat_mv > 0 && vbat_mv < BASE_VBAT_MIN_ARM_MV)     alarms |= RLC_ALARM_BATTERY;
        if (base_fsm_get_error_flags() & ERR_RELAY_FAULT)      alarms |= RLC_ALARM_ARM_FAULT;
        rlc_rgb_led_set_alarms(alarms);

        /* Key switch in ARM but nothing armed yet: the whole map breathes.
         * The base never learns the remote's cursor, so there is no single
         * channel to single out here (the remote breathes its own). */
        rlc_rgb_led_set_key_warning(key_sense_get_debounced() && !armed_ch && !firing_ch);

        int64_t now = esp_timer_get_time() / 1000;

#if CONT_TRACE_INTERVAL_MS > 0
        /* Compact raw-ADC trace for bench work: one line per sweep, raw counts
         * only, so a load can be swapped on a channel and the change watched
         * directly. CH1 also carries mV and band since that is the channel
         * currently under test. */
        static int64_t last_trace_ms = 0;
        if (now - last_trace_ms >= CONT_TRACE_INTERVAL_MS) {
            char tbuf[128];
            int tn = 0;
            for (int c = 1; c <= NUM_CHANNELS && tn < (int)sizeof(tbuf) - 12; c++) {
                tn += snprintf(tbuf + tn, sizeof(tbuf) - tn, "%s%ld",
                               c == 1 ? "" : " ", (long)continuity_get_raw(c));
            }
            /* Rolling window over SWEEPS, purely a reading aid so a single
             * outlier cannot be mistaken for a real change. The per-value
             * figure is already the CONT_OVERSAMPLE_COUNT (64) burst average
             * the band classifier acts on; this adds nothing to that decision,
             * it only makes the number easier to read by eye. Watch spread:
             * while it is wide the window still straddles a load change. */
            #define TRACE_WIN 8
            static int32_t win[TRACE_WIN];
            static int win_n = 0, win_i = 0;
            int32_t r1 = continuity_get_raw(1);
            win[win_i] = r1;
            win_i = (win_i + 1) % TRACE_WIN;
            if (win_n < TRACE_WIN) win_n++;
            int32_t wmin = win[0], wmax = win[0];
            int64_t wsum = 0;
            for (int k = 0; k < win_n; k++) {
                wsum += win[k];
                if (win[k] < wmin) wmin = win[k];
                if (win[k] > wmax) wmax = win[k];
            }
            int32_t wmean10 = (int32_t)((wsum * 10) / win_n);

            static const char *tb[] = { "OPEN", "CONN", "MARG", "CONN" };
            ESP_LOGI(TAG, "TRACE %s | ch1 now %ld  mean %ld.%ld  min %ld max %ld "
                          "spread %ld (n=%d)  %ld uV  %s",
                     tbuf, (long)r1, (long)(wmean10/10), (long)(wmean10%10),
                     (long)wmin, (long)wmax, (long)(wmax-wmin), win_n,
                     (long)continuity_get_uv(1),
                     tb[continuity_get_channel(1) & 3]);
            last_trace_ms = now;
        }
#endif

        if (now - last_status_log_ms >= 5000) {
            rlc_link_status_t ls;
            rlc_link_get_status(&ls);
            uint16_t bands = continuity_get_bands();
            char errbuf[80];
            ESP_LOGI(TAG, "state=%d armed=%u firing=%u rssi=%d txfail=%lu vbat=%u mv cont=0x%04x arm=%d key=%d err=0x%02x (%s)",
                     base_fsm_get_state(), base_fsm_get_armed_channel(),
                     base_fsm_get_firing_channel(),
                     ls.rssi_avg_dbm,
                     (unsigned long)rlc_espnow_get_send_failure_total(),
                     rlc_battery_get_voltage_mv(),
                     bands, arm_sense_get_debounced(),
                     key_sense_get_debounced(),
                     base_fsm_get_error_flags(),
                     rlc_error_flags_str(base_fsm_get_error_flags(),
                                         errbuf, sizeof(errbuf)));
            char cbuf[208];
            int n = 0;
            for (int c = 1; c <= NUM_CHANNELS && n < (int)sizeof(cbuf) - 24; c++) {
                /* Index 3 is the deprecated SHORT value: never produced, but a stale
                 * cached band must not print a name the system no longer uses. */
                static const char *bn[] = { "OPEN", "CONN", "MARG", "CONN" };
                n += snprintf(cbuf + n, sizeof(cbuf) - n, " ch%d=%ld/%ld/%s", c,
                              (long)continuity_get_raw(c),
                              (long)continuity_get_uv(c),
                              bn[continuity_get_channel(c) & 3]);
            }
            ESP_LOGI(TAG, "cont raw/uV:%s", cbuf);

            last_status_log_ms = now;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
