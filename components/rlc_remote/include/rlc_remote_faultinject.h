/**
 * RLC Fault Injection (Remote Unit, TEST BUILDS ONLY)
 *
 * The base has had an injection console since 2026-08-26 (rlc_faultinject.h).
 * This is its remote-side counterpart, added because several display and FSM
 * states are latched only by conditions that cannot be produced from the base
 * or from the air:
 *
 *   REMOTE FAULT on the status band, and the ERROR screen behind it, are
 *   latched by exactly four things — REMOTE BATTERY CRITICAL, DISPLAY FAULT,
 *   MULTI-ARM DETECTED, and a boot failure. None is reachable from the base
 *   harness. A wrong-channel ARM ACK (base key 'a') does NOT latch ERROR: the
 *   remote toasts "CHANNEL MISMATCH ERROR" and reconciles by disarming, which
 *   is the better behaviour but leaves the fault path untested.
 *
 * Driven by single characters on the remote's UART0 console, mirroring the
 * base harness so the two feel the same:
 *
 *   d   post EVT_DISPLAY_FAULT   -> ERROR "DISPLAY FAULT"
 *   b   post EVT_BATTERY_CRITICAL -> ERROR "REMOTE BATTERY CRITICAL"
 *   ?   print current state
 *
 * ════════════════════════════════════════════════════════════════════════
 *  THIS FILE IS INERT UNLESS CONFIG_RLC_REMOTE_FAULT_INJECTION IS SET.
 *  When it is set the firmware can drive itself into a terminal ERROR on a
 *  keystroke and is NOT SAFE FOR LIVE USE. The build prints a #warning and
 *  the running firmware prints a banner at boot.
 * ════════════════════════════════════════════════════════════════════════
 */

#pragma once

#include "sdkconfig.h"

#if CONFIG_RLC_REMOTE_FAULT_INJECTION

/** Start the injection console task and print the boot banner. */
void remote_fault_inject_init(void);

#else  /* injection compiled out — folds to nothing */

static inline void remote_fault_inject_init(void) { }

#endif /* CONFIG_RLC_REMOTE_FAULT_INJECTION */
