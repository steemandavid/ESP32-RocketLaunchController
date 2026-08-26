#!/bin/bash
# Build, flash and monitor the arm-relay AND-gate verifier on the BASE unit.
#
# Sources the ESP-IDF environment itself, so it works from a fresh shell —
# same convention as ./build_base.sh in the project root.
#
# Usage:
#   ./run.sh              # build + flash + monitor (default base port below)
#   ./run.sh -p PORT      # override the port
#
# Exit the monitor with Ctrl-]

set -euo pipefail
cd "$(dirname "$0")"

# Base unit COM port — stable board serial (survives chip swaps, NOT board
# swaps). Find yours with: ls /dev/serial/by-id/
PORT="/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E042156-if00"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p) shift; PORT="$1" ;;
        *)  echo "Usage: $0 [-p PORT]"; exit 1 ;;
    esac
    shift
done

if [[ ! -e "$PORT" ]]; then
    echo "ERROR: $PORT not found. Available:"
    ls /dev/serial/by-id/ 2>/dev/null || echo "  (none — is the board plugged in?)"
    exit 1
fi

source ~/esp/esp-idf/export.sh >/dev/null 2>&1

echo
echo "!! DISCONNECT ALL IGNITERS — this energises the arm relay !!"
echo
echo "=== armgate-test -> $PORT ==="
exec idf.py -p "$PORT" flash monitor
