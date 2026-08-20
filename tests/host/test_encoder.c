/* Host test for the cycle-position quadrature decoder.
 *
 * Two behaviours are pinned: one detent's worth of rotation must not move the
 * channel by more than ENC_DIVIDER allows, and electrical noise must not move
 * it at all. The previous decoder failed both — every accepted edge became a
 * channel change, and a glitch on CLK produced a step in a direction decided
 * by whatever DT happened to read. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

int g_gpio_level[64];
int64_t g_mock_us = 0;   /* backs the esp_timer stub */

/* The encoder is remote-only hardware, and its pins live behind
 * CONFIG_RLC_UNIT_REMOTE in pin_config.h. The runner builds every test once
 * per unit, so under the base build this file compiles to a skip. */
#ifdef CONFIG_RLC_UNIT_REMOTE
#include "rlc_encoder.c"

static int fails = 0, checks = 0;

static void expect_int(const char *what, int got, int want)
{
    checks++;
    if (got != want) { printf("  FAIL %-46s got %d, want %d\n", what, got, want); fails++; }
}

/* Position around the CW cycle that spin() walks from. Must be reset with the
 * decoder, or the first transition of a case lands two positions from the
 * seeded state and is (correctly) rejected as illegal. */
static int s_idx;

static void reset_decoder(void)
{
    s_last_pos = 0; s_accum = 0; s_pos_valid = false;
    s_valid_count = 0;
    s_idx = 0;
}

/* Quadrature states in CW order: 00, 01, 11, 10 */
static const uint8_t CW[4] = { 0, 1, 3, 2 };

/* Feed `transitions` steps around the cycle; returns net channel steps. */
static int spin(int transitions, int dir)
{
    int net = 0;
    for (int i = 0; i < transitions; i++) {
        s_idx = ((s_idx + (dir > 0 ? 1 : 3)) & 3);
        net += encoder_feed(CW[s_idx]);
    }
    return net;
}

int main(void)
{
    printf("RLC encoder cycle-position decoder — host tests\n\n");

    printf("T-Q01 divider: raw transitions per emitted channel step\n");
    expect_int("ENC_DIVIDER is 4", ENC_DIVIDER, 4);
    reset_decoder(); encoder_feed(CW[0]);          /* seed */
    expect_int("3 CW transitions emit nothing", spin(3, +1), 0);
    reset_decoder(); encoder_feed(CW[0]);
    expect_int("4 CW transitions emit one step",  spin(4, +1), 1);
    reset_decoder(); encoder_feed(CW[0]);
    expect_int("8 CW transitions emit two steps", spin(8, +1), 2);
    reset_decoder(); encoder_feed(CW[0]);
    expect_int("4 CCW transitions emit one back", spin(4, -1), -1);

    printf("T-Q02 illegal transitions are discarded\n");
    reset_decoder();
    encoder_feed(0);                                /* seed at 00 */
    /* 00 -> 11 is a jump of two positions: a missed step or a glitch. */
    expect_int("jump of two emits nothing", encoder_feed(3), 0);
    expect_int("and is not counted valid", (int)s_valid_count, 0);
    /* Repeating the same state is no movement at all. */
    expect_int("same state emits nothing", encoder_feed(3), 0);

    printf("T-Q03 contact bounce nets to zero\n");
    reset_decoder();
    encoder_feed(0);
    int net = 0;
    for (int i = 0; i < 20; i++) {                  /* one line chattering */
        net += encoder_feed(1);
        net += encoder_feed(0);
    }
    expect_int("20 bounce cycles emit nothing", net, 0);

    printf("T-Q04 direction reversal restarts the count\n");
    reset_decoder();
    encoder_feed(CW[0]);
    spin(3, +1);                                    /* 3 CW, no step yet */
    expect_int("3 CW then 3 CCW emits nothing", spin(3, -1), 0);
    /* Having reversed, a further full divider CCW does step. */
    expect_int("then 4 more CCW emits one step", spin(4, -1), -1);

    printf("T-Q05 first sample only seeds, never steps\n");
    reset_decoder();
    expect_int("very first sample emits nothing", encoder_feed(2), 0);

    printf("T-Q06 a full detent cannot overshoot\n");
    /* A KY-040 detent is one full quadrature cycle = 4 transitions. With
     * ENC_DIVIDER 4 that is exactly one channel, never two. */
    reset_decoder(); encoder_feed(CW[0]);
    expect_int("one detent = exactly one channel", spin(4, +1), 1);

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}

#else   /* base build — encoder hardware does not exist on this unit */

int main(void)
{
    printf("RLC encoder decoder — skipped (remote-only hardware)\n");
    printf("\n0 checks, 0 failures\n");
    return 0;
}

#endif
