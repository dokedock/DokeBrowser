# DokeBrowser 开发文档

## 产品定位

DokeBrowser 的新目标是“控制台 + 自研 Doke Chromium 内核”：

- DokeBrowser 负责 Profile、代理池、VPN、日志、批量操作、数据落库和桌面控制台。
- Agent 负责统一启动、停止和监控浏览器进程，并把运行状态通过 IPC 回传给控制台。
- 浏览器内核主线改为自研 `doke_chromium`：基于官方 Chromium / ungoogled-chromium / CEF 可行路线做源码级指纹与网络补丁。
- `system_chrome` 只保留为开发期 fallback，不作为最终高通过率方案。
- CloakBrowser 不再作为可选内核运行项，只作为公开竞品、公开 wrapper/API 设计和检测基准参考。

这个路线的核心判断是：完整商业产品不能长期依赖 CloakBrowser 二进制授权和供应节奏。我们可以参考其公开思路和公开 wrapper 代码，但核心浏览器能力必须由 DokeBrowser 自己掌握。

## 外部参考与合法边界

- CloakBrowser: https://github.com/CloakHQ/CloakBrowser
  - 可参考其公开 README、公开 wrapper、启动参数组织、Playwright/Puppeteer 兼容体验、`geoip`、`humanize`、persistent context、代理配置和检测基准。
  - 不复制、不逆向、不反编译、不修改、不打包其专有 CloakBrowser Chromium 二进制。
  - 不把它的未公开 C++ 补丁当成我们的实现来源。
- CloakBrowser Binary License: https://github.com/CloakHQ/CloakBrowser/blob/main/BINARY-LICENSE.md
  - wrapper 源码是 MIT；CloakBrowser Chromium 二进制有单独许可。
  - 默认不允许重新分发、转售、打包进第三方产品，也不允许逆向或修改二进制。
- 可参考的开源基础：
  - Chromium / ungoogled-chromium：作为自研内核基线。
  - CEF：作为后续嵌入式窗口和网络栈控制方案。
  - XChrome / VirtualBrowser：作为 CDP 注入、Profile 管理和检测项参考。

## 架构边界

### DokeBrowser 控制台层

控制台层继续由 `src/app` 负责：

- Profile 列表、分组、搜索、勾选和批量操作
- 基础信息、代理、VPN、日志等 Tabs
- 代理池导入、健康检测、分配、释放、换一个
- SQLite 落库和历史记录
- 向 Agent 发送统一 IPC 消息

### Agent 运行层

Agent 继续由 `src/agent` 负责：

- 接收 `profile.start` / `profile.stop`
- 解析 Profile、代理、VPN、指纹、浏览器引擎参数
- 启动对应浏览器引擎
- 维护 profile_id 到进程/会话的映射
- 回传 `profile.status`、`log.line`、`proxy.test.result`、`vpn.status`

### 浏览器引擎层

引擎层应抽象成统一接口，避免把 `system_chrome`、`doke_chromium`、`cef` 的启动逻辑混在 `IpcServer` 里。

建议新增：

- `BrowserEngine.h`
- `SystemChromeEngine.cpp`
- `DokeChromiumEngine.cpp`
- `BrowserEngineFactory.cpp`

建议接口：

```cpp
struct BrowserLaunchRequest {
  QString profileId;
  QString profileName;
  QString dataDir;
  QString browserEngine;
  QString startUrl;
  QJsonObject proxy;
  QJsonObject fingerprint;
  QJsonObject engineOptions;
};

struct BrowserInstance {
  QString profileId;
  QString engine;
  int pid = 0;
  int debuggingPort = 0;
};

class BrowserEngine {
public:
  virtual ~BrowserEngine() = default;
  virtual QString id() const = 0;
  virtual bool start(const BrowserLaunchRequest& request, QString* error) = 0;
  virtual bool stop(const QString& profileId, QString* error) = 0;
};
```

## 引擎策略

### `system_chrome`

当前已实现的默认路线：

- 启动本机 Google Chrome / Chromium
- 每 Profile 独立 `--user-data-dir`
- 通过启动参数、CDP、MV3 扩展做基础指纹注入
- 代理认证使用 SOCKS inline 或本地 HTTP 代理映射

保留它的原因：

- 不引入新外部依赖
- 便于开发和 smoke test
- 可作为低成本 fallback

### `doke_chromium`

目标定位：

- 作为最终主力内核，由 DokeBrowser 自己维护。
- 基于官方 Chromium / ungoogled-chromium / CEF 中的可行路线建立源码树。
- 将反检测能力尽量下沉到浏览器源码层，而不是长期依赖 CDP/扩展注入。
- 保持 Playwright/CDP 兼容出口，方便自动化检测和后续对接。

第一阶段建议先做“外部自研 Chromium 进程”：

- Agent 通过 `QProcess` 启动自研 Chromium 二进制。
- Profile、代理、数据目录、debugging port 仍由 Agent 统一管理。
- 当前 `system_chrome` 的启动参数、代理映射、CDP 注入逻辑先作为过渡能力迁入 `DokeChromiumEngine`。
- 后续逐步把 UA-CH、WebRTC、Canvas、WebGL、Audio、fonts、plugins、screen、hardware、CDP detection、TLS/network timing 等能力迁到源码补丁层。

长期建议形成独立源码与补丁目录：

- `third_party/chromium` 或外部源码仓库：跟随 Chromium 上游。
- `patches/chromium/*.patch`：DokeBrowser 自研补丁。
- `docs/chromium-patches.md`：每个补丁的目的、影响面、检测项、回归方法。
- `tools/build_doke_chromium.*`：按平台构建自研 Chromium。

### `cef`

CEF 作为后续可选技术路线：

- 只有当我们需要完全内嵌浏览器窗口、深度控制网络栈、或摆脱外部浏览器依赖时再推进。
- 如果自研 Chromium 独立进程已能满足多实例和检测要求，CEF 不应成为近期主线。

## Profile 字段扩展

建议新增或预留以下字段：

- `browser_engine`: `system_chrome | doke_chromium | cef`
- `engine_config_json`: 保存引擎专属参数
- `fingerprint_seed`: 稳定指纹种子
- `start_url`: 默认打开地址
- `headless`: 是否无头
- `humanize_enabled`: DokeBrowser 行为拟真开关
- `geoip_enabled`: 是否按代理 IP 自动匹配时区/语言/定位

SQLite 迁移要保持向后兼容。旧 Profile 默认：

- `browser_engine = system_chrome`
- `humanize_enabled = false`
- `geoip_enabled = fingerprint_mode == follow_ip`

### `engine_config_json` 当前规范

`doke_chromium` 当前已支持以下配置：

```json
{
  "executable": "/absolute/path/to/doke_chromium",
  "binary_path": "/absolute/path/to/doke_chromium",
  "extra_args": [
    "--doke-example-flag=value"
  ],
  "features": {
    "native_fingerprint": false,
    "native_proxy": false,
    "native_geoip": false,
    "native_humanize": false
  }
}
```

- `executable` / `binary_path`: 单 Profile 指定 Doke Chromium 二进制路径；优先级高于 `DOKE_CHROMIUM_PATH` 和 PATH 查找。
- `extra_args`: Doke Chromium 专属启动参数，会插入到最终 URL 之前。
- `features`: 声明后续哪些能力由自研 Chromium 原生补丁承载。当前已接入 `native_fingerprint` 和 `native_geoip` 的 fallback 分流；`native_proxy` 和 `native_humanize` 仍为预留。
- UI 已提供“刷新 / 检测”入口；检测会通过 `engine.probe` 按当前 Profile 的 `engine_config_json` 验证 Doke Chromium 路径。Doke 路径支持从本地文件选择器写入。

## IPC 规划

`profile.start` 继续作为入口，但需要增加引擎字段：

```json
{
  "type": "profile.start",
  "profile_id": "...",
  "browser_engine": "doke_chromium",
  "engine_options": {
    "headless": false,
    "humanize": true,
    "geoip": true,
    "debugging_port": 0
  }
}
```

建议新增能力查询：

```json
{ "type": "engine.list" }
```

当前还支持按 Profile 配置检测；回包带 `profile_id` 时，App 会按 Profile 保存检测结果：

```json
{ "type": "engine.probe", "profile_id": "...", "browser_engine": "doke_chromium", "engine_config_json": "{...}" }
```

返回：

```json
{
  "type": "engine.list.result",
  "engines": [
    { "id": "system_chrome", "available": true },
    { "id": "doke_chromium", "available": false, "error": "binary_not_found" }
  ]
}
```

## 自研 Doke Chromium 里程碑

### M1: 文档与配置

- 更新产品目标、交接文档、开发文档
- 明确许可边界
- 增加 `doke_chromium` 设计

### M2: 数据模型与 UI

- `profiles` 增加 `browser_engine`、`engine_config_json` 等字段
- 基础信息 Tab 增加“浏览器内核”选择，先展示 `system_chrome` 和 `doke_chromium`
- Doke Chromium 专属设置先放在高级折叠区
- UI 已支持编辑 Doke 二进制路径、额外启动参数和原生能力开关

### M3: Agent 引擎抽象

- 从 `IpcServer` 拆出 `BrowserEngine` 接口
- 新增 `BrowserEngineFactory` 统一处理引擎 ID、可执行文件探测、能力列表
- 当前 Chrome 启动参数组装已迁入 `SystemChromeEngine::buildArguments`
- 当前代理认证/指纹注入临时扩展生成已迁入 `SystemChromeEngine::createProfileExtension`
- 当前 CDP attach 轮询、`webSocketDebuggerUrl` 解析和 `CdpClient` 创建已迁入 `SystemChromeEngine::attachCdpWhenReady`
- 当前浏览器进程创建、stdout/stderr 日志、错误状态、兼容重试、running/stopped/crashed 状态已迁入 `SystemChromeEngine::launchProcess`
- 当前 `DokeChromiumEngine` 已有独立 `buildArguments` / `launchProcess` 入口；`profile.start` 已按 `system_chrome` / `doke_chromium` 分流
- Agent 核心已收拢为 `dokebrowser_agent_core` 静态库，`dokebrowser_agent` 与自动化测试共用同一套核心实现
- `OpenVpnManager` 已从 `IpcServer` 拆出，负责 OpenVPN 启停、SOCKS 参数、临时认证文件、进程状态和日志转发
- `ProfileLaunchConfig` 已从 `IpcServer` 拆出，负责 `profile.start` 解析、代理启动参数、Profile 数据目录、debug port 分配和窗口尺寸参数
- `ProfileRuntimeManager` 已从 `IpcServer` 拆出，负责 Profile 浏览器进程、CDP、临时扩展、代理映射和 start/stop 运行态编排
- `ProxyTestRunner` 已从 `IpcServer` 拆出，统一处理 `proxy.test` / `proxy_pool.test` 的请求解析、校验、URL fallback、网络重试和结果组装
- `IpcServer` 已瘦身为 IPC 路由层，主要负责 hello、engine.list、消息分发和统一回包
- 已新增 `dokebrowser_engine_config` 自动化测试，覆盖 Doke Chromium 配置解析、二进制路径优先级、native feature 开关和 extra args 顺序
- 已新增 `dokebrowser_profile_launch_config` 自动化测试，覆盖启动配置解析、内核 ID 归一化、代理参数和窗口尺寸参数
- 已新增 `dokebrowser_profile_runtime_manager` 自动化测试，覆盖运行态早期错误路径和状态/日志回调
- 已新增 `dokebrowser_proxy_test_runner` 自动化测试，覆盖代理测试请求解析、错误校验和 fallback URL 规则
- 已新增 `dokebrowser_openvpn_manager` 自动化测试，覆盖 OpenVPN 请求解析、错误校验和参数组装
- 保持现有 smoke test 通过

### M4: Doke Chromium 进程启动

- 新增 `DokeChromiumEngine`
- 支持配置自研 Chromium 二进制路径
- 支持 profile data dir、proxy、headless、geoip、humanize、start_url
- 回传 debugging endpoint、pid、运行状态和浏览器日志
- `engine_config_json.executable` / `binary_path` 可指定单 Profile 二进制路径
- `engine_config_json.extra_args` 可作为 Doke Chromium 专属启动参数通道，参数会插入最终 URL 之前
- `engine_config_json.features.native_fingerprint` 会抑制 Agent 指纹注入 fallback
- `engine_config_json.features.native_geoip` 会抑制 Agent GeoIP 注入 fallback

### M5: 源码补丁路线

- 建立 Chromium 源码/补丁管理方式
- 优先补齐 UA-CH、WebRTC、Canvas、WebGL、Audio、screen、plugins、hardware、CDP detection
- 每个补丁必须绑定检测项和回归脚本
- 当前路线文档：[docs/CHROMIUM_PATCH_PLAN.md](docs/CHROMIUM_PATCH_PLAN.md)

### M6: 检测基准

- 建立手动或半自动检测清单：
  - BrowserScan
  - FingerprintJS demo
  - CreepJS
  - deviceandbrowserinfo
  - bot.incolumitas
- 对比 `system_chrome`、`doke_chromium` 和 CloakBrowser 公开结果
- 当前检测表：[docs/DETECTION_BASELINE.md](docs/DETECTION_BASELINE.md)

## 风险与约束

- Chromium 源码构建成本高，macOS / Windows 双平台 CI 和签名分发都需要单独规划。
- 反检测补丁容易随 Chromium 上游和检测站点变化失效，需要稳定回归基准。
- CloakBrowser 只能作为公开参考和对标，不能复制或逆向其专有二进制实现。
- `system_chrome` 的 CDP/扩展注入仍可保留，但只作为开发 fallback，不应作为高通过率反检测主线。

## 当前开发优先级

1. 文档目标更新。
2. Profile 数据模型增加 `browser_engine`、`engine_config_json`、`fingerprint_seed`。
3. UI 加内核选择，默认 `system_chrome`，目标 `doke_chromium`。
4. 定义 Doke Chromium 自研二进制参数规范。
5. `DokeChromiumEngine` 启动本地自研 Chromium 二进制并接入参数规范。
6. 建立 Chromium 源码补丁文档与检测基准。
