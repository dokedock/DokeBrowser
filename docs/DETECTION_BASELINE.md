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

The source-controlled target manifest lives in [DETECTION_SITES.json](DETECTION_SITES.json).

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

## Baseline Tooling

List target sites:

```bash
python3 tools/doke_detection_baseline.py manifest
```

Create a run template:

```bash
python3 tools/doke_detection_baseline.py init-run \
  --engine doke_chromium \
  --profile-id profile-a \
  --profile-seed seed-a \
  --browser-path /absolute/path/to/doke_chromium
```

Validate and summarize a filled run file:

```bash
python3 tools/doke_detection_baseline.py validate .tmp/detection_baselines/<run_id>.json
python3 tools/doke_detection_baseline.py summary .tmp/detection_baselines/<run_id>.json
```

Prepare artifact folders and per-phase templates:

```bash
python3 tools/doke_detection_baseline.py prepare-artifacts \
  .tmp/detection_baselines/<run_id>.json
```

This creates:

```text
.tmp/detection_artifacts/<run_id>/<site_id>/<phase>/
  README.md
  notes.md
  signals.json
```

Print the ordered visit plan:

```bash
python3 tools/doke_detection_baseline.py visit-plan \
  .tmp/detection_baselines/<run_id>.json \
  --markdown
```

Print the local IPC command plan:

```bash
python3 tools/doke_detection_baseline.py launch-plan \
  .tmp/detection_baselines/<run_id>.json
```

For `doke_chromium`, the launch plan assumes the agent is already running and emits `engine-list`, `probe-engine`, `start-doke`, and `stop` commands. Use it after a real Doke Chromium binary exists; until then it is a dry execution guide.
When fallback CDP is enabled, `profile.status` includes `debug_port`; use that port as the capture endpoint for `http://127.0.0.1:<debug_port>/json/version`.

Run the prepared capture workflow:

```bash
python3 tools/doke_detection_baseline.py run-capture \
  .tmp/detection_baselines/<run_id>.json \
  --keep-going \
  --report-output .tmp/detection_baselines/<run_id>.md
```

`run-capture` expects `./build/src/agent/dokebrowser_agent` to already be running. It stops any existing matching Profile, starts the target URL, waits for `profile.status.debug_port`, captures the phase through CDP, syncs artifacts back into the run file, and optionally renders a report. Use `--dry-run` to verify the visit plan without connecting to the Agent.

Capture a lightweight CDP snapshot into a prepared artifact directory:

```bash
python3 tools/doke_cdp_capture.py \
  --debug-port <debug_port> \
  --site-id browserscan_overview \
  --url https://www.browserscan.net/ \
  --artifact-dir .tmp/detection_artifacts/<run_id>/browserscan_overview/clean_launch
```

The capture writes `snapshot.json` with navigator/screen/UA-CH/timezone basics, site-specific extractor hints, and `screenshot.png` for visual evidence. Site-specific scores still need to be copied into `signals.json` when the page does not expose a machine-readable export.

Merge collected artifact data back into the run file:

```bash
python3 tools/doke_detection_baseline.py sync-artifacts \
  .tmp/detection_baselines/<run_id>.json
```

`sync-artifacts` reads each phase `signals.json`, adds existing `snapshot.json` / `screenshot.png` / `export.json` paths, and stores a compact `cdp_snapshot` object inside the phase `signals`.

Render a Markdown report:

```bash
python3 tools/doke_detection_baseline.py report \
  .tmp/detection_baselines/<run_id>.json \
  --output .tmp/detection_baselines/<run_id>.md
```

Compare two runs:

```bash
python3 tools/doke_detection_baseline.py compare \
  .tmp/detection_baselines/<system_run_id>.json \
  .tmp/detection_baselines/<doke_run_id>.json \
  --output .tmp/detection_baselines/system-vs-doke.md
```

`compare` aligns rows by `site_id` and `phase`, then reports differences in result/summary/signals and compact CDP snapshot fields. Use this for `system_chrome` vs `doke_chromium`, or before/after a native patch.

Create and compare a paired system/Doke baseline:

```bash
python3 tools/doke_detection_baseline.py init-pair \
  --profile-base profile-a \
  --profile-seed seed-a \
  --browser-path /absolute/path/to/doke_chromium

python3 tools/doke_detection_baseline.py compare-pair \
  .tmp/detection_baselines/<run_prefix>-pair.json \
  --sync \
  --output .tmp/detection_baselines/<run_prefix>-compare.md
```

The run file has two phases per site:

- `clean_launch`: first run after a clean profile launch.
- `restart`: second run after closing and reopening the same Profile.

Each phase starts as `pending`. Fill `result`, `summary`, `signals`, `artifacts`, and `checked_at_utc` after collecting screenshots or exported JSON.
`prepare-artifacts` also writes `artifact_dir` and seeds each phase with README/notes/signals artifact paths.

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
  "schema": "doke_detection_baseline.v1",
  "created_at_utc": "YYYY-MM-DDTHH:MM:SSZ",
  "run_id": "...",
  "engine": "doke_chromium",
  "profile_id": "...",
  "sites": [
    {
      "site_id": "creepjs",
      "runs": [
        {
          "phase": "clean_launch",
          "result": "pass|partial|fail|blocked|pending",
          "summary": "",
          "signals": {},
          "artifacts": []
        }
      ]
    }
  ]
}
```
