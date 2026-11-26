# TetherBench

TetherBench 是面向嵌入式调试探针的本地优先工作台。规划中的系统由电脑端 Gateway、可独立构建的无线探针、成熟调试工具和浏览器控制台组成；项目不重新实现 OpenOCD、pyOCD、probe-rs 或厂商烧录器。

**版本：**`0.0.1-alpha`（唯一来源为 `VERSION`）
**当前状态：**本机 Gateway/控制台原型、C5 共享固件与协议已实现首轮版本。COM8 已烧录启动；无线验收尚未通过，目标 MCU 调试、OTA 回滚和 USB 真枚举未验证。

## 运行原型

需要 Node.js 24 或更新版本。首次运行前执行 `npm ci` 安装锁定的开发依赖。

```powershell
npm run check
npm run dev
```

打开 `http://127.0.0.1:4173`。另开终端运行 `npm run simulate` 注册模拟探针。控制台可查看能力、申请独占调试会话、交换模拟数据并释放会话。按 Ctrl+C 停止模拟探针。

原型状态仅保存在内存中，并且只监听本机回环地址。它没有设备认证能力；此 JSON API 不是探针固件协议。

## 架构状态

- **Gateway 与控制台：**本机回环 HTTP API、设备会话、C5 TLS 路由、OTA 状态与上传恢复、USB/IP 主机模型及 TypeScript 控制台；API 仍是本地 Gateway 接口，不是固件协议。
- **Probe C5：**独立 ESP-IDF 6.1.1 组件工程，包含 TLS RPC、可配置 GPIO 总线、CMSIS-DAP、签名双槽 OTA、目标 MCU UART/SPI OTA 转发和 STA 切频协调；详细范围与验证状态见 [C5 协议](firmware/probe-c5/docs/protocol.md) 和 [验收记录](firmware/probe-c5/docs/validation.md)。
- **硬件限制：**Windows 热点在 2.4 GHz 只协商 11bgn，在临时 5 GHz 测试中协商 11an/VHT，均不是 HE20；固件会拒绝非 Wi-Fi 6 链路。无线发现、认证、真实 DAP/SWD、OTA 与双设备切频尚未完成真机验收。USB/IP 实际枚举也待 VHCI 驱动验证。
- **待完成：**修复或提供可用的 Wi-Fi 6 接入链路并完成真机闭环；随后再继续扩展前端管理能力。

## 目录

```text
apps/gateway/       本机 API、C5 设备管理与调试适配
apps/console/       本地浏览器控制台
apps/simulator/     独立模拟探针客户端
contracts/          Gateway 与 C5 跨工程契约
firmware/probe-c5/  ESP-IDF 固件、工具、测试与协议文档
docs/               架构、决策、验收记录和研究证据
test/               Gateway、控制台、模拟器和适配器测试
```

参见[总体架构](docs/architecture/overview.md)、[本机开发环境](docs/development/environment.md)、[上游研究](docs/upstream/research.md)和[路线图](docs/roadmap.md)。
