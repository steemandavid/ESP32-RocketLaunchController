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
#define RLC_VERSION_MAJOR  1
#define RLC_VERSION_MINOR  1
#define RLC_VERSION_PATCH  1
#define RLC_VERSION_STRING "1.1.1"
