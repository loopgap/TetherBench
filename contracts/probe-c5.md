# Probe C5 跨工程接口

日期：2026-11-26。状态：协议版本 1 已有实现，完整系统验收尚未完成。

此文件是 Gateway、Probe C5、未来 Bridge/Hosted 项目访问共享 C5 的契约入口。权威字段和行为放在 [C5 协议](../firmware/probe-c5/docs/protocol.md)，实现、工具、详细文档和测试随独立 C5 工程维护。改变跨工程行为时同步修改文档、实现与调用方检查。

- TLS `POST /v1/rpc` 承载有限长度二进制帧，不使用 Gateway 的模拟 HTTP API 连接固件。
- C5 独占其 GPIO 和总线；其他固件不得依赖它的私有源码。
- UDP 4440 只提供定位信息，设备身份由本机预置证书确认。
- 租约统一拥有目标资源，到期后释放。负载上限 4096 字节，DAP 上限 1024 字节。
- 网络 CMSIS-DAP 不等于 USB 枚举。OpenOCD、虚拟 USB、CDC 等适配需独立实现和互操作证据。
- OTA 版本 1 仅支持 C5 自身，其他 target 返回 UNSUPPORTED；外部 MCU 的 bootloader、持久化与确认适配尚未实现。

产品版本仍以 `VERSION` 为单一来源。线协议版本独立，不兼容变更必须升级线协议版本。
