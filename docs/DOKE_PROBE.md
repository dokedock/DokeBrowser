# Doke Chromium Probe Contract

This document defines the minimum native handshake a self-developed `doke_chromium` binary must expose before DokeBrowser treats it as a first-class engine.

## Command

```bash
/absolute/path/to/doke_chromium --doke-probe
```

The command must:

- Exit with code `0`.
- Finish within the Agent probe timeout. The current default is 12 seconds to allow real Chromium cold starts on macOS.
- Print one JSON object to stdout or stderr. Extra log lines before or after the JSON object are tolerated, but the JSON object should stay compact.

## JSON Schema

Minimum valid response:

```json
{
  "probe_protocol": 1,
  "version": "Doke Chromium 0.1.0",
  "capabilities": []
}
```

Fields:

- `probe_protocol`: current protocol version. Use `1`.
- `version`: human-readable Doke Chromium version string.
- `capabilities`: native features actually implemented by the binary.

DokeBrowser compares this list with the current Profile's `engine_config_json.features`. If a Profile declares a native feature that the binary does not report, `engine.probe.result` includes it in `missing_native_capabilities`.

Allowed capabilities:

- `native_fingerprint`
- `native_proxy`
- `native_geoip`
- `native_humanize`

The first Chromium patch, `patches/chromium/0001-doke-probe-contract.patch`, intentionally reports an empty capability list. Add a capability only after the matching native patch is implemented and covered by the detection baseline.

`patches/chromium/0003-doke-runtime-ua-lang-switches.patch` starts the native UA path by applying runtime `fingerprint.user_agent` and `fingerprint.language` as Chrome command-line switches when those switches are absent. This is not enough to report `native_fingerprint`; UA-CH and JavaScript-visible metadata still need dedicated patches.

`Doke/runtime.json` also includes `fingerprint.ua_client_hints` when the Profile UA can be parsed. `patches/chromium/0004-doke-runtime-ua-client-hints-ingress.patch` reads and validates this metadata during startup, then bridges it into Doke UA-CH command-line switches. It still does not report `native_fingerprint` until the values pass the detection baseline.

`patches/chromium/0005-doke-runtime-ua-client-hints-override.patch` overrides Chromium's `GetUserAgentMetadata()` from those Doke UA-CH switches. This avoids blocking file reads from the UA utility path while still affecting UA-CH surfaces.

`Doke/runtime.json` includes `webrtc.ip_handling_policy`; `patches/chromium/0006-doke-runtime-webrtc-policy.patch` applies it as `--force-webrtc-ip-handling-policy` when absent. This is the first WebRTC native step, but it should not be treated as a full WebRTC leak fix until real ICE candidate checks pass.

For screen/device metrics, `Doke/runtime.json` includes Chrome-ready `fingerprint.window_size`, `fingerprint.device_scale_factor_arg`, and `fingerprint.touch_events`; `patches/chromium/0007-doke-runtime-screen-device-switches.patch` applies these as startup switches when absent.

For JS-visible hardware metrics, `Doke/runtime.json` includes `fingerprint.hardware_concurrency_arg` and `fingerprint.device_memory_gb_arg`; `patches/chromium/0008-doke-runtime-hardware-switches.patch` forwards those values as Doke-specific switches and `patches/chromium/0009-doke-blink-hardware-overrides.patch` wires them into Blink's `navigator.hardwareConcurrency` and `navigator.deviceMemory` paths. `native_fingerprint` still remains off until a real Doke Chromium build passes the detection baseline.

## Fallback

If `--doke-probe` is missing or invalid, the Agent falls back to:

```bash
/absolute/path/to/doke_chromium --version
```

This fallback only verifies version visibility. It does not prove native capability support.

## Acceptance

Run:

```bash
python3 tools/doke_probe_check.py /absolute/path/to/doke_chromium
```

Require one or more native capabilities:

```bash
python3 tools/doke_probe_check.py /absolute/path/to/doke_chromium \
  --require-capability native_fingerprint \
  --require-capability native_proxy
```

The checker validates:

- The binary path exists and is executable.
- `--doke-probe` exits successfully.
- A JSON object can be extracted from the command output.
- `probe_protocol` is `1`.
- `version` is non-empty.
- `capabilities` is an array of known capability strings.
- Every requested `--require-capability` is reported by the binary.
- `--version` returns a non-empty line as a fallback sanity check.

## Runtime Config

When a Doke Profile starts, the Agent writes `Doke/runtime.json` in the Profile data directory and passes it to the browser with `--doke-runtime-config=...`. The file uses schema `doke_profile_runtime.v1` and records the Profile fingerprint, structured UA-CH metadata, WebRTC policy, screen/device helper values, hardware metrics, Geo settings, requested/supported/missing native capabilities, fallback decisions, and non-secret proxy metadata.

The second Chromium patch, `patches/chromium/0002-doke-runtime-config-load.patch`, only reads and validates this file. It does not change fingerprint behavior or report additional native capabilities by itself.

Validate a generated runtime config:

```bash
python3 tools/doke_runtime_check.py /path/to/profile/Doke/runtime.json
```

Require a specific native/fallback decision:

```bash
python3 tools/doke_runtime_check.py /path/to/profile/Doke/runtime.json \
  --require-supported native_fingerprint \
  --require-native fingerprint \
  --forbid-native geoip \
  --require-fallback geoip
```

## Fake Binary

For local contract testing:

```bash
python3 tools/make_fake_doke.py /tmp/doke_chromium \
  --capability native_fingerprint \
  --capability native_proxy \
  --probe-log "native probe ready"
python3 tools/doke_probe_check.py /tmp/doke_chromium --require-capability native_fingerprint
```
