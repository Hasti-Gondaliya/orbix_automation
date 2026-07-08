#!/usr/bin/env bash
# Fresh-board programming: erase flash, flash factory QR data, build + flash firmware.
#
# Usage:
#   ./scripts/flash_fresh_board.sh <device_number>
#   ./scripts/flash_fresh_board.sh 5 -p /dev/ttyACM0
#   ./scripts/flash_fresh_board.sh 5 --dry-run
#
# Steps:
#   1. Source ESP-IDF and ESP-Matter (get_idf / get_matter)
#   2. idf.py erase-flash + flash factory partition for device N
#   3. idf.py build flash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <device_number> [-p port] [--dry-run]" >&2
    exit 1
fi

SEQUENCE="$1"
shift

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FIRMWARE_DIR="$PROJECT_ROOT/orbix-6-plug-hub"

IDF_EXPORT="${IDF_EXPORT:-/home/haki/Documents/product/esp-idf/export.sh}"
MATTER_EXPORT="${MATTER_EXPORT:-/home/haki/Documents/product/esp-matter/export.sh}"

if [[ ! -f "$IDF_EXPORT" ]]; then
    echo "ERROR: ESP-IDF export.sh not found: $IDF_EXPORT" >&2
    echo "Set IDF_EXPORT or run get_idf manually." >&2
    exit 1
fi
if [[ ! -f "$MATTER_EXPORT" ]]; then
    echo "ERROR: ESP-Matter export.sh not found: $MATTER_EXPORT" >&2
    echo "Set MATTER_EXPORT or run get_matter manually." >&2
    exit 1
fi

echo "==> Activating ESP-IDF"
# shellcheck disable=SC1090
source "$IDF_EXPORT"

echo "==> Activating ESP-Matter"
# export.sh uses ${ESP_MATTER_PATH} under set -u; pre-set if missing (get_matter alias does not use -u)
export ESP_MATTER_PATH="${ESP_MATTER_PATH:-$(cd "$(dirname "$MATTER_EXPORT")" && pwd)}"
# shellcheck disable=SC1090
source "$MATTER_EXPORT"

EXTRA_ARGS=("$@")
DRY_RUN=0
PORT_ARGS=()
for arg in "${EXTRA_ARGS[@]}"; do
    if [[ "$arg" == "--dry-run" ]]; then
        DRY_RUN=1
    fi
done

# Pass -p through to idf.py when provided
prev=""
for arg in "${EXTRA_ARGS[@]}"; do
    if [[ "$prev" == "-p" || "$prev" == "--port" ]]; then
        PORT_ARGS+=("-p" "$arg")
    fi
    prev="$arg"
done

echo ""
echo "==> [1/2] Erase flash and program factory data for device #$SEQUENCE"
python3 "$SCRIPT_DIR/flash_factory_device.py" "$SEQUENCE" --erase-flash "${EXTRA_ARGS[@]}"

echo ""
echo "==> [2/2] Build and flash firmware"
cd "$FIRMWARE_DIR"

if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "Command:"
    echo "  idf.py ${PORT_ARGS[*]} build flash"
    echo "(dry-run: not building/flashing firmware)"
    exit 0
fi

idf.py "${PORT_ARGS[@]}" build flash

echo ""
echo "Done: fresh board programmed as device #$SEQUENCE"
echo "Attach QR label from: mfg_qr_codes/fff2_8001/$SEQUENCE/"
