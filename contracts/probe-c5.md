# Probe C5 跨工程接口

日期：2026-11-27。状态：协议版本 1 已实现并进入硬件验收；完整系统验收尚未完成。

此文件是 Gateway、Probe C5、未来 Bridge/Hosted 项目访问共享 C5 的契约入口。权威字段和行为放在 [C5 协议](../firmware/probe-c5/docs/protocol.md)，本机 Gateway 的浏览器接口放在 [C5 Gateway API](c5-gateway-api.md)。实现、工具、详细文档和测试随独立 C5 工程维护。改变跨工程行为时同步修改文档、实现与调用方检查。

P4 USB 桥接路线使用独立的 [P4 帧契约](bridge-p4.md)，与本文件的纯 C5 无线 RPC 验收分开计证。

- TLS `POST /v1/rpc` 承载有限长度二进制帧，不使用 Gateway 的模拟 HTTP API 连接固件。
- C5 独占其 GPIO 和总线；其他固件不得依赖它的私有源码。
- RPC 字段、状态码和事务恢复以 [C5 协议](../firmware/probe-c5/docs/protocol.md) 为准：格式错误、当前状态冲突、资源忙和 I/O 失败必须使用各自定义的状态码。
- UDP 4440 只提供定位信息，设备身份由本机预置证书确认。
- `INFO.wifi.he20` 是稳定的协议布尔值，仅当 STA 实际协商到 802.11ax HE20 时为 true；AP 模式为 null。`INFO.wifi.phy` 是 IDF 诊断枚举，不得作为 Wi-Fi 6 兼容判断。
- C5 可配置为 STA 或独立 SoftAP；AP 与 STA 不同时运行。AP 模式凭据保存在本机 `.local/ap-network.json`，并以 `INFO.wifi.ap` 标识；单射频一次只有一个频段/信道。AP/STA 配对的 Gateway 自动切频协商尚未验收，当前 Gateway 仅接受 STA 角色。
- 租约统一拥有目标资源，到期后释放。负载上限 4096 字节，DAP 上限 1024 字节。
- 已定义的 UART/SPI/I²C 无线透传是有界 BUS 单次事务（每次收发最多 1024 字节、超时 1..100 ms），不是持续字节流；扩展协议时保留 ID 1..4，新增 ID 必须同时定义引脚角色、能力声明、事务边界、超时和复位释放行为，并同步固件、Gateway、控制台和回归检查。
- 可编程 I/O 重配的调用流程：先读取 `INFO.allowedPins` 与 `CONFIG` 并展示候选映射和当前占用。界面可同时暂存多个协议的候选映射；但每次 `PINS` RPC 只配置或释放一个协议，不提供跨协议原子事务。客户端检查同设备其他协议的 GPIO 冲突，但该检查只是预览，C5 的 PINS 校验是权威。若换脚需要先释放另一个协议的占用，提交前明确说明将按步骤释放并配置；每一步后读取 `CONFIG`。任何一步失败都展示读回的实际部分状态，不得声称旧映射已恢复。网页不能取消已经提交的 DAP/BUS RPC；同一设备须等待有界 RPC 结束，再刷新配置并二次确认重配。RPC 超时或断连导致结果不明时，重新读取 `INFO`、`CONFIG` 和相关 OTA 状态后再决定；不得盲目重放不同配置。C5 OTA、目标 MCU OTA、`INFO.bandStage` 非 IDLE 或状态不确定时暂停 PINS；先通过该状态机支持的安全 ABORT/终态查询或 `BAND_CANCEL` 恢复，并确认状态后再重试。切频 FINALIZE 后的补偿窗口不占用 GPIO，也不额外阻止 PINS。
- 板级引脚名称与用途必须来自明确匹配的 board profile。`allowedPins` 和 `CONFIG` 分别说明固件掩码与当前协议占用，不证明开发板上引脚空闲；不得从 `esp32c5-n4` 名称推断板级接线。
- 对 PINS，格式/范围/占用校验失败不得修改当前配置；同协议重配若驱动初始化失败，则清理新驱动并返回错误，协议保持未配置，不能将旧配置仍在使用或重配成功反馈给用户。跨协议 GPIO 占用返回 `CONFLICT`，驱动错误返回 `IO`；OTA/切频锁拒绝 PINS，状态码按固件协议处理。
- 网络 CMSIS-DAP 不等于 USB 枚举。OpenOCD、虚拟 USB、CDC 等适配需独立实现和互操作证据。
- C5 自身支持签名双槽 OTA；外部 MCU OTA 使用 [Target MCU OTA v1](target-mcu-ota.md)，C5 已实现 UART/SPI 转发、manifest 预检、持久资源锁和恢复状态查询。目标 MCU bootloader、独立验签与回滚能力以及 UART/SPI 真机闭环仍未验证，不能据此宣称目标 MCU OTA 已可用。

产品版本仍以 `VERSION` 为单一来源。线协议版本独立，不兼容变更必须升级线协议版本。
