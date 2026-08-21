/**
 * RLC Remote Unit State Machine Implementation
 *
 * Full FSM per FSD §8. Handles arming via encoder long-press, fire via
 * fresh button press, repeated CMD_FIRE transmission, ACK/NACK handling,
 * and all safety interlocks.
 *
 * Single-task-owner: remote_fsm_task owns all FSM state.
 */

#include "rlc_remote_fsm.h"
#include "rlc_fsm_events.h"
#include "rlc_link.h"
#include "rlc_message.h"
#include "rlc_protocol.h"
#include "rlc_encoder.h"
#include "rlc_fire_button.h"
#include "rlc_arm_switch.h"
#include "rlc_buzzer.h"
#include "rlc_display.h"
#include "rlc_battery.h"
#include "rlc_rgb_led.h"
#include "rlc_config.h"
#include "rlc_version.h"
#include "rlc_watchdog.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "rlc_rfsm";

/* ── Private State ───────────────────────────────────────────── */

static volatile rlc_state_t s_state = STATE_BOOT;
static volatile uint8_t     s_selected_channel = 1;
static volatile uint8_t     s_armed_channel = 0;
static volatile bool        s_fire_repeat_active = false;

/* Cached STATUS_UPDATE from base */
static rlc_payload_status_update_t s_last_status;
static int64_t  s_last_status_rx_ms = 0;

/* Guards s_last_status/s_last_status_rx_ms: written here on the FSM task,
 * read by the display task (remote_fsm_get_status). */
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;

/* Software timer timestamps */
static int64_t  s_prefire_start_ms = 0;

/* Pending command tracking — what wait_for_ack() correlates incoming
 * ACK/NACKs against (4.8).
 *
 * 5.6: these are written by send_cmd_arm()/send_cmd_fire(), and
 * send_cmd_fire() is ALSO called from cmd_fire_repeat_task_fn on its own
 * task. That is safe only because the two never overlap: the repeat task
 * runs solely while s_fire_repeat_active is true, which the FSM task sets
 * after wait_for_ack() has already returned, and clears before it starts any
 * new ACK wait. If a future change starts a wait while repeats are running,
 * these must move under a lock or become per-attempt locals. */
static volatile uint32_t s_pending_cmd_seq = 0;
static volatile uint8_t  s_pending_cmd_type = 0;

/* Task and queue handles */
static QueueHandle_t s_evt_queue = NULL;
static TaskHandle_t  s_fsm_task = NULL;
static TaskHandle_t  s_fire_repeat_task = NULL;

/* Forward declarations */
static void remote_fsm_task(void *arg);
static void cmd_fire_repeat_task_fn(void *arg);
static int  send_cmd_arm(uint8_t channel);
static int  send_cmd_fire(uint8_t channel);
static int  send_cmd_disarm(uint8_t channel);
static int  send_cmd_cease_fire(void);
static void do_enter_idle(void);
static void do_disarm_and_idle(void);
static void do_enter_link_lost(void);
static void do_enter_error(void);

/* ── Helpers ─────────────────────────────────────────────────── */

static inline int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* Cache the latest STATUS_UPDATE from the base (FSM task only). */
static void cache_status(const rlc_payload_status_update_t *st)
{
    int64_t t = now_ms();
    portENTER_CRITICAL(&s_status_lock);
    memcpy(&s_last_status, st, sizeof(s_last_status));
    s_last_status_rx_ms = t;
    portEXIT_CRITICAL(&s_status_lock);
}

static bool is_status_fresh(void)
{
    return (s_last_status_rx_ms > 0) &&
           ((now_ms() - s_last_status_rx_ms) < 2 * STATUS_UPDATE_INTERVAL_MS);
}

/* R2: Multi-arm detection (FSD §6, line 1201).
 * Returns true if more than one bit is set in the lower 8 bits of the bitmask,
 * which would indicate a base firmware bug arming multiple channels. */
static inline bool is_multi_armed(uint16_t armed_mask)
{
    return __builtin_popcount((unsigned)(armed_mask & 0x00FFu)) > 1;
}

/* R2: Multi-arm reaction — broadcast disarm and enter ERROR state.
 * A multi-arm ACK indicates base firmware instability or a serious bug.
 * Halt the system and require power cycle. FSD §6 line 1201 mandates
 * transition to IDLE, but ERROR is more conservative — multi-arm should
 * never happen and indicates a critical failure. */
static void handle_multi_arm_violation(uint16_t armed_mask)
{
    ESP_LOGE(TAG, "MULTI-ARM DETECTED (mask=0x%04x) — broadcasting DISARM, entering ERROR",
             armed_mask);
    s_fire_repeat_active = false;
    send_cmd_disarm(0xFF);  /* 0xFF = all channels (FSD §6 line 1201) */
    s_armed_channel = 0;
    s_prefire_start_ms = 0;
    rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
    buzzer_play(BUZZER_ALARM_CRITICAL);
    display_error("MULTI-ARM DETECTED");
    s_state = STATE_ERROR;
}

/* ── Public API ──────────────────────────────────────────────── */

rlc_state_t remote_fsm_get_state(void)       { return s_state; }
uint8_t     remote_fsm_get_selected_channel(void){ return s_selected_channel; }
uint8_t     remote_fsm_get_armed_channel(void){ return s_armed_channel; }
QueueHandle_t remote_fsm_get_queue(void)      { return s_evt_queue; }
TaskHandle_t  remote_fsm_get_task(void)       { return s_fsm_task; }

bool remote_fsm_get_status(rlc_payload_status_update_t *out)
{
    if (!out) return false;
    int64_t rx_ms;
    portENTER_CRITICAL(&s_status_lock);
    memcpy(out, &s_last_status, sizeof(*out));
    rx_ms = s_last_status_rx_ms;
    portEXIT_CRITICAL(&s_status_lock);

    return (rx_ms > 0) && ((now_ms() - rx_ms) < 2 * STATUS_UPDATE_INTERVAL_MS);
}

uint32_t remote_fsm_get_prefire_remaining_ms(void)
{
    if (s_state != STATE_PRE_FIRE || s_prefire_start_ms == 0) return 0;
    int64_t elapsed = now_ms() - s_prefire_start_ms;
    if (elapsed >= PRE_FIRE_DELAY_MS) return 0;
    return (uint32_t)(PRE_FIRE_DELAY_MS - elapsed);
}

int remote_fsm_init(void)
{
    s_evt_queue = xQueueCreate(FSM_EVENT_QUEUE_LEN, sizeof(rlc_fsm_event_t));
    if (!s_evt_queue) {
        ESP_LOGE(TAG, "event queue alloc failed");
        return -1;
    }

    /* M8: Queue registration deferred to application to avoid init race. */

    ESP_LOGI(TAG, "remote FSM initialised");
    return 0;
}

int remote_fsm_start(void)
{
    if (xTaskCreatePinnedToCore(remote_fsm_task, "rfsm_task", 8192,
                                NULL, 4, &s_fsm_task, 0) != pdPASS) {
        ESP_LOGE(TAG, "FSM task create failed");
        return -1;
    }

    if (xTaskCreatePinnedToCore(cmd_fire_repeat_task_fn, "fire_rep", 2048,
                                NULL, 4, &s_fire_repeat_task, 0) != pdPASS) {
        ESP_LOGE(TAG, "fire repeat task create failed");
        return -1;
    }

    return 0;
}

/* ── Command Sending ─────────────────────────────────────────── */

static int send_cmd_arm(uint8_t channel)
{
    uint32_t token = rlc_link_get_session_token();
    uint32_t seq = rlc_link_next_seq();
    if (seq == 0) return -1;

    rlc_payload_cmd_arm_t p = {0};
    p.channel = channel;

    /* Compute integrity CRC over a temporary header + payload + key */
    rlc_msg_header_t hdr = {
        .protocol_version = RLC_PROTOCOL_VERSION,
        .msg_type = MSG_CMD_ARM,
        .payload_length = sizeof(p),
        .sequence_number = seq,
        .session_token = token,
    };
    /* CRC over header + payload_after_crc_field + key */
    /* In our struct, integrity_crc is the first field, so "payload after CRC"
     * is the channel byte. We compute: CRC(header || channel_byte || key) */
    p.integrity_crc = rlc_compute_integrity_crc(
        &hdr, sizeof(hdr), &p.channel, sizeof(p.channel));

    s_pending_cmd_seq = seq;
    s_pending_cmd_type = MSG_CMD_ARM;

    return rlc_link_send_cmd(MSG_CMD_ARM, seq, &p, sizeof(p));
}

static int send_cmd_fire(uint8_t channel)
{
    uint32_t token = rlc_link_get_session_token();
    uint32_t seq = rlc_link_next_seq();
    if (seq == 0) return -1;

    rlc_payload_cmd_fire_t p = {0};
    p.channel = channel;

    rlc_msg_header_t hdr = {
        .protocol_version = RLC_PROTOCOL_VERSION,
        .msg_type = MSG_CMD_FIRE,
        .payload_length = sizeof(p),
        .sequence_number = seq,
        .session_token = token,
    };
    p.integrity_crc = rlc_compute_integrity_crc(
        &hdr, sizeof(hdr), &p.channel, sizeof(p.channel));

    s_pending_cmd_seq = seq;
    s_pending_cmd_type = MSG_CMD_FIRE;

    return rlc_link_send_cmd(MSG_CMD_FIRE, seq, &p, sizeof(p));
}

static int send_cmd_disarm(uint8_t channel)
{
    uint32_t token = rlc_link_get_session_token();
    uint32_t seq = rlc_link_next_seq();
    if (seq == 0) return -1;

    rlc_payload_cmd_disarm_t p = {0};
    p.channel = channel;

    rlc_msg_header_t hdr = {
        .protocol_version = RLC_PROTOCOL_VERSION,
        .msg_type = MSG_CMD_DISARM,
        .payload_length = sizeof(p),
        .sequence_number = seq,
        .session_token = token,
    };
    p.integrity_crc = rlc_compute_integrity_crc(
        &hdr, sizeof(hdr), &p.channel, sizeof(p.channel));

    return rlc_link_send_cmd(MSG_CMD_DISARM, seq, &p, sizeof(p));
}

static int send_cmd_cease_fire(void)
{
    uint32_t token = rlc_link_get_session_token();
    uint32_t seq = rlc_link_next_seq();
    if (seq == 0) return -1;

    rlc_payload_cmd_cease_fire_t p = {0};

    rlc_msg_header_t hdr = {
        .protocol_version = RLC_PROTOCOL_VERSION,
        .msg_type = MSG_CMD_CEASE_FIRE,
        .payload_length = sizeof(p),
        .sequence_number = seq,
        .session_token = token,
    };
    p.integrity_crc = rlc_compute_integrity_crc(
        &hdr, sizeof(hdr), NULL, 0);

    return rlc_link_send_cmd(MSG_CMD_CEASE_FIRE, seq, &p, sizeof(p));
}

/* ── State Transition Helpers ────────────────────────────────── */

static void do_enter_idle(void)
{
    s_armed_channel = 0;
    s_fire_repeat_active = false;
    s_prefire_start_ms = 0;
    /* 4.9: re-sync the tracked selection with the encoder — after
     * LINKING/LINK_LOST the cached s_selected_channel could be stale,
     * making the display highlight a different channel than a long-press
     * would arm. */
    s_selected_channel = encoder_get_channel();
    rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
    ESP_LOGI(TAG, "-> IDLE");
    s_state = STATE_IDLE;
}

static void do_disarm_and_idle(void)
{
    s_fire_repeat_active = false;
    if (s_armed_channel > 0) {
        send_cmd_disarm(s_armed_channel);
    }
    s_armed_channel = 0;
    s_prefire_start_ms = 0;
    rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
    buzzer_play(BUZZER_BEEP_LONG);
    ESP_LOGI(TAG, "DISARMED -> IDLE");
    s_state = STATE_IDLE;
}

static void do_enter_link_lost(void)
{
    s_fire_repeat_active = false;
    s_armed_channel = 0;
    s_prefire_start_ms = 0;
    buzzer_play(BUZZER_ALARM_LINK_LOST);
    rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
    ESP_LOGI(TAG, "-> LINK_LOST");
    s_state = STATE_LINK_LOST;
}

/* Latch a description for the ERROR screen (FSD §10.2.6) unless a more
 * specific one was already set by the caller. */
static void do_enter_error_text(const char *text)
{
    display_error(text);
    do_enter_error();
}

static void do_enter_error(void)
{
    s_fire_repeat_active = false;
    s_armed_channel = 0;
    buzzer_play(BUZZER_ALARM_CRITICAL);
    rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
    ESP_LOGE(TAG, "-> ERROR");
    s_state = STATE_ERROR;
}

/* ── ACK Wait Helper ─────────────────────────────────────────── */

/**
 * Wait for ACK/NACK with timeout. Returns:
 *  1 = ACK received (channel verified)
 *  0 = timeout (caller may retry)
 * -1 = NACK received (reason in *nack_reason)
 * -2 = channel mismatch in ACK
 * -3 = state already transitioned by an inline-handled critical event
 *      (LINK_LOST or BATTERY_CRITICAL). Caller MUST NOT touch state. (R1)
 * -4 = interrupted by local operator input (arm switch off, fire button
 *      release, or encoder activity). Terminal for the pending command —
 *      callers must NOT retry (2.4: the retry loop's condition is
 *      `result == 0`, so -4 naturally falls out of it).
 */
#define WAIT_FOR_ACK_STATE_HANDLED (-3)
#define WAIT_FOR_ACK_INTERRUPTED   (-4)

static int wait_for_ack(uint8_t expected_channel, uint32_t timeout_ms,
                        uint8_t *nack_reason)
{
    int64_t deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        rlc_fsm_event_t evt;
        if (xQueueReceive(s_evt_queue, &evt, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (evt.type == EVT_CMD_ACK) {
                /* 4.8: correlate to the pending command (acked type + seq).
                 * A stale ACK from an earlier ARM must not satisfy a FIRE
                 * wait — ignore it and keep waiting. */
                if (evt.data.ack.acked_msg_type != s_pending_cmd_type ||
                    evt.data.ack.acked_seq_number != s_pending_cmd_seq) {
                    continue;
                }
                if (evt.data.ack.channel != expected_channel) {
                    return -2;  /* Channel mismatch */
                }
                return 1;  /* ACK OK */
            } else if (evt.type == EVT_CMD_NACK) {
                /* 4.8: same correlation — a stale NACK must not spuriously
                 * abort (and previously disarm) the current attempt. */
                if (evt.data.nack.nacked_msg_type != s_pending_cmd_type ||
                    evt.data.nack.nacked_seq_number != s_pending_cmd_seq) {
                    continue;
                }
                if (nack_reason) *nack_reason = evt.data.nack.reason_code;
                return -1;  /* NACK */
            } else {
                /* Process other events inline during wait */
                if (evt.type == EVT_ARM_SWITCH_CHANGED && !evt.data.arm_state.armed) {
                    return WAIT_FOR_ACK_INTERRUPTED;  /* Arm switch off */
                }
                if (evt.type == EVT_FIRE_BUTTON_RELEASED) {
                    return WAIT_FOR_ACK_INTERRUPTED;  /* Button released */
                }
                if (evt.type == EVT_ENCODER_ROTATE ||
                    evt.type == EVT_ENCODER_SHORT_PRESS ||
                    evt.type == EVT_ENCODER_LONG_PRESS) {
                    return WAIT_FOR_ACK_INTERRUPTED;  /* Encoder activity */
                }
                if (evt.type == EVT_LINK_LOST) {
                    do_enter_link_lost();
                    return WAIT_FOR_ACK_STATE_HANDLED;  /* R1 */
                }
                /* M5: Preserve critical events instead of silently discarding. */
                if (evt.type == EVT_STATUS_UPDATE) {
                    cache_status(&evt.data.status_update.status);
                }
                if (evt.type == EVT_BATTERY_CRITICAL) {
                    do_enter_error_text("REMOTE BATTERY CRITICAL");
                    return WAIT_FOR_ACK_STATE_HANDLED;  /* R1 */
                }
            }
        }
    }
    return 0;  /* Timeout */
}

/* ── Event Processing ─────────────────────────────────────────── */

static void process_event(const rlc_fsm_event_t *evt)
{
    switch (s_state) {

    /* ─── BOOT ─────────────────────────────────────────────── */
    case STATE_BOOT:
        /* Should not reach here — BOOT→LINKING happens at task start. */
        break;

    /* ─── LINKING ─────────────────────────────────────────── */
    case STATE_LINKING:
        if (evt->type == EVT_LINK_ESTABLISHED) {
            ESP_LOGI(TAG, "LINKING -> IDLE (link established)");
            do_enter_idle();
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            ESP_LOGW(TAG, "BATTERY_CRITICAL during LINKING -> ERROR");
            do_enter_error_text("REMOTE BATTERY CRITICAL");
        } else if (evt->type == EVT_LINK_LOST) {
            /* m1: a LINK_REQUEST round that fails after the link had briefly
             * come up posts EVT_LINK_LOST while we are still in LINKING.
             * Adopt it so the alarm and the display agree with the link
             * manager rather than sitting on "Connecting..." silently. */
            do_enter_link_lost();
        }
        break;

    /* ─── IDLE ─────────────────────────────────────────────── */
    case STATE_IDLE:
        if (evt->type == EVT_ENCODER_LONG_PRESS) {
            /* Attempt to ARM (FSD §8.2.3) */
            uint8_t ch = encoder_get_channel();

            /* Guard 1: Arm switch must be ON */
            if (!arm_switch_is_armed()) {
                ESP_LOGI(TAG, "ARM rejected: arm switch OFF");
                display_toast("TURN ARM KEY FIRST");
                break;
            }

            /* Guard 2: Remote battery OK */
            if (rlc_battery_get_voltage_mv() < REMOTE_VBAT_MIN_ARM_MV) {
                ESP_LOGW(TAG, "ARM rejected: remote battery low (%u mv)",
                         rlc_battery_get_voltage_mv());
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("REMOTE BATTERY LOW");
                break;
            }

            /* Guard 3: STATUS_UPDATE fresh */
            if (!is_status_fresh()) {
                ESP_LOGW(TAG, "ARM rejected: stale STATUS_UPDATE");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("NO BASE STATUS DATA");
                break;
            }

            /* Guard 4: Link healthy */
            if (!rlc_link_is_healthy()) {
                ESP_LOGW(TAG, "ARM rejected: link not healthy");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("LINK DEGRADED");
                break;
            }

            /* Send CMD_ARM */
            if (send_cmd_arm(ch) != 0) {
                ESP_LOGE(TAG, "CMD_ARM send failed");
                break;
            }

            /* Wait for ACK (500ms, CMD_RETRY_COUNT retries on timeout) */
            uint8_t nack_reason = 0;
            int result = wait_for_ack(ch, CMD_ACK_TIMEOUT_MS, &nack_reason);
            for (int retry = 0; result == 0 && retry < CMD_RETRY_COUNT; retry++) {
                /* 2.4: a timeout is the only retry licence. Re-check the arm
                 * key before putting another CMD_ARM on the wire — the key
                 * may have gone off since the last check without an event
                 * having been consumed yet. */
                if (!arm_switch_is_armed()) {
                    ESP_LOGI(TAG, "ARM retry aborted: arm switch OFF");
                    break;
                }
                if (send_cmd_arm(ch) != 0) {
                    ESP_LOGE(TAG, "CMD_ARM retry %d send failed", retry + 1);
                    break;
                }
                result = wait_for_ack(ch, CMD_ACK_TIMEOUT_MS, &nack_reason);
            }

            if (result == 1) {
                if (!arm_switch_is_armed()) {
                    /* 2.4: ACK arrived after the key went off (the switch-off
                     * event was consumed inside wait_for_ack). Never enter
                     * ARMED without the key — undo at the base and stay IDLE. */
                    ESP_LOGW(TAG, "ARM ACK after key-off — sending DISARM, staying IDLE");
                    send_cmd_disarm(ch);
                    buzzer_play(BUZZER_BEEP_TRIPLE);
                } else {
                    /* ACK received with matching channel */
                    s_armed_channel = ch;
                    rlc_rgb_led_set_pattern(LED_PATTERN_ARMED);
                    buzzer_play(BUZZER_BEEP_DOUBLE);
                    ESP_LOGI(TAG, "IDLE -> ARMED (ch %u)", ch);
                    s_state = STATE_ARMED;
                }
            } else if (result == WAIT_FOR_ACK_INTERRUPTED) {
                /* 2.4: local operator input ended the attempt — do not retry,
                 * do not enter ARMED. The base times the ARM out or dead-mans
                 * on its own; also disarm in case a CMD_ARM reached it. */
                ESP_LOGW(TAG, "ARM attempt interrupted — aborting");
                send_cmd_disarm(ch);
                /* m13: the interrupting event may have been EVT_ENCODER_ROTATE,
                 * which wait_for_ack() consumed without applying. Re-sync from
                 * the encoder — otherwise the display and strip cursor keep
                 * showing the pre-rotation channel while the next long-press
                 * arms a different one. Same divergence 4.9 closed for the
                 * LINKING/LINK_LOST path. */
                s_selected_channel = encoder_get_channel();
            } else if (result == -1) {
                /* NACK */
                ESP_LOGW(TAG, "ARM NACK: 0x%02x (%s)",
                         nack_reason, rlc_nack_reason_str(nack_reason));
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_nack(rlc_nack_reason_str(nack_reason));
            } else if (result == -2) {
                /* Channel mismatch */
                ESP_LOGE(TAG, "ARM ACK channel mismatch");
                send_cmd_disarm(ch);
                buzzer_play(BUZZER_BEEP_TRIPLE);
            } else if (result == WAIT_FOR_ACK_STATE_HANDLED) {
                /* R1: state already transitioned to LINK_LOST or ERROR — do nothing */
            } else {
                /* Timeout or interrupted */
                ESP_LOGW(TAG, "ARM failed — no response or interrupted");
            }

        } else if (evt->type == EVT_FIRE_BUTTON_PRESSED) {
            /* Ignored in IDLE (FSD §8.2.3) */
        } else if (evt->type == EVT_LINK_LOST) {
            do_enter_link_lost();
        } else if (evt->type == EVT_STATUS_UPDATE) {
            /* Cache STATUS_UPDATE */
            cache_status(&evt->data.status_update.status);
            /* R2: multi-arm detection — base must never report >1 channel armed */
            if (is_multi_armed(s_last_status.channel_armed_bitmask)) {
                handle_multi_arm_violation(s_last_status.channel_armed_bitmask);
            } else if (s_last_status.channel_armed_bitmask != 0) {
                /* 4.10: the base reports an armed channel while we are IDLE
                 * (e.g. our ARM ACK was lost). Reconcile by disarming — the
                 * hazard state must not persist out of sight of this UI
                 * until the base's own 10 s arm timeout. */
                ESP_LOGW(TAG, "base armed while remote IDLE — DISARM to reconcile");
                send_cmd_disarm(0xFF);
            }
        } else if (evt->type == EVT_ENCODER_ROTATE) {
            /* R4: track selected channel in IDLE so getter is not stale */
            s_selected_channel = evt->data.encoder.channel;
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            do_enter_error_text("REMOTE BATTERY CRITICAL");
        }
        break;

    /* ─── ARMED ────────────────────────────────────────────── */
    case STATE_ARMED:
        if (evt->type == EVT_FIRE_BUTTON_PRESSED) {
            /* Attempt to fire (FSD §8.2.4) */
            /* Guard 0 (2.4): arm key must be ON. FSD §8.2.3/§8.2.4 list the
             * arm switch as a fire precondition; the remote must never put a
             * CMD_FIRE on the wire without it, whatever led it into ARMED. */
            if (!arm_switch_is_armed()) {
                ESP_LOGW(TAG, "FIRE rejected: arm switch OFF");
                do_disarm_and_idle();
                break;
            }

            /* Guard 1: STATUS_UPDATE confirms channel still armed */
            uint16_t armed_mask = s_last_status.channel_armed_bitmask;
            /* R2: multi-arm detection — refuse to fire if base reports >1 armed */
            if (is_multi_armed(armed_mask)) {
                handle_multi_arm_violation(armed_mask);
                break;
            }
            if (!(armed_mask & (1U << (s_armed_channel - 1)))) {
                ESP_LOGW(TAG, "FIRE rejected: base no longer armed");
                do_disarm_and_idle();
                break;
            }

            /* M6: Guard 2: STATUS_UPDATE must be fresh (FSD §8.2.4 guard 1) */
            if (!is_status_fresh()) {
                ESP_LOGW(TAG, "FIRE rejected: stale STATUS_UPDATE");
                do_disarm_and_idle();
                break;
            }

            /* Guard 3: link healthy */
            if (!rlc_link_is_healthy()) {
                ESP_LOGW(TAG, "FIRE rejected: link not healthy");
                do_disarm_and_idle();
                break;
            }

            /* Send CMD_FIRE */
            if (send_cmd_fire(s_armed_channel) != 0) {
                ESP_LOGE(TAG, "CMD_FIRE send failed");
                do_disarm_and_idle();
                break;
            }

            /* Wait for ACK (500ms, NO retry for fire) */
            uint8_t nack_reason = 0;
            int result = wait_for_ack(s_armed_channel, CMD_ACK_TIMEOUT_MS,
                                      &nack_reason);

            if (result == 1) {
                if (!arm_switch_is_armed()) {
                    /* 2.4: key went off while the FIRE ACK was in flight —
                     * cancel instead of entering PRE_FIRE. */
                    ESP_LOGW(TAG, "FIRE ACK after key-off — CEASE_FIRE, staying disarmed");
                    send_cmd_cease_fire();
                    do_disarm_and_idle();
                } else {
                    /* ACK — enter PRE_FIRE */
                    s_prefire_start_ms = now_ms();
                    s_fire_repeat_active = true;
                    xTaskNotifyGive(s_fire_repeat_task);  /* Wake fire-repeat task */
                    rlc_rgb_led_set_pattern(LED_PATTERN_PRE_FIRE);
                    ESP_LOGI(TAG, "ARMED -> PRE_FIRE (ch %u)", s_armed_channel);
                    s_state = STATE_PRE_FIRE;
                }
            } else if (result == -1) {
                /* NACK */
                ESP_LOGW(TAG, "FIRE NACK: 0x%02x (%s)",
                         nack_reason, rlc_nack_reason_str(nack_reason));
                display_nack(rlc_nack_reason_str(nack_reason));
                do_disarm_and_idle();
            } else if (result == WAIT_FOR_ACK_STATE_HANDLED) {
                /* R1: LINK_LOST or BATTERY_CRITICAL was handled inline by
                 * wait_for_ack(); state has already transitioned and we MUST
                 * NOT call do_disarm_and_idle() (which would stomp the alarm
                 * state back to IDLE and silence the operator alert). */
            } else {
                /* Timeout or interrupted */
                ESP_LOGW(TAG, "FIRE failed — aborting");
                do_disarm_and_idle();
            }

        } else if (evt->type == EVT_ARM_SWITCH_CHANGED && !evt->data.arm_state.armed) {
            /* Arm switch OFF */
            do_disarm_and_idle();
        } else if (evt->type == EVT_ENCODER_SHORT_PRESS) {
            /* Encoder press in ARMED = DISARM (FSD §8.2.7) */
            do_disarm_and_idle();
        } else if (evt->type == EVT_ENCODER_LONG_PRESS) {
            /* Encoder long press in ARMED = DISARM */
            do_disarm_and_idle();
        } else if (evt->type == EVT_ENCODER_ROTATE) {
            /* Channel change while armed = DISARM (FSD §8.2.7) */
            s_selected_channel = evt->data.encoder.channel;
            do_disarm_and_idle();
        } else if (evt->type == EVT_STATUS_UPDATE) {
            cache_status(&evt->data.status_update.status);

            uint16_t armed_mask = s_last_status.channel_armed_bitmask;

            /* R2: multi-arm detection */
            if (is_multi_armed(armed_mask)) {
                handle_multi_arm_violation(armed_mask);
                break;
            }

            /* Check if base disarmed us */
            if (s_armed_channel > 0 &&
                !(armed_mask & (1U << (s_armed_channel - 1)))) {
                ESP_LOGW(TAG, "STATUS_UPDATE shows base disarmed");
                s_armed_channel = 0;
                rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
                buzzer_play(BUZZER_BEEP_LONG);
                s_state = STATE_IDLE;
            }
            /* N2: dead-code stale check removed; staleness is handled by
             * check_timers() which runs every 50 ms regardless of message arrival. */
        } else if (evt->type == EVT_LINK_LOST) {
            do_enter_link_lost();
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            do_enter_error_text("REMOTE BATTERY CRITICAL");
        }
        break;

    /* ─── PRE_FIRE ─────────────────────────────────────────── */
    case STATE_PRE_FIRE:
        if (evt->type == EVT_FIRE_BUTTON_RELEASED) {
            /* Button released during pre-fire -> abort (FSD §8.2.4) */
            ESP_LOGI(TAG, "Fire button released during PRE_FIRE — abort");
            s_fire_repeat_active = false;
            send_cmd_cease_fire();
            do_enter_idle();
        } else if (evt->type == EVT_ARM_SWITCH_CHANGED && !evt->data.arm_state.armed) {
            s_fire_repeat_active = false;
            send_cmd_cease_fire();
            do_enter_idle();
        } else if (evt->type == EVT_ENCODER_ROTATE ||
                   evt->type == EVT_ENCODER_SHORT_PRESS ||
                   evt->type == EVT_ENCODER_LONG_PRESS) {
            s_fire_repeat_active = false;
            send_cmd_cease_fire();
            do_enter_idle();
        } else if (evt->type == EVT_STATUS_UPDATE) {
            cache_status(&evt->data.status_update.status);

            /* Check if base aborted */
            if (s_last_status.base_state != STATE_PRE_FIRE &&
                s_last_status.base_state != STATE_FIRING &&
                s_last_status.base_state != STATE_ARMED) {
                ESP_LOGW(TAG, "Base left PRE_FIRE/FIRING — sync to base");
                s_fire_repeat_active = false;
                do_enter_idle();
            }
        } else if (evt->type == EVT_LINK_LOST) {
            s_fire_repeat_active = false;
            do_enter_link_lost();
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            /* N1: stop firing immediately; base will dead-man timeout in 500 ms */
            s_fire_repeat_active = false;
            send_cmd_cease_fire();
            do_enter_error_text("REMOTE BATTERY CRITICAL");
        }
        break;

    /* ─── FIRING ───────────────────────────────────────────── */
    case STATE_FIRING:
        if (evt->type == EVT_FIRE_BUTTON_RELEASED) {
            /* Button released -> CEASE_FIRE (FSD §8.2.6) */
            ESP_LOGI(TAG, "Fire button released — CEASE_FIRE");
            s_fire_repeat_active = false;
            send_cmd_cease_fire();
            do_enter_idle();
        } else if (evt->type == EVT_ARM_SWITCH_CHANGED && !evt->data.arm_state.armed) {
            s_fire_repeat_active = false;
            send_cmd_cease_fire();
            do_enter_idle();
        } else if (evt->type == EVT_STATUS_UPDATE) {
            cache_status(&evt->data.status_update.status);

            /* Fire complete detected via STATUS_UPDATE */
            if (s_last_status.base_state == STATE_POST_FIRE ||
                s_last_status.base_state == STATE_IDLE) {
                ESP_LOGI(TAG, "Fire complete detected (base state=%d)",
                         s_last_status.base_state);
                display_fire_complete(s_armed_channel);
                s_fire_repeat_active = false;
                do_enter_idle();
            }
        } else if (evt->type == EVT_LINK_LOST) {
            s_fire_repeat_active = false;
            do_enter_link_lost();
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            /* N1: stop firing immediately; base will dead-man timeout in 500 ms */
            s_fire_repeat_active = false;
            send_cmd_cease_fire();
            do_enter_error_text("REMOTE BATTERY CRITICAL");
        }
        break;

    /* ─── LINK_LOST ────────────────────────────────────────── */
    case STATE_LINK_LOST:
        if (evt->type == EVT_LINK_RECOVERED) {
            buzzer_stop();
            /* 4.9: same re-sync as every other IDLE entry — the encoder may
             * have moved while the link was down. */
            do_enter_idle();
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            /* m1: the base got this fix; the remote had not. The battery task
             * edge-triggers and posts once per crossing, so discarding it here
             * lost it permanently — the remote then recovered the link and
             * returned to IDLE on a critical pack, with only the
             * REMOTE_VBAT_MIN_ARM_MV arming guard left. Critical is terminal. */
            do_enter_error_text("REMOTE BATTERY CRITICAL");
        }
        break;

    /* ─── ERROR ────────────────────────────────────────────── */
    case STATE_ERROR:
        /* Intentionally unrecoverable */
        break;

    default:
        break;
    }
}

/* ── Timer Checks ─────────────────────────────────────────────── */

static void check_timers(void)
{
    /* PRE_FIRE -> FIRING: local countdown elapsed (FSD §8.2.5) */
    if (s_state == STATE_PRE_FIRE && s_prefire_start_ms > 0) {
        if ((now_ms() - s_prefire_start_ms) >= PRE_FIRE_DELAY_MS) {
            ESP_LOGI(TAG, "PRE_FIRE -> FIRING (local countdown elapsed)");
            rlc_rgb_led_set_pattern(LED_PATTERN_FIRING);
            s_state = STATE_FIRING;
        }
    }

    /* Stale data safety timeout in IDLE/ARMED/PRE_FIRE/FIRING */
    if (s_state != STATE_BOOT && s_state != STATE_LINK_LOST &&
        s_state != STATE_ERROR && s_last_status_rx_ms > 0) {
        if ((now_ms() - s_last_status_rx_ms) > STATUS_STALE_TIMEOUT_MS) {
            ESP_LOGW(TAG, "STATUS_UPDATE stale timeout (%lld ms)",
                     now_ms() - s_last_status_rx_ms);
            if (s_state != STATE_IDLE) {
                s_fire_repeat_active = false;
                /* 4.11: PRE_FIRE/FIRING need CEASE_FIRE (the base is mid
                 * sequence — DISARM is the wrong tool there), and DISARM is
                 * only meaningful with a real channel — never 0. */
                if (s_state == STATE_PRE_FIRE || s_state == STATE_FIRING) {
                    send_cmd_cease_fire();
                } else if (s_armed_channel > 0) {
                    send_cmd_disarm(s_armed_channel);
                }
            }
            do_enter_idle();
            /* m3: latch the timeout. s_last_status_rx_ms used to be left
             * alone, so the whole block re-ran on every 50 ms tick until a
             * fresh STATUS_UPDATE arrived — a 20 Hz warning flood during
             * exactly the condition an operator needs to read the log
             * through. Clearing it also makes is_status_fresh() report
             * "no data" rather than "old data", which is the honest answer
             * and is what the ARM guard already treats as a refusal. */
            portENTER_CRITICAL(&s_status_lock);
            s_last_status_rx_ms = 0;
            portEXIT_CRITICAL(&s_status_lock);
        }
    }
}

/* ── CMD_FIRE Repeat Task ─────────────────────────────────────── */

static void cmd_fire_repeat_task_fn(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    ESP_LOGI(TAG, "fire repeat task started");

    while (1) {
        /* Wait for notification from FSM task (start signal).
         * Use a timed wait so we can feed the task watchdog even while
         * dormant (portMAX_DELAY would never reset the WDT). */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WATCHDOG_TIMEOUT_S * 1000 - 500));
        esp_task_wdt_reset();

        while (s_fire_repeat_active) {
            /* R5: Re-check the flag immediately before sending. The FSM may
             * have cleared s_fire_repeat_active and called send_cmd_cease_fire()
             * after our outer-loop check but before this point — we don't want
             * a stray CMD_FIRE on the wire after CEASE_FIRE.
             * 2.4: the arm key is re-checked too — no CMD_FIRE leaves this
             * unit with the key off, even before the FSM consumes the
             * switch-off event. */
            uint8_t ch = s_armed_channel;
            if (ch > 0 && s_fire_repeat_active && arm_switch_is_armed()) {
                send_cmd_fire(ch);  /* Fire-and-forget, no ACK */
            }
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(FIRE_REPEAT_INTERVAL_MS));
        }
    }
}

/* ── FSM Task Main Loop ───────────────────────────────────────── */

static void remote_fsm_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    /* M4: Transition BOOT→LINKING immediately. The link manager handles
     * the LINK_REQUEST/LINK_ACK handshake; this state drives "Connecting..."
     * display and provides a clean FSM state for the linking phase. */
    ESP_LOGI(TAG, "BOOT -> LINKING");
    s_state = STATE_LINKING;

    rlc_fsm_event_t evt;
    ESP_LOGI(TAG, "remote FSM task started");

    while (1) {
        bool got_event = (xQueueReceive(s_evt_queue, &evt,
                                        pdMS_TO_TICKS(50)) == pdTRUE);

        if (got_event) {
            process_event(&evt);
        }

        /* Check software timers */
        check_timers();

        esp_task_wdt_reset();
    }
}
