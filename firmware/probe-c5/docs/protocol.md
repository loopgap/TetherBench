# Probe C5 协议版本 1

日期：2026-11-26。已有源码实现；真实验证范围见 [验收记录](validation.md)。所有多字节整数均为小端，字节偏移从 0 开始。协议字段有定义不等于所有硬件适配已经验证。

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

INFO 字段：id、board、version、idf、protocol、maxPayload、dapPacket、leaseActive、allowedPins、freeHeap、minHeap、wifi（ip/ap/channel/rssi/phy）、otaAvailable、pendingBoot、bandStage、capabilities。STA 模式的 wifi.rssi 是关联 AP 的信号强度；AP 模式返回已关联客户端中的最低 RSSI，未关联或读取失败时为 -127。它表示当前链路，不预测候选信道。wifi.phy 是 STA 协商的 IDF 枚举值，HE20=5，失败为 -1；AP 端能力不能从该字段推断。

## 引脚与数据事务

12 字节配置：protocol u8、count u8、reserved u16=0、hz u32、pins[4] i8。未用脚为 -1。count=0、hz=0、四脚全 -1 表示释放该协议。同协议重配先释放原驱动；新驱动失败时保持未配置。不同协议禁止共用任何 GPIO。CONFIG 按 SWD/UART/SPI/I2C 返回四条记录。

| protocol | 名称 | 脚位顺序 | count | 请求上限 |
|---:|---|---|---|---|
| 1 | SWD | SWCLK、SWDIO、可选开漏 nRESET | 2 或 3 | 1 kHz..4 MHz |
| 2 | UART1 | TX、RX、可选 RTS、CTS | 2 或 4 | 5 Mbaud，8N1 |
| 3 | SPI2 | SCLK、MOSI、MISO、CS | 4 | 20 MHz，mode 0 |
| 4 | I2C0 | SCL、SDA | 2 | 1 MHz，外部上拉 |

上限不是实测速度。候选板配置允许 GPIO 0..6、10、15、23，同时强制排除 Flash 16..22、UART0 11/12、USB 13/14、保守保留的 26..28。N4 不足以识别开发板原理图，候选脚是否接外设仍需确认。默认无映射；连接前核对电平、上拉、占用与接地。没有目标供电控制。

opcode 16 DAP：原生 CMSIS-DAP 请求/响应，最大 1024 字节。支持 Info、HostStatus、Connect/Disconnect、TransferConfigure、Transfer、TransferBlock、WriteABORT、Delay、ResetTarget、SWJ_Pins/Clock/Sequence、SWD_Configure/Sequence、最多 16 条命令的 ExecuteCommands。Transfer 和 TransferBlock 最多 255 项，并按 1024 字节的实际请求、响应预算进一步限制；例如 block-read 最多 255 个字，block-write 最多 254 个字。Sequence 最多 64 组。TransferConfigure 的 idle_cycles 为 0..32，以限制 1 kHz 时的停顿；wait_retry 和 match_retry 接受完整 u16 范围。未映射 SWD 时仅允许 Info、HostStatus、Disconnect。没有 JTAG、SWO、CMSIS UART、QueueCommands 和厂商命令。C5 本机执行 GPIO，单请求 250 ms 预算；SWD 事务重试逐次检查期限，SWD Sequence 至少每 8 个时钟检查一次。超时后关闭 SWD 并复位核心；软件预算不代替波形测量。

opcode 17 BUS：12 字节头 protocol u8、flags u8=0、address u16、tx_length u16、rx_length u16、timeout_ms u16、reserved u16=0，然后 tx_length 字节。收发各最多 1024，至少一个非零，timeout 1..100 ms。成功响应是接收到的原始字节。

UART 可返回少于请求的接收字节；发送超时释放 UART。UART/SPI address=0。SPI 同时收发要求 tx_length=rx_length，单次 CS 事务。I2C 地址 0x08..0x77，先写后读使用重复起始。UART/SPI 检查理论传输时间；实际等待受 FreeRTOS tick 和驱动调度影响。

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

外部 MCU 的签名双槽更新契约处于草案状态；目标 bootloader 及 C5 转发适配均未实现。SWD 通过 CMSIS-DAP 调试，不伪装成外部 bootloader 更新支持。

## 外部 MCU OTA 扩展草案（当前未实现）

外部 MCU 不能使用 C5 的 176 字节自更新 manifest：目标 MCU 使用带签名回滚索引的 184 字节 manifest，并要求目标 bootloader 独立校验签名、镜像摘要、双槽、持久检查点、试启动确认和回滚。目标与 C5 的详细物理帧、UART COBS、SPI 轮询、能力字段和恢复规则见 [Target MCU OTA v1 契约](../../../contracts/target-mcu-ota.md)。

规划的 C5 RPC opcode 39..45 为 `TARGET_INFO`、`TARGET_OTA_BEGIN`、`TARGET_OTA_WRITE`、`TARGET_OTA_FINISH`、`TARGET_OTA_ACTIVATE`、`TARGET_OTA_STATUS` 和 `TARGET_OTA_ABORT`。当前固件未注册这些 opcode，不得向 `INFO.capabilities` 声明目标更新；调用方应收到 `UNSUPPORTED`。实现前必须先加入目标端 bootloader 和可复现主机模型测试，再由 C5 组件维护本机持久资源锁；硬件闭环前继续保持未实现状态。

| opcode | 请求 | 成功响应 |
|---:|---|---|
| 39 TARGET_INFO | UART=2 或 SPI=3 u8 | 180 字节 HELLO 能力、身份和 OTA 状态 |
| 40 TARGET_OTA_BEGIN | 184 字节 target manifest | 64 字节状态 |
| 41 TARGET_OTA_WRITE | `job_id` u64、`offset` u32、1..983 字节 | 64 字节状态 |
| 42 TARGET_OTA_FINISH | `job_id` u64 | 64 字节状态 |
| 43 TARGET_OTA_ACTIVATE | `job_id` u64 | 64 字节状态 |
| 44 TARGET_OTA_STATUS | 空 | 64 字节状态 |
| 45 TARGET_OTA_ABORT | `job_id` u64 | 64 字节状态 |

TARGET_INFO 要求唯一 UART/SPI 总线已配置。HELLO 返回的运行状态使 C5 能在重启后发现尚未完成的目标任务；C5 必须先持久化同一总线的恢复锁，再开放任何目标总线操作。BEGIN 核对 board ID、镜像容量和全部必需安全能力，然后把签名 manifest 与总线协议持久化到独立更新锁；写锁成功后才向目标发 BEGIN。掉电或目标响应超时均保留锁；恢复时重新配置同一总线并查询 STATUS。更新锁期间 PINS 只允许重新配置相同总线，DAP、原始 BUS、切频、C5 自身 OTA 和其他协议配置均返回 BUSY。C5 只在目标明确报告 CONFIRMED、ABORTED 或 ROLLED_BACK 后清锁。

## Wi-Fi 和协商切频

C5 使用双频 AUTO，协议能力含 802.11ax，但只有一条射频链路，同一时刻只工作于一个频段和信道。默认 STA 从本机配置连接热点，得到 IP 后验证 HE20，否则断开并按 1..30 秒退避。`provision.py --ap` 配置独立 SoftAP，不启用 STA 回程；其 SSID 和随机强密码保存在忽略提交的 `.local/ap-network.json`，可用同一个文件把另一台 C5 配成 STA。AP 启动信道为 6，可按准备好的事务切到允许信道；关联客户端收到信道切换公告。AP 会拒绝不支持 802.11ax 的客户端；关联列表检查不等于逐帧 PHY 证明。

INFO 的 `wifi.ap` 标明独立 AP 角色。STA 的 `wifi.ip` 是上行地址，`wifi.rssi` 和 `wifi.phy` 表示关联 AP 的链路；AP 的 `wifi.ip` 是本机 AP 地址，`wifi.rssi` 是关联客户端中最弱的读数，`wifi.phy=-1`，因为设备自身没有 STA 上行链路。AP 模式要求至少一个关联且都支持 11ax 的客户端，才允许 BAND_CONFIRM。Gateway 当前自动协商只接受 STA 角色；AP/STA 配对协商尚未完成端到端验收。

opcode 48 WIFI_SCAN：空请求，首次启动异步扫描并连续进行五轮有界主动扫描，期间返回 BUSY；完成后返回 count u32 加最多 16 条 16 字节：BSSID[6]、channel u8、五次观测的中位 RSSI i8、五次均支持时为 1 的 phy_11ax u8、authmode u8、实际观测次数 u8、reserved u8=0、最早样本年龄 age_ms u32。STA 只扫描配置的目标 SSID；独立 AP 扫描可见网络以评估候选信道。单轮超过 5 秒或驱动错误丢弃整组结果。读取后释放，下次调用重新扫描。Gateway 可用 sightings/5 估算扫描漏检率；STA 共同候选按信道匹配，两个频段可以使用不同 BSSID。年龄和样本新鲜度用于拒绝超过 30 秒的扫描结果，扫描不能代替当前链路 RTT/丢包测量。

opcode 49 BAND_PREPARE：transaction_id u64、candidate_channel u8、current_channel u8、reserved u16=0，再接四条 link，共 60 字节。link 顺序为当前本端、当前对端、候选本端、候选对端；每条 rssi i16、loss_permille u16、rtt_ms u16、samples u16、age_ms u32。至少 5 样本、年龄不超过 30 秒；RSSI -110..-1 dBm、丢包 0..1000 千分比。当前链路 RTT 为 0..5000 ms；候选信道来自相邻扫描结果，rtt_ms=0xffff 表示无主动 RTT 样本，评分只使用 RSSI 和扫描未命中率，不得用该值标记当前链路。

采用两端最差分数：RSSI 在 -90..-40 dBm 映射到 0..100，减去 loss_permille/10 和 rtt_ms/5，整数截断。候选至少改善 10 分；成功后驻留 30 秒。允许信道 1..11、36、40、44、48，仍受地区规则和驱动约束。

opcode 50 BAND_COMMIT：transaction_id u64、delay_ms u32（500..3000）。PREPARE 5 秒过期；COMMIT 到期切换；切换后 15 秒内无 CONFIRM 则回退。opcode 51 BAND_CONFIRM：transaction_id u64；设备要求网络已就绪、实际信道匹配、RSSI 至少 -80 dBm，且 STA 协商到 HE20；AP 模式要求至少一个已关联的 11ax 客户端且所有关联客户端均支持 11ax。任一条件不满足就拒绝确认，等待自动回退；成功后进入 CONFIRMED，暂留 15 秒补偿窗口。opcode 52 BAND_CANCEL：transaction_id u64，可取消 PREPARED/COMMITTED 阶段；已切换或已确认且仍在 15 秒窗口内时恢复旧信道；重复取消为幂等操作。opcode 53 BAND_FINALIZE：transaction_id u64，双端都 CONFIRM 后结束这次协调并把设备置为 IDLE，同时保留 15 秒的显式补偿窗口。bandStage：0 IDLE、1 PREPARED、2 COMMITTED、3 SWITCHED、4 CONFIRMED。

Gateway 已实现双 C5、双方均为 HE20 STA 时的协调：五轮本地扫描、共同候选信道评分、五次当前链路探测、双端预备与提交、双端确认/完成以及失败补偿。当前链路 RTT/丢包是 Gateway 到各 C5 的 TLS 探测数据，不能称为 C5 之间的测量；扫描 RSSI 只代表 C5 到接入点。C5 AP+STA 角色协商、AP 自身候选信道干扰测量和 EMA 平滑尚未实现。Windows 热点信道由 Windows 控制，STA 不会命令它切频。无双 C5 与 Wi-Fi 6 HE20 热点的真机测试环境，主机模拟结果不能替代无线验收。

## UDP 发现与安全边界

UDP 4440 查询恰好 16 字节：`TBC5DISC`[8]、nonce[8]。回复恰好 124 字节：`TBC5HERE`[8]、原 nonce[8]、NUL 填充 identity[24]、TLS port u16、protocol u8=1、AP flag u8、DER 证书 SHA-256[32]、version[32]、board[16]。字符串含终止 NUL。每秒最多一次，只在网络就绪时回应。发现不是身份认证或健康检查。

没有启用硬件安全启动、Flash 加密和 eFuse 防降级，物理写入者可替换固件或读取开发板秘密。当前签名保护远程更新入口，TLS/令牌保护网络入口。量产需要独立密钥生命周期和不可逆烧写授权。Gateway 原型仍只监听 loopback，固件 TLS 不代表其管理 API 可以暴露到局域网。
