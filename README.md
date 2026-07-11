# DokeBrowser

跨平台（macOS / Windows）的指纹浏览器控制台与多实例运行平台。

## 目标
- 每个 Profile 独立浏览器实例（内置 Chromium / CEF）
- 每个 Profile 独立代理配置（HTTP / HTTPS / SOCKS5，支持认证）
- 每个 Profile 独立 OpenVPN（支持 OpenVPN 走 SOCKS）
- 支持“仅浏览器走 VPN”（通过 tun2socks 将 VPN 出口转为本地代理端口）

## 工程结构
- `src/app`：Qt6 + QML 控制台（管理 Profile、实例、代理、VPN、日志）
- `src/agent`：Agent 进程（后续承载 CEF、网络、OpenVPN、tun2socks）

## 构建（macOS）
```bash
QT_PREFIX="$(brew --prefix qt)"
cmake -S . -B build -DCMAKE_PREFIX_PATH="$QT_PREFIX/lib/cmake"
cmake --build build -j 8
./build/src/app/dokebrowser.app/Contents/MacOS/dokebrowser
```
