#!/bin/bash
# Build and optionally flash the REMOTE unit firmware.
#
# Usage:
#   ./build_remote.sh          # build only
#   ./build_remote.sh flash    # build + flash (default PORT below; override with -p <by-id>)
#   ./build_remote.sh -p PORT  # build and flash to custom port

set -euo pipefail
cd "$(dirname "$0")"

SCRIPT_DIR="$(pwd)"
SDKCONFIG_REMOTE="$SCRIPT_DIR/sdkconfig.remote"
BUILD_DIR="$SCRIPT_DIR/build"

# Default flash port (remote unit)
PORT="/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E043219-if00"   # remote COM port — stable board serial (survives chip swaps). Find yours: ls /dev/serial/by-id/

# Parse args
DO_FLASH=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        flash)    DO_FLASH=true ;;
        -p)       shift; PORT="$1" ;;
        *)        echo "Usage: $0 [flash] [-p PORT]"; exit 1 ;;
    esac
    shift
done

# Source ESP-IDF
source ~/esp/esp-idf/export.sh 2>/dev/null

# TT-12: run the host unit tests before every firmware build. They cost a few
# seconds, need no hardware, and cover the safety FSM (tests/host/test_base_fsm.c),
# the debouncer, the continuity classifier, the battery maths and the LED
# renderer. Until now run.sh existed but nothing ever invoked it, so a
# regression could reach a board without anyone running them.
# Set RLC_SKIP_HOST_TESTS=1 to bypass (e.g. on a machine without gcc).
if [ "${RLC_SKIP_HOST_TESTS:-0}" != "1" ]; then
    echo "=== Host unit tests ==="
    if ! "$SCRIPT_DIR/tests/host/run.sh" > /tmp/rlc_host_tests.log 2>&1; then
        echo "HOST TESTS FAILED — refusing to build firmware."
        cat /tmp/rlc_host_tests.log
        exit 1
    fi
    grep -hE "checks, [0-9]+ failures" /tmp/rlc_host_tests.log \
        | awk -F'[ ,]+' '{c+=$1; f+=$3} END {printf "  %d checks, %d failures\n", c, f}'
fi

echo "=== Building REMOTE unit ==="

# Install remote sdkconfig as the active one
cp "$SDKCONFIG_REMOTE" "$SCRIPT_DIR/sdkconfig"

# The T-D09 display profiling harness (CONFIG_RLC_DISPLAY_PROFILE / --profile)
# was removed in 1.1.11 once the measurements were taken. Recover it from git
# history (firmware 1.1.10) if the display refresh ever needs re-measuring.

# Configure and build (default build dir)
idf.py build 2>&1 | grep -E "warning:|error:|Generated|build complete" || true

# Verify the binary contains remote_app_main
NM=$(find ~/.espressif/tools/xtensa-esp-elf -name "xtensa-esp32s3-elf-nm" 2>/dev/null | head -1)
if [ -n "$NM" ]; then
    if $NM "$BUILD_DIR/rlc.elf" 2>/dev/null | grep -q "T remote_app_main"; then
        echo "Verified: remote_app_main in binary"
    else
        echo "ERROR: remote_app_main NOT found in binary!"
        exit 1
    fi
fi

if $DO_FLASH; then
    echo "=== Flashing REMOTE to $PORT ==="
    python3 -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
        --before default_reset --after hard_reset \
        write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
        0x10000 "$BUILD_DIR/rlc.bin" 2>&1 | tail -3
    echo "Done."
else
    echo "Binary at: $BUILD_DIR/rlc.bin"
    echo "Flash with: $0 flash"
fi
