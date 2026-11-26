# Probe C5

共享 ESP32-C5 N4 固件使用 ESP-IDF 6.1.1 组件构建，版本读取根目录 `VERSION`。已实现 TLS 二进制 RPC、UDP 发现、独占租约、可配置 SWD/UART/SPI/I2C、官方 Arm CMSIS-DAP 核心、签名双槽 OTA，以及外部 MCU 的 UART/SPI OTA 转发与恢复锁。Gateway 已实现双 STA 共同信道测量与协调；C5 AP+STA 协商、目标 MCU bootloader 闭环和真实 USB 枚举尚未完成。

跨工程入口：[C5 契约](../../contracts/probe-c5.md)。详细字段和恢复行为：[协议](docs/protocol.md)。编程及格式规范：[C5 编程规范](docs/coding-standards.md)。验证结果：[验收记录](docs/validation.md)。第三方版本及补丁：[CMSIS-DAP 来源](third_party/CMSIS-DAP/UPSTREAM.md)。

`main/` 仅保留 ESP-IDF 应用入口；`components/` 按核心协议、Wi-Fi、总线、CMSIS-DAP、C5 OTA 和 RPC 装配分层，每个组件自行声明依赖；`third_party/` 保存固定上游源码；`tools/`、`tests/` 和 `docs/` 分别保存工程工具、回归和契约记录。`.local/` 的凭据、私钥、Flash 备份和本机配置，以及 `build/` 的编译输出均不提交。N4 只表示 4 MiB Flash，没有确认具体开发板外部电路；默认不分配目标引脚。

STA 模式先读取并启动 Windows 已配置的热点，再生成本机凭据和构建。AP 模式直接运行 `provision.py --ap`，不要求热点配置。WinRT 热点脚本需要 Windows PowerShell 5；路径是 2026-11-26 本机确认值，其他机器通过脚本参数指定环境：

```powershell
powershell.exe -NoProfile -File firmware/probe-c5/tools/windows_hotspot.ps1 -Start
& 'C:/Espressif/tools/python/v6.1.1/venv/Scripts/python.exe' firmware/probe-c5/tools/provision.py
& firmware/probe-c5/tools/build.ps1
```

`provision.py --ap` 配置此 C5 为独立 Wi-Fi 6 AP；默认 STA。STA 从 `.local/wifi.json` 读取热点凭据。AP 模式无需上行热点，首次配置时生成独立网络凭据，保存在忽略提交的 `.local/ap-network.json` 并在重配时复用；连接设备时从该本机文件读取，工具不会打印密码。C5 单射频一次只工作在一个信道，AP 可按协商结果经 CSA 通知客户端切频，不是同时运行 2.4 GHz 和 5 GHz。STA 必须实际协商 HE20，否则断开并退避重连。热点已经启动不代表它支持 Wi-Fi 6。客户端在发送令牌前核对预置的完整 DER 证书，发现报文不能建立信任。每台板卡应使用独立密钥和配置。

首次烧录前确认芯片身份并备份完整 Flash。构建脚本的 `-Flash -Port COM7` 会烧录；本机已备份 4194304 字节至 `.local/original-flash.bin`。分区表保留原 NVS 的 `0x9000/0x6000` 边界。

```powershell
$env:CC='D:/mingw64/bin/gcc.exe'
& 'C:/Espressif/tools/python/v6.1.1/venv/Scripts/python.exe' firmware/probe-c5/tools/test_core.py
& 'C:/Espressif/tools/python/v6.1.1/venv/Scripts/python.exe' -m unittest discover -s firmware/probe-c5/tests -p 'test_*.py' -v
npm run check
& 'C:/Espressif/tools/python/v6.1.1/venv/Scripts/python.exe' firmware/probe-c5/tools/client.py discover
& 'C:/Espressif/tools/python/v6.1.1/venv/Scripts/python.exe' firmware/probe-c5/tools/hil.py --host <设备地址>
```

真机检查工具不分配目标脚。SWD 实际波形、总线电气质量、OTA 掉电和双 C5 切频仍需对应实验。其他芯片的 QEMU 结果不能证明 C5 硬件行为。当前没有启用不可逆 eFuse 配置；应用层签名更新不替代量产安全启动、防物理读出和防降级。
