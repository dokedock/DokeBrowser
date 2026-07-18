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
- `src/agent/core/OpenVpnManager.*`：OpenVPN 启停、SOCKS 参数、临时认证文件、进程状态和日志转发
- `src/agent/core/ProfileLaunchConfig.*`：`profile.start` 解析、代理启动参数、Profile 数据目录、debug port 分配、窗口尺寸参数
- `src/agent/core/ProfileRuntimeManager.*`：Profile 浏览器进程、CDP client、临时扩展、代理映射和 start/stop 运行态编排
- `src/agent/core/ProxyTestRunner.*`：`proxy.test` / `proxy_pool.test` 的请求解析、校验、URL fallback、异步网络测试与结果组装
- `patches/chromium`：自研 Chromium 补丁队列占位，`series` 定义应用顺序
- `third_party`：本地第三方源码工作区说明；完整 Chromium checkout 不提交到仓库
- `tools/apply_chromium_patches.sh` / `tools/build_doke_chromium.sh`：本地补丁应用与构建入口，不负责下载源码
- `tools/ipc_cli.py`：本地 IPC 调试工具，支持 `engine-list`、`probe-engine`、`start-doke`、`stop`
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
- `engine.list` / `engine.probe`
  - `{ "type": "engine.list" }`
  - `{ "type": "engine.probe", "profile_id": "...", "browser_engine": "doke_chromium", "engine_config_json": "{...}" }`
  - `engine.probe` 会按当前 Profile 的 `engine_config_json` 检测指定二进制路径，可用于 UI 中“检测”当前 Doke 路径；显式路径必须是真实可执行文件，路径不存在或不可执行时不会回退到全局 Doke 路径；可用时会优先执行 `--doke-probe` 读取 `version` / `probe_protocol` / `native_capabilities`，失败时带 `native_probe_error` 并 fallback 到 `--version`；同时按 `features` 返回当前 Profile 声明的 `capabilities`，并通过 `missing_native_capabilities` 标记“Profile 声明但二进制未自报支持”的能力；回包带 `profile_id` 时 UI 会按 Profile 保存检测结果
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
  - `{ "type": "profile.status", "profile_id": "...", "status": "starting|running|stopping|stopped|crashed|error", "error": "", "debug_port": 9222 }`
  - `debug_port` 仅在当前 Profile 分配了 CDP 端口且端口大于 0 时出现；用于检测基线采集工具连接 `http://127.0.0.1:<debug_port>/json/version`
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

### Profile Runtime Manager Test
目标：验证 Profile 运行态 manager 的早期错误路径和不支持内核 ID 的状态/日志回调契约。

```bash
cmake --build build -j 8
./build/src/tests/dokebrowser_profile_runtime_manager
```

预期输出：
- `profile_runtime_manager_ok`

### Proxy Test Runner Test
目标：验证 `proxy.test` / `proxy_pool.test` 的请求解析、错误校验、基础结果字段和 URL fallback 规则。

```bash
cmake --build build -j 8
./build/src/tests/dokebrowser_proxy_test_runner
```

预期输出：
- `proxy_test_runner_ok`

### OpenVPN Manager Test
目标：验证 OpenVPN start 请求解析、必填项校验、`--config` / `--socks-proxy` 参数组装。

```bash
cmake --build build -j 8
./build/src/tests/dokebrowser_openvpn_manager
```

预期输出：
- `openvpn_manager_ok`

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
- 功能：Agent 已支持 `engine.probe`，可按当前 Profile 的 `engine_config_json` 精确检测 Doke Chromium 路径；显式路径必须存在且可执行，坏路径不会 fallback 到 `DOKE_CHROMIUM_PATH` / PATH；错误码可区分 not found / missing / not file / not executable；可用时优先读取 `--doke-probe` JSON 握手，回包区分 Profile 声明能力 `capabilities`、二进制自报能力 `native_capabilities` 和缺失能力 `missing_native_capabilities`，App 会在内核状态和日志中提示缺失能力并按 `profile_id` 记录检测结果，避免不同 Profile 的 Doke 路径状态互相覆盖
- 功能：`profile.start` 已按 `--doke-probe` 结果决定 native fallback 分流；只有 Profile 声明且二进制自报支持的能力才关闭对应 Agent fallback，缺失或无法验证时继续保留 fallback 并写运行日志
- 功能：`profile.start` 会为 Doke 写入 `Doke/runtime.json`，并通过 `--doke-runtime-config=...` 传给内核；该配置包含指纹、结构化 UA-CH、WebRTC、screen/device helper、hardware、Geo、native requested/supported/missing、fallback 决策和非敏感代理元数据，供后续 Chromium patch 读取
- 功能：`profile.status` 现在会在已分配 CDP 端口时返回 `debug_port`，供检测基线/自动化采集连接 `json/version`；若受限环境无法分配端口，Agent 会输出 `debug_port_allocation_failed`
- 功能：基础信息页“内核状态”旁已新增“刷新 / 检测”按钮，可手动刷新全局内核状态或检测当前 Profile 的内核配置；Doke 路径支持通过“选择”按钮从本地文件选择器写入
- 工具：`tools/ipc_cli.py` 已支持命令行 `engine-list` / `probe-engine` / `start-doke`，并可通过 `--native` / `--native-fingerprint` / `--native-geoip` 等开关生成 `engine_config_json.features`，便于真实二进制接入时不打开 UI 也能验证路径、能力和启动链路
- 工具：`tools/doke_probe_check.py` 已支持直接校验真实 Doke Chromium 二进制的 `--doke-probe` JSON、能力清单、`--require-capability` 必备能力和 `--version` fallback
- 工具：`tools/doke_runtime_check.py` 已支持校验 Agent 生成的 `Doke/runtime.json`，包括 schema、native requested/supported/missing、fallback 决策、必备 supported 能力，以及 `--require-native` / `--forbid-native` / `--require-fallback` / `--forbid-fallback` 分流断言
- 工具：`tools/make_fake_doke.py` 已支持生成本地 fake `doke_chromium`，用于模拟 `--doke-probe` 能力组合和长运行启动
- 工具：`tools/chromium_patch_queue_check.py` 已支持校验 `patches/chromium/series`、补丁文件存在性、未列入 series 的补丁文件，以及每个 patch 的 `Doke-*` 元数据；`tools/apply_chromium_patches.sh` 已在动 Chromium checkout 前自动运行该校验
- 工具：`tools/install_depot_tools.sh` / `tools/doke_chromium_source_check.py` / `tools/prepare_doke_chromium_source.sh` 已建立 depot_tools 安装/更新、真实 Chromium checkout 绑定与校验入口；`tools/build_doke_chromium.sh` 已接入源码校验、GN/Ninja 检查、默认 `args.gn` 生成、缺失 `build.ninja` 时的 `gn gen`，并可通过 `DOKE_CHROMIUM_APPLY_PATCHES=1` 在构建前应用 patch queue
- 工具：`tools/doke_chromium_fetch_status.py` 已新增，用于诊断 `third_party/chromium` 是 ready、missing、workspace_without_src 还是 partial_failed_checkout，并给出浅历史恢复命令
- 工具：`tools/doke_chromium_fetch_status.py` 已支持 `--size-timeout`，在 29G Chromium checkout 上体积统计超时时会输出 `unknown_timeout`，但不影响 ready/missing 状态判断
- 工具：`docs/DETECTION_SITES.json` / `tools/doke_detection_baseline.py` 已新增，支持列出 BrowserScan / CreepJS / FingerprintJS demo / deviceandbrowserinfo / bot.incolumitas 检测目标，生成 `doke_detection_baseline.v1` run 模板，校验和汇总人工/半自动采集结果；并可通过 `prepare-artifacts` 生成每个站点/阶段的采集目录、README、notes、signals 模板，通过 `visit-plan` / `launch-plan` 输出访问顺序和本地 IPC 启动命令计划，通过 `run-capture` 编排 Agent 启动、CDP 采集、artifact 同步和报告生成，通过 `init-pair` 创建 `system_chrome` / `doke_chromium` 成对基线，通过 `compare-pair` 或 `compare` 生成系统 Chrome vs Doke 或 patch 前后差异报告
- 工具：`tools/doke_cdp_capture.py` 已新增，可通过 `profile.status.debug_port` 连接 DevTools Protocol，保存页面 `snapshot.json` 与 `screenshot.png` 到检测 artifact 目录；当前采集 navigator/screen/UA-CH/timezone 等基础字段，并内置 BrowserScan / CreepJS / FingerprintJS demo / deviceandbrowserinfo / bot.incolumitas 的文本 extractor，将关键词、visitor id、trust/lies/trash、webdriver/headless/CDP/bot score 等可解析线索写入 `extracted_signals`
- Chromium patch：`patches/chromium/0001-doke-probe-contract.patch` 已作为 series 第一个补丁，目标是在 `chrome/app/chrome_main.cc` 早期处理 `--doke-probe`，输出 `{"probe_protocol":1,"version":"Doke Chromium 0.1.0","capabilities":[]}` 后立即退出；第一版 intentionally 不声明 native 能力，后续每个能力 patch 落地后再逐项加入
- Chromium patch：`patches/chromium/0002-doke-runtime-config-load.patch` 已作为 series 第二个补丁，目标是在 `chrome/app/chrome_main.cc` 早期读取 `--doke-runtime-config`、解析 JSON、校验 `doke_profile_runtime.v1` 和 `profile_id`；该补丁只建立 ingress，不改变指纹行为，也不声明 native capability
- Chromium patch：`patches/chromium/0003-doke-runtime-ua-lang-switches.patch` 已作为 series 第三个补丁，目标是从 runtime config 的 `fingerprint.user_agent` / `fingerprint.language` 补充 `--user-agent` / `--lang`；当前不会覆盖显式命令行开关，也不会声明 `native_fingerprint`
- Chromium patch：`patches/chromium/0004-doke-runtime-ua-client-hints-ingress.patch` 已作为 series 第四个补丁，目标是读取 runtime config 的 `fingerprint.ua_client_hints` 并校验 brands / fullVersionList / platform / fullVersion；该补丁只是 UA-CH metadata ingress，还未写入 Chromium 网络/JS UA-CH surface，也不会声明 `native_fingerprint`
- Chromium patch：`patches/chromium/0005-doke-runtime-ua-client-hints-override.patch` 已作为 series 第五个补丁，目标是从 runtime config 覆盖 `components/embedder_support::GetUserAgentMetadata()`，让 UA-CH metadata 进入 Chromium 的 `blink::UserAgentMetadata` 路径；该补丁仍需真实 Chromium 编译和检测基准验证后，才能考虑声明 `native_fingerprint`
- Chromium patch：`patches/chromium/0006-doke-runtime-webrtc-policy.patch` 已作为 series 第六个补丁，目标是从 runtime config 的 `webrtc.ip_handling_policy` 补充 `--force-webrtc-ip-handling-policy`；当前只是 WebRTC native 策略入口，真实 ICE candidate 泄漏验证通过前不声明新 capability
- Chromium patch：`patches/chromium/0007-doke-runtime-screen-device-switches.patch` 已作为 series 第七个补丁，目标是从 runtime config 的 `fingerprint.window_size` / `fingerprint.device_scale_factor_arg` / `fingerprint.touch_events` 补充 `--window-size` / `--force-device-scale-factor` / `--touch-events`
- Chromium patch：`patches/chromium/0008-doke-runtime-hardware-switches.patch` 已作为 series 第八个补丁，目标是从 runtime config 的 `fingerprint.hardware_concurrency_arg` / `fingerprint.device_memory_gb_arg` 补充 `--doke-hardware-concurrency` / `--doke-device-memory-gb`
- Chromium patch：`patches/chromium/0009-doke-blink-hardware-overrides.patch` 已作为 series 第九个补丁，目标是把 Doke 专用硬件开关接入 Blink 的 `navigator.hardwareConcurrency` 和 `navigator.deviceMemory` 路径；真实 Chromium 编译和 BrowserScan/CreepJS/deviceandbrowserinfo 验证通过前仍不声明 `native_fingerprint`
- Chromium patch：`patches/chromium/0010-doke-runtime-rendering-noise-ingress.patch` 已作为 series 第十个补丁，目标是从 runtime config 的 `rendering.canvas` / `rendering.webgl` / `rendering.audio` 读取稳定噪声 seed，并补充 `--doke-canvas-noise-seed` / `--doke-webgl-noise-seed` / `--doke-audio-noise-seed`；当前只是 Canvas/WebGL/Audio native ingress，未改渲染输出，也不声明 `native_fingerprint`
- Chromium patch：`patches/chromium/0011-doke-runtime-surface-preset-ingress.patch` 已作为 series 第十一个补丁，目标是从 runtime config 的 `surfaces.plugins` / `surfaces.mime_types` / `surfaces.fonts` / `surfaces.client_rects` 读取平台 preset 和 seed，并补充 `--doke-plugins-preset` / `--doke-mime-types-preset` / `--doke-fonts-preset` / `--doke-fonts-seed` / `--doke-client-rects-preset` / `--doke-client-rects-seed`；当前只是 plugin/MIME/font/client-rect native ingress，未改 JS 输出，也不声明 `native_fingerprint`
- Chromium patch：`patches/chromium/0012-doke-runtime-alignment-ingress.patch` 已作为 series 第十二个补丁，目标是从 runtime config 的 `alignment.language` / `alignment.timezone` / `alignment.geo` / `alignment.proxy` 读取语言、时区、地理位置和代理对齐元数据，并补充 `--doke-alignment-language` / `--doke-timezone-id` / `--doke-geo-latitude` / `--doke-geo-longitude` / `--doke-geo-accuracy` / `--doke-proxy-scheme` / `--doke-proxy-host` / `--doke-proxy-port`；当前只是 alignment native ingress，未改 JS/network 输出，也不声明 `native_geoip` 或 `native_proxy`
- Chromium patch：`patches/chromium/0013-doke-runtime-automation-ingress.patch` 已作为 series 第十三个补丁，目标是从 runtime config 的 `automation.webdriver_policy` / `automation.devtools_exposure` / `automation.cdp_side_effect_guard` 等字段读取自动化检测策略，并补充 `--doke-webdriver-policy` / `--doke-devtools-exposure` / `--doke-cdp-side-effect-guard` / `--doke-debug-port-required` / `--doke-startup-automation-controlled`
- Chromium patch：`patches/chromium/0014-doke-blink-webdriver-policy.patch` 已作为 series 第十四个补丁，目标是将 `--doke-webdriver-policy=hide` 接入 Blink `Navigator::webdriver()`，让 `navigator.webdriver` 返回 `false`；当前不单独声明 `native_fingerprint`，需要与 0015/0016 和真实检测基线一起验证
- Chromium patch：`patches/chromium/0015-doke-automation-controlled-policy.patch` 已作为 series 第十五个补丁，目标是在 Doke webdriver policy 为 `hide` 时抑制 Blink `AutomationControlled` runtime feature，包括 `--remote-debugging-port=0` 路径；当前仍未覆盖 CDP side-effect guard，也不声明 `native_fingerprint`
- Chromium patch：`patches/chromium/0016-doke-cdp-side-effect-preview-guard.patch` 已作为 series 第十六个补丁，目标是把 `--doke-cdp-side-effect-guard=true` 桥接为 `DOKE_CDP_SIDE_EFFECT_GUARD=1`，并让 V8 inspector 的 Runtime preview wrapping 与 console event preview 降级到 id-only/无 preview，减少 getter / `Error.stack` 类 CDP side-effect 探测；真实 Chromium 构建和检测基线通过前仍不声明 `native_fingerprint`
- 工具：`tools/doke_probe_patch_check.py` / `tools/doke_runtime_patch_check.py` / `tools/doke_ua_patch_check.py` / `tools/doke_ua_ch_patch_check.py` / `tools/doke_ua_ch_override_patch_check.py` / `tools/doke_webrtc_patch_check.py` / `tools/doke_screen_patch_check.py` / `tools/doke_hardware_patch_check.py` / `tools/doke_rendering_patch_check.py` / `tools/doke_surfaces_patch_check.py` / `tools/doke_alignment_patch_check.py` / `tools/doke_automation_patch_check.py` / `tools/doke_webdriver_patch_check.py` / `tools/doke_automation_controlled_patch_check.py` / `tools/doke_cdp_side_effect_patch_check.py` 已支持校验 probe/runtime/UA/UA-CH/WebRTC/screen/hardware/rendering/surfaces/alignment/automation/webdriver/AutomationControlled/CDP side-effect 十六个 Chromium patch 的关键 token、JSON/schema 字段和调用位置；`tools/doke_patch_apply_smoke.py` 已支持生成最小 fake Chromium 树并按 series 连续应用补丁；`tools/apply_chromium_patches.sh` 已在应用 patch 前运行这些检查
- 框架：Agent 已新增 `BrowserEngine` / `BrowserEngineFactory` / `SystemChromeEngine` / `DokeChromiumEngine` 骨架；`profile.start` 已通过 Factory 选择引擎可执行文件
- 框架：Chrome 启动参数组装已从 `IpcServer` 迁入 `SystemChromeEngine::buildArguments`
- 框架：代理认证/指纹注入临时扩展生成已从 `IpcServer` 迁入 `SystemChromeEngine::createProfileExtension`
- 框架：CDP attach 轮询、`webSocketDebuggerUrl` 解析和 `CdpClient` 创建已迁入 `SystemChromeEngine::attachCdpWhenReady`
- 框架：浏览器进程创建、stdout/stderr 日志、错误状态、兼容重试、running/stopped/crashed 状态已迁入 `SystemChromeEngine::launchProcess`
- 框架：`DokeChromiumEngine` 已有独立 `buildArguments` / `launchProcess` 入口；`profile.start` 已按 `system_chrome` / `doke_chromium` 分流，Doke 路径支持 `engine_config_json.executable` / `binary_path` / `extra_args`，并校验显式二进制路径必须可执行
- 框架：`ProfileStartRequest` 已收拢 `profile.start` 解析；Doke UI 已支持二进制路径、额外参数、原生能力开关
- 框架：`native_fingerprint` / `native_geoip` 已接入 fallback 分流；开启后分别抑制 Agent 指纹注入或 GeoIP 注入 fallback
- 框架：`profile.start` 内的 Profile 目录解析、代理启动参数、debug port 分配、窗口尺寸参数已拆成 helper；App 已接入 `engine.list.result` 并在基础信息页展示当前内核可用性
- 框架：Agent 核心已拆为 `dokebrowser_agent_core` 静态库，`dokebrowser_agent` 与自动化测试共用同一套核心编译产物
- 框架：`ProfileLaunchConfig` 已从 `IpcServer` 拆出，负责启动请求解析和纯参数生成
- 框架：`ProfileRuntimeManager` 已从 `IpcServer` 拆出，负责 Profile 浏览器进程、CDP、临时扩展、代理映射和 start/stop 编排
- 框架：`ProxyTestRunner` 已从 `IpcServer` 拆出，统一承载 `proxy.test` 与 `proxy_pool.test` 的解析、校验、网络重试和结果组装
- 框架：`OpenVpnManager` 已从 `IpcServer` 拆出，统一承载 OpenVPN 进程、SOCKS auth 临时文件、状态和日志转发
- 框架：`IpcServer` 已瘦身为 IPC 路由层，当前主要负责 hello、engine.list、消息分发和统一回包
- 文档：新增 [docs/CHROMIUM_PATCH_PLAN.md](file:///Users/mac/Documents/浏览器/docs/CHROMIUM_PATCH_PLAN.md) 与 [docs/DETECTION_BASELINE.md](file:///Users/mac/Documents/浏览器/docs/DETECTION_BASELINE.md)
- 文档：新增 [docs/CHROMIUM_SOURCE.md](file:///Users/mac/Documents/浏览器/docs/CHROMIUM_SOURCE.md)，定义本地源码、补丁队列、构建和二进制交接流程
- 文档：新增 [docs/DOKE_PROBE.md](file:///Users/mac/Documents/浏览器/docs/DOKE_PROBE.md)，定义真实 Doke Chromium 二进制必须实现的最小 `--doke-probe` 握手契约
- 自动化：新增 `dokebrowser_engine_config`，可不依赖真实 Doke Chromium 二进制验证配置解析、路径优先级、无效显式路径拒绝、`--version` 探针、native feature 开关和 extra args 顺序
- 自动化：新增 `dokebrowser_profile_launch_config`，可不依赖 IPC/真实浏览器验证启动配置解析与代理参数生成
- 自动化：新增 `dokebrowser_profile_runtime_manager`，可不启动真实浏览器验证运行态早期错误路径、状态/日志回调，以及 Doke native 能力缺失时继续 fallback 的启动分流
- 自动化：新增 `dokebrowser_proxy_test_runner`，可不依赖外网验证代理测试请求解析、校验和 fallback URL 规则
- 自动化：新增 `dokebrowser_openvpn_manager`，可不启动真实 OpenVPN 验证请求解析、校验和参数组装
- 自动化：Smoke Test 可用于回归关键链路（弱依赖外网可用性），并已覆盖假 Doke Chromium 可执行文件的 `engine.probe` 可用/不可用探测、`--doke-probe` 版本/native 能力回包、缺失 native 能力回包、普通文件拒绝和 `profile.start/profile.stop` 启停链路

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
- Doke Chromium：准备本地 Chromium checkout，设置 `DOKE_CHROMIUM_SRC`，使用 `patches/chromium/series` 和 `tools/build_doke_chromium.sh` 建立第一版二进制
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
- `./build/src/tests/dokebrowser_profile_runtime_manager` 已通过，输出 `profile_runtime_manager_ok`
- `./build/src/tests/dokebrowser_proxy_test_runner` 已通过，输出 `proxy_test_runner_ok`
- `./build/src/tests/dokebrowser_openvpn_manager` 已通过，输出 `openvpn_manager_ok`
- `./build/src/tests/dokebrowser_smoke` 在当前 Codex 沙箱内可能因 `QLocalServer::listen` 返回 `Unknown error 1` 无法创建本地 socket；提权/正常终端运行可通过
- 本次 `engine.probe` 功能接入后，提权复核 `./build/src/tests/dokebrowser_smoke` 已通过，输出 `smoke_ok`
- 本次 `Doke/runtime.json` 分流校验接入后，`./build/src/tests/dokebrowser_smoke` 已通过，输出 `smoke_ok`
- 本次检测基线 artifact/launch/capture 工具接入后，`cmake --build build -j 8` 已通过；`dokebrowser_profile_runtime_manager` / `dokebrowser_engine_config` / `dokebrowser_profile_launch_config` / `dokebrowser_proxy_test_runner` / `dokebrowser_openvpn_manager` 已通过；`dokebrowser_smoke` 在沙箱内仍因 `ipc_connect_failed` 失败，正常权限复核通过并输出 `smoke_ok`
- `PYTHONPYCACHEPREFIX=/tmp/dokebrowser_pycache python3 -m py_compile tools/doke_detection_baseline.py tools/doke_cdp_capture.py` 已通过；`tools/doke_detection_baseline.py prepare-artifacts` / `validate` / `visit-plan` / `launch-plan --json` / `run-capture --dry-run` / `init-pair` / `compare-pair` / `sync-artifacts` / `report` / `compare` 已用 `/tmp/doke_baseline_prepared.json`、`/tmp/doke_baseline_synced.json`、`/tmp/doke_baseline_system_prepared.json`、`/tmp/doke_pair` 烟测通过；`tools/doke_cdp_capture.extract_site_signals()` 已用 CreepJS 样例文本烟测通过
- `python3 tools/chromium_patch_queue_check.py` 已通过，当前 patch series 为 16 个补丁
- `bash tools/apply_chromium_patches.sh` 已验证会先运行 patch queue 自检，并已在本地真实 Chromium checkout 上应用/识别 `0001` 到 `0016`
- `python3 tools/doke_chromium_source_check.py` 已验证当前机器有可用 Chromium checkout
- `bash tools/prepare_doke_chromium_source.sh` 已接入 `--src` / `--fetch` / `--sync`，并会自动识别被 `.gitignore` 排除的 `third_party/depot_tools`
- 使用 `/tmp/doke_fake_chromium_src_*` 临时 fake Chromium `src` 已验证 `tools/doke_chromium_source_check.py --src`、`tools/prepare_doke_chromium_source.sh --src`、`tools/build_doke_chromium.sh`、`tools/apply_chromium_patches.sh` 的正向链路；构建脚本可生成默认 `out/Doke/args.gn`，缺失 `build.ninja` 时会调用 `gn gen`
- 使用临时 fake Chromium `chrome/app/chrome_main.cc` 已验证 `0001-doke-probe-contract.patch` 可被 `tools/apply_chromium_patches.sh` 正常应用，并写入 `MaybeRunDokeProbe` / `doke-probe` / `probe_protocol`
- 使用临时 fake Chromium `chrome/app/chrome_main.cc` 已验证 `0001-doke-probe-contract.patch` + `0002-doke-runtime-config-load.patch` 可按 series 顺序正常应用，并写入 `MaybeLoadDokeRuntimeConfig` / `doke-runtime-config` / `doke_profile_runtime.v1` / `JSONReader` / `ReadFileToString`
- 使用临时 fake Chromium `chrome/app/chrome_main.cc` 已验证 `0001` + `0002` + `0003` 可按 series 顺序正常应用，并写入 `AppendStringSwitchIfAbsent` / `AppendSwitchASCII` / `fingerprint.user_agent` / `fingerprint.language`
- 使用临时 fake Chromium `chrome/app/chrome_main.cc` 已验证 `0001` + `0002` + `0003` + `0004` 可按 series 顺序正常应用，并写入 `MaybeLogDokeUaClientHints` / `fingerprint.ua_client_hints` / `fullVersionList`
- 使用临时 fake Chromium `chrome/app/chrome_main.cc` + `components/embedder_support/user_agent_utils.cc` 已验证 `0001` 到 `0005` 可按 series 顺序正常应用，并写入 `GetDokeUserAgentMetadataOverride` / `ReadDokeBrandList` / `brand_full_version_list`
- 使用临时 fake Chromium `chrome/app/chrome_main.cc` + `components/embedder_support/user_agent_utils.cc` 已验证 `0001` 到 `0006` 可按 series 顺序正常应用，并写入 `force-webrtc-ip-handling-policy` / `root.FindDict("webrtc")`
- 使用 `python3 tools/doke_patch_apply_smoke.py` 生成临时 fake Chromium `chrome/app/chrome_main.cc` + `components/embedder_support/user_agent_utils.cc` + Blink navigator hardware/device/webdriver + `content/child/runtime_features.cc` + V8 inspector Runtime/console 文件，已验证 `0001` 到 `0016` 可按 series 顺序正常应用，并写入 `window-size` / `force-device-scale-factor` / `touch-events` / `doke-hardware-concurrency` / `doke-device-memory-gb` / `navigator.hardwareConcurrency` / `navigator.deviceMemory` / `Navigator::webdriver` / `GetDokeWebdriverOverride` / `ApplyDokeAutomationControlledPolicy` / `DOKE_CDP_SIDE_EFFECT_GUARD` / `generatePreview = false`
- 已联网安装 `third_party/depot_tools`；`fetch/gclient/gn/ninja/autoninja` 可被 `tools/doke_chromium_source_check.py` 自动识别
- 已尝试 `bash tools/prepare_doke_chromium_source.sh --fetch` 拉取真实 Chromium，首次完整历史 clone 在约 64% 时因 `RPC failed; curl 18 transfer closed` / `early EOF` 中断；随后通过 `bash tools/resume_doke_chromium_fetch.sh` 的浅历史、低并发恢复模式完成源码同步
- 当前 `third_party/chromium/src` 状态为 ready，HEAD `534c1497c1`，本地源码约 29G；`python3 tools/doke_chromium_fetch_status.py` 可复查
- 已在真实 Chromium checkout 上成功应用 `patches/chromium/series` 的 `0001` 到 `0016`；`tools/apply_chromium_patches.sh` 已支持断点续打，能通过 token 检测跳过已存在的 Doke patch
- `python3 tools/doke_chromium_fetch_status.py --size-timeout 1` 已验证返回 ready，HEAD `534c1497c1`；大目录体积统计按预期输出 `unknown_timeout`
- 构建阻塞：`bash tools/build_doke_chromium.sh` 已进入 GN 阶段，但当前 macOS 只安装/选择了 Command Line Tools (`/Library/Developer/CommandLineTools`)，没有完整 `/Applications/Xcode.app`；`gn gen` 失败于 `xcodebuild -version`
- 已尝试 hermetic Xcode：`FORCE_MAC_TOOLCHAIN=1 python3 third_party/chromium/src/build/mac_toolchain.py` 失败，CIPD 报 `infra_internal/ios/xcode/xcode_binaries/mac-amd64` 对未认证调用者不可见，需要 `cipd auth-login`
- 工具：`tools/doke_chromium_build_prereq.py` 已新增，`tools/build_doke_chromium.sh` 会在 `gn gen` 前检查完整 Xcode 或 hermetic Xcode，并用清晰错误提前退出
- 当前真实构建/验证节点阻塞：`python3 tools/doke_chromium_build_prereq.py` 返回 `chromium_build_prereq_failed platform=mac`，active developer dir 仍为 `/Library/Developer/CommandLineTools`，需要完整 Xcode 或完成 hermetic Xcode/CIPD 认证后才能进入真实 Doke Chromium build/检测基线验证
- 路线选择：已选择完整 Xcode 作为本机 Doke Chromium 构建主线；`tools/select_chromium_xcode.sh` 已新增，安装 `/Applications/Xcode.app` 后可用 `bash tools/select_chromium_xcode.sh --path /Applications/Xcode.app` 选择并校验，再继续 `bash tools/build_doke_chromium.sh`
- 2026-07-18 真实构建环境更新：已选择 `/Applications/Xcode.app/Contents/Developer`，`xcodebuild -version` 为 `Xcode 26.6` / `Build version 17F113`；`python3 tools/doke_chromium_build_prereq.py` 已返回 `chromium_build_prereq_ok platform=mac`
- 2026-07-18 官方 Metal 构建阻塞：`xcodebuild -showComponent MetalToolchain -json` 显示 `status=uninstalled` / `buildVersion=17F109`，`xcrun metal --version` 报缺少 Metal Toolchain；尝试 `xcodebuild -downloadComponent MetalToolchain` 以及 `-buildVersion 17F109/17F112/17F113` 均因 Apple MobileAsset catalog 获取失败，暂无法完成带 Metal 的正式 macOS Chromium 构建
- 2026-07-18 临时 no-Metal 验证：为验证 Doke patch 编译链路，`third_party/chromium/src/out/Doke/args.gn` 临时加入 `angle_enable_metal = false`、`dawn_enable_metal = false`，`buildtools/mac/gn gen out/Doke` 已通过；该配置只用于本机阶段验证，不是最终 macOS GPU 发布配置
- 2026-07-18 构建缺失生成物修复：已生成 `build/util/LASTCHANGE`、`build/util/LASTCHANGE.committime`，`python3 build/compute_build_timestamp.py default` 可返回时间戳；已生成 `gpu/webgpu/DAWN_VERSION`，避免 GN 读取 Dawn 版本失败
- 2026-07-18 DevTools/Rollup 修复：已补齐 `third_party/devtools-frontend/src/node_modules/@rollup/rollup-darwin-arm64`，并用 Chromium bundled node 验证 `require('@rollup/rollup-darwin-arm64')` 可加载
- 2026-07-18 构建脚本修复：`tools/build_doke_chromium.sh` 现在把 Go build cache 放到 `$SRC_DIR/$OUT_DIR/.gocache`，避免受限环境写 `~/Library/Caches/go-build` 失败；可用 `DOKE_GO_CACHE=/path/to/cache` 覆盖
- 2026-07-18 真实构建进度：`bash tools/build_doke_chromium.sh` 在真实 Chromium checkout 上已推进到 `12060/45799`，最后确认阶段为 Blink core/DevTools 生成与编译，未出现新的编译错误；本轮因用户要求先更新文档并推送而停止跟踪，尚未产出最终 `Chromium.app`，也尚未执行 `--doke-probe` / 启动验证
- Smoke Test 对外网弱依赖：即使 `proxy.test` 返回 `ok=false`，只要 Agent/IPC/结果回传链路通，仍会输出 `smoke_ok`
- 当前机器未配置 `DOKE_CHROMIUM_PATH`，PATH 中也未发现 `doke-chromium` / `doke_chromium` / `dokebrowser-chromium`；真实 doke 启动和检测基准需等自研二进制接入
