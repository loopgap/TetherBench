# ADR 0002：探针管理协议与数据面

- 状态：开放；仅冻结本机原型 API 的范围
- 日期：2026-11-22

## 决定

原型以 HTTP/JSON 表达本机管理操作，详细接口和限制见 [`contracts/mock-gateway-api.md`](../../contracts/mock-gateway-api.md)。它仅供本机 Gateway、浏览器和模拟器互操作，不是跨设备固件协议。

项目尚未确定 C5 到 Gateway 的管理报文格式，也未确定 TLS、配对、设备身份、流控或断线恢复语义。调试数据、固件块及持续 UART/Trace 数据属于数据面候选，不应逐字节封装为 JSON/CBOR。

## 选型依据与后续实验

OpenOCD 上游当前 CMSIS-DAP TCP backend 已定义 8 字节帧头：4 字节小端签名 `DAP`、2 字节小端负载长度、1 字节类型（请求 `0x01`/响应 `0x02`）和 1 字节保留位；最大负载 1024 字节（不含帧头），默认 TCP 端口 4441，阻塞读取最小超时 150 ms。该上游源码明确标注 TCP backend 当前不支持 UART 和 SWO。此处是上游源码观察，不代表本机已验证某个 OpenOCD 发布版，也不代表 TetherBench 兼容。

管理面需比较 MCUmgr SMP/CBOR、有限长度二进制头加 CBOR、Protobuf/nanopb；同时核对设备端代码体积、峰值 RAM、解析耗时、跨语言生成、未知字段处理、最大消息和拒绝服务边界。

CMSIS-DAP、SWD/JTAG、GDB RSP、DAP 和 Microsoft Debug Adapter Protocol 是不同层次的接口。OpenOCD 文档列出 CMSIS-DAP TCP 适配器，但这不等于任意 USB CMSIS-DAP 工具都能直接连接任意网络探针。正式决定前必须按目标 OpenOCD 版本验证报文、连接生命周期、长度、超时、UART/SWO 能力和断线行为。
