# ChromeTaskbarMerger

ChromeTaskbarMerger 是一个计划中的 Windows 原生工具，目标是在多个 Chrome 主窗口同时存在时，让 Windows 任务栏只保留一个固定 Chrome 入口。

## 当前状态

**Phase 0：工程骨架和可重复构建**、**Phase 1：Chrome 主窗口枚举与诊断** 与 **Phase 2：任务栏 API 最小可行性实验** 均已完成验收。Phase 2 确认 `ITaskbarList::DeleteTab` / `AddTab` 可行；Phase 3 尚未开始。

当前提供：

- CMake C++20 工程；
- `--help`、`--version`、只读的 `--list`，以及交互式 `--experiment` 命令行入口；
- `%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log` 基础日志；
- 无第三方测试框架的命令行、窗口识别和任务栏恢复规则测试；
- Debug 控制台程序和 Release Windows 子系统程序。

`--list` 和无参数启动保持只读。只有 `--experiment` 在用户选择一个窗口、选择方法并输入精确的 `APPLY` 后，才会临时修改该窗口的任务栏注册或扩展样式；随后程序会立即执行恢复。

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
ctest --test-dir build -C Release --output-on-failure
```

## 命令行

```powershell
.\build\Debug\ChromeTaskbarMerger.exe --help
.\build\Debug\ChromeTaskbarMerger.exe --version
.\build\Debug\ChromeTaskbarMerger.exe --list
.\build\Debug\ChromeTaskbarMerger.exe --experiment
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

## Phase 2：交互式任务栏实验

Phase 2 分别实验以下两种 Windows 路径：

- 方法 A：`ITaskbarList::DeleteTab` / `AddTab`；
- 方法 B：清除 `WS_EX_APPWINDOW`、设置 `WS_EX_TOOLWINDOW`，并使用 `SWP_FRAMECHANGED` 刷新；恢复时写回完整原始扩展样式。

`--experiment` 保留为可重复诊断入口。请使用 Debug 控制台版本，至少保留两个、建议三个 Chrome 主窗口，并选择一个没有未保存工作的重要窗口。程序输入 `APPLY` 前不会修改任何窗口；输入后不要关闭 PowerShell 或强制结束进程，按提示完成观察，程序会自动恢复。

完整的两轮操作、日志保存方式、实际结果和异常恢复步骤见 [tests/manual_test_plan.md](tests/manual_test_plan.md)。任务栏视觉结论来自用户实际观察，而不是仅凭 API 返回值推断。

实验退出码：

| 退出码 | 含义 |
| --- | --- |
| 0 | API 与必需人工观察均通过，或在 `APPLY` 前安全取消 |
| 4 | 前置条件或移除 API 失败 |
| 5 | 自动恢复未能确认完成，需要人工恢复 |
| 7 | 用户报告按钮未消失、窗口不可用或按钮未恢复 |
| 8 | 必需视觉问题回答为未知，结论不完整 |

Phase 2 于 2026-07-15 在 Windows 11 Pro 10.0.26200、Chrome 150.0.7871.115、WindowTabs `ss_2026.07.14` 环境完成真实窗口验收：

| 方法 | 按钮移除/恢复 | Chrome 与 WindowTabs | Alt+Tab | 结论 |
| --- | --- | --- | --- | --- |
| A：`DeleteTab` / `AddTab` | 均成功，无需重启 Explorer | 窗口仍可用，WindowTabs 可切换 | 隐藏期间缺项 | **采用** |
| B：扩展样式 | 均成功，原样式精确恢复 | 隐藏期间无法正常到达目标窗口 | 缺项 | **淘汰** |

后续实现固定采用方法 A。Alt+Tab 缺项是已知兼容性代价，但不阻塞当前 V1 目标；方法 B 只保留为实验诊断路径，不作为自动回退。自动验收使用 CMake 4.3.0、Visual Studio 18 2026 Community、MSVC 19.51.36246 和 Windows SDK 10.0.26100.0；Debug/Release 构建无警告，Debug/Release CTest 均为 1/1 通过。

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
