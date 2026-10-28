#!/usr/bin/env bash
# build_fw.sh — firmware derlemesi (arduino-cli, core/ kütüphanesi, -O2).
# Single application on the core's standard "No OTA" partition scheme: nvs stays at 0x9000/0x5000 (saved
# settings), app0 at 0x10000. The script fails if the generated table or the app image breaks that contract.
# Kullanım: tools/build_fw.sh [ek arduino-cli argümanları]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN="${FQBN:-esp32:esp32:esp32:CPUFreq=240,PartitionScheme=no_ota}"
OUT="${OUT:-$ROOT/build/fw}"
mkdir -p "$OUT"

# Arduino varsayılanı -Os; sıcak döngüler için -O2.
arduino-cli compile \
  --fqbn "$FQBN" \
  --library "$ROOT/core" \
  --build-property "compiler.optimization_flags=-O2" \
  --build-property "compiler.optimization_flags.release=-O2" \
  --warnings default \
  --output-dir "$OUT" \
  "$@" \
  "$ROOT/firmware/fftviz"

# The core also emits a flash-size merged image that spans NVS; flashing it would erase the saved settings.
rm -f "$OUT/fftviz.ino.merged.bin"

python3 - "$OUT/fftviz.ino.partitions.bin" "$OUT/fftviz.ino.bin" <<'EOF'
import struct, sys
table = open(sys.argv[1], "rb").read()
app_size = len(open(sys.argv[2], "rb").read())
parts = {}
for i in range(0, len(table), 32):
    entry = table[i:i + 32]
    if entry[:2] != b"\xaa\x50":
        break
    kind, sub, off, size = struct.unpack("<BBII", entry[2:12])
    parts[entry[12:28].rstrip(b"\0").decode()] = (kind, sub, off, size)
if parts.get("nvs") != (1, 2, 0x9000, 0x5000):
    sys.exit("build_fw.sh: nvs partition is not data/nvs 0x9000+0x5000: %r" % (parts.get("nvs"),))
apps = {name: p for name, p in parts.items() if p[0] == 0}
if list(apps) != ["app0"] or apps["app0"][2] != 0x10000:
    sys.exit("build_fw.sh: expected exactly one app partition at 0x10000: %r" % (apps,))
if app_size > apps["app0"][3]:
    sys.exit("build_fw.sh: app image %d B does not fit app0 (%d B)" % (app_size, apps["app0"][3]))
print("partitions OK: nvs 0x9000+0x5000, app0 0x10000+0x%x, app %d B (%.1f%% of slot)"
      % (apps["app0"][3], app_size, 100.0 * app_size / apps["app0"][3]))
EOF

ELF="$OUT/fftviz.ino.elf"
if [[ -f "$ELF" ]]; then
  SIZE="$(ls ~/.arduino15/packages/esp32/tools/esp-x32/*/bin/xtensa-esp32-elf-size 2>/dev/null | head -1 || true)"
  [[ -n "$SIZE" ]] && "$SIZE" -A "$ELF" | grep -E '^\.(iram0\.text|dram0\.data|dram0\.bss|flash\.text|flash\.rodata)' || true
  echo "BIN: $OUT/fftviz.ino.bin ($(stat -c %s "$OUT/fftviz.ino.bin") B)"
  (cd "$OUT" && sha256sum fftviz.ino.bin fftviz.ino.bootloader.bin fftviz.ino.partitions.bin)
fi
