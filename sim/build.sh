#!/usr/bin/env bash
# WebAssembly build: core/src/*.cpp + wasm_api.cpp + wasm_shim.cpp -> docs/viewer/fftviz.wasm (zig c++, no libc).
# The resulting binary is committed, so the GitHub Pages viewer works without a build step.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build-sim"
WASM="$ROOT/docs/viewer/fftviz.wasm"
mkdir -p "$OUT/obj" "$ROOT/docs/viewer"

# Locate zig: $ZIG, then tools/.venv (python -m ziglang), then PATH.
if [ -n "${ZIG:-}" ]; then
  ZC=("$ZIG")
elif [ -x "$ROOT/tools/.venv/bin/python" ] && "$ROOT/tools/.venv/bin/python" -m ziglang version >/dev/null 2>&1; then
  ZC=("$ROOT/tools/.venv/bin/python" -m ziglang)
elif command -v zig >/dev/null 2>&1; then
  ZC=(zig)
else
  echo "ERROR: zig not found (set ZIG=... or pip install ziglang into tools/.venv)" >&2
  exit 1
fi

CXXFLAGS=(-target wasm32-freestanding ${OPT:--O2} -std=c++17 -fno-exceptions -fno-rtti -nostdlib
          -fvisibility=hidden -fno-threadsafe-statics -I"$ROOT/core/src")

OBJS=()
compile() {  # $1 = source file, remaining arguments = extra flags
  local src="$1"; shift
  local o="$OUT/obj/$(basename "${src%.cpp}").o"
  if [ ! -f "$o" ] || [ "$src" -nt "$o" ] || [ -n "$(find "$ROOT/core/src" -name '*.h' -newer "$o" -print -quit)" ]; then
    "${ZC[@]}" c++ "${CXXFLAGS[@]}" "$@" -c "$src" -o "$o"
  fi
  OBJS+=("$o")
}

for f in "$ROOT"/core/src/*.cpp; do compile "$f"; done
compile "$ROOT/sim/wasm_api.cpp"
compile "$ROOT/sim/wasm_shim.cpp" -fno-builtin

"${ZC[@]}" c++ -target wasm32-freestanding -O2 -nostdlib "${OBJS[@]}" \
  -Wl,--no-entry -Wl,-z,stack-size=262144 -Wl,--gc-sections -Wl,--strip-all -Wl,--initial-memory=1048576 \
  -o "$WASM"
chmod 644 "$WASM"

# Sanity check: no imports, expected exports present, no build path embedded in the committed binary.
node --input-type=module -e "
import fs from 'node:fs';
const bin = fs.readFileSync('$WASM');
const m = new WebAssembly.Module(bin);
const imp = WebAssembly.Module.imports(m);
const exp = WebAssembly.Module.exports(m).map(e => e.name);
if (imp.length) { console.error('ERROR: unexpected imports:', JSON.stringify(imp)); process.exit(1); }
if (!exp.includes('memory') || !exp.includes('viz_init')) { console.error('ERROR: missing exports'); process.exit(1); }
if (bin.includes('$ROOT')) { console.error('ERROR: the binary embeds the build directory path'); process.exit(1); }
console.log('OK: no imports, ' + exp.length + ' exports, ' + bin.length + ' bytes');
"
