# Doke Chromium Patch Plan

This document defines the first native patch targets for the self-developed `doke_chromium` binary.

## Runtime Contract

DokeBrowser passes engine-specific configuration through `engine_config_json`.
The native binary must expose the `--doke-probe` contract documented in [DOKE_PROBE.md](DOKE_PROBE.md) before a feature is promoted to native.

Supported now:

```json
{
  "executable": "/absolute/path/to/doke_chromium",
  "binary_path": "/absolute/path/to/doke_chromium",
  "extra_args": [
    "--doke-example-flag=value"
  ],
  "features": {
    "native_fingerprint": false,
    "native_proxy": false,
    "native_geoip": false,
    "native_humanize": false
  }
}
```

- `executable` / `binary_path`: per-Profile binary path.
- `extra_args`: appended before the final start URL.
- `features.native_fingerprint`: when true, Agent suppresses fingerprint extension/CDP fallback surfaces.
- `features.native_geoip`: when true, Agent suppresses geolocation fallback injection.
- `features.native_proxy`: reserved for native proxy/auth handling.
- `features.native_humanize`: reserved for native interaction/timing simulation.

## Patch Priority

0. Probe contract
   - Add `--doke-probe` at Chrome startup before full Chromium initialization.
   - Report `probe_protocol`, `version`, and current native `capabilities`.
   - Initial patch reports no native capabilities until each feature patch is implemented.
   - Regression: `tools/doke_probe_check.py`, `engine.probe`, smoke fake Doke path.

0.5. Runtime config ingress
   - Add `--doke-runtime-config` parsing at Chrome startup.
   - Read `Doke/runtime.json`, validate `doke_profile_runtime.v1`, and keep startup non-fatal while native behaviors are still being implemented.
   - Do not claim any native capability from this patch alone.
   - Regression: `tools/doke_runtime_check.py`, `tools/doke_runtime_patch_check.py`, `profile.start`.

1. UA and UA-CH
   - Ensure `navigator.userAgent`, request headers, `navigator.userAgentData`, brands, platform, mobile, and high entropy values agree.
   - Current first step: `0003-doke-runtime-ua-lang-switches.patch` applies runtime `fingerprint.user_agent` and `fingerprint.language` as Chrome command-line switches when absent.
   - Current second step: `Doke/runtime.json` now includes `fingerprint.ua_client_hints`; `0004-doke-runtime-ua-client-hints-ingress.patch` reads and validates this structured metadata.
   - Current third step: `0005-doke-runtime-ua-client-hints-override.patch` overrides `GetUserAgentMetadata()` from runtime UA-CH metadata.
   - Remaining before capability promotion: build real Chromium, verify network Sec-CH-UA and JavaScript-visible `navigator.userAgentData`, then pass the detection baseline.
   - Regression: BrowserScan, deviceandbrowserinfo.

2. WebRTC
   - Prevent non-proxy host and public IP leaks.
   - Preserve enough API behavior to avoid simple "disabled WebRTC" detection.
   - Current first step: `Doke/runtime.json` includes `webrtc.ip_handling_policy`; `0006-doke-runtime-webrtc-policy.patch` applies it as `--force-webrtc-ip-handling-policy` when absent.
   - Remaining before capability promotion: verify ICE candidates on BrowserScan/CreepJS with real Doke Chromium and proxy scenarios.
   - Regression: BrowserScan, CreepJS.

3. Canvas, WebGL, and Audio
   - Stable per Profile seed.
   - Low-noise, deterministic perturbation.
   - Current first step: `Doke/runtime.json` now includes `rendering.canvas` / `rendering.webgl` / `rendering.audio` stable noise seeds; `0010-doke-runtime-rendering-noise-ingress.patch` forwards those seeds as Doke-specific startup switches.
   - Remaining before capability promotion: wire the switches into Canvas/WebGL/Audio code paths, build real Chromium, compare CreepJS/FingerprintJS stability across restarts, then tune noise strength.
   - Regression: CreepJS, FingerprintJS demo.

4. Screen, hardware, and device metrics
   - Align screen dimensions, DPR, touch, memory, hardware concurrency, platform.
   - Current first step: `Doke/runtime.json` includes Chrome-ready `window_size`, `device_scale_factor_arg`, and `touch_events`; `0007-doke-runtime-screen-device-switches.patch` applies them as startup switches when absent.
   - Current second step: `Doke/runtime.json` includes `hardware_concurrency_arg` and `device_memory_gb_arg`; `0008-doke-runtime-hardware-switches.patch` forwards them as Doke-specific startup switches.
   - Current third step: `0009-doke-blink-hardware-overrides.patch` wires those switches into Blink's JS-visible `navigator.hardwareConcurrency` and `navigator.deviceMemory` surfaces.
   - Remaining before capability promotion: real Chromium build, JS-visible screen metric verification, hardware metric verification, and detection baseline pass.
   - Regression: BrowserScan, deviceandbrowserinfo.

5. Plugins, MIME types, fonts, and client rects
   - Build realistic sets by OS/platform preset.
   - Keep stable per Profile seed.
   - Current first step: `Doke/runtime.json` now includes `surfaces.plugins` / `surfaces.mime_types` / `surfaces.fonts` / `surfaces.client_rects` platform presets and seeds; `0011-doke-runtime-surface-preset-ingress.patch` forwards those values as Doke-specific startup switches.
   - Remaining before capability promotion: wire plugin/MIME/font/client-rect surfaces into Blink and plugin code paths, build real Chromium, then verify CreepJS consistency and plausibility per platform preset.
   - Regression: CreepJS.

6. CDP and automation detection
   - Reduce debugger and automation side effects.
   - Avoid `navigator.webdriver`.
   - Current first step: `Doke/runtime.json` now includes `automation.webdriver_policy` / `automation.devtools_exposure` / CDP guard metadata; `0013-doke-runtime-automation-ingress.patch` forwards those values as Doke-specific startup switches.
   - Current second step: `0014-doke-blink-webdriver-policy.patch` wires `--doke-webdriver-policy=hide` into Blink's `Navigator::webdriver()` path.
   - Current third step: `0015-doke-automation-controlled-policy.patch` suppresses Blink `AutomationControlled` runtime feature when Doke webdriver policy is `hide`, including the `--remote-debugging-port=0` path.
   - Current fourth step: `0016-doke-cdp-side-effect-preview-guard.patch` maps `--doke-cdp-side-effect-guard=true` to `DOKE_CDP_SIDE_EFFECT_GUARD=1` and makes V8 inspector downgrade Runtime preview wrapping / console event previews to id-only, avoiding getter-triggered preview side effects.
   - Remaining before capability promotion: build real Chromium, then verify bot.incolumitas, CreepJS, and deviceandbrowserinfo automation/CDP rows.
   - Regression: bot.incolumitas, CreepJS.

7. GeoIP and proxy alignment
   - Native location/timezone/language alignment from Profile data or proxy metadata.
   - Current first step: `Doke/runtime.json` now includes `alignment.language` / `alignment.timezone` / `alignment.geo` / `alignment.proxy`; `0012-doke-runtime-alignment-ingress.patch` forwards those values as Doke-specific startup switches.
   - Remaining before capability promotion: wire timezone/language/geolocation/proxy alignment into Chromium network, permission, and JS surfaces, build real Chromium, then verify BrowserScan timezone/location/proxy consistency.
   - Regression: BrowserScan, location test pages.

## Feature Promotion Rules

A feature can be flipped to native in `engine_config_json.features` only when:

- The corresponding native patch is implemented.
- The binary reports the matching capability through `--doke-probe`.
- A detection row exists in `docs/DETECTION_BASELINE.md` and a validated `doke_detection_baseline.v1` run file records the result.
- The native path is equal or better than extension/CDP fallback.
- The same Profile remains stable across browser restarts.

## Source Policy

- Use public Chromium, ungoogled-chromium, CEF, and public documentation as implementation references.
- Do not copy, reverse, patch, or ship proprietary CloakBrowser binaries or unpublished patches.
- Keep every patch tied to a detection target and a rollback strategy.
- Before applying patches, run `python3 tools/chromium_patch_queue_check.py` or use `bash tools/apply_chromium_patches.sh`, which runs the same queue check automatically.
- Every patch listed in `patches/chromium/series` must include `Doke-Purpose`, `Doke-Subsystem`, `Doke-Detection-Target`, and `Doke-Rollback` metadata near the top.
