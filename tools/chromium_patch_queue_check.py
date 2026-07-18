#!/usr/bin/env python3
import argparse
import json
import os
import posixpath
import sys
from pathlib import Path


REQUIRED_METADATA = {
    "Doke-Purpose": "purpose",
    "Doke-Subsystem": "touched Chromium subsystem",
    "Doke-Detection-Target": "detection target",
    "Doke-Rollback": "rollback notes",
}

REQUIRED_PATCHES = [
    "0001-doke-probe-contract.patch",
    "0002-doke-runtime-config-load.patch",
    "0003-doke-runtime-ua-lang-switches.patch",
    "0004-doke-runtime-ua-client-hints-ingress.patch",
    "0005-doke-runtime-ua-client-hints-override.patch",
    "0006-doke-runtime-webrtc-policy.patch",
    "0007-doke-runtime-screen-device-switches.patch",
    "0008-doke-runtime-hardware-switches.patch",
    "0009-doke-blink-hardware-overrides.patch",
    "0010-doke-runtime-rendering-noise-ingress.patch",
    "0011-doke-runtime-surface-preset-ingress.patch",
    "0012-doke-runtime-alignment-ingress.patch",
    "0013-doke-runtime-automation-ingress.patch",
    "0014-doke-blink-webdriver-policy.patch",
    "0015-doke-automation-controlled-policy.patch",
    "0016-doke-cdp-side-effect-preview-guard.patch",
]


def repo_root():
    return Path(__file__).resolve().parents[1]


def clean_series_entry(line):
    return line.split("#", 1)[0].strip()


def validate_entry_name(entry):
    errors = []
    if not entry.endswith(".patch"):
        errors.append(f"series entry must end with .patch: {entry}")
    if entry.startswith("/") or entry.startswith("~"):
        errors.append(f"series entry must be relative: {entry}")
    normalized = posixpath.normpath(entry)
    if normalized.startswith("../") or normalized == "..":
        errors.append(f"series entry must not escape patch dir: {entry}")
    if normalized != entry:
        errors.append(f"series entry must be normalized: {entry}")
    return errors


def read_lines(path):
    with path.open("r", encoding="utf-8") as handle:
        return handle.readlines()


def validate_patch_metadata(path):
    errors = []
    present = set()
    for line in read_lines(path)[:120]:
        for key in REQUIRED_METADATA:
            if line.startswith(f"{key}:") and line.split(":", 1)[1].strip():
                present.add(key)

    for key, label in REQUIRED_METADATA.items():
        if key not in present:
            errors.append(f"{path.name} missing {label} metadata ({key}: ...)")
    return errors


def validate_queue(patch_dir, series_file):
    errors = []
    warnings = []
    entries = []
    seen = set()

    if not patch_dir.exists() or not patch_dir.is_dir():
        errors.append(f"patch_dir_missing:{patch_dir}")
        return entries, warnings, errors
    if not series_file.exists() or not series_file.is_file():
        errors.append(f"series_missing:{series_file}")
        return entries, warnings, errors

    for line_no, line in enumerate(read_lines(series_file), start=1):
        entry = clean_series_entry(line)
        if not entry:
            continue
        for error in validate_entry_name(entry):
            errors.append(f"series:{line_no}:{error}")
        if entry in seen:
            errors.append(f"series:{line_no}:duplicate entry: {entry}")
            continue
        seen.add(entry)
        entries.append(entry)

        patch_path = patch_dir / entry
        if not patch_path.exists() or not patch_path.is_file():
            errors.append(f"series:{line_no}:missing patch file: {entry}")
            continue
        for error in validate_patch_metadata(patch_path):
            errors.append(f"series:{line_no}:{error}")

    tracked_patches = set(entries)
    for patch_path in sorted(patch_dir.rglob("*.patch")):
        rel = patch_path.relative_to(patch_dir).as_posix()
        if rel not in tracked_patches:
            errors.append(f"unlisted patch file: {rel}")

    for patch_name in REQUIRED_PATCHES:
        if patch_name not in tracked_patches:
            errors.append(f"required patch missing from series: {patch_name}")

    return entries, warnings, errors


def main():
    root = repo_root()
    default_patch_dir = root / "patches" / "chromium"
    parser = argparse.ArgumentParser(description="Validate the Doke Chromium patch queue.")
    parser.add_argument("--patch-dir", default=str(default_patch_dir), help="Patch queue directory")
    parser.add_argument("--series", default="", help="Series file path; defaults to <patch-dir>/series")
    parser.add_argument("--json", action="store_true", help="Print machine-readable result")
    args = parser.parse_args()

    patch_dir = Path(args.patch_dir).resolve()
    series_file = Path(args.series).resolve() if args.series else patch_dir / "series"
    entries, warnings, errors = validate_queue(patch_dir, series_file)

    result = {
        "ok": not errors,
        "patch_dir": str(patch_dir),
        "series": str(series_file),
        "patch_count": len(entries),
        "patches": entries,
        "warnings": warnings,
        "errors": errors,
    }

    if args.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    else:
        if result["ok"]:
            print(f"patch_queue_ok patches={len(entries)}")
            for warning in warnings:
                print(f"warning: {warning}")
        else:
            print("patch_queue_failed")
            for error in errors:
                print(f"- {error}")
            for warning in warnings:
                print(f"warning: {warning}")

    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
