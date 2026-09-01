/**
 * RLC Siren Control Implementation
 */

#include "rlc_siren.h"
#include "pin_config.h"
#include "rlc_config.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "rlc_siren";

static esp_timer_handle_t s_siren_timer = NULL;
static bool s_siren_on = false;
static int s_pulse_count = 0;   /* -1 = infinite, >0 = remaining cycles */

/* 5.4: the esp_timer callback (esp_timer task context) races the FSM-task
 * start/stop calls on s_siren_on/s_pulse_count — a toggle interleaved with
 * siren_off() leaves the siren stuck ON. All pattern state is mutated under
 * this mutex. A mutex (not a critical section) because the guarded sections
 * call esp_timer_start/stop, which may not run with interrupts disabled. */
static SemaphoreHandle_t s_siren_mu = NULL;

/* N2: the mutex alone is not enough. esp_timer_stop() does not cancel a
 * callback that has already been dispatched (only esp_timer_delete() waits),
 * so a callback can be parked on siren_lock() while a task-context call
 * reconfigures the pattern underneath it. Two failures were reachable:
 *
 *   - siren_off() left s_pulse_count at -1 (infinite, from the old ARMED
 *     pulse pattern); the stale callback then toggled the output back ON with
 *     the timer stopped, so nothing ever turned it off again;
 *   - siren_start_continuous() sets s_pulse_count = 0, which the stale
 *     callback read as "pattern finished" and drove the siren OFF — silence
 *     through the whole 2 s PRE_FIRE countdown, the one moment the pad
 *     warning has to sound.
 *
 * Every start/stop path sets this flag under the mutex. It is true only while
 * a periodic pattern is genuinely running, so a callback left over from a
 * cancelled pattern sees false and returns without touching the output.
 *
 * 2026-08-26: the infinite (-1) pattern is gone with the ARMED pulse, so the
 * first failure above is no longer reachable by construction. The flag stays:
 * link-lost and error are still finite periodic patterns, and the second
 * failure — a stale tick from either of them silencing a PRE_FIRE that now
 * follows an already-continuous ARMED — is unchanged. */
static bool s_timer_active = false;

static void siren_lock(void)
{
    if (s_siren_mu) xSemaphoreTake(s_siren_mu, portMAX_DELAY);
}
static void siren_unlock(void)
{
    if (s_siren_mu) xSemaphoreGive(s_siren_mu);
}

/* BF-06: s_siren_timer is NULL if esp_timer_create() failed in siren_init().
 * Passing NULL to the esp_timer API asserts, which would turn a degraded
 * siren into a panic on the fire path. Guard both directions here. */
static inline void siren_timer_stop(void)
{
    if (s_siren_timer) esp_timer_stop(s_siren_timer);
}

/* Returns false when no periodic pattern could be started, so callers can
 * leave s_timer_active clear rather than arming a callback that never runs. */
static bool siren_timer_run(uint32_t half_period_ms)
{
    if (!s_siren_timer) return false;
    return esp_timer_start_periodic(s_siren_timer,
                                    (uint64_t)half_period_ms * 1000) == ESP_OK;
}

static inline void siren_drive(bool on)
{
    int level = on ? PIN_SIREN_ACTIVE : !PIN_SIREN_ACTIVE;
    gpio_set_level(PIN_SIREN, level);
    s_siren_on = on;
}

static void siren_timer_cb(void *arg)
{
    siren_lock();
    /* N2: dispatched before the pattern was cancelled — the output now
     * belongs to whoever cancelled it. Do not touch it. */
    if (!s_timer_active) {
        siren_unlock();
        return;
    }
    if (s_pulse_count == 0) {
        /* Pattern finished its cycle count */
        siren_drive(false);
        siren_timer_stop();
        s_timer_active = false;
        siren_unlock();
        return;
    }

    /* Toggle */
    siren_drive(!s_siren_on);

    if (s_siren_on == false && s_pulse_count > 0) {
        /* Just turned off — count one complete cycle */
        s_pulse_count--;
    }
    siren_unlock();
}

void siren_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_SIREN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    /* BF-06: these returns were discarded. The siren is the pad's only
     * audible warning that a channel is armed — a silent failure to configure
     * it is exactly the kind of fault that must not pass unnoticed. Logged
     * loudly rather than fatal: a working fire path with no siren is still
     * safer than a base that refuses to boot at the pad, and the FSM's own
     * error paths remain intact. */
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SIREN GPIO %d config failed: %s — NO AUDIBLE PAD WARNING",
                 PIN_SIREN, esp_err_to_name(err));
    }
    siren_drive(false);

    s_siren_mu = xSemaphoreCreateMutex();
    if (!s_siren_mu) {
        ESP_LOGE(TAG, "siren mutex alloc failed — pattern state is unprotected");
    }

    esp_timer_create_args_t timer_args = {
        .callback = siren_timer_cb,
        .name     = "siren_pulse",
    };
    err = esp_timer_create(&timer_args, &s_siren_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "siren timer create failed: %s — patterned alerts "
                      "(link-lost/error/continuity-lost) will not sound",
                 esp_err_to_name(err));
        s_siren_timer = NULL;
    }

    ESP_LOGI(TAG, "Siren initialised on GPIO %d", PIN_SIREN);
}

/* Half-periods. A "cycle" is one ON half plus one OFF half, so a pattern of
 * N cycles at half-period H lasts N * 2 * H. */
#define SIREN_LINK_LOST_HALF_MS  500
#define SIREN_ERROR_HALF_MS      200

/* m10: SIREN_LINK_LOST_DURATION_MS used to be dead config — the cycle count
 * was a bare literal, so editing the constant did nothing. Derive it. */
#define SIREN_LINK_LOST_CYCLES \
    (SIREN_LINK_LOST_DURATION_MS / (2 * SIREN_LINK_LOST_HALF_MS))

/* siren_start_pulse() (500 ms on/off, infinite) was removed on 2026-08-26.
 * ARMED now uses siren_start_continuous(): chopping the supply at 1 Hz
 * restarted the siren's internal sweep every half-second and left it less
 * audible, not more. */

void siren_start_continuous(void)
{
    siren_lock();
    siren_timer_stop();
    s_timer_active = false;   /* N2: steady ON, no pattern owns the output */
    s_pulse_count = 0;
    siren_drive(true);
    siren_unlock();
}

void siren_start_link_lost(void)
{
    siren_lock();
    siren_timer_stop();
    s_pulse_count = SIREN_LINK_LOST_CYCLES;
    siren_drive(true);
    if (siren_timer_run(SIREN_LINK_LOST_HALF_MS)) {
        s_timer_active = true;
    } else {
        /* No timer: nothing would ever turn the siren off again. Silence is
         * the only safe degradation — the failure is logged at init. */
        siren_drive(false);
        s_pulse_count = 0;
        s_timer_active = false;
    }
    siren_unlock();
}

void siren_off(void)
{
    siren_lock();
    siren_timer_stop();
    s_timer_active = false;
    /* N2: clear the cycle count too. Leaving it at -1 (as the removed
     * infinite ARMED pulse left it) is what let a stale callback toggle the
     * siren back ON for good. Finite patterns can still park a callback. */
    s_pulse_count = 0;
    siren_drive(false);
    siren_unlock();
}

void siren_start_error(void)
{
    siren_lock();
    siren_timer_stop();
    s_pulse_count = 3;  /* 3 short blasts */
    siren_drive(true);
    if (siren_timer_run(SIREN_ERROR_HALF_MS)) {
        s_timer_active = true;
    } else {
        siren_drive(false);   /* see siren_start_link_lost() */
        s_pulse_count = 0;
        s_timer_active = false;
    }
    siren_unlock();
}

/* BF-03: FSD §12.2 SIREN_CONTINUITY_LOST — 200/200 x3, then silence.
 * Same shape as SIREN_ERROR per the spec table; a separate entry point so the
 * three continuity-loss disarm sites read as what they are, and so the two can
 * diverge later without hunting call sites. Before this, a continuity-loss
 * disarm was audibly indistinguishable from a key-off disarm (both silent) —
 * the operator got no cue that the igniter had left the circuit. */
void siren_start_continuity_lost(void)
{
    siren_lock();
    siren_timer_stop();
    s_pulse_count = 3;
    siren_drive(true);
    if (siren_timer_run(SIREN_ERROR_HALF_MS)) {
        s_timer_active = true;
    } else {
        siren_drive(false);   /* see siren_start_link_lost() */
        s_pulse_count = 0;
        s_timer_active = false;
    }
    siren_unlock();
}

/* SIREN_BOOT_TEST: one 200 ms blast. Starting the timer with the cycle count
 * already at 0 makes the first tick take the "pattern finished" branch in
 * siren_timer_cb() — drive OFF, stop, done — which is exactly a single
 * half-period of sound with no pattern bookkeeping left over. */
void siren_boot_pulse(void)
{
    siren_lock();
    siren_timer_stop();
    s_pulse_count = 0;
    siren_drive(true);
    if (siren_timer_run(SIREN_ERROR_HALF_MS)) {
        s_timer_active = true;
    } else {
        siren_drive(false);   /* see siren_start_link_lost() */
        s_timer_active = false;
    }
    siren_unlock();
}
