#!/usr/bin/env python3
"""Capture a lightweight DokeBrowser detection snapshot over Chrome DevTools Protocol."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import socket
import ssl
import struct
import time
from pathlib import Path
from urllib import parse, request


SNAPSHOT_EXPRESSION = r"""
(() => {
  const uaData = navigator.userAgentData ? {
    brands: navigator.userAgentData.brands,
    mobile: navigator.userAgentData.mobile,
    platform: navigator.userAgentData.platform
  } : null;
  return {
    url: location.href,
    title: document.title,
    userAgent: navigator.userAgent,
    platform: navigator.platform,
    language: navigator.language,
    languages: Array.from(navigator.languages || []),
    webdriver: navigator.webdriver,
    hardwareConcurrency: navigator.hardwareConcurrency,
    deviceMemory: navigator.deviceMemory,
    userAgentData: uaData,
    timezone: Intl.DateTimeFormat().resolvedOptions().timeZone,
    screen: {
      width: screen.width,
      height: screen.height,
      availWidth: screen.availWidth,
      availHeight: screen.availHeight,
      colorDepth: screen.colorDepth,
      pixelDepth: screen.pixelDepth,
      devicePixelRatio: window.devicePixelRatio
    },
    pluginsLength: navigator.plugins ? navigator.plugins.length : 0,
    mimeTypesLength: navigator.mimeTypes ? navigator.mimeTypes.length : 0
  };
})()
"""

BODY_TEXT_EXPRESSION = r"""
(() => (document.body ? document.body.innerText : '').slice(0, 200000))()
"""

LOCAL_HTTP_OPENER = request.build_opener(request.ProxyHandler({}))


def short_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8", errors="ignore")).hexdigest()[:16]


def regex_first(pattern: str, text: str) -> str:
    match = re.search(pattern, text, flags=re.IGNORECASE)
    return match.group(1).strip() if match else ""


def keyword_hits(text: str, keywords: list[str]) -> list[str]:
    lower = text.lower()
    return [keyword for keyword in keywords if keyword.lower() in lower]


def extract_site_signals(site_id: str, body_text: str) -> dict:
    text = body_text or ""
    common_keywords = keyword_hits(
        text,
        [
            "webdriver",
            "headless",
            "automation",
            "devtools",
            "cdp",
            "canvas",
            "webgl",
            "audio",
            "webrtc",
            "timezone",
            "language",
            "visitor",
            "trust",
            "lies",
            "trash",
            "bot",
        ],
    )
    out = {
        "site_id": site_id,
        "body_text_hash": short_hash(text),
        "body_text_length": len(text),
        "keyword_hits": common_keywords,
    }
    if site_id == "creepjs":
        out.update(
            {
                "trust_score": regex_first(r"trust(?:\s*score)?\s*[:\-]?\s*([0-9]+(?:\.[0-9]+)?%?)", text),
                "lies": regex_first(r"lies\s*[:\-]?\s*([0-9]+)", text),
                "trash": regex_first(r"trash\s*[:\-]?\s*([0-9]+)", text),
            }
        )
    elif site_id == "fingerprintjs_demo":
        out.update(
            {
                "visitor_id": regex_first(r"visitor\s*id\s*[:\-]?\s*([A-Za-z0-9_-]{8,})", text)
                or regex_first(r"\b([a-f0-9]{20,})\b", text),
            }
        )
    elif site_id in {"browserscan_overview", "browserscan_bot"}:
        out.update(
            {
                "browser_scan_flags": keyword_hits(text, ["consistent", "inconsistent", "passed", "failed", "bot", "risk"]),
                "webdriver_text": regex_first(r"webdriver\s*[:\-]?\s*(true|false|passed|failed|yes|no)", text),
                "webrtc_text": regex_first(r"webrtc\s*[:\-]?\s*([^\n\r]{1,80})", text),
            }
        )
    elif site_id == "deviceandbrowserinfo":
        out.update(
            {
                "device_flags": keyword_hits(text, ["webdriver", "cdp", "useragentdata", "canvas", "webgl", "worker"]),
                "webdriver_text": regex_first(r"webdriver\s*[:\-]?\s*(true|false|yes|no)", text),
                "cdp_text": regex_first(r"cdp\s*[:\-]?\s*([^\n\r]{1,80})", text),
            }
        )
    elif site_id == "bot_incolumitas":
        out.update(
            {
                "bot_score": regex_first(r"bot\s*(?:score|probability)?\s*[:\-]?\s*([0-9]+(?:\.[0-9]+)?%?)", text),
                "headless_text": regex_first(r"headless\s*[:\-]?\s*(true|false|yes|no|passed|failed)", text),
                "webdriver_text": regex_first(r"webdriver\s*[:\-]?\s*(true|false|yes|no|passed|failed)", text),
            }
        )
    return {key: value for key, value in out.items() if value not in ("", [], None)}


def load_json_url(url: str, timeout: float) -> object:
    with LOCAL_HTTP_OPENER.open(url, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def http_json(method: str, url: str, timeout: float) -> object:
    req = request.Request(url, method=method)
    with LOCAL_HTTP_OPENER.open(req, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def ensure_page_target(debug_port: int, url: str, timeout: float, force_new: bool = False) -> dict:
    base = f"http://127.0.0.1:{debug_port}"
    if not force_new:
        targets = load_json_url(f"{base}/json/list", timeout)
        if isinstance(targets, list):
            for target in targets:
                if target.get("type") == "page" and target.get("webSocketDebuggerUrl"):
                    return target
    if not url:
        raise RuntimeError("no page target found and no url was provided")
    quoted_url = parse.quote(url, safe=":/?&=%#")
    target = http_json("PUT", f"{base}/json/new?{quoted_url}", timeout)
    if not isinstance(target, dict) or not target.get("webSocketDebuggerUrl"):
        raise RuntimeError("failed to create CDP page target")
    return target


class CdpWebSocket:
    def __init__(self, ws_url: str, timeout: float):
        self.ws_url = ws_url
        self.timeout = timeout
        self.sock: socket.socket | ssl.SSLSocket | None = None
        self.next_id = 1

    def __enter__(self) -> "CdpWebSocket":
        parsed = parse.urlparse(self.ws_url)
        if parsed.scheme not in {"ws", "wss"}:
            raise RuntimeError(f"unsupported websocket scheme: {parsed.scheme}")
        host = parsed.hostname or "127.0.0.1"
        port = parsed.port or (443 if parsed.scheme == "wss" else 80)
        raw = socket.create_connection((host, port), timeout=self.timeout)
        raw.settimeout(self.timeout)
        if parsed.scheme == "wss":
            self.sock = ssl.create_default_context().wrap_socket(raw, server_hostname=host)
        else:
            self.sock = raw
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        path = parsed.path or "/"
        if parsed.query:
            path += "?" + parsed.query
        handshake = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        )
        self.sock.sendall(handshake.encode("ascii"))
        response = self.sock.recv(4096)
        if b" 101 " not in response.split(b"\r\n", 1)[0]:
            raise RuntimeError("websocket handshake failed")
        return self

    def __exit__(self, _exc_type, _exc, _tb) -> None:
        if self.sock:
            self.sock.close()
            self.sock = None

    def send_frame(self, payload: bytes) -> None:
        if not self.sock:
            raise RuntimeError("websocket is not connected")
        mask = os.urandom(4)
        header = bytearray([0x81])
        length = len(payload)
        if length < 126:
            header.append(0x80 | length)
        elif length <= 0xFFFF:
            header.append(0x80 | 126)
            header.extend(struct.pack(">H", length))
        else:
            header.append(0x80 | 127)
            header.extend(struct.pack(">Q", length))
        masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        self.sock.sendall(bytes(header) + mask + masked)

    def recv_exact(self, size: int) -> bytes:
        if not self.sock:
            raise RuntimeError("websocket is not connected")
        chunks = bytearray()
        while len(chunks) < size:
            chunk = self.sock.recv(size - len(chunks))
            if not chunk:
                raise RuntimeError("websocket closed")
            chunks.extend(chunk)
        return bytes(chunks)

    def recv_frame(self) -> bytes:
        header = self.recv_exact(2)
        opcode = header[0] & 0x0F
        length = header[1] & 0x7F
        if length == 126:
            length = struct.unpack(">H", self.recv_exact(2))[0]
        elif length == 127:
            length = struct.unpack(">Q", self.recv_exact(8))[0]
        masked = bool(header[1] & 0x80)
        mask = self.recv_exact(4) if masked else b""
        payload = self.recv_exact(length) if length else b""
        if masked:
            payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        if opcode == 0x8:
            raise RuntimeError("websocket closed")
        if opcode == 0x9:
            return self.recv_frame()
        return payload

    def command(self, method: str, params: dict | None = None) -> dict:
        command_id = self.next_id
        self.next_id += 1
        payload = {"id": command_id, "method": method}
        if params is not None:
            payload["params"] = params
        self.send_frame(json.dumps(payload, separators=(",", ":")).encode("utf-8"))
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            message = json.loads(self.recv_frame().decode("utf-8"))
            if message.get("id") == command_id:
                if "error" in message:
                    raise RuntimeError(f"cdp {method} failed: {message['error']}")
                return message
        raise RuntimeError(f"cdp {method} timed out")


def wait_ready(ws: CdpWebSocket, timeout: float) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        result = ws.command(
            "Runtime.evaluate",
            {"expression": "document.readyState", "returnByValue": True},
        )
        value = result.get("result", {}).get("result", {}).get("value", "")
        if value == "complete":
            return
        time.sleep(0.25)


def capture_once(debug_port: int, url: str, artifact_dir: Path, timeout: float, site_id: str, force_new: bool) -> dict:
    artifact_dir.mkdir(parents=True, exist_ok=True)
    target = ensure_page_target(debug_port, url, timeout, force_new=force_new)
    with CdpWebSocket(target["webSocketDebuggerUrl"], timeout) as ws:
        ws.command("Page.enable")
        ws.command("Runtime.enable")
        if url:
            ws.command("Page.navigate", {"url": url})
            wait_ready(ws, timeout)
        snapshot_result = ws.command(
            "Runtime.evaluate",
            {"expression": SNAPSHOT_EXPRESSION, "returnByValue": True, "awaitPromise": True},
        )
        body_text_result = ws.command(
            "Runtime.evaluate",
            {"expression": BODY_TEXT_EXPRESSION, "returnByValue": True, "awaitPromise": True},
        )
        screenshot_result = ws.command("Page.captureScreenshot", {"format": "png", "fromSurface": True})

    snapshot = snapshot_result.get("result", {}).get("result", {}).get("value", {})
    body_text = body_text_result.get("result", {}).get("result", {}).get("value", "")
    screenshot_data = screenshot_result.get("result", {}).get("data", "")
    if not isinstance(snapshot, dict):
        snapshot = {"raw": snapshot}
    if not isinstance(body_text, str):
        body_text = ""
    payload = {
        "schema": "doke_cdp_capture.v1",
        "captured_at_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "debug_port": debug_port,
        "site_id": site_id,
        "target": {
            "id": target.get("id", ""),
            "url": target.get("url", ""),
            "title": target.get("title", ""),
            "type": target.get("type", ""),
        },
        "extracted_signals": extract_site_signals(site_id, body_text),
        "snapshot": snapshot,
        "artifacts": {
            "snapshot": str(artifact_dir / "snapshot.json"),
            "screenshot": str(artifact_dir / "screenshot.png"),
        },
    }
    (artifact_dir / "snapshot.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    if screenshot_data:
        (artifact_dir / "screenshot.png").write_bytes(base64.b64decode(screenshot_data))
    return payload


def capture(debug_port: int, url: str, artifact_dir: Path, timeout: float, site_id: str = "") -> dict:
    last_error: Exception | None = None
    for attempt in range(2):
        try:
            return capture_once(debug_port, url, artifact_dir, timeout, site_id, force_new=attempt > 0)
        except Exception as exc:
            last_error = exc
            message = str(exc).lower()
            retryable = "websocket closed" in message or "connection reset" in message
            if not retryable or attempt == 1:
                break
            time.sleep(0.5)
    raise RuntimeError(str(last_error or "cdp_capture_failed"))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--debug-port", type=int, required=True)
    parser.add_argument("--url", default="")
    parser.add_argument("--site-id", default="")
    parser.add_argument("--artifact-dir", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--json", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        payload = capture(args.debug_port, args.url, args.artifact_dir, args.timeout, args.site_id)
    except Exception as exc:
        print(f"cdp_capture_error: {exc}")
        return 2
    if args.json:
        print(json.dumps(payload, ensure_ascii=False, sort_keys=True))
    else:
        print(f"cdp_capture_ok artifact_dir={args.artifact_dir}")
        print(f"snapshot={args.artifact_dir / 'snapshot.json'}")
        print(f"screenshot={args.artifact_dir / 'screenshot.png'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
