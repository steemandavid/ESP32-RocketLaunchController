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

#include <stdio.h>
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
static int64_t  s_firing_start_ms = 0;

/* MAJ-02: positive evidence that the BASE reached FIRING — that current was
 * actually put on the igniter. The remote's own FIRING entry proves nothing
 * about the pad: the base can abort anywhere in the 5 s countdown while the
 * remote's local countdown runs on regardless, and if that abort's triggered
 * STATUS_UPDATE is lost, the next periodic one shows IDLE with the local
 * elapsed time already past a full pulse. The old completion test would then
 * announce FIRE COMPLETE for a channel that never carried current. The base
 * calls status_update_trigger() on entering FIRING (rlc_base_fsm.c), so this
 * flag is set by the same push that makes POST_FIRE reliable. */
static bool     s_base_reached_firing = false;

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
static void set_prefire_start(int64_t t);

/* ── Helpers ─────────────────────────────────────────────────── */

static inline int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* CM-03 / FSD §6.4.3: update_sequence gap tracking. The base increments this
 * field on every STATUS_UPDATE it sends; the field was generated correctly but
 * never consumed, so a run of lost frames was invisible — the display simply
 * showed slightly older data with no hint that anything had gone missing.
 * Modular comparison so the uint16 wrap at 65535 is not itself a "gap". */
static bool     s_update_seq_valid = false;
static uint16_t s_last_update_seq = 0;

/* Cache the latest STATUS_UPDATE from the base (FSM task only). */
static void cache_status(const rlc_payload_status_update_t *st)
{
    if (s_update_seq_valid) {
        if (st->update_sequence == s_last_update_seq) {
            /* Same frame twice — the link layer's replay guard should have
             * caught it; log rather than counting it as a gap. */
            ESP_LOGW(TAG, "duplicate STATUS_UPDATE seq %u", st->update_sequence);
        } else {
            /* Modular, so the uint16 wrap at 65535 is one step and not a
             * 65535-frame gap (T-U16). */
            uint16_t lost = rlc_update_seq_lost(s_last_update_seq,
                                                st->update_sequence);
            if (lost > 0) {
                /* §6.4.3 sets the operator-visible threshold at "more than 2
                 * consecutive missed" — at a 2 s interval a single lost frame
                 * is ordinary RF, and toasting it would train the operator to
                 * ignore the warning. Every gap is still logged. */
                ESP_LOGW(TAG, "STATUS_UPDATE data gap: %u frame(s) lost "
                              "(seq %u -> %u)",
                         lost, s_last_update_seq, st->update_sequence);
                if (lost > 2) display_toast("DATA GAP");
            }
        }
    }
    s_last_update_seq = st->update_sequence;
    s_update_seq_valid = true;

    /* MAJ-02: latch the one fact that distinguishes a shot from an abort. */
    if (st->base_state == STATE_FIRING) s_base_reached_firing = true;

    int64_t t = now_ms();
    portENTER_CRITICAL(&s_status_lock);
    memcpy(&s_last_status, st, sizeof(s_last_status));
    s_last_status_rx_ms = t;
    portEXIT_CRITICAL(&s_status_lock);
}

/* Continuity band for a channel out of the cached STATUS_UPDATE bitfield
 * (2 bits per channel, channel 1 in bits 0-1). */
static inline uint8_t status_continuity_band(uint8_t channel)
{
    if (channel < 1 || channel > NUM_CHANNELS) return CONT_OPEN;
    return (uint8_t)((s_last_status.continuity_bands >> ((channel - 1) * 2)) & 0x3);
}

/**
 * Display a NACK, enriching NACK_BASE_ERROR with the specific fault.
 *
 * The NACK payload is a fixed 6 bytes and carries only a reason code, so the
 * base cannot tell us *which* error it is in. It does not need to: error_flags
 * arrives in every STATUS_UPDATE, so the remote already knows. "BASE IN ERROR"
 * alone would send an operator hunting; "BASE ERROR: VBAT CRITICAL" names the
 * thing to go and look at. Falls back to the bare reason when no flags are
 * cached — after a remote reboot, say.
 */
static void base_error_toast(char *out, size_t len)
{
    /* MIN-07: the worst flag plus a "+n", not the full comma list — "BASE
     * ERROR: " already spends 12 of the overlay's 40 characters, and a
     * multi-flag list used to be sliced mid-word ("...RELAY F") and overrun
     * the box. The main screen still cycles every flag (§13.2a). */
    char errbuf[26];
    rlc_error_flags_brief(s_last_status.error_flags, errbuf, sizeof(errbuf));
    snprintf(out, len, "BASE ERROR: %s", errbuf);
}

static void show_nack(uint8_t reason)
{
    if (reason == NACK_BASE_ERROR && s_last_status.error_flags != 0) {
        char toast[40];
        base_error_toast(toast, sizeof(toast));
        display_nack(toast);
        return;
    }
    display_nack(rlc_nack_reason_str(reason));
}

static bool is_status_fresh(void);

/**
 * Is the fire button live — i.e. would a press actually start something?
 *
 * Both halves are required (FSD line 1110 + §8.2.4 guard 1). The remote's own
 * state says the button is wired to an action; the base's confirmation says
 * the fire path is really there. The remote can sit in ARMED while the base
 * has already disarmed underneath it — arm timeout, key turned off, a
 * continuity loss not yet reported — and lighting the ring red through that
 * window would promise a fire path that does not exist.
 *
 * Deliberately reuses the same two conditions the FIRE guards check, so the
 * ring and the guards can never disagree: light it red exactly when a press
 * would be accepted.
 */
static bool fire_is_live(void)
{
    if (s_state != STATE_ARMED && s_state != STATE_PRE_FIRE &&
        s_state != STATE_FIRING) {
        return false;
    }
    if (s_armed_channel == 0 || !is_status_fresh()) return false;
    return (s_last_status.channel_armed_bitmask &
            (1U << (s_armed_channel - 1))) != 0;
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
    set_prefire_start(0);
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
    /* RM-11: s_prefire_start_ms is a 64-bit value written on the FSM task and
     * read here from the display task — a plain read can tear across the two
     * 32-bit halves and produce a wildly wrong countdown on the one screen
     * that has to be trusted. Snapshot it under the same lock the cached
     * status uses. */
    int64_t start;
    portENTER_CRITICAL(&s_status_lock);
    start = s_prefire_start_ms;
    portEXIT_CRITICAL(&s_status_lock);

    if (s_state != STATE_PRE_FIRE || start == 0) return 0;
    int64_t elapsed = now_ms() - start;
    if (elapsed >= PRE_FIRE_DELAY_MS) return 0;
    return (uint32_t)(PRE_FIRE_DELAY_MS - elapsed);
}

/* RM-11: all FSM-task writes to s_prefire_start_ms go through this so the
 * display-task reader above can never observe a half-updated value. */
static void set_prefire_start(int64_t t)
{
    portENTER_CRITICAL(&s_status_lock);
    s_prefire_start_ms = t;
    portEXIT_CRITICAL(&s_status_lock);
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
    set_prefire_start(0);
    s_firing_start_ms = 0;
    s_base_reached_firing = false;
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
    set_prefire_start(0);
    rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
    buzzer_play(BUZZER_BEEP_LONG);
    ESP_LOGI(TAG, "DISARMED -> IDLE");
    s_state = STATE_IDLE;
}

static void do_enter_link_lost(void)
{
    s_fire_repeat_active = false;
    s_armed_channel = 0;
    set_prefire_start(0);
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

/**
 * The firing sequence has ended at the base — report what happened to the
 * channel, then stand down. Call only from PRE_FIRE/FIRING.
 *
 * Three outcomes, and the difference between them is what the operator carries
 * to the pad:
 *
 *  - COMPLETE      the channel had its whole pulse.
 *  - CUT SHORT     the channel WAS energised, for less than the full pulse.
 *  - NOT CONFIRMED the base ended the sequence without this unit ever seeing
 *                  it reach FIRING — most likely an abort during the
 *                  countdown, before any current flowed.
 *
 * MAJ-02: the elapsed-time backstop may only declare completion when the base
 * was actually seen firing. Local elapsed time alone measures the remote's own
 * countdown, which keeps running through a base-side abort, so on a single
 * lost STATUS_UPDATE it used to certify a never-fired channel as COMPLETE.
 * The mirror case (a fresh abort, fired_ms < pulse) used to assert "CUT SHORT
 * AT BASE" — current that never flowed. Neither claim is now made without
 * evidence.
 */
static void report_firing_outcome(void)
{
    int64_t fired_ms = (s_firing_start_ms > 0) ? (now_ms() - s_firing_start_ms) : 0;

    /* POST_FIRE is authoritative, and reliable: the base calls
     * status_update_trigger() on entering it (rlc_base_fsm.c), so a completed
     * pulse always pushes a STATUS_UPDATE saying so rather than waiting for
     * the 2 s poll.
     *
     * The elapsed-time test is therefore only a backstop for that one packet
     * being lost over the air, and it takes NO slack. An earlier version
     * allowed 200 ms for clock skew and misclassified a real abort: the
     * operator turned the pad key at 802 ms of a 1000 ms pulse, two
     * milliseconds inside the margin, and the remote called it complete. The
     * skew fear was unfounded in the wrong direction anyway — a measured
     * completion read 1105 ms from the remote's clock, over rather than under.
     *
     * At the full duration the test is also true on its own terms: if the base
     * cut the pulse at or after 1000 ms, the igniter had already had its whole
     * pulse, so "completed" is accurate. */
    bool completed = (s_last_status.base_state == STATE_POST_FIRE) ||
                     (s_base_reached_firing && fired_ms >= FIRE_PULSE_DURATION_MS);

    s_fire_repeat_active = false;

    if (completed) {
        ESP_LOGI(TAG, "Fire complete (base state=%d, %lld ms)",
                 s_last_status.base_state, (long long)fired_ms);
        display_fire_complete(s_armed_channel);
    } else if (!s_base_reached_firing) {
        /* No evidence the channel was ever energised — and no evidence it was
         * not, if two frames went missing. Say exactly that. */
        char tbuf[40];
        ESP_LOGW(TAG, "Sequence ended without a FIRING status (base state=%d, "
                      "%lld ms) — outcome unconfirmed",
                 s_last_status.base_state, (long long)fired_ms);
        snprintf(tbuf, sizeof(tbuf), "CH %u ENDED - NOT CONFIRMED",
                 s_armed_channel);
        buzzer_play(BUZZER_BEEP_TRIPLE);
        display_toast(tbuf);
    } else {
        /* Name the pad key when the status says it is off — that is the
         * common cause and the operator needs to know the pad end acted, not
         * the remote. */
        char tbuf[40];
        ESP_LOGW(TAG, "Pulse cut short at the base after %lld ms (key=%u)",
                 (long long)fired_ms, s_last_status.base_key_switch);
        snprintf(tbuf, sizeof(tbuf),
                 s_last_status.base_key_switch ? "CH %u CUT SHORT AT BASE"
                                               : "CH %u CUT SHORT - BASE KEY",
                 s_armed_channel);
        buzzer_play(BUZZER_BEEP_TRIPLE);
        display_toast(tbuf);
    }

    do_enter_idle();
}

/**
 * The base is in a state that ends the firing sequence but says nothing about
 * the channel — terminal ERROR, or its own LINK_LOST (MAJ-01).
 *
 * Reporting these as "PULSE CUT SHORT" would blame the pulse for a base that
 * needs a power cycle, so they get their own message. Both are the base's
 * problem, not this unit's, so the remote stands down to IDLE; the ARM guard
 * refuses to re-arm into a base in ERROR anyway.
 */
static void report_base_fault_end(void)
{
    char tbuf[40];
    s_fire_repeat_active = false;
    if (s_last_status.base_state == STATE_ERROR) {
        base_error_toast(tbuf, sizeof(tbuf));
        ESP_LOGE(TAG, "base entered ERROR during fire sequence (flags=0x%02x)",
                 s_last_status.error_flags);
    } else {
        snprintf(tbuf, sizeof(tbuf), "BASE LINK LOST - SEQUENCE ENDED");
        ESP_LOGW(TAG, "base reports state %d during fire sequence",
                 s_last_status.base_state);
    }
    buzzer_play(BUZZER_BEEP_TRIPLE);
    display_toast(tbuf);
    do_enter_idle();
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
    /* DS-01 / FSD §5.5.6: display failure is a hard fault from any state.
     * Handled ahead of the state switch for the same reason the base handles
     * EVT_ARM_SENSE_FAULT that way — there is no state in which "the operator
     * is now flying blind" is acceptable, and while ARMED/PRE_FIRE/FIRING the
     * frozen last frame actively lies about the fire path.
     *
     * §5.5.6 requires CMD_DISARM plus ERROR when this happens while armed;
     * do_disarm_and_idle()'s disarm is not enough here because ERROR must
     * follow, so the disarm is issued explicitly first. Already-ERROR is a
     * no-op (unrecoverable by design). */
    if (evt->type == EVT_DISPLAY_FAULT) {
        if (s_state != STATE_ERROR) {
            ESP_LOGE(TAG, "DISPLAY FAULT — disarming and entering ERROR");
            s_fire_repeat_active = false;
            if (s_state == STATE_PRE_FIRE || s_state == STATE_FIRING) {
                send_cmd_cease_fire();
            }
            if (s_armed_channel > 0) {
                send_cmd_disarm(s_armed_channel);
            } else {
                /* Belt and braces: the base may be armed on a channel this
                 * unit has lost track of, and we are about to stop being able
                 * to show anything at all. */
                send_cmd_disarm(0xFF);
            }
            set_prefire_start(0);
            do_enter_error_text("DISPLAY FAULT");
        }
        return;
    }

    switch (s_state) {

    /* ─── BOOT ─────────────────────────────────────────────── */
    case STATE_BOOT:
        /* Should not reach here — BOOT→LINKING happens at task start. */
        break;

    /* ─── LINKING ─────────────────────────────────────────── */
    case STATE_LINKING:
        if (evt->type == EVT_LINK_ESTABLISHED) {
            ESP_LOGI(TAG, "LINKING -> IDLE (link established)");
            /* RM-02: adopt the base's advertised channel count now that the
             * handshake has completed, so the encoder cannot select past it. */
            encoder_set_max_channel(rlc_link_get_peer_num_channels());
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

            /* Sequence guard: refuse the ARM outright if the fire button is
             * already held.
             *
             * The fresh-press rule (T-S04/T-S08) already makes this safe — a
             * button held through ARMED entry cannot fire, because authorisation
             * needs a 0xFF->0x00 transition *after* entry. But arming into a
             * state where the operator is already pressing fire and nothing
             * happens is the confusing case: the most obvious input in the most
             * critical state is silently inert. Refusing the ARM and naming the
             * reason is the honest answer. The sequence is arm key on, encoder
             * held then released, THEN fire held — in that order. */
            if (fire_button_is_pressed()) {
                ESP_LOGW(TAG, "ARM rejected: fire button already held");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("RELEASE FIRE BUTTON FIRST");
                break;
            }

            /* RM-02 / FSD §8.2.2: refuse a channel the base does not have,
             * locally. The base NACKs it (INVALID_CHANNEL) so this is not a
             * safety gap, but a local refusal names the real reason and does
             * not spend a command round-trip on it. */
            uint8_t base_channels = rlc_link_get_peer_num_channels();
            if (ch > base_channels) {
                ESP_LOGW(TAG, "ARM rejected: ch %u > base num_channels %u",
                         ch, base_channels);
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("CHANNEL NOT ON BASE");
                break;
            }

            /* Guard 1: Arm switch must be ON */
            if (!arm_switch_is_armed()) {
                ESP_LOGI(TAG, "ARM rejected: arm switch OFF");
                /* MAJ-06: this was the one guard in the family with no
                 * audible half. §7.2.9a wants both on every refusal. */
                buzzer_play(BUZZER_BEEP_TRIPLE);
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

            /* Guard 4: base not in a terminal ERROR state.
             *
             * Added 2026-08-26 after on-target testing (T-A12 session). The
             * base's ERROR handler is `break;` — deliberately inert, so it
             * discards CMD_ARM without a NACK. Without this guard the remote
             * sent the ARM, got nothing back, and fell through to the
             * ACK-timeout path, which was the one failure path with no
             * operator feedback at all: the encoder long-press did nothing,
             * silently, with no way to tell a flat battery from a dead link
             * from a base needing a power cycle.
             *
             * Refusing locally is better than relying on a NACK the base
             * cannot send, and naming the flag tells the operator which fault
             * to go and look at. ERROR is unrecoverable by design, so the
             * remedy really is a power cycle. */
            if (s_last_status.base_state == STATE_ERROR) {
                char toast[40];
                base_error_toast(toast, sizeof(toast));
                ESP_LOGW(TAG, "ARM rejected: base in ERROR (flags=0x%02x)",
                         s_last_status.error_flags);
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast(toast);
                break;
            }

            /* Guard 5: Link healthy */
            if (!rlc_link_is_healthy()) {
                ESP_LOGW(TAG, "ARM rejected: link not healthy");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("LINK DEGRADED");
                break;
            }

            /* Send CMD_ARM */
            if (send_cmd_arm(ch) != 0) {
                ESP_LOGE(TAG, "CMD_ARM send failed");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("ARM SEND FAILED");
                break;
            }

            /* Wait for ACK (500ms, CMD_RETRY_COUNT retries on timeout) */
            uint8_t nack_reason = 0;
            /* MIN-09: a retry loop abandoned because the key went off is not
             * "no response from base" — that misattribution is the exact class
             * the FIRE path's -4 branch was written to prevent. */
            bool key_off_abort = false;
            int result = wait_for_ack(ch, CMD_ACK_TIMEOUT_MS, &nack_reason);
            for (int retry = 0; result == 0 && retry < CMD_RETRY_COUNT; retry++) {
                /* 2.4: a timeout is the only retry licence. Re-check the arm
                 * key before putting another CMD_ARM on the wire — the key
                 * may have gone off since the last check without an event
                 * having been consumed yet. */
                if (!arm_switch_is_armed()) {
                    ESP_LOGI(TAG, "ARM retry aborted: arm switch OFF");
                    key_off_abort = true;
                    break;
                }
                if (send_cmd_arm(ch) != 0) {
                    ESP_LOGE(TAG, "CMD_ARM retry %d send failed", retry + 1);
                    buzzer_play(BUZZER_BEEP_TRIPLE);
                    display_toast("ARM SEND FAILED");
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
                    display_toast("ARM KEY OFF - NOT ARMED");
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
                /* MAJ-06: no beep followed this branch at all — the disarm
                 * beep belongs to do_disarm_and_idle(), which is not on this
                 * path (we never entered ARMED). */
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("ARM CANCELLED");
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
                show_nack(nack_reason);
            } else if (result == -2) {
                /* Channel mismatch. The base ACKed a channel the operator did
                 * not select, so we refuse to enter ARMED and tell it to stand
                 * down — but say so on the display too. Found by T-A13 on
                 * 2026-08-26 (fault-injection build): the disarm worked and the
                 * relay dropped, yet the operator saw only a triple beep, with
                 * a base that had briefly armed a channel nobody chose. That is
                 * precisely the case that needs naming, and FSD §15.2 T-A13
                 * expects it named. */
                ESP_LOGE(TAG, "ARM ACK channel mismatch");
                send_cmd_disarm(ch);
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("CHANNEL MISMATCH ERROR");
            } else if (result == WAIT_FOR_ACK_STATE_HANDLED) {
                /* R1: state already transitioned to LINK_LOST or ERROR — do nothing */
            } else if (key_off_abort) {
                /* MIN-09: the operator ended it, not the link. */
                send_cmd_disarm(ch);
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("ARM KEY OFF - ARM CANCELLED");
            } else {
                /* Timeout. Every other outcome above tells the operator
                 * something; until 2026-08-26 this one only wrote a log line,
                 * so a base that never answered looked identical to a
                 * long-press that had not registered. */
                ESP_LOGW(TAG, "ARM failed — no response from base");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("NO RESPONSE FROM BASE");
            }

        } else if (evt->type == EVT_ENCODER_SHORT_PRESS) {
            /* RM-01 / FSD §8.2.3: a short press in IDLE is the operator
             * reaching for ARM the wrong way. Answer it — the event used to
             * be dropped entirely, so the only thing telling anyone how to
             * arm was a permanent banner. */
            display_toast("HOLD TO ARM");
        } else if (evt->type == EVT_FIRE_BUTTON_PRESSED) {
            /* Refused in IDLE (FSD §8.2.3) — but say so.
             *
             * This was a bare "ignored" comment: the press did nothing and the
             * operator was told nothing, which is indistinguishable from a dead
             * button. §7.2.9a exists for exactly this, and the natural response
             * to apparent non-response is to press harder or try again — the
             * wrong instinct at a pad. */
            ESP_LOGW(TAG, "FIRE pressed while not armed — refused");
            buzzer_play(BUZZER_BEEP_TRIPLE);
            display_toast("NOT ARMED - ARM FIRST");
        } else if (evt->type == EVT_ARM_SWITCH_CHANGED && evt->data.arm_state.armed) {
            /* Arm switch turned ON. Nothing here starts the arming sequence —
             * that needs the encoder long-press — but if an input is already
             * held the operator is walking the sequence out of order and gets
             * no indication of it, because the held input will simply be
             * discarded later. Name it at the moment it becomes wrong.
             *
             * The switch itself is left ON: it is a physical switch and the
             * firmware cannot move it, so pretending otherwise would put the
             * display out of step with the panel. The refusal that matters is
             * on the ARM, above. */
            if (fire_button_is_pressed()) {
                ESP_LOGW(TAG, "arm switch ON with fire button held");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("RELEASE FIRE BUTTON FIRST");
            } else if (encoder_button_is_pressed()) {
                ESP_LOGW(TAG, "arm switch ON with encoder held");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("RELEASE ENCODER FIRST");
            }
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
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("BASE STATE MISMATCH - DISARMED");
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
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("ARM KEY OFF - FIRE REFUSED");
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
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("BASE NOT ARMED - FIRE REFUSED");
                do_disarm_and_idle();
                break;
            }

            /* M6: Guard 2: STATUS_UPDATE must be fresh (FSD §8.2.4 guard 1) */
            if (!is_status_fresh()) {
                ESP_LOGW(TAG, "FIRE rejected: stale STATUS_UPDATE");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("NO BASE STATUS - FIRE REFUSED");
                do_disarm_and_idle();
                break;
            }

            /* Guard 3: link healthy */
            if (!rlc_link_is_healthy()) {
                ESP_LOGW(TAG, "FIRE rejected: link not healthy");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("LINK DEGRADED - FIRE REFUSED");
                do_disarm_and_idle();
                break;
            }

            /* Send CMD_FIRE */
            if (send_cmd_fire(s_armed_channel) != 0) {
                ESP_LOGE(TAG, "CMD_FIRE send failed");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("FIRE SEND FAILED");
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
                    buzzer_play(BUZZER_BEEP_TRIPLE);
                    display_toast("ARM KEY OFF - FIRE ABORTED");
                    do_disarm_and_idle();
                } else {
                    /* ACK — enter PRE_FIRE */
                    s_base_reached_firing = false;   /* MAJ-02: this shot only */
                    set_prefire_start(now_ms());
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
                buzzer_play(BUZZER_BEEP_TRIPLE);
                show_nack(nack_reason);
                do_disarm_and_idle();
            } else if (result == WAIT_FOR_ACK_INTERRUPTED) {
                /* RM-03: the operator ended this attempt themselves — arm key
                 * off, fire button released, or encoder touched. The behaviour
                 * (cease fire, stand down) is the same as a timeout, but the
                 * message must not be: "NO RESPONSE - FIRE ABORTED" blamed the
                 * base for something the operator did, and sent people looking
                 * for a link fault that was not there. The ARM path has had
                 * this branch since 2.4; the FIRE path did not. */
                ESP_LOGW(TAG, "FIRE attempt interrupted by operator — aborting");
                send_cmd_cease_fire();
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("FIRE CANCELLED");
                do_disarm_and_idle();
            } else if (result == -2) {
                /* MIN-05: the base ACKed a channel we did not ask to fire.
                 * The ARM path names this; the FIRE path used to let it fall
                 * into the generic "NO RESPONSE" branch and blame the link. */
                ESP_LOGE(TAG, "FIRE ACK channel mismatch");
                send_cmd_cease_fire();
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("CHANNEL MISMATCH - FIRE ABORTED");
                do_disarm_and_idle();
            } else if (result == WAIT_FOR_ACK_STATE_HANDLED) {
                /* R1: LINK_LOST or BATTERY_CRITICAL was handled inline by
                 * wait_for_ack(); state has already transitioned and we MUST
                 * NOT call do_disarm_and_idle() (which would stomp the alarm
                 * state back to IDLE and silence the operator alert). */
            } else {
                /* Timeout. The base never answered a FIRE — the operator is
                 * holding the button watching nothing happen, which is the
                 * last moment to leave them guessing. */
                ESP_LOGW(TAG, "FIRE failed — no response from base");
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("NO RESPONSE - FIRE ABORTED");
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
                /* RM-07 / FSD §12.1: when the same frame shows the channel
                 * OPEN, the disarm was the base's continuity-loss disarm
                 * (§7.2.7) — the igniter left the circuit. Say so, with the
                 * distinctive BEEP_CONTINUITY_LOST pattern. Previously every
                 * base-initiated disarm produced the same BEEP_LONG, so a
                 * disconnected igniter was indistinguishable from an arm
                 * timeout, and BEEP_CONTINUITY_LOST was never played at all. */
                bool cont_lost = (status_continuity_band(s_armed_channel) == CONT_OPEN);
                ESP_LOGW(TAG, "STATUS_UPDATE shows base disarmed%s",
                         cont_lost ? " (continuity OPEN)" : "");
                display_toast(cont_lost ? "CONTINUITY LOST - DISARMED"
                                        : "BASE DISARMED");
                s_armed_channel = 0;
                rlc_rgb_led_set_pattern(LED_PATTERN_STATUS);
                buzzer_play(cont_lost ? BUZZER_BEEP_CONTINUITY_LOST
                                      : BUZZER_BEEP_LONG);
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
            if (s_last_status.base_state == STATE_ERROR ||
                s_last_status.base_state == STATE_LINK_LOST) {
                /* MAJ-01: name the fault rather than "ENDED SEQUENCE" — a base
                 * that needs a power cycle is not the same event as an abort. */
                report_base_fault_end();
            } else if (s_last_status.base_state != STATE_PRE_FIRE &&
                       s_last_status.base_state != STATE_FIRING &&
                       s_last_status.base_state != STATE_ARMED) {
                ESP_LOGW(TAG, "Base left PRE_FIRE/FIRING — sync to base");
                /* MAJ-06: this was the weakest of the abort indications —
                 * the firing tone simply stopped. §7.2.9a wants both halves. */
                s_fire_repeat_active = false;
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast("BASE ENDED SEQUENCE");
                do_enter_idle();
            }
        } else if (evt->type == EVT_CMD_NACK &&
                   evt->data.nack.nacked_msg_type == MSG_CMD_FIRE) {
            /* MAJ-03: see the FIRING branch — a NACK for a fire repeat means
             * the base is no longer on the firing path, ~200 ms after the
             * fact instead of up to 2 s. */
            if (evt->data.nack.reason_code == NACK_BASE_BUSY) {
                /* MIN-04: a queue-full refusal says the base could not take
                 * *this frame*, not that it has left the firing path — it is
                 * still counting the dead-man from the last repeat it did
                 * take, and the next one is 200 ms away. Do not end the
                 * sequence on it. */
                ESP_LOGW(TAG, "FIRE repeat dropped at the base (queue full)");
                break;
            }
            ESP_LOGW(TAG, "FIRE repeat NACKed (0x%02x) during PRE_FIRE",
                     evt->data.nack.reason_code);
            s_fire_repeat_active = false;
            buzzer_play(BUZZER_BEEP_TRIPLE);
            show_nack(evt->data.nack.reason_code);
            do_enter_idle();
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
            /* Button released -> CEASE_FIRE (FSD §8.2.6).
             *
             * This returned to IDLE silently, which loses the one fact the
             * operator most needs to carry to the pad: the channel WAS
             * energised, just for less than the full pulse. A silent return
             * looks identical to an abort during the countdown, where no
             * current ever reached the igniter — and those two demand very
             * different behaviour when someone walks out to the rail.
             *
             * "PULSE CUT SHORT" describes what happened without asserting what
             * it means. Whether the igniter took is not knowable from here;
             * the main screen's continuity grid answers that, live, as soon as
             * this toast clears. */
            ESP_LOGI(TAG, "Fire button released — CEASE_FIRE");
            s_fire_repeat_active = false;
            send_cmd_cease_fire();
            {
                /* overlay_post() copies the string, so a stack buffer is fine. */
                char tbuf[40];
                /* MAJ-02: only claim the channel was energised when a
                 * STATUS_UPDATE actually showed the base in FIRING. The local
                 * FIRING entry is this unit's own countdown expiring and says
                 * nothing about the pad. */
                snprintf(tbuf, sizeof(tbuf),
                         s_base_reached_firing ? "CH %u PULSE CUT SHORT"
                                               : "CH %u ENDED - NOT CONFIRMED",
                         s_armed_channel);
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast(tbuf);
            }
            do_enter_idle();
        } else if (evt->type == EVT_ARM_SWITCH_CHANGED && !evt->data.arm_state.armed) {
            /* Same situation, different input — and it was equally silent.
             * Named separately so the operator knows which one ended it. */
            ESP_LOGI(TAG, "Arm switch OFF during FIRING — CEASE_FIRE");
            s_fire_repeat_active = false;
            send_cmd_cease_fire();
            {
                char tbuf[40];
                snprintf(tbuf, sizeof(tbuf),
                         s_base_reached_firing ? "CH %u CUT SHORT - ARM OFF"
                                               : "CH %u ENDED - NOT CONFIRMED",
                         s_armed_channel);
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_toast(tbuf);
            }
            do_enter_idle();
        } else if (evt->type == EVT_STATUS_UPDATE) {
            cache_status(&evt->data.status_update.status);

            /* The base has left the firing path. Two very different things
             * end that way and this used to treat them as one.
             *
             * A COMPLETED pulse runs FIRING -> POST_FIRE -> IDLE. A pulse the
             * BASE cut short — the pad key turned to SAFE, arm sense lost,
             * continuity lost — goes FIRING -> IDLE directly. Both are seen
             * here as base_state IDLE, so the old test announced "Fire
             * complete" for a shot that was interrupted, and put the FIRE
             * COMPLETE screen up over it. Reported from the bench: turning the
             * base key off mid-pulse showed FIRE COMPLETE for a pulse cut at
             * 550 ms of 1000. Claiming a shot completed when it did not is
             * worse than saying nothing.
             *
             * POST_FIRE alone is not a safe discriminator: STATUS_UPDATE_
             * INTERVAL_MS and POST_FIRE_COOLDOWN_MS are both 2000 ms, so the
             * remote can miss the POST_FIRE window entirely and see only IDLE.
             * Local elapsed time is a backstop that needs no packet to land in
             * a particular window; POST_FIRE is accepted as a positive
             * confirmation when it does arrive. See report_firing_outcome().
             *
             * MAJ-01: this is a blacklist, not a whitelist. It used to act
             * only on POST_FIRE/IDLE, so a base that went to terminal ERROR
             * mid-pulse (arm-relay weld fault, battery critical) left the
             * remote in FIRING indefinitely — "IGNITION ACTIVE", firing tone,
             * and 5 Hz CMD_FIRE repeats over a pad that is safe but broken,
             * with its STATUS_UPDATEs still arriving so the stale-data
             * backstop never ran either. Any base state off the firing path
             * ends the sequence; PRE_FIRE's branch has always worked this way. */
            if (s_last_status.base_state == STATE_ERROR ||
                s_last_status.base_state == STATE_LINK_LOST) {
                report_base_fault_end();
            } else if (s_last_status.base_state != STATE_PRE_FIRE &&
                       s_last_status.base_state != STATE_FIRING) {
                report_firing_outcome();
            }
        } else if (evt->type == EVT_CMD_NACK &&
                   evt->data.nack.nacked_msg_type == MSG_CMD_FIRE) {
            /* MAJ-03: heed the NACKs answering our repeats. §8.4 forbids the
             * base answering a CMD_FIRE *while it is on the firing path*, so a
             * NACK for one can only mean it has left — and it arrives within
             * ~200 ms, against the 2 s a STATUS_UPDATE takes. This is the
             * fastest abort signal the remote has, and it used to be discarded
             * (EVT_CMD_NACK was consumed only inside wait_for_ack()). */
            if (evt->data.nack.reason_code == NACK_BASE_BUSY) {
                /* MIN-04: see PRE_FIRE — a dropped frame is not a departure
                 * from the firing path, and ending a live pulse on one would
                 * be a false abort mid-shot. */
                ESP_LOGW(TAG, "FIRE repeat dropped at the base (queue full)");
                break;
            }
            ESP_LOGW(TAG, "FIRE repeat NACKed (0x%02x) — base left the "
                          "firing path", evt->data.nack.reason_code);
            s_fire_repeat_active = false;
            if (evt->data.nack.reason_code == NACK_BASE_ERROR) {
                char tbuf[40];
                base_error_toast(tbuf, sizeof(tbuf));
                buzzer_play(BUZZER_BEEP_TRIPLE);
                display_nack(tbuf);
                do_enter_idle();
            } else {
                report_firing_outcome();
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
            /* RM-02: a recovery may be onto a fresh session with a different
             * base — re-adopt the advertised channel count. */
            encoder_set_max_channel(rlc_link_get_peer_num_channels());
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
    /* RM-07 / FSD §12.1 BEEP_PING_FAIL. Played on the *edge* into a degraded
     * link, not per missed ping: at a 500 ms heartbeat a per-ping beep would
     * be a continuous rattle through exactly the condition the operator needs
     * to hear other alerts during. One 80 ms chirp says "the link just went
     * bad"; ALARM_LINK_LOST takes over if it goes all the way down. Until now
     * this pattern was implemented and never played. */
    {
        static bool s_link_was_healthy = true;
        bool healthy = rlc_link_is_healthy();
        if (s_link_was_healthy && !healthy &&
            s_state != STATE_LINK_LOST && s_state != STATE_ERROR) {
            ESP_LOGW(TAG, "link degraded — ping failure rate over threshold");
            buzzer_play(BUZZER_BEEP_PING_FAIL);
        }
        s_link_was_healthy = healthy;
    }

    /* State tone (FSD §12.1). Driven from the tick rather than on transitions,
     * for the same reason fire_button_set_live() is: the base dropping out
     * underneath an ARMED remote arrives as a STATUS_UPDATE, not as a local
     * state change, and a missed transition would leave the pad sounding armed
     * when it is not. buzzer_set_background() is idempotent, so calling it
     * every tick does not restart the pattern.
     *
     * Set as a BACKGROUND rather than played: ARMED is full of one-shot beeps
     * — the arm-confirm double, and a triple from every FIRE guard refusal —
     * and a plain buzzer_play() tone would be replaced by the first of them
     * and never return. */
    switch (s_state) {
        case STATE_ARMED:
            buzzer_set_background(BUZZER_ALARM_ARMED);
            break;
        case STATE_PRE_FIRE:
        case STATE_FIRING:
            /* PRE_FIRE shares the firing tone deliberately: the countdown is
             * the part the operator must hear starting, and it is where an
             * abort is still free. */
            buzzer_set_background(BUZZER_ALARM_FIRING);
            break;
        default:
            /* LINK_LOST and ERROR play their own repeating alarms through the
             * normal path; leaving the background clear lets those stand. */
            buzzer_set_background(BUZZER_OFF);
            break;
    }

    /* PRE_FIRE -> FIRING: local countdown elapsed (FSD §8.2.5) */
    if (s_state == STATE_PRE_FIRE && s_prefire_start_ms > 0) {
        if ((now_ms() - s_prefire_start_ms) >= PRE_FIRE_DELAY_MS) {
            ESP_LOGI(TAG, "PRE_FIRE -> FIRING (local countdown elapsed)");
            rlc_rgb_led_set_pattern(LED_PATTERN_FIRING);
            /* Needed to tell a completed pulse from one the base cut short —
             * both end with base_state == STATE_IDLE. See STATE_FIRING's
             * STATUS_UPDATE branch. */
            s_firing_start_ms = now_ms();
            s_state = STATE_FIRING;
        }
    }

    /* Stale data safety timeout in IDLE/ARMED/PRE_FIRE/FIRING */
    if (s_state != STATE_BOOT && s_state != STATE_LINK_LOST &&
        s_state != STATE_ERROR && s_last_status_rx_ms > 0) {
        if ((now_ms() - s_last_status_rx_ms) > STATUS_STALE_TIMEOUT_MS) {
            ESP_LOGW(TAG, "STATUS_UPDATE stale timeout (%lld ms)",
                     now_ms() - s_last_status_rx_ms);
            buzzer_play(BUZZER_BEEP_TRIPLE);
            display_toast("BASE STATUS LOST");
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
            /* RM-06: the physical button is checked too. Release arrives as
             * EVT_FIRE_BUTTON_RELEASED, which the FSM task must dequeue before
             * it clears s_fire_repeat_active — up to one more CMD_FIRE could
             * leave this unit after the operator had already let go. Benign
             * (the base's dead-man and the following CEASE_FIRE both cover
             * it), but "the button is up" is the authoritative fact here. */
            uint8_t ch = s_armed_channel;
            if (ch > 0 && s_fire_repeat_active && arm_switch_is_armed() &&
                fire_button_is_pressed()) {
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

        /* Fire ring LED, every tick rather than on transitions alone: it must
         * also follow the base dropping out underneath an ARMED remote, which
         * arrives as a STATUS_UPDATE, not as a local state change. The setter
         * latches, so this only touches GPIO when it actually changes. */
        fire_button_set_live(fire_is_live());

        esp_task_wdt_reset();
    }
}
