#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  tools/select_chromium_xcode.sh [--path /Applications/Xcode.app] [--check-only] [--accept-license]

Select and validate a full Xcode.app for local Doke Chromium builds.

Options:
  --path PATH        Xcode.app path. Defaults to /Applications/Xcode.app.
  --check-only      Only validate the requested Xcode path and active developer dir.
  --accept-license  After selecting Xcode, run xcodebuild -license accept.
  -h, --help        Show this help.
USAGE
}

XCODE_APP="${XCODE_APP:-/Applications/Xcode.app}"
CHECK_ONLY=0
ACCEPT_LICENSE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --path)
      if [[ $# -lt 2 ]]; then
        echo "--path requires a value" >&2
        exit 2
      fi
      XCODE_APP="$2"
      shift 2
      ;;
    --check-only)
      CHECK_ONLY=1
      shift
      ;;
    --accept-license)
      ACCEPT_LICENSE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

DEVELOPER_DIR="$XCODE_APP/Contents/Developer"
XCODEBUILD="$DEVELOPER_DIR/usr/bin/xcodebuild"

if [[ ! -d "$XCODE_APP" ]]; then
  echo "full_xcode_missing path=$XCODE_APP" >&2
  echo "Install full Xcode.app first, then rerun this script." >&2
  exit 1
fi

if [[ ! -d "$DEVELOPER_DIR" || ! -x "$XCODEBUILD" ]]; then
  echo "full_xcode_invalid path=$XCODE_APP" >&2
  echo "Expected $XCODEBUILD to exist and be executable." >&2
  exit 1
fi

ACTIVE_DEVELOPER_DIR="$(xcode-select -p 2>/dev/null || true)"
echo "requested_xcode=$XCODE_APP"
echo "requested_developer_dir=$DEVELOPER_DIR"
echo "active_developer_dir=${ACTIVE_DEVELOPER_DIR:-<none>}"

if "$XCODEBUILD" -version >/dev/null 2>&1; then
  "$XCODEBUILD" -version
else
  echo "warning: $XCODEBUILD -version failed before selection" >&2
fi

if [[ "$CHECK_ONLY" == "1" ]]; then
  if [[ "$ACTIVE_DEVELOPER_DIR" == "$DEVELOPER_DIR" ]] && xcodebuild -version >/dev/null 2>&1; then
    echo "xcode_selection_ok"
    exit 0
  fi
  echo "xcode_selection_required" >&2
  exit 1
fi

if [[ "$EUID" -eq 0 ]]; then
  xcode-select -s "$DEVELOPER_DIR"
else
  sudo xcode-select -s "$DEVELOPER_DIR"
fi

if [[ "$ACCEPT_LICENSE" == "1" ]]; then
  if [[ "$EUID" -eq 0 ]]; then
    xcodebuild -license accept
  else
    sudo xcodebuild -license accept
  fi
fi

if xcodebuild -version; then
  echo "xcode_selection_ok"
else
  echo "xcode_selection_failed" >&2
  echo "If this is an Xcode license prompt, rerun with --accept-license or open Xcode once." >&2
  exit 1
fi
