/**
 * RLC Base Unit State Machine Implementation
 *
 * Full FSM per FSD §7. Single-task-owner model: all state is owned by
 * base_fsm_task. External readers use getter functions (volatile reads).
 *
 * State transitions:
 *   BOOT → IDLE → ARMED → PRE_FIRE → FIRING → POST_FIRE → IDLE
 *   Any → LINK_LOST, Any → ERROR
 */

#include "rlc_base_fsm.h"
#include "rlc_fsm_events.h"
#include "rlc_link.h"
#include "rlc_message.h"
#include "rlc_protocol.h"
#include "rlc_relay.h"
#include "rlc_siren.h"
#include "rlc_fire_timer.h"
#include "rlc_arm_sense.h"
#include "rlc_continuity.h"
#include "rlc_battery.h"
#include "rlc_status_update.h"
#include "rlc_rgb_led.h"
#include "rlc_config.h"
#include "rlc_watchdog.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "rlc_bfsm";

/* ── Private State (owned exclusively by base_fsm_task) ──────── */

static volatile rlc_state_t s_state = STATE_BOOT;
static volatile uint8_t     s_armed_channel = 0;   /* 0=none, 1-8 */
static volatile uint8_t     s_firing_channel = 0;
static volatile uint8_t     s_error_flags = 0;

/* Software timer timestamps (ms since boot) */
static int64_t s_arm_time_ms = 0;
static int64_t s_prefire_start_ms = 0;
static int64_t s_postfire_start_ms = 0;

/* M1: Non-blocking arm sense verification state */
static bool     s_arm_verify_pending = false;
static uint8_t  s_arm_verify_channel = 0;
static int64_t  s_arm_verify_start_ms = 0;
static uint32_t s_arm_verify_seq = 0;

/* C1: Link lost pending during FIRING (COMPLETE_PULSE_ON_LINK_LOSS) */
static bool s_link_lost_pending = false;

/* C3/M3: Local dead-man timestamp — updated from wire-receive time of valid CMD_FIRE */
static int64_t s_last_fire_cmd_ms = 0;

/* Task and queue handles */
static QueueHandle_t s_evt_queue = NULL;
static TaskHandle_t  s_fsm_task = NULL;

/* Forward declarations */
static void base_fsm_task(void *arg);
static void send_ack(uint8_t msg_type, uint32_t seq_num, uint8_t channel);
static void send_nack(uint8_t msg_type, uint32_t seq_num, uint8_t reason);
static void do_disarm(void);
static void do_enter_error(uint8_t err_flag);
static void do_enter_link_lost(void);

/* ── Helpers ──────────────────────────────────────────────────── */

static inline int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* M1: Abort arm sense verification — safe relay de-energise + NACK. */
static void abort_arm_verify(uint8_t nack_reason)
{
    relay_all_safe();
    s_arm_verify_pending = false;
    s_arm_verify_channel = 0;
    s_arm_verify_start_ms = 0;
    send_nack(MSG_CMD_ARM, s_arm_verify_seq, nack_reason);
    status_update_trigger();
}

/* ── Public API ───────────────────────────────────────────────── */

rlc_state_t base_fsm_get_state(void)       { return s_state; }
uint8_t     base_fsm_get_armed_channel(void){ return s_armed_channel; }
uint8_t     base_fsm_get_firing_channel(void){ return s_firing_channel; }
uint8_t     base_fsm_get_error_flags(void)  { return s_error_flags; }
QueueHandle_t base_fsm_get_queue(void)      { return s_evt_queue; }
TaskHandle_t  base_fsm_get_task(void)       { return s_fsm_task; }

bool base_fsm_is_busy(void)
{
    rlc_state_t st = s_state;
    return (st == STATE_ARMED || st == STATE_PRE_FIRE ||
            st == STATE_FIRING || st == STATE_POST_FIRE);
}

void base_fsm_post_event(uint32_t event_type, bool armed)
{
    if (!s_evt_queue) return;
    rlc_fsm_event_t evt = {0};
    evt.type = event_type;
    evt.data.arm_state.armed = armed;
    (void)xQueueSend(s_evt_queue, &evt, 0);
}

int base_fsm_init(void)
{
    s_evt_queue = xQueueCreate(FSM_EVENT_QUEUE_LEN, sizeof(rlc_fsm_event_t));
    if (!s_evt_queue) {
        ESP_LOGE(TAG, "event queue alloc failed");
        return -1;
    }

    /* Queue registration with link manager is done by the application
     * after both link and FSM are initialised (avoids init race — M8). */

    ESP_LOGI(TAG, "base FSM initialised");
    return 0;
}

int base_fsm_start(void)
{
    if (xTaskCreatePinnedToCore(base_fsm_task, "bfsm_task", 8192,
                                NULL, 4, &s_fsm_task, 0) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        return -1;
    }
    return 0;
}

/* ── ACK/NACK Sending ─────────────────────────────────────────── */

static void send_ack(uint8_t msg_type, uint32_t seq_num, uint8_t channel)
{
    rlc_payload_cmd_ack_t p = {0};
    p.acked_msg_type = msg_type;
    p.acked_sequence_number = seq_num;
    p.channel = channel;
    uint32_t seq = rlc_link_next_seq();
    (void)rlc_link_send_cmd(MSG_CMD_ACK, seq, &p, sizeof(p));
    ESP_LOGI(TAG, "ACK sent: type=0x%02x ch=%u seq=%lu",
             msg_type, channel, (unsigned long)seq_num);
}

static void send_nack(uint8_t msg_type, uint32_t seq_num, uint8_t reason)
{
    rlc_payload_cmd_nack_t p = {0};
    p.nacked_msg_type = msg_type;
    p.nacked_sequence_number = seq_num;
    p.reason_code = reason;
    uint32_t seq = rlc_link_next_seq();
    (void)rlc_link_send_cmd(MSG_CMD_NACK, seq, &p, sizeof(p));
    ESP_LOGW(TAG, "NACK sent: type=0x%02x reason=0x%02x (%s)",
             msg_type, reason, rlc_nack_reason_str(reason));
}

/* ── Disarm Sequence (FSD §7.2.7) ────────────────────────────── */

static void do_disarm(void)
{
    relay_all_safe();
    siren_off();
    s_armed_channel = 0;
    s_firing_channel = 0;
    s_arm_time_ms = 0;
    s_prefire_start_ms = 0;
    s_last_fire_cmd_ms = 0;     /* J2: clear dead-man timestamp on disarm */
    s_link_lost_pending = false;
    rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
    status_update_trigger();
    ESP_LOGI(TAG, "DISARMED -> IDLE");
    s_state = STATE_IDLE;
}

static void do_enter_link_lost(void)
{
    relay_all_safe();
    siren_start_link_lost();
    s_armed_channel = 0;
    s_firing_channel = 0;
    s_arm_time_ms = 0;
    s_prefire_start_ms = 0;
    s_last_fire_cmd_ms = 0;     /* J2 */
    s_link_lost_pending = false;
    rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
    status_update_trigger();
    ESP_LOGI(TAG, "-> LINK_LOST");
    s_state = STATE_LINK_LOST;
}

static void do_enter_error(uint8_t err_flag)
{
    relay_all_safe();
    fire_timer_stop();          /* belt-and-braces: ensure no pulse outlives ERROR */
    siren_start_error();
    s_error_flags |= err_flag;
    s_armed_channel = 0;
    s_firing_channel = 0;
    s_arm_time_ms = 0;
    s_prefire_start_ms = 0;
    s_postfire_start_ms = 0;
    s_last_fire_cmd_ms = 0;     /* J2 */
    s_link_lost_pending = false;
    rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
    status_update_trigger();
    ESP_LOGE(TAG, "-> ERROR (flags=0x%02x)", s_error_flags);
    s_state = STATE_ERROR;
}

/* ── Guard: IDLE -> ARMED (FSD §7.2.2) ───────────────────────── */

/**
 * Evaluate arming guards and return the appropriate NACK reason,
 * or 0 if all guards pass.
 *
 * Guards 5 (integrity CRC), 6 (session token), and 7 (sequence number
 * anti-replay) are enforced by the link layer (rlc_link.c process_frame)
 * before commands reach this FSM queue. They are not re-verified here
 * because the link layer's validation is the authoritative gatekeeper:
 * commands arrive with CRC/session/seq already checked and stripped.
 * This architectural dependency is intentional (C2 documentation).
 */
static uint8_t guard_arm(const rlc_fsm_event_t *evt)
{
    uint8_t ch = evt->data.cmd.channel;

    /* Guard 3: Channel in range */
    if (ch < 1 || ch > NUM_CHANNELS) return NACK_INVALID_CHANNEL;

    /* Guard 4: No other channel armed */
    if (s_armed_channel != 0) return NACK_CHANNEL_ALREADY_ARMED;

    /* Guard 4b: Channel has the bug #18 hardware protection fitted.
     * Reuses NACK_INVALID_CHANNEL so the wire protocol is unchanged — the real
     * reason is logged here. Ordered after guard 4 so T-A05 (second channel
     * while armed) still sees NACK_CHANNEL_ALREADY_ARMED. */
    if (!CHANNEL_IS_PROTECTED(ch)) {
        ESP_LOGE(TAG, "ARM ch %u refused — no ADC clamp/snubber fitted "
                      "(protected mask 0x%02X), see bug #18",
                 ch, FIRE_PROTECTED_CHANNEL_MASK);
        return NACK_INVALID_CHANNEL;
    }

    /* Guard 1: Base key switch must be ON — key_sense HIGH */
    if (!key_sense_get_debounced()) return NACK_BASE_SWITCH_OFF;

    /* Guard 2: Continuity not OPEN */
    if (continuity_get_channel(ch) == CONT_OPEN) return NACK_NO_CONTINUITY;

    /* Guard 8: Battery above minimum */
    if (rlc_battery_get_voltage_mv() < BASE_VBAT_MIN_ARM_MV) return NACK_LOW_BATTERY;

    /* Guard 10: Link quality */
    if (!rlc_link_is_healthy()) return NACK_COMM_DEGRADED;

    return 0;  /* All guards pass */
}

/* ── Event Processing ─────────────────────────────────────────── */

static void process_event(const rlc_fsm_event_t *evt)
{
    /* J1: Arm relay contact-weld fault — hard safety fault from any state.
     * FSD §9.1 / §7.3.2: set ERR_RELAY_FAULT and transition to ERROR.
     * Already-ERROR is a no-op (intentionally unrecoverable).
     */
    if (evt->type == EVT_ARM_SENSE_FAULT) {
        if (s_state != STATE_ERROR) {
            ESP_LOGE(TAG, "ARM RELAY CONTACT WELD — entering ERROR");
            if (s_arm_verify_pending) {
                s_arm_verify_pending = false;
                s_arm_verify_channel = 0;
                s_arm_verify_start_ms = 0;
                send_nack(MSG_CMD_ARM, s_arm_verify_seq, NACK_ARM_SENSE_FAULT);
            }
            fire_timer_stop();
            do_enter_error(ERR_RELAY_FAULT);
        }
        return;
    }

    switch (s_state) {

    /* ─── BOOT ─────────────────────────────────────────────── */
    case STATE_BOOT:
        if (evt->type == EVT_LINK_ESTABLISHED) {
            ESP_LOGI(TAG, "BOOT -> IDLE (link established)");
            s_state = STATE_IDLE;
            rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
            status_update_trigger();
        }
        break;

    /* ─── IDLE ─────────────────────────────────────────────── */
    case STATE_IDLE:
        if (evt->type == EVT_CMD_ARM) {
            uint8_t nack_reason = guard_arm(evt);
            if (nack_reason != 0) {
                send_nack(MSG_CMD_ARM, evt->data.cmd.seq_number, nack_reason);
                return;
            }

            uint8_t ch = evt->data.cmd.channel;

            /* Energise arm relay */
            arm_relay_set(true);

            /* M1: Non-blocking arm sense verification.
             * Check immediately — sense may already be HIGH (relay fast). */
            if (arm_sense_get_debounced()) {
                /* Sense already HIGH — proceed to ARMED immediately */
                s_armed_channel = ch;
                s_arm_time_ms = now_ms();
                siren_start_pulse();
                rlc_rgb_led_set_pattern(LED_PATTERN_ARMED);
                send_ack(MSG_CMD_ARM, evt->data.cmd.seq_number, ch);
                status_update_trigger();
                ESP_LOGI(TAG, "IDLE -> ARMED (ch %u)", ch);
                s_state = STATE_ARMED;
            } else {
                /* Start non-blocking verify: wait for EVT_ARM_SENSE_CHANGED or timeout.
                 * FSM stays in STATE_IDLE but with s_arm_verify_pending set.
                 * Safety events (link loss, battery critical, CEASE_FIRE) can still
                 * be processed during the 200 ms window. */
                s_arm_verify_pending = true;
                s_arm_verify_channel = ch;
                s_arm_verify_seq = evt->data.cmd.seq_number;
                s_arm_verify_start_ms = now_ms();
                ESP_LOGI(TAG, "arm verify started (ch %u, waiting for sense HIGH)", ch);
            }

        } else if (evt->type == EVT_ARM_SENSE_CHANGED) {
            /* M1: Complete arm verification when sense goes HIGH */
            if (s_arm_verify_pending && evt->data.arm_state.armed) {
                uint8_t ch = s_arm_verify_channel;
                s_arm_verify_pending = false;
                s_armed_channel = ch;
                s_arm_time_ms = now_ms();
                s_arm_verify_channel = 0;
                s_arm_verify_start_ms = 0;
                siren_start_pulse();
                rlc_rgb_led_set_pattern(LED_PATTERN_ARMED);
                send_ack(MSG_CMD_ARM, s_arm_verify_seq, ch);
                status_update_trigger();
                ESP_LOGI(TAG, "IDLE -> ARMED (ch %u, sense verified)", ch);
                s_state = STATE_ARMED;
            }

        } else if (evt->type == EVT_CMD_DISARM) {
            /* Idempotent ACK (FSD §7.2.7) */
            send_ack(MSG_CMD_DISARM, evt->data.cmd.seq_number,
                     evt->data.cmd.channel);
        } else if (evt->type == EVT_CMD_CEASE_FIRE) {
            /* Idempotent ACK (FSD §7.2.7) */
            if (s_arm_verify_pending) {
                abort_arm_verify(NACK_WRONG_STATE);
            }
            send_ack(MSG_CMD_CEASE_FIRE, evt->data.cmd.seq_number, 0);
        } else if (evt->type == EVT_CMD_FIRE) {
            if (s_arm_verify_pending) {
                abort_arm_verify(NACK_WRONG_STATE);
            }
            send_nack(MSG_CMD_FIRE, evt->data.cmd.seq_number, NACK_WRONG_STATE);
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            if (s_arm_verify_pending) {
                abort_arm_verify(NACK_LOW_BATTERY);
            }
            do_enter_error(ERR_VBAT_CRITICAL);
        } else if (evt->type == EVT_LINK_LOST) {
            if (s_arm_verify_pending) {
                relay_all_safe();
                s_arm_verify_pending = false;
                s_arm_verify_channel = 0;
                s_arm_verify_start_ms = 0;
            }
            do_enter_link_lost();
        }
        break;

    /* ─── ARMED ────────────────────────────────────────────── */
    case STATE_ARMED:
        if (evt->type == EVT_CMD_FIRE) {
            /* Guard: channel must match armed channel (FSD §7.2.3) */
            if (evt->data.cmd.channel != s_armed_channel) {
                send_nack(MSG_CMD_FIRE, evt->data.cmd.seq_number, NACK_WRONG_STATE);
                return;
            }
            /* Guard: arm sense still HIGH */
            if (!arm_sense_get_debounced()) {
                send_nack(MSG_CMD_FIRE, evt->data.cmd.seq_number, NACK_BASE_SWITCH_OFF);
                do_disarm();
                return;
            }

            /* Enter PRE_FIRE */
            s_prefire_start_ms = now_ms();
            s_arm_time_ms = 0;  /* Cancel arm timeout */
            /* J2: seed dead-man timestamp from the triggering CMD_FIRE so the
             * pre-fire countdown's freshness check has a valid baseline. */
            s_last_fire_cmd_ms = evt->data.cmd.received_ms;
            siren_start_continuous();
            rlc_rgb_led_set_pattern(LED_PATTERN_PRE_FIRE);
            send_ack(MSG_CMD_FIRE, evt->data.cmd.seq_number, s_armed_channel);
            status_update_trigger();
            ESP_LOGI(TAG, "ARMED -> PRE_FIRE (ch %u)", s_armed_channel);
            s_state = STATE_PRE_FIRE;

        } else if (evt->type == EVT_CMD_DISARM) {
            send_ack(MSG_CMD_DISARM, evt->data.cmd.seq_number,
                     evt->data.cmd.channel);
            do_disarm();
        } else if (evt->type == EVT_CMD_CEASE_FIRE) {
            send_ack(MSG_CMD_CEASE_FIRE, evt->data.cmd.seq_number, 0);
            do_disarm();
        } else if (evt->type == EVT_CMD_ARM) {
            /* ARM for different channel while already armed */
            send_nack(MSG_CMD_ARM, evt->data.cmd.seq_number, NACK_CHANNEL_ALREADY_ARMED);
        } else if (evt->type == EVT_ARM_SENSE_CHANGED && !evt->data.arm_state.armed) {
            /* Arm relay feedback lost — relay dropout or fault */
            ESP_LOGW(TAG, "Arm sense LOW during ARMED — relay feedback lost");
            do_disarm();
        } else if (evt->type == EVT_KEY_SWITCH_CHANGED && !evt->data.arm_state.armed) {
            /* Key switch turned OFF */
            ESP_LOGW(TAG, "Key switch OFF during ARMED — disarm");
            do_disarm();
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            do_enter_error(ERR_VBAT_CRITICAL);
        } else if (evt->type == EVT_LINK_LOST) {
            do_enter_link_lost();
        }
        break;

    /* ─── PRE_FIRE ─────────────────────────────────────────── */
    case STATE_PRE_FIRE:
        if (evt->type == EVT_CMD_CEASE_FIRE) {
            send_ack(MSG_CMD_CEASE_FIRE, evt->data.cmd.seq_number, 0);
            do_disarm();
        } else if (evt->type == EVT_CMD_FIRE) {
            /* C3/M3: Update dead-man timestamp from wire-receive time.
             * Only update for the correct channel (wrong-channel CMD_FIRE
             * silently discarded per FSD §7.2.3 does not refresh dead-man). */
            if (evt->data.cmd.channel == s_armed_channel) {
                s_last_fire_cmd_ms = evt->data.cmd.received_ms;
            }
        } else if (evt->type == EVT_ARM_SENSE_CHANGED && !evt->data.arm_state.armed) {
            ESP_LOGW(TAG, "Arm sense LOW during PRE_FIRE — abort");
            do_disarm();
        } else if (evt->type == EVT_KEY_SWITCH_CHANGED && !evt->data.arm_state.armed) {
            ESP_LOGW(TAG, "Key switch OFF during PRE_FIRE — abort");
            do_disarm();
        } else if (evt->type == EVT_CMD_DISARM) {
            send_ack(MSG_CMD_DISARM, evt->data.cmd.seq_number,
                     evt->data.cmd.channel);
            do_disarm();
        } else if (evt->type == EVT_CMD_ARM) {
            send_nack(MSG_CMD_ARM, evt->data.cmd.seq_number, NACK_WRONG_STATE);
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            do_enter_error(ERR_VBAT_CRITICAL);
        } else if (evt->type == EVT_LINK_LOST) {
            do_enter_link_lost();
        }
        break;

    /* ─── FIRING ───────────────────────────────────────────── */
    case STATE_FIRING:
        if (evt->type == EVT_FIRE_PULSE_DONE) {
            /* Fire pulse completed (signalled by GPTimer ISR) */
            relay_all_safe();
            siren_off();
            s_firing_channel = 0;
            s_armed_channel = 0;

            /* C1: If link was lost during pulse, transition to LINK_LOST now */
            if (s_link_lost_pending) {
                s_link_lost_pending = false;
                siren_start_link_lost();
                rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
                status_update_trigger();
                ESP_LOGI(TAG, "FIRING -> LINK_LOST (pulse completed, link was lost)");
                s_state = STATE_LINK_LOST;
            } else {
                s_postfire_start_ms = now_ms();
                rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
                status_update_trigger();
                ESP_LOGI(TAG, "FIRING -> POST_FIRE");
                s_state = STATE_POST_FIRE;
            }

        } else if (evt->type == EVT_CMD_CEASE_FIRE) {
            fire_timer_stop();
            relay_all_safe();
            siren_off();
            s_firing_channel = 0;
            s_armed_channel = 0;
            s_link_lost_pending = false;
            send_ack(MSG_CMD_CEASE_FIRE, evt->data.cmd.seq_number, 0);
            rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
            status_update_trigger();
            ESP_LOGI(TAG, "FIRING -> IDLE (CEASE_FIRE)");
            s_state = STATE_IDLE;

        } else if (evt->type == EVT_ARM_SENSE_CHANGED && !evt->data.arm_state.armed) {
            /* Arm relay feedback lost during FIRING — cease fire */
            fire_timer_stop();
            relay_all_safe();
            siren_off();
            s_firing_channel = 0;
            s_armed_channel = 0;
            s_link_lost_pending = false;
            rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
            status_update_trigger();
            ESP_LOGW(TAG, "FIRING -> IDLE (arm sense lost)");
            s_state = STATE_IDLE;

        } else if (evt->type == EVT_KEY_SWITCH_CHANGED && !evt->data.arm_state.armed) {
            /* Key switch OFF during FIRING — same as CEASE_FIRE (FSD §7.2.5) */
            fire_timer_stop();
            relay_all_safe();
            siren_off();
            s_firing_channel = 0;
            s_armed_channel = 0;
            s_link_lost_pending = false;
            rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
            status_update_trigger();
            ESP_LOGW(TAG, "FIRING -> IDLE (key switch OFF)");
            s_state = STATE_IDLE;

        } else if (evt->type == EVT_CMD_FIRE) {
            /* Repeated CMD_FIRE during FIRING — silently discard (FSD §7.2.3) */
        } else if (evt->type == EVT_CMD_ARM) {
            send_nack(MSG_CMD_ARM, evt->data.cmd.seq_number, NACK_WRONG_STATE);
        } else if (evt->type == EVT_CMD_DISARM) {
            fire_timer_stop();
            relay_all_safe();
            siren_off();
            s_firing_channel = 0;
            s_armed_channel = 0;
            s_link_lost_pending = false;
            send_ack(MSG_CMD_DISARM, evt->data.cmd.seq_number, 0);
            rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
            status_update_trigger();
            ESP_LOGI(TAG, "FIRING -> IDLE (DISARM)");
            s_state = STATE_IDLE;

        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            /* Complete the fire pulse, then enter ERROR (FSD §7.2.5) */
            s_error_flags |= ERR_VBAT_CRITICAL;
            /* Don't abort — fire timer will expire and transition to POST_FIRE,
             * then we'll detect ERROR in POST_FIRE check. Mark for error entry. */
            ESP_LOGE(TAG, "Battery critical during FIRING — will error after pulse");

        /* C1: Link lost during FIRING — consult COMPLETE_PULSE_ON_LINK_LOSS config */
        } else if (evt->type == EVT_LINK_LOST) {
            if (COMPLETE_PULSE_ON_LINK_LOSS) {
                /* Let the fire pulse complete, then transition to LINK_LOST */
                s_link_lost_pending = true;
                ESP_LOGW(TAG, "Link lost during FIRING — completing pulse before LINK_LOST");
            } else {
                /* Immediate abort — cut power and enter LINK_LOST */
                fire_timer_stop();
                relay_all_safe();
                siren_off();
                s_firing_channel = 0;
                s_armed_channel = 0;
                siren_start_link_lost();
                rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
                status_update_trigger();
                ESP_LOGW(TAG, "FIRING -> LINK_LOST (immediate abort, link lost)");
                s_state = STATE_LINK_LOST;
            }
        }
        break;

    /* ─── POST_FIRE ────────────────────────────────────────── */
    case STATE_POST_FIRE:
        if (evt->type == EVT_CMD_ARM) {
            send_nack(MSG_CMD_ARM, evt->data.cmd.seq_number, NACK_WRONG_STATE);
        } else if (evt->type == EVT_CMD_CEASE_FIRE) {
            /* J3: Idempotent ACK during cooldown (FSD §7.2.7) — system is
             * already safe; ACK so the remote doesn't time out. */
            send_ack(MSG_CMD_CEASE_FIRE, evt->data.cmd.seq_number, 0);
        } else if (evt->type == EVT_CMD_DISARM) {
            /* J3: Idempotent ACK during cooldown (FSD §7.2.7). */
            send_ack(MSG_CMD_DISARM, evt->data.cmd.seq_number,
                     evt->data.cmd.channel);
        } else if (evt->type == EVT_LINK_LOST) {
            do_enter_link_lost();
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            do_enter_error(ERR_VBAT_CRITICAL);
        }
        /* If battery critical was flagged during FIRING, enter ERROR now */
        if (s_error_flags & ERR_VBAT_CRITICAL) {
            do_enter_error(0);  /* Flag already set */
        }
        break;

    /* ─── LINK_LOST ────────────────────────────────────────── */
    case STATE_LINK_LOST:
        if (evt->type == EVT_LINK_RECOVERED) {
            siren_off();  /* Silence siren immediately (FSD §7.2.8) */
            rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
            ESP_LOGI(TAG, "LINK_LOST -> IDLE (recovered)");
            s_state = STATE_IDLE;
        }
        break;

    /* ─── ERROR ────────────────────────────────────────────── */
    case STATE_ERROR:
        /* Intentionally unrecoverable — requires power cycle */
        break;

    default:
        break;
    }
}

/* ── Timer Checks (called each poll cycle) ──────────────────── */

static void check_timers(void)
{
    int64_t t = now_ms();

    /* M1: Arm sense verification timeout */
    if (s_arm_verify_pending && s_arm_verify_start_ms > 0) {
        if ((t - s_arm_verify_start_ms) >= 200) {
            ESP_LOGW(TAG, "arm sense verify timeout (200 ms) — NACK ARM_SENSE_FAULT");
            abort_arm_verify(NACK_ARM_SENSE_FAULT);
        }
    }

    if (s_state == STATE_ARMED && s_arm_time_ms > 0) {
        /* Arm timeout (FSD §7.2.7) */
        if ((t - s_arm_time_ms) >= ARM_TIMEOUT_MS) {
            ESP_LOGW(TAG, "ARM TIMEOUT (%lld ms) — auto-disarm", (t - s_arm_time_ms));
            do_disarm();
        }
    }

    if (s_state == STATE_PRE_FIRE && s_prefire_start_ms > 0) {
        /* Pre-fire countdown expired */
        if ((t - s_prefire_start_ms) >= PRE_FIRE_DELAY_MS) {
            /* Guard: dead-man check — CMD_FIRE within last 500ms (FSD §7.2.4).
             * C3/M3: uses local s_last_fire_cmd_ms populated from wire-receive time. */
            bool dead_man_ok = (s_last_fire_cmd_ms > 0) &&
                               ((t - s_last_fire_cmd_ms) < FIRE_AUTHORIZATION_TIMEOUT_MS);

            if (!dead_man_ok) {
                ESP_LOGW(TAG, "PRE_FIRE dead-man timeout — abort");
                do_disarm();
                return;
            }

            /* Guard: link health (FSD §7.2.4) */
            if (!rlc_link_is_healthy()) {
                ESP_LOGW(TAG, "PRE_FIRE comm degraded — abort");
                do_disarm();
                return;
            }

            /* Guard: key switch still ON (FSD §7.2.4) */
            if (!key_sense_get_debounced()) {
                ESP_LOGW(TAG, "PRE_FIRE key switch OFF — abort");
                do_disarm();
                return;
            }

            /* Guard: arm sense still HIGH (relay integrity, FSD §7.2.4) */
            if (!arm_sense_get_debounced()) {
                ESP_LOGW(TAG, "PRE_FIRE arm sense lost — abort");
                do_disarm();
                return;
            }

            /* Guard: PONG freshness (FSD §7.2.4) — implicitly checked by
             * rlc_link_is_healthy() which tracks the ping success rate. */

            /* Transition to FIRING */
            s_firing_channel = s_armed_channel;
            relay_fire_set(s_armed_channel, true);
            fire_timer_start(FIRE_PULSE_DURATION_MS, s_armed_channel, s_fsm_task);
            rlc_rgb_led_set_pattern(LED_PATTERN_FIRING);
            status_update_trigger();
            ESP_LOGI(TAG, "PRE_FIRE -> FIRING (ch %u)", s_armed_channel);
            s_state = STATE_FIRING;
        }
    }

    if (s_state == STATE_POST_FIRE && s_postfire_start_ms > 0) {
        /* M7: Check error flags before transitioning to IDLE.
         * If battery critical was flagged during FIRING, enter ERROR. */
        if (s_error_flags & ERR_VBAT_CRITICAL) {
            s_postfire_start_ms = 0;
            do_enter_error(0);  /* Flag already set */
            return;
        }
        /* Post-fire cooldown (FSD §7.2.7) */
        if ((t - s_postfire_start_ms) >= POST_FIRE_COOLDOWN_MS) {
            s_postfire_start_ms = 0;
            rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
            ESP_LOGI(TAG, "POST_FIRE -> IDLE");
            s_state = STATE_IDLE;
        }
    }
}

/* ── FSM Task Main Loop ──────────────────────────────────────── */

static void base_fsm_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    fire_timer_init();

    rlc_fsm_event_t evt;
    ESP_LOGI(TAG, "base FSM task started");

    while (1) {
        /* Block on event queue with 50ms timeout for timer checks */
        bool got_event = (xQueueReceive(s_evt_queue, &evt, pdMS_TO_TICKS(50)) == pdTRUE);

        if (got_event) {
            process_event(&evt);
        }

        /* Check for fire pulse notification (from GPTimer ISR) */
        uint32_t notify_val = 0;
        if (xTaskNotifyWait(0, FIRE_NOTIFY_BIT, &notify_val, 0) == pdTRUE) {
            if (notify_val & FIRE_NOTIFY_BIT) {
                rlc_fsm_event_t fire_evt = {0};
                fire_evt.type = EVT_FIRE_PULSE_DONE;
                process_event(&fire_evt);
            }
        }

        /* Check software timers */
        check_timers();

        esp_task_wdt_reset();
    }
}
