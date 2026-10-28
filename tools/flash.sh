#!/usr/bin/env bash
# flash.sh — derlenmiş firmware'i karta yükler. PORT=/dev/ttyUSB0 (varsayılan).
# Kullanım: tools/flash.sh [--build]   (--build: önce tools/build_fw.sh çalıştırır)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${PORT:-/dev/ttyUSB0}"
FQBN="${FQBN:-esp32:esp32:esp32:CPUFreq=240,PartitionScheme=no_ota}"
OUT="${OUT:-$ROOT/build/fw}"

if [[ "${1:-}" == "--build" || ! -f "$OUT/fftviz.ino.bin" ]]; then
  "$ROOT/tools/build_fw.sh"
fi

arduino-cli upload --fqbn "$FQBN" --port "$PORT" --input-dir "$OUT" "$ROOT/firmware/fftviz"
echo "Yüklendi: $PORT"
