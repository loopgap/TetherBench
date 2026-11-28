# Wireless-Tag Tiny 引脚资料（2026-11-28）

本记录保存本轮选定板级 profile 的上游证据和本机观察。资料是接线依据，不等于本机实物已经通过连续性、波形或协议验收。

用户已确认本项目使用启明云端（Wireless-Tag）TINY 板：两块 C5 为 N4/`WT9932C5-TINY`，P4 按 `WT9932P4-TINY` 工作 profile 规划。若 P4 实物丝印为 `WT9932P4X-TINY`，不得使用本记录的 P4 固件或把 revision 3.x 当作 P4 变体证据。

## C5

- 官方板级页面：[WT9932C5-TINY board features](https://wiki.wireless-tag.com/docs/zh/WT9932C5-TINY/board_features.html)
- 官方 WT_BSP：[`WT9932C5-TINY/board.c`](https://github.com/Wireless-TAG-Maker/WT_BSP/blob/main/components/wt_bsp/boards/WT9932C5-TINY/board.c)
- 页面给出的左排针为 GPIO0、1、2、4、5、6、13、14、3V3、3V3、EN、GND、GND、GND、5V；右排针为 GPIO25、26、28、27、24、10、9、8、7、3V3、TXD、RXD、RTS、DTR、GND。
- WT_BSP 将 GPIO6 定义为板载 RGB，将 GPIO28 定义为按键；本轮 profile 避开这些板载资源。GPIO13/14 保留给 C5 原生 USB，右侧 TXD/RXD/RTS/DTR 保留给 CH343 UART。

## P4

- 官方板级页面：[WT9932P4-TINY board features](https://wiki.wireless-tag.com/docs/zh/WT9932P4-TINY/board_features.html)
- 官方 WT_BSP：[`WT9932P4-TINY/board.c`](https://github.com/Wireless-TAG-Maker/WT_BSP/blob/main/components/wt_bsp/boards/WT9932P4-TINY/board.c)
- 左排针前六个脚是 GPIO16、17、18、19、20、21，均标为 0V/3.3V 普通 GPIO；左排针第 9 脚是 GND。
- 板级资料同时记录 UART0 GPIO37/38、CSI/触摸 I2C GPIO7/8、DSI GPIO26/27、RGB GPIO51、按键 GPIO35，以及 SDMMC GPIO39–44/LDO_VO4；这些资源不进入高速互连 profile。
- 官方页面说明 WT9932P4-TINY 与 WT9932P4X-TINY 的排针资源相近，但芯片版本和固件不可混用。接线前仍需核对板上型号丝印。

## 本机观察

- COM7：USB VID:PID `1A86:55D3`，Windows 显示 CH343；C5 芯片实测为 ESP32-C5HF4、4 MiB、rev1.0。
- COM8：ESP32-C5 原生 USB-Serial/JTAG；芯片实测为 ESP32-C5HF4、4 MiB、rev1.2。
- COM10：P4 高速路径固定帧 PING 已通过，DAP 请求在 C5 链路未接入时按契约返回 `LINK_OFFLINE`。

这些观察确认了芯片和当前端口角色，但没有确认每根杜邦线的连续性、P4 具体是 P4 还是 P4X、SPI 从机时序或 SWD 波形；这些仍是开放验收项。
