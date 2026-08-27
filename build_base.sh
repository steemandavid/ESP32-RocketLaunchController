#!/bin/bash
# Build and optionally flash the BASE unit firmware.
#
# Usage:
#   ./build_base.sh          # build only
#   ./build_base.sh flash    # build + flash (default PORT below; override with -p <by-id>)
#   ./build_base.sh flash --inject   # TEST ONLY: build with the fault injection
#                                    # console (FSD T-A11/T-A13). Deliberately
#                                    # lies to the remote — reflash normally after.
#   ./build_base.sh -p PORT  # build and flash to custom port

set -euo pipefail
cd "$(dirname "$0")"

SCRIPT_DIR="$(pwd)"
SDKCONFIG_BASE="$SCRIPT_DIR/sdkconfig.base"
SDKCONFIG_REMOTE="$SCRIPT_DIR/sdkconfig.remote"
BUILD_DIR="$SCRIPT_DIR/build_base"

# Default flash port (base unit)
PORT="/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E042156-if00"   # base COM port — stable board serial (survives chip swaps). Find yours: ls /dev/serial/by-id/

# Parse args
DO_FLASH=false
DO_INJECT=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        flash)    DO_FLASH=true ;;
        --inject) DO_INJECT=true ;;
        -p)       shift; PORT="$1" ;;
        *)        echo "Usage: $0 [flash] [--inject] [-p PORT]"; exit 1 ;;
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

echo "=== Building BASE unit ==="

# Install base sdkconfig as the active one
cp "$SDKCONFIG_BASE" "$SCRIPT_DIR/sdkconfig"

# --inject: TEST-ONLY build with the fault injection console (FSD T-A11/T-A13).
# Appended to the working copy only — sdkconfig.base is never modified, so the
# option cannot leak into a later normal build. A build dir configured the other
# way is wiped, because Kconfig changes do not always force a full reconfigure
# and a stale one would silently produce the wrong firmware.
INJECT_MARK="$SCRIPT_DIR/.build_base_inject"
if $DO_INJECT; then
    if [ ! -f "$INJECT_MARK" ] && [ -d "$BUILD_DIR" ]; then
        echo "Switching to FAULT INJECTION build — cleaning build_base/"
        rm -rf "$BUILD_DIR"
    fi
    touch "$INJECT_MARK"
    echo
    echo "***********************************************************"
    echo "*  BUILDING WITH FAULT INJECTION - NOT SAFE FOR LIVE USE   *"
    echo "*  Reflash a normal build (./build_base.sh flash) after.   *"
    echo "***********************************************************"
    echo
elif [ -f "$INJECT_MARK" ]; then
    echo "Previous build had FAULT INJECTION — cleaning build_base/"
    rm -rf "$BUILD_DIR"
    rm -f "$INJECT_MARK"
fi

# Clean build dir if it was previously configured as remote
if [ -f "$BUILD_DIR/config/sdkconfig.h" ]; then
    CURRENT_ROLE=$(grep "CONFIG_RLC_UNIT_" "$BUILD_DIR/config/sdkconfig.h" | grep "=1" || true)
    if echo "$CURRENT_ROLE" | grep -q "REMOTE"; then
        echo "Previous build was REMOTE — cleaning build_base/"
        rm -rf "$BUILD_DIR"
    fi
fi

# Configure and build
idf.py -B "$BUILD_DIR" set-target esp32s3 2>&1 | tail -1

# MUST come after set-target: that command regenerates sdkconfig from the
# defaults, so anything appended earlier is silently discarded and you get a
# normal build wearing an injection build's log messages.
if $DO_INJECT; then
    echo "CONFIG_RLC_FAULT_INJECTION=y" >> "$SCRIPT_DIR/sdkconfig"
fi

idf.py -B "$BUILD_DIR" build 2>&1 | grep -E "warning:|error:|Generated|build complete" || true

# Fail loudly rather than flashing a build that silently lacks the option.
if $DO_INJECT; then
    if ! grep -q "define CONFIG_RLC_FAULT_INJECTION 1" "$BUILD_DIR/config/sdkconfig.h"; then
        echo "ERROR: --inject requested but CONFIG_RLC_FAULT_INJECTION is not in the built config!"
        exit 1
    fi
    echo "Verified: CONFIG_RLC_FAULT_INJECTION is ON in the built config"
fi

# Verify the binary contains base_app_main
NM=$(find ~/.espressif/tools/xtensa-esp-elf -name "xtensa-esp32s3-elf-nm" 2>/dev/null | head -1)
if [ -n "$NM" ]; then
    if $NM "$BUILD_DIR/rlc.elf" 2>/dev/null | grep -q "T base_app_main"; then
        echo "Verified: base_app_main in binary"
    else
        echo "ERROR: base_app_main NOT found in binary!"
        exit 1
    fi
fi

# Save back the resolved sdkconfig
cp "$BUILD_DIR/config/sdkconfig.h" "$SCRIPT_DIR/sdkconfig.base.resolved" 2>/dev/null || true

# Restore remote sdkconfig as active (so default `idf.py build` still works for remote)
cp "$SDKCONFIG_REMOTE" "$SCRIPT_DIR/sdkconfig"

if $DO_FLASH; then
    if $DO_INJECT; then
        echo "!! Flashing a FAULT INJECTION build — reflash a normal build afterwards !!"
    fi
    echo "=== Flashing BASE to $PORT ==="
    # Do NOT pipe esptool into tail without checking its status: the pipe's
    # exit code is tail's, so a failed flash exited 0 and printed nothing
    # alarming. That happened for real on 2026-08-27 — a serial contention
    # error left BOTH units on the previous firmware while the script reported
    # success, and the stale build was only caught by reading version banners
    # off the devices. In a project whose safety rule is "flash both units
    # together", a silently-failed flash is the wrong thing to be quiet about.
    FLASH_LOG=$(mktemp)
    if ! python3 -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
        --before default_reset --after hard_reset \
        write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
        0x10000 "$BUILD_DIR/rlc.bin" > "$FLASH_LOG" 2>&1; then
        echo "*** FLASH FAILED — BASE IS STILL RUNNING ITS PREVIOUS FIRMWARE ***"
        tail -15 "$FLASH_LOG"
        rm -f "$FLASH_LOG"
        exit 1
    fi
    tail -3 "$FLASH_LOG"
    rm -f "$FLASH_LOG"
    echo "Done."
else
    echo "Binary at: $BUILD_DIR/rlc.bin"
    echo "Flash with: $0 flash"
fi
