# 验收记录

日期：2026-11-26。只有实际执行结果才能标记通过；源码存在不代表硬件已验证。

| 检查 | 证据 |
|---|---|
| Gateway/控制台/模拟器 | 2026-11-26 复跑 npm run check：严格类型检查、Web 构建通过，37/37 测试通过；包括证书指纹拒绝、临时会话恢复、OTA 未知结果恢复、上传中断保留在线设备、双 C5 协商补偿和 USB/IP 回归 |
| 可移植核心 | 帧、租约、引脚、DAP、五轮扫描聚合与首样本年龄、双频状态机、候选扫描 RTT 哨兵、切换确认门槛、15 秒确认后补偿回滚和幂等取消测试通过 |
| Arm 解析器 | 固定真实源码编译运行通过；Transfer/TransferBlock 最大批量、TransferConfigure 默认重试值、SWD 序列超时检查及 2066 条 fuzz 输入的响应边界检查通过 |
| Python 客户端 | 客户端 3 项通过：帧边界、凭据发送前匹配证书、丢失回复不重放；pyOCD 适配器 4 项通过：TCP 分帧、单并发、错误响应断连、注册和 ID 过滤（pyOCD API 使用替身） |
| OpenOCD 互操作 | ESP-IDF 配套 v0.12.0-esp32-20260831 初始化真实 Arm 解析器，共 18 条命令，退出码 0；Windows 空 socket 轮询仍打印 64 次 WSA 10035。GPIO 为桩，不是 C5 SWD 验证 |
| OTA 状态回归 | 真实 probe_ota.c 加 SDK/密码算法替身测试通过：先持久化再擦写、重启检查点、重复块、存储/写入失败、确认；不代表密码算法和真实掉电验证 |
| IDF 构建 | 本机 ESP-IDF v6.1.1 当前 wireless-console 工作树构建通过；最新镜像 0x11e4b0 字节，1.875 MiB OTA 槽剩余 0xc1b50 字节（约 40%）。COM8 已用此前的 0x11e450 镜像烧录并完成校验 |
| 本轮复核 | ESP-IDF v6.1.1 构建退出码 0，最新镜像 0x11e4b0；`python firmware/probe-c5/tools/test_core.py` 通过，包含核心、OTA SDK/密码算法替身、Arm CMSIS-DAP 与 pyOCD TCP 测试（pyOCD unittest 4 项） |
| 原 Flash | COM7，C5HF4 rev1.0，4 MiB；完整备份 4194304 字节，留在 .local |
| COM8 烧录前备份 | 2026-11-26 只读识别 COM8 为 ESP32-C5HF4 rev1.2、4 MiB XMC Flash；烧录前将当前 4194304 字节 Flash 单独备份到忽略目录，未复用 COM7 备份 |
| COM8 烧录与启动 | ESP-IDF v6.1.1 将 0x11e450 字节固件写入 COM8；快速差异写入校验未通过后，esptool 自动全量重写且最终哈希校验通过。IDF monitor 确认应用启动、分区加载及 TLS RPC 443 监听，无 panic 或启动回滚 |
| COM8 无线连接 | Windows 热点可关联并取得 192.168.137.20，信道 11、20 MHz、RSSI -28 dBm，但协商为 11bgn/he:0；固件按 Wi-Fi 6 要求以 reason=8 断开并退避。因此发现、TLS 客户端认证、DAP、OTA 和双设备协商仍未完成真机端到端验收 |
| COM7 最新烧录与启动 | 2026-11-26 将最新 0x11e4b0 镜像写入 COM7（ESP32-C5HF4 rev1.0、4 MiB），esptool 哈希校验通过；15 秒串口记录确认 ESP-IDF 6.1.1、分区加载、TLS RPC 443 和 UDP 4440 服务启动，无 panic。目标引脚仍未分配 |
| 热点和 HE20 | 未通过：2026-11-26 Windows 热点 2.4 GHz 测试为 phymode=3、he:0、ht:1；临时切换 5 GHz 后为 phymode=3、he:0、vht:1，信道 40、20 MHz、RSSI -50 dBm。两种配置均被固件按 Wi-Fi 6 要求拒绝并退避；热点频段已恢复 2.4 GHz 且保持开启。已确认本机 RZ608 支持 5/6 GHz 11ax，仍需能提供 HE20 的独立接入点 |
| AP 端 RSSI | IDF 编译通过，核心聚合规则测试通过；AP 关联真机读数尚未验证 |
| AP 带宽初始化回归 | 2026-11-27 使用 ESP-IDF 6.1.1 将 AUTO 双频下不支持的单频带宽 API 改为双频 `esp_wifi_set_bandwidths()`；COM7 AP 镜像写入哈希校验通过，串口确认 softAP、DHCP `192.168.4.1`、TLS RPC 443 和 UDP 4440 正常启动，15 秒内无 abort、重启或带宽错误。STA 镜像已恢复并在 COM7 启动；AP 客户端接入和 AP/STA 协同仍未验证 |
| C5 SoftAP 到 C5 STA | 2026-11-27 COM7 运行 AP 镜像、COM8 运行 STA 镜像；COM8 串口确认 `11ax`、`he:1`、`channel 6`、`BW20`、`PHY=HE20`，并取得 `192.168.4.2`。COM7 提供 `192.168.4.1`。证明两种单一角色镜像可在本机组成 HE20 链路；未验证 Gateway 双设备协商、TLS/DAP/OTA 或目标 SWD |
| 发现/TLS/租约/DAP Info | Windows 热点未提供 HE20；C5 SoftAP 与 STA 的关联已验证，但其上的发现、TLS、租约和 DAP 尚未验证 |
| OTA 故障 | 签名、摘要、断电、确认和回滚仍待真机检查 |
| SWD/总线 | 无目标接线与测量，实际速度和电气行为未验证 |
| 双 C5 切频 | Gateway 双 STA 协调与模拟回归已实现；COM7/COM8 已证明纯 SoftAP 到 STA 的 HE20 关联，但没有验证 Gateway 双设备协商、确认或回滚。C5 的 `--ap` 是独立 SoftAP 模式，不提供同机 APSTA 上行；Gateway 当前只接受两个 STA 设备参与协商 |
| pyOCD | pyOCD 0.46.1 已安装到忽略的 C5 调试环境；真实包插件列举和 CMSIS-DAP open 对本地有界 responder 通过，另有 4 项 TCP 传输回归；真实 C5 探针、GDB server 和目标 MCU 仍未验证 |
| IDF 模拟器 | 本机 SDK QEMU 目标没有 C5，没有替代硬件结果 |
| 虚拟 USB | USB/IP 描述符、导入和传输模型有主机测试；Windows VHCI 驱动安装和实际枚举未验证 |

构建日志、串口原始日志、本机凭据在忽略提交的 `.local/`。协议与可复现工具留在源码目录。不得提交私钥、密码或原始 Flash。

本机无线适配器为 RZ608 Wi-Fi 6E 80MHz，但客户端支持 11ax 不代表 Windows 热点以 11ax 提供服务。没有静默降级到 11n，也没有把 TLS 服务启动日志当作无线端到端通过。当前仅保留本地 main 基线，完整验收前不推送远端。
