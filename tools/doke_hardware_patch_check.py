#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


REQUIRED_RUNTIME_TOKENS = [
    "doke-hardware-concurrency",
    "doke-device-memory-gb",
    "hardware_concurrency_arg",
    "device_memory_gb_arg",
    "AppendStringSwitchIfAbsent",
]

REQUIRED_BLINK_TOKENS = [
    "third_party/blink/renderer/core/execution_context/navigator_base.cc",
    "third_party/blink/renderer/core/frame/navigator_device_memory.cc",
    "GetDokeHardwareConcurrencyOverride",
    "GetDokeDeviceMemoryOverride",
    "base::StringToUint",
    "base::StringToDouble",
    "NavigatorBase::hardwareConcurrency",
    "NavigatorDeviceMemory::deviceMemory",
]


def repo_root():
    return Path(__file__).resolve().parents[1]


def check_patch(path, required_tokens):
    errors = []
    text = path.read_text(encoding="utf-8")
    for token in required_tokens:
        if token not in text:
            errors.append(f"{path.name} missing token: {token}")
    if "native_" in text:
        errors.append(f"{path.name} must not claim a native capability")
    return errors


def main():
    root = repo_root()
    parser = argparse.ArgumentParser(description="Validate Doke Chromium hardware override patches.")
    parser.add_argument(
        "--runtime-patch",
        default=str(root / "patches" / "chromium" / "0008-doke-runtime-hardware-switches.patch"),
    )
    parser.add_argument(
        "--blink-patch",
        default=str(root / "patches" / "chromium" / "0009-doke-blink-hardware-overrides.patch"),
    )
    parser.add_argument("--json", action="store_true", help="Print machine-readable result")
    args = parser.parse_args()

    errors = []
    runtime_path = Path(args.runtime_patch)
    blink_path = Path(args.blink_patch)
    if not runtime_path.exists() or not runtime_path.is_file():
        errors.append(f"patch_missing:{runtime_path}")
    else:
        errors.extend(check_patch(runtime_path, REQUIRED_RUNTIME_TOKENS))
    if not blink_path.exists() or not blink_path.is_file():
        errors.append(f"patch_missing:{blink_path}")
    else:
        errors.extend(check_patch(blink_path, REQUIRED_BLINK_TOKENS))

    result = {
        "ok": not errors,
        "runtime_patch": str(runtime_path),
        "blink_patch": str(blink_path),
        "errors": errors,
    }
    if args.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    else:
        if result["ok"]:
            print(f"doke_hardware_patch_ok runtime={runtime_path} blink={blink_path}")
        else:
            print("doke_hardware_patch_failed")
            for error in errors:
                print(f"- {error}")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
