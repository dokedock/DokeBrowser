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

## Patch Queue

Patch files live in:

```text
patches/chromium/
```

The apply order is defined by:

```text
patches/chromium/series
```

Apply patches:

```bash
bash tools/apply_chromium_patches.sh
```

The script does not download Chromium. It only applies local patches to a local source tree.

## Build Entry

Build using:

```bash
bash tools/build_doke_chromium.sh
```

The script expects Chromium build tooling to already exist in the source checkout.

## Binary Handoff

After a successful build, configure DokeBrowser with either:

```bash
export DOKE_CHROMIUM_PATH=/absolute/path/to/doke_chromium
```

or set `engine_config_json.executable` from the UI Doke path selector. Explicit Profile paths must point to an executable file; invalid explicit paths are treated as unavailable and do not fall back to `DOKE_CHROMIUM_PATH` or PATH.

Use the UI "Detect" button or IPC `engine.probe` to verify the path before launching a Profile.
Probe failures return one of `doke_chromium_not_found`, `doke_chromium_path_missing`, `doke_chromium_path_not_file`, or `doke_chromium_path_not_executable`.
Successful probes first run `doke_chromium --doke-probe` with a short timeout. A native Doke Chromium binary should return JSON:

```json
{
  "probe_protocol": 1,
  "version": "Doke Chromium 0.1.0",
  "capabilities": ["native_fingerprint", "native_proxy"]
}
```

The IPC response maps native JSON `capabilities` to `native_capabilities`. It also returns Profile-declared `capabilities` derived from `engine_config_json.features`. If `--doke-probe` fails, the agent returns `native_probe_error` and falls back to `--version`, returning `version` or `version_error`.

Command-line probe:

```bash
python3 tools/ipc_cli.py engine-list
python3 tools/ipc_cli.py probe-engine doke_chromium /absolute/path/to/doke_chromium
```

Command-line launch through Agent:

```bash
python3 tools/ipc_cli.py start-doke cli-doke /absolute/path/to/doke_chromium /tmp/doke_cli_profile about:blank
```

Stop the launched Profile:

```bash
python3 tools/ipc_cli.py stop cli-doke
```
