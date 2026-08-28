/* Host test for the error-flag naming helpers in rlc_protocol.h.
 *
 * The remote displays these names instead of a raw bitmask, so a wrong or
 * missing name is a field-legibility bug: every bit 0-7 must resolve to
 * something, and multi-flag masks must enumerate cleanly. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "rlc_protocol.h"

static int fails = 0, checks = 0;

static void expect_str(const char *what, const char *got, const char *want)
{
    checks++;
    if (!got || strcmp(got, want) != 0) {
        printf("  FAIL %-42s got \"%s\", want \"%s\"\n",
               what, got ? got : "(null)", want);
        fails++;
    }
}

static void expect_int(const char *what, int got, int want)
{
    checks++;
    if (got != want) { printf("  FAIL %-42s got %d, want %d\n", what, got, want); fails++; }
}

int main(void)
{
    char buf[80];

    printf("RLC error-flag naming — host tests\n\n");

    /* ── T-E01: every defined flag has a name ── */
    printf("T-E01 individual flag names\n");
    expect_str("0x01 VBAT LOW",       rlc_error_flag_str(ERR_VBAT_LOW),       "VBAT LOW");
    expect_str("0x02 VBAT CRITICAL",  rlc_error_flag_str(ERR_VBAT_CRITICAL),  "VBAT CRITICAL");
    expect_str("0x04 RELAY FAULT",    rlc_error_flag_str(ERR_RELAY_FAULT),    "RELAY FAULT");
    expect_str("0x10 COMM DEGRADED",  rlc_error_flag_str(ERR_COMM_DEGRADED),  "COMM DEGRADED");
    expect_str("0x20 WATCHDOG RESET", rlc_error_flag_str(ERR_WATCHDOG_RESET), "WATCHDOG RESET");
    expect_str("0x40 INTERNAL FAULT", rlc_error_flag_str(ERR_INTERNAL),       "INTERNAL FAULT");

    /* ── T-E02: no bit is left unnamed, including the unassigned ones ── */
    printf("T-E02 every bit 0-7 resolves to a name\n");
    expect_str("0x08 reserved bit",  rlc_error_flag_str(1 << 3), "RESERVED BIT3");
    expect_str("0x80 undefined bit", rlc_error_flag_str(1 << 7), "UNDEFINED BIT7");
    for (int b = 0; b < 8; b++) {
        const char *n = rlc_error_flag_str((uint8_t)(1u << b));
        checks++;
        if (!n || !*n || strcmp(n, "UNKNOWN") == 0) {
            printf("  FAIL bit %d has no usable name (\"%s\")\n", b, n ? n : "(null)");
            fails++;
        }
    }

    /* ── T-E03: counting ── */
    printf("T-E03 flag counting\n");
    expect_int("no flags",     rlc_error_flags_count(0x00), 0);
    expect_int("single flag",  rlc_error_flags_count(ERR_VBAT_CRITICAL), 1);
    expect_int("two flags",    rlc_error_flags_count(ERR_VBAT_CRITICAL | ERR_RELAY_FAULT), 2);
    expect_int("all eight",    rlc_error_flags_count(0xFF), 8);

    /* ── T-E04: nth lookup, used by the display to cycle flags ── */
    printf("T-E04 nth-flag lookup\n");
    uint8_t multi = ERR_VBAT_CRITICAL | ERR_RELAY_FAULT | ERR_WATCHDOG_RESET;
    expect_str("nth 0 = lowest bit set", rlc_error_flag_nth(multi, 0), "VBAT CRITICAL");
    expect_str("nth 1",                  rlc_error_flag_nth(multi, 1), "RELAY FAULT");
    expect_str("nth 2",                  rlc_error_flag_nth(multi, 2), "WATCHDOG RESET");
    checks++;
    if (rlc_error_flag_nth(multi, 3) != NULL) {
        printf("  FAIL nth past the end should be NULL\n"); fails++;
    }

    /* ── T-E05: the joined list used in logs ── */
    printf("T-E05 comma-separated list\n");
    expect_str("single", rlc_error_flags_str(ERR_VBAT_CRITICAL, buf, sizeof(buf)),
               "VBAT CRITICAL");
    expect_str("multiple", rlc_error_flags_str(multi, buf, sizeof(buf)),
               "VBAT CRITICAL, RELAY FAULT, WATCHDOG RESET");
    expect_str("empty mask", rlc_error_flags_str(0x00, buf, sizeof(buf)), "NONE");

    /* ── T-E06: truncation must not overrun or lose termination ── */
    printf("T-E06 buffer safety\n");
    char small[10];
    const char *r = rlc_error_flags_str(0xFF, small, sizeof(small));
    checks++;
    if (strlen(r) >= sizeof(small)) {
        printf("  FAIL truncated result overran its buffer\n"); fails++;
    }
    char tiny[1];
    rlc_error_flags_str(0xFF, tiny, sizeof(tiny));
    expect_str("1-byte buffer yields empty string", tiny, "");

    /* ── T-E08: MIN-07 one-line brief for the 40-char overlay ── */
    printf("T-E08 one-line brief (worst flag + count)\n");
    expect_str("single flag needs no count",
               rlc_error_flags_brief(ERR_VBAT_CRITICAL, buf, sizeof(buf)),
               "VBAT CRITICAL");
    expect_str("worst of three, rest counted",
               rlc_error_flags_brief(multi, buf, sizeof(buf)),
               "VBAT CRITICAL +2");
    expect_str("relay fault outranks comm degraded",
               rlc_error_flags_brief(ERR_COMM_DEGRADED | ERR_RELAY_FAULT,
                                     buf, sizeof(buf)),
               "RELAY FAULT +1");
    expect_str("low battery is the least of them",
               rlc_error_flags_brief(ERR_VBAT_LOW, buf, sizeof(buf)),
               "VBAT LOW");
    expect_str("empty mask", rlc_error_flags_brief(0x00, buf, sizeof(buf)),
               "NONE");
    /* The reason this helper exists: "BASE ERROR: " + this must fit 40. */
    {
        char toast[40];
        char brief[26];   /* the buffer the remote actually passes */
        rlc_error_flags_brief(0xFF, brief, sizeof(brief));
        snprintf(toast, sizeof(toast), "BASE ERROR: %s", brief);
        checks++;
        if (strlen(toast) >= sizeof(toast) - 1) {
            printf("  FAIL worst-case brief fills the overlay: \"%s\"\n", toast);
            fails++;
        }
    }
    {
        char tiny[4];
        rlc_error_flags_brief(0xFF, tiny, sizeof(tiny));
        checks++;
        if (strlen(tiny) >= sizeof(tiny)) {
            printf("  FAIL brief overran a 4-byte buffer\n"); fails++;
        }
    }

    /* ── T-E07: the real case that prompted this — 0x02 on the base ── */
    printf("T-E07 the reported case: base error 0x02\n");
    expect_str("0x02 names the critical battery",
               rlc_error_flag_str(0x02), "VBAT CRITICAL");
    expect_int("0x02 is a single flag", rlc_error_flags_count(0x02), 1);

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
