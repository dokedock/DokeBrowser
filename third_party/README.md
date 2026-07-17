# Third Party Source Workspace

This directory is reserved for local source checkouts used during DokeBrowser development.

Do not commit a full Chromium checkout here. Chromium is far too large for this repository.

Recommended local layout:

```text
third_party/
  chromium/          # local Chromium or ungoogled-chromium checkout, gitignored later if needed
```

The DokeBrowser repository should track only:

- patch files under `patches/chromium/`
- build helper scripts under `tools/`
- documentation under `docs/`

