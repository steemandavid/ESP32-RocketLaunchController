/**
 * RLC Shift-Register Debounce Engine
 */

#include "rlc_debounce.h"

void rlc_debounce_init(rlc_debounce_t *db, int gpio_num, rlc_debounce_width_t width)
{
    if (!db) return;
    db->shift_reg    = 0xFFFF;  /* Start as released/inactive (all HIGH) */
    db->width        = width;
    db->gpio_num     = gpio_num;
    db->stable_state = false;   /* Not active */
    db->initialised  = false;
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
    bool changed = false;

    if (masked == all_low) {
        /* Stably LOW = active (pressed / continuity OK / armed) */
        if (!db->stable_state || !db->initialised) {
            db->stable_state = true;
            db->initialised  = true;
            changed = true;
        }
    } else if (masked == all_high) {
        /* Stably HIGH = inactive (released / no continuity / disarmed) */
        if (db->stable_state || !db->initialised) {
            db->stable_state = false;
            db->initialised  = true;
            changed = true;
        }
    }
    /* Else: in transition — retain previous state */

    if (changed && cb) {
        cb(db->gpio_num, db->stable_state, user_data);
    }

    return changed;
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
