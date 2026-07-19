# Doke Chromium Source Workflow

This document defines the local source and patch workflow for the self-developed `doke_chromium` binary.

## Repository Policy

- Do not commit Chromium source into this repository.
- Keep Chromium source in a local checkout such as `third_party/chromium` or an external path.
- Track only DokeBrowser-owned patch files, scripts, and documentation.
- Keep every native patch tied to a detection target in `docs/DETECTION_BASELINE.md`.

## Local Paths

The helper scripts use these environment variables:

```bash
export DOKE_CHROMIUM_SRC=/absolute/path/to/chromium/src
export DOKE_CHROMIUM_OUT=out/Doke
export DOKE_CHROMIUM_TARGET=chrome
```

If `DOKE_CHROMIUM_SRC` is not set, scripts look for:

```text
third_party/chromium/src
third_party/chromium
```

Validate the current source binding:

```bash
python3 tools/doke_chromium_source_check.py
python3 tools/doke_chromium_fetch_status.py
```

For very large Chromium checkouts, size calculation is best-effort. Use a shorter timeout when only the ready/missing state matters:

```bash
python3 tools/doke_chromium_fetch_status.py --size-timeout 1
```

Bind an existing checkout:

```bash
bash tools/prepare_doke_chromium_source.sh --src /absolute/path/to/chromium/src
export DOKE_CHROMIUM_SRC=/absolute/path/to/chromium/src
```

Install or update local `depot_tools` when network access is available:

```bash
bash tools/install_depot_tools.sh
export PATH="$PWD/third_party/depot_tools:$PATH"
```

Create or sync the ignored local checkout when `depot_tools` is available:

```bash
bash tools/prepare_doke_chromium_source.sh --fetch --nohistory --nohooks
bash tools/resume_doke_chromium_fetch.sh
```

`tools/resume_doke_chromium_fetch.sh` is the preferred recovery wrapper after a network interruption. It repeatedly runs `prepare_doke_chromium_source.sh --sync --nohistory --shallow --nohooks -j 1`, which reduces history transfer, avoids hook downloads during the source bootstrap step, and keeps gclient SCM concurrency low.

If a previous fetch was interrupted, inspect the local state first:

```bash
python3 tools/doke_chromium_fetch_status.py
```

`partial_failed_checkout` means `third_party/chromium/.gclient` exists and `third_party/chromium/src/.git` exists, but required Chromium files such as `BUILD.gn` are still missing. Re-run:

```bash
bash tools/resume_doke_chromium_fetch.sh
```

## Patch Queue

Patch files live in:

```text
patches/chromium/
```

The apply order is defined by:

```text
patches/chromium/series
```

Validate patch queue hygiene before touching a Chromium checkout:

```bash
python3 tools/chromium_patch_queue_check.py
python3 tools/doke_probe_patch_check.py
python3 tools/doke_runtime_patch_check.py
python3 tools/doke_ua_patch_check.py
python3 tools/doke_ua_ch_patch_check.py
python3 tools/doke_ua_ch_override_patch_check.py
python3 tools/doke_webrtc_patch_check.py
python3 tools/doke_screen_patch_check.py
python3 tools/doke_hardware_patch_check.py
python3 tools/doke_rendering_patch_check.py
python3 tools/doke_surfaces_patch_check.py
python3 tools/doke_alignment_patch_check.py
python3 tools/doke_automation_patch_check.py
python3 tools/doke_patch_apply_smoke.py
```

Apply patches:

```bash
bash tools/apply_chromium_patches.sh
```

The script does not download Chromium. It validates the patch queue, then applies local patches to a local source tree.
The first patches in the queue are:

- `0001-doke-probe-contract.patch`: adds the minimum `--doke-probe` JSON handshake in `chrome/app/chrome_main.cc`.
- `0002-doke-runtime-config-load.patch`: reads `--doke-runtime-config`, parses JSON, validates `doke_profile_runtime.v1`, and logs the loaded Profile id. It intentionally does not change fingerprint behavior yet.
- `0003-doke-runtime-ua-lang-switches.patch`: applies `fingerprint.user_agent` and `fingerprint.language` from the runtime config as native Chrome command-line switches when those switches are not already present. It does not claim `native_fingerprint` yet.
- `0004-doke-runtime-ua-client-hints-ingress.patch`: reads structured `fingerprint.ua_client_hints` metadata and validates the fields needed for later native UA-CH overrides. It is an ingress patch only.
- `0005-doke-runtime-ua-client-hints-override.patch`: overrides `components/embedder_support::GetUserAgentMetadata()` from structured runtime UA-CH metadata. This is the first patch intended to affect `navigator.userAgentData` and Sec-CH-UA metadata, but `native_fingerprint` should still wait for a real Chromium build and detection baseline pass.
- `0006-doke-runtime-webrtc-policy.patch`: applies `webrtc.ip_handling_policy` from runtime config as `--force-webrtc-ip-handling-policy` when the command-line switch is absent.
- `0007-doke-runtime-screen-device-switches.patch`: applies `fingerprint.window_size`, `fingerprint.device_scale_factor_arg`, and `fingerprint.touch_events` as Chrome command-line switches when absent.
- `0008-doke-runtime-hardware-switches.patch`: applies `fingerprint.hardware_concurrency_arg` and `fingerprint.device_memory_gb_arg` as Doke-specific startup switches for Blink-side consumption.
- `0009-doke-blink-hardware-overrides.patch`: overrides JS-visible `navigator.hardwareConcurrency` and `navigator.deviceMemory` from the Doke-specific switches.
- `0010-doke-runtime-rendering-noise-ingress.patch`: reads `rendering.canvas` / `rendering.webgl` / `rendering.audio` stable noise seeds from runtime config and forwards them as Doke-specific startup switches for later native rendering patches.
- `0011-doke-runtime-surface-preset-ingress.patch`: reads `surfaces.plugins` / `surfaces.mime_types` / `surfaces.fonts` / `surfaces.client_rects` platform presets and seeds from runtime config and forwards them as Doke-specific startup switches.
- `0012-doke-runtime-alignment-ingress.patch`: reads `alignment.language` / `alignment.timezone` / `alignment.geo` / `alignment.proxy` metadata and forwards them as Doke-specific startup switches for later native GeoIP/timezone/language/proxy alignment patches.
- `0013-doke-runtime-automation-ingress.patch`: reads `automation.webdriver_policy` / `automation.devtools_exposure` / CDP guard fields and forwards them as Doke-specific startup switches for later native automation-detection hardening patches.

## Build Entry

Validate host build prerequisites:

```bash
python3 tools/doke_chromium_build_prereq.py
```

On macOS, Chromium builds require either a full `Xcode.app` selected by `xcode-select`, or Chromium's hermetic Xcode under `build/mac_files/xcode_binaries`. Command Line Tools alone are not enough.

The current DokeBrowser build route uses full Xcode. Hermetic Xcode can be useful on Google-managed Chromium infrastructure, but the required CIPD package may be invisible without the right authentication.

If full Xcode is installed, select and validate it with:

```bash
bash tools/select_chromium_xcode.sh --path /Applications/Xcode.app
python3 tools/doke_chromium_build_prereq.py
```

If `xcodebuild` reports that the license has not been accepted, either open Xcode once or rerun the helper explicitly:

```bash
bash tools/select_chromium_xcode.sh --path /Applications/Xcode.app --accept-license
```

The helper also supports a non-mutating check:

```bash
bash tools/select_chromium_xcode.sh --check-only
```

Hermetic Xcode remains documented for reference, but it is not the preferred local route:

```bash
cd third_party/chromium/src
cipd auth-login
FORCE_MAC_TOOLCHAIN=1 python3 build/mac_toolchain.py
```

Build using:

```bash
bash tools/build_doke_chromium.sh
```

The script validates the source checkout, checks GN and Ninja, creates a default `out/Doke/args.gn` when missing, runs `gn gen` when `build.ninja` is missing, then builds `DOKE_CHROMIUM_TARGET` (`chrome` by default).

The script stores Go build cache under the Chromium output directory by default:

```bash
$DOKE_CHROMIUM_SRC/$DOKE_CHROMIUM_OUT/.gocache
```

Override it when needed:

```bash
DOKE_GO_CACHE=/absolute/path/to/go-cache bash tools/build_doke_chromium.sh
```

Apply the Doke patch queue before the build in the same command:

```bash
DOKE_CHROMIUM_APPLY_PATCHES=1 bash tools/build_doke_chromium.sh
```

The script expects Chromium build tooling to already exist in the source checkout. It does not install `depot_tools` or download Chromium by itself.

## Current macOS Build Note

Status as of 2026-07-19:

- Local Chromium checkout is ready at `third_party/chromium/src`, HEAD `534c1497c1`.
- Doke patch queue `0001` through `0016` has been applied/recognized in the real checkout.
- Full Xcode is selected from `/Applications/Xcode.app/Contents/Developer`; `xcodebuild -version` reports `Xcode 26.6` / `Build version 17F113`.
- Host prerequisite check passes with `chromium_build_prereq_ok platform=mac`.
- Official full Metal/ANGLE Metal release builds are still blocked because the Apple MetalToolchain component is not installed. `xcrun metal --version` reports the missing Metal Toolchain, and `xcodebuild -downloadComponent MetalToolchain` currently fails to fetch a matching Apple MobileAsset catalog for the observed build versions.
- First deliverable validation uses `out/Doke/args.gn` with:

```gn
is_debug = false
is_component_build = false
symbol_level = 1
blink_symbol_level = 0
angle_enable_metal = false
dawn_enable_metal = true
```

This disables ANGLE Metal to avoid the missing Apple MetalToolchain path while keeping Dawn Metal enabled, which is required by macOS GPU/WebGPU linkage. It is the current local deliverable configuration, not the final full Metal/ANGLE Metal release configuration.

Known local generated/restored files needed by this checkout:

- `build/util/LASTCHANGE`
- `build/util/LASTCHANGE.committime`
- `gpu/webgpu/DAWN_VERSION`
- `gpu/webgpu/dawn_commit_hash.h`
- `gpu/config/gpu_lists_version.h`
- `third_party/devtools-frontend/src/node_modules/@rollup/rollup-darwin-arm64`

Latest successful build:

```text
bash tools/build_doke_chromium.sh
Built target 'chrome' in /Users/mac/Documents/浏览器/third_party/chromium/src/out/Doke
```

First deliverable artifacts:

- App bundle: `third_party/chromium/src/out/Doke/Chromium.app`
- Executable: `third_party/chromium/src/out/Doke/Chromium.app/Contents/MacOS/Chromium`

Verified commands:

```bash
python3 tools/doke_probe_check.py /Users/mac/Documents/浏览器/third_party/chromium/src/out/Doke/Chromium.app/Contents/MacOS/Chromium
python3 tools/ipc_cli.py probe-engine doke_chromium /Users/mac/Documents/浏览器/third_party/chromium/src/out/Doke/Chromium.app/Contents/MacOS/Chromium real-doke-final-probe
python3 tools/ipc_cli.py start-doke real-doke-final-start /Users/mac/Documents/浏览器/third_party/chromium/src/out/Doke/Chromium.app/Contents/MacOS/Chromium /private/tmp/doke_real_profile_final about:blank --headless=new --disable-gpu --no-sandbox
python3 tools/doke_runtime_check.py /private/tmp/doke_real_profile_final/Doke/runtime.json
python3 tools/ipc_cli.py stop real-doke-final-start
```

The final `profile.start` reached `running`, wrote `Doke/runtime.json`, and stopped cleanly. Validate `Doke/runtime.json` while the Profile is running; `profile.stop` may remove runtime-only files as part of resource cleanup. The earlier `GetUserAgentMetadata()` blocking DCHECK was fixed by moving UA-CH runtime ingestion to startup-time Doke switches instead of reading `Doke/runtime.json` from `components/embedder_support/user_agent_utils.cc`.

## Binary Handoff

After a successful build, configure DokeBrowser with either:

```bash
export DOKE_CHROMIUM_PATH=/absolute/path/to/doke_chromium
```

or set `engine_config_json.executable` from the UI Doke path selector. Explicit Profile paths must point to an executable file; invalid explicit paths are treated as unavailable and do not fall back to `DOKE_CHROMIUM_PATH` or PATH.

Use the UI "Detect" button or IPC `engine.probe` to verify the path before launching a Profile.
Probe failures return one of `doke_chromium_not_found`, `doke_chromium_path_missing`, `doke_chromium_path_not_file`, or `doke_chromium_path_not_executable`.
Successful probes first run `doke_chromium --doke-probe` with the Agent probe timeout. The current default is 12 seconds to allow real Chromium cold starts on macOS. A native Doke Chromium binary should return JSON:

```json
{
  "probe_protocol": 1,
  "version": "Doke Chromium 0.1.0",
  "capabilities": ["native_fingerprint", "native_proxy"]
}
```

The IPC response maps native JSON `capabilities` to `native_capabilities`. It also returns Profile-declared `capabilities` derived from `engine_config_json.features`, plus `missing_native_capabilities` for declared capabilities that the binary did not report. If `--doke-probe` fails, the agent returns `native_probe_error` and falls back to `--version`, returning `version` or `version_error`.

During `profile.start`, the Agent uses the same native capability check. A Profile-declared native feature disables the corresponding Agent fallback only when the binary reports that capability. Missing or unverifiable native capabilities keep the Agent fallback enabled and are logged.

The Agent also writes a per-Profile runtime config and passes it to Doke Chromium:

```bash
--doke-runtime-config=/path/to/profile/Doke/runtime.json
```

The JSON uses schema `doke_profile_runtime.v1` and includes fingerprint, structured UA-CH metadata, WebRTC policy, screen/device command-line helper values, hardware metrics, rendering noise seeds, plugin/MIME/font/client-rect surface presets, Geo, language/timezone/proxy alignment metadata, automation/CDP exposure policy, native requested/supported/missing capabilities, fallback decisions, and non-secret proxy metadata. Real Chromium patches should read this file instead of parsing DokeBrowser UI state directly.

Validate a generated runtime config:

```bash
python3 tools/doke_runtime_check.py /path/to/profile/Doke/runtime.json \
  --require-supported native_fingerprint \
  --require-native fingerprint \
  --require-fallback geoip
```

Create a fake binary for local contract testing:

```bash
python3 tools/make_fake_doke.py /tmp/doke_chromium \
  --capability native_fingerprint \
  --capability native_proxy \
  --probe-log "native probe ready"
python3 tools/doke_probe_check.py /tmp/doke_chromium --require-capability native_fingerprint
```

The full probe contract is documented in [DOKE_PROBE.md](DOKE_PROBE.md).

Validate a binary directly:

```bash
python3 tools/doke_probe_check.py /absolute/path/to/doke_chromium
python3 tools/doke_probe_check.py /absolute/path/to/doke_chromium --require-capability native_fingerprint
```

Command-line probe:

```bash
python3 tools/ipc_cli.py engine-list
python3 tools/ipc_cli.py probe-engine doke_chromium /absolute/path/to/doke_chromium cli-probe --native-fingerprint
```

Command-line launch through Agent:

```bash
python3 tools/ipc_cli.py start-doke cli-doke /absolute/path/to/doke_chromium /tmp/doke_cli_profile about:blank --native-fingerprint
```

Stop the launched Profile:

```bash
python3 tools/ipc_cli.py stop cli-doke
```
