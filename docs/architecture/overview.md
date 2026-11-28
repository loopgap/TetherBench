# 总体架构

## 状态与边界

本仓库目前实现的是本机原型，不是已完成的产品架构。图中 **实线** 表示当前模拟链路，**虚线** 表示规划或待验证能力。Gateway 先按模块化单体组织；设备实时 SWD/JTAG、UART 和其他总线事务应在拥有硬件引脚的探针上完成。

```mermaid
flowchart LR
  UI[浏览器控制台
  已实现] -->|本机 HTTP/JSON| GW[Gateway 原型
  Node.js，临时实验实现]
  SIM[独立模拟探针
  已实现] -->|本机 HTTP/JSON| GW
  IDE[IDE / CLI / Agent
  规划] -.-> GW
  GW -.-> AD[OpenOCD / pyOCD / probe-rs 适配器
  待验证]
  AD -.-> P[ESP32-C5 无线探针
  规划]
  GW -.-> P4[ESP32-P4 USB Bridge
  规划]
  P -.-> MCU[STM32F405 参考目标
  规划]
  P4 -.-> MCU
```

## 纯 C5 无线探针路线

Gateway 管理发现、身份、权限、会话和工具适配；探针在本机执行 CMSIS-DAP 和实时引脚操作。配对、加密、发现方式、重连及 TLS 资源预算仍未定，不能将模拟 API 当成设备链路。

```mermaid
flowchart LR
  TOOLS[IDE / CLI / Agent] --> GW[电脑 Gateway]
  GW -->|发现、配对、管理控制：待定义| C5[Wi-Fi C5 探针]
  TOOLS -->|标准调试后端适配：待验证| GW
  C5 -->|本地 SWD/JTAG / UART| MCU[目标 MCU]
```

这条路线独立验证电脑 Gateway 直接管理 C5：C5 自己执行 SWD/JTAG、UART、SPI 和 I2C 事务，电脑只承担发现、认证、租约和协议适配。双 C5 AP/STA 的关联、DHCP、HE20 和断连证据只能证明无线链路状态；RPC、DAP、目标 MCU OTA 和掉电恢复仍需在同一条路线中分别验收。

## P4 + C5 USB 桥接路线

P4 复合 USB 设备、CDC、CMSIS-DAP v2 Bulk、USB High-Speed 能力和目标绑定必须在 P4 板卡、PHY、驱动和接线明确后单独验证。电脑端 Gateway 管理通道与 USB 调试通道最终共享目标会话，但不能用纯 C5 无线结果替代这条路线的 USB、P4-C5 链路或目标事务证据。当前 `firmware/bridge-p4` 只提供有界 CDC 帧和离线安全响应骨架，未宣称 High-Speed 或 CMSIS-DAP 已通过。

```mermaid
flowchart LR
  HOST[电脑] -->|管理 API：规划| GW[Gateway]
  HOST -->|USB CMSIS-DAP：规划| P4[ESP32-P4 Bridge]
  P4 -->|ESP-Hosted：兼容性待测| C5[ESP32-C5 无线协处理器]
  C5 --> MCU[目标 MCU]
  GW -.->|目标绑定、权限和会话同步：待设计| P4
```

## 工程依赖与所有权

公共契约只定义接口，不依赖任何业务工程。Gateway、模拟器和固件通过契约通信；固件工程不得引用其他固件工程的私有源码。

```mermaid
flowchart BT
  CONTRACTS[contracts：接口唯一来源]
  GW[apps/gateway] --> CONTRACTS
  CONSOLE[apps/console] --> CONTRACTS
  SIM[apps/simulator] --> CONTRACTS
  PROBE[firmware/probe-c5] --> CONTRACTS
  P4[firmware/bridge-p4：独立 USB 桥接工程] --> CONTRACTS
  HOSTED[firmware/hosted-c5：规划] --> CONTRACTS
  MCU[firmware/targets/stm32f405：规划] --> CONTRACTS
```

## 模拟链路与硬件责任

当前原型只在 Gateway 进程内保存设备和会话状态。设备断开或心跳超时后，Gateway 必须释放其会话。真实设备身份、认证和分布式状态恢复尚未实现。

```mermaid
sequenceDiagram
  participant S as 模拟探针
  participant G as Gateway 原型
  participant C as 浏览器控制台
  S->>G: 注册设备和能力
  C->>G: 查询设备
  C->>G: 申请独占会话
  G-->>C: 会话标识
  C->>G: 发送模拟数据
  G-->>C: 回显（不是 DAP）
  S->>G: 心跳
  S-xG: 断开或心跳过期
  G->>G: 删除设备并释放会话
```

## 权限与会话（目标生命周期，未实现）

当前原型仅以一个活跃会话和本机回环限制模拟独占，不代表生产级权限模型。生产配对应验证身份，再按设备与资源授权；租约、取消、断线清理和恢复语义需由 Gateway 与探针契约共同定义。

```mermaid
stateDiagram-v2
  [*] --> 未配对
  未配对 --> 已配对: 用户确认设备身份
  已配对 --> 可用: 认证与能力校验
  可用 --> 会话占用: 授权并锁定目标资源
  会话占用 --> 可用: 显式释放或租约结束
  会话占用 --> 恢复中: 连接中断
  恢复中 --> 可用: 重新认证并确认目标状态
  恢复中 --> 需人工恢复: 无法确认硬件状态
```

## OTA 生命周期（规划）

Gateway 调度任务，探针负责传输，目标 Bootloader 执行授权检查、安装和启动确认。当前没有镜像签名链、备份布局、Bootloader 或回滚能力；不得覆盖唯一有效镜像，也不得在开发阶段烧写 eFuse/RDP 等不可逆配置。

```mermaid
stateDiagram-v2
  [*] --> 待下载
  待下载 --> 已接收: 按块传输并校验长度
  已接收 --> 已验证: 验签、目标与版本策略通过
  已验证 --> 安装中: 保留已知有效镜像
  安装中 --> 待启动确认: 安装完整
  待启动确认 --> 已确认: 目标自检通过
  待启动确认 --> 回滚中: 启动失败或超时
  回滚中 --> 已恢复: 启动上一有效镜像
  已接收 --> 失败: 校验或传输失败
```

## Worktree 与发布流程

```mermaid
flowchart LR
  MAIN[main：稳定基线] --> WT[独立 Git worktree + 功能分支]
  WT --> TEST[本地检查与 CI]
  TEST --> PR[审查与合并]
  PR --> MAIN
  MAIN --> REL[显式授权后建立版本 Tag / Release]
```

`VERSION` 是项目版本唯一人工维护来源；协议、Schema、硬件修订和安全版本各自独立管理。普通提交、CI 和初始化不得自动改版本、打 Tag 或发布 Release。
