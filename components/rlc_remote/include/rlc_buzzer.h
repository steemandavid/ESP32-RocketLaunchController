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
 * Stop any active buzzer pattern.
 */
void buzzer_stop(void);
