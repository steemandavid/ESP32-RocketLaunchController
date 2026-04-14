#!/bin/bash
# Build and optionally flash the BASE unit firmware.
#
# Usage:
#   ./build_base.sh          # build only
#   ./build_base.sh flash    # build and flash to /dev/ttyACM0
#   ./build_base.sh -p PORT  # build and flash to custom port

set -euo pipefail
cd "$(dirname "$0")"

SCRIPT_DIR="$(pwd)"
SDKCONFIG_BASE="$SCRIPT_DIR/sdkconfig.base"
SDKCONFIG_REMOTE="$SCRIPT_DIR/sdkconfig.remote"
BUILD_DIR="$SCRIPT_DIR/build_base"

# Default flash port (base unit)
PORT="/dev/ttyACM0"

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

echo "=== Building BASE unit ==="

# Install base sdkconfig as the active one
cp "$SDKCONFIG_BASE" "$SCRIPT_DIR/sdkconfig"

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
idf.py -B "$BUILD_DIR" build 2>&1 | grep -E "warning:|error:|Generated|build complete" || true

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
    echo "=== Flashing BASE to $PORT ==="
    python3 -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
        --before default_reset --after hard_reset \
        write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
        0x10000 "$BUILD_DIR/rlc.bin" 2>&1 | tail -3
    echo "Done."
else
    echo "Binary at: $BUILD_DIR/rlc.bin"
    echo "Flash with: $0 flash"
fi
