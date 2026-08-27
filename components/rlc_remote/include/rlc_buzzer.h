/**
 * RLC Buzzer Pattern Player (Remote Unit)
 * FSD §12.1: pattern timings.
 */

#pragma once

/**
 * Buzzer pattern identifiers.
 */
typedef enum {
    BUZZER_BEEP_SHORT,          /* 100ms on — single confirmation */
    BUZZER_BEEP_DOUBLE,         /* 100on/100off/100on — arm confirmed */
    BUZZER_BEEP_TRIPLE,         /* 100on/80off x2 + 100on — error/NACK */
    BUZZER_BEEP_LONG,           /* 500ms on — disarm */
    BUZZER_BEEP_PING_FAIL,      /* 80ms on — ping failure */
    BUZZER_BEEP_CONTINUITY_LOST,/* 200on/100off x2 + 200on */
    BUZZER_ALARM_LINK_LOST,     /* 200on/200off repeating */
    BUZZER_ALARM_CRITICAL,      /* 100on/100off repeating */
    BUZZER_ALARM_ARMED,         /* 80on/1120off repeating — pad live, standing by */
    BUZZER_ALARM_FIRING,        /* 90on/160off repeating — sequence running */
    BUZZER_OFF,                 /* Silence */
} rlc_buzzer_pattern_t;

/**
 * Initialise buzzer GPIO and pattern player task.
 */
void buzzer_init(void);

/**
 * Play a buzzer pattern. New patterns preempt active ones.
 */
void buzzer_play(rlc_buzzer_pattern_t pattern);

/**
 * Stop any active buzzer pattern, and clear the background pattern.
 */
void buzzer_stop(void);

/**
 * Set the pattern the player returns to whenever nothing else is sounding.
 *
 * A plain buzzer_play() of a repeating pattern is not enough for a state tone:
 * the next one-shot beep replaces it and it never comes back. ARMED in
 * particular is full of one-shots — the arm-confirm double beep, and every
 * refusal triple beep from the FIRE guards. The background is re-entered when
 * each of those finishes.
 *
 * Idempotent: setting the pattern already in force does nothing, so this can
 * be driven from a periodic tick without restarting the tone every call.
 * BUZZER_OFF clears it.
 */
void buzzer_set_background(rlc_buzzer_pattern_t pattern);
