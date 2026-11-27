# 验收记录

日期：2026-11-27。只有实际执行结果才能标记通过；源码存在不代表硬件已验证。

| 检查 | 证据 |
|---|---|
| Gateway/控制台/模拟器 | 2026-11-26 复跑 npm run check：严格类型检查、Web 构建通过，37/37 测试通过；包括证书指纹拒绝、临时会话恢复、OTA 未知结果恢复、上传中断保留在线设备、双 C5 协商补偿和 USB/IP 回归 |
| 可移植核心 | 帧、租约、引脚、DAP、五轮扫描聚合与首样本年龄、双频状态机、候选扫描 RTT 哨兵、切换确认门槛、15 秒确认后补偿回滚和幂等取消测试通过 |
| Arm 解析器 | 固定真实源码编译运行通过；Transfer/TransferBlock 最大批量、TransferConfigure 默认重试值、SWD 序列超时检查及 2066 条 fuzz 输入的响应边界检查通过 |
| Python 客户端 | 客户端 3 项通过：帧边界、凭据发送前匹配证书、丢失回复不重放；pyOCD 适配器 4 项通过：TCP 分帧、单并发、错误响应断连、注册和 ID 过滤（pyOCD API 使用替身） |
| OpenOCD 互操作 | ESP-IDF 配套 v0.12.0-esp32-20260831 初始化真实 Arm 解析器，共 18 条命令，退出码 0；Windows 空 socket 轮询仍打印 64 次 WSA 10035。GPIO 为桩，不是 C5 SWD 验证 |
| OTA 状态回归 | 真实 probe_ota.c 加 SDK/密码算法替身测试通过：先持久化再擦写、重启检查点、重复块、存储/写入失败、确认；不代表密码算法和真实掉电验证 |
| IDF 构建 | 本机 ESP-IDF v6.1.1 工作树构建通过；最新镜像 0x11e4b0 字节，1.875 MiB OTA 槽剩余 0xc1b50 字节（约 40%）。COM8 已用此前的 0x11e450 镜像烧录并完成校验 |
| 2026-11-27 main 恢复契约回归 | `main` cherry-pick `fix(firmware): preserve target status and band recovery` 后，`tools/test_core.py` 通过、Python 单测 11/11、根目录 `npm run check` 8/8、C5 自有 35 个 C/C++ 文件 clang-format 检查通过，ESP-IDF v6.1.1 构建通过；镜像 0x120a70 字节，最小 OTA 槽余 0xbf590 字节。IDF 列出 `esp32c5` 构建目标，但当前 PATH 未发现 QEMU 系统模拟器；不能据此声称 C5 仿真已通过。随后已将当前 `main` 镜像写入 COM7 并通过哈希校验；启动日志确认 ESP-IDF 6.1.1、4 MiB Flash、TLS RPC 443 和 UDP 4440。Windows 热点完成关联并分配 IP，但 PHY 为 802.11n/he:0，固件按 Wi-Fi 6 契约断开并退避；这不是无线 RPC 验收。无外接 MCU，目标 OTA/SWD 仍未实测 |
| 2026-11-27 PINS 请求边界复验 | RPC 5 先区分帧格式错误与设备当前状态：长度非 12 或 reserved 非零返回 `INVALID`；合法配置在 OTA/切频期间仍返回 `STATE`。新增 C 核心解码回归覆盖截短、超长、reserved、空指针和字段小端解析。`tools/test_core.py` 通过、Python 单测 11/11、根目录 `npm run check` 8/8、自有 C/C++ 格式检查和 `git diff --check` 通过。ESP-IDF v6.1.1 构建通过，原镜像为 0x120aa0 字节，最小 OTA 槽余 0xbf560 字节 |
| 2026-11-27 INFO HE20 契约与 COM7 复验 | 本机 ESP-IDF v6.1.1 头文件确认 `WIFI_PHY_MODE_HE20=6`、值 5 为 HT40；旧的 `wifi.phy===5` 判断会误拒绝 HE20 STA。C5 INFO 新增稳定的 `wifi.he20`（STA 为实际协商布尔值，AP 为 null），HIL 改查此字段，`wifi.phy` 留作 IDF 诊断。核心与传输回归、Python 11/11、`npm run check` 8/8、自有 C/C++ 格式检查及 ESP-IDF v6.1.1 构建通过；镜像 0x120ae0 字节，最小 OTA 槽余 0xbf520 字节。镜像已写入 COM7（C5HF4 rev1.0、4 MiB），esptool 哈希校验通过；串口确认 ESP-IDF 6.1.1、TLS RPC 443 和 UDP 4440 启动，无新 panic。Windows 热点仍报告 `he:0`，设备按契约断开并退避；未能通过该链路运行 HIL。`wireless-console` 分支现有 `=== 5` 消费者仍待迁移到 `he20 === true` |
| 本轮复核 | ESP-IDF v6.1.1 构建退出码 0，最新镜像 0x11e4b0；`python firmware/probe-c5/tools/test_core.py` 通过，包含核心、OTA SDK/密码算法替身、Arm CMSIS-DAP 与 pyOCD TCP 测试（pyOCD unittest 4 项） |
| 2026-11-27 main OTA journal 修复复验 | `14b9d55` 后 `tools/test_core.py` 通过（核心、OTA SDK/密码替身、Arm CMSIS-DAP 与 pyOCD TCP 的 4 项 unittest）；ESP-IDF v6.1.1 构建通过，镜像 0x11e900，OTA 槽剩余 0xc1700（约 40%）。本次只构建，未烧录；不构成掉电或真机 OTA 证据 |
| 2026-11-27 C5 组件分层和格式规范 | 按 ESP-IDF 组件拆分的源码、公开头文件和依赖通过 ESP-IDF v6.1.1 构建；主机 `tools/test_core.py` 通过，包含核心、OTA SDK/密码替身、Arm DAP 边界与 2066 条 fuzz 检查，以及 4 项 pyOCD 传输单测；Python 工具单测 6/6 通过；clang-format 22.1.7 对 C5 自有 30 个 C/C++ 文件执行格式检查通过。镜像 0x11e910 字节，OTA 槽剩余 0xc16f0 字节（约 40%）。这些检查不代表外设电气或掉电行为 |
| 原 Flash | COM7，C5HF4 rev1.0，4 MiB；完整备份 4194304 字节，留在 .local |
| COM8 烧录前备份 | 2026-11-26 只读识别 COM8 为 ESP32-C5HF4 rev1.2、4 MiB XMC Flash；烧录前将当前 4194304 字节 Flash 单独备份到忽略目录，未复用 COM7 备份 |
| COM8 烧录与启动 | ESP-IDF v6.1.1 将 0x11e450 字节固件写入 COM8；快速差异写入校验未通过后，esptool 自动全量重写且最终哈希校验通过。IDF monitor 确认应用启动、分区加载及 TLS RPC 443 监听，无 panic 或启动回滚 |
| COM8 无线连接 | Windows 热点可关联并取得 192.168.137.20，信道 11、20 MHz、RSSI -28 dBm，但协商为 11bgn/he:0；固件按 Wi-Fi 6 要求以 reason=8 断开并退避。因此发现、TLS 客户端认证、DAP、OTA 和双设备协商仍未完成真机端到端验收 |
| COM7 最新烧录与启动 | 2026-11-26 将最新 0x11e4b0 镜像写入 COM7（ESP32-C5HF4 rev1.0、4 MiB），esptool 哈希校验通过；15 秒串口记录确认 ESP-IDF 6.1.1、分区加载、TLS RPC 443 和 UDP 4440 服务启动，无 panic。目标引脚仍未分配 |
| 2026-11-27 组件版烧录与启动 | ROM 识别 COM7 为 ESP32-C5HF4 rev1.0、4 MiB XMC；对应 4 MiB 原始 Flash 备份在 `.local/original-flash.bin`。组件版 0x11e910 镜像烧录且哈希校验通过；复位日志确认应用、TLS RPC 443 和 UDP 4440 服务启动，15 秒内无新 panic 或重启。Wi-Fi 随后以 reason=201 断开，未完成 HE20 或端到端无线控制验证 |
| 2026-11-27 目标 MCU 查询期限与编程规范复验 | C5 主机回归通过，新增 UART/SPI 全事务 100 ms 截止时间、发送阶段耗尽期限、非成功响应携带负载、分片 UART、SPI BUSY 轮询等检查；ESP-IDF v6.1.1 构建通过，镜像 0x11f8e0 字节、OTA 应用槽余量约 40%；IDF Python 环境单测 6/6、clang-format 检查通过、`npm run check` 8/8。未刷写本次镜像；未连接目标 MCU，因此不构成目标 OTA 或目标总线实机验证 |
| 2026-11-27 动态状态缓存与规范回归 | `TB_TARGET_OTA_STATUS` 从成功响应缓存中排除，新增源码策略回归通过；ESP-IDF v6.1.1 Python 环境 `unittest discover` 11/11、pyOCD 传输 4/4、核心回归和 clang-format 检查通过，`npm run check` 8/8。系统 Python 3.14 未安装 `cryptography`，未将该环境结果作为固件工具验收依据 |
| 2026-11-27 wireless-console 合入复验 | 组件化 C5 工程在 ESP-IDF v6.1.1 下重新构建通过，镜像 0x120950 字节，最小 OTA 槽剩余 0xbf6b0 字节（40%）；`tools/test_core.py`、Python 单测 11/11、clang-format 检查和根目录 `npm run check`（40/40）通过。本次未烧录，未连接目标 MCU，不构成无线、目标 OTA、掉电或 SWD 电气验收 |
| 2026-11-27 COM8 组件版烧录与启动 | ESP32-C5HF4 rev1.2、4 MiB XMC；0x120950 镜像写入 COM8，快速差异校验失败后自动全量重写，最终哈希校验通过。20 秒串口记录确认 ESP-IDF 6.1.1、分区加载、TLS RPC 443 和 UDP 4440 服务启动，无 panic 或重启；Wi-Fi 仍因接入点不满足 HE20 以 reason=201 退避，目标引脚未分配。此记录不构成无线、目标 MCU OTA、掉电或 SWD 电气验收 |
| 2026-11-27 目标状态/租约契约固件 | `tools/test_core.py`、Python 单测 11/11、`npm run check` 48/48 和 ESP-IDF v6.1.1 构建通过；0x120a70 镜像烧录 COM8 并通过哈希校验。IDF monitor 确认 TLS RPC 443 和 UDP 4440 启动；Windows 2.4 GHz 热点协商为 11bgn、`he:0`，C5 按 Wi-Fi 6 要求以 reason=8 断开，发现为空，HIL TLS 连接超时。目标引脚未分配，未连接目标 MCU；无线端到端及目标 OTA 仍未验收 |
| 2026-11-27 Gateway target OTA 路由回归 | TypeScript C5 client 增加 39..45 编解码；Gateway 完成 target info/upload/status/activate/abort 路由，模拟 UART 分片上传、能力拒绝、状态检查和协议选择测试通过；目标 MCU bootloader、签名验签、回滚和 UART/SPI 真机仍未验证 |
| 2026-11-27 无线总线与 Gateway 复验 | 根目录 `npm run check`：严格 TypeScript 检查、Web 构建、48/48 测试通过；Gateway 验证 UART/SPI 原始字节事务映射及协议参数拒绝；`tools/test_core.py` 通过核心/目标链路/OTA/DAP 主机检查。持续 UART/SPI 串流与目标 MCU 真机未实现或验收，不作为本次通过范围 |
| COM7 已存 core dump | 组件版启动时发现 Flash 中有 5376 字节旧 core dump；读取到 `.local/com7-coredump-20261127.bin`。其记录的应用 SHA-256 前缀为 `50f8ff3e0`，与当前 ELF 的 `dbb060624` 不同，不能用当前符号文件可靠解析；当前启动日志没有新 panic。原始转储保留在忽略目录，未擦除 |
| 热点和 HE20 | 未通过：2026-11-26 Windows 热点 2.4 GHz 测试为 phymode=3、he:0、ht:1；临时切换 5 GHz 后为 phymode=3、he:0、vht:1，信道 40、20 MHz、RSSI -50 dBm。两种配置均被固件按 Wi-Fi 6 要求拒绝并退避；热点频段已恢复 2.4 GHz 且保持开启。已确认本机 RZ608 支持 5/6 GHz 11ax，仍需能提供 HE20 的独立接入点 |
| AP 端 RSSI | IDF 编译通过，核心聚合规则测试通过；AP 关联真机读数尚未验证 |
| AP 带宽初始化回归 | 2026-11-27 使用 ESP-IDF 6.1.1 将 AUTO 双频下不支持的单频带宽 API 改为双频 `esp_wifi_set_bandwidths()`；COM7 AP 镜像写入哈希校验通过，串口确认 softAP、DHCP `192.168.4.1`、TLS RPC 443 和 UDP 4440 正常启动，15 秒内无 abort、重启或带宽错误。后续 COM8 的 C5 STA 在 COM7 SoftAP 上以 WPA3/11ax、HE20 关联并取得 `192.168.4.2`；这证明 AP/STA 无线链路，不证明 Gateway RPC、发现或双端切频通过。STA 镜像已恢复并在 COM7 启动 |
| 发现/TLS/租约/DAP Info | 因热点不满足 HE20，发现返回空，无线真机验证尚未通过 |
| OTA 故障 | 签名、摘要、断电、确认和回滚仍待真机检查 |
| SWD/总线 | 无目标接线与测量，实际速度和电气行为未验证 |
| 双 C5 切频 | COM7 SoftAP 与 COM8 STA 的 HE20 关联已通过；`main` 的 C5 端无线链路具备真机证据。Gateway 双 STA 协调仍只存在 `wireless-console` 分支；当前分支的 AP/STA 双设备事务、发现及协商回滚尚未执行，且 Windows 热点 STA 路径因无 HE20 被拒绝 |
| pyOCD | pyOCD 0.46.1 安装在忽略的 `.local/debug-venv`。真实 `DAPAccessCMSISDAP` 经本机 TCP 有界 responder 完成 open、SWD connect 与 DP_IDCODE 读取，得到 `0x2BA01477`，并断言使用了 `DAP_TransferBlock` 请求；测试 1/1、TCP 传输测试 4/4。ESP-IDF Python 环境不含 pyOCD；这不代表完整 Cortex-M attach、GDB server、USB、C5 探针或目标 MCU 已验证 |
| IDF 模拟器 | 本机 SDK QEMU 目标没有 C5，没有替代硬件结果 |
| 虚拟 USB | USB/IP 描述符、导入和传输模型有主机测试；Windows VHCI 驱动安装和实际枚举未验证 |
| 2026-11-27 协议边界与目标 OTA 断点语义 | 通用帧解码器/编码器新增空指针与非法状态码拒绝回归；目标 OTA 测试覆盖 `received` 超过持久化 `checkpoint` 后按 checkpoint 重传。主机 C/C++ 回归、Python 11/11、`npm run check` 8/8、clang-format 和 IDF v6.1.1 构建通过；镜像 0x120b00 字节，最小 OTA 槽余 0xbf500 字节。镜像已烧录 COM7，esptool 哈希校验通过；复位后 20 秒日志确认 IDF 6.1.1、TLS RPC 443、UDP 4440 启动且无 panic，STA 因网络协商 `reason=201` 退避。无目标 MCU，断点语义仅由 C5 主机替身测试覆盖。 |
| 2026-11-27 目标 OTA 完成态与回滚计数器不变式 | C5 拒绝非完整 `VERIFIED/TRIAL/CONFIRMED/ROLLED_BACK` 状态，并要求 `CONFIRMED` 的回滚计数器等于签名 manifest、其他阶段严格小于该值；异常状态查询保持持久更新锁。核心/目标链路回归、Python 11/11、`npm run check` 8/8、格式与差异检查通过；ESP-IDF v6.1.1 构建镜像 0x120b40 字节，OTA 槽余 0xbf4c0 字节。已烧录 COM7 且哈希校验通过；15 秒串口捕获确认应用、TLS RPC 443 与 UDP 4440 启动，无 panic。热点广播探测为空，STA 报 `reason=201`；没有目标 MCU，不能据此验收目标 bootloader。 |
| 2026-11-27 OTA phase/result 契约与 Gateway 会话撤销竞态 | C5 解码器拒绝全部不匹配的状态阶段/启动结果组合，核心回归穷举 8×4 组合；状态矩阵已同步到跨项目契约和 C5 协议文档。Gateway 在接收交换请求体期间若会话被撤销，返回 404；分块请求竞态回归通过。`tools/test_core.py` 通过、IDF Python 单测 11/11、`npm run check` 9/9、clang-format 与 `git diff --check` 通过。ESP-IDF v6.1.1 构建通过，镜像 0x120b70 字节，最小 OTA 槽剩余 0xbf490 字节。本次只构建，未烧录；无目标 MCU，未验证目标 bootloader。Gateway 仍是本机模拟 API。 |
| 2026-11-27 SWD 目标模型与安全 I/O 重配 | Host CMSIS-DAP 回归新增有界 SWD 目标模型，覆盖 DP IDCODE、AP 内存单次/块读写、WAIT 重试/耗尽、FAULT、奇偶校验和期限取消；`tools/test_core.py`、IDF Python 单测 12/12、`npm run check` 9/9、相关 clang-format 与 `git diff --check` 通过。设备端在目标 OTA 锁（包括状态存储故障）下拒绝全部 PINS，主机策略回归通过；协议规定先读 INFO/CONFIG 预览冲突，等待在途有界 DAP/BUS RPC 完成后再二次确认，分步配置后逐步读回。ESP-IDF v6.1.1 构建镜像 0x120b20 字节，OTA 槽余 0xbf4e0 字节。COM7 确认为 ESP32-C5HF4 revision 1.0、4 MiB；烧录镜像 SHA 校验通过，串口日志确认 IDF v6.1.1、TLS RPC 443、UDP 4440 服务启动且未映射目标 GPIO。SWD 模型不代表目标芯片或 GPIO 电气验收；未配置 SWD 引脚、未运行实体 SWD，总线自动错线试探未启用。 |

| 2026-11-27 IO 组件边界与 HIL 安全前置 | `probe_io.h` 不再导出可变引脚表或 GPIO 寄存器操作；跨组件读取经校验后复制配置，SWD 快速内联接口仅供 IO/DAP 内部使用。HIL 在获取租约前验证 INFO、48 字节 CONFIG 和 28 字节 C5 OTA 状态，活动租约、已映射引脚、未完成事务或异常状态都会中止。主机核心测试通过、Python 17/17、`npm run check` 9/9、clang-format 与 `git diff --check` 通过。ESP-IDF 6.1.1 镜像 0x120cd0 字节，最小 OTA 槽余 0xbf330 字节（40%）。COM7 烧录哈希校验通过；20 秒串口确认 IDF 6.1.1、4 MiB Flash、应用、TLS RPC 443 与 UDP 4440 启动，无新 panic。STA 报 `reason=201` 且发现为空，本次未运行 HIL；真实 SWD、GPIO 电气与目标总线仍未验收。 |
| 2026-11-27 Wi-Fi 扫描资源回收 | 扫描失败事件与 `esp_wifi_scan_get_ap_records()` 失败后显式清除驱动 AP 列表；超时只在 `esp_wifi_scan_stop()` 成功后清理，停止失败则保留扫描状态等待完成事件。新增主机 API 替身回归，覆盖读取失败、失败事件、超时停止成功/失败、启动失败和五轮正常扫描。`python firmware/probe-c5/tools/test_core.py` 通过（核心、目标链路、OTA 替身、DAP 边界及 pyOCD 传输测试）；`cmake --build build` 用现有 ESP-IDF v6.1.1 配置构建通过，镜像 0x120f80 字节，OTA 槽余 0xbf080 字节（40%）。PowerShell 的 IDF export 因工具注册数据未识别已安装工具而失败，因此本次复用既有 IDF CMake/Ninja 配置直接构建；未烧录或运行无线 HIL。 |

构建日志、串口原始日志、本机凭据在忽略提交的 `.local/`。协议与可复现工具留在源码目录。不得提交私钥、密码或原始 Flash。

本机无线适配器为 RZ608 Wi-Fi 6E 80MHz，但客户端支持 11ax 不代表 Windows 热点以 11ax 提供服务。没有静默降级到 11n，也没有把 TLS 服务启动日志当作无线端到端通过。当前仅保留本地 main 基线，完整验收前不推送远端。
