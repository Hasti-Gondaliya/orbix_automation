#!/usr/bin/env bash
# Generate Orbix factory partitions + unique QR codes, then organize as:
#   <project>/out/fff2_8001/<1..N>/<uuid>/<uuid>-qrcode.png
set -euo pipefail

COUNT="${1:-50}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_ROOT="${OUT_ROOT:-$PROJECT_ROOT}"
VID="${VID:-0xFFF2}"
PID="${PID:-0x8001}"

if [[ -z "${ESP_MATTER_PATH:-}" ]]; then
    echo "ERROR: ESP_MATTER_PATH is not set" >&2
    exit 1
fi

MATTER_SDK_PATH="${MATTER_SDK_PATH:-$ESP_MATTER_PATH/connectedhomeip/connectedhomeip}"

mkdir -p "$OUT_ROOT"
cd "$OUT_ROOT"

echo "==> Generating $COUNT factory partition(s)..."
esp-matter-mfg-tool -n "$COUNT" \
    -v "$VID" -p "$PID" \
    --vendor-name "Orbix" --product-name "6-Plug Controller" \
    --hw-ver 1 --hw-ver-str v1.0 --mfg-date "$(date +%Y%m%d)" \
    --pai \
    -k "$MATTER_SDK_PATH/credentials/test/attestation/Chip-Test-PAI-FFF2-8001-Key.pem" \
    -c "$MATTER_SDK_PATH/credentials/test/attestation/Chip-Test-PAI-FFF2-8001-Cert.pem" \
    -cd "$MATTER_SDK_PATH/credentials/test/certification-declaration/Chip-Test-CD-FFF2-8001.der" \
    --outdir "$OUT_ROOT/mfg_qr_codes"

echo "==> Organizing into numbered folders (1..$COUNT)..."
python3 "$SCRIPT_DIR/reorganize_mfg_output.py" \
    --outdir "$OUT_ROOT/mfg_qr_codes" \
    --vendor-id "$VID" \
    --product-id "$PID"

VID_PID="$(printf '%04x_%04x' "$VID" "$PID")"
echo ""
echo "Output layout:"
echo "  $OUT_ROOT/mfg_qr_codes/$VID_PID/<1..$COUNT>/<uuid>/<uuid>-qrcode.png"
echo "  $OUT_ROOT/mfg_qr_codes/$VID_PID/<1..$COUNT>/<uuid>/<uuid>-partition.bin"
echo ""
echo "Example flash (device #1):"
echo "  python3 $SCRIPT_DIR/flash_factory_device.py 1"
