import argparse
import json
import os
import subprocess
import sys


ALLOWED_CAPABILITIES = {
    "native_fingerprint",
    "native_proxy",
    "native_geoip",
    "native_humanize",
}


def run_command(executable, args, timeout):
    try:
        completed = subprocess.run(
            [executable] + args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
            text=True,
        )
    except subprocess.TimeoutExpired as exc:
        return {
            "ok": False,
            "error": "timeout",
            "stdout": exc.stdout or "",
            "stderr": exc.stderr or "",
        }
    except OSError as exc:
        return {
            "ok": False,
            "error": f"start_failed:{exc}",
            "stdout": "",
            "stderr": "",
        }

    return {
        "ok": completed.returncode == 0,
        "exit_code": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
    }


def extract_json_object(text):
    stripped = text.strip()
    if stripped.startswith("{") and stripped.endswith("}"):
        return stripped
    start = stripped.find("{")
    end = stripped.rfind("}")
    if start < 0 or end <= start:
        return ""
    return stripped[start : end + 1]


def validate_probe(obj):
    errors = []
    protocol = obj.get("probe_protocol")
    if protocol != 1 and protocol != "1":
        errors.append("probe_protocol must be 1")

    version = str(obj.get("version") or "").strip()
    if not version:
        errors.append("version must be non-empty")

    capabilities = obj.get("capabilities")
    if not isinstance(capabilities, list):
        errors.append("capabilities must be an array")
        capabilities = []

    for capability in capabilities:
        if not isinstance(capability, str) or not capability.strip():
            errors.append("capabilities must contain non-empty strings")
            continue
        if capability.strip() not in ALLOWED_CAPABILITIES:
            errors.append(f"unknown capability: {capability}")

    return errors


def first_line(text):
    for line in text.splitlines():
        line = line.strip()
        if line:
            return line
    return ""


def main():
    parser = argparse.ArgumentParser(description="Validate a Doke Chromium --doke-probe implementation.")
    parser.add_argument("executable", help="Path to doke_chromium")
    parser.add_argument("--timeout", type=float, default=2.0, help="Probe timeout in seconds")
    parser.add_argument("--json", action="store_true", help="Print machine-readable result")
    args = parser.parse_args()

    result = {
        "ok": False,
        "executable": args.executable,
        "errors": [],
        "probe": {},
        "version_line": "",
    }

    if not os.path.exists(args.executable):
        result["errors"].append("path_missing")
    elif not os.path.isfile(args.executable):
        result["errors"].append("path_not_file")
    elif not os.access(args.executable, os.X_OK):
        result["errors"].append("path_not_executable")

    if not result["errors"]:
        probe_run = run_command(args.executable, ["--doke-probe"], args.timeout)
        if not probe_run["ok"]:
            result["errors"].append(probe_run.get("error") or f"probe_exit_{probe_run.get('exit_code')}")
        raw_probe = (probe_run.get("stdout") or "") + "\n" + (probe_run.get("stderr") or "")
        payload = extract_json_object(raw_probe)
        if not payload:
            result["errors"].append("probe_json_missing")
        else:
            try:
                result["probe"] = json.loads(payload)
            except json.JSONDecodeError as exc:
                result["errors"].append(f"probe_json_invalid:{exc.msg}")
            else:
                result["errors"].extend(validate_probe(result["probe"]))

        version_run = run_command(args.executable, ["--version"], args.timeout)
        if version_run["ok"]:
            result["version_line"] = first_line((version_run.get("stdout") or "") + "\n" + (version_run.get("stderr") or ""))
            if not result["version_line"]:
                result["errors"].append("version_empty")
        else:
            result["errors"].append(version_run.get("error") or f"version_exit_{version_run.get('exit_code')}")

    result["ok"] = not result["errors"]
    if args.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    else:
        if result["ok"]:
            caps = ", ".join(result["probe"].get("capabilities") or [])
            print(f"probe_ok version={result['probe'].get('version')} capabilities={caps}")
            if result["version_line"]:
                print(f"version_ok {result['version_line']}")
        else:
            print("probe_failed")
            for error in result["errors"]:
                print(f"- {error}")

    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
