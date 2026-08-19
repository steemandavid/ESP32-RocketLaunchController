#!/bin/bash
# Host-compiled unit tests (FSD §15.5). No hardware required.
#
#   ./tests/host/run.sh
#
# test_strip.c includes components/rlc_common/src/rlc_rgb_led.c directly and
# links it against mock led_strip / FreeRTOS / esp_timer headers in stubs/,
# so the real rendering functions are exercised and every emitted pixel is
# captured and asserted.

set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# The two units wire their strips data-in at opposite ends, so the renderer is
# compiled and asserted once per unit (RLC_STRIP_REVERSED differs).
fail=0
for t in test_*.c; do
    for unit in BASE REMOTE; do
        bin="$OUT/${t%.c}_$unit"
        gcc -std=c11 -Wall -Wextra -Wno-unused-variable \
            "-DCONFIG_RLC_UNIT_$unit" \
            -I stubs \
            -I "$ROOT/components/rlc_common/include" \
            -I "$ROOT/components/rlc_common/src" \
            "$t" -o "$bin"
        echo "--- $t  [$unit unit] ---"
        "$bin" || fail=1
    done
done
exit $fail
