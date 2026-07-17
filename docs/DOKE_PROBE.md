# Doke Chromium Probe Contract

This document defines the minimum native handshake a self-developed `doke_chromium` binary must expose before DokeBrowser treats it as a first-class engine.

## Command

```bash
/absolute/path/to/doke_chromium --doke-probe
```

The command must:

- Exit with code `0`.
- Finish quickly. The Agent currently uses a short timeout.
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

Allowed capabilities:

- `native_fingerprint`
- `native_proxy`
- `native_geoip`
- `native_humanize`

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

The checker validates:

- The binary path exists and is executable.
- `--doke-probe` exits successfully.
- A JSON object can be extracted from the command output.
- `probe_protocol` is `1`.
- `version` is non-empty.
- `capabilities` is an array of known capability strings.
- `--version` returns a non-empty line as a fallback sanity check.

