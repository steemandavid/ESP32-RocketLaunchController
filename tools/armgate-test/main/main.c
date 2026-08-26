/**
 * RLC arm-relay AND-gate verifier.
 *
 * Standalone bring-up firmware that proves the base unit's hardware AND gate
 * (FSD 5.4.4) electrically, at the ARM SENSE node, independently of the
 * indicator LEDs and of the RLC firmware's FSM.
 *
 * Why it exists (2026-08-26): bug #28 and its sibling — the ARM RELAY LED
 * lighting with the key in SAFE, and the arm-key red/green LEDs lighting
 * together — were both sneak paths in the *indicator* wiring. That wiring has
 * just been reworked. The LEDs are normally how an operator judges arm relay
 * state, so after touching them the LEDs are exactly what must not be trusted
 * as the instrument. This checks the node instead.
 *
 * The gate under test has two independent legs, and neither alone may
 * energise the arm relay:
 *
 *     key switch ON  (VBAT to the arm relay coil +, via the key switch COM->NO)
 *   AND GPIO 47 HIGH (IRLZ44N sinks the coil - to GND)
 *
 * ARM SENSE (GPIO 21) reads the arm relay COM output through a 27k/10k
 * divider: HIGH = VBAT present on the fire path, LOW = fire path broken.
 *
 * ┌──────┬─────────┬───────────────────────────────────────┐
 * │ Key  │ GPIO 47 │                Expect                 │
 * ├──────┼─────────┼───────────────────────────────────────┤
 * │ SAFE │ driven  │ relay out, ARM SENSE (GPIO 21) = 0    │
 * │ ON   │ low     │ relay out, ARM SENSE = 0              │
 * │ ON   │ driven  │ relay in, ARM SENSE = 1, coil LED lit │
 * └──────┴─────────┴───────────────────────────────────────┘
 *
 * Three extra steps are run beyond that table, because each covers a failure
 * the three rows cannot see on their own — see the step comments below.
 *
 * The operator moves the key; the firmware moves GPIO 47. Key position is
 * read from KEY SENSE (GPIO 42), so the program waits for the right position
 * rather than asking anyone to type — hands stay on the hardware. Step 0
 * validates KEY SENSE first, since every later step relies on it.
 *
 * SAFETY: this energises the arm relay, which puts VBAT on the fire bus.
 * The channel relays are held de-energised throughout, so no current can
 * reach an igniter — but DISCONNECT ALL IGNITERS before running it anyway.
 * The whole point of the exercise is that the interlock is not yet trusted.
 *
 * Build/flash:  cd tools/armgate-test && ./run.sh    (sources ESP-IDF itself)
 *   (base by-id as of 2026-08-26:
 *    /dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E042156-if00)
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Pins duplicated from components/rlc_common/include/pin_config.h rather than
 * included: this tool is deliberately standalone, so it still builds and runs
 * if the firmware tree is mid-edit. Keep them in step with pin_config.h. */
#define PIN_ARM_RELAY   47   /* out, active HIGH — IRLZ44N gate, coil low side */
#define PIN_ARM_SENSE   21   /* in  — arm relay COM via 27k/10k divider */
#define PIN_KEY_SENSE   42   /* in  — key switch NO via 27k/10k divider */
#define PIN_SIREN       40   /* out, active HIGH — held OFF here */

static const int PIN_RELAY_CH[8] = { 11, 12, 13, 14, 15, 16, 17, 18 };

/* Relay pull-in and drop-out both settle well inside this. Everything before
 * SETTLE_MS is discarded; the verdict comes from the HOLD_MS window after. */
#define SETTLE_MS    150
#define HOLD_MS      2000
#define SAMPLE_MS    10

static const char *TAG = "armgate";

/* ── Console (UART0 — the CH340 bridge) ────────────────────────
 *
 * UART0, NOT the ESP32-S3's native USB-Serial/JTAG port. Both base and remote
 * are reached through the CH340 USB-to-UART bridge on the board (the
 * `usb-1a86_USB_Single_Serial_*` by-id), and the RLC firmware itself is built
 * the same way — UART0 primary console, USB-Serial/JTAG secondary. A tool that
 * talks to the native USB port instead appears to boot-loop: the ROM banner
 * still reaches UART0, but nothing after it does.
 *
 * The VFS driver is installed so getchar() blocks. Without it stdin is
 * non-blocking and every prompt returns EOF instantly.
 */

static void console_init(void)
{
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));
    uart_vfs_dev_use_driver(UART_NUM_0);
    /* idf_monitor sends CR for ENTER; the terminal wants CRLF back. */
    uart_vfs_dev_port_set_rx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CRLF);
}

static void con_puts(const char *s)
{
    fputs(s, stdout);
    fflush(stdout);
}

static void con_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

/* Blocking single-key read, echoed so the operator sees what landed. */
static char con_getc(void)
{
    int c;
    do {
        c = getchar();
        if (c == EOF) vTaskDelay(pdMS_TO_TICKS(10));
    } while (c == EOF);
    putchar(c);
    fflush(stdout);
    return (char)c;
}

/* ── Sampling ──────────────────────────────────────────────────── */

/**
 * Watch one input for HOLD_MS and report what it did.
 *
 * A single gpio_get_level() would miss the failure this tool is looking for:
 * a sneak path can be marginal, and relay contacts bounce. So sample the
 * whole window and report the count of HIGH samples, not just a level. Any
 * mixture at all is a finding in its own right — every step here expects a
 * rock-steady line.
 *
 * @param pin        input to watch
 * @param[out] highs number of HIGH samples
 * @return           total samples taken
 */
static int watch(int pin, int *highs)
{
    int n = 0;
    *highs = 0;
    for (int t = 0; t < HOLD_MS; t += SAMPLE_MS) {
        if (gpio_get_level(pin)) (*highs)++;
        n++;
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
    return n;
}

/* ── Results ───────────────────────────────────────────────────── */

#define MAX_STEPS 8

static struct {
    const char *name;
    bool        pass;
    char        detail[96];
} s_result[MAX_STEPS];

static int  s_steps = 0;
static bool s_all_pass = true;

static void record(const char *name, bool pass, const char *detail)
{
    if (s_steps >= MAX_STEPS) return;
    s_result[s_steps].name = name;
    s_result[s_steps].pass = pass;
    snprintf(s_result[s_steps].detail, sizeof(s_result[s_steps].detail), "%s", detail);
    s_steps++;
    if (!pass) s_all_pass = false;
    con_printf("  --> %s  %s\n\n", pass ? "PASS" : "**FAIL**", detail);
}

/**
 * Drive GPIO 47, let the relay settle, watch ARM SENSE, and judge it against
 * the level this configuration of the AND gate is supposed to produce.
 */
static void step_sense(const char *name, bool relay_drive, int expect_high)
{
    con_printf("%s\n", name);
    con_printf("  GPIO %d -> %s, settling %d ms...\n",
               PIN_ARM_RELAY, relay_drive ? "HIGH (driven)" : "LOW", SETTLE_MS);

    gpio_set_level(PIN_ARM_RELAY, relay_drive ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));

    int highs, n = watch(PIN_ARM_SENSE, &highs);
    char detail[96];

    if (highs == n) {
        snprintf(detail, sizeof(detail), "ARM SENSE steady HIGH (%d/%d)", highs, n);
        record(name, expect_high == 1, detail);
    } else if (highs == 0) {
        snprintf(detail, sizeof(detail), "ARM SENSE steady LOW (0/%d)", n);
        record(name, expect_high == 0, detail);
    } else {
        /* Never acceptable, whichever level was expected: a line that cannot
         * hold a level is not an interlock you can reason about. */
        snprintf(detail, sizeof(detail), "ARM SENSE UNSTABLE — %d/%d samples HIGH", highs, n);
        record(name, false, detail);
    }
}

/* ── Key switch ────────────────────────────────────────────────── */

static bool key_is_on(void) { return gpio_get_level(PIN_KEY_SENSE) == 1; }

/**
 * Block until the key has been in the requested position, steadily, for
 * 300 ms. Steady rather than instantaneous so a half-turn resting between
 * detents cannot start a step.
 */
static void wait_key(bool want_on)
{
    const char *pos = want_on ? "ARM (ON)" : "SAFE";
    if (key_is_on() != want_on) {
        con_printf("  >>> Turn the key to %s ...\n", pos);
    }
    int stable = 0;
    while (stable < 300 / SAMPLE_MS) {
        stable = (key_is_on() == want_on) ? stable + 1 : 0;
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
    con_printf("  Key confirmed %s (KEY SENSE GPIO %d = %d)\n",
               pos, PIN_KEY_SENSE, want_on ? 1 : 0);
}

/* ── Main ──────────────────────────────────────────────────────── */

static void safe_outputs(void)
{
    /* Boot safety, FSD 9.7: every relay output driven inactive before
     * anything else runs. ESP32-S3 GPIOs are high-impedance from power-on
     * until configured, and the channel MOSFET gate pull-downs are what hold
     * the relays out during that window — this closes it. */
    for (int i = 0; i < 8; i++) {
        gpio_config_t c = {
            .pin_bit_mask = 1ULL << PIN_RELAY_CH[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&c);
        gpio_set_level(PIN_RELAY_CH[i], 0);
    }

    gpio_config_t o = {
        .pin_bit_mask = (1ULL << PIN_ARM_RELAY) | (1ULL << PIN_SIREN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&o);
    gpio_set_level(PIN_ARM_RELAY, 0);
    gpio_set_level(PIN_SIREN, 0);

    /* No internal pulls on either sense input — both have an external
     * divider and clamp, and a pull would fight the divider. */
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_ARM_SENSE) | (1ULL << PIN_KEY_SENSE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in);
}

void app_main(void)
{
    safe_outputs();

    /* Console after the outputs are safe, never before: an ESP_ERROR_CHECK
     * abort inside the driver install would otherwise leave every relay GPIO
     * floating. */
    console_init();
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGI(TAG, "all relay outputs driven inactive, siren off");

    con_puts("\n\n"
        "==========================================================\n"
        "  RLC arm-relay AND-gate verifier   (FSD 5.4.4)\n"
        "==========================================================\n"
        "\n"
        "  Proves the arm relay interlock AT THE NODE, not from the\n"
        "  indicator LEDs. Run after any rework of the arm wiring.\n"
        "\n"
        "  !! DISCONNECT ALL IGNITERS BEFORE CONTINUING !!\n"
        "\n"
        "  This firmware energises the arm relay, which puts VBAT on\n"
        "  the fire bus. Channel relays are held de-energised the\n"
        "  whole time, so no current can reach an igniter -- but the\n"
        "  interlock is what is under test, so do not rely on it.\n"
        "\n"
        "  Have a meter on the ARM SENSE node for step 3.\n"
        "\n"
        "  Press ENTER when igniters are disconnected and the base\n"
        "  battery is connected.\n");
    con_getc();
    con_puts("\n");

    /* ── Step 0 ────────────────────────────────────────────────────
     * KEY SENSE is the instrument every later step uses to know where
     * the key is. Validate it first, in both directions, or a stuck
     * input turns the whole run into a silent no-op that reports PASS.
     */
    con_puts("Step 0 — KEY SENSE tracks the key switch\n");
    wait_key(false);
    wait_key(true);
    wait_key(false);
    record("0. KEY SENSE tracks key", true, "SAFE -> ARM -> SAFE all observed");

    /* ── Step 1 — table row 1 ──────────────────────────────────────
     * Key SAFE, MOSFET driven. The software leg is asserted and the
     * hardware leg is not. This is the exact configuration bug #28's
     * sneak path lived in.
     */
    wait_key(false);
    step_sense("Step 1 — key SAFE + GPIO 47 driven  (software leg alone)", true, 0);
    gpio_set_level(PIN_ARM_RELAY, 0);

    /* ── Step 2 — table row 2 ──────────────────────────────────────
     * Key ON, MOSFET low. The hardware leg is asserted and the
     * software leg is not. A HIGH here is also the contact-weld
     * signature (FSD 7.3.2) — the relay's own contacts stuck closed.
     */
    wait_key(true);
    step_sense("Step 2 — key ARM + GPIO 47 low  (hardware leg alone)", false, 0);

    /* ── Step 3 — table row 3 ──────────────────────────────────────
     * Both legs. The only configuration that may energise the relay.
     */
    step_sense("Step 3 — key ARM + GPIO 47 driven  (both legs — relay MUST pull in)", true, 1);
    con_puts("  Relay should be audibly pulled in and the ARM RELAY (coil) LED lit.\n"
             "  Meter ARM SENSE at the node now if you have not already.\n"
             "  Is the coil LED LIT? [y/n] ");
    {
        char c = con_getc();
        bool lit = (c == 'y' || c == 'Y');
        con_puts("\n");
        record("3b. Coil LED lit with relay in", lit,
               lit ? "operator confirmed lit" : "operator says NOT lit — indicator wiring");
    }

    /* ── Step 4 — release ──────────────────────────────────────────
     * Not in the table, but the table cannot see a relay that pulls
     * in and never lets go. Drop the software leg with the key still
     * ON: ARM SENSE must return LOW.
     */
    step_sense("Step 4 — key still ARM, GPIO 47 back low  (relay MUST release)", false, 0);

    /* ── Step 5 — the other leg, from the armed side ───────────────
     * Step 1 approached the key-SAFE case from a cold start. This
     * reaches it from a relay that was just energised, with the
     * software leg re-asserted: the key switch alone must break the
     * coil circuit. This is the leg that has to hold with the ESP32
     * crashed or unpowered, so it is worth proving twice, from both
     * directions.
     */
    con_puts("Step 5 — GPIO 47 driven, then key turned back to SAFE\n");
    gpio_set_level(PIN_ARM_RELAY, 1);
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    con_puts("  Relay is energised again (key still ARM).\n");
    wait_key(false);
    {
        int highs, n = watch(PIN_ARM_SENSE, &highs);
        char detail[96];
        if (highs == 0) {
            snprintf(detail, sizeof(detail), "ARM SENSE steady LOW (0/%d) — key broke the coil", n);
            record("5. Key to SAFE drops the relay", true, detail);
        } else {
            snprintf(detail, sizeof(detail), "ARM SENSE %d/%d HIGH — key did NOT break the coil", highs, n);
            record("5. Key to SAFE drops the relay", false, detail);
        }
    }
    gpio_set_level(PIN_ARM_RELAY, 0);

    /* ── Summary ──────────────────────────────────────────────── */

    con_puts("\n==========================================================\n"
             "  SUMMARY\n"
             "==========================================================\n");
    for (int i = 0; i < s_steps; i++) {
        con_printf("  %-8s %-34s %s\n",
                   s_result[i].pass ? "PASS" : "FAIL",
                   s_result[i].name, s_result[i].detail);
    }
    con_puts("\n");

    if (s_all_pass) {
        con_puts("  ALL PASS — both legs of the AND gate verified at the node,\n"
                 "  and the coil LED agrees with the node.\n"
                 "\n"
                 "  Reflash the real base firmware before any further testing:\n"
                 "      ./build_base.sh flash\n");
    } else {
        con_puts("  ONE OR MORE STEPS FAILED — do NOT fire.\n"
                 "\n"
                 "  Step 1 or 5 failing means current reaches the coil with the\n"
                 "  key in SAFE: a sneak path around the key switch, which is\n"
                 "  the leg that must hold with the ESP32 crashed or unpowered.\n"
                 "  Step 2 failing means the relay contacts are closed with no\n"
                 "  coil drive at all — suspect a welded contact (FSD 7.3.2).\n"
                 "  Step 3 failing means the relay is not energising when it\n"
                 "  should: coil drive, MOSFET, or the sense divider.\n"
                 "  Step 4 failing means the relay pulls in and does not let go.\n"
                 "  Step 3b alone failing is indicator wiring only — the node\n"
                 "  behaved, the LED did not.\n");
    }

    con_puts("\n  Outputs are safe. Reset the board to run again.\n");

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
