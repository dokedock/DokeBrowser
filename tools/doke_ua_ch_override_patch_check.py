#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


REQUIRED_TOKENS = [
    "components/embedder_support/user_agent_utils.cc",
    "GetDokeUserAgentMetadataOverride",
    "blink::UserAgentMetadata",
    "ReadDokeBrandList",
    "brand_version_list",
    "brand_full_version_list",
    "full_version",
    "platform_version",
    "form_factors",
    "GetUserAgentMetadata(bool only_low_entropy_ch)",
    "return doke_metadata.value();",
]


def repo_root():
    return Path(__file__).resolve().parents[1]


def check_patch(path):
    errors = []
    text = path.read_text(encoding="utf-8")
    for token in REQUIRED_TOKENS:
        if token not in text:
            errors.append(f"missing token: {token}")
    if "native_fingerprint" in text:
        errors.append("UA-CH override patch must not claim native_fingerprint capability")
    if "base::JSONReader::Read" not in text or "base::ReadFileToString" not in text:
        errors.append("patch must read and parse Doke/runtime.json")
    if "only_low_entropy_ch" not in text:
        errors.append("patch must preserve low-entropy-only behavior")
    if "#if BUILDFLAG(IS_WIN)\n+constexpr char kDokeRuntimeConfigSwitch" in text:
        errors.append("Doke UA-CH helpers must not be inserted inside the Windows-only block")
    return errors


def main():
    default_patch = repo_root() / "patches" / "chromium" / "0005-doke-runtime-ua-client-hints-override.patch"
    parser = argparse.ArgumentParser(description="Validate the Doke Chromium UA-CH metadata override patch.")
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
            print(f"doke_ua_ch_override_patch_ok patch={path}")
        else:
            print("doke_ua_ch_override_patch_failed")
            for error in errors:
                print(f"- {error}")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
