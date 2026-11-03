/**
 * RLC Camera Rig (CRIG) interface — base unit only. FSD §5.4.12, §7.5.
 *
 * Two isolated output lines to the CRIG remote (a camera rig that photographs
 * liftoffs):
 *
 *   TRIG (GPIO 39): steady HIGH from CAM_TRIG_LEAD_MS before ignition until
 *                   CAM_TRIG_POST_MS after the fire pulse — the camera shoots.
 *   ARM  (GPIO 41): heartbeat. 1 Hz = base connected, not armed;
 *                   10 Hz = base armed, or TRIG still HIGH.
 *
 * OUTPUTS ONLY. Nothing here reads an input, touches the radio or changes FSM
 * state, and no failure in this module is a base ERROR: the worst it can do is
 * lose the photos. All GPIO writes happen inside a short critical section,
 * from task context (the FSM hooks below) or from esp_timer callbacks; no call
 * blocks.
 *
 * Once FIRING has been entered the igniter may already have lit, so from then
 * on cam_trig_cancel() never drops TRIG early — it ends through the post timer
 * (or the hard cap). Before FIRING, cancel drops TRIG at once. This lets every
 * FSM exit path call cam_trig_cancel() without caring where it came from.
 */

#pragma once

#include <stdbool.h>

/** Configure both pins LOW and start the ARM heartbeat (idle pattern).
 *  Call early in boot, right after relay/siren init (FSD §9.13 step 1). */
void cam_trig_init(void);

/** ARMED -> PRE_FIRE: start the lead timer (TRIG HIGH at
 *  PRE_FIRE_DELAY_MS - CAM_TRIG_LEAD_MS, if still in PRE_FIRE then). */
void cam_trig_on_prefire_enter(void);

/** PRE_FIRE -> FIRING: from now on TRIG only ends through the post timer. */
void cam_trig_on_firing_enter(void);

/** Fire pulse ended (EVT_FIRE_PULSE_DONE, any path): start the post timer. */
void cam_trig_on_fire_pulse_end(void);

/** Any other exit: before FIRING, drop TRIG now; after FIRING, behave like
 *  cam_trig_on_fire_pulse_end() (post timer started once, never early). */
void cam_trig_cancel(void);

/** Select the ARM pattern. The 10 Hz pattern is shown while
 *  (armed || TRIG asserted), so ARM stays "armed" until TRIG drops. */
void cam_arm_set(bool armed);

/** Diagnostics: current intended line levels (true = asserted). */
bool cam_trig_is_asserted(void);
bool cam_arm_is_armed_pattern(void);
