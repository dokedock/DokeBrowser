import json
import os
import socket
import struct
import sys
import time


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

    if cmd == "start":
        profile_id = sys.argv[2] if len(sys.argv) > 2 else "demo"
        data_dir = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.environ.get("TMPDIR") or "/tmp", "doke_demo_profile")
        url = sys.argv[4] if len(sys.argv) > 4 else "https://example.com"
        os.makedirs(data_dir, exist_ok=True)
        send(
            s,
            {
                "type": "profile.start",
                "profile_id": profile_id,
                "profile_name": profile_id,
                "data_dir": data_dir,
                "url": url,
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
            },
        )
        read_loop(s, 6.0)
        send(s, {"type": "profile.stop", "profile_id": profile_id, "profile_name": profile_id})
        read_loop(s, 4.0)
        return

    raise SystemExit(f"unknown cmd: {cmd}")


if __name__ == "__main__":
    main()

