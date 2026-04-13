#pragma once

/* -----------------------------------------------------------------------
 * pin_config.h — All GPIO assignments for RLC remote unit HW test
 * RLC-HWTEST-REMOTE-001 v1.0 — aligned with FSD v1.11
 * ----------------------------------------------------------------------- */

/* --- ADC inputs -------------------------------------------------------- */
#define PIN_BATT_ADC            1   /* ADC1_CH0 — battery voltage divider */
#define ADC_CH_BATT             ADC_CHANNEL_0

/* --- Battery divider --------------------------------------------------- */
#define BATT_DIVIDER_RATIO      2.8f    /* 18 kΩ + 10 kΩ */
#define BATT_ADC_SAMPLES        8

/* --- Rotary encoder ---------------------------------------------------- */
#define PIN_ENCODER_A           4   /* CLK — interrupt, pull-up */
#define PIN_ENCODER_B           5   /* DT  — interrupt, pull-up */
#define PIN_ENCODER_SW          6   /* Push — pull-up, 16-bit debounce */

/* --- Arm/disarm switch ------------------------------------------------- */
#define PIN_ARM_SWITCH          7   /* Pull-up, 16-bit debounce */
#define PIN_ARM_LED             8   /* Red LED, digital output */
#define PIN_ARM_LED_ACTIVE      0   /* 0 = active LOW (LED wired 3.3V→resistor→GPIO) */

/* --- Fire button ------------------------------------------------------- */
#define PIN_FIRE_BUTTON         15  /* Pull-up, 8-bit debounce */
#define PIN_FIRE_LED_RED        17  /* Red ring LED, digital output (built-in series resistor) */
#define PIN_FIRE_LED_GREEN      18  /* Green ring LED, digital output (built-in series resistor) */

/* --- Buzzer ------------------------------------------------------------ */
#define PIN_BUZZER              16  /* Digital output */
#define PIN_BUZZER_ACTIVE       0   /* 0 = active LOW (BC547 inverts) */

/* --- Display (ILI9488, 480×320, RGB666, SPI) --------------------------- */
#define PIN_DISP_MOSI           11  /* SPI2 MOSI */
#define PIN_DISP_SCLK           12  /* SPI2 CLK */
#define PIN_DISP_MISO           9   /* SPI2 MISO */
#define PIN_DISP_CS             10  /* Chip select */
#define PIN_DISP_DC             13  /* Data/command */
#define PIN_DISP_RST            14  /* Hardware reset */
#define PIN_DISP_BACKLIGHT      21  /* Always HIGH (100% brightness) */

/* --- RGB LED ----------------------------------------------------------- */
#define PIN_RGB_LED             48  /* WS2812 on-board, RMT peripheral */

/* --- Display parameters ------------------------------------------------ */
#define DISPLAY_WIDTH           480
#define DISPLAY_HEIGHT          320
#define DISPLAY_SPI_HOST        SPI2_HOST
#define DISPLAY_SPI_FREQ_HZ     20000000    /* 20 MHz */
