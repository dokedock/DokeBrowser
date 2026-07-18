#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECKOUT_ROOT="${DOKE_CHROMIUM_CHECKOUT_ROOT:-$ROOT_DIR/third_party/chromium}"
DEPOT_TOOLS_DIR="${DOKE_DEPOT_TOOLS_DIR:-$ROOT_DIR/third_party/depot_tools}"
SRC_ARG=""
FETCH_CHROMIUM=0
SYNC_CHROMIUM=0
NOHOOKS=0
NOHISTORY=0
SHALLOW=0
GIT_CACHE=0
JOBS=""

quarantine_partial_src() {
  local src_dir="$CHECKOUT_ROOT/src"
  if [[ ! -d "$src_dir/.git" ]]; then
    return
  fi
  if git -C "$src_dir" rev-parse --verify HEAD >/dev/null 2>&1; then
    return
  fi

  mkdir -p "$CHECKOUT_ROOT/_bad_scm"
  local stamp
  stamp="$(date +%Y%m%d%H%M%S)"
  local dest="$CHECKOUT_ROOT/_bad_scm/src_partial_$stamp"
  echo "Quarantining partial Chromium src checkout without HEAD: $dest" >&2
  mv "$src_dir" "$dest"
}

ensure_git_cache_config() {
  local gclient_file="$CHECKOUT_ROOT/.gclient"
  if [[ ! -f "$gclient_file" ]]; then
    return
  fi
  if [[ "$GIT_CACHE" -ne 1 ]]; then
    if grep -q '^cache_dir[[:space:]]*=' "$gclient_file"; then
      local temp_file
      temp_file="$(mktemp "$CHECKOUT_ROOT/.gclient.no_cache.XXXXXX")"
      grep -v '^cache_dir[[:space:]]*=' "$gclient_file" > "$temp_file"
      mv "$temp_file" "$gclient_file"
      echo "Disabled gclient git cache for single-branch recovery sync" >&2
    fi
    return
  fi
  if grep -q '^cache_dir[[:space:]]*=' "$gclient_file"; then
    return
  fi

  cat >> "$gclient_file" <<'EOF'

cache_dir = "git_cache"
EOF
  echo "Enabled gclient git cache at $CHECKOUT_ROOT/git_cache" >&2
}

usage() {
  cat <<'USAGE'
Usage:
  bash tools/prepare_doke_chromium_source.sh --src /absolute/path/to/chromium/src
  bash tools/prepare_doke_chromium_source.sh --fetch --nohistory --nohooks
  bash tools/prepare_doke_chromium_source.sh --sync --no-history --shallow -j 1

Options:
  --src PATH        Validate and print export lines for an existing Chromium src checkout.
  --fetch           Use depot_tools fetch to create third_party/chromium/src.
  --sync            Run gclient sync in the existing checkout workspace.
  --nohooks         Skip Chromium hooks during fetch/sync.
  --nohistory       Use shallow/no-history fetch where supported.
  --no-history      Alias for --nohistory.
  --shallow         Ask gclient to shallow clone into the cache dir.
  --git-cache       Ask fetch to bootstrap and use a shared git cache.
  -j, --jobs N      Pass gclient sync job count.
USAGE
}

if [[ -d "$DEPOT_TOOLS_DIR" ]]; then
  export PATH="$DEPOT_TOOLS_DIR:$PATH"
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --src)
      SRC_ARG="${2:-}"
      if [[ -z "$SRC_ARG" ]]; then
        echo "--src requires a path" >&2
        exit 2
      fi
      shift 2
      ;;
    --fetch)
      FETCH_CHROMIUM=1
      shift
      ;;
    --sync)
      SYNC_CHROMIUM=1
      shift
      ;;
    --nohooks|--no-hooks)
      NOHOOKS=1
      shift
      ;;
    --nohistory|--no-history)
      NOHISTORY=1
      shift
      ;;
    --shallow)
      SHALLOW=1
      shift
      ;;
    --git-cache)
      GIT_CACHE=1
      shift
      ;;
    -j|--jobs)
      JOBS="${2:-}"
      if [[ -z "$JOBS" ]]; then
        echo "$1 requires a value" >&2
        exit 2
      fi
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -n "$SRC_ARG" ]]; then
  SRC_PATH="$(cd "$SRC_ARG" && pwd)"
  python3 "$ROOT_DIR/tools/doke_chromium_source_check.py" --src "$SRC_PATH"
  cat <<EOF

Add this to your shell before building:
export DOKE_CHROMIUM_SRC="$SRC_PATH"
export DOKE_CHROMIUM_OUT="\${DOKE_CHROMIUM_OUT:-out/Doke}"
export DOKE_CHROMIUM_TARGET="\${DOKE_CHROMIUM_TARGET:-chrome}"
EOF
  exit 0
fi

if [[ "$FETCH_CHROMIUM" -eq 1 ]]; then
  if ! command -v fetch >/dev/null 2>&1; then
    echo "depot_tools fetch is not available in PATH." >&2
    echo "Install depot_tools, add it to PATH, then rerun with --fetch." >&2
    exit 3
  fi
  mkdir -p "$CHECKOUT_ROOT"
  cd "$CHECKOUT_ROOT"
  if [[ ! -f ".gclient" && ! -d "src" ]]; then
    fetch_args=()
    [[ "$NOHOOKS" -eq 1 ]] && fetch_args+=(--nohooks)
    [[ "$NOHISTORY" -eq 1 ]] && fetch_args+=(--nohistory)
    [[ "$GIT_CACHE" -eq 1 ]] && fetch_args+=(--git-cache)
    fetch "${fetch_args[@]}" chromium
  else
    echo "Existing Chromium workspace detected at $CHECKOUT_ROOT"
  fi
  SYNC_CHROMIUM=1
fi

if [[ "$SYNC_CHROMIUM" -eq 1 ]]; then
  if ! command -v gclient >/dev/null 2>&1; then
    echo "depot_tools gclient is not available in PATH." >&2
    exit 3
  fi
  if [[ -f "$CHECKOUT_ROOT/.gclient" ]]; then
    cd "$CHECKOUT_ROOT"
  elif [[ -f "$CHECKOUT_ROOT/../.gclient" ]]; then
    cd "$CHECKOUT_ROOT/.."
  else
    echo "No .gclient workspace found under $CHECKOUT_ROOT." >&2
    exit 2
  fi
  ensure_git_cache_config
  quarantine_partial_src
  sync_args=()
  [[ "$NOHOOKS" -eq 1 ]] && sync_args+=(--nohooks)
  [[ "$NOHISTORY" -eq 1 ]] && sync_args+=(--no-history)
  [[ "$SHALLOW" -eq 1 ]] && sync_args+=(--shallow)
  [[ -n "$JOBS" ]] && sync_args+=(-j "$JOBS")
  gclient sync "${sync_args[@]}"
fi

python3 "$ROOT_DIR/tools/doke_chromium_source_check.py"
