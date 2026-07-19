#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path


REQUIRED_TOKENS = [
    "doke-probe",
    "probe_protocol",
    "version",
    "capabilities",
    "MaybeRunDokeProbe",
]


def repo_root():
    return Path(__file__).resolve().parents[1]


def decode_c_string(fragment):
    return bytes(fragment, "utf-8").decode("unicode_escape")


def extract_probe_json_literal(text):
    collecting = False
    parts = []
    for line in text.splitlines():
        if "std::fprintf(stdout" in line:
            collecting = True
            continue
        if not collecting:
            continue
        if "stdout" in line:
            break
        match = re.search(r'"(.*)"', line)
        if match:
            parts.append(decode_c_string(match.group(1)))
    return "".join(parts).strip()


def check_patch(path):
    errors = []
    text = path.read_text(encoding="utf-8")

    for token in REQUIRED_TOKENS:
        if token not in text:
            errors.append(f"missing token: {token}")

    if "chrome/app/chrome_main.cc" not in text:
        errors.append("patch must touch chrome/app/chrome_main.cc")
    if "std::fprintf(stdout" not in text:
        errors.append("patch should emit probe JSON without starting Chromium")
    if "return doke_probe_result;" not in text:
        errors.append("patch should return immediately after probe output")

    literal = extract_probe_json_literal(text)
    if not literal:
        errors.append("probe JSON literal was not found")
    else:
        try:
            obj = json.loads(literal)
        except json.JSONDecodeError as exc:
            errors.append(f"probe JSON literal is invalid: {exc}")
        else:
            if obj.get("probe_protocol") != 1:
                errors.append("probe_protocol must be 1")
            if not isinstance(obj.get("version"), str) or not obj["version"].strip():
                errors.append("version must be a non-empty string")
            if obj.get("capabilities") != []:
                errors.append("first probe patch should start with empty capabilities")

    return errors


def main():
    default_patch = repo_root() / "patches" / "chromium" / "0001-doke-probe-contract.patch"
    parser = argparse.ArgumentParser(description="Validate the Doke Chromium --doke-probe patch contract.")
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
            print(f"doke_probe_patch_ok patch={path}")
        else:
            print("doke_probe_patch_failed")
            for error in errors:
                print(f"- {error}")

    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
