#!/usr/bin/env python3
"""Report local Doke Chromium checkout/download status."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


REQUIRED_SRC_FILES = [
    "BUILD.gn",
    "chrome/BUILD.gn",
    "build/config/BUILDCONFIG.gn",
    "content/public/browser/browser_context.h",
]


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def human_size(path: Path, timeout_s: float) -> str:
    if not path.exists():
        return "missing"
    try:
        result = subprocess.run(
            ["du", "-sh", str(path)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=timeout_s,
        )
    except subprocess.TimeoutExpired:
        return "unknown_timeout"
    if result.returncode != 0:
        return "unknown"
    return result.stdout.split()[0]


def git_remote(path: Path) -> str:
    if not (path / ".git").exists():
        return ""
    result = subprocess.run(
        ["git", "-C", str(path), "remote", "get-url", "origin"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else ""


def git_head(path: Path) -> str:
    if not (path / ".git").exists():
        return ""
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "--short", "HEAD"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else ""


def status(root: Path, size_timeout_s: float) -> dict:
    depot_tools = root / "third_party" / "depot_tools"
    checkout = root / "third_party" / "chromium"
    src = checkout / "src"
    bad_scm = checkout / "_bad_scm"
    missing_required = [
        relative for relative in REQUIRED_SRC_FILES if not (src / relative).exists()
    ]
    bad_entries = sorted(path.name for path in bad_scm.iterdir()) if bad_scm.exists() else []

    if missing_required and src.exists() and (src / ".git").exists():
        state = "partial_failed_checkout"
    elif missing_required and checkout.exists():
        state = "workspace_without_src"
    elif missing_required:
        state = "missing"
    else:
        state = "ready"

    return {
        "state": state,
        "checkout": str(checkout),
        "src": str(src),
        "depot_tools": str(depot_tools),
        "depot_tools_ready": (depot_tools / "gclient").exists(),
        "workspace_ready": (checkout / ".gclient").exists(),
        "src_exists": src.exists(),
        "src_git": (src / ".git").exists(),
        "src_remote": git_remote(src),
        "src_head": git_head(src),
        "missing_required": missing_required,
        "bad_scm_entries": bad_entries,
        "sizes": {
            "depot_tools": human_size(depot_tools, size_timeout_s),
            "checkout": human_size(checkout, size_timeout_s),
            "src": human_size(src, size_timeout_s),
            "bad_scm": human_size(bad_scm, size_timeout_s),
        },
        "recommended_resume": (
            "bash tools/prepare_doke_chromium_source.sh "
            "--sync --nohistory --shallow --nohooks -j 1"
        ),
    }


def print_text(result: dict) -> None:
    print(f"doke_chromium_fetch_state={result['state']}")
    print(f"checkout={result['checkout']}")
    print(f"src={result['src']}")
    print(f"depot_tools_ready={str(result['depot_tools_ready']).lower()}")
    print(f"workspace_ready={str(result['workspace_ready']).lower()}")
    print(f"src_git={str(result['src_git']).lower()}")
    if result["src_remote"]:
        print(f"src_remote={result['src_remote']}")
    if result["src_head"]:
        print(f"src_head={result['src_head']}")
    if result["missing_required"]:
        print("missing_required=" + ",".join(result["missing_required"]))
    if result["bad_scm_entries"]:
        print("bad_scm_entries=" + ",".join(result["bad_scm_entries"]))
    for key, value in result["sizes"].items():
        print(f"size_{key}={value}")
    if result["state"] != "ready":
        print(f"recommended_resume={result['recommended_resume']}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="Print JSON output.")
    parser.add_argument(
        "--size-timeout",
        type=float,
        default=5.0,
        help="Seconds to wait for each du size calculation before reporting unknown_timeout.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    result = status(repo_root(), max(0.1, args.size_timeout))
    if args.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    else:
        print_text(result)
    return 0 if result["state"] == "ready" else 1


if __name__ == "__main__":
    raise SystemExit(main())
