/**
 * RLC Camera Rig (CRIG) interface — base unit only. FSD §5.4.12, §7.5.
 *
 * See rlc_cam_trig.h for the contract. Implementation notes:
 *
 * Concurrency. State is touched from the FSM task (the hooks) and from the
 * esp_timer task (the four callbacks). Every read-modify-write of the state
 * AND the matching gpio_set_level() happen inside one critical section, so a
 * task-context cancel and a callback-context assert cannot interleave into a
 * line left at the wrong level. esp_timer_start/stop are never called inside
 * the critical section.
 *
 * Stale callbacks. esp_timer_stop() does not recall a callback that has
 * already been dispatched (the N2 lesson from rlc_siren.c). So no callback
 * trusts the fact that it ran: each one re-checks a deadline held in the
 * state — the lead/post callbacks only act if their deadline is still set and
 * has been reached, the cap callback only if the CURRENT assertion has lasted
 * CAM_TRIG_MAX_MS. A callback left over from a cancelled or earlier cycle
 * therefore finds nothing to do.
 *
 * Failure policy (CRIG F-8). Nothing here is fatal and nothing raises a base
 * error flag. A timer that cannot be created or started is logged and the
 * affected line is left LOW: ARM LOW reads "not connected" at the CRIG, which
 * then ignores TRIG; TRIG LOW means no photos. The launch is unaffected.
 */

#include "rlc_cam_trig.h"
#include "rlc_base_fsm.h"
#include "rlc_protocol.h"
#include "pin_config.h"
#include "rlc_config.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rlc_cam";

/* esp_timer never fires early, but allow 1 ms when comparing a deadline taken
 * from esp_timer_get_time() against the dispatch time. */
#define CAM_DEADLINE_SLACK_US   1000

#define CAM_TRIG_DELAY_MS   (PRE_FIRE_DELAY_MS - CAM_TRIG_LEAD_MS)

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static esp_timer_handle_t s_lead_timer = NULL;
static esp_timer_handle_t s_post_timer = NULL;
static esp_timer_handle_t s_cap_timer  = NULL;
static esp_timer_handle_t s_tick_timer = NULL;
static bool s_trig_ok = false;          /* all three one-shots exist */

/* ── State (guarded by s_mux) ─────────────────────────────────── */
static bool     s_armed = false;        /* FSM says ARMED .. POST_FIRE */
static bool     s_trig = false;         /* TRIG line asserted */
static bool     s_fired = false;        /* FIRING entered this cycle */
static int64_t  s_lead_due_us = 0;      /* 0 = no lead pending */
static int64_t  s_post_due_us = 0;      /* 0 = no post pending */
static int64_t  s_trig_since_us = 0;    /* start of the current assertion */
static uint32_t s_tick = 0;             /* ARM pattern phase, in ticks */
static bool     s_pattern_armed = false;

static inline void trig_write(bool on)
{
    gpio_set_level(PIN_CAM_TRIG, on ? PIN_CAM_TRIG_ACTIVE : !PIN_CAM_TRIG_ACTIVE);
}

static inline void arm_write(bool on)
{
    gpio_set_level(PIN_CAM_ARM, on ? PIN_CAM_ARM_ACTIVE : !PIN_CAM_ARM_ACTIVE);
}

static inline void timer_stop(esp_timer_handle_t t)
{
    if (t) esp_timer_stop(t);   /* ESP_ERR_INVALID_STATE if idle: harmless */
}

/* Drop TRIG and forget the cycle. Caller holds s_mux. */
static void trig_drop_locked(void)
{
    s_trig = false;
    s_fired = false;
    s_lead_due_us = 0;
    s_post_due_us = 0;
    trig_write(false);
}

/* ── esp_timer callbacks (esp_timer task context) ─────────────── */

static void lead_cb(void *arg)
{
    (void)arg;
    /* CRIG F-5: assert only if the FSM is still in PRE_FIRE. The cancel hooks
     * already clear s_lead_due_us on every PRE_FIRE exit; this is the second,
     * independent check. */
    bool in_prefire = (base_fsm_get_state() == STATE_PRE_FIRE);
    int64_t now = esp_timer_get_time();
    bool asserted = false;

    taskENTER_CRITICAL(&s_mux);
    if (s_lead_due_us != 0 && now >= s_lead_due_us - CAM_DEADLINE_SLACK_US) {
        s_lead_due_us = 0;
        if (in_prefire) {
            s_trig = true;
            s_trig_since_us = now;
            trig_write(true);
            asserted = true;
        }
    }
    taskEXIT_CRITICAL(&s_mux);

    if (!asserted) return;
    /* CRIG F-6: hard cap on this assertion. A cap we cannot arm is not worth
     * the risk of a TRIG line with no guaranteed end — drop it. */
    if (esp_timer_start_once(s_cap_timer, (uint64_t)CAM_TRIG_MAX_MS * 1000) != ESP_OK) {
        taskENTER_CRITICAL(&s_mux);
        trig_drop_locked();
        taskEXIT_CRITICAL(&s_mux);
        ESP_LOGE(TAG, "TRIG cap timer failed to start — TRIG dropped");
        return;
    }
    ESP_LOGI(TAG, "TRIG HIGH (T-%d ms)", CAM_TRIG_LEAD_MS);
}

static void post_cb(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    bool dropped = false;

    taskENTER_CRITICAL(&s_mux);
    if (s_post_due_us != 0 && now >= s_post_due_us - CAM_DEADLINE_SLACK_US) {
        trig_drop_locked();
        dropped = true;
    }
    taskEXIT_CRITICAL(&s_mux);

    if (dropped) {
        timer_stop(s_cap_timer);
        ESP_LOGI(TAG, "TRIG LOW (pulse end + %d ms)", CAM_TRIG_POST_MS);
    }
}

static void cap_cb(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    bool capped = false;

    taskENTER_CRITICAL(&s_mux);
    if (s_trig &&
        now - s_trig_since_us >= (int64_t)CAM_TRIG_MAX_MS * 1000 - CAM_DEADLINE_SLACK_US) {
        trig_drop_locked();
        capped = true;
    }
    taskEXIT_CRITICAL(&s_mux);

    if (capped) {
        timer_stop(s_post_timer);
        ESP_LOGW(TAG, "TRIG hard cap (%d ms) — TRIG dropped", CAM_TRIG_MAX_MS);
    }
}

static void tick_cb(void *arg)
{
    (void)arg;
    taskENTER_CRITICAL(&s_mux);
    bool armed_pat = s_armed || s_trig;
    uint32_t on_t, period_t;
    if (armed_pat) {
        on_t     = CAM_ARM_ARMED_ON_MS / CAM_ARM_TICK_MS;
        period_t = (CAM_ARM_ARMED_ON_MS + CAM_ARM_ARMED_OFF_MS) / CAM_ARM_TICK_MS;
    } else {
        on_t     = CAM_ARM_IDLE_ON_MS / CAM_ARM_TICK_MS;
        period_t = (CAM_ARM_IDLE_ON_MS + CAM_ARM_IDLE_OFF_MS) / CAM_ARM_TICK_MS;
    }
    if (armed_pat != s_pattern_armed) {
        /* Start the new pattern at the beginning of its LOW phase. Starting
         * on a HIGH would merge with a HIGH already on the line (one 150 ms
         * pulse that belongs to neither pattern). Cost: the armed pattern's
         * first rise is <= 2 ticks away, the idle pattern's ~900 ms. */
        s_pattern_armed = armed_pat;
        s_tick = on_t;
    }
    arm_write((s_tick % period_t) < on_t);
    s_tick = (s_tick + 1) % period_t;
    taskEXIT_CRITICAL(&s_mux);
}

/* ── Init ─────────────────────────────────────────────────────── */

static esp_timer_handle_t make_timer(void (*cb)(void *), const char *name)
{
    esp_timer_create_args_t a = {
        .callback = cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = name,
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t t = NULL;
    if (esp_timer_create(&a, &t) != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create(%s) failed", name);
        return NULL;
    }
    return t;
}

void cam_trig_init(void)
{
    /* CRIG F-7: both lines LOW before anything else. The external 10 kΩ
     * pull-downs hold them LOW until here. */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_CAM_TRIG) | (1ULL << PIN_CAM_ARM),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    trig_write(false);
    arm_write(false);
    gpio_config(&io);
    trig_write(false);
    arm_write(false);

    s_lead_timer = make_timer(lead_cb, "cam_lead");
    s_post_timer = make_timer(post_cb, "cam_post");
    s_cap_timer  = make_timer(cap_cb,  "cam_cap");
    s_trig_ok = s_lead_timer && s_post_timer && s_cap_timer;
    if (!s_trig_ok) {
        ESP_LOGE(TAG, "TRIG disabled this session (timer create failed) — no photos");
    }

    /* CRIG F-8: no heartbeat -> ARM stays LOW -> CRIG reads "not connected"
     * and ignores TRIG. Logged, never a base ERROR. */
    s_tick_timer = make_timer(tick_cb, "cam_arm");
    if (!s_tick_timer ||
        esp_timer_start_periodic(s_tick_timer, (uint64_t)CAM_ARM_TICK_MS * 1000) != ESP_OK) {
        ESP_LOGE(TAG, "ARM heartbeat failed to start — ARM held LOW, CRIG will ignore TRIG");
        return;
    }
    ESP_LOGI(TAG, "camera rig interface up (TRIG GPIO %d, ARM GPIO %d)",
             PIN_CAM_TRIG, PIN_CAM_ARM);
}

/* ── FSM hooks (FSM task context) ─────────────────────────────── */

void cam_trig_on_prefire_enter(void)
{
    if (!s_trig_ok) return;
    /* A new countdown starts a new cycle. TRIG can only still be HIGH here if
     * the previous shot's post window (which outlasts POST_FIRE by ~1 s) is
     * still running; end it so the CRIG sees two distinct bursts. */
    timer_stop(s_lead_timer);
    timer_stop(s_post_timer);
    timer_stop(s_cap_timer);

    taskENTER_CRITICAL(&s_mux);
    trig_drop_locked();
    s_lead_due_us = esp_timer_get_time() + (int64_t)CAM_TRIG_DELAY_MS * 1000;
    taskEXIT_CRITICAL(&s_mux);

    if (esp_timer_start_once(s_lead_timer, (uint64_t)CAM_TRIG_DELAY_MS * 1000) != ESP_OK) {
        taskENTER_CRITICAL(&s_mux);
        s_lead_due_us = 0;
        taskEXIT_CRITICAL(&s_mux);
        ESP_LOGE(TAG, "TRIG lead timer failed to start — no photos this shot");
    }
}

void cam_trig_on_firing_enter(void)
{
    taskENTER_CRITICAL(&s_mux);
    s_fired = true;
    s_lead_due_us = 0;          /* a lead that has not fired by now never will */
    bool trig = s_trig;
    taskEXIT_CRITICAL(&s_mux);
    timer_stop(s_lead_timer);
    if (!trig && s_trig_ok) {
        ESP_LOGW(TAG, "FIRING entered with TRIG not asserted");
    }
}

void cam_trig_on_fire_pulse_end(void)
{
    taskENTER_CRITICAL(&s_mux);
    bool start = false;
    if (s_trig && s_post_due_us == 0) {
        /* First end-of-pulse for this assertion: start the post window once. */
        s_post_due_us = esp_timer_get_time() + (int64_t)CAM_TRIG_POST_MS * 1000;
        start = true;
    } else if (!s_trig) {
        s_fired = false;        /* nothing to time — cycle over */
    }
    taskEXIT_CRITICAL(&s_mux);

    if (start &&
        esp_timer_start_once(s_post_timer, (uint64_t)CAM_TRIG_POST_MS * 1000) != ESP_OK) {
        taskENTER_CRITICAL(&s_mux);
        trig_drop_locked();
        taskEXIT_CRITICAL(&s_mux);
        timer_stop(s_cap_timer);
        ESP_LOGE(TAG, "TRIG post timer failed to start — TRIG dropped");
    }
}

void cam_trig_cancel(void)
{
    taskENTER_CRITICAL(&s_mux);
    bool fired = s_fired;
    bool dropped = false;
    if (!fired) {
        /* Before FIRING: nothing has been lit — stop at once. */
        dropped = s_trig || s_lead_due_us != 0;
        trig_drop_locked();
    }
    taskEXIT_CRITICAL(&s_mux);

    if (fired) {
        /* After FIRING the igniter may have lit: keep shooting, end through
         * the post timer (started once, whichever path gets here first). */
        cam_trig_on_fire_pulse_end();
        return;
    }
    timer_stop(s_lead_timer);
    timer_stop(s_cap_timer);
    if (dropped) ESP_LOGI(TAG, "TRIG cancelled before FIRING");
}

void cam_arm_set(bool armed)
{
    taskENTER_CRITICAL(&s_mux);
    s_armed = armed;
    taskEXIT_CRITICAL(&s_mux);
}

bool cam_trig_is_asserted(void)
{
    taskENTER_CRITICAL(&s_mux);
    bool t = s_trig;
    taskEXIT_CRITICAL(&s_mux);
    return t;
}

bool cam_arm_is_armed_pattern(void)
{
    taskENTER_CRITICAL(&s_mux);
    bool a = s_armed || s_trig;
    taskEXIT_CRITICAL(&s_mux);
    return a;
}
