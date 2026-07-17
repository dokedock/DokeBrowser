#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

resolve_src() {
  if [[ -n "${DOKE_CHROMIUM_SRC:-}" ]]; then
    printf '%s\n' "$DOKE_CHROMIUM_SRC"
    return
  fi
  if [[ -d "$ROOT_DIR/third_party/chromium/src" ]]; then
    printf '%s\n' "$ROOT_DIR/third_party/chromium/src"
    return
  fi
  if [[ -d "$ROOT_DIR/third_party/chromium" ]]; then
    printf '%s\n' "$ROOT_DIR/third_party/chromium"
    return
  fi
}

SRC_DIR="$(resolve_src || true)"
OUT_DIR="${DOKE_CHROMIUM_OUT:-out/Doke}"
TARGET="${DOKE_CHROMIUM_TARGET:-chrome}"

if [[ -z "${SRC_DIR:-}" || ! -d "$SRC_DIR" ]]; then
  echo "DOKE_CHROMIUM_SRC is not set and no local Chromium checkout was found." >&2
  echo "Set DOKE_CHROMIUM_SRC=/absolute/path/to/chromium/src." >&2
  exit 2
fi

cd "$SRC_DIR"

if [[ ! -x "tools/gn/bootstrap/bootstrap.py" && ! -x "buildtools/linux64/gn" && ! -x "buildtools/mac/gn" ]]; then
  echo "Chromium build tools were not found in $SRC_DIR." >&2
  echo "Prepare the Chromium checkout first, then rerun this script." >&2
  exit 3
fi

if ! command -v autoninja >/dev/null 2>&1 && ! command -v ninja >/dev/null 2>&1; then
  echo "Neither autoninja nor ninja is available in PATH." >&2
  exit 3
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

if command -v autoninja >/dev/null 2>&1; then
  autoninja -C "$OUT_DIR" "$TARGET"
else
  ninja -C "$OUT_DIR" "$TARGET"
fi

echo "Built target '$TARGET' in $SRC_DIR/$OUT_DIR"
