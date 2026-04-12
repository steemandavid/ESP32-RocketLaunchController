#include "cli.h"
#include "pin_config.h"
#include "hw_encoder.h"
#include "hw_buttons.h"
#include "hw_display.h"
#include "hw_buzzer.h"
#include "hw_battery.h"
#include "hw_rgb_led.h"
#include "hw_leds.h"
#include "driver/usb_serial_jtag.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>

#define CLI_BUF_SIZE    256
#define PROMPT          "\r\nremote> "

static const char *TAG = "cli";

/* ------------------------------------------------------------------ */
/* Console helpers (USB-Serial/JTAG)                                    */
/* ------------------------------------------------------------------ */

static void uart_puts(const char *s)
{
    usb_serial_jtag_write_bytes(s, strlen(s), portMAX_DELAY);
}

static void uart_putc(char c)
{
    usb_serial_jtag_write_bytes(&c, 1, portMAX_DELAY);
}

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
            if (pos > 0) { pos--; uart_puts("\b \b"); }
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
        "  pins                         Print pin assignments\r\n"
        "  exit                         Exit help\r\n");
    uart_puts(
        "\r\nEncoder:\r\n"
        "  enc monitor                  Monitor encoder events (any key stops)\r\n"
        "  enc count                    Show rotation count\r\n"
        "  enc reset                    Reset rotation count\r\n"
        "  enc channel                  Simulate channel selection (any key stops)\r\n"
        "  enc button                   Monitor push button (any key stops)\r\n"
        "  enc longpress                Test long-press detection (any key stops)\r\n");
    uart_puts(
        "\r\nButtons:\r\n"
        "  fire monitor                 Monitor fire button (any key stops)\r\n"
        "  fire fresh                   Test fresh-press detection (any key stops)\r\n"
        "  arm monitor                  Monitor arm switch (any key stops)\r\n");
    uart_puts(
        "\r\nDisplay (ILI9341):\r\n"
        "  disp init                    Initialise display and read ID\r\n"
        "  disp id                      Read display ID\r\n"
        "  disp fill <r> <g> <b>        Fill screen with colour\r\n"
        "  disp test                    Run test patterns\r\n"
        "  disp text <string>           Display text (white on black)\r\n"
        "  disp gradient                Horizontal gradient\r\n"
        "  disp speed                   Measure fill speed\r\n"
        "  disp pixel <x> <y> <r> <g> <b>  Set single pixel\r\n"
        "  disp rect <x> <y> <w> <h> <r> <g> <b>  Draw rectangle\r\n"
        "  disp backlight on|off        Control backlight\r\n");
    uart_puts(
        "\r\nBuzzer:\r\n"
        "  buzz on|off                  Activate/deactivate\r\n"
        "  buzz beep <ms>               Single beep\r\n"
        "  buzz pattern <on> <off> <N>  Custom pattern\r\n"
        "  buzz test                    Run all FSD patterns\r\n");
    uart_puts(
        "\r\nBattery:\r\n"
        "  batt                         Read battery voltage\r\n"
        "  batt raw [N]                 Raw ADC statistics\r\n");
    uart_puts(
        "\r\nRGB LED:\r\n"
        "  led <r> <g> <b>              Set RGB colour\r\n"
        "  led off                      Turn off\r\n"
        "  led test                     Cycle status patterns\r\n"
        "  led brightness <0-255>       Set brightness\r\n");
    uart_puts(
        "\r\nIndicator LEDs:\r\n"
        "  leds arm on|off              Arm switch LED (red)\r\n"
        "  leds fire red on|off         Fire button LED (red)\r\n"
        "  leds fire green on|off       Fire button LED (green)\r\n"
        "  leds off                     All indicator LEDs off\r\n");
    uart_puts(
        "\r\nDebounce:\r\n"
        "  debounce fire                Fire button shift register (any key stops)\r\n"
        "  debounce arm                 Arm switch shift register (any key stops)\r\n"
        "  debounce encoder             Encoder SW shift register (any key stops)\r\n");
    uart_puts(
        "\r\nGPIO:\r\n"
        "  gpio <pin> <0|1>             Drive GPIO for debugging\r\n");
}

static void cmd_status(void)
{
    uart_puts("=== System Status ===\r\n");

    printf("Encoder count   : %d\r\n", enc_get_count());
    printf("Arm switch      : raw=%d  %s\r\n", arm_read_raw(), arm_read_debounced() ? "ARMED" : "DISARMED");
    printf("Fire button     : raw=%d  %s\r\n", fire_read_raw(), fire_read_debounced() ? "PRESSED" : "RELEASED");
    printf("Encoder SW      : raw=%d  %s\r\n", enc_sw_read_raw(), enc_sw_read_debounced() ? "PRESSED" : "RELEASED");

    batt_reading_t batt = batt_read();
    printf("Battery         : raw=%d  pin=%"PRId32" mV  scaled=%"PRId32" mV\r\n",
           batt.raw, batt.mv_calibrated, batt.mv_scaled);
}

static void cmd_pins(void)
{
    uart_puts("=== Pin Assignments ===\r\n");
    printf("Batt ADC        : GPIO %d\r\n", PIN_BATT_ADC);
    printf("Encoder A       : GPIO %d\r\n", PIN_ENCODER_A);
    printf("Encoder B       : GPIO %d\r\n", PIN_ENCODER_B);
    printf("Encoder SW      : GPIO %d\r\n", PIN_ENCODER_SW);
    printf("Arm switch      : GPIO %d\r\n", PIN_ARM_SWITCH);
    printf("Arm LED (red)   : GPIO %d\r\n", PIN_ARM_LED);
    printf("Fire button     : GPIO %d\r\n", PIN_FIRE_BUTTON);
    printf("Fire LED (red)  : GPIO %d\r\n", PIN_FIRE_LED_RED);
    printf("Fire LED (green): GPIO %d\r\n", PIN_FIRE_LED_GREEN);
    printf("Buzzer          : GPIO %d\r\n", PIN_BUZZER);
    printf("Display MOSI    : GPIO %d\r\n", PIN_DISP_MOSI);
    printf("Display SCLK    : GPIO %d\r\n", PIN_DISP_SCLK);
    printf("Display MISO    : GPIO %d\r\n", PIN_DISP_MISO);
    printf("Display CS      : GPIO %d\r\n", PIN_DISP_CS);
    printf("Display DC      : GPIO %d\r\n", PIN_DISP_DC);
    printf("Display RST     : GPIO %d\r\n", PIN_DISP_RST);
    printf("Display BL      : GPIO %d\r\n", PIN_DISP_BACKLIGHT);
    printf("RGB LED         : GPIO %d\r\n", PIN_RGB_LED);
    printf("Spare GPIOs     : 2, 38, 39, 40, 41, 42\r\n");
}

/* --- Encoder commands -------------------------------------------------- */

static void cmd_enc(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: enc monitor|count|reset|channel|button|longpress\r\n"); return; }

    if (strcmp(toks[1], "count") == 0) {
        printf("Encoder count: %d\r\n", enc_get_count());
        return;
    }
    if (strcmp(toks[1], "reset") == 0) {
        enc_reset_count();
        uart_puts("Encoder count reset to 0.\r\n");
        return;
    }
    if (strcmp(toks[1], "monitor") == 0) {
        uart_puts("Monitoring encoder — press any key to stop.\r\n");
        int last = enc_get_count();
        uint8_t dummy;
        while (usb_serial_jtag_read_bytes(&dummy, 1, 0) <= 0) {
            int c = enc_get_count();
            int d = enc_get_direction();
            if (c != last) {
                printf("Count: %d  dir: %s\r\n", c, d > 0 ? "CW" : "CCW");
                last = c;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        uart_puts("Stopped.\r\n");
        return;
    }
    if (strcmp(toks[1], "channel") == 0) {
        int ch = 1;
        printf("Channel: %d  (rotate encoder, any key stops)\r\n", ch);
        int last = enc_get_count();
        uint8_t dummy;
        while (usb_serial_jtag_read_bytes(&dummy, 1, 0) <= 0) {
            int c = enc_get_count();
            if (c != last) {
                int diff = c - last;
                ch += diff;
                while (ch > 8) ch -= 8;
                while (ch < 1) ch += 8;
                printf("Channel: %d\r\n", ch);
                last = c;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        uart_puts("Stopped.\r\n");
        return;
    }
    if (strcmp(toks[1], "button") == 0) {
        uart_puts("Monitoring encoder SW — press any key to stop.\r\n");
        uint8_t dummy;
        while (usb_serial_jtag_read_bytes(&dummy, 1, 0) <= 0) {
            int raw = enc_sw_read_raw();
            int db  = enc_sw_read_debounced();
            printf("SW: raw=%d  debounced=%s\r\n", raw, db ? "PRESSED" : "RELEASED");
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        uart_puts("Stopped.\r\n");
        return;
    }
    if (strcmp(toks[1], "longpress") == 0) {
        uart_puts("Long-press test — press encoder SW. Any key stops.\r\n");
        uint8_t dummy;
        int was_pressed = 0;
        int64_t press_time = 0;
        while (usb_serial_jtag_read_bytes(&dummy, 1, 0) <= 0) {
            int db = enc_sw_read_debounced();
            if (db && !was_pressed) {
                press_time = esp_timer_get_time();
                printf("PRESS detected\r\n");
            }
            if (!db && was_pressed) {
                int64_t dur = (esp_timer_get_time() - press_time) / 1000;
                printf("%s (%lld ms)\r\n", dur >= 500 ? "LONG PRESS" : "SHORT PRESS", (long long)dur);
            }
            was_pressed = db;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        uart_puts("Stopped.\r\n");
        return;
    }
    uart_puts("Unknown enc subcommand.\r\n");
}

/* --- Fire button commands ---------------------------------------------- */

static void cmd_fire(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: fire monitor|fresh\r\n"); return; }

    if (strcmp(toks[1], "monitor") == 0) {
        uart_puts("Monitoring fire button — press any key to stop.\r\n");
        uint8_t dummy;
        while (usb_serial_jtag_read_bytes(&dummy, 1, 0) <= 0) {
            int raw = fire_read_raw();
            int sr  = fire_get_shift_reg();
            int db  = fire_read_debounced();
            printf("Fire: raw=%d  SR=0x%02X  %s\r\n", raw, sr, db ? "PRESSED" : "RELEASED");
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        uart_puts("Stopped.\r\n");
    } else if (strcmp(toks[1], "fresh") == 0) {
        uart_puts("Fresh-press test — press fire button. Any key stops.\r\n");
        /* Reset fresh-press tracking */
        hw_buttons_init();
        uint8_t dummy;
        while (usb_serial_jtag_read_bytes(&dummy, 1, 0) <= 0) {
            if (fire_fresh_press()) {
                printf("FRESH PRESS detected!\r\n");
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        uart_puts("Stopped.\r\n");
    } else {
        uart_puts("Unknown fire subcommand.\r\n");
    }
}

/* --- Arm switch commands ----------------------------------------------- */

static void cmd_arm(char *toks[], int ntok)
{
    if (ntok < 2 || strcmp(toks[1], "monitor") == 0) {
        uart_puts("Monitoring arm switch — press any key to stop.\r\n");
        uint8_t dummy;
        while (usb_serial_jtag_read_bytes(&dummy, 1, 0) <= 0) {
            int raw = arm_read_raw();
            uint16_t sr = arm_get_shift_reg();
            int db  = arm_read_debounced();
            printf("Arm: raw=%d  SR=0x%04X  %s\r\n", raw, sr, db ? "ARMED" : "DISARMED");
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        uart_puts("Stopped.\r\n");
    } else {
        uart_puts("Usage: arm monitor\r\n");
    }
}

/* --- Display commands -------------------------------------------------- */

static void cmd_disp(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: disp init|id|fill|test|text|gradient|speed|pixel|rect|backlight\r\n"); return; }

    if (strcmp(toks[1], "init") == 0) {
        uart_puts("Initialising display...\r\n");
        hw_display_init();
        uint32_t id = 0;
        bool ok = display_read_id(&id);
        printf("Display ID: 0x%08" PRIX32 "  (%s)\r\n", id, ok ? "read OK" : "read FAILED");
    } else if (strcmp(toks[1], "id") == 0) {
        uint32_t id = 0;
        bool ok = display_read_id(&id);
        printf("Display ID: 0x%08" PRIX32 "  (%s)\r\n", id, ok ? "read OK" : "read FAILED");
    } else if (strcmp(toks[1], "fill") == 0) {
        if (ntok < 5) { uart_puts("Usage: disp fill <r> <g> <b>\r\n"); return; }
        display_fill(atoi(toks[2]), atoi(toks[3]), atoi(toks[4]));
        printf("Filled (%s,%s,%s)\r\n", toks[2], toks[3], toks[4]);
    } else if (strcmp(toks[1], "test") == 0) {
        display_test();
    } else if (strcmp(toks[1], "text") == 0) {
        if (ntok < 3) { uart_puts("Usage: disp text <string>\r\n"); return; }
        display_text(toks[2]);
        printf("Text displayed: %s\r\n", toks[2]);
    } else if (strcmp(toks[1], "gradient") == 0) {
        display_gradient();
        uart_puts("Gradient displayed.\r\n");
    } else if (strcmp(toks[1], "speed") == 0) {
        display_speed();
    } else if (strcmp(toks[1], "pixel") == 0) {
        if (ntok < 7) { uart_puts("Usage: disp pixel <x> <y> <r> <g> <b>\r\n"); return; }
        display_pixel(atoi(toks[2]), atoi(toks[3]), atoi(toks[4]), atoi(toks[5]), atoi(toks[6]));
        printf("Pixel set at (%s,%s)\r\n", toks[2], toks[3]);
    } else if (strcmp(toks[1], "rect") == 0) {
        if (ntok < 9) { uart_puts("Usage: disp rect <x> <y> <w> <h> <r> <g> <b>\r\n"); return; }
        display_rect(atoi(toks[2]), atoi(toks[3]), atoi(toks[4]), atoi(toks[5]),
                     atoi(toks[6]), atoi(toks[7]), atoi(toks[8]));
        printf("Rect drawn.\r\n");
    } else if (strcmp(toks[1], "backlight") == 0) {
        if (ntok < 3) { uart_puts("Usage: disp backlight on|off\r\n"); return; }
        display_backlight(strcmp(toks[2], "on") == 0 ? 1 : 0);
        printf("Backlight %s\r\n", strcmp(toks[2], "on") == 0 ? "ON" : "OFF");
    } else {
        uart_puts("Unknown disp subcommand.\r\n");
    }
}

/* --- Buzzer commands --------------------------------------------------- */

static void cmd_buzz(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: buzz on|off|beep|pattern|test\r\n"); return; }

    if (strcmp(toks[1], "on") == 0) {
        buzzer_set(1);
        uart_puts("Buzzer ON\r\n");
    } else if (strcmp(toks[1], "off") == 0) {
        buzzer_set(0);
        uart_puts("Buzzer OFF\r\n");
    } else if (strcmp(toks[1], "test") == 0) {
        buzzer_test();
    } else if (strcmp(toks[1], "beep") == 0) {
        if (ntok < 3) { uart_puts("Usage: buzz beep <ms>\r\n"); return; }
        uint32_t ms = atoi(toks[2]);
        printf("Beep %"PRIu32" ms\r\n", ms);
        buzzer_beep(ms);
    } else if (strcmp(toks[1], "pattern") == 0) {
        if (ntok < 5) { uart_puts("Usage: buzz pattern <on_ms> <off_ms> <count>\r\n"); return; }
        uint32_t on_ms  = atoi(toks[2]);
        uint32_t off_ms = atoi(toks[3]);
        int count = atoi(toks[4]);
        printf("Pattern: %"PRIu32"/%"PRIu32" x%d\r\n", on_ms, off_ms, count);
        buzzer_pattern(on_ms, off_ms, count);
    } else {
        uart_puts("Unknown buzz subcommand.\r\n");
    }
}

/* --- Battery commands -------------------------------------------------- */

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

/* --- LED commands ------------------------------------------------------ */

static void cmd_led(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: led <r> <g> <b>  |  led off  |  led test  |  led brightness <n>\r\n"); return; }

    if (strcmp(toks[1], "off") == 0) {
        led_off();
        uart_puts("LED off\r\n");
    } else if (strcmp(toks[1], "test") == 0) {
        uart_puts("Running LED pattern test (FSD §11.2)...\r\n");
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
        int r = atoi(toks[1]), g = atoi(toks[2]), b = atoi(toks[3]);
        led_set((uint8_t)r, (uint8_t)g, (uint8_t)b);
        printf("LED set to R=%d G=%d B=%d\r\n", r, g, b);
    }
}

/* --- Indicator LED commands -------------------------------------------- */

static void cmd_leds(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: leds arm on|off  |  leds fire red on|off  |  leds fire green on|off  |  leds off\r\n"); return; }

    if (strcmp(toks[1], "off") == 0) {
        all_leds_off();
        uart_puts("All indicator LEDs off.\r\n");
    } else if (strcmp(toks[1], "arm") == 0) {
        if (ntok < 3) { uart_puts("Usage: leds arm on|off\r\n"); return; }
        arm_led_set(strcmp(toks[2], "on") == 0);
        printf("Arm LED %s\r\n", strcmp(toks[2], "on") == 0 ? "ON" : "OFF");
    } else if (strcmp(toks[1], "fire") == 0) {
        if (ntok < 4) { uart_puts("Usage: leds fire red|green on|off\r\n"); return; }
        int on = strcmp(toks[3], "on") == 0;
        if (strcmp(toks[2], "red") == 0) {
            fire_led_red(on);
            printf("Fire LED red %s\r\n", on ? "ON" : "OFF");
        } else if (strcmp(toks[2], "green") == 0) {
            fire_led_green(on);
            printf("Fire LED green %s\r\n", on ? "ON" : "OFF");
        } else {
            uart_puts("Expected 'red' or 'green'.\r\n");
        }
    } else {
        uart_puts("Unknown leds subcommand.\r\n");
    }
}

/* --- Debounce visualisation commands ----------------------------------- */

static void cmd_debounce(char *toks[], int ntok)
{
    if (ntok < 2) { uart_puts("Usage: debounce fire|arm|encoder\r\n"); return; }

    uart_puts("Debounce visualisation — press any key to stop.\r\n");
    uint8_t dummy;

    if (strcmp(toks[1], "fire") == 0) {
        while (usb_serial_jtag_read_bytes(&dummy, 1, 0) <= 0) {
            int sr = fire_get_shift_reg();
            char bin[9];
            for (int i = 7; i >= 0; i--) bin[7-i] = (sr & (1<<i)) ? '1' : '0';
            bin[8] = '\0';
            printf("Fire SR: %s (0x%02X)\r\n", bin, sr);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } else if (strcmp(toks[1], "arm") == 0) {
        while (usb_serial_jtag_read_bytes(&dummy, 1, 0) <= 0) {
            uint16_t sr = arm_get_shift_reg();
            printf("Arm SR: 0x%04X\r\n", sr);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } else if (strcmp(toks[1], "encoder") == 0) {
        while (usb_serial_jtag_read_bytes(&dummy, 1, 0) <= 0) {
            uint16_t sr = enc_sw_get_shift_reg();
            printf("Enc SW SR: 0x%04X\r\n", sr);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } else {
        uart_puts("Unknown debounce target.\r\n");
        return;
    }
    uart_puts("Stopped.\r\n");
}

/* --- GPIO debug command ------------------------------------------------ */

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
    else if (strcmp(toks[0], "pins")     == 0) cmd_pins();
    else if (strcmp(toks[0], "exit")     == 0) uart_puts("Press Ctrl+] to exit the IDF monitor.\r\n");
    else if (strcmp(toks[0], "enc")      == 0) cmd_enc(toks, ntok);
    else if (strcmp(toks[0], "fire")     == 0) cmd_fire(toks, ntok);
    else if (strcmp(toks[0], "arm")      == 0) cmd_arm(toks, ntok);
    else if (strcmp(toks[0], "disp")     == 0) cmd_disp(toks, ntok);
    else if (strcmp(toks[0], "buzz")     == 0) cmd_buzz(toks, ntok);
    else if (strcmp(toks[0], "batt")     == 0) cmd_batt(toks, ntok);
    else if (strcmp(toks[0], "led")      == 0) cmd_led(toks, ntok);
    else if (strcmp(toks[0], "leds")     == 0) cmd_leds(toks, ntok);
    else if (strcmp(toks[0], "debounce") == 0) cmd_debounce(toks, ntok);
    else if (strcmp(toks[0], "gpio")     == 0) cmd_gpio(toks, ntok);
    else uart_puts("Unknown command. Type 'help' for usage.\r\n");
}

/* ------------------------------------------------------------------ */
/* Init and task                                                        */
/* ------------------------------------------------------------------ */

void cli_init(void)
{
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

    uart_puts("\r\n=== RLC Remote Unit Hardware Test ===\r\n");
    uart_puts("Type 'help' for available commands.\r\n");

    while (1) {
        uart_puts(PROMPT);
        uart_readline(buf, sizeof(buf));
        dispatch(buf);
    }
}
