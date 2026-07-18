#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


REQUIRED_TOKENS = [
    "chrome/app/chrome_main.cc",
    "v8/src/inspector/v8-runtime-agent-impl.cc",
    "v8/src/inspector/v8-console-message.cc",
    "doke-cdp-side-effect-guard",
    "DOKE_CDP_SIDE_EFFECT_GUARD",
    "ApplyDokeInspectorEnvironment",
    "IsDokeCdpSideEffectGuardEnabled",
    "WrapMode::kIdOnly",
    "generatePreview = false",
]


def repo_root():
    return Path(__file__).resolve().parents[1]


def check_patch(path):
    errors = []
    text = path.read_text(encoding="utf-8")
    for token in REQUIRED_TOKENS:
        if token not in text:
            errors.append(f"{path.name} missing token: {token}")
    if "native_" in text:
        errors.append(f"{path.name} must not claim a native capability")
    return errors


def main():
    root = repo_root()
    default_patch = root / "patches" / "chromium" / "0016-doke-cdp-side-effect-preview-guard.patch"
    parser = argparse.ArgumentParser(description="Validate the Doke Chromium CDP side-effect preview guard patch.")
    parser.add_argument("patch", nargs="?", default=str(default_patch))
    parser.add_argument("--json", action="store_true", help="Print machine-readable result")
    args = parser.parse_args()

    path = Path(args.patch)
    errors = []
    if not path.exists() or not path.is_file():
        errors.append(f"patch_missing:{path}")
    else:
        errors.extend(check_patch(path))

    result = {"ok": not errors, "patch": str(path), "errors": errors}
    if args.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    else:
        if result["ok"]:
            print(f"doke_cdp_side_effect_patch_ok patch={path}")
        else:
            print("doke_cdp_side_effect_patch_failed")
            for error in errors:
                print(f"- {error}")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
