#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATCH_DIR="$ROOT_DIR/patches/chromium"
SERIES_FILE="$PATCH_DIR/series"

python3 "$ROOT_DIR/tools/chromium_patch_queue_check.py" --patch-dir "$PATCH_DIR" --series "$SERIES_FILE" >/dev/null
python3 "$ROOT_DIR/tools/doke_probe_patch_check.py" "$PATCH_DIR/0001-doke-probe-contract.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_runtime_patch_check.py" "$PATCH_DIR/0002-doke-runtime-config-load.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_ua_patch_check.py" "$PATCH_DIR/0003-doke-runtime-ua-lang-switches.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_ua_ch_patch_check.py" "$PATCH_DIR/0004-doke-runtime-ua-client-hints-ingress.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_ua_ch_override_patch_check.py" "$PATCH_DIR/0005-doke-runtime-ua-client-hints-override.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_webrtc_patch_check.py" "$PATCH_DIR/0006-doke-runtime-webrtc-policy.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_screen_patch_check.py" "$PATCH_DIR/0007-doke-runtime-screen-device-switches.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_hardware_patch_check.py" \
  --runtime-patch "$PATCH_DIR/0008-doke-runtime-hardware-switches.patch" \
  --blink-patch "$PATCH_DIR/0009-doke-blink-hardware-overrides.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_rendering_patch_check.py" "$PATCH_DIR/0010-doke-runtime-rendering-noise-ingress.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_surfaces_patch_check.py" "$PATCH_DIR/0011-doke-runtime-surface-preset-ingress.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_alignment_patch_check.py" "$PATCH_DIR/0012-doke-runtime-alignment-ingress.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_automation_patch_check.py" "$PATCH_DIR/0013-doke-runtime-automation-ingress.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_webdriver_patch_check.py" "$PATCH_DIR/0014-doke-blink-webdriver-policy.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_automation_controlled_patch_check.py" "$PATCH_DIR/0015-doke-automation-controlled-policy.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_cdp_side_effect_patch_check.py" "$PATCH_DIR/0016-doke-cdp-side-effect-preview-guard.patch" >/dev/null
python3 "$ROOT_DIR/tools/doke_patch_apply_smoke.py" --patch-dir "$PATCH_DIR" >/dev/null

SRC_DIR="$(python3 "$ROOT_DIR/tools/doke_chromium_source_check.py" --print-src 2>/dev/null || true)"
if [[ -z "$SRC_DIR" ]]; then
  echo "DOKE_CHROMIUM_SRC is not set and no local Chromium checkout was found." >&2
  echo "Set DOKE_CHROMIUM_SRC=/absolute/path/to/chromium/src." >&2
  exit 2
fi

if [[ ! -f "$SERIES_FILE" ]]; then
  echo "Missing patch series: $SERIES_FILE" >&2
  exit 2
fi

cd "$SRC_DIR"

patch_already_present() {
  local patch_name="$1"
  case "$patch_name" in
    0001-doke-probe-contract.patch)
      grep -q 'kDokeProbeSwitch' chrome/app/chrome_main.cc
      ;;
    0002-doke-runtime-config-load.patch)
      grep -q 'MaybeLoadDokeRuntimeConfig' chrome/app/chrome_main.cc
      ;;
    0003-doke-runtime-ua-lang-switches.patch)
      grep -q 'kDokeUserAgentSwitch' chrome/app/chrome_main.cc
      ;;
    0004-doke-runtime-ua-client-hints-ingress.patch)
      grep -q 'kDokeUaClientHintsKey' chrome/app/chrome_main.cc
      ;;
    0005-doke-runtime-ua-client-hints-override.patch)
      grep -q 'GetDokeUserAgentMetadataOverride' components/embedder_support/user_agent_utils.cc
      ;;
    0006-doke-runtime-webrtc-policy.patch)
      grep -q 'kDokeWebRtcIpHandlingPolicySwitch' chrome/app/chrome_main.cc
      ;;
    0007-doke-runtime-screen-device-switches.patch)
      grep -q 'kDokeWindowSizeSwitch' chrome/app/chrome_main.cc
      ;;
    0008-doke-runtime-hardware-switches.patch)
      grep -q 'kDokeHardwareConcurrencySwitch' chrome/app/chrome_main.cc
      ;;
    0009-doke-blink-hardware-overrides.patch)
      grep -q 'GetDokeHardwareConcurrencyOverride' third_party/blink/renderer/core/execution_context/navigator_base.cc &&
        grep -q 'GetDokeDeviceMemoryOverride' third_party/blink/renderer/core/frame/navigator_device_memory.cc
      ;;
    0010-doke-runtime-rendering-noise-ingress.patch)
      grep -q 'kDokeCanvasNoiseSeedSwitch' chrome/app/chrome_main.cc
      ;;
    0011-doke-runtime-surface-preset-ingress.patch)
      grep -q 'kDokePluginsPresetSwitch' chrome/app/chrome_main.cc
      ;;
    0012-doke-runtime-alignment-ingress.patch)
      grep -q 'kDokeTimezoneIdSwitch' chrome/app/chrome_main.cc
      ;;
    0013-doke-runtime-automation-ingress.patch)
      grep -q 'kDokeWebdriverPolicySwitch' chrome/app/chrome_main.cc
      ;;
    0014-doke-blink-webdriver-policy.patch)
      grep -q 'GetDokeWebdriverOverride' third_party/blink/renderer/core/frame/navigator.cc
      ;;
    0015-doke-automation-controlled-policy.patch)
      grep -q 'ApplyDokeAutomationControlledPolicy' content/child/runtime_features.cc
      ;;
    0016-doke-cdp-side-effect-preview-guard.patch)
      grep -q 'ApplyDokeInspectorEnvironment' chrome/app/chrome_main.cc &&
        grep -q 'IsDokeCdpSideEffectGuardEnabled' v8/src/inspector/v8-runtime-agent-impl.cc &&
        grep -q 'generatePreview = false' v8/src/inspector/v8-console-message.cc
      ;;
    *)
      return 1
      ;;
  esac
}

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

  if git apply --reverse --check "$patch_path" >/dev/null 2>&1; then
    echo "Already applied $patch_name"
    applied=$((applied + 1))
    continue
  fi
  if patch_already_present "$patch_name"; then
    echo "Already present $patch_name"
    applied=$((applied + 1))
    continue
  fi

  echo "Applying $patch_name"
  git apply --check "$patch_path"
  git apply "$patch_path"
  applied=$((applied + 1))
done < "$SERIES_FILE"

echo "Applied $applied Chromium patch(es) to $SRC_DIR"
