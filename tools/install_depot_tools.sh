#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPOT_TOOLS_DIR="${DOKE_DEPOT_TOOLS_DIR:-$ROOT_DIR/third_party/depot_tools}"
DEPOT_TOOLS_URL="${DOKE_DEPOT_TOOLS_URL:-https://chromium.googlesource.com/chromium/tools/depot_tools.git}"

if [[ -d "$DEPOT_TOOLS_DIR/.git" ]]; then
  git -C "$DEPOT_TOOLS_DIR" pull --ff-only
else
  mkdir -p "$(dirname "$DEPOT_TOOLS_DIR")"
  git clone "$DEPOT_TOOLS_URL" "$DEPOT_TOOLS_DIR"
fi

cat <<EOF
depot_tools ready: $DEPOT_TOOLS_DIR
Add this before preparing Chromium:
export PATH="$DEPOT_TOOLS_DIR:\$PATH"
EOF
