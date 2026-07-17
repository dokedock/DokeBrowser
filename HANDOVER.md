# DokeBrowser 交接文档

## 目标
- 跨平台（macOS / Windows）的指纹浏览器控制台与多实例运行平台
- 新主线：DokeBrowser 负责控制台/运营层，并研发自有 `doke_chromium` 浏览器内核
- 每个 Profile 独立代理配置（HTTP / HTTPS / SOCKS5，支持认证）
- 每个 Profile 独立 OpenVPN（支持 OpenVPN 走 SOCKS）
- 支持“仅浏览器走 VPN”（后续通过 tun2socks 将 VPN 出口转为本地代理端口，仅对目标 Profile 生效）
- 浏览器引擎采用阶段策略：`system_chrome` 作为开发 fallback，`doke_chromium` 作为主力目标，`cef` 暂缓为后续选项

## 当前工程结构
- [CMakeLists.txt](file:///Users/mac/Documents/浏览器/CMakeLists.txt)：根入口
- [src/CMakeLists.txt](file:///Users/mac/Documents/浏览器/src/CMakeLists.txt)：Qt6 依赖与子目录
- `src/app`：Qt6 + QML 控制台（环境列表、基础信息、代理、VPN、日志）
- `src/agent`：Agent 进程（IPC 服务端、代理测试、OpenVPN 管理）；已拆出 `dokebrowser_agent_core` 静态库，Agent 可执行文件和测试共用核心实现
- `src/agent/core/ProfileLaunchConfig.*`：`profile.start` 解析、代理启动参数、Profile 数据目录、debug port 分配、窗口尺寸参数
- `src/agent/core/ProxyTestRunner.*`：`proxy.test` / `proxy_pool.test` 的请求解析、校验、URL fallback、异步网络测试与结果组装
- `src/shared`：共享库（本地 IPC：4 字节长度前缀 + JSON）
- `src/tests`：自动化测试（启动 agent + IPC + 代理直连自检；Doke Chromium 配置解析与启动参数回归）
- [DEVELOPMENT.md](file:///Users/mac/Documents/浏览器/DEVELOPMENT.md)：新开发路线文档，记录 DokeBrowser 控制台 + 自研 Doke Chromium 方案

## 开发路线（已更新）
1. 先搭建可运行骨架（Qt Quick App + Agent stub）
2. 建立 Host(app) ↔ Agent 的本地 IPC，确保 UI 与后台能稳定交互
3. UI 布局改为接近 XChrome 风格：左侧导航 + 顶部筛选/搜索 + 表格列表 + 底部详情 Tabs
4. 按优先级完成 Tabs：基础信息 → 代理 → VPN
5. 增加自动化 Smoke Test，确保关键链路无明显问题
6. 新增浏览器引擎抽象：`system_chrome | doke_chromium | cef`
7. 先把现有 Chrome 启动逻辑迁到 `SystemChromeEngine`
8. 新增 `DokeChromiumEngine`，启动自研 Chromium 二进制
9. 建立 BrowserScan / FingerprintJS / CreepJS 等检测基准，对比 `system_chrome`、`doke_chromium` 和 CloakBrowser 公开结果

## CloakBrowser 参考原则
- CloakBrowser 只作为公开竞品、公开 wrapper/API 设计和检测基准参考，不作为 DokeBrowser 运行时可选内核
- 可以参考其公开 README、公开 wrapper、启动参数组织、Playwright/Puppeteer 兼容体验、`geoip`、`humanize`、persistent context、代理配置和检测基准
- 不复制、不逆向、不反编译、不修改、不打包其专有 CloakBrowser Chromium 二进制
- 不把它的未公开 C++ 补丁当成我们的实现来源；Doke Chromium 的源码补丁必须基于公开 Chromium/ungoogled-chromium/CEF 路线自研
- 当前 `system_chrome` 的 CDP/扩展注入继续保留为开发 fallback，不再作为高通过率反检测的最终主线

## 构建与运行（macOS）
```bash
QT_PREFIX="$(brew --prefix qt)"
cmake -S . -B build -DCMAKE_PREFIX_PATH="$QT_PREFIX/lib/cmake"
cmake --build build -j 8
./build/src/app/dokebrowser.app/Contents/MacOS/dokebrowser
```

## 数据存储
- 当前使用 SQLite 落盘（QtSql），由 `ProfileRepository` 统一读写
- 路径：`QStandardPaths::AppDataLocation/profiles.sqlite`
- 表结构（概要）
  - `profiles`：Profile 基础信息（name/group/remark/status/created_at/last_open_at 等）
    - 已扩展：`browser_engine`、`engine_config_json`、`fingerprint_seed`、`start_url`、`humanize_enabled`、`geoip_enabled`
  - `proxy_configs`：代理配置（enabled/type/host/port/username/password）
  - `proxies` / `proxy_assignments`：代理池与分配关系（一个代理同一时刻最多分配给一个 Profile）
  - `vpn_openvpn_configs`：OpenVPN 配置（exe/config/socks 等）
  - `profile_runs`：运行事件（start/stop/vpn.status 等）
  - `logs`：日志归档
  - `proxy_test_runs`：代理自检历史
- 自动清理（按 Profile 分桶裁剪）
  - `logs` 保留 500 条
  - `profile_runs` / `proxy_test_runs` 各保留 100 条

## UI 现状（对齐思路）
主界面为“环境管理”视图，分为四块：
- 左侧：新建浏览器 + 导航（其它模块入口暂保留但禁用）
- 顶部：分组过滤 + 搜索输入（搜索逻辑待接入）
- 中间：环境列表（ID/状态/名称/分组/IP/备注/最近打开/创建时间/更多）
- 底部：操作条（运行/停止/删除/清空日志）+ Tabs（基础信息/代理/VPN/日志）

### 基础信息 Tab
- 可编辑：
  - 名称、分组、数据目录、备注
  - 指纹策略：`follow_ip | random`
  - 浏览器相关：UA、分辨率、DPR
  - 语言相关：语言、时区
  - 硬件相关：CPU 线程（hardwareConcurrency）、内存 GB（deviceMemory）
  - 触控：touchEnabled
  - 定位：geoEnabled + (lat/lon/accuracy)
- 只读：ID、状态、创建时间、最近打开
- 修改会自动落盘（SQLite：`profiles` 表）

### 指纹策略说明
- `follow_ip`：
  - “测试代理”成功后，Host 会使用 Geo API 查询出口 IP 对应的时区/国家/坐标，并自动写回 Profile（写回语言/时区/定位）
  - 写回后需要重启 Profile 才能在浏览器中完全生效
- `random`：
  - 随机生成并固定一套画像字段（UA/分辨率/DPR/语言/时区/硬件等）
  - 为避免“一致性崩盘”，macOS 环境下随机画像默认不生成 `Win32` 平台字段

### 代理 Tab
- 每 Profile 的代理配置字段：启用、类型（direct/http/https/socks5）、host、port、用户名、密码
- “测试代理”：通过 IPC 下发到 Agent，Agent 使用 QtNetwork 请求 `https://httpbin.org/ip`（失败会自动 fallback 尝试 `https://api.ipify.org?format=json`）
  - 回传 `proxy.test.result`，并在 UI 显示摘要（OK/FAIL、status、耗时、ip、错误）
  - 批量测试：Host 侧带并发/队列控制（默认并发=3），并支持取消；为防止取消/超时后“晚到结果”覆盖，批量链路带 `batch_id`/`request_id` 去串包
  - `follow_ip` 策略下：若测试成功且拿到 observed_ip，Host 会触发 Geo API 写回（语言/时区/定位）

### VPN Tab（OpenVPN 走 SOCKS）
- 每 Profile 的 OpenVPN 配置字段：启用、openvpn 可执行、config 路径、是否走 SOCKS、SOCKS host/port/用户名/密码
- 启动/停止：通过 IPC 请求 Agent 管理 OpenVPN 进程
- 日志：OpenVPN stdout/stderr 会转发为 `log.line` 出现在“日志”Tab
- 状态：Agent 回传 `vpn.status`，UI 显示 selectedVpnStatus

## IPC 协议（v1）
传输层：QLocalSocket/QLocalServer，本地 socket，帧格式为：
- 4 字节 big-endian 长度（payload bytes）
- payload 为 JSON（Compact）

### 常用消息
- `hello`（App/Smoke → Agent）
  - `{ "type": "hello", "client": "app" }`
- `hello.ack`（Agent → App/Smoke）
  - `{ "type": "hello.ack", "agent": "dokebrowser_agent", "version": 1 }`
- `log.line`（Agent → App）
  - `{ "type": "log.line", "message": "..." }`
- `profile.start/profile.stop`（App → Agent）
  - `{ "type": "profile.start", "profile_id": "...", "profile_name": "...", "data_dir": "...", "chrome_compat": false, "fingerprint_mode": "follow_ip|random", "language": "ja-JP", "timezone": "Asia/Tokyo", "user_agent": "...", "platform": "MacIntel", "hardware_concurrency": 8, "device_memory_gb": 8, "device_scale_factor": 1, "resolution": "1280x720", "touch_enabled": false, "geo_enabled": true, "geo_latitude": 35.6895, "geo_longitude": 139.6917, "geo_accuracy": 1000, "proxy": { "enabled": true, "type":"http|https|socks5|direct", "host":"...", "port": 8080, "username":"", "password":"" } }`
  - 已扩展字段：`browser_engine: "system_chrome|doke_chromium|cef"`、`engine_options: { "humanize": true, "geoip": true }`
  - 代理认证说明：
    - `socks5`：若提供 username/password，Agent 使用 `--proxy-server=socks5://user:pass@host:port`
    - `http/https`：若提供 username/password，Agent 生成临时 MV3 扩展并通过 `--load-extension=...` 加载，用 `webRequest.onAuthRequired` 注入凭据
  - 指纹注入说明（Agent）：
    - 启动参数层：`--lang`、`--user-agent`、`--window-size`、`--force-webrtc-ip-handling-policy=disable_non_proxied_udp`
    - CDP 层：remote-debugging-port + `Target.setAutoAttach`，对每个 page target 注入：
      - `Emulation.setTimezoneOverride`、`Emulation.setGeolocationOverride`、`Emulation.setDeviceMetricsOverride`
      - `Network.setUserAgentOverride`、`Network.setExtraHTTPHeaders(Accept-Language)`
      - `Page.addScriptToEvaluateOnNewDocument`（覆盖 webdriver/language/platform/hardware 等；并加 Canvas/WebGL/Audio 轻量噪声）
    - 扩展层（兜底）：动态生成 MV3 扩展，`content_scripts run_at=document_start world=MAIN` 注入同一份 JS 逻辑；并提供 `check.html` 自检页面
- `profile.status`（Agent → App）
  - `{ "type": "profile.status", "profile_id": "...", "status": "starting|running|stopping|stopped|crashed|error", "error": "..." }`
- `proxy.test`（App → Agent）
- `proxy.test`（App → Agent）
  - `{ "type": "proxy.test", "profile_id": "...", "proxy": { ... }, "url": "https://httpbin.org/ip", "request_id": "...", "batch_id": "..." }`
- `proxy.test.result`（Agent → App）
  - `{ "type": "proxy.test.result", "profile_id":"...", "ok": true, "observed_ip": "...", "status_code": 200, "duration_ms": 123, "qt_error": 0, "error": "", "request_id":"...", "batch_id":"..." }`
- `vpn.openvpn.start/vpn.openvpn.stop`（App → Agent）
  - `{ "type": "vpn.openvpn.start", "profile_id": "...", "exe": "openvpn", "config": "/path/to.ovpn", "socks": { ... } }`
- `vpn.status`（Agent → App）
  - `{ "type": "vpn.status", "profile_id": "...", "status": "running|stopped|stopping|crashed|error", "error": "..." }`

## 自动化测试

### Engine Config Test
目标：验证 `doke_chromium` 的 `engine_config_json` 解析、单 Profile 二进制路径优先级、原生能力开关，以及 `extra_args` 插入最终 URL 之前的启动参数契约。

```bash
cmake --build build -j 8
./build/src/tests/dokebrowser_engine_config
```

预期输出：
- `engine_config_ok`

### Profile Launch Config Test
目标：验证 `profile.start` 请求解析、浏览器内核 ID 归一化、代理启动参数生成、窗口尺寸参数生成等纯配置契约。

```bash
cmake --build build -j 8
./build/src/tests/dokebrowser_profile_launch_config
```

预期输出：
- `profile_launch_config_ok`

### Proxy Test Runner Test
目标：验证 `proxy.test` / `proxy_pool.test` 的请求解析、错误校验、基础结果字段和 URL fallback 规则。

```bash
cmake --build build -j 8
./build/src/tests/dokebrowser_proxy_test_runner
```

预期输出：
- `proxy_test_runner_ok`

### Smoke Test
目标：验证“启动 agent → 建立 IPC → hello → proxy.test(直连) → 收到 proxy.test.result”全链路。

```bash
cmake --build build -j 8
./build/src/tests/dokebrowser_smoke
```

预期输出：
- `smoke_ok`

## 当前进度（已完成）
- UI：环境管理主界面骨架已按目标风格对齐
- Profile：新建/删除/选择，字段编辑落库，列表列已扩展；支持分组/关键字过滤与“仅看勾选”
- IPC：app↔agent 本地 socket JSON 帧协议，支持重连
- 代理：配置 + 单测/批量“测试代理”链路跑通（并发/队列/超时/取消 + 防串包）
- 代理池：已支持导入/列表/一键分配/释放/换一个；支持批量健康自检并写回 last_ok/last_ip；分配时会在没有健康空闲代理的情况下自动触发一次“空闲代理健康自检”作为兜底
- VPN：OpenVPN（可选 SOCKS）启动/停止 + 日志/状态回传链路跑通
- 浏览器实例：Agent 已可启动本机 Chrome/Chromium 独立用户目录，并通过 CDP + 扩展双路径注入 Profile 指纹
- 指纹对抗：已补 UA-CH / `navigator.userAgentData` 与 UA 的基础一致性（CDP `userAgentMetadata` + JS 兜底）
- 开发路线：已确定“DokeBrowser 控制台 + 自研 Doke Chromium 内核”，并新增 [DEVELOPMENT.md](file:///Users/mac/Documents/浏览器/DEVELOPMENT.md)
- 框架：Profile 已新增 `browser_engine`、`engine_config_json`、`fingerprint_seed`、`start_url`、`humanize_enabled`、`geoip_enabled` 字段；UI 基础信息页已能编辑；`profile.start` 已携带这些字段
- 框架：Agent 已支持 `engine.list`，可探测 `system_chrome` 与 `doke_chromium`（通过 `DOKE_CHROMIUM_PATH` 或 PATH 中的 `doke-chromium`/`doke_chromium`/`dokebrowser-chromium`）
- 框架：Agent 已新增 `BrowserEngine` / `BrowserEngineFactory` / `SystemChromeEngine` / `DokeChromiumEngine` 骨架；`profile.start` 已通过 Factory 选择引擎可执行文件
- 框架：Chrome 启动参数组装已从 `IpcServer` 迁入 `SystemChromeEngine::buildArguments`，`IpcServer` 当前只填充 `LaunchOptions`
- 框架：代理认证/指纹注入临时扩展生成已从 `IpcServer` 迁入 `SystemChromeEngine::createProfileExtension`，`IpcServer` 仅保留扩展目录清理映射
- 框架：CDP attach 轮询、`webSocketDebuggerUrl` 解析和 `CdpClient` 创建已迁入 `SystemChromeEngine::attachCdpWhenReady`，`IpcServer` 仅提供 Profile 存活判断、客户端替换和日志回调
- 框架：浏览器进程创建、stdout/stderr 日志、错误状态、兼容重试、running/stopped/crashed 状态已迁入 `SystemChromeEngine::launchProcess`，`IpcServer` 仅保留 Profile 进程映射和停止请求
- 框架：`DokeChromiumEngine` 已有独立 `buildArguments` / `launchProcess` 入口；`profile.start` 已按 `system_chrome` / `doke_chromium` 分流，Doke 路径支持 `engine_config_json.executable` / `binary_path` / `extra_args`
- 框架：`ProfileStartRequest` 已收拢 `profile.start` 解析；Doke UI 已支持二进制路径、额外参数、原生能力开关
- 框架：`native_fingerprint` / `native_geoip` 已接入 fallback 分流；开启后分别抑制 Agent 指纹注入或 GeoIP 注入 fallback
- 框架：`profile.start` 内的 Profile 目录解析、代理启动参数、debug port 分配、窗口尺寸参数已拆成 helper；App 已接入 `engine.list.result` 并在基础信息页展示当前内核可用性
- 框架：Agent 核心已拆为 `dokebrowser_agent_core` 静态库，`dokebrowser_agent` 与自动化测试共用同一套核心编译产物
- 框架：`ProfileLaunchConfig` 已从 `IpcServer` 拆出，`IpcServer` 现在更偏 IPC 分发和运行态资源映射
- 框架：`ProxyTestRunner` 已从 `IpcServer` 拆出，统一承载 `proxy.test` 与 `proxy_pool.test` 的解析、校验、网络重试和结果组装
- 文档：新增 [docs/CHROMIUM_PATCH_PLAN.md](file:///Users/mac/Documents/浏览器/docs/CHROMIUM_PATCH_PLAN.md) 与 [docs/DETECTION_BASELINE.md](file:///Users/mac/Documents/浏览器/docs/DETECTION_BASELINE.md)
- 自动化：新增 `dokebrowser_engine_config`，可不依赖真实 Doke Chromium 二进制验证配置解析、路径优先级、native feature 开关和 extra args 顺序
- 自动化：新增 `dokebrowser_profile_launch_config`，可不依赖 IPC/真实浏览器验证启动配置解析与代理参数生成
- 自动化：新增 `dokebrowser_proxy_test_runner`，可不依赖外网验证代理测试请求解析、校验和 fallback URL 规则
- 自动化：Smoke Test 可用于回归关键链路（弱依赖外网可用性）

## 参考项目（指纹对抗与架构借鉴）
- XChrome / zchrome（WPF 控制台 + CDP 注入思路）
  - 参考链接：https://github.com/chanawudi/XChrome
  - 借鉴点：
    - CDP 的 `Page.addScriptToEvaluateOnNewDocument` + `Emulation.*` 注入模型
    - 代理认证与稳定性：优先考虑“本地端口转发/映射”而不是让 Chrome 弹认证窗（后续可做增强）
- VirtualBrowser（Chromium 指纹浏览器，BrowserLeaks/fingerprintjs 作为目标）
  - 参考链接：https://github.com/Virtual-Browser/VirtualBrowser
  - 借鉴点：
    - “按 IP 自动匹配”语言/时区/定位的闭环策略
    - Canvas/WebGL/Audio 等高阶项采用“稳定 seed + 轻量噪声”的工程化做法
    - 提供本地 API 返回 `debuggingPort` 并允许 Playwright `connect_over_cdp` 的自动化出口

## 下一步建议
- Doke Chromium：接入真实自研二进制后，按检测表验证 `native_fingerprint` / `native_geoip` 能力并逐项关闭 fallback
- Agent：继续瘦身 `IpcServer`，下一步优先拆出代理参数解析和 debug port 分配 helper
- Chromium 源码：建立自研源码/补丁管理方案，优先补 UA-CH、WebRTC、Canvas、WebGL、Audio、screen、plugins、hardware、CDP detection
- “仅浏览器走 VPN”：引入 tun2socks，将 VPN 出口转为本地代理端口，并仅给目标 Profile 的 CEF 网络栈设置代理
- OpenVPN 健壮性：进程崩溃检测、重启策略、状态归档与日志落库
- 指纹对抗增强：
  - fonts/clientRects/speechSynthesis 等高阶项分层接入，并以 profile 固定 seed 保持稳定
  - 对 `system_chrome` 继续补强作为 fallback；对高通过率目标优先推进 `doke_chromium`

## 本地验证备注
- `cmake --build build -j 8` 已通过
- `./build/src/tests/dokebrowser_engine_config` 已通过，输出 `engine_config_ok`
- `./build/src/tests/dokebrowser_profile_launch_config` 已通过，输出 `profile_launch_config_ok`
- `./build/src/tests/dokebrowser_proxy_test_runner` 已通过，输出 `proxy_test_runner_ok`
- `./build/src/tests/dokebrowser_smoke` 在当前 Codex 沙箱内可能因 `QLocalServer::listen` 返回 `Unknown error 1` 无法创建本地 socket；提权/正常终端运行可通过
- 本次 `ProxyTestRunner` 拆分后，提权复核 `./build/src/tests/dokebrowser_smoke` 已通过，输出 `smoke_ok`
- Smoke Test 对外网弱依赖：即使 `proxy.test` 返回 `ok=false`，只要 Agent/IPC/结果回传链路通，仍会输出 `smoke_ok`
- 当前机器未配置 `DOKE_CHROMIUM_PATH`，PATH 中也未发现 `doke-chromium` / `doke_chromium` / `dokebrowser-chromium`；真实 doke 启动和检测基准需等自研二进制接入
