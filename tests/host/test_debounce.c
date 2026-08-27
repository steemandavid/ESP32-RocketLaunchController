/* Host tests for the shift-register debounce engine (FSD §5.5.2/§5.5.3).
 *
 * This file exists because of review finding N1. The debouncer has one
 * subtle, deliberate behaviour that bit the arm switch hard:
 *
 *   The FIRST stable reading establishes the starting state WITHOUT firing
 *   the callback. That suppression is required by the fire button — a button
 *   held down at power-on must not be reported as a press (FSD §5.5.3
 *   fresh-press interlock, review 4.12). But it means any consumer that
 *   caches state only from the callback silently keeps its init default when
 *   the input is already active at boot. The remote's arm key did exactly
 *   that, and arming was impossible until the key was toggled.
 *
 * So the contract is pinned from both sides here: T-D02 asserts the
 * suppression the fire button depends on, T-D03 asserts that
 * rlc_debounce_get_state()/is_stable() DO reflect the initial determination,
 * which is what consumers must sync from. Break either and one of the two
 * inputs misbehaves.
 *
 * The real production source is compiled in — no mirror. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "rlc_debounce.h"
#include "rlc_debounce.c"   /* the real engine, not a mirror */

static int fails = 0, checks = 0;

/* Callback bookkeeping */
static int  cb_count;
static bool cb_last_state;

static void on_change(int gpio, bool state, void *user)
{
    (void)gpio; (void)user;
    cb_count++;
    cb_last_state = state;
}

static void reset_cb(void) { cb_count = 0; cb_last_state = false; }

static void check(const char *what, bool ok)
{
    checks++;
    if (!ok) { printf("  FAIL %s\n", what); fails++; }
}

/* Drive n polls at a constant level. */
static void feed(rlc_debounce_t *db, int level, int n)
{
    for (int i = 0; i < n; i++) rlc_debounce_update(db, level, on_change, NULL);
}

int main(void)
{
    printf("RLC debounce engine — host tests\n\n");

    /* ── T-D01: width determines the settle time ───────────────────── */
    printf("T-D01 settle width (8-bit = 80 ms, 16-bit = 160 ms at 10 ms poll)\n");
    {
        rlc_debounce_t db8, db16;
        rlc_debounce_init(&db8,  1, DEBOUNCE_8BIT);
        rlc_debounce_init(&db16, 2, DEBOUNCE_16BIT);
        reset_cb();

        /* 7 LOW polls: neither is stable-LOW yet (8-bit needs 8, 16-bit 16) */
        feed(&db8, 0, 7);
        feed(&db16, 0, 7);
        check("8-bit not yet active after 7 LOW polls",
              rlc_debounce_get_state(&db8) == false);
        check("16-bit not yet active after 7 LOW polls",
              rlc_debounce_get_state(&db16) == false);

        /* 8th LOW poll settles the 8-bit register only */
        feed(&db8, 0, 1);
        feed(&db16, 0, 1);
        check("8-bit active after 8 LOW polls",
              rlc_debounce_get_state(&db8) == true);
        check("16-bit still not active after 8 LOW polls",
              rlc_debounce_get_state(&db16) == false);

        /* 16 total settles the 16-bit register */
        feed(&db16, 0, 8);
        check("16-bit active after 16 LOW polls",
              rlc_debounce_get_state(&db16) == true);
    }

    /* ── T-D02: initial determination fires NO callback ────────────── */
    /* This is the fire-button fresh-press interlock (FSD §5.5.3, review
     * 4.12): a button already held at power-on must not look like a press. */
    printf("T-D02 first stable reading fires no callback (fresh-press interlock)\n");
    {
        rlc_debounce_t db;
        rlc_debounce_init(&db, 3, DEBOUNCE_8BIT);
        reset_cb();

        feed(&db, 0, 8);          /* input active from the very first poll */
        check("held-at-boot input produced no press callback", cb_count == 0);
        check("...but the engine did record it as active",
              rlc_debounce_get_state(&db) == true);

        /* Same on the other side: input inactive at boot, no spurious
         * "released" callback (the defect the suppression was added for). */
        rlc_debounce_t db2;
        rlc_debounce_init(&db2, 4, DEBOUNCE_8BIT);
        reset_cb();
        feed(&db2, 1, 8);
        check("released-at-boot input produced no callback", cb_count == 0);
        check("...and the engine recorded it as inactive",
              rlc_debounce_get_state(&db2) == false);
    }

    /* ── T-D03: the initial state IS visible to a polling consumer ─── */
    /* N1: this is the escape hatch the arm switch must use. If a consumer
     * caches only from the callback, an already-ON key is invisible. */
    printf("T-D03 initial state is readable via get_state/is_stable (N1)\n");
    {
        rlc_debounce_t db;
        rlc_debounce_init(&db, 5, DEBOUNCE_16BIT);
        reset_cb();

        check("not stable before any full window", !rlc_debounce_is_stable(&db));
        feed(&db, 0, 16);         /* arm key already ON at power-up */
        check("stable once the window fills", rlc_debounce_is_stable(&db));
        check("get_state reports ACTIVE even though no callback fired",
              rlc_debounce_get_state(&db) == true);
        check("no callback fired (T-D02 invariant still holds)", cb_count == 0);
    }

    /* ── T-D04: real transitions DO fire the callback, once each ───── */
    printf("T-D04 transitions after the initial determination fire once each\n");
    {
        rlc_debounce_t db;
        rlc_debounce_init(&db, 6, DEBOUNCE_8BIT);
        feed(&db, 1, 8);          /* establish: inactive */
        reset_cb();

        feed(&db, 0, 8);          /* press */
        check("press fired exactly one callback", cb_count == 1);
        check("press callback reported active", cb_last_state == true);

        feed(&db, 0, 20);         /* held — no repeats */
        check("held input fires no further callbacks", cb_count == 1);

        feed(&db, 1, 8);          /* release */
        check("release fired exactly one more callback", cb_count == 2);
        check("release callback reported inactive", cb_last_state == false);
    }

    /* ── T-D05: bounce inside the window is rejected ───────────────── */
    printf("T-D05 contact bounce shorter than the window is rejected\n");
    {
        rlc_debounce_t db;
        rlc_debounce_init(&db, 7, DEBOUNCE_8BIT);
        feed(&db, 1, 8);          /* establish: inactive */
        reset_cb();

        /* Seven LOW polls then a HIGH glitch, repeatedly — never 8 in a row */
        for (int burst = 0; burst < 5; burst++) {
            feed(&db, 0, 7);
            feed(&db, 1, 1);
        }
        check("bouncing input never latched active", cb_count == 0);
        check("state remained inactive throughout",
              rlc_debounce_get_state(&db) == false);

        /* A clean 8 in a row does latch */
        feed(&db, 0, 8);
        check("clean press after the bounce latched", cb_count == 1);
    }

    /* ── T-D06: NULL guards ────────────────────────────────────────── */
    printf("T-D07 dead-man asymmetry: fast release, slow press (fire button)\n");
    {
        /* The defect this pins, found on target 2026-08-27: with symmetric
         * debouncing, mashing the fire button fired the channel. A release
         * shorter than the full 8-sample window never filled the register, so
         * no release was ever reported and every layer above saw a continuous
         * hold. Two dead-man layers sat downstream of that one decision. */
        rlc_debounce_t db;
        rlc_debounce_init(&db, 7, DEBOUNCE_8BIT);
        rlc_debounce_set_fast_release(&db, 2);
        reset_cb();

        /* Establish a stable press. */
        feed(&db, 0, 8);
        check("press after 8 low samples", rlc_debounce_get_state(&db) == true);

        /* THE REGRESSION: 3 high samples is far short of the 8-sample window,
         * and used to report nothing at all. It must now report a release. */
        reset_cb();
        feed(&db, 1, 3);
        check("release reported within 3 samples (was invisible before)",
              rlc_debounce_get_state(&db) == false);
        check("release fired exactly one callback", cb_count == 1);
        check("callback reported inactive", cb_last_state == false);

        /* A single-sample glitch must NOT read as a release: that is why the
         * threshold is 2 rather than 1. Contact bounce is 1-10 ms. */
        feed(&db, 0, 8);                       /* back to a stable press */
        check("stable press again", rlc_debounce_get_state(&db) == true);
        reset_cb();
        feed(&db, 1, 1);                       /* one high sample only */
        check("single-sample glitch is not a release",
              rlc_debounce_get_state(&db) == true);
        check("glitch fired no callback", cb_count == 0);

        /* Press direction must remain SLOW — noise must not start a sequence. */
        feed(&db, 1, 8);                       /* settle released */
        check("settled released", rlc_debounce_get_state(&db) == false);
        reset_cb();
        feed(&db, 0, 7);                       /* 7 low samples: not enough */
        check("7 low samples do not register a press",
              rlc_debounce_get_state(&db) == false);
        feed(&db, 0, 1);                       /* the 8th completes it */
        check("8th low sample registers the press",
              rlc_debounce_get_state(&db) == true);
    }

    printf("T-D08 fast release is opt-in — default debouncing stays symmetric\n");
    {
        rlc_debounce_t db;
        rlc_debounce_init(&db, 8, DEBOUNCE_8BIT);   /* no set_fast_release */
        feed(&db, 0, 8);
        check("press established", rlc_debounce_get_state(&db) == true);
        feed(&db, 1, 3);
        check("3 high samples do NOT release a symmetric input",
              rlc_debounce_get_state(&db) == true);
        feed(&db, 1, 5);
        check("full 8 samples do release it",
              rlc_debounce_get_state(&db) == false);
    }

    printf("T-D06 NULL-safe API\n");
    {
        rlc_debounce_init(NULL, 0, DEBOUNCE_8BIT);   /* must not crash */
        check("update(NULL) returns false",
              rlc_debounce_update(NULL, 0, on_change, NULL) == false);
        check("get_state(NULL) returns false", rlc_debounce_get_state(NULL) == false);
        check("is_stable(NULL) returns false", rlc_debounce_is_stable(NULL) == false);
    }

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
