import json
import os
import socket
import struct
import sys
import time


NATIVE_FEATURE_FLAGS = {
    "--native-fingerprint": "native_fingerprint",
    "--native-proxy": "native_proxy",
    "--native-geoip": "native_geoip",
    "--native-humanize": "native_humanize",
}

NATIVE_FEATURE_ALIASES = {
    "fingerprint": "native_fingerprint",
    "proxy": "native_proxy",
    "geoip": "native_geoip",
    "humanize": "native_humanize",
    "native_fingerprint": "native_fingerprint",
    "native_proxy": "native_proxy",
    "native_geoip": "native_geoip",
    "native_humanize": "native_humanize",
}


def sock_path():
    tmpdir = os.environ.get("TMPDIR") or "/tmp"
    return os.path.join(tmpdir, "dokebrowser_agent_ipc_v1")


def send(sock, obj):
    b = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    sock.sendall(struct.pack(">I", len(b)) + b)


def recv_one(sock):
    hdr = sock.recv(4)
    if not hdr:
        return None
    n = struct.unpack(">I", hdr)[0]
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            break
        buf += chunk
    return json.loads(buf.decode("utf-8"))


def read_loop(sock, seconds):
    end = time.time() + seconds
    while time.time() < end:
        try:
            msg = recv_one(sock)
            if msg is None:
                return
            print(json.dumps(msg, ensure_ascii=False))
        except socket.timeout:
            pass


def all_native_features():
    return {
        "native_fingerprint": True,
        "native_proxy": True,
        "native_geoip": True,
        "native_humanize": True,
    }


def parse_native_flags(values):
    features = {}
    rest = []
    for value in values:
        if value == "--native":
            features.update(all_native_features())
            continue
        if value in NATIVE_FEATURE_FLAGS:
            features[NATIVE_FEATURE_FLAGS[value]] = True
            continue
        if value.startswith("--native="):
            names = [part.strip().lower() for part in value.split("=", 1)[1].split(",")]
            for name in names:
                if not name:
                    continue
                if name == "all":
                    features.update(all_native_features())
                    continue
                feature = NATIVE_FEATURE_ALIASES.get(name)
                if feature:
                    features[feature] = True
            continue
        rest.append(value)
    return features, rest


def engine_config(executable="", extra_args=None, native=False, features=None):
    cfg = {}
    if executable:
        cfg["executable"] = executable
    if extra_args:
        cfg["extra_args"] = extra_args
    feature_map = {}
    if native:
        feature_map.update(all_native_features())
    if features:
        feature_map.update(features)
    if feature_map:
        cfg["features"] = feature_map
    return json.dumps(cfg, separators=(",", ":"))


def main():
    sp = sock_path()
    cmd = sys.argv[1] if len(sys.argv) > 1 else "hello"

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sp)
    s.settimeout(1.0)

    if cmd == "hello":
        send(s, {"type": "hello", "client": "cli"})
        read_loop(s, 2.0)
        return

    if cmd == "engine-list":
        send(s, {"type": "engine.list"})
        read_loop(s, 2.0)
        return

    if cmd == "probe-engine":
        engine = sys.argv[2] if len(sys.argv) > 2 else "doke_chromium"
        executable = sys.argv[3] if len(sys.argv) > 3 else os.environ.get("DOKE_CHROMIUM_PATH", "")
        profile_id = sys.argv[4] if len(sys.argv) > 4 else "cli-probe"
        features, _ = parse_native_flags(sys.argv[5:])
        send(
            s,
            {
                "type": "engine.probe",
                "profile_id": profile_id,
                "browser_engine": engine,
                "engine_config_json": engine_config(executable, features=features),
            },
        )
        read_loop(s, 2.0)
        return

    if cmd == "start-doke":
        profile_id = sys.argv[2] if len(sys.argv) > 2 else "cli-doke"
        executable = sys.argv[3] if len(sys.argv) > 3 else os.environ.get("DOKE_CHROMIUM_PATH", "")
        data_dir = sys.argv[4] if len(sys.argv) > 4 else os.path.join(os.environ.get("TMPDIR") or "/tmp", "doke_cli_profile")
        url = sys.argv[5] if len(sys.argv) > 5 else "about:blank"
        features, extra_args = parse_native_flags(sys.argv[6:] if len(sys.argv) > 6 else [])
        os.makedirs(data_dir, exist_ok=True)
        send(
            s,
            {
                "type": "profile.start",
                "profile_id": profile_id,
                "profile_name": profile_id,
                "data_dir": data_dir,
                "url": url,
                "browser_engine": "doke_chromium",
                "engine_config_json": engine_config(executable, extra_args, features=features),
            },
        )
        read_loop(s, 8.0)
        return

    if cmd == "start":
        profile_id = sys.argv[2] if len(sys.argv) > 2 else "demo"
        data_dir = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.environ.get("TMPDIR") or "/tmp", "doke_demo_profile")
        url = sys.argv[4] if len(sys.argv) > 4 else "https://example.com"
        language = sys.argv[5] if len(sys.argv) > 5 else ""
        timezone = sys.argv[6] if len(sys.argv) > 6 else ""
        resolution = sys.argv[7] if len(sys.argv) > 7 else ""
        touch_enabled_raw = sys.argv[8] if len(sys.argv) > 8 else ""
        touch_enabled = touch_enabled_raw.lower() in ("1", "true", "yes", "y", "on")
        user_agent = sys.argv[9] if len(sys.argv) > 9 else ""
        platform = sys.argv[10] if len(sys.argv) > 10 else ""
        hardware_concurrency_raw = sys.argv[11] if len(sys.argv) > 11 else ""
        device_memory_gb_raw = sys.argv[12] if len(sys.argv) > 12 else ""
        device_scale_factor_raw = sys.argv[13] if len(sys.argv) > 13 else ""
        geo_enabled_raw = sys.argv[14] if len(sys.argv) > 14 else ""
        geo_latitude_raw = sys.argv[15] if len(sys.argv) > 15 else ""
        geo_longitude_raw = sys.argv[16] if len(sys.argv) > 16 else ""
        geo_accuracy_raw = sys.argv[17] if len(sys.argv) > 17 else ""
        try:
            hardware_concurrency = int(hardware_concurrency_raw) if hardware_concurrency_raw else 0
        except Exception:
            hardware_concurrency = 0
        try:
            device_memory_gb = int(device_memory_gb_raw) if device_memory_gb_raw else 0
        except Exception:
            device_memory_gb = 0
        try:
            device_scale_factor = float(device_scale_factor_raw) if device_scale_factor_raw else 0
        except Exception:
            device_scale_factor = 0
        geo_enabled = geo_enabled_raw.lower() in ("1", "true", "yes", "y", "on")
        try:
            geo_latitude = float(geo_latitude_raw) if geo_latitude_raw else 0
        except Exception:
            geo_latitude = 0
        try:
            geo_longitude = float(geo_longitude_raw) if geo_longitude_raw else 0
        except Exception:
            geo_longitude = 0
        try:
            geo_accuracy = float(geo_accuracy_raw) if geo_accuracy_raw else 0
        except Exception:
            geo_accuracy = 0
        os.makedirs(data_dir, exist_ok=True)
        send(
            s,
            {
                "type": "profile.start",
                "profile_id": profile_id,
                "profile_name": profile_id,
                "data_dir": data_dir,
                "url": url,
                "language": language,
                "user_agent": user_agent,
                "platform": platform,
                "hardware_concurrency": hardware_concurrency,
                "device_memory_gb": device_memory_gb,
                "device_scale_factor": device_scale_factor,
                "timezone": timezone,
                "resolution": resolution,
                "touch_enabled": touch_enabled,
                "geo_enabled": geo_enabled,
                "geo_latitude": geo_latitude,
                "geo_longitude": geo_longitude,
                "geo_accuracy": geo_accuracy,
            },
        )
        read_loop(s, 8.0)
        return

    if cmd == "stop":
        profile_id = sys.argv[2] if len(sys.argv) > 2 else "demo"
        send(s, {"type": "profile.stop", "profile_id": profile_id, "profile_name": profile_id})
        read_loop(s, 5.0)
        return

    if cmd == "start-stop":
        profile_id = sys.argv[2] if len(sys.argv) > 2 else "demo"
        data_dir = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.environ.get("TMPDIR") or "/tmp", "doke_demo_profile")
        url = sys.argv[4] if len(sys.argv) > 4 else "https://example.com"
        language = sys.argv[5] if len(sys.argv) > 5 else ""
        timezone = sys.argv[6] if len(sys.argv) > 6 else ""
        resolution = sys.argv[7] if len(sys.argv) > 7 else ""
        touch_enabled_raw = sys.argv[8] if len(sys.argv) > 8 else ""
        touch_enabled = touch_enabled_raw.lower() in ("1", "true", "yes", "y", "on")
        user_agent = sys.argv[9] if len(sys.argv) > 9 else ""
        platform = sys.argv[10] if len(sys.argv) > 10 else ""
        hardware_concurrency_raw = sys.argv[11] if len(sys.argv) > 11 else ""
        device_memory_gb_raw = sys.argv[12] if len(sys.argv) > 12 else ""
        device_scale_factor_raw = sys.argv[13] if len(sys.argv) > 13 else ""
        geo_enabled_raw = sys.argv[14] if len(sys.argv) > 14 else ""
        geo_latitude_raw = sys.argv[15] if len(sys.argv) > 15 else ""
        geo_longitude_raw = sys.argv[16] if len(sys.argv) > 16 else ""
        geo_accuracy_raw = sys.argv[17] if len(sys.argv) > 17 else ""
        try:
            hardware_concurrency = int(hardware_concurrency_raw) if hardware_concurrency_raw else 0
        except Exception:
            hardware_concurrency = 0
        try:
            device_memory_gb = int(device_memory_gb_raw) if device_memory_gb_raw else 0
        except Exception:
            device_memory_gb = 0
        try:
            device_scale_factor = float(device_scale_factor_raw) if device_scale_factor_raw else 0
        except Exception:
            device_scale_factor = 0
        geo_enabled = geo_enabled_raw.lower() in ("1", "true", "yes", "y", "on")
        try:
            geo_latitude = float(geo_latitude_raw) if geo_latitude_raw else 0
        except Exception:
            geo_latitude = 0
        try:
            geo_longitude = float(geo_longitude_raw) if geo_longitude_raw else 0
        except Exception:
            geo_longitude = 0
        try:
            geo_accuracy = float(geo_accuracy_raw) if geo_accuracy_raw else 0
        except Exception:
            geo_accuracy = 0
        os.makedirs(data_dir, exist_ok=True)
        send(s, {"type": "hello", "client": "cli"})
        read_loop(s, 1.0)
        send(
            s,
            {
                "type": "profile.start",
                "profile_id": profile_id,
                "profile_name": profile_id,
                "data_dir": data_dir,
                "url": url,
                "language": language,
                "user_agent": user_agent,
                "platform": platform,
                "hardware_concurrency": hardware_concurrency,
                "device_memory_gb": device_memory_gb,
                "device_scale_factor": device_scale_factor,
                "timezone": timezone,
                "resolution": resolution,
                "touch_enabled": touch_enabled,
                "geo_enabled": geo_enabled,
                "geo_latitude": geo_latitude,
                "geo_longitude": geo_longitude,
                "geo_accuracy": geo_accuracy,
            },
        )
        read_loop(s, 6.0)
        send(s, {"type": "profile.stop", "profile_id": profile_id, "profile_name": profile_id})
        read_loop(s, 4.0)
        return

    raise SystemExit(f"unknown cmd: {cmd}")


if __name__ == "__main__":
    main()
