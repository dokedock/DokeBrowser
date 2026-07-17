# DokeBrowser Detection Baseline

This document tracks manual and semi-automated fingerprint checks for `system_chrome` and `doke_chromium`.

## Engines

- `system_chrome`: development fallback using local Chrome/Chromium, extension injection, and CDP fallback.
- `doke_chromium`: target self-developed Chromium binary, resolved from `engine_config_json.executable`, `engine_config_json.binary_path`, `DOKE_CHROMIUM_PATH`, or PATH. If a Profile explicitly sets an executable path, that path must exist and be executable; invalid explicit paths do not fall back to global resolution.

## Test Matrix

| Site | Purpose | system_chrome | doke_chromium | Notes |
| --- | --- | --- | --- | --- |
| BrowserScan | Browser and fingerprint consistency | TBD | TBD | Check UA, UA-CH, WebRTC, canvas, WebGL, fonts, timezone, language, location. |
| CreepJS | High-signal fingerprint drift | TBD | TBD | Record trust score, lies, trash, fingerprint stability across restarts. |
| FingerprintJS demo | Visitor stability | TBD | TBD | Same Profile should keep visitor identity stable unless seed changes. |
| deviceandbrowserinfo | UA-CH and browser metadata | TBD | TBD | Verify `navigator.userAgentData`, platform, brands, mobile flags. |
| bot.incolumitas | Automation and bot signals | TBD | TBD | Track webdriver, CDP traces, timing and interaction signals. |

## Baseline Procedure

1. Create two Profiles with identical proxy, timezone, language, UA, platform, resolution, and seed.
2. Set one Profile to `system_chrome`; set the other to `doke_chromium`.
3. For `doke_chromium`, configure:

```json
{
  "executable": "/absolute/path/to/doke_chromium",
  "extra_args": [],
  "features": {
    "native_fingerprint": false,
    "native_proxy": false,
    "native_geoip": false,
    "native_humanize": false
  }
}
```

4. Run each test site twice per Profile:
   - first run after clean launch
   - second run after full browser restart
5. Record screenshots or exported JSON where the site provides it.
6. Only mark a feature as native after the relevant detection row improves without extension/CDP fallback.

## Pass Criteria

- UA and UA-CH are internally consistent.
- Language, timezone, geolocation, and proxy country do not contradict each other.
- Canvas, WebGL, Audio, screen, hardware, and plugins are stable per Profile seed.
- WebRTC does not leak non-proxy host or public IP candidates.
- `navigator.webdriver` and obvious CDP automation traces are absent.
- Restarting the same Profile keeps identity stable; changing seed changes the expected high-entropy surfaces.

## Result Format

```json
{
  "date": "YYYY-MM-DD",
  "engine": "doke_chromium",
  "profile_id": "...",
  "site": "CreepJS",
  "result": "pass|partial|fail",
  "summary": "",
  "artifacts": []
}
```
