#include "cli.h"
#include "pin_config.h"
#include "hw_relay.h"
#include "hw_continuity.h"
#include "hw_battery.h"
#include "hw_inputs.h"
#include "hw_siren.h"
#include "hw_rgb_led.h"
#include "hw_fire_timer.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define UART_NUM        UART_NUM_0
#define UART_BAUD       115200
#define CLI_BUF_SIZE    256
#define PROMPT          "\r\nbase> "

static const char *TAG = "cli";

/* ------------------------------------------------------------------ */
/* UART helpers                                                         */
/* ------------------------------------------------------------------ */

static void uart_puts(const char *s)
{
    uart_write_bytes(UART_NUM, s, strlen(s));
}

static void uart_putc(char c)
{
    uart_write_bytes(UART_NUM, &c, 1);
}

/* Read one line; echo characters; handle backspace. Returns length. */
static int uart_readline(char *buf, int max_len)
{
    int pos = 0;
    while (1) {
        uint8_t c;
        int n = uart_read_bytes(UART_NUM, &c, 1, portMAX_DELAY);
        if (n <= 0) continue;

        if (c == '\r' || c == '\n') {
            uart_puts("\r\n");
            buf[pos] = '\0';
            return pos;
        } else if (c == 0x7f || c == '\b') {
            if (pos > 0) {
                pos--;
                uart_puts("\b \b");
            }
        } else if (c >= 0x20 && pos < max_len - 1) {
            buf[pos++] = c;
            uart_putc(c);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Tokeniser                                                            */
/* ------------------------------------------------------------------ */

#define MAX_TOKENS 8

static int tokenise(char *line, char *toks[], int max)
{
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        toks[n++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Command handlers                                                     */
/* ------------------------------------------------------------------ */

static void cmd_help(void)
{
    uart_puts(
        "General:\r\n"
        "  help                         This message\r\n"
        "  status                       Full system status\r\n"
        "  safe                         Deactivate all relays immediately\r\n"
        "  pins                         Print pin assignments\r\n"
        "\r\nRelay:\r\n"
        "  relay <ch> on|off            Activate/deactivate channel relay (1-8)\r\n"
        "  relay all off                Deactivate all 8 channel relays\r\n"
        "  lowside on|off               Close/open low-side relay\r\n"
        "  relay sweep                  Sweep all 8 channels (500 ms each)\r\n"
        "  relay feedback               Read relay feedback GPIO\r\n"
        "\r\nContinuity:\r\n"
        "  cont <ch>                    Read continuity channel (1-8)\r\n"
        "  cont all                     Read all 8 channels\r\n"
        "  cont <ch> raw [N]            Take N raw samples (default 64)\r\n"
        "  cont mosfet on|off           Enable/disable continuity MOSFET\r\n"
        "  cont monitor                 Continuously monitor all channels (any key stops)\r\n"
        "\r\nBattery:\r\n"
        "  batt                         Read battery voltage\r\n"
        "  batt raw [N]                 Take N raw samples (default 8)\r\n"
        "\r\nInputs:\r\n"
        "  arm                          Poll arm switch until key press\r\n"
        "  feedback                     Read relay feedback\r\n"
        "\r\nSiren:\r\n"
        "  siren on|off                 Activate/deactivate siren\r\n"
        "  siren pulse <on> <off> <N>   Pulse N times\r\n"
        "  siren test                   Run all siren patterns\r\n"
        "\r\nLED:\r\n"
        "  led <r> <g> <b>              Set RGB colour (0-255)\r\n"
        "  led off                      Turn off LED\r\n"
        "  led test                     Cycle all status patterns\r\n"
        "  led brightness <0-255>       Set brightness\r\n"
        "\r\nFire:\r\n"
        "  fire <ch> <ms>               Fire channel for duration_ms, then safe\r\n"
        "  fire <ch> <ms> nosafe        Fire channel, leave relays after\r\n"
    );
}

static void cmd_status(void)
{
    uart_puts("=== System Status ===\r\n");

    /* Relay feedback + arm switch */
    int fb  = relay_feedback_read();
    int arm = arm_switch_read_debounced();
    printf("Arm switch : %s (GPIO raw=%d)\r\n", arm ? "ARMED" : "DISARMED",
           arm_switch_read_raw());
    printf("Rel. feedback: %s (GPIO raw=%d)\r\n", fb ? "SAFE" : "FAULT", fb);

    /* Battery */
    batt_reading_t batt = batt_read();
    printf("Battery    : raw=%d  pin=%"PRId32" mV  scaled=%"PRId32" mV\r\n",
           batt.raw, batt.mv_calibrated, batt.mv_scaled);

    /* Continuity all */
    uart_puts("Continuity :\r\n");
    for (int ch = 1; ch <= 8; ch++) {
        cont_reading_t c = cont_read(ch);
        printf("  CH%d: raw=%d  %"PRId32" µV  %s\r\n",
               ch, c.raw, c.uv, cont_band_str(c.band));
    }
}

static void cmd_pins(void)
{
    uart_puts("=== Pin Assignments ===\r\n");
    printf("Batt ADC        : GPIO %d\r\n", PIN_BATT_ADC);
    for (int i = 0; i < 8; i++) {
        int pins[] = { PIN_CONT_CH1_ADC, PIN_CONT_CH2_ADC, PIN_CONT_CH3_ADC,
                       PIN_CONT_CH4_ADC, PIN_CONT_CH5_ADC, PIN_CONT_CH6_ADC,
                       PIN_CONT_CH7_ADC, PIN_CONT_CH8_ADC };
        printf("Cont CH%d ADC    : GPIO %d\r\n", i + 1, pins[i]);
    }
    int relays[] = { PIN_RELAY_CH1, PIN_RELAY_CH2, PIN_RELAY_CH3, PIN_RELAY_CH4,
                     PIN_RELAY_CH5, PIN_RELAY_CH6, PIN_RELAY_CH7, PIN_RELAY_CH8 };
    for (int i = 0; i < 8; i++) {
        printf("Relay CH%d       : GPIO %d\r\n", i + 1, relays[i]);
    }
    printf("Low-side relay  : GPIO %d\r\n", PIN_LOWSIDE_RELAY);
    printf("Relay feedback  : GPIO %d  level=%d\r\n",
           PIN_RELAY_FEEDBACK, gpio_get_level(PIN_RELAY_FEEDBACK));
    printf("Arm switch      : GPIO %d  level=%d\r\n",
           PIN_ARM_SWITCH, gpio_get_level(PIN_ARM_SWITCH));
    printf("Siren           : GPIO %d\r\n", PIN_SIREN);
    printf("Cont MOSFET     : GPIO %d  (active %s)\r\n",
           PIN_CONT_MOSFET, PIN_CONT_MOSFET_ACTIVE ? "HIGH" : "LOW");
    printf("RGB LED         : GPIO %d\r\n", PIN_RGB_LED);
}

static void cmd_relay(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: relay <ch> on|off  |  relay all off  |  relay sweep  |  relay feedback\r\n"); return; }

    if (strcmp(toks[1], "sweep") == 0) {
        uart_puts("Relay sweep: 8 channels × 500 ms\r\n");
        relay_sweep();
        uart_puts("Sweep complete.\r\n");
        return;
    }
    if (strcmp(toks[1], "feedback") == 0) {
        int lvl = relay_feedback_read();
        printf("Relay feedback: GPIO=%d  %s\r\n", lvl, lvl ? "SAFE" : "FAULT");
        return;
    }
    if (strcmp(toks[1], "all") == 0) {
        if (ntok >= 3 && strcmp(toks[2], "off") == 0) {
            relay_all_off();
            uart_puts("All channel relays deactivated.\r\n");
        } else {
            uart_puts("Unknown relay all subcommand.\r\n");
        }
        return;
    }

    int ch = atoi(toks[1]);
    if (ch < 1 || ch > 8) { uart_puts("Channel must be 1–8.\r\n"); return; }
    if (ntok < 3) { uart_puts("Usage: relay <ch> on|off\r\n"); return; }

    if (strcmp(toks[2], "on") == 0) {
        relay_set(ch, 1);
        printf("Relay CH%d ON\r\n", ch);
    } else if (strcmp(toks[2], "off") == 0) {
        relay_set(ch, 0);
        printf("Relay CH%d OFF\r\n", ch);
    } else {
        uart_puts("Expected 'on' or 'off'.\r\n");
    }
}

static void cmd_lowside(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: lowside on|off\r\n"); return; }
    if (strcmp(toks[1], "on") == 0) {
        lowside_set(1);
        uart_puts("Low-side relay ON\r\n");
    } else if (strcmp(toks[1], "off") == 0) {
        lowside_set(0);
        uart_puts("Low-side relay OFF\r\n");
    } else {
        uart_puts("Expected 'on' or 'off'.\r\n");
    }
}

static void cmd_cont(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: cont <ch|all|mosfet|monitor>\r\n"); return; }

    if (strcmp(toks[1], "mosfet") == 0) {
        if (ntok < 3) { uart_puts("Usage: cont mosfet on|off\r\n"); return; }
        if (strcmp(toks[2], "on") == 0) {
            cont_mosfet_set(1);
            uart_puts("Continuity MOSFET ON (circuit powered)\r\n");
        } else {
            cont_mosfet_set(0);
            uart_puts("Continuity MOSFET OFF (circuit de-energised)\r\n");
        }
        return;
    }

    if (strcmp(toks[1], "monitor") == 0) {
        uart_puts("Monitoring all channels — press any key to stop.\r\n");
        cont_band_t prev[8] = { CONT_BAND_OPEN, CONT_BAND_OPEN, CONT_BAND_OPEN, CONT_BAND_OPEN,
                                CONT_BAND_OPEN, CONT_BAND_OPEN, CONT_BAND_OPEN, CONT_BAND_OPEN };
        uint8_t dummy;
        while (uart_read_bytes(UART_NUM, &dummy, 1, 0) <= 0) {
            for (int ch = 1; ch <= 8; ch++) {
                cont_reading_t r = cont_read(ch);
                if (r.band != prev[ch - 1]) {
                    printf("CH%d: %s → %s  (%"PRId32" µV)\r\n",
                           ch, cont_band_str(prev[ch - 1]),
                           cont_band_str(r.band), r.uv);
                    prev[ch - 1] = r.band;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        uart_puts("Monitor stopped.\r\n");
        return;
    }

    if (strcmp(toks[1], "all") == 0) {
        for (int ch = 1; ch <= 8; ch++) {
            cont_reading_t r = cont_read(ch);
            printf("CH%d: raw=%d  %"PRId32" µV  %s\r\n",
                   ch, r.raw, r.uv, cont_band_str(r.band));
        }
        return;
    }

    int ch = atoi(toks[1]);
    if (ch < 1 || ch > 8) { uart_puts("Channel must be 1–8.\r\n"); return; }

    /* cont <ch> raw [N] */
    if (ntok >= 3 && strcmp(toks[2], "raw") == 0) {
        int n = (ntok >= 4) ? atoi(toks[3]) : 64;
        if (n <= 0) n = 64;
        printf("Taking %d raw samples on CH%d...\r\n", n, ch);
        int mean, mn, mx, sd;
        cont_read_raw_stats(ch, n, &mean, &mn, &mx, &sd);
        printf("  mean=%d  min=%d  max=%d  stddev=%d\r\n", mean, mn, mx, sd);
        return;
    }

    cont_reading_t r = cont_read(ch);
    printf("CH%d: raw=%d  %"PRId32" µV  %s\r\n",
           ch, r.raw, r.uv, cont_band_str(r.band));
}

static void cmd_batt(char *toks[], int ntok)
{
    if (ntok >= 2 && strcmp(toks[1], "raw") == 0) {
        int n = (ntok >= 3) ? atoi(toks[2]) : 8;
        if (n <= 0) n = 8;
        int mean, mn, mx, sd;
        batt_read_raw_stats(n, &mean, &mn, &mx, &sd);
        printf("Battery raw stats (%d samples): mean=%d  min=%d  max=%d  stddev=%d\r\n",
               n, mean, mn, mx, sd);
        return;
    }
    batt_reading_t r = batt_read();
    printf("Battery: raw=%d  pin=%"PRId32" mV  scaled=%"PRId32" mV  (ratio=%.1f)\r\n",
           r.raw, r.mv_calibrated, r.mv_scaled, (double)BATT_DIVIDER_RATIO);
}

static void cmd_arm(void)
{
    uart_puts("Polling arm switch — press any key to stop.\r\n");
    uint8_t dummy;
    int last = -1;
    while (uart_read_bytes(UART_NUM, &dummy, 1, 0) <= 0) {
        int armed = arm_switch_read_debounced();
        int raw   = arm_switch_read_raw();
        if (armed != last) {
            printf("Arm switch: raw=%d  %s\r\n", raw, armed ? "ARMED" : "DISARMED");
            last = armed;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    uart_puts("Stopped.\r\n");
}

static void cmd_feedback(void)
{
    int lvl = feedback_read_raw();
    printf("Relay feedback: GPIO=%d  %s\r\n", lvl, lvl ? "SAFE (no current)" : "FAULT (current detected)");
}

static void cmd_siren(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: siren on|off|pulse|test\r\n"); return; }

    if (strcmp(toks[1], "on") == 0) {
        siren_set(1);
        uart_puts("Siren ON\r\n");
    } else if (strcmp(toks[1], "off") == 0) {
        siren_set(0);
        uart_puts("Siren OFF\r\n");
    } else if (strcmp(toks[1], "test") == 0) {
        uart_puts("Running siren patterns...\r\n");
        siren_test();
    } else if (strcmp(toks[1], "pulse") == 0) {
        if (ntok < 5) { uart_puts("Usage: siren pulse <on_ms> <off_ms> <count>\r\n"); return; }
        uint32_t on_ms  = atoi(toks[2]);
        uint32_t off_ms = atoi(toks[3]);
        int      count  = atoi(toks[4]);
        printf("Siren pulse: %"PRIu32" ms on / %"PRIu32" ms off × %d\r\n", on_ms, off_ms, count);
        siren_pulse(on_ms, off_ms, count);
    } else {
        uart_puts("Unknown siren subcommand.\r\n");
    }
}

static void cmd_led(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: led <r> <g> <b>  |  led off  |  led test  |  led brightness <n>\r\n"); return; }

    if (strcmp(toks[1], "off") == 0) {
        led_off();
        uart_puts("LED off\r\n");
    } else if (strcmp(toks[1], "test") == 0) {
        uart_puts("Running LED pattern test...\r\n");
        led_test();
    } else if (strcmp(toks[1], "brightness") == 0) {
        if (ntok < 3) { uart_puts("Usage: led brightness <0-255>\r\n"); return; }
        int b = atoi(toks[2]);
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        led_set_brightness((uint8_t)b);
        printf("LED brightness set to %d\r\n", b);
    } else {
        if (ntok < 4) { uart_puts("Usage: led <r> <g> <b>\r\n"); return; }
        int r = atoi(toks[1]);
        int g = atoi(toks[2]);
        int b = atoi(toks[3]);
        led_set((uint8_t)r, (uint8_t)g, (uint8_t)b);
        printf("LED set to R=%d G=%d B=%d\r\n", r, g, b);
    }
}

static void cmd_fire(char *toks[], int ntok)
{
    if (ntok < 3) { uart_puts("Usage: fire <ch> <ms> [nosafe]\r\n"); return; }
    int      ch         = atoi(toks[1]);
    uint32_t duration   = atoi(toks[2]);
    int      safe_after = 1;
    if (ntok >= 4 && strcmp(toks[3], "nosafe") == 0) safe_after = 0;

    if (ch < 1 || ch > 8) { uart_puts("Channel must be 1–8.\r\n"); return; }
    if (duration == 0)     { uart_puts("Duration must be > 0 ms.\r\n"); return; }

    printf("Fire CH%d for %"PRIu32" ms%s...\r\n", ch, duration, safe_after ? " (safe after)" : " (nosafe)");
    int elapsed = fire_pulse(ch, duration, safe_after);
    printf("Fire complete. Elapsed: %d ms  (requested: %"PRIu32" ms)\r\n", elapsed, duration);
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                             */
/* ------------------------------------------------------------------ */

static void dispatch(char *line)
{
    char *toks[MAX_TOKENS];
    int ntok = tokenise(line, toks, MAX_TOKENS);
    if (ntok == 0) return;

    if      (strcmp(toks[0], "help")     == 0) cmd_help();
    else if (strcmp(toks[0], "status")   == 0) cmd_status();
    else if (strcmp(toks[0], "safe")     == 0) { relay_all_safe(); fire_abort(); uart_puts("All outputs safe.\r\n"); }
    else if (strcmp(toks[0], "pins")     == 0) cmd_pins();
    else if (strcmp(toks[0], "relay")    == 0) cmd_relay(toks, ntok);
    else if (strcmp(toks[0], "lowside")  == 0) cmd_lowside(toks, ntok);
    else if (strcmp(toks[0], "cont")     == 0) cmd_cont(toks, ntok);
    else if (strcmp(toks[0], "batt")     == 0) cmd_batt(toks, ntok);
    else if (strcmp(toks[0], "arm")      == 0) cmd_arm();
    else if (strcmp(toks[0], "feedback") == 0) cmd_feedback();
    else if (strcmp(toks[0], "siren")    == 0) cmd_siren(toks, ntok);
    else if (strcmp(toks[0], "led")      == 0) cmd_led(toks, ntok);
    else if (strcmp(toks[0], "fire")     == 0) cmd_fire(toks, ntok);
    else uart_puts("Unknown command. Type 'help' for usage.\r\n");
}

/* ------------------------------------------------------------------ */
/* Init and task                                                        */
/* ------------------------------------------------------------------ */

void cli_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &cfg));
    /* GPIO 43 (TX) and 44 (RX) are default for UART0 on ESP32-S3 */
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, 1024, 0, 0, NULL, 0));
    ESP_LOGI(TAG, "CLI UART initialised at %d baud", UART_BAUD);
}

void cli_task(void *arg)
{
    char buf[CLI_BUF_SIZE];

    uart_puts("\r\n=== RLC Base Unit Hardware Test ===\r\n");
    uart_puts("Type 'help' for available commands.\r\n");

    while (1) {
        uart_puts(PROMPT);
        uart_readline(buf, sizeof(buf));
        dispatch(buf);
    }
}
