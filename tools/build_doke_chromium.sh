#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPOT_TOOLS_DIR="${DOKE_DEPOT_TOOLS_DIR:-$ROOT_DIR/third_party/depot_tools}"
OUT_DIR="${DOKE_CHROMIUM_OUT:-out/Doke}"
TARGET="${DOKE_CHROMIUM_TARGET:-chrome}"
APPLY_PATCHES="${DOKE_CHROMIUM_APPLY_PATCHES:-0}"

if [[ -d "$DEPOT_TOOLS_DIR" ]]; then
  export PATH="$DEPOT_TOOLS_DIR:$PATH"
fi

python3 "$ROOT_DIR/tools/doke_chromium_source_check.py" --out-dir "$OUT_DIR" --require-build-tools

SRC_DIR="$(python3 "$ROOT_DIR/tools/doke_chromium_source_check.py" --out-dir "$OUT_DIR" --print-src || true)"
if [[ -z "$SRC_DIR" ]]; then
  echo "DOKE_CHROMIUM_SRC is not set and no local Chromium checkout was found." >&2
  echo "Set DOKE_CHROMIUM_SRC=/absolute/path/to/chromium/src." >&2
  exit 2
fi

if ! python3 "$ROOT_DIR/tools/doke_chromium_build_prereq.py" --src "$SRC_DIR"; then
  exit 4
fi

python3 "$ROOT_DIR/tools/ensure_chromium_generated_versions.py" --src "$SRC_DIR"

cd "$SRC_DIR"

export GOCACHE="${DOKE_GO_CACHE:-$SRC_DIR/$OUT_DIR/.gocache}"
mkdir -p "$GOCACHE"

if ! command -v autoninja >/dev/null 2>&1 && ! command -v ninja >/dev/null 2>&1; then
  echo "Neither autoninja nor ninja is available in PATH." >&2
  exit 3
fi

find_gn() {
  if command -v gn >/dev/null 2>&1; then
    command -v gn
    return
  fi
  for candidate in buildtools/mac/gn buildtools/linux64/gn buildtools/win/gn.exe; do
    if [[ -x "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return
    fi
  done
}

GN_BIN="$(find_gn || true)"
if [[ -z "$GN_BIN" ]]; then
  echo "GN is unavailable. Install depot_tools or prepare Chromium buildtools." >&2
  exit 3
fi

if [[ "$APPLY_PATCHES" == "1" || "$APPLY_PATCHES" == "true" ]]; then
  bash "$ROOT_DIR/tools/apply_chromium_patches.sh"
fi

if [[ ! -f "$OUT_DIR/args.gn" ]]; then
  mkdir -p "$OUT_DIR"
  cat > "$OUT_DIR/args.gn" <<'ARGS'
is_debug = false
is_component_build = false
symbol_level = 1
blink_symbol_level = 0
ARGS
  echo "Created default $OUT_DIR/args.gn. Review it before production builds."
fi

if [[ ! -f "$OUT_DIR/build.ninja" || "$OUT_DIR/args.gn" -nt "$OUT_DIR/build.ninja" ]]; then
  "$GN_BIN" gen "$OUT_DIR"
fi

if command -v autoninja >/dev/null 2>&1; then
  autoninja -C "$OUT_DIR" "$TARGET"
else
  ninja -C "$OUT_DIR" "$TARGET"
fi

echo "Built target '$TARGET' in $SRC_DIR/$OUT_DIR"
