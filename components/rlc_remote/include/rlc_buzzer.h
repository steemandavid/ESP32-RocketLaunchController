/**
 * RLC Buzzer Pattern Player (Remote Unit)
 * FSD §12.1: pattern timings.
 */

#pragma once

/**
 * Buzzer pattern identifiers.
 */
typedef enum {
    BUZZER_BEEP_SHORT,          /* 200ms on — single confirmation */
    BUZZER_BEEP_DOUBLE,         /* 250on/300off/250on — arm confirmed */
    BUZZER_BEEP_TRIPLE,         /* 250on/250off/250on/250off/250on — error/NACK */
    BUZZER_BEEP_LONG,           /* 500ms on — disarm */
    BUZZER_BEEP_PING_FAIL,      /* 150ms on — ping failure */
    BUZZER_BEEP_CONTINUITY_LOST,/* 300on/300off/300on/300off/300on */
    BUZZER_ALARM_LINK_LOST,     /* 400on/400off repeating */
    BUZZER_ALARM_CRITICAL,      /* 250on/250off repeating */
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
