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

echo "=== Building REMOTE unit ==="

# Install remote sdkconfig as the active one
cp "$SDKCONFIG_REMOTE" "$SCRIPT_DIR/sdkconfig"

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
