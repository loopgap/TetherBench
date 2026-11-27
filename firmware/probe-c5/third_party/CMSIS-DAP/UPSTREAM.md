# Arm CMSIS-DAP 来源与适配

引入日期：2026-11-26。仓库 `https://github.com/ARM-software/CMSIS-DAP`，固定提交 `12636590eec66fae2d1bba4518749426ad5a4595`。用户已批准引入官方核心。保留原版权头和 Apache-2.0 `LICENSE`。

实际编译 `Source/DAP.c`、`Source/SW_DP.c`。`DAP_vendor.c` 和 `Config/DAP_config.h` 仅为上游参考。没有复制整个 SDK。

本地补丁：

- `Include/DAP.h`：Arm 专用延时改成 C5 RISC-V 周期计数，并提供主机测试分支。
- `Source/SW_DP.c`：SWD 事务与 SWD 序列期限检查；长序列每个字节之间检查本机期限。
- `Source/DAP.c`：删除 SWJ_Clock 的未用局部变量。

C5 的 `components/probe_dap/private/DAP_config.h`、`cmsis_compiler.h` 和 `probe_dap.c` GPIO/传输适配属于本工程。网络输入先经 `probe_core` 检查长度、命令数、延时和响应预算，再交给上游核心。TransferConfigure 的 wait/match 字段保留 CMSIS-DAP 的完整 u16 范围，SWD 执行期限在每次事务中检查。`tests/dap_test.c` 直接编译固定上游源码，以 GPIO 桩检查解析、响应边界和超时中断；不能证明真实 SWD 波形。升级核心时需检查这些差异并重跑对应测试。
