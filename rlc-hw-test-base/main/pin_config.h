#pragma once

/* -----------------------------------------------------------------------
 * pin_config.h — All GPIO assignments and polarity for RLC base unit HW test
 * RLC-HWTEST-BASE-001 v1.1 — aligned with FSD v1.13
 *
 * Arm relay redesign (v1.13): physical key switch (SPDT) now controls arm
 * relay coil drive path (not directly in fire path). Arm relay (GPIO 47
 * via IRLZ44N) is primary fire path interlock. Arm sense (GPIO 21) reads
 * ARM SENSE node (arm relay COM output) via 27kΩ/10kΩ divider + zener.
 * Hardware AND gate: key switch ON AND software MOSFET drive required.
 * ----------------------------------------------------------------------- */

/* --- ADC inputs -------------------------------------------------------- */
#define PIN_BATT_ADC            1   /* ADC1_CH0 — battery voltage divider */
#define PIN_CONT_CH1_ADC        2   /* ADC1_CH1 */
#define PIN_CONT_CH2_ADC        10  /* ADC1_CH9 */
#define PIN_CONT_CH3_ADC        4   /* ADC1_CH3 */
#define PIN_CONT_CH4_ADC        5   /* ADC1_CH4 */
#define PIN_CONT_CH5_ADC        6   /* ADC1_CH5 */
#define PIN_CONT_CH6_ADC        7   /* ADC1_CH6 */
#define PIN_CONT_CH7_ADC        8   /* ADC1_CH7 */
#define PIN_CONT_CH8_ADC        9   /* ADC1_CH8 */

/* ADC channel numbers (ADC1) */
#define ADC_CH_BATT             ADC_CHANNEL_0
#define ADC_CH_CONT1            ADC_CHANNEL_1
#define ADC_CH_CONT2            ADC_CHANNEL_9
#define ADC_CH_CONT3            ADC_CHANNEL_3
#define ADC_CH_CONT4            ADC_CHANNEL_4
#define ADC_CH_CONT5            ADC_CHANNEL_5
#define ADC_CH_CONT6            ADC_CHANNEL_6
#define ADC_CH_CONT7            ADC_CHANNEL_7
#define ADC_CH_CONT8            ADC_CHANNEL_8

/* --- SPDT relay outputs (via IRLZ44N MOSFETs, active HIGH) ------------- */
#define PIN_RELAY_CH1           11
#define PIN_RELAY_CH1_ACTIVE    1

#define PIN_RELAY_CH2           12
#define PIN_RELAY_CH2_ACTIVE    1

#define PIN_RELAY_CH3           13
#define PIN_RELAY_CH3_ACTIVE    1

#define PIN_RELAY_CH4           14
#define PIN_RELAY_CH4_ACTIVE    1

#define PIN_RELAY_CH5           15
#define PIN_RELAY_CH5_ACTIVE    1

#define PIN_RELAY_CH6           16
#define PIN_RELAY_CH6_ACTIVE    1

#define PIN_RELAY_CH7           17
#define PIN_RELAY_CH7_ACTIVE    1

#define PIN_RELAY_CH8           18
#define PIN_RELAY_CH8_ACTIVE    1

/* --- Arm sense input (§5.4.3) ----------------------------------------- */
/* External circuit: 27 kΩ / 10 kΩ voltage divider + 3.3V zener clamp.
 * Senses ARM SENSE node (arm relay COM output).
 * HIGH (2.4–3.3V) = arm relay closed, VBAT present on fire path.
 * LOW  (~0V)      = arm relay de-energised, R2 pulls to GND.             */
#define PIN_ARM_SENSE           21

/* --- Arm relay output (GPIO 47, via IRLZ44N MOSFET) -------------------- */
/* GPIO HIGH drives IRLZ44N MOSFET, energising the arm relay coil.
 * In production: arm relay coil positive terminal connected through
 * physical key switch (hardware AND gate: key ON AND software drive).
 * In test firmware: simulates the armed condition directly.
 * Arm relay is the primary fire path interlock — when energised, VBAT
 * is connected to the ARM SENSE node (fire path for all channels).       */
#define PIN_ARM_SIM_RELAY       47
#define PIN_ARM_SIM_RELAY_ACTIVE 1

/* --- Siren output (via IRLZ44N MOSFET) --------------------------------- */
#define PIN_SIREN               40
#define PIN_SIREN_ACTIVE        1

/* --- RGB LED strip ----------------------------------------------------- */
#define PIN_RGB_LED             48  /* WS2812 8-pixel strip + on-board LED (parallel), RMT peripheral */
#define NUM_RGB_LEDS            8   /* 8 addressable pixels; on-board LED mirrors pixel 0 */

/* --- Battery divider --------------------------------------------------- */
#define BATT_DIVIDER_RATIO      4.3f

/* --- Continuity thresholds (µV) --------------------------------------- */
/*
 * KNOWN ISSUE: Cannot distinguish between wire shorts and actual igniters.
 * See memory/continuity_circuit_issue.md for details.
 *
 * Using original FSD v1.14 values. Both wire shorts (0 Ω) and igniters (1-2 Ω)
 * read ~35000 µV due to relay NC contact resistance or circuit topology.
 * SHORT detection is not functional - will classify as GOOD.
 * This is acceptable because SHORT is informational only (does not block arming).
 */
#define CONT_SHORT_UV               500     /* < 0.5 mV = SHORT (FSD default - not functional) */
#define CONT_MARGINAL_UV            66000   /* > 66 mV = MARGINAL */
#define CONT_OPEN_UV                1500000 /* > 1500 mV = OPEN */
#define CONT_HYSTERESIS_SHORT_UV    200
#define CONT_HYSTERESIS_MARGINAL_UV 5000
#define CONT_HYSTERESIS_OPEN_UV     50000

/* --- ADC oversampling -------------------------------------------------- */
#define CONT_ADC_SAMPLES        64
#define BATT_ADC_SAMPLES        8
