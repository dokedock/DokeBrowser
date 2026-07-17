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
- Each patch must state:
  - purpose
  - touched Chromium subsystem
  - detection target
  - rollback notes

Initial priority is documented in `docs/CHROMIUM_PATCH_PLAN.md`.

