# Probe C5 跨工程接口

日期：2026-11-27。状态：协议版本 1 已实现并进入硬件验收；完整系统验收尚未完成。

此文件是 Gateway、Probe C5、未来 Bridge/Hosted 项目访问共享 C5 的契约入口。权威字段和行为放在 [C5 协议](../firmware/probe-c5/docs/protocol.md)，本机 Gateway 的浏览器接口放在 [C5 Gateway API](c5-gateway-api.md)。实现、工具、详细文档和测试随独立 C5 工程维护。改变跨工程行为时同步修改文档、实现与调用方检查。

P4 USB 桥接路线使用独立的 [P4 帧契约](bridge-p4.md)，与本文件的纯 C5 无线 RPC 验收分开计证。

- TLS `POST /v1/rpc` 承载有限长度二进制帧，不使用 Gateway 的模拟 HTTP API 连接固件。
- C5 独占其 GPIO 和总线；其他固件不得依赖它的私有源码。
- RPC 字段、状态码和事务恢复以 [C5 协议](../firmware/probe-c5/docs/protocol.md) 为准：格式错误、当前状态冲突、资源忙和 I/O 失败必须使用各自定义的状态码。
- UDP 4440 只提供定位信息，设备身份由本机预置证书确认。
- C5 可配置为 STA 或独立 SoftAP；AP 与 STA 不同时运行。AP 模式凭据保存在本机 `.local/ap-network.json`，并以 `INFO.wifi.ap` 标识；单射频一次只有一个频段/信道。AP/STA 配对的 Gateway 自动切频协商尚未验收，当前 Gateway 仅接受 STA 角色。
- 租约统一拥有目标资源，到期后释放。负载上限 4096 字节，DAP 上限 1024 字节。
- 已定义的 UART/SPI/I²C 无线透传是有界 BUS 单次事务（每次收发最多 1024 字节、超时 1..100 ms），不是持续字节流；扩展协议时保留 ID 1..4，新增 ID 必须同时定义引脚角色、能力声明、事务边界、超时和复位释放行为，并同步固件、Gateway、控制台和回归检查。
- 网络 CMSIS-DAP 不等于 USB 枚举。OpenOCD、虚拟 USB、CDC 等适配需独立实现和互操作证据。
- C5 自身支持签名双槽 OTA；外部 MCU OTA 使用 [Target MCU OTA v1](target-mcu-ota.md)，C5 已实现 UART/SPI 转发、manifest 预检、持久资源锁和恢复状态查询。目标 MCU bootloader、独立验签与回滚能力以及 UART/SPI 真机闭环仍未验证，不能据此宣称目标 MCU OTA 已可用。

产品版本仍以 `VERSION` 为单一来源。线协议版本独立，不兼容变更必须升级线协议版本。
