# 本机模拟 Gateway API

状态：原型接口，不是稳定公共 API，也不是 C5 固件协议。实现位于 `apps/gateway/`；浏览器、模拟探针和测试均通过 HTTP 调用，不直接读取 Gateway 内存。

## 边界与限制

- 只监听 `127.0.0.1`；不支持 LAN 暴露、TLS、配对或认证。
- 仅接受同源浏览器写请求；无 `Origin` 的本机命令行客户端允许调用。
- 所有 JSON 请求体最多 16 KiB，UTF-8 编码。
- 最多 64 个设备；每个设备最多 16 项能力。设备 ID/能力长度最多 64 字符，设备名最多 64 字符。
- 每个设备最多一个活动会话；模拟交换数据最多 4096 UTF-8 字节。
- 心跳默认 15 秒未更新即判定设备断开，并删除其会话。
- 状态仅驻留内存，Gateway 重启后清空。无设备持久身份、真实总线数据或 OTA。

错误响应统一为 `{ "error": { "code": "...", "message": "..." } }`。设备 ID 格式为字母或数字开头，后续仅允许字母、数字、点、下划线和连字符。

## 端点

新增会话恢复端点：GET /api/v1/sessions/{id} 返回活动会话对象，已释放、设备失效或 Gateway 重启后返回 404。控制台在 sessionStorage 保存当前页面拥有的会话 ID，刷新时重新验证；列表不会暴露其他客户端的会话 ID。浏览器探针也在刷新时核对存活状态。写操作限制为一个在途请求，心跳串行执行，单次网络请求最多等待 5 秒。

| 方法与路径 | 请求 | 成功响应 |
|---|---|---|
| `GET /api/v1/health` | 无 | `state`、在线设备数、活动会话数 |
| `GET /api/v1/devices` | 无 | `{ "devices": [...] }` |
| `POST /api/v1/devices` | `{ "id", "name", "capabilities": [] }` | `201` 和设备对象 |
| `POST /api/v1/devices/{id}/heartbeat` | 空请求体 | 更新时间后的设备对象 |
| `DELETE /api/v1/devices/{id}` | 无 | `{ "disconnected": true, "releasedSession": true/false }` |
| `POST /api/v1/devices/{id}/sessions` | 空请求体 | `201` 和 `{ "id", "deviceId", "createdAt" }` |
| `POST /api/v1/sessions/{id}/exchange` | `{ "data": "..." }` | `{ "data": "...", "byteLength": n }`，仅回显模拟数据 |
| `DELETE /api/v1/sessions/{id}` | 无 | `{ "released": true, "id" }` |

常见状态码：`400` 无效 JSON/字段，`403` 来源不匹配，`404` 资源不存在，`409` 重复设备或会话已占用，`413` 请求体超限，`415` Content-Type 不支持，`429` 设备容量已满。

**原型与正式协议的分界：**本接口使用 JSON 仅为本机软件联调。正式管理协议仍需比较 MCUmgr SMP/CBOR、有限帧加 CBOR 和 Protobuf/nanopb；CMSIS-DAP、UART、Trace、固件数据需使用正确的二进制/流接口。
