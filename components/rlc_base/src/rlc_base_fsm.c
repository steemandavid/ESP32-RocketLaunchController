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
#include "rlc_faultinject.h"

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
static int64_t s_firing_start_ms = 0;   /* 4.5: max-duration backstop */

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
static void do_disarm_continuity_lost(void);
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

/* BF-05: base_fsm_post_event() was removed 2026-08-27. It was the original
 * zero-timeout event poster; every caller was migrated to a short blocking
 * send (10 ms) in rlc_base_main.c because dropping a safety event on a
 * transient queue burst is not acceptable. Keeping an unused zero-timeout
 * poster in the header was an invitation to reintroduce that. */

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
    /* T-A13 injection: corrupt the channel of an ARM ACK so the remote's
     * mismatch check has something to catch. Restricted to MSG_CMD_ARM — that
     * is the ACK the test names, and corrupting a DISARM ACK would instead
     * exercise the remote's recovery path while it is already recovering.
     * Compiles to nothing in a normal build. */
    if (msg_type == MSG_CMD_ARM) {
        (void)fault_inject_take_wrong_channel(&channel);
    }

    rlc_payload_cmd_ack_t p = {0};
    p.acked_msg_type = msg_type;
    p.acked_sequence_number = seq_num;
    p.channel = channel;
    uint32_t seq = rlc_link_next_seq();
    /* m6: 0 means the tx counter overflowed; rlc_link_next_seq() has already
     * dropped the link. Sending anyway would put a frame on the wire that the
     * peer rejects as a replay. */
    if (seq == 0) {
        ESP_LOGE(TAG, "ACK not sent (type=0x%02x) — seq overflow, link dropped", msg_type);
        return;
    }
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
    if (seq == 0) {   /* m6 — see send_ack() */
        ESP_LOGE(TAG, "NACK not sent (type=0x%02x) — seq overflow, link dropped", msg_type);
        return;
    }
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

/* BF-03: disarm caused by the armed channel going OPEN (FSD §7.2.7, §12.2).
 * Identical to do_disarm() except the siren sounds SIREN_CONTINUITY_LOST
 * instead of falling silent, so the operator can tell "the igniter left the
 * circuit" from "someone turned the key off". do_disarm() has already driven
 * the siren off, so the pattern is started after it — starting it before would
 * be cancelled by siren_off(). */
static void do_disarm_continuity_lost(void)
{
    do_disarm();
    siren_start_continuity_lost();
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
    char errbuf[80];
    ESP_LOGE(TAG, "-> ERROR (flags=0x%02x: %s)", s_error_flags,
             rlc_error_flags_str(s_error_flags, errbuf, sizeof(errbuf)));
    s_state = STATE_ERROR;
}

/* 2.2: Shared tail for every *abort* exit from FIRING, so an
 * ERR_VBAT_CRITICAL flag latched during the pulse (FSD §7.2.5: "complete the
 * pulse, then ERROR") can never be dropped. Without this, operator-abort
 * exits returned the unit to service on a critical battery, and the stale
 * flag later detonated as a spurious terminal ERROR at the next POST_FIRE
 * entry. Callers must already have made the hardware safe
 * (fire_timer_stop/relay_all_safe/siren_off) and cleared
 * s_firing_channel/s_armed_channel/s_link_lost_pending.
 *
 * m4: NOT used by the normal FIRING -> POST_FIRE completion path. That path
 * is the one case where the pulse did finish, so FSD §7.2.5's "complete the
 * pulse, then ERROR" is honoured by the POST_FIRE flag check instead
 * (process_event's POST_FIRE case and check_timers). Do not "unify" the two
 * without re-reading that requirement. */
static void firing_exit(rlc_state_t safe_state)
{
    if (s_error_flags & ERR_VBAT_CRITICAL) {
        ESP_LOGE(TAG, "battery critical latched during FIRING — terminal ERROR");
        do_enter_error(0);  /* Flag already set */
        return;
    }
    if (safe_state == STATE_LINK_LOST) siren_start_link_lost();
    rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
    status_update_trigger();
    s_state = safe_state;
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

/* ── Guard: continuity loss while armed (FSD §7.2.7) ─────────── */

/**
 * True when this event says the *armed* channel's igniter has gone OPEN.
 *
 * Added 2026-08-26. Until then continuity was checked only at arm time
 * (guard_arm guard 2) and band changes were informational: FSD §7.3.1 called
 * mid-arm igniter disconnection "an accepted low-probability risk" bounded by
 * ARM_TIMEOUT_MS. On-target testing showed what that means in practice — pull
 * the igniter lead while ARMED and the base stays armed, siren sounding, fire
 * path live, with nothing on either unit saying the igniter is gone. The risk
 * is not the accepted one; disconnection is exactly what happens when someone
 * is working at the pad, which is when being armed matters most.
 *
 * Only OPEN disarms, matching guard 2 — OPEN is the sole band that blocks
 * arming. MARGINAL and SHORT stay informational (§7.3.1 step 2).
 *
 * Deliberately NOT applied in FIRING or POST_FIRE. During FIRING the armed
 * channel's relay is on NO, so its NC sense line is physically disconnected
 * and reads OPEN *by design* — acting on that would abort every fire pulse
 * the instant it started. In POST_FIRE, OPEN is the success indicator: it
 * means the igniter fired (§7.3.1, post-fire igniter status).
 */
static bool armed_channel_went_open(const rlc_fsm_event_t *evt)
{
    return evt->type == EVT_CONTINUITY_CHANGED &&
           s_armed_channel != 0 &&
           evt->data.continuity.channel == s_armed_channel &&
           evt->data.continuity.band == CONT_OPEN;
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
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            /* §6: battery posts this once per crossing — discarding it here
             * would lose it permanently. Critical battery is terminal. */
            do_enter_error(ERR_VBAT_CRITICAL);
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
                siren_start_continuous();
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

                /* Bug #30: re-check continuity before completing the ARM.
                 * guard_arm() checked it up to ARM_SENSE_VERIFY_TIMEOUT_MS ago
                 * and the FSM has been sitting in STATE_IDLE since, where
                 * EVT_CONTINUITY_CHANGED is not handled — so a disconnection
                 * inside that window was dropped, and because the band has
                 * already changed it will never be reported again. Refuse here
                 * rather than arming and relying on an edge that has already
                 * been consumed. */
                if (continuity_get_channel(ch) == CONT_OPEN) {
                    ESP_LOGW(TAG, "ARM aborted — ch %u went OPEN during arm verify", ch);
                    abort_arm_verify(NACK_NO_CONTINUITY);
                    return;
                }

                s_arm_verify_pending = false;
                s_armed_channel = ch;
                s_arm_time_ms = now_ms();
                s_arm_verify_channel = 0;
                s_arm_verify_start_ms = 0;
                siren_start_continuous();
                rlc_rgb_led_set_pattern(LED_PATTERN_ARMED);
                send_ack(MSG_CMD_ARM, s_arm_verify_seq, ch);
                status_update_trigger();
                ESP_LOGI(TAG, "IDLE -> ARMED (ch %u, sense verified)", ch);
                s_state = STATE_ARMED;
            }

        } else if (evt->type == EVT_CMD_DISARM) {
            /* 2.1: a DISARM arriving inside the 200 ms arm-verify window must
             * cancel the pending ARM (the arm relay is already energised) —
             * same as CEASE_FIRE below. Otherwise the verify completes after
             * the remote has shown "disarmed", leaving the base ARMED behind
             * the operator's back. */
            if (s_arm_verify_pending) {
                abort_arm_verify(NACK_WRONG_STATE);
            }
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
        } else if (evt->type == EVT_KEY_SWITCH_CHANGED && !evt->data.arm_state.armed) {
            /* 4.6: key OFF during the verify window cancels the pending ARM
             * with the true reason — previously the window ignored key-off
             * and the verify timeout later misreported NACK_ARM_SENSE_FAULT. */
            if (s_arm_verify_pending) {
                abort_arm_verify(NACK_BASE_SWITCH_OFF);
            }
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

            /* Guard 3 (4.7): key switch still ON at ARMED→PRE_FIRE. The key
             * is re-checked again at PRE_FIRE→FIRING, but refusing early
             * stops the siren/2 s countdown from starting at all. */
            if (!key_sense_get_debounced()) {
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
            /* Already continuous since ARMED (2026-08-26) — re-asserted so
             * PRE_FIRE does not depend on how ARMED was entered. */
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
        } else if (armed_channel_went_open(evt)) {
            /* Igniter disconnected or blown while armed — go safe. */
            ESP_LOGW(TAG, "Continuity OPEN on armed ch %u during ARMED — disarm",
                     s_armed_channel);
            do_disarm_continuity_lost();
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
        } else if (armed_channel_went_open(evt)) {
            /* The relay is still on NC through the whole countdown, so this
             * reading is live and real. An igniter that has left the circuit
             * will not fire — abort rather than run the countdown out. */
            ESP_LOGW(TAG, "Continuity OPEN on armed ch %u during PRE_FIRE — abort",
                     s_armed_channel);
            do_disarm_continuity_lost();
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
            /* BF-01: this is the ONE exit from FIRING that used to skip
             * fire_timer_stop(). An expired one-shot alarm disables the alarm
             * but leaves the GPTimer in RUN state, so the next
             * fire_timer_start() of the same power cycle hit a running timer.
             * fire_timer_start() now stops defensively too, but stopping on
             * the normal path keeps the timer in a known state between pulses
             * rather than relying on the next start to clean up. */
            fire_timer_stop();
            relay_all_safe();
            siren_off();
            s_firing_channel = 0;
            s_armed_channel = 0;

            /* C1: If link was lost during pulse, transition to LINK_LOST now */
            if (s_link_lost_pending) {
                s_link_lost_pending = false;
                ESP_LOGI(TAG, "FIRING -> LINK_LOST (pulse completed, link was lost)");
                firing_exit(STATE_LINK_LOST);
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
            ESP_LOGI(TAG, "FIRING -> IDLE (CEASE_FIRE)");
            firing_exit(STATE_IDLE);

        } else if (evt->type == EVT_ARM_SENSE_CHANGED && !evt->data.arm_state.armed) {
            /* Arm relay feedback lost during FIRING — cease fire */
            fire_timer_stop();
            relay_all_safe();
            siren_off();
            s_firing_channel = 0;
            s_armed_channel = 0;
            s_link_lost_pending = false;
            ESP_LOGW(TAG, "FIRING -> IDLE (arm sense lost)");
            firing_exit(STATE_IDLE);

        } else if (evt->type == EVT_KEY_SWITCH_CHANGED && !evt->data.arm_state.armed) {
            /* Key switch OFF during FIRING — same as CEASE_FIRE (FSD §7.2.5) */
            fire_timer_stop();
            relay_all_safe();
            siren_off();
            s_firing_channel = 0;
            s_armed_channel = 0;
            s_link_lost_pending = false;
            ESP_LOGW(TAG, "FIRING -> IDLE (key switch OFF)");
            firing_exit(STATE_IDLE);

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
            ESP_LOGI(TAG, "FIRING -> IDLE (DISARM)");
            firing_exit(STATE_IDLE);

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
                s_link_lost_pending = false;
                ESP_LOGW(TAG, "FIRING -> LINK_LOST (immediate abort, link lost)");
                firing_exit(STATE_LINK_LOST);
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
        }
        /* Battery critical — either arriving now, or latched during FIRING and
         * carried here so the pulse could complete first (FSD §7.2.5).
         * m4: a single check. Previously EVT_BATTERY_CRITICAL called
         * do_enter_error() and then fell into an unconditional second call
         * below — two relay_all_safe() passes (20 ms of vTaskDelay each), two
         * siren restarts and a duplicate ERROR log for one event. */
        if ((evt->type == EVT_BATTERY_CRITICAL) ||
            (s_error_flags & ERR_VBAT_CRITICAL)) {
            do_enter_error(ERR_VBAT_CRITICAL);
        }
        break;

    /* ─── LINK_LOST ────────────────────────────────────────── */
    case STATE_LINK_LOST:
        if (evt->type == EVT_LINK_RECOVERED) {
            siren_off();  /* Silence siren immediately (FSD §7.2.8) */
            rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
            ESP_LOGI(TAG, "LINK_LOST -> IDLE (recovered)");
            s_state = STATE_IDLE;
        } else if (evt->type == EVT_BATTERY_CRITICAL) {
            /* §6: previously silently discarded — the unit then idled on a
             * critical battery after link recovery with only guard 8 left. */
            do_enter_error(ERR_VBAT_CRITICAL);
        }
        break;

    /* ─── ERROR ────────────────────────────────────────────── */
    case STATE_ERROR:
        /* The state is intentionally unrecoverable — nothing here changes it,
         * and no command is acted on. But commands are ANSWERED (2026-08-26).
         *
         * Until now this was a bare `break;`: every command was discarded
         * without a reply, so the remote could only time out, and a timeout
         * carries no reason. An operator saw a long-press do nothing, with no
         * way to tell a dead link from a base needing a power cycle.
         *
         * All four commands get the same NACK. Answering a DISARM with a
         * refusal may look odd when the base is already safe — relay_all_safe()
         * ran on entry to ERROR — but "I am in ERROR" is the operative fact in
         * every case, and it is what tells the operator a power cycle is
         * required. Silently ACKing a disarm would say "all is well" about a
         * unit that is out of service. */
        switch (evt->type) {
        case EVT_CMD_ARM:
            send_nack(MSG_CMD_ARM, evt->data.cmd.seq_number, NACK_BASE_ERROR);
            break;
        case EVT_CMD_FIRE:
            send_nack(MSG_CMD_FIRE, evt->data.cmd.seq_number, NACK_BASE_ERROR);
            break;
        case EVT_CMD_DISARM:
            send_nack(MSG_CMD_DISARM, evt->data.cmd.seq_number, NACK_BASE_ERROR);
            break;
        case EVT_CMD_CEASE_FIRE:
            send_nack(MSG_CMD_CEASE_FIRE, evt->data.cmd.seq_number, NACK_BASE_ERROR);
            break;
        default:
            break;   /* local I/O, timers, link events — nothing to answer */
        }
        break;

    default:
        break;
    }
}

/* ── Timer Checks (called each poll cycle) ──────────────────── */

static void check_timers(void)
{
    int64_t t = now_ms();

    /* Bug #30: level-triggered backstop for the continuity-loss disarm.
     *
     * EVT_CONTINUITY_CHANGED is edge-triggered — continuity_task posts once,
     * on the transition. Any edge that is lost or arrives at the wrong moment
     * silently removes the protection, because the band has already changed
     * and will never be reported again. Two such cases exist: an event posted
     * during the arm-verify window, when the FSM is still in STATE_IDLE and
     * does not handle it; and an event dropped because the FSM queue was full
     * (on_io_change() logs, but cannot regenerate it).
     *
     * Re-reading the level cannot miss an edge. If the armed channel is OPEN
     * it is OPEN on every tick until something changes it, so this converges
     * within one 50 ms tick of any missed event.
     *
     * The event path stays the fast detector; this is the backstop. An
     * edge-triggered safety monitor should always have one.
     *
     * Scoped exactly as armed_channel_went_open(): ARMED and PRE_FIRE only.
     * NOT FIRING (the relay is on NO, so the sense line reads OPEN by design)
     * and NOT POST_FIRE (where OPEN means the igniter fired). */
    if ((s_state == STATE_ARMED || s_state == STATE_PRE_FIRE) &&
        s_armed_channel != 0 &&
        continuity_get_channel(s_armed_channel) == CONT_OPEN) {
        ESP_LOGW(TAG, "Continuity OPEN on armed ch %u (level backstop) — disarm",
                 s_armed_channel);
        do_disarm_continuity_lost();
        return;   /* state is now IDLE; the rest of this pass does not apply */
    }

    /* M1: Arm sense verification timeout */
    if (s_arm_verify_pending && s_arm_verify_start_ms > 0) {
        if ((t - s_arm_verify_start_ms) >= ARM_SENSE_VERIFY_TIMEOUT_MS) {
            ESP_LOGW(TAG, "arm sense verify timeout (%d ms) — NACK ARM_SENSE_FAULT",
                     ARM_SENSE_VERIFY_TIMEOUT_MS);
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

            /* Guard 2 (BF-02): heartbeat freshness — the last frame from the
             * remote must be no older than HEARTBEAT_INTERVAL_MS +
             * HEARTBEAT_TIMEOUT_MS (FSD §7.2.4 guard 2). This used to be
             * treated as "implicitly covered by rlc_link_is_healthy()", but
             * that function measures a *rate* over the last 10 pings: 2 misses
             * out of 10 is 20%, passes the 30% test, and still means ~1.5 s of
             * silence — the igniter energised at the exact moment the link
             * died, which is what this guard exists to prevent.
             *
             * The base is the PONG *sender*, so its equivalent of "last PONG
             * received" is the last well-formed frame received from the remote
             * (the PING that each PONG answers). Per §7.2.4 this abort routes
             * to LINK_LOST, not IDLE. */
            int64_t contact_age = rlc_link_ms_since_contact();
            if (contact_age < 0 ||
                contact_age > (HEARTBEAT_INTERVAL_MS + HEARTBEAT_TIMEOUT_MS)) {
                ESP_LOGW(TAG, "PRE_FIRE heartbeat stale (%lld ms) — abort to LINK_LOST",
                         contact_age);
                do_enter_link_lost();
                return;
            }

            /* Guard 4: link quality (FSD §7.2.4) — ping failure rate ≤30%.
             * Spec routes this abort to IDLE, unlike guard 2 above. */
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
            s_firing_start_ms = t;   /* 4.5: backstop baseline */
            relay_fire_set(s_armed_channel, true);
            /* BF-01: the pulse has no timed end if the timer will not start.
             * The 4.5 max-duration backstop would eventually catch it, but a
             * timer that refuses to start is a hardware/driver fault, not a
             * lost notification — cut the pulse now and latch ERROR rather
             * than let it run out the backstop margin. */
            if (fire_timer_start(FIRE_PULSE_DURATION_MS, s_armed_channel,
                                 s_fsm_task) != ESP_OK) {
                ESP_LOGE(TAG, "fire timer failed to start — aborting pulse");
                s_firing_channel = 0;
                s_firing_start_ms = 0;
                do_enter_error(ERR_INTERNAL);
                return;
            }
            rlc_rgb_led_set_pattern(LED_PATTERN_FIRING);
            status_update_trigger();
            ESP_LOGI(TAG, "PRE_FIRE -> FIRING (ch %u)", s_armed_channel);
            s_state = STATE_FIRING;
        }
    }

    /* 4.5: max-duration backstop. If the GPTimer's EVT_FIRE_PULSE_DONE
     * notification is ever lost, the FSM would sit in FIRING with the relay
     * energised forever (the task keeps feeding the TWDT, so nothing else
     * would catch it). Synthesise the completion event well after the pulse
     * should have ended — the normal handler then makes everything safe. */
    if (s_state == STATE_FIRING && s_firing_start_ms > 0) {
        if ((t - s_firing_start_ms) >=
                FIRE_PULSE_DURATION_MS + FIRE_PULSE_BACKSTOP_MARGIN_MS) {
            ESP_LOGE(TAG, "fire pulse done notification lost — backstop abort "
                          "(%lld ms in FIRING)", (t - s_firing_start_ms));
            s_firing_start_ms = 0;
            /* m5: stop the GPTimer explicitly. The EVT_FIRE_PULSE_DONE
             * handler does not (it is the path where the timer has already
             * expired), but here we do not know *why* the notification never
             * arrived — if the timer itself is the problem it is still armed.
             * Every other FIRING exit calls this; so should the backstop. */
            fire_timer_stop();
            rlc_fsm_event_t fe = {0};
            fe.type = EVT_FIRE_PULSE_DONE;
            process_event(&fe);
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
