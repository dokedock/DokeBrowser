#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


REQUIRED_TOKENS = [
    "ua_client_hints",
    "MaybeLogDokeUaClientHints",
    "FindDict(kDokeUaClientHintsKey)",
    "FindList(\"brands\")",
    "FindList(\"fullVersionList\")",
    "FindString(\"platform\")",
    "FindString(\"fullVersion\")",
]


def repo_root():
    return Path(__file__).resolve().parents[1]


def check_patch(path):
    errors = []
    text = path.read_text(encoding="utf-8")
    for token in REQUIRED_TOKENS:
        if token not in text:
            errors.append(f"missing token: {token}")
    if "chrome/app/chrome_main.cc" not in text:
        errors.append("patch must touch chrome/app/chrome_main.cc")
    if "native_fingerprint" in text:
        errors.append("UA-CH ingress patch must not claim native_fingerprint capability")
    if "MaybeLogDokeUaClientHints(*fingerprint);" not in text:
        errors.append("runtime config loader must call UA-CH metadata ingress")
    return errors


def main():
    default_patch = repo_root() / "patches" / "chromium" / "0004-doke-runtime-ua-client-hints-ingress.patch"
    parser = argparse.ArgumentParser(description="Validate the Doke Chromium UA-CH runtime patch contract.")
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
            print(f"doke_ua_ch_patch_ok patch={path}")
        else:
            print("doke_ua_ch_patch_failed")
            for error in errors:
                print(f"- {error}")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
