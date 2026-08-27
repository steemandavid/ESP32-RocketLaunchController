/**
 * RLC Shift-Register Debounce Engine
 */

#include "rlc_debounce.h"

/* Are the most recent `n` samples all HIGH (inactive)?
 *
 * New samples shift in at the LSB (shift_reg = (shift_reg << 1) | raw), so the
 * most recent n samples are the low n bits, and "all inactive" means all ones. */
static inline bool release_run_reached(uint16_t shift_reg, uint8_t n)
{
    uint16_t m = (uint16_t)((1u << n) - 1u);
    return (shift_reg & m) == m;
}

void rlc_debounce_set_fast_release(rlc_debounce_t *db, uint8_t samples)
{
    if (!db) return;
    if (samples > (uint8_t)db->width) samples = (uint8_t)db->width;
    db->release_samples = samples;
}

void rlc_debounce_init(rlc_debounce_t *db, int gpio_num, rlc_debounce_width_t width)
{
    if (!db) return;
    db->shift_reg    = 0xFFFF;  /* Start as released/inactive (all HIGH) */
    db->width        = width;
    db->gpio_num     = gpio_num;
    db->stable_state = false;   /* Not active */
    db->initialised  = false;
    db->release_samples = 0;   /* symmetric by default */
}

bool rlc_debounce_update(rlc_debounce_t *db, int raw_level,
                         rlc_debounce_cb_t cb, void *user_data)
{
    if (!db) return false;

    /* Shift in the new reading at LSB */
    db->shift_reg = (db->shift_reg << 1) | (raw_level & 0x01);

    /* Mask to register width */
    uint16_t mask;
    uint16_t all_low;
    uint16_t all_high;

    if (db->width == DEBOUNCE_8BIT) {
        mask     = 0x00FF;
        all_low  = 0x0000;
        all_high = 0x00FF;
    } else {
        mask     = 0xFFFF;
        all_low  = 0x0000;
        all_high = 0xFFFF;
    }

    uint16_t masked = db->shift_reg & mask;
    bool changed  = false;
    bool initial  = false;

    if (masked == all_low) {
        /* Stably LOW = active (pressed / continuity OK / armed) */
        if (!db->initialised) {
            db->stable_state = true;
            db->initialised  = true;
            initial = true;
        } else if (!db->stable_state) {
            db->stable_state = true;
            changed = true;
        }
    } else if (db->release_samples > 0 && db->initialised && db->stable_state &&
               release_run_reached(db->shift_reg, db->release_samples)) {
        /* Fast-release path (dead-man inputs only). The full-width all_high
         * test below still applies to every other caller; this one fires as
         * soon as the most recent N samples agree that the input is inactive,
         * without waiting for the whole register to fill. */
        db->stable_state = false;
        changed = true;
    } else if (masked == all_high) {
        /* Stably HIGH = inactive (released / no continuity / disarmed) */
        if (!db->initialised) {
            db->stable_state = false;
            db->initialised  = true;
            initial = true;
        } else if (db->stable_state) {
            db->stable_state = false;
            changed = true;
        }
    }
    /* Else: in transition — retain previous state */

    /* The first stable reading establishes the starting state — it is not a
     * transition. Without this guard every input produced a spurious
     * "released" callback on the very first poll, because the register is
     * seeded all-HIGH in rlc_debounce_init. */
    if (changed && cb) {
        cb(db->gpio_num, db->stable_state, user_data);
    }

    return changed || initial;
}

bool rlc_debounce_get_state(const rlc_debounce_t *db)
{
    if (!db) return false;
    return db->stable_state;
}

bool rlc_debounce_is_stable(const rlc_debounce_t *db)
{
    if (!db) return false;
    return db->initialised;
}
