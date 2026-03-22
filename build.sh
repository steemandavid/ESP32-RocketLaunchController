#!/usr/bin/env bash
#
# Build helper for ESP32 Wireless Rocket Launch Controller
#
# Usage:
#   ./build.sh base     # Build base unit firmware
#   ./build.sh remote   # Build remote unit firmware
#   ./build.sh clean    # Clean build directory
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Source ESP-IDF environment
if [ -z "$IDF_PATH" ]; then
    if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
        source "$HOME/esp/esp-idf/export.sh" 2>/dev/null
    else
        echo "ERROR: ESP-IDF not found. Set IDF_PATH or install ESP-IDF."
        exit 1
    fi
fi

case "${1:-}" in
    base)
        echo "=== Building BASE unit firmware ==="
        rm -rf build
        SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.base" \
            idf.py set-target esp32s3 build
        echo ""
        echo "=== Base unit build complete ==="
        echo "Flash with: idf.py -p /dev/ttyUSBx flash monitor"
        ;;
    remote)
        echo "=== Building REMOTE unit firmware ==="
        rm -rf build
        SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.remote" \
            idf.py set-target esp32s3 build
        echo ""
        echo "=== Remote unit build complete ==="
        echo "Flash with: idf.py -p /dev/ttyUSBx flash monitor"
        ;;
    clean)
        echo "Cleaning build directory..."
        rm -rf build sdkconfig sdkconfig.old managed_components dependencies.lock
        echo "Done."
        ;;
    *)
        echo "Usage: $0 {base|remote|clean}"
        exit 1
        ;;
esac
