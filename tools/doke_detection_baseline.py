#!/usr/bin/env python3
"""Create and validate DokeBrowser detection baseline run files."""

from __future__ import annotations

import argparse
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import doke_cdp_capture


SCHEMA = "doke_detection_baseline.v1"
SITES_SCHEMA = "doke_detection_sites.v1"
PHASES = ("clean_launch", "restart")
RESULTS = {"pending", "pass", "partial", "fail", "blocked"}
ENGINES = {"system_chrome", "doke_chromium"}
SIGNALS_SCHEMA = "doke_detection_signals.v1"
COMPARE_SNAPSHOT_FIELDS = (
    "user_agent",
    "platform",
    "language",
    "languages",
    "timezone",
    "webdriver",
    "hardware_concurrency",
    "device_memory",
    "screen",
    "ua_client_hints",
    "plugins_length",
    "mime_types_length",
)
NATIVE_FEATURES = ("native_fingerprint", "native_proxy", "native_geoip", "native_humanize")
VOLATILE_COMPARE_FIELDS = {"extractor.body_text_hash", "extractor.body_text_length"}
FINGERPRINT_FIELDS = (
    "language",
    "user_agent",
    "platform",
    "timezone",
    "resolution",
    "hardware_concurrency",
    "device_memory_gb",
    "device_scale_factor",
    "screen_color_depth",
    "screen_avail_width",
    "screen_avail_height",
    "touch_enabled",
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_manifest() -> Path:
    return repo_root() / "docs" / "DETECTION_SITES.json"


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")


def ipc_socket_path() -> Path:
    return Path(os.environ.get("TMPDIR") or "/tmp") / "dokebrowser_agent_ipc_v1"


def send_ipc(sock: socket.socket, obj: dict) -> None:
    body = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    sock.sendall(struct.pack(">I", len(body)) + body)


def recv_ipc(sock: socket.socket) -> dict | None:
    header = sock.recv(4)
    if not header:
        return None
    size = struct.unpack(">I", header)[0]
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            break
        chunks.extend(chunk)
    if not chunks:
        return None
    return json.loads(bytes(chunks).decode("utf-8"))


def connect_ipc(timeout: float) -> socket.socket:
    deadline = time.time() + timeout
    last_error: Exception | None = None
    path = str(ipc_socket_path())
    while time.time() < deadline:
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(1.0)
            sock.connect(path)
            return sock
        except Exception as exc:
            last_error = exc
            time.sleep(0.2)
    raise RuntimeError(f"ipc_connect_failed:{last_error}")


def wait_profile_status(sock: socket.socket, profile_id: str, statuses: set[str], timeout: float, verbose_logs: bool = False) -> dict:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            msg = recv_ipc(sock)
        except socket.timeout:
            continue
        if not msg:
            continue
        if msg.get("type") == "log.line" and verbose_logs:
            print(f"agent_log {msg.get('message', '')}")
            continue
        if msg.get("type") != "profile.status":
            continue
        if msg.get("profile_id") != profile_id:
            continue
        if str(msg.get("status", "")) in statuses:
            return msg
    raise RuntimeError(f"profile_status_timeout:{profile_id}:{','.join(sorted(statuses))}")


def drain_ipc(sock: socket.socket, duration: float = 0.25) -> int:
    previous_timeout = sock.gettimeout()
    deadline = time.time() + duration
    drained = 0
    try:
        sock.settimeout(0.05)
        while time.time() < deadline:
            try:
                msg = recv_ipc(sock)
            except socket.timeout:
                continue
            if not msg:
                continue
            drained += 1
    finally:
        sock.settimeout(previous_timeout)
    return drained


def repo_relative(path: Path) -> str:
    root = repo_root().resolve()
    resolved = path.resolve()
    try:
        return resolved.relative_to(root).as_posix()
    except ValueError:
        return resolved.as_posix()


def workspace_path(path: Path) -> Path:
    if path.is_absolute():
        return path
    return repo_root() / path


def engine_config_json(payload: dict, native_features: list[str] | None = None) -> str:
    cfg: dict[str, object] = {}
    browser_path = str(payload.get("browser_path", "")).strip()
    if browser_path:
        cfg["executable"] = browser_path
    features = {feature: True for feature in (native_features or []) if feature in NATIVE_FEATURES}
    if features:
        cfg["features"] = features
    return json.dumps(cfg, separators=(",", ":"))


def profile_data_dir(payload: dict) -> str:
    return repo_relative(
        repo_root()
        / ".tmp"
        / "detection_profiles"
        / slug(str(payload.get("run_id", "baseline")))
        / slug(str(payload.get("profile_id", "baseline-profile")))
    )


def fingerprint_from_payload(payload: dict) -> dict:
    fp = payload.get("fingerprint", {})
    if not isinstance(fp, dict):
        return {}
    out: dict[str, object] = {}
    for key in FINGERPRINT_FIELDS:
        value = fp.get(key)
        if value in ("", None, [], {}):
            continue
        out[key] = value
    return out


def start_message(payload: dict, url: str, native_features: list[str] | None = None) -> dict:
    engine = str(payload.get("engine", "system_chrome"))
    msg = {
        "type": "profile.start",
        "profile_id": str(payload.get("profile_id", "baseline-profile")),
        "profile_name": str(payload.get("profile_id", "baseline-profile")),
        "data_dir": profile_data_dir(payload),
        "url": url,
        "browser_engine": engine,
        "capture_debug_port": True,
    }
    msg.update(fingerprint_from_payload(payload))
    if engine == "doke_chromium":
        msg["engine_config_json"] = engine_config_json(payload, native_features)
    return msg


def stop_message(payload: dict) -> dict:
    profile_id = str(payload.get("profile_id", "baseline-profile"))
    return {"type": "profile.stop", "profile_id": profile_id, "profile_name": profile_id}


def load_sites(path: Path) -> list[dict]:
    payload = load_json(path)
    if payload.get("schema") != SITES_SCHEMA:
        raise ValueError(f"invalid sites schema: {path}")
    sites = payload.get("sites")
    if not isinstance(sites, list):
        raise ValueError("sites must be an array")
    seen: set[str] = set()
    for site in sites:
        if not isinstance(site, dict):
            raise ValueError("site entries must be objects")
        site_id = str(site.get("id", "")).strip()
        if not site_id:
            raise ValueError("site id must be non-empty")
        if site_id in seen:
            raise ValueError(f"duplicate site id: {site_id}")
        seen.add(site_id)
        if not str(site.get("url", "")).startswith("https://"):
            raise ValueError(f"site url must be https: {site_id}")
        if not isinstance(site.get("signals"), list) or not site.get("signals"):
            raise ValueError(f"site signals must be a non-empty array: {site_id}")
    return sites


def slug(value: str) -> str:
    out = re.sub(r"[^A-Za-z0-9_.-]+", "-", value.strip())
    return out.strip("-") or "baseline"


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def fingerprint_from_args(args: argparse.Namespace) -> dict:
    out: dict[str, object] = {}
    for key in FINGERPRINT_FIELDS:
        value = getattr(args, key, None)
        if value in ("", None, [], {}):
            continue
        out[key] = value
    return out


def make_run(args: argparse.Namespace, sites: list[dict]) -> dict:
    created = utc_now()
    run_id = args.run_id.strip() if args.run_id else f"{created[:10]}-{args.engine}-{slug(args.profile_id)}"
    selected = set(args.site or [])
    filtered_sites = [site for site in sites if not selected or site["id"] in selected]
    if selected:
        missing = sorted(selected.difference(site["id"] for site in filtered_sites))
        if missing:
            raise ValueError(f"unknown site id(s): {','.join(missing)}")

    payload = {
        "schema": SCHEMA,
        "run_id": run_id,
        "created_at_utc": created,
        "engine": args.engine,
        "profile_id": args.profile_id,
        "profile_seed": args.profile_seed,
        "browser_path": args.browser_path,
        "notes": args.notes,
        "sites": [
            {
                "site_id": site["id"],
                "name": site["name"],
                "url": site["url"],
                "purpose": site["purpose"],
                "signals": site["signals"],
                "runs": [
                    {
                        "phase": phase,
                        "result": "pending",
                        "summary": "",
                        "signals": {},
                        "artifacts": [],
                        "checked_at_utc": "",
                    }
                    for phase in PHASES
                ],
            }
            for site in filtered_sites
        ],
    }
    fingerprint = fingerprint_from_args(args)
    if fingerprint:
        payload["fingerprint"] = fingerprint
    return payload


def validate_run(payload: dict, sites: list[dict]) -> list[str]:
    errors: list[str] = []
    known_sites = {site["id"]: site for site in sites}
    if payload.get("schema") != SCHEMA:
        errors.append(f"schema must be {SCHEMA}")
    if payload.get("engine") not in ENGINES:
        errors.append(f"engine must be one of {','.join(sorted(ENGINES))}")
    for key in ("run_id", "profile_id", "created_at_utc"):
        if not isinstance(payload.get(key), str) or not payload.get(key, "").strip():
            errors.append(f"{key} must be a non-empty string")

    fingerprint = payload.get("fingerprint", {})
    if fingerprint and not isinstance(fingerprint, dict):
        errors.append("fingerprint must be an object")
    elif isinstance(fingerprint, dict):
        for key in fingerprint:
            if key not in FINGERPRINT_FIELDS:
                errors.append(f"fingerprint.{key} is not supported")

    run_sites = payload.get("sites")
    if not isinstance(run_sites, list) or not run_sites:
        errors.append("sites must be a non-empty array")
        return errors

    seen: set[str] = set()
    for site in run_sites:
        if not isinstance(site, dict):
            errors.append("site entries must be objects")
            continue
        site_id = str(site.get("site_id", "")).strip()
        if not site_id:
            errors.append("site.site_id must be non-empty")
            continue
        if site_id in seen:
            errors.append(f"duplicate site in run: {site_id}")
        seen.add(site_id)
        if site_id not in known_sites:
            errors.append(f"unknown site_id: {site_id}")
        if not isinstance(site.get("signals"), list):
            errors.append(f"{site_id}.signals must be an array")
        runs = site.get("runs")
        if not isinstance(runs, list) or not runs:
            errors.append(f"{site_id}.runs must be a non-empty array")
            continue
        phases = []
        for item in runs:
            if not isinstance(item, dict):
                errors.append(f"{site_id}.runs entries must be objects")
                continue
            phase = item.get("phase")
            phases.append(phase)
            if phase not in PHASES:
                errors.append(f"{site_id}.phase must be one of {','.join(PHASES)}")
            if item.get("result") not in RESULTS:
                errors.append(f"{site_id}.{phase}.result must be one of {','.join(sorted(RESULTS))}")
            if not isinstance(item.get("signals"), dict):
                errors.append(f"{site_id}.{phase}.signals must be an object")
            artifacts = item.get("artifacts")
            if not isinstance(artifacts, list):
                errors.append(f"{site_id}.{phase}.artifacts must be an array")
            else:
                for artifact in artifacts:
                    if not isinstance(artifact, str) or not artifact.strip():
                        errors.append(f"{site_id}.{phase}.artifacts entries must be non-empty strings")
            artifact_dir = item.get("artifact_dir", "")
            if artifact_dir and not isinstance(artifact_dir, str):
                errors.append(f"{site_id}.{phase}.artifact_dir must be a string")
        for phase in PHASES:
            if phase not in phases:
                errors.append(f"{site_id} missing phase: {phase}")
    return errors


def phase_artifact_dir(artifacts_root: Path, run_id: str, site_id: str, phase: str) -> Path:
    return artifacts_root / slug(run_id) / slug(site_id) / slug(phase)


def write_phase_templates(payload: dict, site: dict, run: dict, phase_dir: Path) -> list[str]:
    phase_dir.mkdir(parents=True, exist_ok=True)
    readme = phase_dir / "README.md"
    notes = phase_dir / "notes.md"
    signals = phase_dir / "signals.json"
    signal_names = site.get("signals", [])
    readme.write_text(
        "\n".join(
            [
                f"# {site.get('name', site.get('site_id', 'Detection Site'))}",
                "",
                f"- run_id: `{payload.get('run_id', '')}`",
                f"- engine: `{payload.get('engine', '')}`",
                f"- profile_id: `{payload.get('profile_id', '')}`",
                f"- phase: `{run.get('phase', '')}`",
                f"- url: {site.get('url', '')}",
                "",
                "## Signals",
                "",
                *[f"- `{signal}`" for signal in signal_names],
                "",
                "## Checklist",
                "",
                "- Capture `screenshot.png` after the page has fully settled.",
                "- Save site export as `export.json` when the site provides one.",
                "- Record key values in `signals.json`.",
                "- Add manual observations to `notes.md`.",
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    if not notes.exists():
        notes.write_text("# Notes\n\n", encoding="utf-8")
    if not signals.exists():
        signal_values = {signal: "" for signal in signal_names}
        write_json(
            signals,
            {
                "schema": "doke_detection_signals.v1",
                "run_id": payload.get("run_id", ""),
                "site_id": site.get("site_id", ""),
                "phase": run.get("phase", ""),
                "result": "pending",
                "summary": "",
                "signals": signal_values,
            },
        )
    return [repo_relative(readme), repo_relative(notes), repo_relative(signals)]


def path_from_record(value: str) -> Path:
    path = Path(value)
    if path.is_absolute():
        return path
    return repo_root() / path


def compact_snapshot(snapshot_payload: dict) -> dict:
    snapshot = snapshot_payload.get("snapshot", {})
    if not isinstance(snapshot, dict):
        return {}
    screen = snapshot.get("screen", {})
    ua_data = snapshot.get("userAgentData", {})
    out = {
        "url": snapshot.get("url", ""),
        "title": snapshot.get("title", ""),
        "user_agent": snapshot.get("userAgent", ""),
        "platform": snapshot.get("platform", ""),
        "language": snapshot.get("language", ""),
        "languages": snapshot.get("languages", []),
        "webdriver": snapshot.get("webdriver", None),
        "hardware_concurrency": snapshot.get("hardwareConcurrency", None),
        "device_memory": snapshot.get("deviceMemory", None),
        "timezone": snapshot.get("timezone", ""),
        "plugins_length": snapshot.get("pluginsLength", None),
        "mime_types_length": snapshot.get("mimeTypesLength", None),
    }
    if isinstance(screen, dict):
        out["screen"] = screen
    if isinstance(ua_data, dict):
        out["ua_client_hints"] = ua_data
    return out


def sync_run_artifacts(payload: dict) -> int:
    synced = 0
    for site in payload.get("sites", []):
        for run in site.get("runs", []):
            artifact_dir = run.get("artifact_dir", "")
            if not artifact_dir:
                continue
            directory = path_from_record(artifact_dir)
            if not directory.is_dir():
                continue
            artifacts = set(run.get("artifacts", []))
            for name in ("README.md", "notes.md", "signals.json", "snapshot.json", "screenshot.png", "export.json"):
                candidate = directory / name
                if candidate.exists():
                    artifacts.add(repo_relative(candidate))
            signals_path = directory / "signals.json"
            if signals_path.exists():
                signals_payload = load_json(signals_path)
                if signals_payload.get("schema") == SIGNALS_SCHEMA:
                    if isinstance(signals_payload.get("signals"), dict):
                        non_empty_signals = {
                            key: value
                            for key, value in signals_payload["signals"].items()
                            if value not in ("", None, [], {})
                        }
                        if non_empty_signals:
                            run["signals"] = non_empty_signals
                    result = signals_payload.get("result")
                    if result in RESULTS and result != "pending":
                        run["result"] = result
                    summary = signals_payload.get("summary")
                    if isinstance(summary, str) and summary.strip():
                        run["summary"] = summary
                    checked_at = signals_payload.get("checked_at_utc")
                    if isinstance(checked_at, str) and checked_at.strip():
                        run["checked_at_utc"] = checked_at
            snapshot_path = directory / "snapshot.json"
            if snapshot_path.exists():
                snapshot_payload = load_json(snapshot_path)
                run["result"] = "partial"
                run["summary"] = "cdp_snapshot captured"
                signals = run.get("signals")
                if not isinstance(signals, dict):
                    signals = {}
                extracted = snapshot_payload.get("extracted_signals")
                if isinstance(extracted, dict):
                    for key, value in extracted.items():
                        if value not in ("", None, [], {}):
                            signals[f"extractor.{key}"] = value
                signals["cdp_snapshot"] = compact_snapshot(snapshot_payload)
                run["signals"] = signals
            run["artifacts"] = sorted(artifacts)
            synced += 1
    return synced


def cmd_prepare_artifacts(args: argparse.Namespace) -> int:
    sites = load_sites(args.manifest)
    payload = load_json(args.run_file)
    errors = validate_run(payload, sites)
    if errors:
        print("detection_baseline_failed")
        for error in errors:
            print(f"- {error}")
        return 1

    artifacts_root = workspace_path(args.artifacts_root)
    payload["artifacts_root"] = repo_relative(artifacts_root)
    for site in payload.get("sites", []):
        for run in site.get("runs", []):
            phase_dir = phase_artifact_dir(
                artifacts_root,
                str(payload.get("run_id", "")),
                str(site.get("site_id", "")),
                str(run.get("phase", "")),
            )
            artifacts = set(run.get("artifacts", []))
            artifacts.update(write_phase_templates(payload, site, run, phase_dir))
            run["artifact_dir"] = repo_relative(phase_dir)
            run["artifacts"] = sorted(artifacts)

    output = args.output or args.run_file
    write_json(output, payload)
    print(f"detection_artifacts_prepared run_file={output} root={payload['artifacts_root']}")
    return 0


def visit_rows(payload: dict) -> list[dict]:
    rows: list[dict] = []
    for site in payload.get("sites", []):
        for run in site.get("runs", []):
            rows.append(
                {
                    "site_id": site.get("site_id", ""),
                    "name": site.get("name", ""),
                    "phase": run.get("phase", ""),
                    "url": site.get("url", ""),
                    "artifact_dir": run.get("artifact_dir", ""),
                    "signals": site.get("signals", []),
                }
            )
    return rows


def cmd_visit_plan(args: argparse.Namespace) -> int:
    payload = load_json(args.run_file)
    rows = visit_rows(payload)
    if args.json:
        print(json.dumps({"run_id": payload.get("run_id", ""), "visits": rows}, ensure_ascii=False, sort_keys=True))
        return 0
    if args.markdown:
        print(f"# Detection Visit Plan: {payload.get('run_id', '')}")
        print("")
        for index, row in enumerate(rows, 1):
            print(f"{index}. **{row['name']}** `{row['phase']}`")
            print(f"   - URL: {row['url']}")
            print(f"   - Artifacts: `{row['artifact_dir'] or 'run prepare-artifacts first'}`")
            print(f"   - Signals: {', '.join(row['signals'])}")
        return 0
    for index, row in enumerate(rows, 1):
        print(
            f"{index}\t{row['site_id']}\t{row['phase']}\t{row['url']}\t"
            f"{row['artifact_dir'] or 'run prepare-artifacts first'}"
        )
    return 0


def shell_quote(value: str) -> str:
    if not value:
        return "''"
    if re.fullmatch(r"[A-Za-z0-9_@%+=:,./-]+", value):
        return value
    return "'" + value.replace("'", "'\"'\"'") + "'"


def cmd_launch_plan(args: argparse.Namespace) -> int:
    payload = load_json(args.run_file)
    first_url = ""
    rows = visit_rows(payload)
    if rows:
        first_url = rows[0]["url"]

    run_id = slug(str(payload.get("run_id", "baseline")))
    profile_id = str(payload.get("profile_id", "baseline-profile"))
    engine = str(payload.get("engine", ""))
    browser_path = str(payload.get("browser_path", ""))
    data_dir = profile_data_dir(payload)

    commands: list[str] = ["python3 tools/ipc_cli.py engine-list"]
    if engine == "doke_chromium":
        commands.append(
            "python3 tools/ipc_cli.py probe-engine "
            f"doke_chromium {shell_quote(browser_path)} {shell_quote(profile_id)}"
        )
        commands.append(
            "python3 tools/ipc_cli.py start-doke "
            f"{shell_quote(profile_id)} {shell_quote(browser_path)} {shell_quote(data_dir)} "
            f"{shell_quote(first_url or 'about:blank')}"
        )
    else:
        commands.append(
            "python3 tools/ipc_cli.py start "
            f"{shell_quote(profile_id)} {shell_quote(data_dir)} {shell_quote(first_url or 'about:blank')}"
        )
    commands.append(f"python3 tools/ipc_cli.py stop {shell_quote(profile_id)}")
    captures = []
    for row in rows:
        if row["artifact_dir"]:
            captures.append(
                "python3 tools/doke_cdp_capture.py "
                f"--debug-port <debug_port> --site-id {shell_quote(row['site_id'])} --url {shell_quote(row['url'])} "
                f"--artifact-dir {shell_quote(row['artifact_dir'])}"
            )

    if args.json:
        print(
            json.dumps(
                {
                    "run_id": payload.get("run_id", ""),
                    "engine": engine,
                    "profile_id": profile_id,
                    "data_dir": data_dir,
                    "commands": commands,
                    "captures": captures,
                    "visits": rows,
                },
                ensure_ascii=False,
                sort_keys=True,
            )
        )
        return 0
    print(f"# Start agent first: ./build/src/agent/dokebrowser_agent")
    print(f"# Run: {payload.get('run_id', '')}")
    print(f"# Data dir: {data_dir}")
    print("")
    for command in commands:
        print(command)
    print("")
    print("# Visit sequence:")
    for index, row in enumerate(rows, 1):
        print(f"# {index}. {row['site_id']} {row['phase']} {row['url']} -> {row['artifact_dir']}")
    if captures:
        print("")
        print("# Replace <debug_port> with profile.status.debug_port, then capture:")
        for command in captures:
            print(command)
    return 0


def cmd_sync_artifacts(args: argparse.Namespace) -> int:
    sites = load_sites(args.manifest)
    payload = load_json(args.run_file)
    errors = validate_run(payload, sites)
    if errors:
        print("detection_baseline_failed")
        for error in errors:
            print(f"- {error}")
        return 1
    synced = sync_run_artifacts(payload)
    output = args.output or args.run_file
    write_json(output, payload)
    print(f"detection_artifacts_synced run_file={output} phases={synced}")
    return 0


def signal_summary(signals: object) -> str:
    if not isinstance(signals, dict) or not signals:
        return ""

    def value_text(value: object) -> str:
        if isinstance(value, (bool, int, float, list, dict)):
            return json.dumps(value, ensure_ascii=False, sort_keys=True)
        return str(value)

    snapshot = signals.get("cdp_snapshot")
    if isinstance(snapshot, dict):
        parts = []
        for key in ("user_agent", "platform", "language", "timezone", "webdriver"):
            value = snapshot.get(key)
            if value is not None and value != "":
                parts.append(f"{key}={value_text(value)}")
        return "; ".join(parts)
    filled = []
    for key, value in signals.items():
        if value not in ("", None, [], {}):
            filled.append(f"{key}={value_text(value)}")
        if len(filled) >= 5:
            break
    return "; ".join(filled)


def render_report(payload: dict) -> str:
    summary = summarize(payload)
    lines = [
        f"# DokeBrowser Detection Baseline: {summary['run_id']}",
        "",
        f"- Engine: `{summary['engine']}`",
        f"- Profile: `{summary['profile_id']}`",
        f"- Sites: {summary['site_count']}",
        f"- Phases: {summary['phase_count']}",
        "",
        "## Results",
        "",
        "| Site | Phase | Result | Summary | Artifacts | Signals |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for site in payload.get("sites", []):
        for run in site.get("runs", []):
            artifacts = run.get("artifacts", [])
            artifact_text = "<br>".join(f"`{artifact}`" for artifact in artifacts) if artifacts else ""
            lines.append(
                "| {site} | `{phase}` | `{result}` | {summary_text} | {artifacts} | {signals} |".format(
                    site=site.get("name", site.get("site_id", "")),
                    phase=run.get("phase", ""),
                    result=run.get("result", ""),
                    summary_text=str(run.get("summary", "")).replace("|", "\\|"),
                    artifacts=artifact_text.replace("|", "\\|"),
                    signals=signal_summary(run.get("signals", {})).replace("|", "\\|"),
                )
            )
    lines.extend(
        [
            "",
            "## Counts",
            "",
        ]
    )
    for result, count in summary["result_counts"].items():
        lines.append(f"- `{result}`: {count}")
    return "\n".join(lines) + "\n"


def cmd_report(args: argparse.Namespace) -> int:
    payload = load_json(args.run_file)
    text = render_report(payload)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
        print(f"detection_report_created path={args.output}")
    else:
        print(text, end="")
    return 0


def run_index(payload: dict) -> dict[tuple[str, str], dict]:
    out: dict[tuple[str, str], dict] = {}
    for site in payload.get("sites", []):
        site_id = str(site.get("site_id", ""))
        for run in site.get("runs", []):
            phase = str(run.get("phase", ""))
            if site_id and phase:
                out[(site_id, phase)] = {"site": site, "run": run}
    return out


def comparable_signal_values(run: dict) -> dict:
    signals = run.get("signals", {})
    if not isinstance(signals, dict):
        return {}
    out = {}
    snapshot = signals.get("cdp_snapshot")
    if isinstance(snapshot, dict):
        for field in COMPARE_SNAPSHOT_FIELDS:
            if field in snapshot:
                out[f"cdp_snapshot.{field}"] = snapshot[field]
    for key, value in signals.items():
        if key == "cdp_snapshot":
            continue
        if key in VOLATILE_COMPARE_FIELDS:
            continue
        if value not in ("", None, [], {}):
            out[str(key)] = value
    return out


def compare_payloads(left: dict, right: dict) -> dict:
    left_index = run_index(left)
    right_index = run_index(right)
    keys = list(left_index.keys())
    for key in right_index:
        if key not in left_index:
            keys.append(key)
    rows = []
    changed = 0
    missing = 0
    for site_id, phase in keys:
        left_entry = left_index.get((site_id, phase))
        right_entry = right_index.get((site_id, phase))
        if not left_entry or not right_entry:
            missing += 1
            rows.append(
                {
                    "site_id": site_id,
                    "site_name": (left_entry or right_entry or {}).get("site", {}).get("name", site_id),
                    "phase": phase,
                    "status": "missing",
                    "differences": [{"field": "phase", "left": bool(left_entry), "right": bool(right_entry)}],
                }
            )
            continue

        left_run = left_entry["run"]
        right_run = right_entry["run"]
        differences = []
        for field in ("result", "summary"):
            left_value = left_run.get(field, "")
            right_value = right_run.get(field, "")
            if left_value != right_value:
                differences.append({"field": field, "left": left_value, "right": right_value})

        left_signals = comparable_signal_values(left_run)
        right_signals = comparable_signal_values(right_run)
        for field in sorted(set(left_signals) | set(right_signals)):
            left_value = left_signals.get(field, "")
            right_value = right_signals.get(field, "")
            if left_value != right_value:
                differences.append({"field": field, "left": left_value, "right": right_value})

        if differences:
            changed += 1
        rows.append(
            {
                "site_id": site_id,
                "site_name": left_entry["site"].get("name", site_id),
                "phase": phase,
                "status": "changed" if differences else "same",
                "differences": differences,
            }
        )

    return {
        "schema": "doke_detection_compare.v1",
        "left": summarize(left),
        "right": summarize(right),
        "phase_count": len(keys),
        "changed_phase_count": changed,
        "missing_phase_count": missing,
        "rows": rows,
    }


def diff_text(value: object) -> str:
    if value == "":
        return "(missing)"
    if isinstance(value, (bool, int, float, list, dict)):
        return json.dumps(value, ensure_ascii=False, sort_keys=True)
    return str(value)


def render_compare_report(compare: dict) -> str:
    left = compare["left"]
    right = compare["right"]
    lines = [
        f"# DokeBrowser Detection Compare: {left['run_id']} vs {right['run_id']}",
        "",
        f"- Left: `{left['engine']}` / `{left['profile_id']}`",
        f"- Right: `{right['engine']}` / `{right['profile_id']}`",
        f"- Phases: {compare['phase_count']}",
        f"- Changed: {compare['changed_phase_count']}",
        f"- Missing: {compare['missing_phase_count']}",
        "",
        "## Differences",
        "",
        "| Site | Phase | Status | Differences |",
        "| --- | --- | --- | --- |",
    ]
    for row in compare["rows"]:
        differences = row.get("differences", [])
        if differences:
            parts = [
                "`{field}`: `{left}` -> `{right}`".format(
                    field=item.get("field", ""),
                    left=diff_text(item.get("left", "")).replace("`", "\\`"),
                    right=diff_text(item.get("right", "")).replace("`", "\\`"),
                )
                for item in differences[:8]
            ]
            if len(differences) > 8:
                parts.append(f"... +{len(differences) - 8} more")
            diff_cell = "<br>".join(parts)
        else:
            diff_cell = ""
        lines.append(
            "| {site} | `{phase}` | `{status}` | {diffs} |".format(
                site=row.get("site_name", row.get("site_id", "")).replace("|", "\\|"),
                phase=row.get("phase", ""),
                status=row.get("status", ""),
                diffs=diff_cell.replace("|", "\\|"),
            )
        )
    return "\n".join(lines) + "\n"


def cmd_compare(args: argparse.Namespace) -> int:
    sites = load_sites(args.manifest)
    left = load_json(args.left_run_file)
    right = load_json(args.right_run_file)
    errors = [f"left: {error}" for error in validate_run(left, sites)]
    errors.extend(f"right: {error}" for error in validate_run(right, sites))
    if errors:
        print("detection_compare_failed")
        for error in errors:
            print(f"- {error}")
        return 1
    compare = compare_payloads(left, right)
    if args.json:
        text = json.dumps(compare, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    else:
        text = render_compare_report(compare)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
        print(f"detection_compare_created path={args.output}")
    else:
        print(text, end="")
    return 0


def native_features_from_args(args: argparse.Namespace) -> list[str]:
    features: list[str] = []
    for value in args.native_feature or []:
        normalized = value.strip().lower()
        if not normalized:
            continue
        if normalized == "all":
            for feature in NATIVE_FEATURES:
                if feature not in features:
                    features.append(feature)
            continue
        if normalized in NATIVE_FEATURES and normalized not in features:
            features.append(normalized)
    return features


def update_phase_from_capture(run: dict, capture_ok: bool, summary: str) -> None:
    run["checked_at_utc"] = utc_now()
    if capture_ok:
        run["result"] = "partial"
        run["summary"] = summary
    else:
        run["result"] = "blocked"
        run["summary"] = summary


def cmd_run_capture(args: argparse.Namespace) -> int:
    sites = load_sites(args.manifest)
    payload = load_json(args.run_file)
    errors = validate_run(payload, sites)
    if errors:
        print("detection_baseline_failed")
        for error in errors:
            print(f"- {error}")
        return 1

    if not all(run.get("artifact_dir") for site in payload.get("sites", []) for run in site.get("runs", [])):
        artifacts_root = workspace_path(args.artifacts_root)
        payload["artifacts_root"] = repo_relative(artifacts_root)
        for site in payload.get("sites", []):
            for run in site.get("runs", []):
                phase_dir = phase_artifact_dir(
                    artifacts_root,
                    str(payload.get("run_id", "")),
                    str(site.get("site_id", "")),
                    str(run.get("phase", "")),
                )
                artifacts = set(run.get("artifacts", []))
                artifacts.update(write_phase_templates(payload, site, run, phase_dir))
                run["artifact_dir"] = repo_relative(phase_dir)
                run["artifacts"] = sorted(artifacts)

    rows = visit_rows(payload)
    native_features = native_features_from_args(args)
    if args.dry_run:
        print(
            json.dumps(
                {
                    "run_id": payload.get("run_id", ""),
                    "engine": payload.get("engine", ""),
                    "profile_id": payload.get("profile_id", ""),
                    "profile_data_dir": profile_data_dir(payload),
                    "native_features": native_features,
                    "visits": rows,
                },
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
            )
        )
        return 0

    sock = connect_ipc(args.ipc_timeout)
    profile_id = str(payload.get("profile_id", ""))
    completed = 0
    try:
        send_ipc(sock, {"type": "hello", "client": "doke_detection_baseline"})
        drain_ipc(sock)
        attempted = 0
        for site in payload.get("sites", []):
            for run in site.get("runs", []):
                if args.only_blocked and run.get("result") not in {"blocked", "pending"}:
                    continue
                attempted += 1
                artifact_dir = path_from_record(str(run.get("artifact_dir", "")))
                url = str(site.get("url", ""))
                try:
                    send_ipc(sock, stop_message(payload))
                    try:
                        wait_profile_status(sock, profile_id, {"stopped", "error"}, args.stop_timeout, args.verbose_logs)
                    except RuntimeError:
                        pass
                    drain_ipc(sock)
                    send_ipc(sock, start_message(payload, url, native_features))
                    status = wait_profile_status(sock, profile_id, {"running", "error", "crashed"}, args.start_timeout, args.verbose_logs)
                    if status.get("status") != "running":
                        raise RuntimeError(status.get("error") or status.get("status") or "profile_start_failed")
                    debug_port = int(status.get("debug_port", 0) or 0)
                    if debug_port <= 0:
                        raise RuntimeError("profile_running_without_debug_port")
                    doke_cdp_capture.capture(debug_port, url, artifact_dir, args.capture_timeout, str(site.get("site_id", "")))
                    update_phase_from_capture(run, True, "cdp_snapshot captured")
                    completed += 1
                    print(f"detection_capture_phase_ok site={site.get('site_id')} phase={run.get('phase')}")
                except Exception as exc:
                    update_phase_from_capture(run, False, str(exc))
                    print(f"detection_capture_phase_failed site={site.get('site_id')} phase={run.get('phase')} error={exc}")
                    if not args.keep_going:
                        raise
                finally:
                    send_ipc(sock, stop_message(payload))
                    try:
                        wait_profile_status(sock, profile_id, {"stopped", "error"}, args.stop_timeout, args.verbose_logs)
                    except RuntimeError:
                        pass
    finally:
        sock.close()

    sync_run_artifacts(payload)
    output = args.output or args.run_file
    write_json(output, payload)
    if args.report_output:
        args.report_output.parent.mkdir(parents=True, exist_ok=True)
        args.report_output.write_text(render_report(payload), encoding="utf-8")
    print(f"detection_run_capture_done run_file={output} captured={completed} phases={attempted}")
    return 0


def cmd_init_pair(args: argparse.Namespace) -> int:
    sites = load_sites(args.manifest)
    created = utc_now()
    prefix = slug(args.run_prefix or created[:10])
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    class InitArgs:
        pass

    made = {}
    for engine in ("system_chrome", "doke_chromium"):
        init_args = InitArgs()
        init_args.engine = engine
        init_args.profile_id = f"{args.profile_base}-{engine}"
        init_args.profile_seed = args.profile_seed
        init_args.browser_path = args.browser_path if engine == "doke_chromium" else ""
        init_args.notes = args.notes
        init_args.run_id = f"{prefix}-{engine}"
        init_args.site = args.site
        for key in FINGERPRINT_FIELDS:
            setattr(init_args, key, getattr(args, key, None))
        payload = make_run(init_args, sites)
        path = output_dir / f"{payload['run_id']}.json"
        write_json(path, payload)
        made[engine] = str(path)

    pair_path = output_dir / f"{prefix}-pair.json"
    write_json(
        pair_path,
        {
            "schema": "doke_detection_pair.v1",
            "created_at_utc": created,
            "system_chrome": made["system_chrome"],
            "doke_chromium": made["doke_chromium"],
        },
    )
    print(f"detection_pair_created pair={pair_path} system={made['system_chrome']} doke={made['doke_chromium']}")
    return 0


def cmd_compare_pair(args: argparse.Namespace) -> int:
    pair = load_json(args.pair_file)
    if pair.get("schema") != "doke_detection_pair.v1":
        raise ValueError("invalid pair schema")
    system_path = path_from_record(str(pair.get("system_chrome", "")))
    doke_path = path_from_record(str(pair.get("doke_chromium", "")))
    system_payload = load_json(system_path)
    doke_payload = load_json(doke_path)
    if args.sync:
        sync_run_artifacts(system_payload)
        sync_run_artifacts(doke_payload)
        write_json(system_path, system_payload)
        write_json(doke_path, doke_payload)
    compare = compare_payloads(system_payload, doke_payload)
    text = json.dumps(compare, ensure_ascii=False, indent=2, sort_keys=True) + "\n" if args.json else render_compare_report(compare)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
        print(f"detection_pair_compare_created path={args.output}")
    else:
        print(text, end="")
    return 0


def summarize(payload: dict) -> dict:
    counts = {result: 0 for result in sorted(RESULTS)}
    sites = payload.get("sites", [])
    for site in sites:
        for run in site.get("runs", []):
            result = run.get("result", "pending")
            counts[result] = counts.get(result, 0) + 1
    return {
        "run_id": payload.get("run_id", ""),
        "engine": payload.get("engine", ""),
        "profile_id": payload.get("profile_id", ""),
        "site_count": len(sites),
        "phase_count": sum(len(site.get("runs", [])) for site in sites),
        "result_counts": counts,
    }


def print_manifest(sites: list[dict], as_json: bool) -> None:
    if as_json:
        print(json.dumps({"schema": SITES_SCHEMA, "sites": sites}, ensure_ascii=False, sort_keys=True))
        return
    for site in sites:
        print(f"{site['id']}\t{site['url']}\t{','.join(site['signals'])}")


def add_fingerprint_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--language", default="")
    parser.add_argument("--user-agent", dest="user_agent", default="")
    parser.add_argument("--platform", default="")
    parser.add_argument("--timezone", default="")
    parser.add_argument("--resolution", default="")
    parser.add_argument("--hardware-concurrency", dest="hardware_concurrency", type=int, default=0)
    parser.add_argument("--device-memory-gb", dest="device_memory_gb", type=int, default=0)
    parser.add_argument("--device-scale-factor", dest="device_scale_factor", type=float, default=0)
    parser.add_argument("--screen-color-depth", dest="screen_color_depth", type=int, default=0)
    parser.add_argument("--screen-avail-width", dest="screen_avail_width", type=int, default=0)
    parser.add_argument("--screen-avail-height", dest="screen_avail_height", type=int, default=0)
    parser.add_argument("--touch-enabled", dest="touch_enabled", action="store_true")


def cmd_init(args: argparse.Namespace) -> int:
    sites = load_sites(args.manifest)
    payload = make_run(args, sites)
    output = args.output
    if output.is_dir() or str(output).endswith("/"):
        output = output / f"{payload['run_id']}.json"
    write_json(output, payload)
    print(f"detection_baseline_created path={output}")
    return 0


def cmd_validate(args: argparse.Namespace) -> int:
    sites = load_sites(args.manifest)
    payload = load_json(args.run_file)
    errors = validate_run(payload, sites)
    result = {"ok": not errors, "run_file": str(args.run_file), "errors": errors, **summarize(payload)}
    if args.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    elif errors:
        print("detection_baseline_failed")
        for error in errors:
            print(f"- {error}")
    else:
        summary = summarize(payload)
        print(
            "detection_baseline_ok "
            f"run_id={summary['run_id']} sites={summary['site_count']} phases={summary['phase_count']}"
        )
    return 0 if not errors else 1


def cmd_summary(args: argparse.Namespace) -> int:
    payload = load_json(args.run_file)
    summary = summarize(payload)
    if args.json:
        print(json.dumps(summary, ensure_ascii=False, sort_keys=True))
    else:
        print(f"run_id={summary['run_id']}")
        print(f"engine={summary['engine']}")
        print(f"profile_id={summary['profile_id']}")
        print(f"sites={summary['site_count']}")
        for result, count in summary["result_counts"].items():
            print(f"{result}={count}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=default_manifest())
    sub = parser.add_subparsers(dest="command", required=True)

    manifest = sub.add_parser("manifest", help="Print detection target manifest")
    manifest.add_argument("--json", action="store_true")

    init = sub.add_parser("init-run", help="Create a baseline run JSON template")
    init.add_argument("--engine", required=True, choices=sorted(ENGINES))
    init.add_argument("--profile-id", required=True)
    init.add_argument("--profile-seed", default="")
    init.add_argument("--browser-path", default="")
    init.add_argument("--notes", default="")
    init.add_argument("--run-id", default="")
    init.add_argument("--site", action="append", help="Limit to a site id. Can be passed more than once.")
    init.add_argument("--output", type=Path, default=repo_root() / ".tmp" / "detection_baselines")
    add_fingerprint_args(init)

    validate = sub.add_parser("validate", help="Validate a baseline run JSON")
    validate.add_argument("run_file", type=Path)
    validate.add_argument("--json", action="store_true")

    summary = sub.add_parser("summary", help="Summarize a baseline run JSON")
    summary.add_argument("run_file", type=Path)
    summary.add_argument("--json", action="store_true")

    prepare = sub.add_parser("prepare-artifacts", help="Create artifact directories and templates for a run")
    prepare.add_argument("run_file", type=Path)
    prepare.add_argument("--artifacts-root", type=Path, default=repo_root() / ".tmp" / "detection_artifacts")
    prepare.add_argument("--output", type=Path)

    visit_plan = sub.add_parser("visit-plan", help="Print the ordered site/phase visit plan")
    visit_plan.add_argument("run_file", type=Path)
    visit_plan.add_argument("--json", action="store_true")
    visit_plan.add_argument("--markdown", action="store_true")

    launch_plan = sub.add_parser("launch-plan", help="Print IPC CLI commands for a baseline run")
    launch_plan.add_argument("run_file", type=Path)
    launch_plan.add_argument("--json", action="store_true")

    sync_artifacts = sub.add_parser("sync-artifacts", help="Merge artifact directory data back into a run JSON")
    sync_artifacts.add_argument("run_file", type=Path)
    sync_artifacts.add_argument("--output", type=Path)

    report = sub.add_parser("report", help="Render a Markdown report from a baseline run JSON")
    report.add_argument("run_file", type=Path)
    report.add_argument("--output", type=Path)

    compare = sub.add_parser("compare", help="Compare two baseline run JSON files")
    compare.add_argument("left_run_file", type=Path)
    compare.add_argument("right_run_file", type=Path)
    compare.add_argument("--json", action="store_true")
    compare.add_argument("--output", type=Path)

    run_capture = sub.add_parser("run-capture", help="Run the IPC/CDP capture workflow for a baseline run")
    run_capture.add_argument("run_file", type=Path)
    run_capture.add_argument("--artifacts-root", type=Path, default=repo_root() / ".tmp" / "detection_artifacts")
    run_capture.add_argument("--output", type=Path)
    run_capture.add_argument("--report-output", type=Path)
    run_capture.add_argument("--native-feature", action="append", choices=[*NATIVE_FEATURES, "all"])
    run_capture.add_argument("--ipc-timeout", type=float, default=8.0)
    run_capture.add_argument("--start-timeout", type=float, default=15.0)
    run_capture.add_argument("--stop-timeout", type=float, default=5.0)
    run_capture.add_argument("--capture-timeout", type=float, default=15.0)
    run_capture.add_argument("--keep-going", action="store_true")
    run_capture.add_argument("--only-blocked", action="store_true", help="Capture only pending or blocked phases.")
    run_capture.add_argument("--verbose-logs", action="store_true", help="Print Agent log.line messages while waiting for profile status.")
    run_capture.add_argument("--dry-run", action="store_true")

    init_pair = sub.add_parser("init-pair", help="Create matching system_chrome and doke_chromium baseline runs")
    init_pair.add_argument("--profile-base", required=True)
    init_pair.add_argument("--profile-seed", default="")
    init_pair.add_argument("--browser-path", default="")
    init_pair.add_argument("--run-prefix", default="")
    init_pair.add_argument("--notes", default="")
    init_pair.add_argument("--site", action="append", help="Limit to a site id. Can be passed more than once.")
    init_pair.add_argument("--output-dir", type=Path, default=repo_root() / ".tmp" / "detection_baselines")
    add_fingerprint_args(init_pair)

    compare_pair = sub.add_parser("compare-pair", help="Compare a system/doke baseline pair file")
    compare_pair.add_argument("pair_file", type=Path)
    compare_pair.add_argument("--sync", action="store_true")
    compare_pair.add_argument("--json", action="store_true")
    compare_pair.add_argument("--output", type=Path)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        if args.command == "manifest":
            print_manifest(load_sites(args.manifest), args.json)
            return 0
        if args.command == "init-run":
            return cmd_init(args)
        if args.command == "validate":
            return cmd_validate(args)
        if args.command == "summary":
            return cmd_summary(args)
        if args.command == "prepare-artifacts":
            return cmd_prepare_artifacts(args)
        if args.command == "visit-plan":
            return cmd_visit_plan(args)
        if args.command == "launch-plan":
            return cmd_launch_plan(args)
        if args.command == "sync-artifacts":
            return cmd_sync_artifacts(args)
        if args.command == "report":
            return cmd_report(args)
        if args.command == "compare":
            return cmd_compare(args)
        if args.command == "run-capture":
            return cmd_run_capture(args)
        if args.command == "init-pair":
            return cmd_init_pair(args)
        if args.command == "compare-pair":
            return cmd_compare_pair(args)
    except Exception as exc:
        print(f"detection_baseline_error: {exc}", file=sys.stderr)
        return 2
    parser.error("unknown command")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
