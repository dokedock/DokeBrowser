#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import sys
from pathlib import Path


def repo_root():
    return Path(__file__).resolve().parents[1]


def resolve_source(root):
    env_path = os.environ.get("DOKE_CHROMIUM_SRC", "").strip()
    if env_path:
        return Path(env_path).expanduser().resolve(), "DOKE_CHROMIUM_SRC"

    for candidate in (root / "third_party" / "chromium" / "src", root / "third_party" / "chromium"):
        if candidate.exists():
            return candidate.resolve(), str(candidate.relative_to(root))
    return None, ""


def command_path(name, root=None):
    if root:
        local = root / "third_party" / "depot_tools" / name
        if local.exists() and os.access(local, os.X_OK):
            return str(local)
    path = shutil.which(name)
    return path or ""


def local_gn_paths(src):
    candidates = [
        src / "buildtools" / "mac" / "gn",
        src / "buildtools" / "linux64" / "gn",
        src / "buildtools" / "win" / "gn.exe",
    ]
    return [str(path) for path in candidates if path.exists() and os.access(path, os.X_OK)]


def validate_source(src, out_dir):
    errors = []
    warnings = []

    if src is None:
        errors.append("DOKE_CHROMIUM_SRC is not set and no local Chromium checkout was found")
        return warnings, errors

    if not src.exists() or not src.is_dir():
        errors.append(f"source directory does not exist: {src}")
        return warnings, errors

    required = [
        "BUILD.gn",
        "chrome/BUILD.gn",
        "build/config/BUILDCONFIG.gn",
        "content/public/browser/browser_context.h",
    ]
    for rel in required:
        if not (src / rel).exists():
            errors.append(f"source does not look like Chromium src; missing {rel}")

    if not (src / ".git").exists() and not (src.parent / ".gclient").exists():
        warnings.append("source is not obviously a Chromium git checkout or gclient workspace")

    out_path = src / out_dir
    if not (out_path / "args.gn").exists():
        warnings.append(f"{out_dir}/args.gn has not been generated yet")
    if not (out_path / "build.ninja").exists():
        warnings.append(f"{out_dir}/build.ninja has not been generated yet")

    return warnings, errors


def build_tool_status(src, root):
    gn_paths = local_gn_paths(src) if src else []
    return {
        "fetch": command_path("fetch", root),
        "gclient": command_path("gclient", root),
        "gn": command_path("gn", root) or (gn_paths[0] if gn_paths else ""),
        "autoninja": command_path("autoninja", root),
        "ninja": command_path("ninja", root),
    }


def main():
    root = repo_root()
    parser = argparse.ArgumentParser(description="Validate the local Doke Chromium source checkout.")
    parser.add_argument("--src", default="", help="Explicit Chromium src path")
    parser.add_argument("--out-dir", default=os.environ.get("DOKE_CHROMIUM_OUT", "out/Doke"))
    parser.add_argument("--require-build-tools", action="store_true", help="Fail when GN or Ninja is unavailable")
    parser.add_argument("--print-src", action="store_true", help="Print only the resolved src path")
    parser.add_argument("--json", action="store_true", help="Print machine-readable result")
    args = parser.parse_args()

    if args.src:
        src = Path(args.src).expanduser().resolve()
        source_from = "--src"
    else:
        src, source_from = resolve_source(root)

    warnings, errors = validate_source(src, args.out_dir)
    tools = build_tool_status(src, root)

    if args.require_build_tools:
        if not tools["gn"]:
            errors.append("GN is unavailable; install depot_tools or generate Chromium buildtools")
        if not (tools["autoninja"] or tools["ninja"]):
            errors.append("autoninja/ninja is unavailable; install depot_tools or ninja")

    result = {
        "ok": not errors,
        "source": str(src) if src else "",
        "source_from": source_from,
        "out_dir": args.out_dir,
        "tools": tools,
        "warnings": warnings,
        "errors": errors,
    }

    if args.print_src:
        if result["ok"] and result["source"]:
            print(result["source"])
            return 0
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    else:
        if result["ok"]:
            print(f"chromium_source_ok src={result['source']}")
            for warning in warnings:
                print(f"warning: {warning}")
        else:
            print("chromium_source_failed")
            for error in errors:
                print(f"- {error}")
            for warning in warnings:
                print(f"warning: {warning}")

    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
