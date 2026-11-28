# ADR 0005：Wireless-Tag Tiny 互连引脚

日期：2026-11-28。状态：暂定，等待 P4 变体确认和实机电气/帧验证。

## 背景

本机的 COM7/COM8 是两颗 ESP32-C5，用户确认板卡为启明云端（Wireless-Tag）TINY；COM7 枚举为 CH343 USB-UART，COM8 枚举为 C5 原生 USB-Serial/JTAG。P4 为 WT9932P4-TINY 系列，COM10 当前可完成固定帧 PING，但 P4↔C5 物理链路尚未接入。芯片 GPIO 号不能直接替代开发板排针位置，P4-TINY 和 P4X-TINY 的芯片固件也不能混用。

## 决定

- 以 `contracts/wt-tiny-pin-profile.md` 作为跨工程板级接线权威。
- P4↔C5 首版使用 P4 左排针 GPIO16、17、18、19、20、21；C5 使用 GPIO0、1、8、9、24 和 `EN`。信号分别为 SPI_SCLK、SPI_MOSI、SPI_MISO、SPI_CS_N、C5_IRQ、C5_RESET_N。
- C5 `EN` 由 P4 GPIO21 以开漏方式拉低，复位释放依靠 C5 板级上拉；P4 复位阶段所有 SPI 输出保持高阻或安全空闲电平。
- 纯 C5→STM32 SWD 使用 C5 GPIO8、9、10。它和 P4 互连 profile 在同一块 C5 上互斥，切换前必须停止有界事务并重新读取 `INFO`/`CONFIG`。
- 两块开发板分别供电，只共地，不把两个 3.3 V 稳压输出直接并联。首轮 SPI 以 8 MHz 验证，确认波形和错误率后再评估是否提升。
- C5 provision 默认允许掩码收敛为 GPIO0、1、8、9、10、24；其他 GPIO 需要新的板级 profile、资源说明和回归测试才能启用。

## 依据与限制

引脚表来自 Wireless-Tag 的 C5/P4 板级页面和 WT_BSP 源码；本地固件构建、COM 号和 USB 枚举不能证明实际焊盘连续性。当前 C5 目标 MCU SPI 驱动是 master，P4↔C5 方案要求后续增加受边界保护的 C5 SPI slave 适配，现阶段不得把 P4 DAP 物理互操作标为完成。具体来源和待验收项见 `docs/upstream/2026-11-28-wt-tiny-pin-profile.md`。
