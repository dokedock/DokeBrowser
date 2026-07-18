#!/usr/bin/env python3
"""Validate host prerequisites for building Doke Chromium."""

from __future__ import annotations

import argparse
import json
import platform
import subprocess
import sys
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_src(root: Path) -> Path:
    return root / "third_party" / "chromium" / "src"


def run_text(command: list[str]) -> tuple[int, str, str]:
    result = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    return result.returncode, result.stdout.strip(), result.stderr.strip()


def xcode_select_path() -> str:
    code, stdout, _ = run_text(["xcode-select", "-p"])
    return stdout if code == 0 else ""


def xcodebuild_version() -> tuple[bool, str]:
    code, stdout, stderr = run_text(["xcodebuild", "-version"])
    return code == 0, stdout or stderr


def check_mac(src: Path) -> dict:
    hermetic_root = src / "build" / "mac_files" / "xcode_binaries"
    hermetic_developer = hermetic_root / "Contents" / "Developer"
    hermetic_ok = hermetic_developer.exists()

    selected = xcode_select_path()
    system_ok, xcodebuild_output = xcodebuild_version()
    selected_is_full_xcode = "Xcode.app" in selected
    system_xcode_ok = system_ok and selected_is_full_xcode

    errors: list[str] = []
    warnings: list[str] = []
    if not system_xcode_ok and not hermetic_ok:
        errors.append(
            "macOS Chromium builds require full Xcode or Chromium hermetic Xcode"
        )
        if selected:
            warnings.append(f"active_developer_dir={selected}")
        if xcodebuild_output:
            warnings.append(f"xcodebuild={xcodebuild_output.splitlines()[0]}")
        warnings.append(
            "install Xcode.app and run bash tools/select_chromium_xcode.sh --path /Applications/Xcode.app"
        )
        warnings.append(
            "hermetic Xcode is possible but may require CIPD auth for infra_internal packages"
        )

    return {
        "ok": not errors,
        "platform": "mac",
        "system_xcode_ok": system_xcode_ok,
        "active_developer_dir": selected,
        "xcodebuild_output": xcodebuild_output,
        "hermetic_xcode_ok": hermetic_ok,
        "hermetic_xcode_path": str(hermetic_root),
        "errors": errors,
        "warnings": warnings,
    }


def check(src: Path) -> dict:
    host = platform.system().lower()
    if host == "darwin":
        return check_mac(src)
    return {
        "ok": True,
        "platform": host,
        "errors": [],
        "warnings": [],
    }


def print_text(result: dict) -> None:
    if result["ok"]:
        print(f"chromium_build_prereq_ok platform={result['platform']}")
    else:
        print(f"chromium_build_prereq_failed platform={result['platform']}")
        for error in result["errors"]:
            print(f"- {error}")
    for warning in result["warnings"]:
        print(f"warning: {warning}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--src", type=Path, default=default_src(repo_root()))
    parser.add_argument("--json", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    result = check(args.src.resolve())
    if args.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    else:
        print_text(result)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
