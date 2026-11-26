# 本机开发环境

检查日期：2026-11-26。本文件记录当前工作区机器的观察结果，不自动更新全局环境。

## 已发现

| 工具 | 本机版本/状态 | 用途 |
|---|---|---|
| Git | 2.49.0.windows.1 | 仓库与 worktree |
| GitHub CLI | 2.105.0，认证失败（401） | 仅已验证公开只读 Git 操作 |
| Node.js / npm | 24.15.0 / 11.15.0 | 本机 Gateway 原型与测试 |
| Rust / Cargo | 1.97.1 | 候选；未安装依赖或构建生产 Gateway |
| Python | 3.14.3 | 已用于 pyOCD/自动化兼容检查 |
| CMake / Ninja | 4.3.2 / 1.13.2 | 可供原生固件构建使用 |
| Go | 未发现 | Rust/Go 比较前再补充 |
| ESP-IDF / `idf.py` | v6.1.1 已安装；新 PowerShell 默认未激活 | 通过 `firmware/probe-c5/tools/build.ps1` 激活并构建 |
| OpenOCD / ARM GNU 工具链 | 未发现 | 真实烧录/调试未验证 |

CPU/RAM 查询在当前受限会话失败，且未发现可枚举的串口设备；不填推测值。官方当前 Node 24 LTS 最新补丁为 24.22.3；本机 24.15.0 能运行原型测试，但版本落后。建议下一次本机工具更新时只更新 Node 24 LTS，不需要同时安装 Node 26。

## 增补（2026-11-26）

ESP-IDF v6.1.1 安装在 `D:\esp-idf\v6.1.1\esp-idf`。新 shell 默认没有激活环境，直接调用 `idf.py` 不可用。运行 `firmware/probe-c5/tools/build.ps1` 会激活对应工具链；2026-11-26 复跑构建通过，最新镜像 0x11e4b0 字节。详见 C5 [验收记录](../../firmware/probe-c5/docs/validation.md)。

## 当前可执行

当前 Gateway、控制台和模拟器检查使用现有 Node.js/npm；2026-11-26 `npm run check` 通过。C5 构建使用本机已安装的 ESP-IDF v6.1.1；不需要再安装该工具链。

## 后续按里程碑补充

1. Gateway 语言实验前：使用官方渠道准备 Go 1.27.4，Rust 更新至当时稳定版；不全局切换默认环境，记录下载来源和校验值。
2. 新主机首次构建 Probe C5 前：按 Espressif 官方安装说明单独安装 ESP-IDF v6.1.1 及其 C5 工具链；本机使用 `firmware/probe-c5/tools/build.ps1` 激活已安装环境，不安装其他芯片目标。
3. 硬件调试里程碑前：根据选定后端补充 OpenOCD/pyOCD/probe-rs 和对应 STM32 工具，不提前同时安装所有候选。
4. 只有 P4 USB 验证启动后，才安装其 USB/ESP-IDF 所需工具和驱动。

SDK 与完整编译器应安装在仓库外。仓库只记录工程配置、必要版本约束和构建命令；不提交用户 SDK 路径、设备配置、SDK 缓存或工具链归档。
