# Doke Chromium Patch Queue

This directory is the source-controlled patch queue for the self-developed `doke_chromium` binary.

Patch naming convention:

```text
0001-area-short-description.patch
0002-area-short-description.patch
```

Each patch must also be listed in `series` in apply order.

Patch rules:

- Every patch must be based on public Chromium, ungoogled-chromium, CEF, or public documentation.
- Do not include proprietary CloakBrowser binary diffs or unpublished patch content.
- Each patch must include these metadata lines near the top of the patch:
  - `Doke-Purpose: ...`
  - `Doke-Subsystem: ...`
  - `Doke-Detection-Target: ...`
  - `Doke-Rollback: ...`

Initial priority is documented in `docs/CHROMIUM_PATCH_PLAN.md`.

Validate queue hygiene:

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
python3 tools/doke_webdriver_patch_check.py
python3 tools/doke_automation_controlled_patch_check.py
python3 tools/doke_cdp_side_effect_patch_check.py
python3 tools/doke_patch_apply_smoke.py
```

`tools/apply_chromium_patches.sh` runs the same check before touching the Chromium checkout.
