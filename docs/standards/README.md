# 标准与接口依据

以下是需要优先核对的原始规范。将某标准列在这里不代表产品已实现该标准。

| 领域 | 权威来源 | 当前用途 |
|---|---|---|
| TCP | [RFC 9293](https://www.rfc-editor.org/rfc/rfc9293) | 传输层基础 |
| TLS 1.3 | [RFC 8446](https://www.rfc-editor.org/rfc/rfc8446) | 候选安全传输；探针资源预算待测 |
| WebSocket | [RFC 6455](https://www.rfc-editor.org/rfc/rfc6455) | 浏览器实时 API 候选，尚未实现 |
| CBOR | [RFC 8949](https://www.rfc-editor.org/rfc/rfc8949) | 管理消息编码候选，尚未选型 |
| CDDL | [RFC 8610](https://www.rfc-editor.org/rfc/rfc8610) | CBOR 数据模型候选 |
| mDNS | [RFC 6762](https://www.rfc-editor.org/rfc/rfc6762) | 局域网发现候选 |
| DNS-SD | [RFC 6763](https://www.rfc-editor.org/rfc/rfc6763) | 服务发现候选 |
| QUIC | [RFC 9000](https://www.rfc-editor.org/rfc/rfc9000) | 仅在测量表明多流或恢复有收益时评估 |
| Arm CMSIS-DAP | [ARM-software/CMSIS-DAP](https://github.com/ARM-software/CMSIS-DAP) | 调试探针协议；不是 Microsoft DAP |
| GDB Remote Serial Protocol | [GDB 手册](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Remote-Protocol.html) | 调试器与 GDB Server 之间的协议 |
| Microsoft Debug Adapter Protocol | [DAP 规范](https://microsoft.github.io/debug-adapter-protocol/) | IDE 调试 UI 适配；与 CMSIS-DAP 不同 |
| ESP-IDF | [ESP-IDF v6.1.1 文档](https://docs.espressif.com/projects/esp-idf/en/v6.1.1/esp32c5/index.html) | C5 固件构建基线候选；本机未验证 |

下一步需要从候选管理协议中选定一个最小子集，定义帧长上限、字节序、未知字段、错误码、请求关联、超时/取消和兼容策略，并为 C、Rust/Go 实现维护同一组向量。连续 DAP、UART、Trace 和固件数据使用适合的二进制/流接口，不应逐字节 JSON 化。
