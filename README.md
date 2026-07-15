# ChromeTaskbarMerger

ChromeTaskbarMerger 是一个计划中的 Windows 原生工具，目标是在多个 Chrome 主窗口同时存在时，让 Windows 任务栏只保留一个固定 Chrome 入口。

## 当前状态

**Phase 0：工程骨架和可重复构建** 与 **Phase 1：Chrome 主窗口枚举与诊断** 已完成验收。Phase 2 尚未开始。

当前提供：

- CMake C++20 工程；
- `--help`、`--version` 和只读的 `--list` 命令行入口；
- `%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log` 基础日志；
- 无第三方测试框架的命令行与窗口识别规则测试；
- Debug 控制台程序和 Release Windows 子系统程序。

当前版本只读取顶层窗口和对应进程的诊断信息，不会修改 Chrome 窗口，也不会调用任务栏修改 API。

完整开发计划见 [CODEX_TASK_Chrome_Taskbar_Merger_CPP.md](CODEX_TASK_Chrome_Taskbar_Merger_CPP.md)。

## 构建要求

- Windows 11 x64；
- CMake 3.24 或更高版本；
- Visual Studio 2022 或更高版本，包含“使用 C++ 的桌面开发”组件。

## 构建

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
cmake --build build --config Release
```

## 自动测试

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

## 命令行

```powershell
.\build\Debug\ChromeTaskbarMerger.exe --help
.\build\Debug\ChromeTaskbarMerger.exe --version
.\build\Debug\ChromeTaskbarMerger.exe --list
.\build\Debug\ChromeTaskbarMerger.exe
```

未知参数会返回退出码 `2`：

```powershell
.\build\Debug\ChromeTaskbarMerger.exe --unknown
$LASTEXITCODE
```

## 日志

默认日志文件：

```text
%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log
```

日志目录无法创建时，程序会输出警告，但不会崩溃。

## Phase 0 验收记录

验收日期：2026-07-15。

验证环境：

- CMake 4.3.0；
- Visual Studio 18 2026 Community；
- MSVC 19.51.36246；
- Windows SDK 10.0.26100.0；
- x64 目标平台。

| 验收项 | 结果 |
| --- | --- |
| CMake x64 配置 | PASS |
| Debug 构建 | PASS，无编译警告 |
| Release 构建 | PASS，无编译警告 |
| Debug CTest | PASS，1/1 |
| Release CTest | PASS，1/1 |
| `--help` | PASS，退出码 0 |
| `--version` | PASS，退出码 0 |
| 未知参数 | PASS，退出码 2 |
| 无参数启动与日志写入 | PASS |
| Debug PE | PASS，x64 / Windows CUI |
| Release PE | PASS，x64 / Windows GUI |
| Phase 0 任务栏 API 安全扫描 | PASS，未发现任务栏修改 API |

Phase 0 不涉及任务栏视觉变化，因此不需要用户手动验收。

## Phase 1：Chrome 窗口诊断

`--list` 会枚举系统顶层窗口，使用进程完整路径确认 `chrome.exe`，并集中评估：

- 是否为顶层、可见窗口；
- 标题和窗口类是否有效；
- 类名是否兼容 `Chrome_WidgetWin_*`；
- 是否带有 `WS_CHILD`、`WS_EX_TOOLWINDOW` 或 `WS_EX_NOACTIVATE`；
- HWND、PID、TID、Owner、普通样式、扩展样式和 DWM cloaked 状态。

输出会同时列出 `MANAGEABLE` 和 `EXCLUDED` Chrome 候选，并为被排除窗口打印具体原因。窗口标题不参与 `chrome.exe` 身份判断。

2026-07-15 自动诊断快照：

```text
Scanned top-level windows: 480
Process-query failures: 0
Chrome candidates: 13
Manageable: 2; Excluded: 11
```

自动检查结果：Debug/Release 构建无警告，Debug/Release CTest 均为 1/1 通过，Debug 与 Release 的 `--list` 均返回退出码 `0`，UTF-8 日志包含完整窗口诊断信息。

真实窗口人工对照结果：用户打开了 3 个 Chrome 主窗口，`--list` 恰好输出 3 个 `MANAGEABLE`；其余 12 个 Chrome 候选均为不可见的工具、状态托盘、电源消息、Crashpad 或 IME 辅助窗口，没有漏报或误报。Phase 1 验收通过。

原始人工测试输出可能包含窗口标题、HWND 和 PID，仅保留在本地 `result.txt`，不纳入 Git 或未来 remote。
