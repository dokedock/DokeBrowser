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

or set `engine_config_json.executable` from the UI Doke path selector.

Use the UI "Detect" button or IPC `engine.probe` to verify the path before launching a Profile.

