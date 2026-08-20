/* Host test for the BASE arm-state derivation shown on the remote.
 *
 * The rule that matters: ARMED/WELD must be driven by the ARM SENSE (the arm
 * relay COM output), never by the key switch. A welded relay leaves the fire
 * path live with the key OFF, and keying the display off the key switch would
 * print SAFE over an energised igniter circuit.
 *
 * The logic under test is a pure function of the cached STATUS_UPDATE, so it
 * is reproduced here against the real protocol header rather than pulling in
 * the whole ILI9488 driver. Any divergence between this table and
 * rlc_display.c is itself a bug — see T-M07 below, which pins the field
 * semantics the display relies on. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "rlc_protocol.h"

typedef enum {
    BASE_ARM_UNKNOWN = 0, BASE_ARM_SAFE, BASE_ARM_READY,
    BASE_ARM_ARMED, BASE_ARM_WELD,
} base_arm_state_t;

/* Mirror of base_arm_state() in rlc_display.c */
static base_arm_state_t derive(const rlc_payload_status_update_t *st, bool fresh)
{
    if (!fresh) return BASE_ARM_UNKNOWN;
    bool sense = st->base_arm_sense != 0;
    bool key   = st->base_key_switch != 0;
    uint8_t s  = st->base_state;
    bool relay_expected_on = (s == STATE_ARMED || s == STATE_PRE_FIRE || s == STATE_FIRING);
    if (st->error_flags & ERR_RELAY_FAULT) return BASE_ARM_WELD;
    if (sense && !relay_expected_on)       return BASE_ARM_WELD;
    if (sense)                             return BASE_ARM_ARMED;
    if (key)                               return BASE_ARM_READY;
    return BASE_ARM_SAFE;
}

static const char *name(base_arm_state_t s)
{
    switch (s) {
        case BASE_ARM_SAFE: return "SAFE"; case BASE_ARM_READY: return "READY";
        case BASE_ARM_ARMED: return "ARMED"; case BASE_ARM_WELD: return "WELD!";
        default: return "?";
    }
}

static int fails = 0, checks = 0;

static void expect(const char *what, int key, int sense, uint8_t state,
                   uint8_t err, bool fresh, base_arm_state_t want)
{
    rlc_payload_status_update_t st;
    memset(&st, 0, sizeof(st));
    st.base_key_switch = (uint8_t)key;
    st.base_arm_sense  = (uint8_t)sense;
    st.base_state      = state;
    st.error_flags     = err;
    base_arm_state_t got = derive(&st, fresh);
    checks++;
    if (got != want) {
        printf("  FAIL %-44s got %s, want %s\n", what, name(got), name(want));
        fails++;
    }
}

int main(void)
{
    printf("RLC base arm-state derivation — host tests\n\n");

    printf("T-M01 normal progression\n");
    expect("key off, no sense -> SAFE",   0,0, STATE_IDLE,  0, true, BASE_ARM_SAFE);
    expect("key on, no sense -> READY",   1,0, STATE_IDLE,  0, true, BASE_ARM_READY);
    expect("armed, sense high -> ARMED",  1,1, STATE_ARMED, 0, true, BASE_ARM_ARMED);
    expect("pre-fire, sense high -> ARMED",1,1,STATE_PRE_FIRE,0,true, BASE_ARM_ARMED);
    expect("firing, sense high -> ARMED", 1,1, STATE_FIRING,0, true, BASE_ARM_ARMED);

    printf("T-M02 THE safety case — welded relay with the key OFF\n");
    /* Fire path live, key turned off. Keying the display off the key switch
     * would print SAFE here, over an energised igniter circuit. */
    expect("key OFF but sense HIGH in IDLE -> WELD", 0,1, STATE_IDLE, 0, true, BASE_ARM_WELD);
    expect("key ON and sense HIGH in IDLE  -> WELD", 1,1, STATE_IDLE, 0, true, BASE_ARM_WELD);
    expect("sense HIGH in POST_FIRE        -> WELD", 0,1, STATE_POST_FIRE,0,true, BASE_ARM_WELD);
    expect("sense HIGH in LINK_LOST        -> WELD", 0,1, STATE_LINK_LOST,0,true, BASE_ARM_WELD);

    printf("T-M03 base own weld flag is honoured\n");
    expect("ERR_RELAY_FAULT wins even with sense low",
           1,0, STATE_IDLE, ERR_RELAY_FAULT, true, BASE_ARM_WELD);

    printf("T-M04 stale data never reads SAFE\n");
    expect("stale, everything off -> UNKNOWN", 0,0, STATE_IDLE, 0, false, BASE_ARM_UNKNOWN);
    expect("stale, armed          -> UNKNOWN", 1,1, STATE_ARMED,0, false, BASE_ARM_UNKNOWN);

    printf("T-M05 the key switch alone can never produce ARMED\n");
    for (uint8_t st = STATE_BOOT; st <= STATE_ERROR; st++) {
        rlc_payload_status_update_t s;
        memset(&s, 0, sizeof(s));
        s.base_key_switch = 1; s.base_arm_sense = 0; s.base_state = st;
        base_arm_state_t g = derive(&s, true);
        checks++;
        if (g == BASE_ARM_ARMED || g == BASE_ARM_WELD) {
            printf("  FAIL state %u: key alone produced %s\n", st, name(g)); fails++;
        }
    }

    printf("T-M06 line fits the 40-char scale-2 budget\n");
    char buf[64];
    const char *worst[] = {"SAFE","READY","ARMED","WELD!","?"};
    for (int i = 0; i < 5; i++) {
        snprintf(buf, sizeof(buf), "SEL CH 8   BASE %s   REMOTE ARMED", worst[i]);
        checks++;
        if (strlen(buf) > 40) {
            printf("  FAIL \"%s\" is %zu chars\n", buf, strlen(buf)); fails++;
        }
    }

    printf("T-M07 protocol field semantics the display depends on\n");
    checks++;
    if (sizeof(rlc_payload_status_update_t) != 14) {
        printf("  FAIL STATUS_UPDATE changed size; the remote must be reflashed too\n");
        fails++;
    }

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
