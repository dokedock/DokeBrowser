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

1. UA and UA-CH
   - Ensure `navigator.userAgent`, request headers, `navigator.userAgentData`, brands, platform, mobile, and high entropy values agree.
   - Regression: BrowserScan, deviceandbrowserinfo.

2. WebRTC
   - Prevent non-proxy host and public IP leaks.
   - Preserve enough API behavior to avoid simple "disabled WebRTC" detection.
   - Regression: BrowserScan, CreepJS.

3. Canvas, WebGL, and Audio
   - Stable per Profile seed.
   - Low-noise, deterministic perturbation.
   - Regression: CreepJS, FingerprintJS demo.

4. Screen, hardware, and device metrics
   - Align screen dimensions, DPR, touch, memory, hardware concurrency, platform.
   - Regression: BrowserScan, deviceandbrowserinfo.

5. Plugins, MIME types, fonts, and client rects
   - Build realistic sets by OS/platform preset.
   - Keep stable per Profile seed.
   - Regression: CreepJS.

6. CDP and automation detection
   - Reduce debugger and automation side effects.
   - Avoid `navigator.webdriver`.
   - Regression: bot.incolumitas, CreepJS.

7. GeoIP and proxy alignment
   - Native location/timezone/language alignment from Profile data or proxy metadata.
   - Regression: BrowserScan, location test pages.

## Feature Promotion Rules

A feature can be flipped to native in `engine_config_json.features` only when:

- The corresponding native patch is implemented.
- The binary reports the matching capability through `--doke-probe`.
- A detection row exists in `docs/DETECTION_BASELINE.md`.
- The native path is equal or better than extension/CDP fallback.
- The same Profile remains stable across browser restarts.

## Source Policy

- Use public Chromium, ungoogled-chromium, CEF, and public documentation as implementation references.
- Do not copy, reverse, patch, or ship proprietary CloakBrowser binaries or unpublished patches.
- Keep every patch tied to a detection target and a rollback strategy.
