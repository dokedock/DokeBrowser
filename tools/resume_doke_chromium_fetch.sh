#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ATTEMPTS="${DOKE_CHROMIUM_FETCH_ATTEMPTS:-3}"
SLEEP_SECONDS="${DOKE_CHROMIUM_FETCH_RETRY_SLEEP:-30}"

usage() {
  cat <<'USAGE'
Usage:
  bash tools/resume_doke_chromium_fetch.sh

Environment:
  DOKE_CHROMIUM_FETCH_ATTEMPTS=3       Number of sync attempts.
  DOKE_CHROMIUM_FETCH_RETRY_SLEEP=30   Seconds to sleep between attempts.

This wrapper resumes the ignored local Chromium checkout using the least
heavy recovery mode:

  prepare_doke_chromium_source.sh --sync --nohistory --shallow --nohooks -j 1

Use this after network interruptions. It does not delete partial checkouts;
prepare_doke_chromium_source.sh quarantines unusable src directories under
third_party/chromium/_bad_scm.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if ! [[ "$ATTEMPTS" =~ ^[0-9]+$ ]] || [[ "$ATTEMPTS" -lt 1 ]]; then
  echo "DOKE_CHROMIUM_FETCH_ATTEMPTS must be a positive integer" >&2
  exit 2
fi

attempt=1
while [[ "$attempt" -le "$ATTEMPTS" ]]; do
  echo "Chromium fetch status before attempt $attempt/$ATTEMPTS:"
  python3 "$ROOT_DIR/tools/doke_chromium_fetch_status.py" || true

  if bash "$ROOT_DIR/tools/prepare_doke_chromium_source.sh" \
    --sync --nohistory --shallow --nohooks -j 1; then
    python3 "$ROOT_DIR/tools/doke_chromium_fetch_status.py"
    exit 0
  fi

  if [[ "$attempt" -lt "$ATTEMPTS" ]]; then
    echo "Chromium sync attempt $attempt failed; retrying in $SLEEP_SECONDS seconds..." >&2
    sleep "$SLEEP_SECONDS"
  fi
  attempt=$((attempt + 1))
done

echo "Chromium sync failed after $ATTEMPTS attempt(s)." >&2
python3 "$ROOT_DIR/tools/doke_chromium_fetch_status.py" || true
exit 1
