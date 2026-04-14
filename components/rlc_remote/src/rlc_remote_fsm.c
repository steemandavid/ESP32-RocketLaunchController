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

/* Software timer timestamps */
static int64_t  s_prefire_start_ms = 0;

/* Pending command tracking */
static uint32_t s_pending_cmd_seq = 0;
static uint8_t  s_pending_cmd_type = 0;

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

static bool is_status_fresh(void)
{
    return (s_last_status_rx_ms > 0) &&
           ((now_ms() - s_last_status_rx_ms) < 2 * STATUS_UPDATE_INTERVAL_MS);
}

/* ── Public API ──────────────────────────────────────────────── */

rlc_state_t remote_fsm_get_state(void)       { return s_state; }
uint8_t     remote_fsm_get_selected_channel(void){ return s_selected_channel; }
uint8_t     remote_fsm_get_armed_channel(void){ return s_armed_channel; }
QueueHandle_t remote_fsm_get_queue(void)      { return s_evt_queue; }
TaskHandle_t  remote_fsm_get_task(void)       { return s_fsm_task; }
bool remote_fsm_is_fire_repeat_active(void)   { return s_fire_repeat_active; }

volatile uint8_t *remote_fsm_get_armed_channel_ptr(void)
{
    return &s_armed_channel;
}

void remote_fsm_stop_fire_repeat(void)
{
    s_fire_repeat_active = false;
    if (s_fire_repeat_task) {
        xTaskNotifyGive(s_fire_repeat_task);  /* Wake it up to check the flag */
    }
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

    return rlc_link_send_cmd(MSG_CMD_ARM, &p, sizeof(p));
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

    return rlc_link_send_cmd(MSG_CMD_FIRE, &p, sizeof(p));
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

    return rlc_link_send_cmd(MSG_CMD_DISARM, &p, sizeof(p));
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

    return rlc_link_send_cmd(MSG_CMD_CEASE_FIRE, &p, sizeof(p));
}

/* ── State Transition Helpers ────────────────────────────────── */

static void do_enter_idle(void)
{
    s_armed_channel = 0;
    s_fire_repeat_active = false;
    s_prefire_start_ms = 0;
    rlc_rgb_led_set_pattern(LED_PATTERN_IDLE);
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
    rlc_rgb_led_set_pattern(LED_PATTERN_IDLE);
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
    rlc_rgb_led_set_pattern(LED_PATTERN_LINK_LOST);
    ESP_LOGI(TAG, "-> LINK_LOST");
    s_state = STATE_LINK_LOST;
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
 *  0 = timeout
 * -1 = NACK received (reason in *nack_reason)
 * -2 = channel mismatch in ACK
 */
static int wait_for_ack(uint8_t expected_channel, uint32_t timeout_ms,
                        uint8_t *nack_reason)
{
    int64_t deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        rlc_fsm_event_t evt;
        if (xQueueReceive(s_evt_queue, &evt, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (evt.type == EVT_CMD_ACK) {
                if (evt.data.ack.channel != expected_channel) {
                    return -2;  /* Channel mismatch */
                }
                return 1;  /* ACK OK */
            } else if (evt.type == EVT_CMD_NACK) {
                if (nack_reason) *nack_reason = evt.data.nack.reason_code;
                return -1;  /* NACK */
            } else {
                /* Process other events inline during wait */
                /* Arm switch change, fire button release, link loss */
                if (evt.type == EVT_ARM_SWITCH_CHANGED && !evt.data.arm_state.armed) {
                    return 0;  /* Arm switch off during wait */
                }
                if (evt.type == EVT_FIRE_BUTTON_RELEASED) {
                    return 0;  /* Button released during wait */
                }
                if (evt.type == EVT_ENCODER_ROTATE ||
                    evt.type == EVT_ENCODER_SHORT_PRESS ||
                    evt.type == EVT_ENCODER_LONG_PRESS) {
                    return 0;  /* Encoder activity during wait */
                }
                if (evt.type == EVT_LINK_LOST) {
                    do_enter_link_lost();
                    return 0;
                }
                /* M5: Preserve critical events instead of silently discarding. */
                if (evt.type == EVT_STATUS_UPDATE) {
                    memcpy(&s_last_status, &evt.data.status_update.status,
                           sizeof(rlc_payload_status_update_t));
                    s_last_status_rx_ms = now_ms();
                }
                if (evt.type == EVT_BATTERY_CRITICAL) {
                    do_enter_error();
                    return 0;
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
                /* Display: "Turn ARM key first" — Phase 4 */
                break;
            }

            /* Guard 2: Remote battery OK */
            if (rlc_battery_get_voltage_mv() < REMOTE_VBAT_MIN_ARM_MV) {
                ESP_LOGW(TAG, "ARM rejected: remote battery low (%u mv)",
                         rlc_battery_get_voltage_mv());
                buzzer_play(BUZZER_BEEP_TRIPLE);
                break;
            }

            /* Guard 3: STATUS_UPDATE fresh */
            if (!is_status_fresh()) {
                ESP_LOGW(TAG, "ARM rejected: stale STATUS_UPDATE");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                break;
            }

            /* Guard 4: Link healthy */
            if (!rlc_link_is_healthy()) {
                ESP_LOGW(TAG, "ARM rejected: link not healthy");
                buzzer_play(BUZZER_BEEP_TRIPLE);
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
                if (send_cmd_arm(ch) != 0) {
                    ESP_LOGE(TAG, "CMD_ARM retry %d send failed", retry + 1);
                    break;
                }
                result = wait_for_ack(ch, CMD_ACK_TIMEOUT_MS, &nack_reason);
            }

            if (result == 1) {
                /* ACK received with matching channel */
                s_armed_channel = ch;
                rlc_rgb_led_set_pattern(LED_PATTERN_ARMED);
                buzzer_play(BUZZER_BEEP_DOUBLE);
                ESP_LOGI(TAG, "IDLE -> ARMED (ch %u)", ch);
                s_state = STATE_ARMED;
            } else if (result == -1) {
                /* NACK */
                ESP_LOGW(TAG, "ARM NACK: 0x%02x (%s)",
                         nack_reason, rlc_nack_reason_str(nack_reason));
                buzzer_play(BUZZER_BEEP_TRIPLE);
                /* NACK display for 3s — Phase 4 */
            } else if (result == -2) {
                /* Channel mismatch */
                ESP_LOGE(TAG, "ARM ACK channel mismatch");
                send_cmd_disarm(ch);
                buzzer_play(BUZZER_BEEP_TRIPLE);
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
            memcpy(&s_last_status, &evt->data.status_update.status,
                   sizeof(rlc_payload_status_update_t));
            s_last_status_rx_ms = now_ms();
        }
        break;

    /* ─── ARMED ────────────────────────────────────────────── */
    case STATE_ARMED:
        if (evt->type == EVT_FIRE_BUTTON_PRESSED) {
            /* Attempt to fire (FSD §8.2.4) */
            /* Guard 1: STATUS_UPDATE confirms channel still armed */
            uint16_t armed_mask = s_last_status.channel_armed_bitmask;
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
                /* ACK — enter PRE_FIRE */
                s_prefire_start_ms = now_ms();
                s_fire_repeat_active = true;
                xTaskNotifyGive(s_fire_repeat_task);  /* Wake fire-repeat task */
                rlc_rgb_led_set_pattern(LED_PATTERN_PRE_FIRE);
                ESP_LOGI(TAG, "ARMED -> PRE_FIRE (ch %u)", s_armed_channel);
                s_state = STATE_PRE_FIRE;
            } else if (result == -1) {
                /* NACK */
                ESP_LOGW(TAG, "FIRE NACK: 0x%02x (%s)",
                         nack_reason, rlc_nack_reason_str(nack_reason));
                do_disarm_and_idle();
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
            memcpy(&s_last_status, &evt->data.status_update.status,
                   sizeof(rlc_payload_status_update_t));
            s_last_status_rx_ms = now_ms();

            /* Check if base disarmed us */
            uint16_t armed_mask = s_last_status.channel_armed_bitmask;
            if (s_armed_channel > 0 &&
                !(armed_mask & (1U << (s_armed_channel - 1)))) {
                ESP_LOGW(TAG, "STATUS_UPDATE shows base disarmed");
                s_armed_channel = 0;
                rlc_rgb_led_set_pattern(LED_PATTERN_IDLE);
                buzzer_play(BUZZER_BEEP_LONG);
                s_state = STATE_IDLE;
            }

            /* Stale data safety timeout */
            if (s_last_status_rx_ms > 0 &&
                (now_ms() - s_last_status_rx_ms) > STATUS_STALE_TIMEOUT_MS) {
                ESP_LOGW(TAG, "STATUS_UPDATE stale (%lld ms)",
                         now_ms() - s_last_status_rx_ms);
                do_disarm_and_idle();
            }
        } else if (evt->type == EVT_LINK_LOST) {
            do_enter_link_lost();
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            do_enter_error();
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
            memcpy(&s_last_status, &evt->data.status_update.status,
                   sizeof(rlc_payload_status_update_t));
            s_last_status_rx_ms = now_ms();

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
            memcpy(&s_last_status, &evt->data.status_update.status,
                   sizeof(rlc_payload_status_update_t));
            s_last_status_rx_ms = now_ms();

            /* Fire complete detected via STATUS_UPDATE */
            if (s_last_status.base_state == STATE_POST_FIRE ||
                s_last_status.base_state == STATE_IDLE) {
                ESP_LOGI(TAG, "Fire complete detected (base state=%d)",
                         s_last_status.base_state);
                s_fire_repeat_active = false;
                do_enter_idle();
            }
        } else if (evt->type == EVT_LINK_LOST) {
            s_fire_repeat_active = false;
            do_enter_link_lost();
        }
        break;

    /* ─── LINK_LOST ────────────────────────────────────────── */
    case STATE_LINK_LOST:
        if (evt->type == EVT_LINK_RECOVERED) {
            buzzer_stop();
            rlc_rgb_led_set_pattern(LED_PATTERN_IDLE);
            ESP_LOGI(TAG, "LINK_LOST -> IDLE (recovered)");
            s_state = STATE_IDLE;
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
                send_cmd_disarm(s_armed_channel);
            }
            do_enter_idle();
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
        /* Wait for notification from FSM task (start signal) */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (s_fire_repeat_active) {
            uint8_t ch = s_armed_channel;
            if (ch > 0 && s_fire_repeat_active) {
                send_cmd_fire(ch);  /* Fire-and-forget, no ACK */
            }
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
