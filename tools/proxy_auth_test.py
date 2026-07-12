import json
import os
import socket
import struct
import time


def sock_path() -> str:
    tmpdir = os.environ.get("TMPDIR") or "/tmp"
    return os.path.join(tmpdir, "dokebrowser_agent_ipc_v1")


def send(sock: socket.socket, obj: dict) -> None:
    b = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    sock.sendall(struct.pack(">I", len(b)) + b)


def recv_one(sock: socket.socket):
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


def read_loop(sock: socket.socket, seconds: float) -> None:
    end = time.time() + seconds
    while time.time() < end:
        try:
            msg = recv_one(sock)
            if msg is None:
                return
            print(json.dumps(msg, ensure_ascii=False))
        except socket.timeout:
            pass


def main() -> None:
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock_path())
    s.settimeout(1.0)

    data_dir = os.path.abspath("./.tmp/proxy_auth_demo")
    os.makedirs(data_dir, exist_ok=True)

    send(
        s,
        {
            "type": "profile.start",
            "profile_id": "authdemo",
            "profile_name": "authdemo",
            "data_dir": data_dir,
            "url": "https://httpbin.org/ip",
            "proxy": {
                "enabled": True,
                "type": "http",
                "host": "127.0.0.1",
                "port": 8080,
                "username": "u",
                "password": "p",
            },
        },
    )

    read_loop(s, 6.0)


if __name__ == "__main__":
    main()

