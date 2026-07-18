import argparse
import json
import os
import sys


ALLOWED_CAPABILITIES = {
    "native_fingerprint",
    "native_proxy",
    "native_geoip",
    "native_humanize",
}

NATIVE_FLAGS = {"fingerprint", "proxy", "geoip", "humanize"}
FALLBACK_FLAGS = {"fingerprint", "geoip", "proxy_auth"}
SURFACE_PRESETS = {"macos", "windows", "linux", "android", "ios"}
WEBDRIVER_POLICIES = {"hide", "default"}
DEVTOOLS_EXPOSURE = {"minimize", "fallback_required", "default"}


def load_json(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def ensure_object(value, name, errors):
    if not isinstance(value, dict):
        errors.append(f"{name} must be an object")
        return {}
    return value


def ensure_array(value, name, errors):
    if not isinstance(value, list):
        errors.append(f"{name} must be an array")
        return []
    return value


def normalize_capabilities(values, name, errors):
    out = []
    for value in ensure_array(values, name, errors):
        if not isinstance(value, str) or not value.strip():
            errors.append(f"{name} must contain non-empty strings")
            continue
        capability = value.strip()
        if capability not in ALLOWED_CAPABILITIES:
            errors.append(f"{name} contains unknown capability: {capability}")
            continue
        if capability not in out:
            out.append(capability)
    return out


def validate_brand_array(value, name, errors):
    brands = ensure_array(value, name, errors)
    for idx, brand in enumerate(brands):
        item = ensure_object(brand, f"{name}[{idx}]", errors)
        if not isinstance(item.get("brand"), str) or not item.get("brand", "").strip():
            errors.append(f"{name}[{idx}].brand must be a non-empty string")
        if not isinstance(item.get("version"), str) or not item.get("version", "").strip():
            errors.append(f"{name}[{idx}].version must be a non-empty string")


def validate_ua_client_hints(value, errors):
    hints = ensure_object(value, "fingerprint.ua_client_hints", errors)
    validate_brand_array(hints.get("brands"), "fingerprint.ua_client_hints.brands", errors)
    validate_brand_array(hints.get("fullVersionList"), "fingerprint.ua_client_hints.fullVersionList", errors)
    for key in ("fullVersion", "platform", "platformVersion", "architecture", "bitness", "model"):
        if key in hints and not isinstance(hints.get(key), str):
            errors.append(f"fingerprint.ua_client_hints.{key} must be a string")
    if "mobile" in hints and not isinstance(hints.get("mobile"), bool):
        errors.append("fingerprint.ua_client_hints.mobile must be a boolean")


def validate_rendering_noise_section(value, name, errors):
    section = ensure_object(value, name, errors)
    if "enabled" in section and not isinstance(section.get("enabled"), bool):
        errors.append(f"{name}.enabled must be a boolean")
    if not isinstance(section.get("strategy"), str) or section.get("strategy") != "stable_noise":
        errors.append(f"{name}.strategy must be stable_noise")
    seed = section.get("seed")
    if not isinstance(seed, str) or not seed.strip():
        errors.append(f"{name}.seed must be a non-empty string")


def validate_surface_section(value, name, errors, require_seed):
    section = ensure_object(value, name, errors)
    if "enabled" in section and not isinstance(section.get("enabled"), bool):
        errors.append(f"{name}.enabled must be a boolean")
    if not isinstance(section.get("strategy"), str) or section.get("strategy") != "platform_preset":
        errors.append(f"{name}.strategy must be platform_preset")
    preset = section.get("preset")
    if not isinstance(preset, str) or preset not in SURFACE_PRESETS:
        errors.append(f"{name}.preset must be one of {','.join(sorted(SURFACE_PRESETS))}")
    if require_seed:
        seed = section.get("seed")
        if not isinstance(seed, str) or not seed.strip():
            errors.append(f"{name}.seed must be a non-empty string")


def validate_alignment(value, errors):
    alignment = ensure_object(value, "alignment", errors)
    for key in ("language", "timezone"):
        if key in alignment and not isinstance(alignment.get(key), str):
            errors.append(f"alignment.{key} must be a string")

    geo = ensure_object(alignment.get("geo"), "alignment.geo", errors)
    if "enabled" in geo and not isinstance(geo.get("enabled"), bool):
        errors.append("alignment.geo.enabled must be a boolean")
    for key in ("latitude", "longitude", "accuracy"):
        if key in geo and not isinstance(geo.get(key), (int, float)):
            errors.append(f"alignment.geo.{key} must be a number")
    for key in ("latitude_arg", "longitude_arg", "accuracy_arg"):
        if key in geo and not isinstance(geo.get(key), str):
            errors.append(f"alignment.geo.{key} must be a string")

    proxy = ensure_object(alignment.get("proxy"), "alignment.proxy", errors)
    if "enabled" in proxy and not isinstance(proxy.get("enabled"), bool):
        errors.append("alignment.proxy.enabled must be a boolean")
    if "port" in proxy and not isinstance(proxy.get("port"), int):
        errors.append("alignment.proxy.port must be an integer")
    for key in ("scheme", "host", "port_arg"):
        if key in proxy and not isinstance(proxy.get(key), str):
            errors.append(f"alignment.proxy.{key} must be a string")


def validate_automation(value, errors):
    automation = ensure_object(value, "automation", errors)
    webdriver_policy = automation.get("webdriver_policy")
    if not isinstance(webdriver_policy, str) or webdriver_policy not in WEBDRIVER_POLICIES:
        errors.append(f"automation.webdriver_policy must be one of {','.join(sorted(WEBDRIVER_POLICIES))}")
    devtools_exposure = automation.get("devtools_exposure")
    if not isinstance(devtools_exposure, str) or devtools_exposure not in DEVTOOLS_EXPOSURE:
        errors.append(f"automation.devtools_exposure must be one of {','.join(sorted(DEVTOOLS_EXPOSURE))}")
    for key in ("cdp_side_effect_guard", "debug_port_required", "startup_automation_controlled"):
        if key in automation and not isinstance(automation.get(key), bool):
            errors.append(f"automation.{key} must be a boolean")


def validate_runtime(obj, args):
    errors = []
    if obj.get("schema") != "doke_profile_runtime.v1":
        errors.append("schema must be doke_profile_runtime.v1")

    if not str(obj.get("profile_id") or "").strip():
        errors.append("profile_id must be non-empty")

    fingerprint = ensure_object(obj.get("fingerprint"), "fingerprint", errors)
    geo = ensure_object(obj.get("geo"), "geo", errors)
    native = ensure_object(obj.get("native"), "native", errors)
    fallback = ensure_object(obj.get("fallback"), "fallback", errors)
    proxy = ensure_object(obj.get("proxy"), "proxy", errors)
    webrtc = ensure_object(obj.get("webrtc"), "webrtc", errors)
    rendering = ensure_object(obj.get("rendering"), "rendering", errors)
    surfaces = ensure_object(obj.get("surfaces"), "surfaces", errors)
    validate_alignment(obj.get("alignment"), errors)
    validate_automation(obj.get("automation"), errors)

    for key in ("seed", "hardware_concurrency", "device_memory_gb"):
        if key in fingerprint and not isinstance(fingerprint.get(key), int):
            errors.append(f"fingerprint.{key} must be an integer")
    if "touch_enabled" in fingerprint and not isinstance(fingerprint.get("touch_enabled"), bool):
        errors.append("fingerprint.touch_enabled must be a boolean")
    for key in ("resolution", "window_size", "device_scale_factor_arg", "hardware_concurrency_arg",
                "device_memory_gb_arg", "touch_events"):
        if key in fingerprint and not isinstance(fingerprint.get(key), str):
            errors.append(f"fingerprint.{key} must be a string")
    if fingerprint.get("touch_events") not in (None, "enabled"):
        errors.append("fingerprint.touch_events must be enabled when present")
    if "ua_client_hints" in fingerprint:
        validate_ua_client_hints(fingerprint.get("ua_client_hints"), errors)

    if "enabled" in geo and not isinstance(geo.get("enabled"), bool):
        errors.append("geo.enabled must be a boolean")
    for key in ("latitude", "longitude", "accuracy"):
        if key in geo and not isinstance(geo.get(key), (int, float)):
            errors.append(f"geo.{key} must be a number")

    requested = normalize_capabilities(native.get("requested"), "native.requested", errors)
    supported = normalize_capabilities(native.get("supported"), "native.supported", errors)
    missing = normalize_capabilities(native.get("missing"), "native.missing", errors)

    expected_missing = [capability for capability in requested if capability not in supported]
    if missing != expected_missing:
        errors.append("native.missing must match requested minus supported")

    for capability in args.require_supported:
        if capability not in ALLOWED_CAPABILITIES:
            errors.append(f"unknown required capability: {capability}")
            continue
        if capability not in supported:
            errors.append(f"missing required supported capability: {capability}")

    for key in ("fingerprint", "proxy", "geoip", "humanize"):
        if key in native and not isinstance(native.get(key), bool):
            errors.append(f"native.{key} must be a boolean")
    for key in ("fingerprint", "geoip", "proxy_auth"):
        if key in fallback and not isinstance(fallback.get(key), bool):
            errors.append(f"fallback.{key} must be a boolean")

    if native.get("fingerprint") and fallback.get("fingerprint"):
        errors.append("native.fingerprint and fallback.fingerprint cannot both be true")
    if native.get("geoip") and fallback.get("geoip"):
        errors.append("native.geoip and fallback.geoip cannot both be true")

    for key in args.require_native:
        if not native.get(key):
            errors.append(f"native.{key} must be true")
    for key in args.forbid_native:
        if native.get(key):
            errors.append(f"native.{key} must be false")
    for key in args.require_fallback:
        if not fallback.get(key):
            errors.append(f"fallback.{key} must be true")
    for key in args.forbid_fallback:
        if fallback.get(key):
            errors.append(f"fallback.{key} must be false")

    if "enabled" in proxy and not isinstance(proxy.get("enabled"), bool):
        errors.append("proxy.enabled must be a boolean")
    if "port" in proxy and not isinstance(proxy.get("port"), int):
        errors.append("proxy.port must be an integer")

    if "ip_handling_policy" in webrtc and not isinstance(webrtc.get("ip_handling_policy"), str):
        errors.append("webrtc.ip_handling_policy must be a string")
    if webrtc.get("ip_handling_policy") not in (None, "disable_non_proxied_udp", "default_public_interface_only",
                                                "default_public_and_private_interfaces"):
        errors.append("webrtc.ip_handling_policy is not an allowed value")
    if "proxy_only" in webrtc and not isinstance(webrtc.get("proxy_only"), bool):
        errors.append("webrtc.proxy_only must be a boolean")

    for key in ("canvas", "webgl", "audio"):
        validate_rendering_noise_section(rendering.get(key), f"rendering.{key}", errors)

    validate_surface_section(surfaces.get("plugins"), "surfaces.plugins", errors, require_seed=False)
    validate_surface_section(surfaces.get("mime_types"), "surfaces.mime_types", errors, require_seed=False)
    validate_surface_section(surfaces.get("fonts"), "surfaces.fonts", errors, require_seed=True)
    validate_surface_section(surfaces.get("client_rects"), "surfaces.client_rects", errors, require_seed=True)

    return errors


def main():
    parser = argparse.ArgumentParser(description="Validate a Doke profile runtime config JSON file.")
    parser.add_argument("runtime_config", help="Path to Doke/runtime.json")
    parser.add_argument(
        "--require-supported",
        action="append",
        default=[],
        choices=sorted(ALLOWED_CAPABILITIES),
        help="Require a native capability listed in native.supported. Can be passed more than once.",
    )
    parser.add_argument(
        "--require-native",
        action="append",
        default=[],
        choices=sorted(NATIVE_FLAGS),
        help="Require native.<name> to be true. Can be passed more than once.",
    )
    parser.add_argument(
        "--forbid-native",
        action="append",
        default=[],
        choices=sorted(NATIVE_FLAGS),
        help="Require native.<name> to be false. Can be passed more than once.",
    )
    parser.add_argument(
        "--require-fallback",
        action="append",
        default=[],
        choices=sorted(FALLBACK_FLAGS),
        help="Require fallback.<name> to be true. Can be passed more than once.",
    )
    parser.add_argument(
        "--forbid-fallback",
        action="append",
        default=[],
        choices=sorted(FALLBACK_FLAGS),
        help="Require fallback.<name> to be false. Can be passed more than once.",
    )
    parser.add_argument("--json", action="store_true", help="Print machine-readable result")
    args = parser.parse_args()

    result = {
        "ok": False,
        "runtime_config": args.runtime_config,
        "errors": [],
        "required_supported": args.require_supported,
        "required_native": args.require_native,
        "forbidden_native": args.forbid_native,
        "required_fallback": args.require_fallback,
        "forbidden_fallback": args.forbid_fallback,
    }

    if not os.path.exists(args.runtime_config):
        result["errors"].append("runtime_config_missing")
    elif not os.path.isfile(args.runtime_config):
        result["errors"].append("runtime_config_not_file")
    else:
        try:
            obj = load_json(args.runtime_config)
        except Exception as exc:
            result["errors"].append(f"runtime_config_invalid_json:{exc}")
        else:
            result["profile_id"] = obj.get("profile_id", "")
            result["errors"].extend(validate_runtime(obj, args))

    result["ok"] = not result["errors"]
    if args.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    else:
        if result["ok"]:
            print(f"runtime_ok profile={result.get('profile_id', '')}")
        else:
            print("runtime_failed")
            for error in result["errors"]:
                print(f"- {error}")

    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
