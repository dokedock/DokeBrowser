#!/usr/bin/env python3
"""Apply the Doke Chromium patch queue to a tiny fake Chromium tree."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


CHROME_MAIN = r'''#include "chrome/app/chrome_main.h"

#include <stdint.h>

#include <iostream>
#include <memory>
#include <optional>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/sampling_heap_profiler/poisson_allocation_sampler.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/app/chrome_main_delegate.h"
#include "chrome/app/startup_timestamps.h"

namespace {

std::optional<base::CommandLine>& GetInitialCommandLineStorage() {
  static base::NoDestructor<std::optional<base::CommandLine>>
      initial_command_line;
  return *initial_command_line;
}

}  // namespace

const base::CommandLine& GetInitialBrowserCommandLine() {
  CHECK(GetInitialCommandLineStorage().has_value());
  return GetInitialCommandLineStorage().value();
}

int ChromeMain(int argc, const char** argv) {
  base::CommandLine::Init(0, nullptr);

  base::CommandLine* command_line(base::CommandLine::ForCurrentProcess());

  // Capture the unpolluted command line snapshot in the browser process.
  // This must happen immediately after CommandLine::Init to ensure we capture
  // the state before any internal programmatic mutations.
  GetInitialCommandLineStorage().emplace(*command_line);

  ChromeMainDelegate chrome_main_delegate;
  return 0;
}
'''


USER_AGENT_UTILS = r'''#include "components/embedder_support/user_agent_utils.h"

#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/system/sys_info.h"
#include "base/version.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"

namespace embedder_support {
namespace {

#if BUILDFLAG(IS_WIN)

// The registry key where the UniversalApiContract version value can be read
// from.
constexpr wchar_t kWindowsRuntimeWellKnownContractsRegKeyName[] =
    L"SOFTWARE\\Microsoft\\WindowsRuntime\\WellKnownContracts";
#endif
}  // namespace

blink::UserAgentMetadata GetUserAgentMetadata(bool only_low_entropy_ch) {
  blink::UserAgentMetadata metadata;
  metadata.mobile = GetMobileBitForUAMetadata();
  metadata.platform = GetPlatformForUAMetadata();

  // For users providing a valid user-agent override via the command line:
  // If kUACHOverrideBlank is enabled, set user-agent metadata with the
  // default blank values, otherwise return the default UserAgentMetadata values
  return metadata;
}
}  // namespace embedder_support
'''


NAVIGATOR_BASE = r'''// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/execution_context/navigator_base.h"

#include "base/feature_list.h"
#include "build/build_config.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/navigator_concurrent_hardware.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

namespace blink {

namespace {

String GetReducedNavigatorPlatform() {
#if BUILDFLAG(IS_ANDROID)
  return "Linux armv81";
#elif BUILDFLAG(IS_MAC)
  return "MacIntel";
#else
  return "Linux x86_64";
#endif
}

}  // namespace

NavigatorBase::NavigatorBase(ExecutionContext* context)
    : NavigatorLanguage(context), ExecutionContextClient(context) {}

unsigned int NavigatorBase::hardwareConcurrency() const {
  unsigned int hardware_concurrency =
      NavigatorConcurrentHardware::hardwareConcurrency();

  probe::ApplyHardwareConcurrencyOverride(
      probe::ToCoreProbeSink(GetExecutionContext()), hardware_concurrency);
  return hardware_concurrency;
}

}  // namespace blink
'''


NAVIGATOR_DEVICE_MEMORY = r'''// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/frame/navigator_device_memory.h"

#include "third_party/blink/public/common/device_memory/approximated_device_memory.h"
#include "third_party/blink/public/mojom/use_counter/metrics/web_feature.mojom-shared.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"

namespace blink {

float NavigatorDeviceMemory::deviceMemory() const {
  return ApproximatedDeviceMemory::GetApproximatedDeviceMemory();
}

}  // namespace blink
'''


NAVIGATOR_CC = r'''/*
 *  Copyright (C) 2000 Harri Porten (porten@kde.org)
 */

#include "third_party/blink/renderer/core/frame/navigator.h"

#include "third_party/blink/renderer/bindings/core/v8/script_controller.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/execution_context/navigator_base.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/loader/frame_loader.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/platform/language.h"

namespace blink {

Navigator::Navigator(ExecutionContext* context) : NavigatorBase(context) {}

String Navigator::productSub() const {
  return "20030107";
}

String Navigator::vendor() const {
  return "Google Inc.";
}

String Navigator::vendorSub() const {
  return "";
}

String Navigator::platform() const {
  return NavigatorBase::platform();
}

bool Navigator::cookieEnabled() const {
  return true;
}

bool Navigator::webdriver() const {
  if (RuntimeEnabledFeatures::AutomationControlledEnabled())
    return true;

  bool automation_enabled = false;
  probe::ApplyAutomationOverride(GetExecutionContext(), automation_enabled);
  return automation_enabled;
}

String Navigator::GetAcceptLanguages() {
  return DefaultLanguage();
}

}  // namespace blink
'''


RUNTIME_FEATURES_CC = r'''#include "content/child/runtime_features.h"

#include <string>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/strings/string_number_conversions.h"
#include "content/public/common/content_switches.h"
#include "third_party/blink/public/platform/web_runtime_features.h"

using blink::WebRuntimeFeatures;

namespace {

struct SwitchToFeatureMap {
  // The enabler function defined in web_runtime_features.cc.
  void (*feature_enabler)(bool);
  // The switch to check for on command line.
  const char* switch_name;
  // This is the desired state for the runtime feature if the
  // switch exists on command line.
  bool target_enabled_state;
};

// Sets blink runtime features controlled by command line switches.
void SetRuntimeFeaturesFromCommandLine(const base::CommandLine& command_line) {
  // To add a new switch-controlled runtime feature, add a new
  // SwitchToFeatureMap entry to the initializer list below.
  using wrf = WebRuntimeFeatures;
  const SwitchToFeatureMap switchToFeatureMapping[] = {
      {wrf::EnableAutomationControlled, switches::kEnableAutomation, true},
      {wrf::EnableAutomationControlled, switches::kHeadless, true},
      {wrf::EnableAutomationControlled, switches::kRemoteDebuggingPipe, true},
  };

  for (const auto& mapping : switchToFeatureMapping) {
    if (command_line.HasSwitch(mapping.switch_name)) {
      mapping.feature_enabler(mapping.target_enabled_state);
    }
  }

  // Set EnableAutomationControlled if the caller passes
  // --remote-debugging-port=0 on the command line. This means
  // the caller has requested an ephemeral port which is how ChromeDriver
  // launches the browser by default.
  // If the caller provides a specific port number, this is
  // more likely for attaching a debugger, so we should leave
  // EnableAutomationControlled unset to ensure the browser behaves as it does
  // when not under automation control.
  if (command_line.HasSwitch(switches::kRemoteDebuggingPort)) {
    std::string port_str =
        command_line.GetSwitchValueASCII(::switches::kRemoteDebuggingPort);
    int port;
    if (base::StringToInt(port_str, &port) && port == 0) {
      WebRuntimeFeatures::EnableAutomationControlled(true);
    }
  }
}

}  // namespace
'''


V8_RUNTIME_AGENT_IMPL_CC = r'''/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 */

#include "src/inspector/v8-runtime-agent-impl.h"

#include <inttypes.h>

#include <memory>

namespace v8_inspector {

namespace V8RuntimeAgentImplState {
static const char customObjectFormatterEnabled[] =
    "customObjectFormatterEnabled";
static const char maxCallStackSizeToCapture[] = "maxCallStackSizeToCapture";
static const char runtimeEnabled[] = "runtimeEnabled";
static const char contextBindings[] = "contextBindings";
static const char globalBindings[] = "globalBindings";
}  // namespace V8RuntimeAgentImplState

using protocol::Runtime::RemoteObject;

namespace {

template <typename ProtocolCallback>
class EvaluateCallbackWrapper : public EvaluateCallback {
 public:
  static std::shared_ptr<EvaluateCallback> wrap(
      std::unique_ptr<ProtocolCallback> callback) {
    return std::shared_ptr<EvaluateCallback>(
        new EvaluateCallbackWrapper(std::move(callback)));
  }
};

Response getWrapOptions(std::optional<bool> returnByValue,
                        std::optional<bool> generatePreview,
                        std::unique_ptr<protocol::Runtime::SerializationOptions>
                            maybeSerializationOptions,
                        v8::Isolate* isolate,
                        std::unique_ptr<WrapOptions>* result) {
  if (maybeSerializationOptions) {
    return Response::Success();
  }

  if (returnByValue.value_or(false)) {
    *result = std::make_unique<WrapOptions>(WrapOptions{WrapMode::kJson});
    return Response::Success();
  }
  if (generatePreview.value_or(false)) {
    *result = std::make_unique<WrapOptions>(WrapOptions{WrapMode::kPreview});
    return Response::Success();
  }
  *result = std::make_unique<WrapOptions>(WrapOptions{WrapMode::kIdOnly});
  return Response::Success();
}

}  // namespace
}  // namespace v8_inspector
'''


V8_CONSOLE_MESSAGE_CC = r'''// Copyright 2016 the V8 project authors. All rights reserved.

#include "src/inspector/v8-console-message.h"

#include <span>

#include "include/v8-container.h"
#include "include/v8-context.h"
#include "include/v8-inspector.h"
#include "include/v8-microtask-queue.h"
#include "include/v8-primitive-object.h"

namespace v8_inspector {

namespace {

String16 consoleAPITypeValue(ConsoleAPIType type) {
  switch (type) {
    case ConsoleAPIType::kLog:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Log;
  }
}

}  // namespace

void V8ConsoleMessage::reportToFrontend(protocol::Runtime::Frontend* frontend,
                                        V8InspectorSessionImpl* session,
                                        bool generatePreview) const {
  int contextGroupId = session->contextGroupId();
  V8InspectorImpl* inspector = session->inspector();
  // Protect against reentrant debugger calls via interrupts.
  v8::debug::PostponeInterruptsScope no_interrupts(inspector->isolate());
}

}  // namespace v8_inspector
'''


FIXTURES = {
    "chrome/app/chrome_main.cc": CHROME_MAIN,
    "components/embedder_support/user_agent_utils.cc": USER_AGENT_UTILS,
    "third_party/blink/renderer/core/execution_context/navigator_base.cc": NAVIGATOR_BASE,
    "third_party/blink/renderer/core/frame/navigator_device_memory.cc": NAVIGATOR_DEVICE_MEMORY,
    "third_party/blink/renderer/core/frame/navigator.cc": NAVIGATOR_CC,
    "content/child/runtime_features.cc": RUNTIME_FEATURES_CC,
    "v8/src/inspector/v8-runtime-agent-impl.cc": V8_RUNTIME_AGENT_IMPL_CC,
    "v8/src/inspector/v8-console-message.cc": V8_CONSOLE_MESSAGE_CC,
}


REQUIRED_TOKENS = {
    "chrome/app/chrome_main.cc": [
        "doke-probe",
        "doke-runtime-config",
        "doke_profile_runtime.v1",
        "user-agent",
        "ua_client_hints",
        "force-webrtc-ip-handling-policy",
        "window-size",
        "force-device-scale-factor",
        "touch-events",
        "doke-hardware-concurrency",
        "doke-device-memory-gb",
        "doke-canvas-noise-seed",
        "doke-webgl-noise-seed",
        "doke-audio-noise-seed",
        "AppendRenderingNoiseSwitchIfAbsent",
        "doke-plugins-preset",
        "doke-mime-types-preset",
        "doke-fonts-preset",
        "doke-fonts-seed",
        "doke-client-rects-preset",
        "doke-client-rects-seed",
        "AppendPlatformPresetSwitchIfAbsent",
        "AppendSurfaceSeedSwitchIfAbsent",
        "doke-alignment-language",
        "doke-timezone-id",
        "doke-geo-latitude",
        "doke-geo-longitude",
        "doke-geo-accuracy",
        "doke-proxy-scheme",
        "doke-proxy-host",
        "doke-proxy-port",
        "AppendAlignmentSwitchesIfAbsent",
        "doke-webdriver-policy",
        "doke-devtools-exposure",
        "doke-cdp-side-effect-guard",
        "doke-debug-port-required",
        "doke-startup-automation-controlled",
        "AppendAutomationSwitchesIfAbsent",
        "AppendBoolSwitchIfAbsent",
    ],
    "components/embedder_support/user_agent_utils.cc": [
        "GetDokeUserAgentMetadataOverride",
        "brand_version_list",
        "brand_full_version_list",
        "form_factors",
    ],
    "third_party/blink/renderer/core/execution_context/navigator_base.cc": [
        "GetDokeHardwareConcurrencyOverride",
        "navigator_base",
        "doke-hardware-concurrency",
    ],
    "third_party/blink/renderer/core/frame/navigator_device_memory.cc": [
        "GetDokeDeviceMemoryOverride",
        "doke-device-memory-gb",
        "ApproximatedDeviceMemory",
    ],
    "third_party/blink/renderer/core/frame/navigator.cc": [
        "GetDokeWebdriverOverride",
        "doke-webdriver-policy",
        "Navigator::webdriver",
        "RuntimeEnabledFeatures::AutomationControlledEnabled",
    ],
    "content/child/runtime_features.cc": [
        "ShouldHideDokeWebdriver",
        "ApplyDokeAutomationControlledPolicy",
        "EnableAutomationControlled(false)",
        "EnableAutomationControlled(!doke_hide_webdriver)",
    ],
    "v8/src/inspector/v8-runtime-agent-impl.cc": [
        "DOKE_CDP_SIDE_EFFECT_GUARD",
        "IsDokeCdpSideEffectGuardEnabled",
        "WrapMode::kIdOnly",
    ],
    "v8/src/inspector/v8-console-message.cc": [
        "DOKE_CDP_SIDE_EFFECT_GUARD",
        "IsDokeCdpSideEffectGuardEnabled",
        "generatePreview = false",
    ],
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def read_series(patch_dir: Path) -> list[str]:
    series_path = patch_dir / "series"
    patches: list[str] = []
    for line in series_path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            patches.append(stripped)
    return patches


def write_fixtures(root: Path) -> None:
    for relative_path, contents in FIXTURES.items():
        path = root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")


def apply_patches(root: Path, patch_dir: Path, patches: list[str]) -> None:
    for patch_name in patches:
        patch_path = patch_dir / patch_name
        result = subprocess.run(
            ["git", "apply", str(patch_path)],
            cwd=root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if result.returncode != 0:
            sys.stderr.write(f"failed_patch={patch_name}\n")
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            raise SystemExit(result.returncode)


def verify_tokens(root: Path) -> None:
    missing: list[str] = []
    for relative_path, tokens in REQUIRED_TOKENS.items():
        contents = (root / relative_path).read_text(encoding="utf-8")
        for token in tokens:
            if token not in contents:
                missing.append(f"{relative_path}:{token}")
    if missing:
        for item in missing:
            sys.stderr.write(f"missing_token={item}\n")
        raise SystemExit(1)


def run(args: argparse.Namespace) -> int:
    patch_dir = args.patch_dir.resolve()
    patches = read_series(patch_dir)

    if args.keep:
        root = Path(tempfile.mkdtemp(prefix="doke_patch_apply_smoke_"))
        write_fixtures(root)
        apply_patches(root, patch_dir, patches)
        verify_tokens(root)
        print(f"doke_patch_apply_smoke_ok patches={len(patches)} tree={root}")
        return 0

    with tempfile.TemporaryDirectory(prefix="doke_patch_apply_smoke_") as root_text:
        root = Path(root_text)
        write_fixtures(root)
        apply_patches(root, patch_dir, patches)
        verify_tokens(root)
        print(f"doke_patch_apply_smoke_ok patches={len(patches)}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--patch-dir",
        type=Path,
        default=repo_root() / "patches" / "chromium",
        help="Path to the Chromium patch queue directory.",
    )
    parser.add_argument(
        "--keep",
        action="store_true",
        help="Keep the generated fake Chromium tree and print its path.",
    )
    return parser.parse_args()


if __name__ == "__main__":
    raise SystemExit(run(parse_args()))
