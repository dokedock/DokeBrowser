import argparse
import json
import os
import stat
import sys


ALLOWED_CAPABILITIES = {
    "native_fingerprint",
    "native_proxy",
    "native_geoip",
    "native_humanize",
}


def shell_quote_single(value):
    return "'" + value.replace("'", "'\"'\"'") + "'"


def write_unix_fake(path, version, capabilities, probe_log, sleep_seconds):
    payload = json.dumps(
        {
            "probe_protocol": 1,
            "version": version,
            "capabilities": capabilities,
        },
        separators=(",", ":"),
    )
    lines = [
        "#!/bin/sh",
        'if [ "$1" = "--doke-probe" ]; then',
    ]
    if probe_log:
        lines.append("  echo " + shell_quote_single(probe_log))
    lines.extend(
        [
            "  printf '%s\\n' " + shell_quote_single(payload),
            "  exit 0",
            "fi",
            'if [ "$1" = "--version" ]; then',
            "  echo " + shell_quote_single(version),
            "  exit 0",
            "fi",
        ]
    )
    if sleep_seconds > 0:
        lines.append(f"sleep {int(sleep_seconds)}")
    lines.append("exit 0")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    os.chmod(path, stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR | stat.S_IRGRP | stat.S_IXGRP)


def main():
    parser = argparse.ArgumentParser(description="Create a fake doke_chromium executable for local probe/runtime testing.")
    parser.add_argument("output", help="Path to write the fake executable")
    parser.add_argument("--version", default="Doke Chromium fake", help="Version string returned by probes")
    parser.add_argument(
        "--capability",
        action="append",
        default=[],
        choices=sorted(ALLOWED_CAPABILITIES),
        help="Native capability to report. Can be passed more than once.",
    )
    parser.add_argument("--all-capabilities", action="store_true", help="Report every known native capability")
    parser.add_argument("--probe-log", default="", help="Optional log line printed before --doke-probe JSON")
    parser.add_argument("--sleep", type=int, default=30, help="Seconds to sleep for normal browser launch")
    args = parser.parse_args()

    output_dir = os.path.dirname(os.path.abspath(args.output))
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    capabilities = sorted(ALLOWED_CAPABILITIES) if args.all_capabilities else []
    for capability in args.capability:
        if capability not in capabilities:
            capabilities.append(capability)

    if os.name == "nt":
        print("Windows fake generation is not implemented yet", file=sys.stderr)
        return 2

    write_unix_fake(args.output, args.version, capabilities, args.probe_log, args.sleep)
    print(args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
