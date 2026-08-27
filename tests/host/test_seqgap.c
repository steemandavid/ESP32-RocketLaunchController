/* Host tests for the protocol sequence rules (FSD §15.5 T-U09, T-U16, and the
 * anti-replay half of T-U04).
 *
 * Both functions under test are the production ones, compiled from
 * rlc_message.c — not a copy. They exist as pure functions precisely so the
 * rules can be pinned here instead of only inside a link-layer that needs a
 * radio to exercise.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

int64_t g_mock_us = 0;   /* unused here, but the esp_timer stub declares it */

#include "rlc_message.c"

static int checks = 0, fails = 0;

static void eq_u16(const char *what, unsigned got, unsigned want)
{
    checks++;
    if (got != want) {
        printf("  FAIL %-54s got %u, want %u\n", what, got, want);
        fails++;
    }
}

static void eq_bool(const char *what, bool got, bool want)
{
    checks++;
    if (got != want) {
        printf("  FAIL %-54s got %s, want %s\n", what,
               got ? "true" : "false", want ? "true" : "false");
        fails++;
    }
}

/* T-U09: STATUS_UPDATE data-gap detection (FSD §6.4.3). */
static void t_update_seq_gap(void)
{
    printf("T-U09 update_sequence gap detection\n");

    eq_u16("consecutive frames report no loss", rlc_update_seq_lost(10, 11), 0);
    eq_u16("one frame missed",                  rlc_update_seq_lost(10, 12), 1);
    eq_u16("two frames missed",                 rlc_update_seq_lost(10, 13), 2);
    eq_u16("three frames missed (toast threshold)",
           rlc_update_seq_lost(10, 14), 3);
    eq_u16("a long outage",                     rlc_update_seq_lost(100, 200), 99);

    /* A duplicate is not a gap. The link layer's replay guard should have
     * dropped it; if one gets through it must not be reported as loss. */
    eq_u16("duplicate is not a gap", rlc_update_seq_lost(42, 42), 0);
}

/* T-U16: the uint16 wrap is one step, not a 65535-frame gap. */
static void t_update_seq_wrap(void)
{
    printf("T-U16 update_sequence wrap-around\n");

    eq_u16("65535 -> 0 is consecutive",   rlc_update_seq_lost(65535, 0), 0);
    eq_u16("65534 -> 0 loses one",        rlc_update_seq_lost(65534, 0), 1);
    eq_u16("65535 -> 2 loses two",        rlc_update_seq_lost(65535, 2), 2);
    eq_u16("65530 -> 3 across the wrap",  rlc_update_seq_lost(65530, 3), 8);

    /* The naive signed subtraction this replaced would have called the wrap a
     * 65535-frame gap and toasted DATA GAP once every ~36 hours of uptime. */
    eq_bool("wrap never crosses the toast threshold",
            rlc_update_seq_lost(65535, 0) > 2, false);
}

/* T-U04 (anti-replay half): rlc_seq_validate is the rule the link layer's
 * seq_is_replay() mirrors — strictly greater than the last accepted. */
static void t_seq_validate(void)
{
    printf("T-U04 sequence-number anti-replay\n");

    uint32_t last = 0;
    eq_bool("first real frame (seq 1) accepted", rlc_seq_validate(1, &last), true);
    eq_u16("counter advanced", (unsigned)last, 1);

    eq_bool("forward jump accepted", rlc_seq_validate(5, &last), true);
    eq_bool("same seq again rejected", rlc_seq_validate(5, &last), false);
    eq_bool("older seq rejected", rlc_seq_validate(3, &last), false);
    eq_u16("counter not moved by a rejection", (unsigned)last, 5);

    /* CM-05: seq 0 is never legitimate — every sender pre-increments, so the
     * first frame of a session carries 1. Accepting 0 while the counter was
     * still 0 was an unlimited replay window at the start of every session. */
    last = 0;
    eq_bool("seq 0 rejected even on a fresh counter",
            rlc_seq_validate(0, &last), false);
    eq_u16("fresh counter untouched by a seq-0 frame", (unsigned)last, 0);

    /* Overflow: the counter must never wrap. The senders drop the link
     * instead; the validator simply refuses anything not greater. */
    last = UINT32_MAX;
    eq_bool("nothing is greater than UINT32_MAX",
            rlc_seq_validate(UINT32_MAX, &last), false);
    eq_bool("NULL last is refused, not dereferenced",
            rlc_seq_validate(1, NULL), false);
}

int main(void)
{
    printf("RLC protocol sequence rules — host tests\n\n");

    t_update_seq_gap();
    t_update_seq_wrap();
    t_seq_validate();

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
