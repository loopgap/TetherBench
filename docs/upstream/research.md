# 上游项目与版本研究

版本核对日期：2026-11-22。版本只代表该日查到的官方资料，不构成兼容性实测。源代码复用前仍需单独核实许可证、修改路径、NOTICE 和目标平台支持。

## 工具链快照

| 项目 | 2026-11-22 官方稳定信息 | 本机检查 | 结论 |
|---|---|---|---|
| Node.js | v26.13.1 与 v24.22.3 均为 LTS 线最新版本 | v24.15.0，测试可用 | 原型按 Node 24+ 运行；本轮不改系统安装，下一轮本机补丁更新可直接升级到 v24.22.3 |
| Rust | 1.100.0 于 2026-11-12 发布 | rustc/cargo 1.97.1 | 有 Rust 工具链，但版本落后；Gateway 候选实验前再更新，不全局安装 |
| Go | 1.27.4 于 2026-11-10 发布 | 未安装 | Go 是正式比较候选；稍后只补充一个官方工具链，不预装完整嵌入式栈 |
| ESP-IDF | v6.1.1 于 2026-11-16 发布，官方发布说明列出 C5/P4 支持 | `IDF_PATH`、`idf.py` 未发现 | 仅配置 C5 工程；本轮不宣称固件已构建 |
| GitHub CLI | gh 2.105.0 | 已安装；当前凭据返回 HTTP 401 | 远端公开只读操作可用；不尝试登录或写 GitHub |

本机另检测到 Python 3.14.3、CMake 4.3.2、Ninja 1.13.2 和 npm 11.15.0。未能从受限系统接口读取 CPU/RAM；Ports 设备查询没有列出当前设备。版本来源链接见下方。

## 复用候选

| 项目 | 复用价值 | 当前决定 |
|---|---|---|
| [CMSIS-DAP](https://github.com/ARM-software/CMSIS-DAP) | ARM 调试探针命令和固件实现 | 优先对齐协议；具体代码移植需评估许可证及 C5 时序 |
| [OpenOCD](https://github.com/openocd-org/openocd) | 多架构调试后端、适配器与目标分层；上游含 CMSIS-DAP TCP 后端 | 首要互操作候选；源码当前注明 TCP 后端不支持 UART/SWO，硬件尚未互操作 |
| [pyOCD](https://github.com/pyocd/pyOCD) | Python 调试器、探针接口和 Flash 算法生态 | 候选后端；先核对其 Remote Probe，而非预设兼容 |
| [probe-rs](https://github.com/probe-rs/probe-rs) | Rust 调试/烧录库、芯片描述和工具 | 候选后端，同时是 Rust Gateway 生态参考；网络接入需验证 |
| [ESP-IDF](https://github.com/espressif/esp-idf) | C5/P4 官方 SDK 与构建系统 | 固件原生构建体系；候选基线 v6.1.1，未在本机安装 |
| [ESP-Hosted](https://github.com/espressif/esp-hosted) | P4/C5 协处理器链路 | 只在实际芯片组合和稳定版本构建验证后采用 |
| [TinyUSB](https://github.com/hathach/tinyusb) | USB Device / CDC 等成熟实现 | 评估 P4 实际 USB PHY、控制器及 ESP-IDF 集成后再决定 |
| [MCUmgr](https://github.com/apache/mynewt-mcumgr) | 管理及固件升级协议候选 | 与 CBOR/有限帧和安全模型一起比较；当前未采用 |

## 需要的实验证据

生产 Gateway 需要 Rust/Go 等价的设备注册、单设备独占会话、断线释放 HTTP 服务原型；各运行至少三次并比较冷/热构建、发布大小、内存、空闲 CPU 和模拟请求延迟。模拟器不是实体吞吐测试。前端选型应先用同一个真实设备列表/会话页面比较依赖、增量构建、类型检查和渲染成本，之前不引入完整组件集。

## 官方版本来源

- [Go 官方发布历史](https://go.dev/doc/devel/release)
- [Rust 官方发布公告](https://blog.rust-lang.org/releases/)
- [Node.js 官方版本支持表](https://nodejs.org/en/about/previous-releases)
- [Node.js v26.13.1 版本档案](https://nodejs.org/en/download/archive/v26.13.1)
- [Node.js v24.22.3 版本档案](https://nodejs.org/en/download/archive/v24.22.3)
- [ESP-IDF ESP32-C5 v6.1.1 文档](https://docs.espressif.com/projects/esp-idf/en/v6.1.1/esp32c5/index.html)
- [OpenOCD CMSIS-DAP TCP 后端源码及报文定义](https://openocd.org/doc/doxygen/html/cmsis__dap__tcp_8c_source.html)
- [actions/checkout v7.0.1 发布](https://github.com/actions/checkout/releases/tag/v7.0.1)
- [actions/setup-node v7.1.0 发布](https://github.com/actions/setup-node/releases/tag/v7.1.0)
