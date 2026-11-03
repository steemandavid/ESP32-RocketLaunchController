/* Host tests for the camera rig (CRIG) interface — FSD §5.4.12 / §7.5.
 *
 * #includes the production rlc_cam_trig.c and runs it against a simulated
 * esp_timer wheel and a GPIO edge recorder, so the TRIG and ARM waveforms the
 * CRIG remote will decode are asserted edge by edge, on the simulated clock:
 *
 *   T-CAM01  boot: both lines LOW, then ARM idle pattern 100 ms / 1000 ms
 *   T-CAM02  armed pattern 50 ms / 100 ms, and back to idle
 *   T-CAM03  full cycle: TRIG at PRE_FIRE+3000, LOW at pulse end+3000; ARM
 *            stays 10 Hz while TRIG is HIGH after POST_FIRE -> IDLE
 *   T-CAM04  abort before FIRING drops TRIG at once (or it never rises)
 *   T-CAM05  abort after FIRING keeps TRIG until its post window ends
 *   T-CAM06  lead expiry outside PRE_FIRE does not assert (CRIG F-5)
 *   T-CAM07  hard cap CAM_TRIG_MAX_MS (CRIG F-6)
 *   T-CAM08  stale lead callback from a cancelled cycle does nothing
 *   T-CAM09  new countdown during the previous post window
 *   T-CAM10  timer failures: logged, lines LOW, nothing fatal (CRIG F-8)
 *
 * Base-unit only; the REMOTE build compiles to a SKIPPED stub.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

int64_t g_mock_us = 0;
int g_crit_depth = 0;

#ifdef CONFIG_RLC_UNIT_BASE

#include "esp_timer.h"
#include "driver/gpio.h"
#include "rlc_protocol.h"

/* ── FSM state the lead callback reads (CRIG F-5) ─────────────────── */
static rlc_state_t g_fsm_state = STATE_IDLE;
rlc_state_t base_fsm_get_state(void) { return g_fsm_state; }

/* ── Simulated esp_timer wheel ────────────────────────────────────── */
struct esp_timer {
    void (*cb)(void *);
    void *arg;
    const char *name;
    bool running;
    int64_t due_us;
    int64_t period_us;   /* 0 = one-shot */
};
static struct esp_timer g_timers[8];
static int g_ntimers = 0;
static const char *g_fail_create = NULL;   /* name of timer whose create fails */
static const char *g_fail_start = NULL;    /* name of timer whose start fails */

esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *out)
{
    if (g_fail_create && strcmp(g_fail_create, a->name) == 0) return ESP_FAIL;
    struct esp_timer *t = &g_timers[g_ntimers++];
    memset(t, 0, sizeof(*t));
    t->cb = a->callback; t->arg = a->arg; t->name = a->name;
    *out = t;
    return ESP_OK;
}
static esp_err_t start(esp_timer_handle_t t, uint64_t us, bool periodic)
{
    if (g_crit_depth != 0) { printf("  FAIL timer API inside critical section\n"); }
    if (g_fail_start && strcmp(g_fail_start, t->name) == 0) return ESP_FAIL;
    if (t->running) return ESP_ERR_INVALID_STATE;
    t->running = true;
    t->due_us = g_mock_us + (int64_t)us;
    t->period_us = periodic ? (int64_t)us : 0;
    return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t us) { return start(t, us, false); }
esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t us) { return start(t, us, true); }
esp_err_t esp_timer_stop(esp_timer_handle_t t)
{
    if (g_crit_depth != 0) { printf("  FAIL timer API inside critical section\n"); }
    if (!t->running) return ESP_ERR_INVALID_STATE;
    t->running = false;
    return ESP_OK;
}

/* ── GPIO edge recorder ──────────────────────────────────────────── */
int g_gpio_level[64];
typedef struct { int64_t us; int pin; int level; } edge_t;
static edge_t g_edges[20000];
static int g_nedges = 0;

esp_err_t gpio_set_level(int pin, uint32_t level)
{
    int lv = level ? 1 : 0;
    if (g_gpio_level[pin & 63] != lv && g_nedges < (int)(sizeof g_edges / sizeof g_edges[0])) {
        g_edges[g_nedges++] = (edge_t){ g_mock_us, pin, lv };
    }
    g_gpio_level[pin & 63] = lv;
    return ESP_OK;
}

/* The unit under test. */
#include "rlc_cam_trig.c"

/* ── Scaffolding ─────────────────────────────────────────────────── */
static int checks = 0, fails = 0;
static void expect(const char *what, bool cond)
{
    checks++;
    if (!cond) { printf("  FAIL %s\n", what); fails++; }
}
static void expect_near(const char *what, int64_t got_ms, int64_t want_ms, int64_t tol_ms)
{
    checks++;
    if (got_ms < want_ms - tol_ms || got_ms > want_ms + tol_ms) {
        printf("  FAIL %-50s got %lld ms, want %lld ± %lld\n", what,
               (long long)got_ms, (long long)want_ms, (long long)tol_ms);
        fails++;
    }
}

/* Run every timer due up to and including `until_us`, in due order. */
static void run_until(int64_t until_us)
{
    for (;;) {
        struct esp_timer *next = NULL;
        for (int i = 0; i < g_ntimers; i++) {
            struct esp_timer *t = &g_timers[i];
            if (t->running && t->due_us <= until_us && (!next || t->due_us < next->due_us))
                next = t;
        }
        if (!next) break;
        g_mock_us = next->due_us;
        if (next->period_us) next->due_us += next->period_us;
        else next->running = false;
        next->cb(next->arg);
        if (g_crit_depth != 0) { printf("  FAIL unbalanced critical section\n"); fails++; g_crit_depth = 0; }
    }
    g_mock_us = until_us;
}
static void run_ms(int64_t ms) { run_until(g_mock_us + ms * 1000); }

static int64_t T0;   /* reference instant, µs */
static int64_t rel_ms(int64_t us) { return (us - T0) / 1000; }

/* First edge on `pin` to `level` at or after `from_us`; -1 if none. */
static int64_t edge_at(int pin, int level, int64_t from_us)
{
    for (int i = 0; i < g_nedges; i++)
        if (g_edges[i].pin == pin && g_edges[i].level == level && g_edges[i].us >= from_us)
            return g_edges[i].us;
    return -1;
}
static int count_edges(int pin, int level, int64_t from_us, int64_t to_us)
{
    int n = 0;
    for (int i = 0; i < g_nedges; i++)
        if (g_edges[i].pin == pin && g_edges[i].level == level &&
            g_edges[i].us >= from_us && g_edges[i].us < to_us) n++;
    return n;
}

/* Check the ARM waveform between two instants: every rising edge `period_ms`
 * after the previous one, each HIGH lasting `high_ms`. */
static void expect_arm_pattern(const char *what, int64_t from_us, int64_t to_us,
                               int period_ms, int high_ms)
{
    int64_t prev_rise = -1;
    int rises = 0, bad = 0;
    for (int i = 0; i < g_nedges; i++) {
        edge_t *e = &g_edges[i];
        if (e->pin != PIN_CAM_ARM || e->us < from_us || e->us >= to_us) continue;
        if (e->level == 1) {
            if (prev_rise >= 0 && (e->us - prev_rise) != (int64_t)period_ms * 1000) bad++;
            prev_rise = e->us;
            rises++;
            int64_t fall = edge_at(PIN_CAM_ARM, 0, e->us);
            /* fall < 0: the pulse is still HIGH when the run stopped */
            if (fall >= 0 && (fall - e->us) != (int64_t)high_ms * 1000) bad++;
        }
    }
    int want = (int)((to_us - from_us) / 1000 / period_ms);
    checks++;
    if (bad || rises < want - 1) {
        printf("  FAIL %-50s %d rises (want ~%d), %d off-pattern\n", what, rises, want, bad);
        fails++;
    }
}

static void reset_module(void)
{
    memset(g_timers, 0, sizeof g_timers);
    g_ntimers = 0;
    g_fail_create = g_fail_start = NULL;
    memset(g_gpio_level, 0, sizeof g_gpio_level);
    g_nedges = 0;
    g_mock_us = 5000000;
    g_crit_depth = 0;
    g_fsm_state = STATE_IDLE;

    s_lead_timer = s_post_timer = s_cap_timer = s_tick_timer = NULL;
    s_trig_ok = false;
    s_armed = s_trig = s_fired = false;
    s_lead_due_us = s_post_due_us = s_trig_since_us = 0;
    s_tick = 0;
    s_pattern_armed = false;
    cam_trig_init();
}

/* FSM-side helpers mirroring rlc_base_fsm.c's hook calls. */
static void fsm_arm(void)      { g_fsm_state = STATE_ARMED; cam_arm_set(true); }
static void fsm_prefire(void)  { g_fsm_state = STATE_PRE_FIRE; cam_trig_on_prefire_enter(); }
static void fsm_firing(void)   { g_fsm_state = STATE_FIRING; cam_trig_on_firing_enter(); }
static void fsm_pulse_end(void){ g_fsm_state = STATE_POST_FIRE; cam_trig_on_fire_pulse_end(); }
static void fsm_post_idle(void){ g_fsm_state = STATE_IDLE; cam_arm_set(false); }
static void fsm_abort(rlc_state_t to)
{ g_fsm_state = to; cam_trig_cancel(); cam_arm_set(false); }

#define LEAD_DELAY_MS (PRE_FIRE_DELAY_MS - CAM_TRIG_LEAD_MS)

/* ── Tests ───────────────────────────────────────────────────────── */

static void t_boot(void)
{
    printf("T-CAM01 boot state and idle heartbeat\n");
    reset_module();
    expect("TRIG LOW after init", g_gpio_level[PIN_CAM_TRIG] == !PIN_CAM_TRIG_ACTIVE);
    expect("ARM LOW after init", g_gpio_level[PIN_CAM_ARM] == !PIN_CAM_ARM_ACTIVE);
    expect("no edges during init", g_nedges == 0);
    int64_t t = g_mock_us;
    run_ms(2000);
    int64_t first = edge_at(PIN_CAM_ARM, 1, t);
    expect("first ARM pulse within ~1 s of init",
           first >= 0 && first - t <= (CAM_ARM_IDLE_OFF_MS + CAM_ARM_TICK_MS) * 1000);
    run_ms(8000);
    expect_arm_pattern("idle 100 ms HIGH every 1000 ms", t, g_mock_us, 1000, 100);
    expect("TRIG never moved", count_edges(PIN_CAM_TRIG, 1, 0, g_mock_us) == 0);
}

static void t_armed_pattern(void)
{
    printf("T-CAM02 armed heartbeat\n");
    reset_module();
    run_ms(3000);
    fsm_arm();
    int64_t t = g_mock_us;
    run_ms(3000);
    int64_t first = edge_at(PIN_CAM_ARM, 1, t);
    expect("armed pattern starts within 100 ms (T-27)", first >= 0 && first - t <= 100000);
    expect_arm_pattern("armed 50 ms HIGH every 100 ms", first, g_mock_us, 100, 50);
    fsm_abort(STATE_IDLE);
    t = g_mock_us;
    run_ms(5000);
    int64_t back = edge_at(PIN_CAM_ARM, 1, t + 1);   /* strictly after the switch */
    /* A pattern change starts in the new pattern's LOW phase, so the first
     * idle pulse comes one idle OFF time after the switch (CRIG: "1 Hz about
     * 1 s after"); the line drops within one tick. */
    expect("ARM LOW within one tick of disarm",
           edge_at(PIN_CAM_ARM, 0, t) - t <= CAM_ARM_TICK_MS * 1000 ||
           g_gpio_level[PIN_CAM_ARM] == 0);
    expect("idle pattern resumes within ~1 s of disarm",
           back >= 0 && back - t <= (CAM_ARM_IDLE_OFF_MS + CAM_ARM_TICK_MS) * 1000);
    expect_arm_pattern("idle after disarm", back, g_mock_us, 1000, 100);
}

static void t_full_cycle(void)
{
    printf("T-CAM03 full cycle (T-21)\n");
    reset_module();
    run_ms(1000);
    fsm_arm();
    run_ms(500);
    fsm_prefire();
    T0 = g_mock_us;
    run_ms(PRE_FIRE_DELAY_MS);
    fsm_firing();
    run_ms(FIRE_PULSE_DURATION_MS);
    int64_t pulse_end = g_mock_us;
    fsm_pulse_end();
    run_ms(POST_FIRE_COOLDOWN_MS);
    fsm_post_idle();
    int64_t idle_at = g_mock_us;
    run_ms(5000);

    int64_t rise = edge_at(PIN_CAM_TRIG, 1, T0);
    int64_t fall = edge_at(PIN_CAM_TRIG, 0, T0);
    expect_near("TRIG HIGH at PRE_FIRE + lead delay", rel_ms(rise), LEAD_DELAY_MS, 0);
    expect_near("TRIG LOW at pulse end + post", (fall - pulse_end) / 1000, CAM_TRIG_POST_MS, 0);
    expect("exactly one TRIG pulse", count_edges(PIN_CAM_TRIG, 1, T0, g_mock_us) == 1);
    expect_arm_pattern("ARM 10 Hz after POST_FIRE->IDLE until TRIG drops", idle_at, fall, 100, 50);
    int64_t first_idle = -1;
    for (int i = 0; i < g_nedges; i++)
        if (g_edges[i].pin == PIN_CAM_ARM && g_edges[i].level == 1 && g_edges[i].us >= fall)
            { first_idle = g_edges[i].us; break; }
    expect_arm_pattern("ARM back to 1 Hz after TRIG drops", first_idle, g_mock_us, 1000, 100);
    expect_near("TRIG drops ~1 s after POST_FIRE -> IDLE", (fall - idle_at) / 1000,
                CAM_TRIG_POST_MS - POST_FIRE_COOLDOWN_MS, 0);
}

static void t_abort_before_firing(void)
{
    printf("T-CAM04 abort before FIRING (T-22)\n");
    reset_module();
    fsm_arm();
    fsm_prefire();
    T0 = g_mock_us;
    run_ms(LEAD_DELAY_MS + 1000);
    expect("TRIG HIGH in late PRE_FIRE", g_gpio_level[PIN_CAM_TRIG] == 1);
    int64_t abort_at = g_mock_us;
    fsm_abort(STATE_IDLE);
    expect("TRIG LOW immediately on abort", g_gpio_level[PIN_CAM_TRIG] == 0);
    expect("TRIG fell at the abort instant", edge_at(PIN_CAM_TRIG, 0, T0) == abort_at);
    run_ms(15000);
    expect("TRIG stays LOW after abort", count_edges(PIN_CAM_TRIG, 1, abort_at, g_mock_us) == 0);

    reset_module();
    fsm_arm();
    fsm_prefire();
    T0 = g_mock_us;
    run_ms(1000);                       /* abort before the lead expires */
    fsm_abort(STATE_LINK_LOST);
    run_ms(15000);
    expect("early abort: TRIG never rises", count_edges(PIN_CAM_TRIG, 1, T0, g_mock_us) == 0);
}

static void t_abort_after_firing(void)
{
    printf("T-CAM05 abort after FIRING keeps shooting\n");
    reset_module();
    fsm_arm();
    fsm_prefire();
    T0 = g_mock_us;
    run_ms(PRE_FIRE_DELAY_MS);
    fsm_firing();
    run_ms(400);                        /* CEASE_FIRE mid-pulse */
    int64_t abort_at = g_mock_us;
    fsm_abort(STATE_IDLE);
    expect("TRIG still HIGH after FIRING abort", g_gpio_level[PIN_CAM_TRIG] == 1);
    run_ms(1000);
    cam_trig_cancel();                  /* a later disarm must not cut or extend it */
    cam_trig_on_fire_pulse_end();
    run_ms(10000);
    int64_t fall = edge_at(PIN_CAM_TRIG, 0, T0);
    expect_near("TRIG LOW at abort + post", (fall - abort_at) / 1000, CAM_TRIG_POST_MS, 0);
    expect_arm_pattern("ARM 10 Hz while TRIG HIGH after abort", abort_at, fall, 100, 50);

    /* Link lost, pulse completed: normal pulse end then LINK_LOST entry */
    reset_module();
    fsm_arm();
    fsm_prefire();
    run_ms(PRE_FIRE_DELAY_MS);
    fsm_firing();
    run_ms(FIRE_PULSE_DURATION_MS);
    int64_t pe = g_mock_us;
    cam_trig_on_fire_pulse_end();
    fsm_abort(STATE_LINK_LOST);         /* do_enter_link_lost via firing_exit */
    run_ms(8000);
    fall = edge_at(PIN_CAM_TRIG, 0, pe);
    expect_near("link-loss completed pulse: TRIG LOW at pulse end + post",
                (fall - pe) / 1000, CAM_TRIG_POST_MS, 0);
}

static void t_lead_outside_prefire(void)
{
    printf("T-CAM06 lead expiry outside PRE_FIRE (F-5)\n");
    reset_module();
    fsm_arm();
    fsm_prefire();
    T0 = g_mock_us;
    run_ms(500);
    g_fsm_state = STATE_IDLE;           /* FSM left PRE_FIRE without the hook */
    run_ms(10000);
    expect("no TRIG when FSM is not in PRE_FIRE",
           count_edges(PIN_CAM_TRIG, 1, T0, g_mock_us) == 0);
}

static void t_hard_cap(void)
{
    printf("T-CAM07 hard cap (F-6)\n");
    reset_module();
    fsm_arm();
    fsm_prefire();
    T0 = g_mock_us;
    run_ms(PRE_FIRE_DELAY_MS);
    fsm_firing();                       /* no pulse end, no cancel, ever */
    run_ms(20000);
    int64_t rise = edge_at(PIN_CAM_TRIG, 1, T0);
    int64_t fall = edge_at(PIN_CAM_TRIG, 0, T0);
    expect_near("TRIG capped at CAM_TRIG_MAX_MS", (fall - rise) / 1000, CAM_TRIG_MAX_MS, 0);
    expect("TRIG stays LOW after cap", count_edges(PIN_CAM_TRIG, 1, fall, g_mock_us) == 0);
}

static void t_stale_callback(void)
{
    printf("T-CAM08 stale lead callback\n");
    reset_module();
    fsm_arm();
    fsm_prefire();
    int64_t old_due = s_lead_due_us;
    run_ms(1000);
    fsm_abort(STATE_IDLE);
    fsm_arm();
    run_ms(500);
    fsm_prefire();                      /* new cycle, new deadline */
    T0 = g_mock_us;
    g_mock_us = old_due;                /* the old dispatch arrives late */
    lead_cb(NULL);
    expect("stale lead callback does not assert TRIG", g_gpio_level[PIN_CAM_TRIG] == 0);
    run_until(T0 + (int64_t)(LEAD_DELAY_MS + 100) * 1000);
    expect_near("TRIG at the NEW deadline", rel_ms(edge_at(PIN_CAM_TRIG, 1, T0)),
                LEAD_DELAY_MS, 0);
}

static void t_rearm_in_post_window(void)
{
    printf("T-CAM09 new countdown during previous post window\n");
    reset_module();
    fsm_arm();
    fsm_prefire();
    run_ms(PRE_FIRE_DELAY_MS);
    fsm_firing();
    run_ms(FIRE_PULSE_DURATION_MS);
    fsm_pulse_end();
    run_ms(500);
    fsm_arm();
    fsm_prefire();                      /* TRIG still HIGH from shot 1 */
    T0 = g_mock_us;
    expect("previous TRIG ended by new countdown", g_gpio_level[PIN_CAM_TRIG] == 0);
    run_ms(LEAD_DELAY_MS + 10);
    expect_near("second TRIG at its own lead", rel_ms(edge_at(PIN_CAM_TRIG, 1, T0)),
                LEAD_DELAY_MS, 0);
    run_ms(PRE_FIRE_DELAY_MS - LEAD_DELAY_MS);
    fsm_firing();
    run_ms(FIRE_PULSE_DURATION_MS);
    int64_t pe = g_mock_us;
    fsm_pulse_end();
    run_ms(5000);
    expect_near("second TRIG ends at its own post", (edge_at(PIN_CAM_TRIG, 0, pe) - pe) / 1000,
                CAM_TRIG_POST_MS, 0);
}

static void t_timer_failures(void)
{
    printf("T-CAM10 timer failures (F-8)\n");
    /* Heartbeat timer cannot start: ARM held LOW, TRIG still works but the
     * CRIG will ignore it (no armed pattern). */
    memset(g_timers, 0, sizeof g_timers); g_ntimers = 0;
    reset_module();
    g_ntimers = 0; g_nedges = 0;
    s_lead_timer = s_post_timer = s_cap_timer = s_tick_timer = NULL;
    g_fail_start = "cam_arm";
    cam_trig_init();
    run_ms(5000);
    expect("ARM held LOW when heartbeat fails",
           count_edges(PIN_CAM_ARM, 1, 0, g_mock_us) == 0);

    /* One-shot cannot be created: TRIG disabled, hooks are harmless. */
    g_ntimers = 0; g_nedges = 0;
    s_lead_timer = s_post_timer = s_cap_timer = s_tick_timer = NULL;
    g_fail_start = NULL;
    g_fail_create = "cam_lead";
    cam_trig_init();
    fsm_arm(); fsm_prefire(); run_ms(PRE_FIRE_DELAY_MS);
    fsm_firing(); run_ms(1000); fsm_pulse_end(); fsm_abort(STATE_IDLE);
    run_ms(10000);
    expect("TRIG never rises with no lead timer",
           count_edges(PIN_CAM_TRIG, 1, 0, g_mock_us) == 0);
    expect("ARM heartbeat still runs", count_edges(PIN_CAM_ARM, 1, 0, g_mock_us) > 5);

    /* Post timer cannot start: TRIG dropped at pulse end, not left HIGH. */
    reset_module();
    fsm_arm(); fsm_prefire(); run_ms(PRE_FIRE_DELAY_MS);
    fsm_firing(); run_ms(1000);
    g_fail_start = "cam_post";
    fsm_pulse_end();
    expect("TRIG dropped when post timer fails", g_gpio_level[PIN_CAM_TRIG] == 0);
    g_fail_start = NULL;

    /* Cap timer cannot start: TRIG dropped right after asserting. */
    reset_module();
    g_fail_start = "cam_cap";
    fsm_arm(); fsm_prefire(); run_ms(PRE_FIRE_DELAY_MS);
    expect("TRIG not left HIGH without a cap", g_gpio_level[PIN_CAM_TRIG] == 0);
    g_fail_start = NULL;
}

int main(void)
{
    printf("RLC camera rig interface — host tests (FSD 5.4.12 / 7.5)\n\n");
    t_boot();
    t_armed_pattern();
    t_full_cycle();
    t_abort_before_firing();
    t_abort_after_firing();
    t_lead_outside_prefire();
    t_hard_cap();
    t_stale_callback();
    t_rearm_in_post_window();
    t_timer_failures();
    expect("critical sections balanced", g_crit_depth == 0);
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}

#else

int main(void)
{
    printf("RLC camera rig interface — SKIPPED (base-only)\n");
    return 0;
}

#endif
