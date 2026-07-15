# ChromeTaskbarMerger

ChromeTaskbarMerger 是一个计划中的 Windows 原生工具，目标是在多个 Chrome 主窗口同时存在时，让 Windows 任务栏只保留一个固定 Chrome 入口。

## 当前状态

**Phase 0～Phase 3** 均已完成验收。Phase 4 的生命周期监控实现和自动验收已经完成，正在等待真实任务栏人工验收；Phase 5 尚未开始。

当前提供：

- CMake C++20 工程；
- `--help`、`--version`、只读的 `--list`、交互式 `--experiment`，以及固定入口 `--manage` 命令行入口；
- `%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log` 基础日志；
- 无第三方测试框架的命令行、窗口识别、任务栏恢复规则和扫描调度测试；
- Debug 控制台程序和 Release Windows 子系统程序。

`--list` 和无参数启动保持只读。`--experiment` 只有在用户输入精确的 `APPLY` 后才临时修改一个窗口并立即恢复；`--manage` 只有在用户输入精确的 `MANAGE` 后才启动固定入口生命周期监控，并在暂停或正常退出时恢复本会话跟踪的修改。

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
.\build\Debug\ChromeTaskbarMerger.exe --manage
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

`--experiment` 保留为可重复诊断入口。请使用 Debug 控制台版本，至少保留两个、建议三个 Chrome 主窗口，并选择一个没有未保存工作、非关键的窗口。程序输入 `APPLY` 前不会修改任何窗口；输入后不要关闭 PowerShell 或强制结束进程，按提示完成观察，程序会自动恢复。

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

## Phase 4：固定主入口生命周期监控

请使用 Debug 控制台版本：

```powershell
.\build\Debug\ChromeTaskbarMerger.exe --manage
```

程序先只读列出可管理窗口；只有输入精确的 `MANAGE` 才会应用规则。首次同步优先保留启动时的前台 Chrome，否则保留最低 HWND 对应的稳定入口。只要该窗口身份仍有效，切换 WindowTabs、Alt+Tab、鼠标或其他前台程序都不会更换入口。开始后每 2 秒自动扫描一次 Chrome 生命周期；控制台以阻塞方式等待扫描截止时间或命令，不使用忙循环。

以下命令均为单键操作，**无需按 Enter**：

| 命令 | 行为 |
| --- | --- |
| `s` | 立即扫描；启用时同步，暂停时只读 |
| `a` | 应用/恢复固定入口管理并立即扫描 |
| `r` | 恢复本会话移除的全部按钮并暂停 |
| `h` | 显示命令帮助 |
| `q` | 恢复全部并正常退出 |

Phase 2 表明被移除的 Chrome 不会出现在 Alt+Tab 中，因此 `--manage` 要求 `WindowTabs.exe` 正在运行。每次自动或手动同步前都会检查；若 WindowTabs 不可用，将停止新增移除、恢复已跟踪按钮并暂停。重新启动 WindowTabs 后按 `a` 可恢复管理。

开始管理后 Ctrl+C 被忽略，请使用 `q` 正常退出。正常退出、`r` 和异常作用域退出只恢复本会话中 `DeleteTab` 成功的窗口，并在失败时重试一次。若出现恢复警告或退出码 `5`，不要再次启动管理，保留日志并通过任务管理器重启 Windows 资源管理器。

Debug/Release 构建无警告，Debug/Release CTest 均为 3/3 通过。自动测试使用伪任务栏控制器和可控时钟覆盖 0、1、3、5 窗口、运行中新建窗口、关闭非主/主窗口、全部关闭后重开、固定主入口、重复同步、暂停后重用、失败重试、HWND 身份变化、2 秒扫描间隔和去抖。

2026-07-15 真实任务栏验收通过：一窗口无需移除；三窗口恰好移除/恢复两个非主入口，并验证 `r` 后恢复、`a` 后重新应用；五窗口恰好移除/恢复四个非主入口。三轮重复扫描均保持固定主入口 `HWND=0x0000000000020332`，没有重复 API 调用。日志合计为 7 次成功同步、8 次成功 `DeleteTab`、8 次成功 `AddTab`、4 次成功恢复流程，错误和警告均为 0。用户确认管理期间任务栏只剩一个入口、全部 Chrome 仍可由 WindowTabs 操作，恢复和退出后所有入口重新出现。完整证据与回归步骤见 [tests/manual_test_plan.md](tests/manual_test_plan.md)。

上述真实任务栏证据属于 Phase 3。Phase 4 的代码与自动验收已通过，动态新建/关闭/重开、暂停恢复和空闲 CPU 的人工验收仍待执行。

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
