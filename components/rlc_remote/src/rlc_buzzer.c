/**
 * RLC Buzzer Pattern Player
 */

#include "rlc_buzzer.h"
#include "pin_config.h"
#include "rlc_config.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "rlc_buzzer";

static QueueHandle_t s_pattern_queue = NULL;

/* Pattern re-entered whenever nothing else is sounding. See the header for why
 * a state tone cannot simply be buzzer_play()ed once. volatile: written by the
 * FSM task, read by the buzzer task; a single machine word, single writer. */
static volatile rlc_buzzer_pattern_t s_background = BUZZER_OFF;
static TaskHandle_t s_buzzer_task = NULL;

static inline void buzzer_drive(bool on)
{
    int level = on ? PIN_BUZZER_ACTIVE : !PIN_BUZZER_ACTIVE;
    gpio_set_level(PIN_BUZZER, level);
}

/* Pattern step: duration_ms, on=true/off=false */
typedef struct {
    uint16_t duration_ms;
    bool     on;
} buzzer_step_t;

static void play_steps(const buzzer_step_t *steps, int count, bool repeat)
{
    do {
        for (int i = 0; i < count; i++) {
            buzzer_drive(steps[i].on);
            TickType_t ticks = pdMS_TO_TICKS(steps[i].duration_ms);
            /* Check for new pattern while waiting */
            rlc_buzzer_pattern_t new_pat;
            if (xQueueReceive(s_pattern_queue, &new_pat, ticks) == pdTRUE) {
                buzzer_drive(false);
                /* RM-05: put it back for the main loop. The queue is a
                 * one-deep mailbox, so a failure here means buzzer_play() has
                 * already overwritten it with something newer — which is
                 * exactly what should win. Never retry or block. */
                (void)xQueueSendToFront(s_pattern_queue, &new_pat, 0);
                return;
            }
        }
        /* 5.10: repeating alarms (LINK_LOST/CRITICAL) loop here until a new
         * pattern arrives — feed the TWDT each cycle so a hung buzzer task
         * is still caught while a working one is not. */
        esp_task_wdt_reset();
    } while (repeat);

    buzzer_drive(false);
}

static void buzzer_task(void *arg)
{
    (void)arg;
    /* 5.10: TWDT coverage — a hung buzzer task previously went undetected.
     * Timed (not portMAX_DELAY) queue wait so the idle task still feeds. */
    esp_task_wdt_add(NULL);
    rlc_buzzer_pattern_t pattern;

    while (1) {
        bool got = (xQueueReceive(s_pattern_queue, &pattern,
                                  pdMS_TO_TICKS(1000)) == pdTRUE);
        if (!got) {
            /* Idle. Fall through to the background below. */
            pattern = BUZZER_OFF;
        }
        if (got) {
            switch (pattern) {
                case BUZZER_BEEP_SHORT: {
                    buzzer_step_t steps[] = {{100, true}};
                    play_steps(steps, 1, false);
                    break;
                }
                case BUZZER_BEEP_DOUBLE: {
                    buzzer_step_t steps[] = {{100, true}, {100, false}, {100, true}};
                    play_steps(steps, 3, false);
                    break;
                }
                case BUZZER_BEEP_TRIPLE: {
                    buzzer_step_t steps[] = {
                        {100, true}, {80, false},
                        {100, true}, {80, false},
                        {100, true}
                    };
                    play_steps(steps, 5, false);
                    break;
                }
                case BUZZER_BEEP_LONG: {
                    buzzer_step_t steps[] = {{500, true}};
                    play_steps(steps, 1, false);
                    break;
                }
                case BUZZER_BEEP_PING_FAIL: {
                    buzzer_step_t steps[] = {{80, true}};
                    play_steps(steps, 1, false);
                    break;
                }
                case BUZZER_BEEP_CONTINUITY_LOST: {
                    buzzer_step_t steps[] = {
                        {200, true}, {100, false},
                        {200, true}, {100, false},
                        {200, true}
                    };
                    play_steps(steps, 5, false);
                    break;
                }
                case BUZZER_ALARM_LINK_LOST: {
                    buzzer_step_t steps[] = {{200, true}, {200, false}};
                    play_steps(steps, 2, true);
                    break;
                }
                case BUZZER_ALARM_CRITICAL: {
                    buzzer_step_t steps[] = {{100, true}, {100, false}};
                    play_steps(steps, 2, true);
                    break;
                }
                case BUZZER_ALARM_ARMED: {
                    /* Slow and sparse: the pad is live and standing by, which
                     * may last the full 10 s arm window. Deliberately unlike
                     * ALARM_CRITICAL and ALARM_LINK_LOST, which are both
                     * urgent 2.5 Hz patterns — this one is a heartbeat, not an
                     * alarm, and must not read as a fault. */
                    buzzer_step_t steps[] = {{80, true}, {1120, false}};
                    play_steps(steps, 2, true);
                    break;
                }
                case BUZZER_ALARM_FIRING: {
                    /* Fast and insistent: the countdown is running or the
                     * igniter is live. Roughly 4 Hz against ARMED's 0.8 Hz, so
                     * the transition into the firing sequence is unmistakable
                     * by ear alone without looking at the panel. */
                    buzzer_step_t steps[] = {{90, true}, {160, false}};
                    play_steps(steps, 2, true);
                    break;
                }
                case BUZZER_OFF:
                    buzzer_drive(false);
                    break;
            }
        }

        /* Whatever just finished, return to the state tone if one is set.
         * play_steps() returns as soon as a new pattern is queued and pushes
         * it back to the front, so this never swallows an incoming beep. */
        rlc_buzzer_pattern_t bg = s_background;
        if (bg != BUZZER_OFF && uxQueueMessagesWaiting(s_pattern_queue) == 0) {
            switch (bg) {
                case BUZZER_ALARM_ARMED: {
                    buzzer_step_t steps[] = {{80, true}, {1120, false}};
                    play_steps(steps, 2, true);
                    break;
                }
                case BUZZER_ALARM_FIRING: {
                    buzzer_step_t steps[] = {{90, true}, {160, false}};
                    play_steps(steps, 2, true);
                    break;
                }
                default:
                    break;
            }
        }
        esp_task_wdt_reset();
    }
}

void buzzer_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BUZZER),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "buzzer GPIO %d config failed: %s — no audible feedback",
                 PIN_BUZZER, esp_err_to_name(err));
    }
    buzzer_drive(false);

    /* RM-05: depth 1 — this is a mailbox, not a backlog. Only the newest
     * pattern is ever wanted; queueing older ones just delays it. */
    s_pattern_queue = xQueueCreate(1, sizeof(rlc_buzzer_pattern_t));
    if (!s_pattern_queue) {
        ESP_LOGE(TAG, "pattern queue alloc failed");
        return;
    }
    /* RM-04 / CI-03: priority 1 pinned to core 1, per FSD §9.10. At the
     * previous unpinned priority 5 a UI task outranked the safety FSM
     * (priority 4) and could preempt it on either core — a SHALL violation of
     * that section for the sake of beep timing. */
    if (xTaskCreatePinnedToCore(buzzer_task, "buzzer_task", 2048, NULL, 1,
                                &s_buzzer_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "buzzer task create failed");
    }

    ESP_LOGI(TAG, "Buzzer initialised on GPIO %d", PIN_BUZZER);
}

void buzzer_play(rlc_buzzer_pattern_t pattern)
{
    if (!s_pattern_queue) return;
    /* RM-05: atomic replace. The old xQueueReset()+xQueueSend() pair had a
     * window between the two calls in which the buzzer task's own
     * xQueueSendToFront() (from play_steps, putting a just-received pattern
     * back) could land — so the stale pattern played instead of the new one.
     * xQueueOverwrite on a depth-1 queue is a single atomic operation and
     * always leaves the newest pattern in place. */
    (void)xQueueOverwrite(s_pattern_queue, &pattern);
}

void buzzer_set_background(rlc_buzzer_pattern_t pattern)
{
    if (pattern == s_background) return;   /* idempotent — do not restart */
    s_background = pattern;
    /* Nudge the task so the change takes effect now rather than after its 1 s
     * idle timeout. BUZZER_OFF silences whatever is sounding; the loop then
     * picks up the new background immediately. */
    buzzer_play(BUZZER_OFF);
}

void buzzer_stop(void)
{
    /* Clears the background too: "stop" that leaves a state tone which resumes
     * a moment later would not be a stop. */
    s_background = BUZZER_OFF;
    buzzer_play(BUZZER_OFF);
}
