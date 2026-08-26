/**
 * RLC Firmware Version
 *
 * Both units must match on all three components (MAJOR.MINOR.PATCH)
 * to establish a link.
 */

#pragma once

/* 1.1.1 (2026-08-21): post-review fix round — arm-key state adopted at boot
 * (N1), siren stale-callback race (N2), and 11 minors. Arm-path behaviour
 * changed on BOTH units, so the bump is deliberate: the strict version check
 * makes a half-flashed pair refuse to link rather than run mismatched safety
 * logic. Flash base and remote together. */
/* 1.1.2 (2026-08-26): base siren sounds continuously from ARMED through
 * PRE_FIRE and FIRING (the 500 ms ARMED pulse fought the siren's own internal
 * modulation), and continuity loss on the armed channel now disarms the base
 * from ARMED or PRE_FIRE instead of being informational. Base-only changes,
 * no wire-protocol change — but the strict version check is on all three
 * components, so flash base and remote together or they will refuse to
 * link. */
#define RLC_VERSION_MAJOR  1
#define RLC_VERSION_MINOR  1
#define RLC_VERSION_PATCH  2
#define RLC_VERSION_STRING "1.1.2"
