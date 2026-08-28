/* Host harness for the base unit's safety FSM (TT-04).
 *
 * FSD §4.5 requires the state machines to be testable by event injection.
 * Until this file existed there were ZERO automated tests for either FSM: the
 * dead-man guard, the arm-verify window, the continuity-loss disarm and its
 * bug-#30 level backstop, and every FIRE guard were covered only by manual
 * bench tests — several of which are physically unreachable as written
 * (T-F06/F07/F09: a 1 s pulse is shorter than the 1.5 s link-loss detection).
 *
 * The FSM is already queue-driven and single-task-owner, so it can be driven
 * off-target directly: this file #includes rlc_base_fsm.c and calls its
 * static process_event()/check_timers() with synthesised rlc_fsm_event_t
 * sequences, against fakes for every peripheral it touches. Those fakes record
 * what the FSM did (relays, siren, fire timer, ACK/NACKs) so behaviour is
 * asserted, not just "did not crash".
 *
 * What this discharges: §4.5 event-injection testability, the "verify by code
 * review" substitute agreed for T-F06/T-F07/T-F09 and T-S12/T-S13, T-U04
 * (arming guards), T-U09/T-U16, and positive verification of bug #30.
 *
 * Base-unit only — under the REMOTE build this compiles to a SKIPPED stub,
 * the same convention test_encoder.c uses in the other direction.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

int64_t g_mock_us = 0;    /* backs the esp_timer stub */

#ifdef CONFIG_RLC_UNIT_BASE

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "rlc_protocol.h"
#include "rlc_config.h"
#include "rlc_rgb_led.h"
#include "rlc_continuity_class.h"

/* ── Recorded peripheral state (the fakes below drive these) ─────── */

static struct {
    bool    arm_relay_on;
    bool    fire_relay_on[NUM_CHANNELS];
    int     all_safe_calls;

    bool    siren_on;               /* continuous */
    int     siren_link_lost_calls;
    int     siren_error_calls;
    int     siren_continuity_calls;
    int     siren_off_calls;

    bool    fire_timer_running;
    int     fire_timer_starts;
    int     fire_timer_stops;
    uint8_t fire_timer_channel;
    int     fire_timer_fail_next;   /* >0: next start returns an error */

    int     status_triggers;

    /* Last ACK / NACK the FSM emitted */
    int     ack_count, nack_count;
    uint8_t last_ack_type, last_ack_channel;
    uint8_t last_nack_type, last_nack_reason;
} hw;

/* Inputs the FSM reads back from the world. */
static struct {
    bool    key_sense;
    bool    arm_sense;
    uint8_t band[NUM_CHANNELS];     /* rlc_continuity_band_t per channel */
    uint16_t vbat_mv;
    bool    link_healthy;
    int64_t last_contact_ms;        /* -1 = never */
} in;

/* ── Fakes for everything rlc_base_fsm.c calls ───────────────────── */

/* relays */
void relay_init(void) {}
void relay_fire_set(uint8_t ch, bool on)
{ if (ch >= 1 && ch <= NUM_CHANNELS) hw.fire_relay_on[ch - 1] = on; }
void relay_fire_all_off(void)
{ for (int i = 0; i < NUM_CHANNELS; i++) hw.fire_relay_on[i] = false; }
void arm_relay_set(bool on) { hw.arm_relay_on = on; }
bool arm_relay_get_intended(void) { return hw.arm_relay_on; }
void relay_all_safe(void)
{ hw.all_safe_calls++; arm_relay_set(false); relay_fire_all_off(); }

/* siren */
void siren_init(void) {}
void siren_start_continuous(void)      { hw.siren_on = true; }
void siren_start_link_lost(void)       { hw.siren_link_lost_calls++; hw.siren_on = false; }
void siren_start_error(void)           { hw.siren_error_calls++; hw.siren_on = false; }
void siren_start_continuity_lost(void) { hw.siren_continuity_calls++; hw.siren_on = false; }
void siren_off(void)                   { hw.siren_off_calls++; hw.siren_on = false; }

/* fire timer */
void fire_timer_init(void) {}
esp_err_t fire_timer_start(uint32_t ms, uint8_t ch, TaskHandle_t t)
{
    (void)ms; (void)t;
    if (hw.fire_timer_fail_next > 0) { hw.fire_timer_fail_next--; return ESP_FAIL; }
    hw.fire_timer_starts++;
    hw.fire_timer_running = true;
    hw.fire_timer_channel = ch;
    return ESP_OK;
}
void fire_timer_stop(void) { hw.fire_timer_stops++; hw.fire_timer_running = false; }

/* arm / key sense */
void arm_sense_init(void) {}
void arm_sense_start_task(void) {}
bool arm_sense_get_debounced(void) { return in.arm_sense; }
bool key_sense_get_debounced(void) { return in.key_sense; }
void arm_sense_register_cb(void (*cb)(bool)) { (void)cb; }
void arm_sense_register_fault_cb(void (*cb)(void)) { (void)cb; }
void key_sense_register_cb(void (*cb)(bool)) { (void)cb; }

/* continuity */
rlc_continuity_band_t continuity_get_channel(uint8_t ch)
{
    if (ch < 1 || ch > NUM_CHANNELS) return CONT_OPEN;
    return (rlc_continuity_band_t)in.band[ch - 1];
}
uint16_t continuity_get_bands(void)
{
    uint16_t p = 0;
    for (int i = 0; i < NUM_CHANNELS; i++) p |= (uint16_t)in.band[i] << (i * 2);
    return p;
}

/* battery */
uint16_t rlc_battery_get_voltage_mv(void) { return in.vbat_mv; }

/* status update */
void status_update_trigger(void) { hw.status_triggers++; }

/* rgb led */
void rlc_rgb_led_set_pattern(rlc_led_pattern_t p) { (void)p; }

/* link */
bool rlc_link_is_healthy(void) { return in.link_healthy; }
int64_t rlc_link_ms_since_contact(void) { return in.last_contact_ms; }
uint32_t rlc_link_next_seq(void) { static uint32_t s = 0; return ++s; }
int rlc_link_send_cmd(uint8_t type, uint32_t seq, const void *p, uint16_t len)
{
    (void)seq; (void)len;
    if (type == MSG_CMD_ACK) {
        const rlc_payload_cmd_ack_t *a = p;
        hw.ack_count++;
        hw.last_ack_type = a->acked_msg_type;
        hw.last_ack_channel = a->channel;
    } else if (type == MSG_CMD_NACK) {
        const rlc_payload_cmd_nack_t *n = p;
        hw.nack_count++;
        hw.last_nack_type = n->nacked_msg_type;
        hw.last_nack_reason = n->reason_code;
    }
    return 0;
}

/* fault injection: rlc_faultinject.h already provides inert static inlines in
 * a non-injection build, so nothing to fake here. */

/* watchdog */
int rlc_watchdog_init(void) { return 0; }
void rlc_watchdog_feed(void) {}
int rlc_watchdog_register_self(void) { return 0; }

/* The unit under test. */
#include "rlc_base_fsm.c"

/* ── Test scaffolding ────────────────────────────────────────────── */

static int checks = 0, fails = 0;

static void expect(const char *what, bool cond)
{
    checks++;
    if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static void expect_state(const char *what, rlc_state_t want)
{
    checks++;
    if (s_state != want) {
        printf("  FAIL %-52s state %d, want %d\n", what, s_state, want);
        fails++;
    }
}

static void expect_nack(const char *what, uint8_t reason)
{
    checks++;
    if (hw.last_nack_reason != reason) {
        printf("  FAIL %-52s NACK 0x%02x, want 0x%02x\n",
               what, hw.last_nack_reason, reason);
        fails++;
    }
}

static void advance_ms(int64_t ms) { g_mock_us += ms * 1000; }

/* Reset the FSM and the world to "linked, key on, everything healthy, IDLE". */
static void reset_world(void)
{
    memset(&hw, 0, sizeof(hw));
    memset(&in, 0, sizeof(in));
    in.key_sense = true;
    in.arm_sense = false;
    for (int i = 0; i < NUM_CHANNELS; i++) in.band[i] = CONT_CONNECTED;
    in.vbat_mv = 12000;
    in.link_healthy = true;
    in.last_contact_ms = 100;
    g_mock_us = 1000000;

    s_state = STATE_IDLE;
    s_armed_channel = 0;
    s_firing_channel = 0;
    s_error_flags = 0;
    s_arm_time_ms = 0;
    s_prefire_start_ms = 0;
    s_postfire_start_ms = 0;
    s_firing_start_ms = 0;
    s_arm_verify_pending = false;
    s_arm_verify_channel = 0;
    s_arm_verify_start_ms = 0;
    s_arm_verify_seq = 0;
    s_arm_verify_timeouts = 0;   /* MIN-02 strike counter */
    s_link_lost_pending = false;
    s_last_fire_cmd_ms = 0;
}

static void post(uint32_t type)
{
    rlc_fsm_event_t e = {0};
    e.type = type;
    process_event(&e);
}

static void post_cmd(uint32_t type, uint8_t ch)
{
    rlc_fsm_event_t e = {0};
    e.type = type;
    e.data.cmd.channel = ch;
    e.data.cmd.seq_number = 42;
    e.data.cmd.received_ms = now_ms();
    process_event(&e);
}

static void post_bool(uint32_t type, bool armed)
{
    rlc_fsm_event_t e = {0};
    e.type = type;
    e.data.arm_state.armed = armed;
    process_event(&e);
}

static void post_cont(uint8_t ch, uint8_t band)
{
    rlc_fsm_event_t e = {0};
    e.type = EVT_CONTINUITY_CHANGED;
    e.data.continuity.channel = ch;
    e.data.continuity.band = band;
    process_event(&e);
}

/* Drive IDLE -> ARMED on `ch` with the arm sense already HIGH. */
static void arm_now(uint8_t ch)
{
    in.arm_sense = true;
    post_cmd(EVT_CMD_ARM, ch);
}

/* Drive ARMED -> PRE_FIRE -> FIRING, honouring all the real guards. */
static void fire_now(uint8_t ch)
{
    post_cmd(EVT_CMD_FIRE, ch);            /* -> PRE_FIRE */
    advance_ms(PRE_FIRE_DELAY_MS - 100);
    check_timers();
    /* Keep the dead-man fresh across the countdown, as the remote's repeat
     * task does at FIRE_REPEAT_INTERVAL_MS. */
    post_cmd(EVT_CMD_FIRE, ch);
    advance_ms(200);
    check_timers();                        /* -> FIRING */
}

/* ── Tests ───────────────────────────────────────────────────────── */

/* T-U04 / FSD §7.2.2: every arming guard, in the order the spec lists them. */
static void t_arm_guards(void)
{
    printf("T-FSM01 arming guards (FSD 7.2.2)\n");

    reset_world();
    post_cmd(EVT_CMD_ARM, 0);
    expect_nack("ARM ch 0 -> INVALID_CHANNEL", NACK_INVALID_CHANNEL);
    expect_state("ARM ch 0 stays IDLE", STATE_IDLE);

    reset_world();
    post_cmd(EVT_CMD_ARM, NUM_CHANNELS + 1);
    expect_nack("ARM ch 9 -> INVALID_CHANNEL", NACK_INVALID_CHANNEL);

    reset_world();
    arm_now(1);
    expect_state("valid ARM -> ARMED", STATE_ARMED);
    post_cmd(EVT_CMD_ARM, 2);
    expect_nack("second ARM -> CHANNEL_ALREADY_ARMED (T-A05)",
                NACK_CHANNEL_ALREADY_ARMED);
    expect("still armed on ch 1", s_armed_channel == 1);

    reset_world();
    in.key_sense = false;
    post_cmd(EVT_CMD_ARM, 1);
    expect_nack("key OFF -> BASE_SWITCH_OFF", NACK_BASE_SWITCH_OFF);

    reset_world();
    in.band[0] = CONT_OPEN;
    post_cmd(EVT_CMD_ARM, 1);
    expect_nack("continuity OPEN -> NO_CONTINUITY", NACK_NO_CONTINUITY);

    reset_world();
    in.band[0] = CONT_MARGINAL;
    arm_now(1);
    expect_state("MARGINAL still arms (only OPEN blocks)", STATE_ARMED);

    /* T-U07: the battery *gating* behaviour, not just the sampling. */
    reset_world();
    in.vbat_mv = BASE_VBAT_MIN_ARM_MV - 1;
    post_cmd(EVT_CMD_ARM, 1);
    expect_nack("VBAT below arm floor -> LOW_BATTERY (T-U07)", NACK_LOW_BATTERY);

    reset_world();
    in.link_healthy = false;
    post_cmd(EVT_CMD_ARM, 1);
    expect_nack("degraded link -> COMM_DEGRADED", NACK_COMM_DEGRADED);
}

/* FSD §7.2.2 M1 arm-verify window: the arm relay is already energised inside
 * it, so everything that can cancel an ARM must cancel it there too. */
static void t_arm_verify_window(void)
{
    printf("T-FSM02 arm-verify window\n");

    reset_world();
    in.arm_sense = false;
    post_cmd(EVT_CMD_ARM, 1);
    expect("verify pending", s_arm_verify_pending);
    expect("arm relay energised during verify", hw.arm_relay_on);
    expect_state("still IDLE during verify", STATE_IDLE);

    in.arm_sense = true;
    post_bool(EVT_ARM_SENSE_CHANGED, true);
    expect_state("sense HIGH completes ARM", STATE_ARMED);
    expect("armed channel latched", s_armed_channel == 1);

    /* Timeout path */
    reset_world();
    in.arm_sense = false;
    post_cmd(EVT_CMD_ARM, 1);
    advance_ms(ARM_SENSE_VERIFY_TIMEOUT_MS + 1);
    check_timers();
    expect_nack("verify timeout -> ARM_SENSE_FAULT", NACK_ARM_SENSE_FAULT);
    expect("relays safe after verify timeout", !hw.arm_relay_on);
    expect_state("verify timeout stays IDLE", STATE_IDLE);
    expect("first timeout latches no error flag", s_error_flags == 0);

    /* MIN-02 / §7.2.2 escalation: the SECOND consecutive timeout is a relay,
     * fuse or wiring fault — ERR_RELAY_FAULT and terminal ERROR. */
    post_cmd(EVT_CMD_ARM, 1);
    advance_ms(ARM_SENSE_VERIFY_TIMEOUT_MS + 1);
    check_timers();
    expect_nack("second timeout still NACKs 0x0B", NACK_ARM_SENSE_FAULT);
    expect("second timeout sets ERR_RELAY_FAULT",
           (s_error_flags & ERR_RELAY_FAULT) != 0);
    expect_state("second timeout -> ERROR", STATE_ERROR);
    expect("relays safe in ERROR", !hw.arm_relay_on);

    /* A successful verify in between clears the strike count, so a slow relay
     * that arms on the retry never escalates. */
    reset_world();
    in.arm_sense = false;
    post_cmd(EVT_CMD_ARM, 1);
    advance_ms(ARM_SENSE_VERIFY_TIMEOUT_MS + 1);
    check_timers();
    expect_state("strike 1 stays IDLE", STATE_IDLE);
    in.arm_sense = true;
    post_cmd(EVT_CMD_ARM, 1);
    expect_state("retry arms (sense already HIGH)", STATE_ARMED);
    post_cmd(EVT_CMD_DISARM, 1);
    in.arm_sense = false;
    post_cmd(EVT_CMD_ARM, 1);
    advance_ms(ARM_SENSE_VERIFY_TIMEOUT_MS + 1);
    check_timers();
    expect_state("timeout after a success is strike 1 again", STATE_IDLE);
    expect("no error flag after success-cleared strike", s_error_flags == 0);

    /* Key off inside the window (fix 4.6) */
    reset_world();
    in.arm_sense = false;
    post_cmd(EVT_CMD_ARM, 1);
    in.key_sense = false;
    post_bool(EVT_KEY_SWITCH_CHANGED, false);
    expect_nack("key OFF in window -> BASE_SWITCH_OFF", NACK_BASE_SWITCH_OFF);
    expect("verify cancelled", !s_arm_verify_pending);

    /* DISARM inside the window (fix 2.1) */
    reset_world();
    in.arm_sense = false;
    post_cmd(EVT_CMD_ARM, 1);
    post_cmd(EVT_CMD_DISARM, 1);
    expect("DISARM cancels pending verify", !s_arm_verify_pending);
    expect("arm relay released", !hw.arm_relay_on);

    /* Bug #30: continuity lost during the window must refuse the ARM even
     * though the EVT_CONTINUITY_CHANGED edge was dropped (IDLE does not
     * handle it), because the level is re-read at completion. */
    reset_world();
    in.arm_sense = false;
    post_cmd(EVT_CMD_ARM, 1);
    in.band[0] = CONT_OPEN;             /* edge lost — no event posted */
    in.arm_sense = true;
    post_bool(EVT_ARM_SENSE_CHANGED, true);
    expect_state("bug #30: OPEN during verify refuses ARM", STATE_IDLE);
    expect_nack("bug #30 entry re-check -> NO_CONTINUITY", NACK_NO_CONTINUITY);
    expect("bug #30: relays safe", !hw.arm_relay_on);
}

/* FSD §7.2.7 (v1.35) + bug #30 backstop: armed channel OPEN disarms. */
static void t_continuity_loss_disarm(void)
{
    printf("T-FSM03 continuity-loss disarm (FSD 7.2.7, bug #30)\n");

    /* Event path, ARMED */
    reset_world();
    arm_now(1);
    in.band[0] = CONT_OPEN;
    post_cont(1, CONT_OPEN);
    expect_state("ARMED + ch OPEN -> IDLE", STATE_IDLE);
    expect("relays safe", !hw.arm_relay_on);
    expect("BF-03: continuity-lost siren sounded", hw.siren_continuity_calls == 1);

    /* Only the ARMED channel counts */
    reset_world();
    arm_now(1);
    in.band[1] = CONT_OPEN;
    post_cont(2, CONT_OPEN);
    expect_state("other channel OPEN does not disarm", STATE_ARMED);

    /* Only OPEN counts */
    reset_world();
    arm_now(1);
    in.band[0] = CONT_MARGINAL;
    post_cont(1, CONT_MARGINAL);
    expect_state("MARGINAL is informational", STATE_ARMED);

    /* Event path, PRE_FIRE */
    reset_world();
    arm_now(1);
    post_cmd(EVT_CMD_FIRE, 1);
    expect_state("FIRE -> PRE_FIRE", STATE_PRE_FIRE);
    in.band[0] = CONT_OPEN;
    post_cont(1, CONT_OPEN);
    expect_state("PRE_FIRE + ch OPEN -> IDLE", STATE_IDLE);

    /* Bug #30 level backstop: the edge never arrives at all. */
    reset_world();
    arm_now(1);
    in.band[0] = CONT_OPEN;             /* no event posted — edge lost */
    check_timers();
    expect_state("bug #30 backstop disarms without an event", STATE_IDLE);
    expect("bug #30 backstop sounds the siren", hw.siren_continuity_calls == 1);

    /* ...and is scoped OUT of FIRING, where the armed channel's sense line
     * reads OPEN by design (relay on NO). Aborting there would kill every
     * pulse the instant it started. */
    reset_world();
    arm_now(1);
    fire_now(1);
    expect_state("reached FIRING", STATE_FIRING);
    in.band[0] = CONT_OPEN;
    check_timers();
    expect_state("FIRING is not disarmed by OPEN", STATE_FIRING);
    post_cont(1, CONT_OPEN);
    expect_state("FIRING ignores the OPEN event too", STATE_FIRING);
}

/* FSD §7.2.4: the PRE_FIRE -> FIRING guards. */
static void t_prefire_guards(void)
{
    printf("T-FSM04 PRE_FIRE -> FIRING guards (FSD 7.2.4)\n");

    /* Dead-man: no CMD_FIRE inside FIRE_AUTHORIZATION_TIMEOUT_MS -> abort. */
    reset_world();
    arm_now(1);
    post_cmd(EVT_CMD_FIRE, 1);
    advance_ms(PRE_FIRE_DELAY_MS + 1);   /* no refresh in between */
    check_timers();
    expect_state("dead-man timeout aborts to IDLE", STATE_IDLE);
    expect("no fire relay closed", !hw.fire_relay_on[0]);
    expect("fire timer never started", hw.fire_timer_starts == 0);

    /* Wrong-channel CMD_FIRE must NOT refresh the dead-man (FSD §7.2.3). */
    reset_world();
    arm_now(1);
    post_cmd(EVT_CMD_FIRE, 1);
    advance_ms(PRE_FIRE_DELAY_MS - 100);
    post_cmd(EVT_CMD_FIRE, 2);           /* wrong channel */
    advance_ms(200);
    check_timers();
    expect_state("wrong-channel FIRE does not refresh dead-man", STATE_IDLE);

    /* BF-02: heartbeat freshness is its own guard, and aborts to LINK_LOST. */
    reset_world();
    arm_now(1);
    post_cmd(EVT_CMD_FIRE, 1);
    advance_ms(PRE_FIRE_DELAY_MS - 100);
    post_cmd(EVT_CMD_FIRE, 1);
    in.last_contact_ms = HEARTBEAT_INTERVAL_MS + HEARTBEAT_TIMEOUT_MS + 1;
    advance_ms(200);
    check_timers();
    expect_state("BF-02: stale heartbeat -> LINK_LOST", STATE_LINK_LOST);
    expect("BF-02: no ignition", !hw.fire_relay_on[0]);

    /* ...and a healthy failure *rate* no longer masks it: 20% failures pass
     * rlc_link_is_healthy() while contact is 1.5 s old. */
    reset_world();
    arm_now(1);
    post_cmd(EVT_CMD_FIRE, 1);
    advance_ms(PRE_FIRE_DELAY_MS - 100);
    post_cmd(EVT_CMD_FIRE, 1);
    in.link_healthy = true;
    in.last_contact_ms = 1500;
    advance_ms(200);
    check_timers();
    expect_state("BF-02: healthy rate does not excuse 1.5 s silence",
                 STATE_LINK_LOST);

    /* Guard 4: degraded link aborts to IDLE (not LINK_LOST). */
    reset_world();
    arm_now(1);
    post_cmd(EVT_CMD_FIRE, 1);
    advance_ms(PRE_FIRE_DELAY_MS - 100);
    post_cmd(EVT_CMD_FIRE, 1);
    in.link_healthy = false;
    advance_ms(200);
    check_timers();
    expect_state("COMM_DEGRADED aborts to IDLE", STATE_IDLE);

    /* Guard 3: key switch. */
    reset_world();
    arm_now(1);
    post_cmd(EVT_CMD_FIRE, 1);
    advance_ms(PRE_FIRE_DELAY_MS - 100);
    post_cmd(EVT_CMD_FIRE, 1);
    in.key_sense = false;
    advance_ms(200);
    check_timers();
    expect_state("key OFF at ignition aborts", STATE_IDLE);

    /* Guard 3b: arm sense. */
    reset_world();
    arm_now(1);
    post_cmd(EVT_CMD_FIRE, 1);
    advance_ms(PRE_FIRE_DELAY_MS - 100);
    post_cmd(EVT_CMD_FIRE, 1);
    in.arm_sense = false;
    advance_ms(200);
    check_timers();
    expect_state("arm sense lost at ignition aborts", STATE_IDLE);

    /* All guards satisfied: ignition. */
    reset_world();
    arm_now(1);
    fire_now(1);
    expect_state("all guards pass -> FIRING", STATE_FIRING);
    expect("channel relay energised", hw.fire_relay_on[0]);
    expect("fire timer started once", hw.fire_timer_starts == 1);
    expect("fire timer on the armed channel", hw.fire_timer_channel == 1);
}

/* BF-01: the defect that made a second launch per power cycle unsafe. */
static void t_two_fire_cycles(void)
{
    printf("T-FSM05 two complete fire cycles per power-on (BF-01)\n");

    reset_world();

    for (int cycle = 1; cycle <= 2; cycle++) {
        char what[80];

        arm_now(1);
        snprintf(what, sizeof(what), "cycle %d: ARMED", cycle);
        expect_state(what, STATE_ARMED);

        fire_now(1);
        snprintf(what, sizeof(what), "cycle %d: FIRING", cycle);
        expect_state(what, STATE_FIRING);
        snprintf(what, sizeof(what), "cycle %d: fire timer started", cycle);
        expect(what, hw.fire_timer_starts == cycle);

        /* Pulse completes normally — the path that used to skip the stop. */
        post(EVT_FIRE_PULSE_DONE);
        snprintf(what, sizeof(what), "cycle %d: POST_FIRE", cycle);
        expect_state(what, STATE_POST_FIRE);
        snprintf(what, sizeof(what),
                 "BF-01: cycle %d stops the timer on normal completion", cycle);
        expect(what, !hw.fire_timer_running);
        snprintf(what, sizeof(what), "cycle %d: relays safe after pulse", cycle);
        expect(what, !hw.arm_relay_on && !hw.fire_relay_on[0]);

        advance_ms(POST_FIRE_COOLDOWN_MS + 1);
        check_timers();
        snprintf(what, sizeof(what), "cycle %d: back to IDLE", cycle);
        expect_state(what, STATE_IDLE);
        snprintf(what, sizeof(what), "cycle %d: no error latched", cycle);
        expect(what, s_error_flags == 0);
    }

    expect("BF-01: two pulses in one power cycle", hw.fire_timer_starts == 2);

    /* BF-01 second half: a timer that refuses to start must cut the pulse and
     * latch ERROR, never leave the igniter energised on an untimed pulse. */
    reset_world();
    arm_now(1);
    hw.fire_timer_fail_next = 1;
    post_cmd(EVT_CMD_FIRE, 1);
    advance_ms(PRE_FIRE_DELAY_MS - 100);
    post_cmd(EVT_CMD_FIRE, 1);
    advance_ms(200);
    check_timers();
    expect_state("BF-01: fire timer start failure -> ERROR", STATE_ERROR);
    expect("BF-01: igniter de-energised", !hw.fire_relay_on[0]);
    expect("BF-01: arm relay de-energised", !hw.arm_relay_on);
    expect("BF-01: ERR_INTERNAL latched", (s_error_flags & ERR_INTERNAL) != 0);
}

/* FSD §7.2.5: every abort path out of FIRING, plus the 4.5 backstop. */
static void t_firing_exits(void)
{
    printf("T-FSM06 FIRING exit paths (FSD 7.2.5)\n");

    /* CEASE_FIRE */
    reset_world();
    arm_now(1); fire_now(1);
    post_cmd(EVT_CMD_CEASE_FIRE, 0);
    expect_state("CEASE_FIRE during FIRING -> IDLE", STATE_IDLE);
    expect("CEASE_FIRE stops the timer", hw.fire_timer_stops >= 1);
    expect("CEASE_FIRE cuts the igniter", !hw.fire_relay_on[0]);

    /* Key OFF */
    reset_world();
    arm_now(1); fire_now(1);
    in.key_sense = false;
    post_bool(EVT_KEY_SWITCH_CHANGED, false);
    expect_state("key OFF during FIRING -> IDLE", STATE_IDLE);
    expect("key OFF cuts the igniter", !hw.fire_relay_on[0]);

    /* Arm sense lost */
    reset_world();
    arm_now(1); fire_now(1);
    in.arm_sense = false;
    post_bool(EVT_ARM_SENSE_CHANGED, false);
    expect_state("arm sense lost during FIRING -> IDLE", STATE_IDLE);

    /* T-S13 / COMPLETE_PULSE_ON_LINK_LOSS: link loss completes the pulse. */
    reset_world();
    arm_now(1); fire_now(1);
    post(EVT_LINK_LOST);
    if (COMPLETE_PULSE_ON_LINK_LOSS) {
        expect_state("link lost during FIRING completes the pulse",
                     STATE_FIRING);
        expect("igniter still energised mid-pulse", hw.fire_relay_on[0]);
        post(EVT_FIRE_PULSE_DONE);
        expect_state("then -> LINK_LOST", STATE_LINK_LOST);
    } else {
        expect_state("link lost during FIRING aborts immediately",
                     STATE_LINK_LOST);
    }
    expect("igniter de-energised at the end either way", !hw.fire_relay_on[0]);

    /* Battery critical during FIRING: complete the pulse, then ERROR (m4). */
    reset_world();
    arm_now(1); fire_now(1);
    post(EVT_BATTERY_CRITICAL);
    expect_state("VBAT critical does not abort the pulse", STATE_FIRING);
    post(EVT_FIRE_PULSE_DONE);
    expect_state("pulse completes into POST_FIRE first", STATE_POST_FIRE);
    /* M7: the latched flag is acted on by the POST_FIRE check, so the unit
     * never returns to service on a critical battery. */
    check_timers();
    expect_state("...then ERROR out of POST_FIRE", STATE_ERROR);
    expect("ERR_VBAT_CRITICAL latched", (s_error_flags & ERR_VBAT_CRITICAL) != 0);

    /* 4.5 max-duration backstop: the completion notification is lost. */
    reset_world();
    arm_now(1); fire_now(1);
    int stops_before = hw.fire_timer_stops;
    advance_ms(FIRE_PULSE_DURATION_MS + FIRE_PULSE_BACKSTOP_MARGIN_MS + 1);
    check_timers();
    expect_state("lost pulse-done notification -> backstop -> POST_FIRE",
                 STATE_POST_FIRE);
    expect("backstop stops the timer", hw.fire_timer_stops > stops_before);
    expect("backstop cuts the igniter", !hw.fire_relay_on[0]);
}

/* FSD §7.3.2 / §9.1: arm relay contact weld is terminal from any state. */
static void t_weld_fault(void)
{
    printf("T-FSM07 arm-relay weld fault (FSD 7.3.2)\n");

    reset_world();
    post(EVT_ARM_SENSE_FAULT);
    expect_state("weld in IDLE -> ERROR", STATE_ERROR);
    expect("ERR_RELAY_FAULT latched", (s_error_flags & ERR_RELAY_FAULT) != 0);
    expect("error siren sounded", hw.siren_error_calls == 1);

    reset_world();
    arm_now(1); fire_now(1);
    post(EVT_ARM_SENSE_FAULT);
    expect_state("weld in FIRING -> ERROR", STATE_ERROR);
    expect("weld cuts the igniter", !hw.fire_relay_on[0]);
    expect("weld stops the fire timer", hw.fire_timer_stops >= 1);
}

/* FSD §7.2.9a / 2026-08-26: ERROR answers commands, never acts on them. */
static void t_error_is_terminal(void)
{
    printf("T-FSM08 ERROR answers but never acts\n");

    reset_world();
    do_enter_error(ERR_INTERNAL);
    hw.nack_count = 0;

    post_cmd(EVT_CMD_ARM, 1);
    expect_nack("ARM in ERROR -> BASE_ERROR", NACK_BASE_ERROR);
    post_cmd(EVT_CMD_FIRE, 1);
    expect_nack("FIRE in ERROR -> BASE_ERROR", NACK_BASE_ERROR);
    post_cmd(EVT_CMD_DISARM, 1);
    expect_nack("DISARM in ERROR -> BASE_ERROR", NACK_BASE_ERROR);
    post_cmd(EVT_CMD_CEASE_FIRE, 0);
    expect_nack("CEASE_FIRE in ERROR -> BASE_ERROR", NACK_BASE_ERROR);
    expect("four commands, four NACKs", hw.nack_count == 4);
    expect_state("ERROR is unrecoverable", STATE_ERROR);

    post(EVT_LINK_RECOVERED);
    expect_state("link recovery does not clear ERROR", STATE_ERROR);
    expect("nothing armed in ERROR", s_armed_channel == 0);
}

/* FSD §7.2.7 arm timeout + §7.2.8 link recovery. */
static void t_timeouts_and_recovery(void)
{
    printf("T-FSM09 arm timeout and link recovery\n");

    reset_world();
    arm_now(1);
    advance_ms(ARM_TIMEOUT_MS + 1);
    check_timers();
    expect_state("ARM_TIMEOUT_MS auto-disarms", STATE_IDLE);
    expect("auto-disarm releases the arm relay", !hw.arm_relay_on);

    /* An armed unit that loses the link disarms and sounds the alarm. */
    reset_world();
    arm_now(1);
    post(EVT_LINK_LOST);
    expect_state("link loss while ARMED -> LINK_LOST", STATE_LINK_LOST);
    expect("link loss disarms", s_armed_channel == 0);
    expect("link-lost siren sounded", hw.siren_link_lost_calls == 1);

    post(EVT_LINK_RECOVERED);
    expect_state("recovery -> IDLE", STATE_IDLE);
    expect("recovery never re-enters ARMED", s_armed_channel == 0);
}

int main(void)
{
    printf("RLC base FSM — host event-injection tests (FSD 4.5 / TT-04)\n\n");

    t_arm_guards();
    t_arm_verify_window();
    t_continuity_loss_disarm();
    t_prefire_guards();
    t_two_fire_cycles();
    t_firing_exits();
    t_weld_fault();
    t_error_is_terminal();
    t_timeouts_and_recovery();

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}

#else   /* remote build — the base FSM is not part of this unit */

int main(void)
{
    printf("RLC base FSM — SKIPPED (base-only; run under the BASE unit build "
           "for real coverage)\n");
    return 0;
}

#endif
