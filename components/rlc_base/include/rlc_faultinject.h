/**
 * RLC Fault Injection (Base Unit, TEST BUILDS ONLY)
 *
 * Two FSD §15.2 arming tests cannot be induced from outside the firmware:
 *
 *   T-A11  ARM with a stale STATUS_UPDATE (> STATUS_STALE_TIMEOUT_MS).
 *          Not producible by interfering with the radio: link loss trips at
 *          3 missed heartbeats (1.5 s), long before the 5 s staleness timeout,
 *          so the remote enters LINK_LOST instead of sitting on stale data.
 *          The only way to get "linked but stale" is for the base to keep
 *          answering PINGs while withholding STATUS_UPDATE.
 *
 *   T-A13  Wrong channel in CMD_ACK. Requires the base to emit a malformed
 *          ACK, which nothing in normal operation does.
 *
 * Both injections are therefore base-side, and both are here.
 *
 * ════════════════════════════════════════════════════════════════════════
 *  THIS FILE IS INERT UNLESS CONFIG_RLC_FAULT_INJECTION IS SET.
 *  When it is set the firmware is NOT SAFE FOR LIVE USE: it deliberately
 *  lies to the remote. The build prints a #warning, and the running firmware
 *  prints a banner at boot and refuses to be quiet about it.
 * ════════════════════════════════════════════════════════════════════════
 *
 * Driven by single characters on UART0 (the CH340 console), so no extra
 * hardware and no protocol change:
 *
 *   s   toggle STATUS_UPDATE suppression      (T-A11)
 *   a   arm a one-shot wrong-channel ARM ACK  (T-A13)
 *   ?   print current injection state
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"

#if CONFIG_RLC_FAULT_INJECTION

/** Start the injection console task and print the boot banner. */
void fault_inject_init(void);

/**
 * True while STATUS_UPDATE transmission is suppressed (T-A11).
 * Heartbeats are untouched, so the link stays up and the remote's cached
 * status ages out — which is the condition T-A11 needs and nothing else
 * produces.
 */
bool fault_inject_suppress_status(void);

/**
 * One-shot: if a wrong-channel ACK is armed, rewrite *ch and disarm the
 * injection. Returns true if the channel was altered.
 *
 * One-shot deliberately — a sticky version would corrupt the DISARM ACK that
 * the remote sends in response, and the resulting two-fault interaction is not
 * what T-A13 is testing.
 */
bool fault_inject_take_wrong_channel(uint8_t *ch);

#else  /* injection compiled out — these fold to nothing */

static inline void fault_inject_init(void) { }
static inline bool fault_inject_suppress_status(void) { return false; }
static inline bool fault_inject_take_wrong_channel(uint8_t *ch) { (void)ch; return false; }

#endif /* CONFIG_RLC_FAULT_INJECTION */
