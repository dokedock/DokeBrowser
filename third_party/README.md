# Third Party Source Workspace

This directory is reserved for local source checkouts used during DokeBrowser development.

Do not commit a full Chromium checkout here. Chromium is far too large for this repository.

Recommended local layout:

```text
third_party/
  depot_tools/       # ignored Chromium depot_tools checkout
  chromium/          # ignored local Chromium workspace
    .gclient
    src/             # DOKE_CHROMIUM_SRC can point here
```

The DokeBrowser repository should track only:

- patch files under `patches/chromium/`
- build helper scripts under `tools/`
- documentation under `docs/`

Useful commands:

```bash
bash tools/install_depot_tools.sh
bash tools/prepare_doke_chromium_source.sh --src /absolute/path/to/chromium/src
bash tools/prepare_doke_chromium_source.sh --fetch --nohistory --nohooks
bash tools/resume_doke_chromium_fetch.sh
python3 tools/doke_chromium_source_check.py
python3 tools/doke_chromium_fetch_status.py
python3 tools/doke_chromium_build_prereq.py
bash tools/select_chromium_xcode.sh --check-only
```

If a Chromium fetch is interrupted by network errors, `tools/doke_chromium_fetch_status.py` reports `partial_failed_checkout` and prints the recommended resume command.

On macOS, Command Line Tools are not enough for a Chromium build. The current DokeBrowser route is full Xcode:

```bash
bash tools/select_chromium_xcode.sh --path /Applications/Xcode.app
bash tools/build_doke_chromium.sh
```

Chromium's hermetic Xcode route is not preferred locally because the CIPD package may require authentication and may be invisible without the right account.
