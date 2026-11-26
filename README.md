# TetherBench

TetherBench 是面向嵌入式调试探针的本地优先工作台。规划中的系统由电脑端 Gateway、可独立构建的无线探针、成熟调试工具和浏览器控制台组成；项目不重新实现 OpenOCD、pyOCD、probe-rs 或厂商烧录器。

**版本：**`0.0.1-alpha`（唯一来源为 `VERSION`）
**当前状态：**Probe C5 共享固件与协议已有首轮实现，并已用 ESP-IDF 6.1.1 构建；无线、目标 SWD、OTA 掉电回滚和 USB 真机闭环仍未验收。

## 运行原型

需要 Node.js 24 或更新版本，无需安装 npm 依赖。

```powershell
npm run check
npm run dev
```

打开 `http://127.0.0.1:4173`。另开终端运行 `npm run simulate` 注册模拟探针。控制台可查看能力、申请独占调试会话、交换模拟数据并释放会话。按 Ctrl+C 停止模拟探针。

原型状态仅保存在内存中，并且只监听本机回环地址。它没有设备认证能力；此 JSON API 不是探针固件协议。

## 架构状态

- **Probe C5：**独立 ESP-IDF 6.1.1 工程，包含 TLS RPC、UDP 发现、资源租约、可配置 SWD/UART/SPI/I2C、CMSIS-DAP、签名双槽 OTA 和双频状态机。
- **已验证范围：**C5 可移植核心、Arm 解析器、OTA 状态恢复替身、provisioning 和本机构建均有回归证据。
- **未完成范围：**Windows 热点未提供 HE20；真实无线发现、TLS、目标 SWD、外设电气时序、掉电回滚和第二块 C5 协商仍待硬件验证。
- **边界：**本地 Gateway/控制台和 USB/IP 适配在 wireless-console 分支维护；本分支不把主机模拟结果当作硬件支持。

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
