#include "cli.h"
#include "pin_config.h"
#include "hw_relay.h"
#include "hw_continuity.h"
#include "hw_battery.h"
#include "hw_inputs.h"
#include "hw_siren.h"
#include "hw_rgb_led.h"
#include "hw_fire_timer.h"
#include "driver/usb_serial_jtag.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define CLI_BUF_SIZE    256
#define PROMPT          "\r\nbase> "

static const char *TAG = "cli";

/* ------------------------------------------------------------------ */
/* Console helpers (USB-Serial/JTAG — ttyACM0 on ESP32-S3)              */
/* ------------------------------------------------------------------ */

static void uart_puts(const char *s)
{
    usb_serial_jtag_write_bytes(s, strlen(s), portMAX_DELAY);
}

static void uart_putc(char c)
{
    usb_serial_jtag_write_bytes(&c, 1, portMAX_DELAY);
}

/* Read one line; echo characters; handle backspace. Returns length. */
static int uart_readline(char *buf, int max_len)
{
    int pos = 0;
    while (1) {
        uint8_t c;
        int n = usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY);
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
        "  safe                         De-energise all SPDT relays immediately\r\n"
        "  pins                         Print pin assignments\r\n"
        "  exit                         Exit help — press Ctrl+] to quit monitor\r\n");
    uart_puts(
        "\r\nRelay (SPDT, via IRLZ44N MOSFETs):\r\n"
        "  relay <ch> on|off            Energise/de-energise channel relay (1-8)\r\n"
        "  relay all off                De-energise all 8 channel relays\r\n"
        "  relay sweep                  Sweep all 8 channels + arm relay (500 ms each)\r\n");
    uart_puts(
        "\r\nContinuity (always-on via SPDT NC contact):\r\n"
        "  cont <ch>                    Read continuity channel (1-8)\r\n"
        "  cont all                     Read all 8 channels\r\n"
        "  cont <ch> raw [N]            Take N raw samples (default 64)\r\n"
        "  cont monitor                 Continuously monitor all channels (any key stops)\r\n");
    uart_puts(
        "\r\nBattery:\r\n"
        "  batt                         Read battery voltage\r\n"
        "  batt raw [N]                 Take N raw samples (default 8)\r\n"
        "\r\nInputs:\r\n"
        "  arm                          Poll arm sense input (ARM SENSE node) until key press\r\n"
        "  arm sim on|off               Energise/de-energise arm relay (GPIO 47)\r\n");
    uart_puts(
        "\r\nSiren (via IRLZ44N MOSFET):\r\n"
        "  siren on|off                 Activate/deactivate siren\r\n"
        "  siren pulse <on> <off> <N>   Pulse N times\r\n"
        "  siren test                   Run all siren patterns\r\n");
    uart_puts(
        "\r\nLED:\r\n"
        "  led <r> <g> <b>              Set RGB colour (0-255)\r\n"
        "  led off                      Turn off LED\r\n"
        "  led test                     Cycle all status patterns\r\n"
        "  led strip                    8-pixel strip diagnostic\r\n"
        "  led brightness <0-255>       Set brightness\r\n"
        "  led gpiotest                 Raw GPIO48 toggle (diagnostic)\r\n");
    uart_puts(
        "\r\nFire:\r\n"
        "  fire <ch> <ms>               Energise channel SPDT relay for duration_ms, then safe\r\n"
        "  fire <ch> <ms> nosafe        Energise channel, leave relay after\r\n");
}

static void cmd_status(void)
{
    uart_puts("=== System Status ===\r\n");

    /* Arm sense input */
    int armed = arm_sense_read_debounced();
    printf("Arm sense  : %s (GPIO raw=%d)\r\n", armed ? "ARMED" : "DISARMED",
           arm_sense_read_raw());

    /* Battery */
    batt_reading_t batt = batt_read();
    printf("Battery    : raw=%d  pin=%"PRId32" mV  scaled=%"PRId32" mV\r\n",
           batt.raw, batt.mv_calibrated, batt.mv_scaled);

    /* Continuity all */
    uart_puts("Continuity :\r\n");
    for (int ch = 1; ch <= 8; ch++) {
        cont_reading_t c = cont_read(ch);
        printf("  CH%d: raw=%d  %"PRId32" uV  %s\r\n",
               ch, c.raw, c.uv, cont_band_str(c.band));
    }
}

static void cmd_pins(void)
{
    uart_puts("=== Pin Assignments (FSD v1.10) ===\r\n");
    printf("Batt ADC        : GPIO %d\r\n", PIN_BATT_ADC);
    int cont_pins[] = { PIN_CONT_CH1_ADC, PIN_CONT_CH2_ADC, PIN_CONT_CH3_ADC,
                        PIN_CONT_CH4_ADC, PIN_CONT_CH5_ADC, PIN_CONT_CH6_ADC,
                        PIN_CONT_CH7_ADC, PIN_CONT_CH8_ADC };
    for (int i = 0; i < 8; i++) {
        printf("Cont CH%d ADC    : GPIO %d\r\n", i + 1, cont_pins[i]);
    }
    int relays[] = { PIN_RELAY_CH1, PIN_RELAY_CH2, PIN_RELAY_CH3, PIN_RELAY_CH4,
                     PIN_RELAY_CH5, PIN_RELAY_CH6, PIN_RELAY_CH7, PIN_RELAY_CH8 };
    for (int i = 0; i < 8; i++) {
        printf("SPDT relay CH%d  : GPIO %2d  level=%d  (active HIGH via IRLZ44N MOSFET)\r\n",
               i + 1, relays[i], gpio_get_level(relays[i]));
    }
    printf("Arm sense       : GPIO %d  level=%d  (%s)\r\n",
           PIN_ARM_SENSE, gpio_get_level(PIN_ARM_SENSE),
           gpio_get_level(PIN_ARM_SENSE) ? "ARMED (arm relay closed)" : "DISARMED (arm relay open)");
    printf("Arm relay       : GPIO %d  (IRLZ44N MOSFET, fire path interlock)\r\n", PIN_ARM_SIM_RELAY);
    printf("Siren           : GPIO %d  (via IRLZ44N MOSFET)\r\n", PIN_SIREN);
    printf("RGB LED strip   : GPIO %d  (WS2812, %d pixels + on-board mirror)\r\n", PIN_RGB_LED, NUM_RGB_LEDS);
    printf("Spare GPIOs     : 38, 39, 41, 42\r\n");
}

static void cmd_relay(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: relay <ch> on|off  |  relay all off  |  relay sweep\r\n"); return; }

    if (strcmp(toks[1], "sweep") == 0) {
        uart_puts("Relay sweep: 8 SPDT channels + arm relay x 500 ms\r\n");
        relay_sweep();
        uart_puts("Sweep complete.\r\n");
        return;
    }
    if (strcmp(toks[1], "all") == 0) {
        if (ntok >= 3 && strcmp(toks[2], "off") == 0) {
            relay_all_off();
            uart_puts("All SPDT relays de-energised.\r\n");
        } else {
            uart_puts("Unknown relay all subcommand.\r\n");
        }
        return;
    }

    int ch = atoi(toks[1]);
    if (ch < 1 || ch > 8) { uart_puts("Channel must be 1-8.\r\n"); return; }
    if (ntok < 3) { uart_puts("Usage: relay <ch> on|off\r\n"); return; }

    if (strcmp(toks[2], "on") == 0) {
        relay_set(ch, 1);
        printf("SPDT relay CH%d energised (NC->NO)\r\n", ch);
    } else if (strcmp(toks[2], "off") == 0) {
        relay_set(ch, 0);
        printf("SPDT relay CH%d de-energised (->NC)\r\n", ch);
    } else {
        uart_puts("Expected 'on' or 'off'.\r\n");
    }
}

static void cmd_cont(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: cont <ch|all|monitor>\r\n"); return; }

    if (strcmp(toks[1], "monitor") == 0) {
        uart_puts("Monitoring all channels — press any key to stop.\r\n");
        cont_band_t prev[8] = { CONT_BAND_OPEN, CONT_BAND_OPEN, CONT_BAND_OPEN, CONT_BAND_OPEN,
                                CONT_BAND_OPEN, CONT_BAND_OPEN, CONT_BAND_OPEN, CONT_BAND_OPEN };
        uint8_t dummy;
        while (usb_serial_jtag_read_bytes(&dummy, 1, 0) <= 0) {
            for (int ch = 1; ch <= 8; ch++) {
                cont_reading_t r = cont_read(ch);
                if (r.band != prev[ch - 1]) {
                    printf("CH%d: %s -> %s  (%"PRId32" uV)\r\n",
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
            printf("CH%d: raw=%d  %"PRId32" uV  %s\r\n",
                   ch, r.raw, r.uv, cont_band_str(r.band));
        }
        return;
    }

    int ch = atoi(toks[1]);
    if (ch < 1 || ch > 8) { uart_puts("Channel must be 1-8.\r\n"); return; }

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
    printf("CH%d: raw=%d  %"PRId32" uV  %s\r\n",
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

static void cmd_arm(char *toks[], int ntok)
{
    if (ntok >= 2 && strcmp(toks[1], "sim") == 0) {
        if (ntok < 3) { uart_puts("Usage: arm sim on|off\r\n"); return; }
        if (strcmp(toks[2], "on") == 0) {
            arm_sim_set(1);
            uart_puts("Arm sim relay ON — simulating ARMED.\r\n");
        } else if (strcmp(toks[2], "off") == 0) {
            arm_sim_set(0);
            uart_puts("Arm sim relay OFF — simulating DISARMED.\r\n");
        } else {
            uart_puts("Expected 'on' or 'off'.\r\n");
        }
        return;
    }

    uart_puts("Polling arm sense (GPIO 21, ARM SENSE node) — press any key to stop.\r\n");
    uart_puts("HIGH = ARMED (VBAT on fire path), LOW = DISARMED\r\n");
    uint8_t dummy;
    int last = -1;
    while (usb_serial_jtag_read_bytes(&dummy, 1, 0) <= 0) {
        int armed = arm_sense_read_debounced();
        int raw   = arm_sense_read_raw();
        if (armed != last) {
            printf("Arm sense: raw=%d  %s\r\n", raw, armed ? "ARMED" : "DISARMED");
            last = armed;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    uart_puts("Stopped.\r\n");
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
        printf("Siren pulse: %"PRIu32" ms on / %"PRIu32" ms off x %d\r\n", on_ms, off_ms, count);
        siren_pulse(on_ms, off_ms, count);
    } else {
        uart_puts("Unknown siren subcommand.\r\n");
    }
}

static void cmd_led(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: led <r> <g> <b>  |  led off  |  led test  |  led strip  |  led brightness <n>\r\n"); return; }

    if (strcmp(toks[1], "off") == 0) {
        led_off();
        uart_puts("LED off\r\n");
    } else if (strcmp(toks[1], "strip") == 0) {
        uart_puts("Running 8-pixel LED strip diagnostic...\r\n");
        led_strip_test();
    } else if (strcmp(toks[1], "gpiotest") == 0) {
        /* Raw GPIO diagnostic — bypasses led_strip, drives GPIO 48 directly */
        uart_puts("GPIO48 raw toggle test (3 blinks)...\r\n");
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << PIN_RGB_LED),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        for (int i = 0; i < 3; i++) {
            gpio_set_level(PIN_RGB_LED, 1);
            printf("  GPIO %d HIGH\r\n", PIN_RGB_LED);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(PIN_RGB_LED, 0);
            printf("  GPIO %d LOW\r\n", PIN_RGB_LED);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        uart_puts("GPIO48 test done — re-init led_strip.\r\n");
        hw_rgb_led_init();
    } else if (strcmp(toks[1], "test") == 0) {
        uart_puts("Running LED pattern test (FSD v1.10 §11.1)...\r\n");
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

    if (ch < 1 || ch > 8) { uart_puts("Channel must be 1-8.\r\n"); return; }
    if (duration == 0)     { uart_puts("Duration must be > 0 ms.\r\n"); return; }

    printf("Fire CH%d for %"PRIu32" ms%s (SPDT relay NC->NO)...\r\n",
           ch, duration, safe_after ? " (safe after)" : " (nosafe)");
    int elapsed = fire_pulse(ch, duration, safe_after);
    printf("Fire complete. Elapsed: %d ms  (requested: %"PRIu32" ms)\r\n", elapsed, duration);
}

static void cmd_gpio(char *toks[], int ntok)
{
    if (ntok < 3) { uart_puts("Usage: gpio <pin> <0|1>\r\n"); return; }
    int pin = atoi(toks[1]);
    int lvl = atoi(toks[2]);
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(pin, lvl);
    printf("GPIO %d driven %d  (readback=%d)\r\n", pin, lvl, gpio_get_level(pin));
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
    else if (strcmp(toks[0], "exit")     == 0) uart_puts("Press Ctrl+] to exit the IDF monitor.\r\n");
    else if (strcmp(toks[0], "relay")    == 0) cmd_relay(toks, ntok);
    else if (strcmp(toks[0], "cont")     == 0) cmd_cont(toks, ntok);
    else if (strcmp(toks[0], "batt")     == 0) cmd_batt(toks, ntok);
    else if (strcmp(toks[0], "arm")      == 0) cmd_arm(toks, ntok);
    else if (strcmp(toks[0], "siren")    == 0) cmd_siren(toks, ntok);
    else if (strcmp(toks[0], "led")      == 0) cmd_led(toks, ntok);
    else if (strcmp(toks[0], "fire")     == 0) cmd_fire(toks, ntok);
    else if (strcmp(toks[0], "gpio")     == 0) cmd_gpio(toks, ntok);
    else uart_puts("Unknown command. Type 'help' for usage.\r\n");
}

/* ------------------------------------------------------------------ */
/* Init and task                                                        */
/* ------------------------------------------------------------------ */

void cli_init(void)
{
    /* USB-Serial/JTAG on ESP32-S3 native USB (GPIO 19/20) → /dev/ttyACM0.
     * Single-cable flash + console. Baud rate is ignored (USB CDC). */
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = 1024,
        .tx_buffer_size = 4096,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    ESP_LOGI(TAG, "CLI USB-Serial/JTAG initialised");
}

void cli_task(void *arg)
{
    char buf[CLI_BUF_SIZE];

    uart_puts("\r\n=== RLC Base Unit Hardware Test (FSD v1.10) ===\r\n");
    uart_puts("Type 'help' for available commands.\r\n");

    while (1) {
        uart_puts(PROMPT);
        uart_readline(buf, sizeof(buf));
        dispatch(buf);
    }
}
