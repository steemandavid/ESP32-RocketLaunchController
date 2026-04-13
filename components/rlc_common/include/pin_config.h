/**
 * RLC Pin Assignments and Polarity Configuration
 *
 * All GPIO pins and active-state polarities defined here.
 * Changing polarity requires only adjusting the _ACTIVE constants.
 */

#pragma once

#include "sdkconfig.h"

/* ══════════════════════════════════════════════════════════════════
 * BASE UNIT PIN ASSIGNMENTS
 * ══════════════════════════════════════════════════════════════════ */

#ifdef CONFIG_RLC_UNIT_BASE

/* Channel relay outputs (8x) — active state configurable */
#define PIN_RELAY_CH1              4
#define PIN_RELAY_CH2              5
#define PIN_RELAY_CH3              6
#define PIN_RELAY_CH4              7
#define PIN_RELAY_CH5              15
#define PIN_RELAY_CH6              16
#define PIN_RELAY_CH7              17
#define PIN_RELAY_CH8              18

#define PIN_RELAY_CH_ACTIVE        1    /* 1 = active HIGH, 0 = active LOW */

/* Channel continuity inputs (8x) — LOW = continuity OK */
#define PIN_CONT_CH1               11
#define PIN_CONT_CH2               12
#define PIN_CONT_CH3               13
#define PIN_CONT_CH4               14
#define PIN_CONT_CH5               21
#define PIN_CONT_CH6               38
#define PIN_CONT_CH7               39
#define PIN_CONT_CH8               40

/* Low-side relay output */
#define PIN_LOWSIDE_RELAY          48
#define PIN_LOWSIDE_RELAY_ACTIVE   1    /* 1 = active HIGH, 0 = active LOW */

/* Relay feedback input — HIGH = safe (no current), LOW = fault */
#define PIN_RELAY_FEEDBACK         41

/* Arm/disarm switch — LOW = armed, HIGH = disarmed */
#define PIN_ARM_SWITCH             42

/* Battery voltage ADC (ADC1 only — GPIO 1-10) */
#define PIN_VBAT_ADC               1

/* Siren output */
#define PIN_SIREN                  2
#define PIN_SIREN_ACTIVE           1    /* 1 = active HIGH, 0 = active LOW */

#endif /* CONFIG_RLC_UNIT_BASE */

/* ══════════════════════════════════════════════════════════════════
 * REMOTE UNIT PIN ASSIGNMENTS
 * ══════════════════════════════════════════════════════════════════ */

#ifdef CONFIG_RLC_UNIT_REMOTE

/* Rotary encoder */
#define PIN_ENCODER_CLK            4    /* A / CLK — interrupt */
#define PIN_ENCODER_DT             5    /* B / DT  — interrupt */
#define PIN_ENCODER_SW             6    /* Push button */

/* Arm/disarm switch — LOW = armed, HIGH = disarmed */
#define PIN_ARM_SWITCH             7
#define PIN_ARM_LED                8    /* Red LED (built-in series resistor) */
#define PIN_ARM_LED_ACTIVE         0    /* 0 = active LOW (LED wired 3.3V→resistor→GPIO) */

/* Fire button — LOW = pressed, HIGH = released */
#define PIN_FIRE_BUTTON            15
#define PIN_FIRE_LED_RED           17   /* Red ring LED (built-in series resistor) */
#define PIN_FIRE_LED_GREEN         18   /* Green ring LED (built-in series resistor) */

/* Battery voltage ADC (ADC1 only) */
#define PIN_VBAT_ADC               1

/* Buzzer output */
#define PIN_BUZZER                 16
#define PIN_BUZZER_ACTIVE          0    /* 0 = active LOW (BC547 NPN transistor inverts) */

/* ILI9488 LCD Display (SPI2) */
#define PIN_DISPLAY_MOSI           11
#define PIN_DISPLAY_SCLK           12
#define PIN_DISPLAY_CS             10
#define PIN_DISPLAY_DC             13
#define PIN_DISPLAY_RST            14
#define PIN_DISPLAY_BL             21   /* Backlight — PWM capable */
#define PIN_DISPLAY_MISO           9

#endif /* CONFIG_RLC_UNIT_REMOTE */

/* ══════════════════════════════════════════════════════════════════
 * COMMON (both units)
 * ══════════════════════════════════════════════════════════════════ */

/* On-board WS2812 RGB LED */
#define PIN_RGB_LED                48
