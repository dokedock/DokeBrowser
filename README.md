# DokeBrowser

跨平台（macOS / Windows）的指纹浏览器控制台与多实例运行平台。

> 当前项目仍处于雏形开发阶段，功能、接口和数据结构都可能继续大幅调整。请不要直接用于生产环境或真实业务场景。

当前主线：DokeBrowser 做 Profile、代理池、VPN、日志和批量运营控制台；浏览器内核以自研 `doke_chromium` 为目标，`system_chrome` 仅作为开发期 fallback。

## 目标
- 每个 Profile 独立浏览器实例（开发期 `system_chrome`，目标 `doke_chromium`，后续评估 `cef`）
- 每个 Profile 独立代理配置（HTTP / HTTPS / SOCKS5，支持认证）
- 每个 Profile 独立 OpenVPN（支持 OpenVPN 走 SOCKS）
- 支持“仅浏览器走 VPN”（通过 tun2socks 将 VPN 出口转为本地代理端口）
- 以 DokeBrowser 控制台 + 自研 Doke Chromium 内核为优先开发方向

## 工程结构
- `src/app`：Qt6 + QML 控制台（管理 Profile、实例、代理、VPN、日志）
- `src/agent`：Agent 进程（浏览器引擎启动、IPC、代理测试、OpenVPN、后续 tun2socks）
- `DEVELOPMENT.md`：DokeBrowser 控制台 + 自研 Doke Chromium 内核的开发路线
- `HANDOVER.md`：当前实现状态与交接记录

## 构建（macOS）
```bash
QT_PREFIX="$(brew --prefix qt)"
cmake -S . -B build -DCMAKE_PREFIX_PATH="$QT_PREFIX/lib/cmake"
cmake --build build -j 8
./build/src/app/dokebrowser.app/Contents/MacOS/dokebrowser
```
