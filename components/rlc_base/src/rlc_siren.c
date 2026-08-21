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
 *   - siren_off() left s_pulse_count at -1 (infinite, from siren_start_pulse);
 *     the stale callback then toggled the output back ON with the timer
 *     stopped, so nothing ever turned it off again;
 *   - siren_start_continuous() sets s_pulse_count = 0, which the stale
 *     callback read as "pattern finished" and drove the siren OFF — silence
 *     through the whole 2 s PRE_FIRE countdown, the one moment the pad
 *     warning has to sound.
 *
 * Every start/stop path sets this flag under the mutex. It is true only while
 * a periodic pattern is genuinely running, so a callback left over from a
 * cancelled pattern sees false and returns without touching the output. */
static bool s_timer_active = false;

static void siren_lock(void)
{
    if (s_siren_mu) xSemaphoreTake(s_siren_mu, portMAX_DELAY);
}
static void siren_unlock(void)
{
    if (s_siren_mu) xSemaphoreGive(s_siren_mu);
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
        esp_timer_stop(s_siren_timer);
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
    gpio_config(&cfg);
    siren_drive(false);

    s_siren_mu = xSemaphoreCreateMutex();

    esp_timer_create_args_t timer_args = {
        .callback = siren_timer_cb,
        .name     = "siren_pulse",
    };
    esp_timer_create(&timer_args, &s_siren_timer);

    ESP_LOGI(TAG, "Siren initialised on GPIO %d", PIN_SIREN);
}

/* Half-periods. A "cycle" is one ON half plus one OFF half, so a pattern of
 * N cycles at half-period H lasts N * 2 * H. */
#define SIREN_PULSE_HALF_MS      500
#define SIREN_LINK_LOST_HALF_MS  500
#define SIREN_ERROR_HALF_MS      200

/* m10: SIREN_LINK_LOST_DURATION_MS used to be dead config — the cycle count
 * was a bare literal, so editing the constant did nothing. Derive it. */
#define SIREN_LINK_LOST_CYCLES \
    (SIREN_LINK_LOST_DURATION_MS / (2 * SIREN_LINK_LOST_HALF_MS))

void siren_start_pulse(void)
{
    siren_lock();
    esp_timer_stop(s_siren_timer);
    s_pulse_count = -1;  /* Infinite */
    siren_drive(true);
    esp_timer_start_periodic(s_siren_timer, SIREN_PULSE_HALF_MS * 1000);
    s_timer_active = true;
    siren_unlock();
}

void siren_start_continuous(void)
{
    siren_lock();
    esp_timer_stop(s_siren_timer);
    s_timer_active = false;   /* N2: steady ON, no pattern owns the output */
    s_pulse_count = 0;
    siren_drive(true);
    siren_unlock();
}

void siren_start_link_lost(void)
{
    siren_lock();
    esp_timer_stop(s_siren_timer);
    s_pulse_count = SIREN_LINK_LOST_CYCLES;
    siren_drive(true);
    esp_timer_start_periodic(s_siren_timer, SIREN_LINK_LOST_HALF_MS * 1000);
    s_timer_active = true;
    siren_unlock();
}

void siren_off(void)
{
    siren_lock();
    esp_timer_stop(s_siren_timer);
    s_timer_active = false;
    /* N2: clear the cycle count too. Leaving it at -1 (from siren_start_pulse)
     * is what let a stale callback toggle the siren back ON for good. */
    s_pulse_count = 0;
    siren_drive(false);
    siren_unlock();
}

void siren_start_error(void)
{
    siren_lock();
    esp_timer_stop(s_siren_timer);
    s_pulse_count = 3;  /* 3 short blasts */
    siren_drive(true);
    esp_timer_start_periodic(s_siren_timer, SIREN_ERROR_HALF_MS * 1000);
    s_timer_active = true;
    siren_unlock();
}
