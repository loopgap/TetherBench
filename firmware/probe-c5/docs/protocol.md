# Probe C5 协议版本 1

日期：2026-11-27。已有源码实现；真实验证范围见 [验收记录](validation.md)。所有多字节整数均为小端，字节偏移从 0 开始。协议字段有定义不等于所有硬件适配已经验证。

## 传输和设备身份

HTTPS TCP 443，`POST /v1/rpc`，请求与响应 `Content-Type: application/octet-stream`。请求头 `Authorization: Bearer ` 后恰好 64 个十六进制字符。身份失败 HTTP 401；帧、长度或 Content-Type 错误 HTTP 400，关闭连接。调用方不能跟随重定向发送令牌。

设备使用本机生成的 P-256 证书。Python 客户端先比较预置 PEM 对应的完整 DER 证书，成功后才发送 HTTP 请求；禁止 HTTP 库隐式重连，每个新 TLS 连接都重新验证。发现回复里的证书摘要只能提供提示，不能自动配对。证书轮换需要受信任的本机配置更新。

最多两个 TLS socket；HTTP 服务可并行接收连接，但 RPC 处理器用独立互斥量串行保护固定请求、响应和最近事务缓冲，设备状态仍由控制互斥量保护。握手期限 5 秒，收发等待 2 秒，请求体总读取期限约 5 秒。没有无限任务队列。局域网攻击者仍可占用连接资源，不能承诺消除无线拒绝服务。

| 帧偏移 | 字节数 | 内容 |
|---|---:|---|
| 0 | 4 | ASCII `TBC5` |
| 4 | 1 | 版本 1 |
| 5 | 1 | opcode |
| 6 | 2 | 请求 0；响应 `0x8000 OR status` |
| 8 | 4 | request_id，原样回传 |
| 12 | 4 | payload_length，0..4096 |
| 16 | 8 | lease，原样回传；新租约放 ACQUIRE 响应负载 |
| 24 | 可变 | 负载，HTTP body 必须恰好含一帧 |

| status | 名称与恢复依据 |
|---:|---|
| 0 | OK |
| 1 | INVALID：长度、字段、范围错误 |
| 2 | UNAUTHORIZED：保留；当前使用 HTTP 401 |
| 3 | BUSY：资源或任务占用 |
| 4 | LEASE：租约缺失、不匹配或过期 |
| 5 | UNSUPPORTED：命令或目标未实现 |
| 6 | IO：驱动、Flash 或持久化失败 |
| 7 | INTEGRITY：签名、镜像或内容失败 |
| 8 | STATE：当前状态不允许 |
| 9 | CONFLICT：引脚、偏移、重复数据冲突 |
| 10 | TIMEOUT：执行期限已到 |

调用方检查 HTTP 状态、魔数、版本、opcode、request_id、lease、状态和精确长度。未知状态当作失败。

最近一次成功修改及回复最多保留 60 秒。完全相同的帧回复缓存；同一 opcode/request_id/lease 内容不同返回 CONFLICT。查询不替换缓存，另一成功修改会替换它；租约过期清空缓存。这不是持久化的全局 exactly-once 保证。同一租约修改必须串行提交，结果不明时不得自动重放 DAP/BUS。OTA WRITE 另有重复块比较规则。

## 租约与查询

| opcode | 名称 | 请求负载 | 成功响应 |
|---:|---|---|---|
| 1 | INFO | 空 | UTF-8 JSON |
| 2 | ACQUIRE | ttl_ms u32，lease=0 | lease u64、ttl_ms u32 |
| 3 | RENEW | ttl_ms u32 | 空 |
| 4 | RELEASE | 空 | 空 |
| 5 | PINS | 12 字节配置 | 空 |
| 6 | CONFIG | 空 | 四条 12 字节配置 |
| 63 | REBOOT | 空 | 空，约 500 ms 后重启 |

INFO、CONFIG、OTA_STATUS 仅要求有效设备认证，其他操作必须拥有租约，ACQUIRE 除外。租约为随机非零 64 位，TTL 5000..60000 ms。释放、到期、重启会删除驱动并将目标脚设为输入。断开网络连接不等于释放租约。更新任务留在 Flash，可由新的认证租约继续。

INFO 字段：id、board、version、idf、protocol、maxPayload、dapPacket、leaseActive、allowedPins、freeHeap、minHeap、wifi（ip/ap/channel/rssi/phy/he20）、otaAvailable、pendingBoot、bandStage、capabilities。STA 模式的 wifi.rssi 是关联 AP 的信号强度；AP 模式返回已关联客户端中的最低 RSSI，未关联或读取失败时为 -127。它表示当前链路，不预测候选信道。`wifi.he20` 是稳定的协议布尔字段：STA 模式下仅当驱动确认实际协商为 802.11ax HE20 时为 true；AP 模式为 null，因为固件只读取关联客户端的 11ax 能力位，不能据此证明每帧都使用 HE20。`wifi.phy` 仅供诊断，表示当前 ESP-IDF 的 `wifi_phy_mode_t` 数值；ESP-IDF 6.1.1 中 HE20=6，STA 查询失败或 AP 模式为 -1。控制器必须按 `wifi.he20` 判断 STA 的协议条件，不得硬编码 SDK 枚举。

## 引脚与数据事务

12 字节配置：protocol u8、count u8、reserved u16=0、hz u32、pins[4] i8。负载长度不是 12 或 reserved 非零返回 INVALID；字段范围错误返回 INVALID、GPIO 冲突返回 CONFLICT、驱动配置失败返回 IO；C5 自身 OTA 或切频事务活动期间返回 STATE。未用脚为 -1。count=0、hz=0、四脚全 -1 表示释放该协议。同协议重配先释放原驱动；新驱动失败时会清理新驱动并保持该协议未配置。参数/冲突校验在释放旧驱动之前完成，因此这些校验失败时旧配置保留。不同协议禁止共用任何 GPIO。CONFIG 按 SWD/UART/SPI/I2C 返回四条记录。

控制台或其他调用方进行可编程 I/O 重配时，先读取 INFO.allowedPins 与 CONFIG，再预览候选协议、GPIO、信号角色、时钟和现有占用。客户端根据 CONFIG 检查其他协议是否已占用候选 GPIO，并根据明确匹配的板级 profile 标注引脚功能；该检查只用于预览，CONFIG 可能已过期，C5 的 PINS 校验始终是冲突与可用性判断的权威。allowedPins 是固件允许的 GPIO 位掩码，不代表这些脚在某块开发板上未接外设，也不替代板级 profile；不得仅凭 `esp32c5-n4` 板名推断开发板接线。缺少匹配 profile 时只能将 GPIO 作为未映射编号展示，不能声称其板级用途或安全可接。

界面可以暂存多个协议的候选映射，但每次 PINS RPC 只配置或释放一个协议，不是跨协议原子事务。若一个候选映射要使用另一个已配置协议占用的 GPIO，先向用户预览冲突和分步动作，再逐个释放/配置，并在每一步后读取 CONFIG。任一步失败时按 CONFIG 显示真实部分状态；PINS 不提供跨协议自动回滚，调用方不得报告原映射已恢复。

PINS 是同步、不可由网页取消的设备操作。同一设备已有 DAP 或 BUS RPC 执行时，客户端必须等待该有界 RPC 返回后才能再次读取 CONFIG/allowedPins、展示二次确认并提交 PINS；不得并发提交或用浏览器取消当作总线操作已停止。若请求超时、断连或响应无法验证，状态视为不确定：先重新认证/取得租约并读取 CONFIG、INFO 及适用的 OTA/切频状态，确认设备状态后才可重试；不得把超时解释为 PINS 成功或失败。

C5 自身 OTA、UART/SPI 目标 MCU OTA、非 IDLE 切频阶段或任何状态不确定期间，调用方必须禁用 PINS 并由 C5 拒绝重配。目标 OTA 期间（包括目标更新存储状态故障）PINS 返回 BUSY；C5 OTA 或 band.stage 非 IDLE 返回 STATE。先使用协议允许的安全恢复路径：未激活的 OTA 可在其状态机允许时 ABORT；目标 OTA 仅可在试启动前安全 ABORT，已激活后查询终态并等待 CONFIRMED/ROLLED_BACK；已知切频事务用 BAND_CANCEL 并核实恢复结果。恢复操作的结果未确认前不得重试 PINS。响应丢失或超时后，重新读取 INFO、CONFIG 及相关 OTA 状态，再按读回的真实状态决定下一步，不得盲目重放不同配置。BAND_FINALIZE 后的 15 秒补偿窗口仅影响切频恢复，不代表 GPIO 资源仍被占用，且不额外阻止 PINS。

| protocol | 名称 | 脚位顺序 | count | 请求上限 |
|---:|---|---|---|---|
| 1 | SWD | SWCLK、SWDIO、可选开漏 nRESET | 2 或 3 | 1 kHz..4 MHz |
| 2 | UART1 | TX、RX、可选 RTS、CTS | 2 或 4 | 5 Mbaud，8N1 |
| 3 | SPI2 | SCLK、MOSI、MISO、CS | 4 | 20 MHz，mode 0 |
| 4 | I2C0 | SCL、SDA | 2 | 1 MHz，外部上拉 |

上限不是实测速度。当前 `WT9932C5-TINY` profile 允许 GPIO0、1、8、9、10、24，避开板载 RGB、USB、UART、Flash 和启动/板级保留脚；完整排针位置、P4 互连和纯 C5 SWD 接线见 [Wireless-Tag Tiny 板级引脚配置](../../../contracts/wt-tiny-pin-profile.md)。P4↔C5 SPI 的固定 1024 字节事务、IRQ、复位和恢复规则见共享 [P4-C5 SPI 传输契约](../../../contracts/p4-c5-spi-transport.md)。N4 不足以证明任一脚的实测波形，默认无映射；连接前核对电平、上拉、占用与接地。没有目标供电控制。

opcode 16 DAP：原生 CMSIS-DAP 请求/响应，最大 1024 字节。支持 Info、HostStatus、Connect/Disconnect、TransferConfigure、Transfer、TransferBlock、WriteABORT、Delay、ResetTarget、SWJ_Pins/Clock/Sequence、SWD_Configure/Sequence、最多 16 条命令的 ExecuteCommands。Transfer 和 TransferBlock 最多 255 项，并按 1024 字节的实际请求、响应预算进一步限制；例如 block-read 最多 255 个字，block-write 最多 254 个字。Sequence 最多 64 组。TransferConfigure 的 idle_cycles 为 0..32，以限制 1 kHz 时的停顿；wait_retry 和 match_retry 接受完整 u16 范围。未映射 SWD 时仅允许 Info、HostStatus、Disconnect。没有 JTAG、SWO、CMSIS UART、QueueCommands 和厂商命令。C5 本机执行 GPIO，单请求 250 ms 预算；SWD 事务重试逐次检查期限，SWD Sequence 至少每 8 个时钟检查一次。超时后关闭 SWD 并复位核心；软件预算不代替波形测量。

opcode 17 BUS：12 字节头 protocol u8、flags u8=0、address u16、tx_length u16、rx_length u16、timeout_ms u16、reserved u16=0，然后 tx_length 字节。收发各最多 1024，至少一个非零，timeout 1..100 ms。成功响应是接收到的原始字节。

UART 可返回少于请求的接收字节；发送超时释放 UART。UART/SPI address=0。SPI 同时收发要求 tx_length=rx_length，单次 CS 事务。I2C 地址 0x08..0x77，先写后读使用重复起始。UART/SPI 检查理论传输时间；实际等待受 FreeRTOS tick 和驱动调度影响。

BUS 是经 Wi-Fi RPC 执行的有界原始字节事务，不提供 UART 持续字节流、异步推送或无限缓冲。UART RX 可短读；调用方须依据本次返回长度处理数据，连接结果不明确时先读取设备/目标状态，不能自动重放事务。SPI 每个请求对应一次 CS 事务；UART 为 8N1，SPI 仅 mode 0。以上速率是配置上限，不代表无线吞吐或 GPIO 实测值。

协议 ID 1..4（SWD、UART、SPI、I2C）和 opcode 16..17 固定兼容。新增总线协议使用新 ID，不重解释现有编号；新增项必须同时为引脚角色、合法时钟、最大事务、超时、短读/返回长度、资源互斥与租约释放、能力声明及错误/恢复语义定义行为，并更新本机 Gateway、控制台、客户端和回归测试。当前 `INFO.capabilities` 明确列出现有协议。持续流传输需单独版本化，定义双向帧、流控/背压、连接恢复与租约取消，不能复用 BUS 轮询冒充透明串流。

开始更新/切频前必须释放所有协议；更新和切频期间拒绝 DAP/BUS。

## 签名双槽 OTA

| opcode | 名称 | 请求负载 | 成功响应 |
|---:|---|---|---|
| 32 | OTA_BEGIN | 176 字节已签名 manifest | 28 字节状态 |
| 33 | OTA_WRITE | job_id u64、offset u32、1..4084 字节 | 28 字节状态 |
| 34 | OTA_FINISH | job_id u64 | 空 |
| 35 | OTA_ACTIVATE | job_id u64 | 空，约 500 ms 后重启 |
| 36 | OTA_STATUS | 空 | 28 字节状态 |
| 37 | OTA_ABORT | job_id u64 | 空 |
| 38 | BOOT_CONFIRM | 空 | 空 |

manifest 前 112 字节参与 ECDSA P-256/SHA-256 签名；末尾签名是 32 字节大端 r 紧接 32 字节大端 s，不是 ASN.1 DER。公钥编译进入固件，私钥留在本机。

| 偏移 | 字节数 | 内容 |
|---|---:|---|
| 0 | 8 | 非零 job_id |
| 8 | 1 | target，0 为此 C5；其他返回 UNSUPPORTED |
| 9 | 1 | flags=0 |
| 10 | 2 | reserved=0 |
| 12 | 4 | 完整镜像长度，不得超过非运行槽 |
| 16 | 32 | 完整镜像 SHA-256 |
| 48 | 32 | NUL 结尾 version，须与 app descriptor 一致 |
| 80 | 32 | NUL 结尾 board_id，必须 `esp32c5-n4` |
| 112 | 64 | P-256 签名 |

状态 28 字节：job_id u64、phase u32、received u32、checkpoint u32、total u32、pendingBoot u32。phase：0 EMPTY、1 RECEIVING、2 VERIFIED、3 ACTIVATED、4 CONFIRMED、5 FAILED。

BEGIN 检查格式、签名、board、大小和启动确认状态。仅操作非运行槽，先持久化新签名任务再擦写，避免掉电后新内容被旧 VERIFIED 任务激活。同一签名 manifest 可恢复接收，不同任务返回 BUSY。启动时会校验 journal 的目标分区、阶段与偏移：RECEIVING 必须指向当前下一 OTA 槽且检查点为 16 KiB 对齐；VERIFIED 必须仍指向下一 OTA 槽；VERIFIED、ACTIVATED、CONFIRMED 的偏移必须等于镜像总长。ACTIVATED 在新镜像启动后允许目标分区等于当前运行槽；若设备仍运行旧镜像则该任务标记 FAILED。违反这些条件的 journal 会让 OTA 初始化失败，不会擦除 NVS。

WRITE 按顺序；已接收范围内相同块读回比较后成功，不同内容或跨越当前边界返回 CONFLICT。每跨越 16 KiB 持久化对齐检查点。重启后 received 回到 checkpoint；重新提交相同 BEGIN，从设备状态恢复，不能从主机上次发送位置恢复。检查点之后的数据可重擦重传。Flash 写失败终止句柄并标记 FAILED。

FINISH 要求完整长度，从 Flash 重新计算摘要、检查 project_name=`tetherbench_probe_c5`、version、IDF 镜像有效性，成功才持久化 VERIFIED。ACTIVATE 先持久化再选择启动槽。新镜像运行后必须在 45 秒内通过认证、持租约提交 BOOT_CONFIRM，且网络已就绪；否则由 IDF 回滚。断网不损坏旧运行槽。

ABORT 不允许撤销 ACTIVATED，删除持久任务失败不报告成功。日志损坏则禁用更新，在 INFO 显示，不擦整个 NVS。没有 eFuse 防降级，旧版本有效签名仍可安装。两槽是运行镜像和候选镜像，连续更新会复用旧槽，并非三份备份。

| 分区 | 偏移 | 大小 |
|---|---|---|
| nvs | 0x9000 | 0x6000 |
| otadata | 0xf000 | 0x2000 |
| phy_init | 0x11000 | 0x1000 |
| ota_0 | 0x20000 | 0x1e0000 |
| ota_1 | 0x200000 | 0x1e0000 |
| ota_journal | 0x3e0000 | 0x10000 |
| coredump | 0x3f0000 | 0x10000 |

外部 MCU 的签名双槽更新契约定义在 [Target MCU OTA v1 契约](../../../contracts/target-mcu-ota.md)。C5 端的 RPC 39..45 转发、目标身份校验和 NVS 恢复锁已实现；目标 MCU bootloader、受保护密钥、双槽回滚及 UART/SPI 实机闭环仍未实现或验证。因此，C5 主机模型与固件构建不能证明任意连接 MCU 支持安全 OTA。SWD 通过 CMSIS-DAP 调试，不伪装成外部 bootloader 更新支持。

## 外部 MCU OTA 扩展（C5 转发已实现，目标侧未闭环）

外部 MCU 不能使用 C5 的 176 字节自更新 manifest：目标 MCU 使用带签名回滚索引的 184 字节 manifest，并要求目标 bootloader 独立校验签名、镜像摘要、双槽、持久检查点、试启动确认和回滚。目标与 C5 的详细物理帧、UART COBS、SPI 轮询、能力字段和恢复规则见 [Target MCU OTA v1 契约](../../../contracts/target-mcu-ota.md)。

C5 RPC opcode 39..45 为 `TARGET_INFO`、`TARGET_OTA_BEGIN`、`TARGET_OTA_WRITE`、`TARGET_OTA_FINISH`、`TARGET_OTA_ACTIVATE`、`TARGET_OTA_STATUS` 和 `TARGET_OTA_ABORT`。C5 实现目标帧编解码、HELLO/状态解析、签名 manifest 预检、UART/SPI 转发及 NVS 持久资源锁；这些 RPC 是 C5 到目标 bootloader 的命令接口，不代表目标端实现已存在，也不能替代目标独立验签、双槽、持久检查点、试启动确认和回滚。主机替身测试只覆盖 C5 逻辑。

| opcode | 请求 | 成功响应 |
|---:|---|---|
| 39 TARGET_INFO | UART=2 或 SPI=3 u8 | 180 字节 HELLO 能力、身份和 OTA 状态 |
| 40 TARGET_OTA_BEGIN | protocol u8（UART=2/SPI=3）+ 184 字节 target manifest，共 185 字节 | 64 字节状态 |
| 41 TARGET_OTA_WRITE | `job_id` u64、`offset` u32、1..983 字节 | 64 字节状态 |
| 42 TARGET_OTA_FINISH | `job_id` u64 | 64 字节状态 |
| 43 TARGET_OTA_ACTIVATE | `job_id` u64 | 64 字节状态 |
| 44 TARGET_OTA_STATUS | 有更新锁时为空；无更新锁时 protocol u8（UART=2/SPI=3） | 64 字节状态 |
| 45 TARGET_OTA_ABORT | `job_id` u64 | 64 字节状态 |
RPC 41..45 返回目标的 64 字节状态记录。`received` 是目标已写入的连续偏移，`checkpoint` 才是复位后保证保留的连续偏移；主机恢复必须从 `checkpoint` 开始，`received` 超前时允许重发相同字节。对 `VERIFIED`、`TRIAL`、`CONFIRMED`、`ROLLED_BACK`，C5 要求 `received == checkpoint == total`；对已确认状态，回滚计数器必须等于签名 manifest 值，其他阶段必须小于该值。状态不变式和幂等要求以 [Target MCU OTA v1 契约](../../../contracts/target-mcu-ota.md) 为准。

状态的 phase/result 组合必须严格匹配：`EMPTY/NONE` 表示整条状态记录全零；有任务时 `RECEIVING/NONE`、`VERIFIED/NONE`、`ABORTED/NONE`、`FAILED/NONE`、`TRIAL/TRIAL_PENDING`、`CONFIRMED/CONFIRMED`、`ROLLED_BACK/ROLLED_BACK` 才有效。其他组合视为格式错误并保留更新锁。

TARGET_INFO 是只读预检，要求唯一 UART/SPI 总线已配置，并返回经验证的 HELLO 记录；它不设置更新锁，也不代表目标符合 OTA 能力要求。Gateway 可展示其能力记录；上传入口在转发前拒绝缺少必需 OTA 能力的目标。opcode 40 的协议字节选择已配置的 UART 或 SPI，并紧接 184 字节 manifest；C5 在持久化同一总线的恢复锁之前核对 board ID、镜像容量、全部必需安全能力及非零目标 UID。若 C5 重启时已持久化锁而目标仍报告 EMPTY，C5 仅可用完全相同的 journal manifest 和 UID 重发 BEGIN，并重新检查当前回滚索引。锁定期间每次状态查询或可能改变目标状态的 RPC 前，C5 均通过 HELLO 验证 journal 中记录的 UID；不匹配、异常响应或总线错误都会保留锁并拒绝操作。

opcode 44 兼容两种查询形式：空负载只读取当前持久化更新任务，必须已有更新锁；单字节 UART/SPI 协议号用于无更新锁时读取该总线 HELLO 中的状态记录。无锁查询仍要求该总线已配置且另一 UART/SPI 总线未配置。持锁时可省略协议号；若提供，必须与 journal 记录的协议一致。无锁 HELLO 状态查询不创建或清除更新锁。

目标帧响应的状态码非 OK 时 payload 必须为空；携带 payload 的错误响应按格式错误处理。目标 OTA 持久锁期间所有 PINS 请求均被拒绝，包括 journal 所选的同一 UART/SPI 协议；目标存储状态故障也封锁 PINS。锁定期间 DAP、原始 BUS、切频及 C5 OTA 写入均被拒绝。C5 只在目标明确报告 CONFIRMED、ABORTED 或 ROLLED_BACK 后清锁。目标 UID 校验会在每次更新 RPC 前多产生一次 HELLO 总线事务，吞吐开销需要后续在每种总线上测量；目前没有目标板实测数据。

## Wi-Fi 和协商切频

C5 使用双频 AUTO，协议能力含 802.11ax，但只有一条射频链路，同一时刻只工作于一个频段和信道。默认 STA 从本机配置连接热点，在 WIFI_EVENT_STA_CONNECTED 时检查协商 PHY；只有 HE20 才继续等待 IP，非 HE20 或 PHY 查询失败时立即断开。HE20 关联后 10 秒仍未获得 IP 时断开。重连采用 1、2、4、8、16、30 秒封顶退避；每个到期点只发起一次连接请求，下一次退避由断开事件安排。SDK 返回 ESP_ERR_WIFI_CONN 时等待当前连接尝试的驱动事件，不重复发起请求；其他同步错误按退避重试。成功取得 IP 后重置退避。`provision.py --ap` 配置独立 SoftAP，不启用 STA 回程；其 SSID 和随机强密码保存在忽略提交的 `.local/ap-network.json`，可用同一个文件把另一台 C5 配成 STA。AP 启动信道为 6，可按准备好的事务切到允许信道；关联客户端收到信道切换公告。AP 会拒绝不支持 802.11ax 的客户端；关联列表检查不等于逐帧 PHY 证明。

INFO 的 `wifi.ap` 标明独立 AP 角色。STA 的 `wifi.ip` 是上行地址，`wifi.rssi` 和 `wifi.phy` 表示关联 AP 的链路；AP 的 `wifi.ip` 是本机 AP 地址，`wifi.rssi` 是关联客户端中最弱的读数，`wifi.phy=-1` 且 `wifi.he20=null`，因为设备自身没有 STA 上行链路。AP 模式要求至少一个关联且都支持 11ax 的客户端，才允许 BAND_CONFIRM。`wireless-console` 分支的 Gateway 自动协商当前只接受 STA 角色；该分支目前仍按 `wifi.phy===5` 判断 HE20，须在本分支修复后同步改用 `wifi.he20===true`，AP/STA 配对协商尚未完成端到端验收。

opcode 48 WIFI_SCAN：空请求，首次启动异步扫描并连续进行五轮有界主动扫描，期间返回 BUSY；完成后返回 count u32 加最多 16 条 16 字节：BSSID[6]、channel u8、五次观测的中位 RSSI i8、五次均支持时为 1 的 phy_11ax u8、authmode u8、实际观测次数 u8、reserved u8=0、最早样本年龄 age_ms u32。STA 只扫描配置的目标 SSID；独立 AP 扫描可见网络以评估候选信道。单轮超过 5 秒或驱动错误丢弃整组结果。读取后释放，下次调用重新扫描。Gateway 可用 sightings/5 估算扫描漏检率；STA 共同候选按信道匹配，两个频段可以使用不同 BSSID。年龄和样本新鲜度用于拒绝超过 30 秒的扫描结果，扫描不能代替当前链路 RTT/丢包测量。

opcode 49 BAND_PREPARE：transaction_id u64、candidate_channel u8、current_channel u8、reserved u16=0，再接四条 link，共 60 字节。link 顺序为当前本端、当前对端、候选本端、候选对端；每条 rssi i16、loss_permille u16、rtt_ms u16、samples u16、age_ms u32。至少 5 样本、年龄不超过 30 秒；RSSI -110..-1 dBm、丢包 0..1000 千分比。当前链路 RTT 为 0..5000 ms；候选信道来自相邻扫描结果，rtt_ms=0xffff 表示无主动 RTT 样本，评分只使用 RSSI 和扫描未命中率，不得用该值标记当前链路。

采用两端最差分数：RSSI 在 -90..-40 dBm 映射到 0..100，减去 loss_permille/10 和 rtt_ms/5，整数截断。候选至少改善 10 分；成功后驻留 30 秒。允许信道 1..11、36、40、44、48，仍受地区规则和驱动约束。

opcode 50 BAND_COMMIT：transaction_id u64、delay_ms u32（500..3000）。PREPARE 5 秒过期；COMMIT 到期切换；切换后 15 秒内无 CONFIRM 则回退。opcode 51 BAND_CONFIRM：transaction_id u64；设备要求网络已就绪、实际信道匹配、RSSI 至少 -80 dBm，且 STA 协商到 HE20；AP 模式要求至少一个已关联的 11ax 客户端且所有关联客户端均支持 11ax。任一条件不满足就拒绝确认，等待自动回退；成功后进入 CONFIRMED，暂留 15 秒补偿窗口。opcode 52 BAND_CANCEL：transaction_id u64，可取消 PREPARED/COMMITTED 阶段；已切换或已确认且仍在 15 秒窗口内时恢复旧信道；重复取消为幂等操作。opcode 53 BAND_FINALIZE：transaction_id u64，双端都 CONFIRM 后结束这次协调并把设备置为 IDLE，同时保留 15 秒的显式补偿窗口。BAND_FINALIZE 后释放或到期租约不会清除此补偿窗口，新租约可在窗口内发送 BAND_CANCEL。相反，非 IDLE 阶段遇到 RELEASE 或租约到期会取消事务；若已切换则先恢复旧信道，再释放协议资源和租约。恢复失败时返回 IO 并保留事务与租约供后续重试。bandStage：0 IDLE、1 PREPARED、2 COMMITTED、3 SWITCHED、4 CONFIRMED。
opcode 50 BAND_COMMIT：transaction_id u64、delay_ms u32（500..3000）。PREPARE 5 秒过期；COMMIT 到期切换；切换后 15 秒内无 CONFIRM 则回退。opcode 51 BAND_CONFIRM：transaction_id u64；设备要求网络已就绪、实际信道匹配、RSSI 至少 -80 dBm，且 STA 协商到 HE20；AP 模式要求至少一个已关联的 11ax 客户端且所有关联客户端均支持 11ax。任一条件不满足就拒绝确认，等待自动回退；成功后进入 CONFIRMED，暂留 15 秒补偿窗口。opcode 52 BAND_CANCEL：transaction_id u64，可取消 PREPARED/COMMITTED 阶段；已切换或已确认且仍在 15 秒窗口内时恢复旧信道；重复取消为幂等操作。opcode 53 BAND_FINALIZE：transaction_id u64，双端都 CONFIRM 后结束这次协调并把设备置为 IDLE，同时保留 15 秒的显式补偿窗口。BAND_FINALIZE 后释放或到期租约不会清除此补偿窗口，新租约可在窗口内发送 BAND_CANCEL。相反，非 IDLE 阶段遇到 RELEASE 或租约到期会取消事务；若已切换则先恢复旧信道，再释放协议资源和租约。恢复失败时返回 IO 并保留事务与租约供后续重试。bandStage：0 IDLE、1 PREPARED、2 COMMITTED、3 SWITCHED、4 CONFIRMED。

`wireless-console` 分支的 Gateway 对双 C5、双方均为 HE20 STA 的协商实现五轮本地扫描、共同候选信道评分、五次当前链路探测、双端预备与提交、双端确认/完成以及失败补偿。当前 `main` 不含该 Gateway 协调器。当前链路 RTT/丢包是 Gateway 到各 C5 的 TLS 探测数据，不能称为 C5 之间的测量；扫描 RSSI 只代表 C5 到接入点。C5 AP+STA 角色协商、AP 自身候选信道干扰测量和 EMA 平滑尚未实现。Windows 热点信道由 Windows 控制，STA 不会命令它切频。无双 C5 与 Wi-Fi 6 HE20 热点的真机测试环境，主机模拟结果不能替代无线验收。

## UDP 发现与安全边界

UDP 4440 查询恰好 16 字节：`TBC5DISC`[8]、nonce[8]。回复恰好 124 字节：`TBC5HERE`[8]、原 nonce[8]、NUL 填充 identity[24]、TLS port u16、protocol u8=1、AP flag u8、DER 证书 SHA-256[32]、version[32]、board[16]。字符串含终止 NUL。每秒最多一次，只在网络就绪时回应。发现不是身份认证或健康检查。

没有启用硬件安全启动、Flash 加密和 eFuse 防降级，物理写入者可替换固件或读取开发板秘密。当前签名保护远程更新入口，TLS/令牌保护网络入口。量产需要独立密钥生命周期和不可逆烧写授权。Gateway 原型仍只监听 loopback，固件 TLS 不代表其管理 API 可以暴露到局域网。
