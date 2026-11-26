# Probe C5 编程规范

本规范约束 `firmware/probe-c5` 自有源码。`third_party/` 保留上游排版和命名，除非上游补丁明确要求修改。组件按 ESP-IDF 组件构建；`main/` 只装配应用入口，组件之间通过公开头文件和 CMake 依赖通信。

## C 与 C++

- 固件组件使用 C11；`main/` 的入口可使用 C++17，但只承担启动和装配，不在其中实现协议或总线逻辑。
- 缩进使用 4 个空格，文件使用 LF，行宽以 120 列为目标。格式由本目录 `.clang-format` 约束，修改后从 C5 工程目录运行 `clang-format -i`，仅处理 `components/`、`main/` 和 `tests/` 中自有的 C/C++ 文件。
- 对外函数和类型使用 `tb_` 前缀；头文件声明与实现保持一致。可被 C++ 引用的 C 接口使用 `extern "C"`。组件私有状态和仅本文件使用的函数声明为 `static`，状态名应能说明用途。
- 组件对外头文件放在 `components/<component>/include/`，私有实现留在组件目录。每个 `idf_component_register()` 明确列出公开和私有依赖；禁止依赖其他固件工程的私有源码。
- 输入在长度、范围和状态变更入口处校验。缓冲区、队列、任务栈和超时必须有明确上限；资源获取后在所有失败路径释放。不得把错误伪装成成功或空结果。
- 快速 GPIO 和 SWD 时序留在 C5 本地执行。慢速 RPC、日志和存储不能进入位时序关键路径；共享外设按租约取得和释放。

## Python 工具与测试

- `tools/` 脚本使用 4 空格缩进、标准库优先、明确超时和失败退出码；不打印密钥、令牌或私有配置。
- 新的状态机、解析器、持久化和安全行为应有边界测试。主机替身只验证它覆盖的逻辑，不能替代 C5 实机、掉电或无线链路验收。
- 组件变更先运行 `tools/test_core.py`、`python -m unittest discover -s tests -p 'test_*.py' -v` 和 `tools/build.ps1`。在 C5 工程目录中，用以下 PowerShell 命令格式化或检查自有 C/C++ 文件；不把格式化应用到 `third_party/`：

  ```powershell
  $files = Get-ChildItem components, main, tests -Recurse -File -Include *.c, *.h, *.cpp | ForEach-Object FullName
  clang-format -i --style=file $files
  clang-format --dry-run --Werror --style=file $files
  ```

安全、资源所有权和错误处理优先于缩短代码；不为尚未支持的硬件行为引入虚构引脚、成功状态或模拟器保证。
