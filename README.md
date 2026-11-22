# TetherBench

TetherBench 是面向嵌入式调试探针的本地优先工作台。规划中的系统由电脑端 Gateway、可独立构建的无线探针、成熟调试工具和浏览器控制台组成；项目不重新实现 OpenOCD、pyOCD、probe-rs 或厂商烧录器。

**版本：**`0.0.1-alpha`（唯一来源为 `VERSION`）
**当前状态：**本机软件原型，尚未支持任何真实探针或目标板。

## 运行原型

需要 Node.js 24 或更新版本，无需安装 npm 依赖。

```powershell
npm run check
npm run dev
```

打开 `http://127.0.0.1:4173`。另开终端运行 `npm run simulate` 注册模拟探针。控制台可查看能力、申请独占调试会话、交换模拟数据并释放会话。按 Ctrl+C 停止模拟探针。

原型状态仅保存在内存中，并且只监听本机回环地址。它没有设备认证能力；此 JSON API 不是探针固件协议。

## 架构状态

- **已实现原型：**本机 HTTP API、浏览器控制台、独立模拟探针进程、有界请求体、会话所有权、断线清理及测试。
- **已配置但未构建：**最小 C/C++ 混合 ESP-IDF Probe C5 工程；检查时本机没有 ESP-IDF。
- **待决事项：**正式 Gateway 语言、探针传输与分帧、设备认证、调试后端、板级引脚和 OTA 启动链，以及 GUI 框架。
- **不作出的声明：**USB High-Speed、Wi-Fi 吞吐、SWD/JTAG、烧录、OTA、硬件恢复或任何实体设备支持。

## 目录

```text
apps/gateway/       仅监听本机回环的 API 原型
apps/console/       本地浏览器控制台
apps/simulator/     独立模拟探针客户端
contracts/          原型 API 契约和请求限制
firmware/probe-c5/  ESP-IDF C/C++ 最小工程配置
docs/               架构、决策、研究证据和路线图
test/               API 与生命周期测试
```

参见[总体架构](docs/architecture/overview.md)、[本机开发环境](docs/development/environment.md)、[上游研究](docs/upstream/research.md)和[路线图](docs/roadmap.md)。
