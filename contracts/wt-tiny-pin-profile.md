# Wireless-Tag Tiny 板级引脚配置

状态：**暂定板级配置，已由官方资料交叉核对，尚未完成本机示波器/逻辑分析仪和 P4↔C5 实机事务验证**。

这份配置只描述启明云端（Wireless-Tag）TINY 开发板的物理引脚和建议信号角色。它不把 COM 号、芯片 GPIO 数量或模拟器结果当作接线依据。跨工程实现必须先选择本文件中的 profile，再由设备自身执行资源和冲突校验。

## 板卡身份与资料

- C5：`WT9932C5-TINY`，模块丝印为 N4，板载 CH343P，RGB 使用 GPIO6，右排针提供 GPIO24、10、9、8、7；左排针提供 GPIO0、1、2、4、5、6、13、14。官方板级资料：<https://wiki.wireless-tag.com/docs/zh/WT9932C5-TINY/board_features.html>。
- P4：`WT9932P4-TINY` 或同排针布局的 `WT9932P4X-TINY`。官方板级资料：<https://wiki.wireless-tag.com/docs/zh/WT9932P4-TINY/board_features.html>。P4 与 P4X 的芯片版本和固件不可混用，烧录前仍需核对板上型号丝印。
- 本机当前观察到 COM7 为 CH343 USB-UART、COM8 为 C5 原生 USB-Serial/JTAG，能确认是两颗 C5，不能单独证明排针版本。接线时按板上 `WT9932C5-TINY` 丝印和排针标签核对。

## C5 TINY 排针索引

排针序号从板顶端向 USB-C 端排列。序号是物理排针位置，不是 GPIO 编号。

| 侧 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|
| 左 | GPIO0 | GPIO1 | GPIO2 | GPIO4 | GPIO5 | GPIO6 | GPIO13 | GPIO14 | 3V3 |
| 右 | GPIO25 | GPIO26 | GPIO28 | GPIO27 | GPIO24 | GPIO10 | GPIO9 | GPIO8 | GPIO7 |

左侧继续为：10=`3V3`、11=`EN`、12=`GND`、13=`GND`、14=`GND`、15=`5V`。

右侧继续为：10=`3V3`、11=`TXD`、12=`RXD`、13=`RTS`、14=`DTR`、15=`GND`。

GPIO6 接板载 RGB，GPIO25/26/27/28 和 GPIO7 属于启动相关或板级保留候选；首版互连 profile 不使用这些脚。GPIO13/14 是 C5 原生 USB D-/D+，保留给 COM8 调试路径。C5 的 CH343 UART 不重用右侧 `TXD/RXD/RTS/DTR`。

## P4↔C5 高速互连 profile

profile 名称：`wt-tiny-p4-c5-spi-v1`。P4 是 SPI master，C5 是 SPI slave；C5 端需要先完成 SPI slave 适配，当前 C5 的目标 MCU SPI BUS 驱动是 master，不能把现有 BUS 实现误报成已支持该互连。

P4 使用左排针前 6 个 GPIO，C5 使用上表标出的实际排针位置：

| 信号 | P4 GPIO / 左排针 | C5 GPIO / 排针 | 方向 | 空闲/复位要求 |
|---|---:|---:|---|---|
| `SPI_SCLK` | GPIO16 / 左1 | GPIO0 / 左1 | P4 → C5 | C5 复位期间 P4 保持低速或高阻 |
| `SPI_MOSI` | GPIO17 / 左2 | GPIO1 / 左2 | P4 → C5 | C5 复位期间 P4 高阻 |
| `SPI_MISO` | GPIO18 / 左3 | GPIO8 / 右8 | C5 → P4 | C5 未选中时高阻 |
| `SPI_CS_N` | GPIO19 / 左4 | GPIO9 / 右7 | P4 → C5 | 复位和上电阶段保持高电平 |
| `C5_IRQ` | GPIO20 / 左5 | GPIO24 / 右5 | C5 → P4 | 推挽输出，未就绪时保持低电平 |
| `C5_RESET_N` | GPIO21 / 左6 | `EN` / 左11 | P4 → C5 | P4 只能开漏拉低，释放依靠 C5 板上上拉 |
| `GND` | 左9 | 左12/13/14 或右15 | — | 必须共地 |

两块板分别用 USB 供电，不把两个 3.3 V 稳压输出直接并联。逻辑电平均应为 3.3 V；如需要电平参考，使用受控的 VTREF 测试点而不是反向灌电。初次接线使用 8 MHz SPI，确认片选、IRQ、复位和错误率后再提升到 20 MHz 以内；短线并在 P4 的 SCLK/MOSI/CS 源端预留 22–47 Ω 串联电阻位置。

P4 左排针 GPIO16–21 在官方板级表中是普通 0V/3.3V GPIO。P4 的 GPIO37/38（UART0/strap）、GPIO2–5（JTAG）、GPIO7/8（CSI/触摸 I2C）、GPIO26/27（DSI）、GPIO35/51（按键/RGB）和 GPIO39–44（SDMMC/LDO_VO4）不用于本 profile。

## C5→STM32 SWD profile

profile 名称：`wt-c5-swd-target-v1`。它用于纯 C5 路线，和上面的 P4 互连 profile 在同一块 C5 上互斥；换 profile 前释放租约、停止有界事务并重新读取 `INFO`/`CONFIG`。

| 信号 | C5 GPIO / 排针 | 目标端 |
|---|---:|---|
| `SWCLK` | GPIO8 / 右8 | SWCLK/TCK |
| `SWDIO` | GPIO9 / 右7 | SWDIO/TMS |
| `nRESET` | GPIO10 / 右6 | NRST（可选开漏） |
| `VTREF` | C5 3V3 / 左9 或左10 | 仅电平参考 |
| `GND` | C5 GND / 左12 或右15 | GND |

C5 的 3V3 不是目标 MCU 供电承诺；目标板必须有自己的受控电源。SWD 频率先从 1 MHz 开始，确认波形和复位极性后再提高。不要把 P4 的 SPI 线同时接到这组三线，避免两个物理驱动源争用。

## 备用 UART profile

只有在 SPI profile 未占用资源时才使用：P4 左7=`GPIO22` → C5 右6=`GPIO10`（TX→RX），C5 右5=`GPIO24` → P4 左8=`GPIO23`（TX→RX），另接 GND。RTS/CTS 不在首版接线中。该 profile 需要 P4 固件实现 UART 传输后才能验收，不能替代 SPI 主链路证据。

## 接线前检查

1. 先断电，按排针丝印和本表的 GPIO 编号逐根核对；不要用“左第几脚”跨板盲接。
2. 两板单独供电后先测信号空闲电平和 GND 连通性；任何 GPIO 不得接 5 V。
3. 上电复位时 P4 的 GPIO16–21 必须为高阻或安全电平，尤其是 `SPI_CS_N` 保持高、`C5_RESET_N` 只能开漏拉低。
4. 未完成 P4 变体确认、C5↔P4 SPI slave 适配和实机帧错误率测试前，控制台只能显示“暂定 profile”，不能显示“高速 DAP 已验证”。
