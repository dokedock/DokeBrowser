#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATCH_DIR="$ROOT_DIR/patches/chromium"
SERIES_FILE="$PATCH_DIR/series"

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
if [[ -z "${SRC_DIR:-}" || ! -d "$SRC_DIR" ]]; then
  echo "DOKE_CHROMIUM_SRC is not set and no local Chromium checkout was found." >&2
  echo "Set DOKE_CHROMIUM_SRC=/absolute/path/to/chromium/src." >&2
  exit 2
fi

if [[ ! -f "$SERIES_FILE" ]]; then
  echo "Missing patch series: $SERIES_FILE" >&2
  exit 2
fi

cd "$SRC_DIR"

applied=0
while IFS= read -r entry; do
  patch_name="${entry%%#*}"
  patch_name="$(printf '%s' "$patch_name" | xargs)"
  [[ -z "$patch_name" ]] && continue

  patch_path="$PATCH_DIR/$patch_name"
  if [[ ! -f "$patch_path" ]]; then
    echo "Missing patch file listed in series: $patch_name" >&2
    exit 3
  fi

  echo "Applying $patch_name"
  git apply --check "$patch_path"
  git apply "$patch_path"
  applied=$((applied + 1))
done < "$SERIES_FILE"

echo "Applied $applied Chromium patch(es) to $SRC_DIR"

