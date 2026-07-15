# ChromeTaskbarMerger 手工测试计划

## 状态说明

- `PASS`：已经执行并符合预期；
- `FAIL`：已经执行但不符合预期；
- `BLOCKED`：环境不足或等待人工观察；
- `NOT RUN`：尚未进入对应 Phase。
- `AUTOMATIC PASS / MANUAL PENDING`：实现和可自动验证项目通过，仍等待真实桌面观察。

## Phase 0：工程骨架

Phase 0 不修改任务栏，原则上由 Codex 自动验收，不需要用户协助。

### 自动验收项目

| 项目 | 预期结果 | 实际结果 | 状态 |
| --- | --- | --- | --- |
| CMake 配置 | 生成 x64 Visual Studio 工程 | Visual Studio 18 2026，x64 | PASS |
| Debug 构建 | 无编译或链接错误 | 构建成功，无警告 | PASS |
| Release 构建 | 无编译或链接错误 | 构建成功，无警告 | PASS |
| Debug CTest | 所有测试通过 | 1/1 通过 | PASS |
| Release CTest | 所有测试通过 | 1/1 通过 | PASS |
| `--help` | 打印帮助，退出码为 0 | 输出正确，退出码 0 | PASS |
| `--version` | 打印版本，退出码为 0 | `0.1.0-dev`，退出码 0 | PASS |
| 未知参数 | 打印错误，退出码非 0 | 输出错误，退出码 2 | PASS |
| 无参数启动 | 创建日志并明确说明未修改任务栏 | 日志创建且安全提示正确 | PASS |
| Debug 子系统 | PE 标记为 x64 Windows CUI | machine 8664，subsystem 3 | PASS |
| Release 子系统 | PE 标记为 x64 Windows GUI | machine 8664，subsystem 2 | PASS |
| API 安全扫描 | 不包含任务栏修改 API | 未发现相关 API | PASS |

自动验收日期：2026-07-15。

工具链：CMake 4.3.0、Visual Studio 18 2026 Community、MSVC 19.51.36246、Windows SDK 10.0.26100.0。

### 可选人工复核

如果需要人工复核，Codex 会提供确切的可执行文件路径。操作步骤如下：

1. 在 PowerShell 中运行 Debug 版本的 `--help`；
2. 确认显示版本、用法和“不会修改任务栏”的提示；
3. 运行无参数 Debug 版本；
4. 确认日志路径被打印，且 Chrome 窗口和任务栏没有变化。

安全恢复方法：Phase 0 没有修改窗口或任务栏状态，因此不需要恢复操作。

## Phase 1：Chrome 主窗口枚举与诊断

### 自动验收项目

| 项目 | 预期结果 | 实际结果 | 状态 |
| --- | --- | --- | --- |
| Debug 构建 | `/W4` 下无错误或警告 | 构建成功，无警告 | PASS |
| Release 构建 | `/W4` 下无错误或警告 | 构建成功，无警告 | PASS |
| Debug CTest | 识别规则测试全部通过 | 1/1 通过 | PASS |
| Release CTest | 识别规则测试全部通过 | 1/1 通过 | PASS |
| `--list` 退出码 | 正常枚举返回 0 | Debug 与 Release 均为 0 | PASS |
| Chrome 进程识别 | 完整路径、文件名大小写测试通过 | 合成测试通过 | PASS |
| 排除规则 | 非 Chrome、不可见、空标题、工具/子/不可激活窗口被排除 | 合成测试通过 | PASS |
| 零 Chrome 摘要 | 正常报告零候选而非错误 | 合成测试通过 | PASS |
| 实际只读扫描 | 不崩溃并输出接受/排除原因 | 480 个顶层窗口；13 个 Chrome 候选 | PASS |
| 实际候选分类 | 输出可管理与排除数量 | 2 个可管理；11 个排除 | PASS |
| 进程查询 | 权限或生命周期问题不会崩溃 | 本次失败数为 0 | PASS |
| UTF-8 日志 | 保存 HWND、PID、标题、类名和样式 | 已验证 | PASS |
| API 安全扫描 | 不包含任务栏修改 API | 未发现任务栏修改 API | PASS |

自动扫描环境：Windows 11 10.0.26200、Chrome 150.0.7871.115、x64 Debug/Release。

### 必需人工对照

人工对照状态：`PASS`。

2026-07-15 实际结果：

| 对照项 | 结果 | 状态 |
| --- | --- | --- |
| 用户实际打开的 Chrome 主窗口 | 3 个 | PASS |
| `MANAGEABLE` 数量 | 3 个 | PASS |
| 可见 Chrome 主窗口漏报 | 无 | PASS |
| 辅助窗口误报为主窗口 | 无 | PASS |
| `EXCLUDED` Chrome 候选 | 12 个，均为隐藏工具或辅助窗口 | PASS |
| 进程查询失败 | 0 | PASS |

人工结果来源：用户提供的本地 `result.txt`。该文件包含窗口标题、HWND 和 PID，不提交到 Git。

操作步骤：

1. 打开希望后续管理的 Chrome 配置文件主窗口，建议至少三个；
2. 确认这些主窗口当前均能通过 WindowTabs、Alt+Tab 或鼠标访问；
3. 在项目目录运行：

   ```powershell
   .\build\Debug\ChromeTaskbarMerger.exe --list
   ```

4. 查看输出开头的 `Manageable` 数量；
5. 检查每个 `MANAGEABLE` 项的标题是否分别对应真实 Chrome 主窗口；
6. 检查是否有真实主窗口错误地出现在 `EXCLUDED` 项中；
7. 向 Codex 回报：实际主窗口数量、`Manageable` 数量、是否漏报、是否误报。

预期结果：每个计划管理的 Chrome 主窗口恰好对应一个 `MANAGEABLE` 项；辅助、IME、状态托盘和隐藏窗口均为 `EXCLUDED` 或不显示。

安全恢复方法：Phase 1 只读取窗口信息，没有修改窗口、任务栏或 Chrome 数据，因此不需要恢复。

Phase 1 结论：自动验收和真实窗口人工对照均通过，可以进入 Phase 2 的任务栏 API 最小可行性实验。

## Phase 2：任务栏 API 最小可行性实验

当前状态：`PASS`。实现、自动验收和真实任务栏人工验收均已完成；正式技术路线为方法 A（`ITaskbarList::DeleteTab` / `AddTab`）。

### 自动验收项目

| 项目 | 预期结果 | 实际结果 | 状态 |
| --- | --- | --- | --- |
| Debug 构建 | `/W4` 下无错误或警告 | 构建成功，无警告 | PASS |
| Release 构建 | `/W4` 下无错误或警告 | 构建成功，无警告 | PASS |
| Debug CTest | 所有恢复与安全测试通过 | 1/1 通过 | PASS |
| Release CTest | 所有恢复与安全测试通过 | 1/1 通过 | PASS |
| `--experiment` 参数 | 正确进入 Phase 2 交互入口 | 解析测试通过 | PASS |
| 方法 A 初始化 | COM、`CoCreateInstance` 和 `HrInit` 成功并检查 HRESULT | 本机 HRESULT 为 `0x00000000` | PASS |
| 方法 B 样式计算 | 清除 `WS_EX_APPWINDOW`、设置 `WS_EX_TOOLWINDOW`、保留无关位 | 合成测试通过 | PASS |
| 方法 B 安全往返 | 只修改所选窗口并精确恢复完整原值 | 两个不可见测试窗口验证通过 | PASS |
| 恢复幂等性 | 未修改或已恢复状态不重复调用窗口 API | 合成测试通过 | PASS |
| 无效 HWND | 安全失败且不产生恢复义务 | 合成测试通过 | PASS |
| 窗口身份保护 | PID、TID 或类名变化时拒绝陈旧 HWND | 合成测试通过 | PASS |
| 操作前取消 | 三个取消节点都不修改真实窗口 | 退出码均为 0 | PASS |
| 真实任务栏视觉结果 | A、B 均实际执行并由用户观察 | A 通过，B 因窗口不可到达而失败 | PASS |

自动验收日期：2026-07-15。自动测试没有对真实 Chrome 调用 `DeleteTab`、`AddTab` 或 `SetWindowLongPtrW`；实际修改只允许在下面的人工步骤中，由用户输入 `APPLY` 后发生。

### 真实窗口验收结果

验收日期：2026-07-15。

验收环境：Windows 11 Pro 10.0.26200、Chrome 150.0.7871.115、WindowTabs `ss_2026.07.14`。

| 验收项 | 方法 A：`DeleteTab` / `AddTab` | 方法 B：扩展样式 |
| --- | --- | --- |
| 移除 API | `S_OK` | 成功，Win32 错误码 0 |
| 修改前扩展样式 | `0x00200100` | `0x00200100` |
| 修改后扩展样式 | 未改变 | `0x00200180` |
| 任务栏按钮消失 | 是 | 是 |
| Chrome 仍打开、可到达、可操作 | 是 | 否 |
| WindowTabs 仍可切换目标窗口 | 是 | 否 |
| Alt+Tab 仍可到达目标窗口 | 否 | 否 |
| 恢复 API | `AddTab` 返回 `S_OK` | 成功，精确恢复 `0x00200100` |
| 无需重启 Explorer 即恢复按钮 | 是 | 是 |
| 视觉判定 | **PASS** | **FAIL** |

分析结论：方法 A 满足 Phase 2 的全部硬门槛。Alt+Tab 缺项已在 V1 范围修订中明确允许，只要 Chrome 窗口仍能由 WindowTabs 等正常方式到达和操作，因此不影响通过。方法 B 虽然能可靠移除、恢复按钮并精确恢复样式，但在修改期间令目标窗口无法正常到达，不满足最低可用性要求，后续不得作为自动回退。

证据来源：`phase2-method-a.txt`、`phase2-method-b.txt` 和 `%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log`。PowerShell transcript 对交互式原生程序的输出记录不完整，因此 API 与样式证据以程序自身的 UTF-8 日志为准；两份 transcript 和日志均只保留在本地，不提交到 Git。

### 必需人工验收

先完成方法 A，再对方法 B 完整重复一次。两个方法都必须执行并记录；Phase 2 的通过条件是至少一种方法能让目标按钮真实消失和恢复，同时目标 Chrome 窗口仍然打开、可到达和可操作。

准备工作：

1. 保持 3 个 Chrome 主窗口打开，确认当前任务栏有对应的多个 Chrome 按钮；
2. 保存重要网页表单或其他未保存内容；
3. 选择一个非主要窗口作为实验对象，在两轮实验中尽量选择同一个窗口；
4. 实验输入 `APPLY` 后不要关闭 PowerShell、不要结束程序，也不要关闭目标 Chrome；程序问完观察问题后会自动恢复；
5. `WindowTabs` 或 `Alt+Tab` 可以受影响，但请如实回答；它们不单独决定 V1 是否通过。

#### 方法 A

在项目根目录打开 PowerShell，运行：

```powershell
Start-Transcript -Path .\phase2-method-a.txt -Force
.\build\Debug\ChromeTaskbarMerger.exe --experiment
```

然后按顺序操作：

1. 输入目标 Chrome 对应的窗口编号；
2. 输入 `1` 选择 `ITaskbarList::DeleteTab / AddTab`；
3. 再核对程序打印的标题和 HWND，确认目标正确后输入大写 `APPLY`；
4. 观察目标任务栏按钮是否消失，对第一个问题回答 `y`、`n` 或 `u`；
5. 用 WindowTabs、鼠标或其他正常方式访问目标 Chrome，确认它仍打开、可到达且可操作，并回答第二个问题；
6. 简单测试 WindowTabs 和 Alt+Tab，分别回答后两个兼容性问题；
7. 程序随后自动恢复；确认按钮是否无需重启 Explorer 即重新出现，并回答最后一个问题；
8. 看到 `Experiment observation summary` 后运行：

   ```powershell
   $LASTEXITCODE
   Stop-Transcript
   ```

#### 方法 B

重新打开或继续使用 PowerShell，运行：

```powershell
Start-Transcript -Path .\phase2-method-b.txt -Force
.\build\Debug\ChromeTaskbarMerger.exe --experiment
```

重复方法 A 的全部观察步骤，但在方法选择时输入 `2`。输入 `APPLY` 前，程序会打印根据枚举快照计算的 `Proposed ExStyle`；实际操作时仍会重新读取并保存完整原值。结束后同样运行：

```powershell
$LASTEXITCODE
Stop-Transcript
```

`phase2-method-a.txt`、`phase2-method-b.txt` 以及以前的 `result.txt` 可能包含窗口标题、HWND 和 PID，均已由 `.gitignore` 排除，不会进入未来 remote。

### 异常时的安全恢复

- 正常情况下程序会在观察问题之后立即恢复；移除过程中发生错误，也会先尝试恢复，再退出。
- 应用修改后 Ctrl+C 会被临时忽略，以避免打断恢复；不要关闭终端窗口或强制结束进程。
- 若程序显示 `WARNING: automatic restoration did not complete` 或退出码 `5`，不要再次运行实验。保留 transcript 和默认日志，先确认 Chrome 窗口仍在，然后按 `Ctrl+Shift+Esc` 打开任务管理器，在“进程”中选择“Windows 资源管理器”并点击“重新启动”。
- Explorer 重启后若按钮仍未恢复，停止测试并把两个 transcript 与 `%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log` 的相关内容交给 Codex。
- 如果目标 Chrome 在实验中被关闭，恢复保护会识别窗口已不存在并跳过；它不会依据陈旧 HWND 修改后来创建的其他窗口。

### 用户回报模板

```text
Windows 版本：
Chrome 版本：
WindowTabs 版本（未使用则填无）：
方法 A：按钮消失 是/否/未知；按钮恢复 是/否/未知；窗口仍可用 是/否；退出码：
方法 B：按钮消失 是/否/未知；按钮恢复 是/否/未知；窗口仍可用 是/否；退出码：
Alt+Tab 影响：
WindowTabs 影响：
是否闪烁：
是否被 Chrome 或 Explorer 自动恢复：
是否出现恢复警告：
其他现象：
```

Phase 2 结论：验收通过，选择方法 A 进入后续开发。Phase 3 随后已开始，并停在下述人工验收门槛。

## Phase 3：固定主入口 MVP

当前状态：`PASS`，实现、自动验收和 1/3/5 个真实 Chrome 窗口的任务栏人工验收均已完成。

### 自动验收项目

| 项目 | 预期结果 | 实际结果 | 状态 |
| --- | --- | --- | --- |
| Debug 构建 | `/W4` 下无错误或警告 | 构建成功，无警告 | PASS |
| Release 构建 | `/W4` 下无错误或警告 | 构建成功，无警告 | PASS |
| Debug CTest | 核心与固定入口测试全部通过 | 2/2 通过 | PASS |
| Release CTest | 核心与固定入口测试全部通过 | 2/2 通过 | PASS |
| 0/1 个窗口 | 不调用任务栏移除 API | 伪后端测试通过 | PASS |
| 3/5 个窗口 | 固定一个入口，分别只移除 2/4 个 | 伪后端测试通过 | PASS |
| 主入口选择 | 前台 Chrome 优先；否则稳定回退 | 伪后端测试通过 | PASS |
| 固定主入口 | 主入口有效时不随前台变化 | 伪后端测试通过 | PASS |
| 重复同步 | 不重复调用 `DeleteTab` | 伪后端测试通过 | PASS |
| 恢复全部 | 只对成功移除项调用一次 `AddTab` | 伪后端测试通过 | PASS |
| 恢复后重新应用 | 保留固定入口并重新移除其他项 | 伪后端测试通过 | PASS |
| 失败处理 | 恢复失败保留重试，协调失败不新增移除 | 伪后端测试通过 | PASS |
| 主入口失效 | 提升剩余前台 Chrome 并恢复其按钮 | 伪后端测试通过 | PASS |
| HWND 身份变化 | 旧状态与新 PID 身份分开处理 | 伪后端与真实控制器测试通过 | PASS |
| WindowTabs 前置保护 | 同步前检查，缺失时不新增移除并恢复 | 实现审计与 Debug 构建通过 | PASS |
| `--manage` 操作前取消 | 不初始化 COM、不修改任务栏、退出码 0 | 本机安全路径通过 | PASS |
| 命令行回归 | 帮助、默认启动、`--list`、非法参数正确 | 退出码 0/0/0/2 | PASS |
| 真实任务栏单入口 | 1、3、5 个窗口均符合预期 | 三轮均成功 | PASS |

自动验收日期：2026-07-15。自动验收未输入 `MANAGE`，因此没有对真实 Chrome 批量调用 `DeleteTab`。

### 真实窗口验收结果

验收日期：2026-07-15。

验收环境：Windows 11 Pro 10.0.26200、Chrome 150.0.7871.115、WindowTabs `ss_2026.07.14`。

三轮均选择同一固定主入口：`HWND=0x0000000000020332`，PID/TID `23492/12032`，类名 `Chrome_WidgetWin_1`。

| 场景 | 首次同步 | 重复 `s` | 恢复/重新应用 | 用户视觉确认 | 状态 |
| --- | --- | --- | --- | --- | --- |
| 1 个窗口 | 0 次移除，跟踪数 0 | 0 次新调用 | `q` 无需恢复且成功 | 始终 1 个入口 | PASS |
| 3 个窗口 | 2 次 `DeleteTab` 成功 | 主入口不变，`Already removed=2`，0 次新调用 | `r`：2 次 `AddTab`；`a`：2 次 `DeleteTab`；`q`：2 次 `AddTab`，全部成功 | 管理时 1 个、全部窗口可用；`r/q` 后 3 个恢复 | PASS |
| 5 个窗口 | 4 次 `DeleteTab` 成功 | 主入口不变，`Already removed=4`，0 次新调用 | `q`：4 次 `AddTab` 成功 | 管理时 1 个、全部窗口可用；退出后 5 个恢复 | PASS |

日志汇总：7 次 `Fixed-entry synchronization: SUCCESS`、8 次 `DeleteTab: SUCCESS`、8 次 `AddTab: SUCCESS`、4 次恢复流程成功；全部 HRESULT 和 Win32 错误码均为 0，`FAIL`/警告/错误计数为 0。WindowTabs 前置检查在每次同步前均通过。

证据来源：本地 `phase3-1-window.txt`、`phase3-3-window.txt`、`phase3-5-window.txt`，以及 `%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log`。PowerShell transcript 对三、五窗口的交互式原生输出存在交叉和漏录，API 次数、主入口身份和恢复结果以程序自身 UTF-8 日志为准；用户“都成功”的回报作为任务栏最终绘制和窗口可用性的人工证据。所有原始文件仅保留在本地，不提交 Git。

### 人工验收步骤（已执行，可用于回归）

使用 Debug 版本分别完成 1、3、5 个 Chrome 主窗口的三轮测试。每轮开始前确认 WindowTabs 正在运行并保存重要内容。PowerShell transcript 对原生交互输出可能不完整，API 证据以 `%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log` 为准；transcript 主要用于保留命令顺序。

每轮使用对应文件开始记录：

```powershell
Start-Transcript -Path .\phase3-1-window.txt -Force
.\build\Debug\ChromeTaskbarMerger.exe --manage
```

三窗口和五窗口轮次分别把文件名改为 `phase3-3-windows.txt`、`phase3-5-windows.txt`。

#### 一窗口轮次

1. 只保留 1 个 Chrome 主窗口，确认任务栏有 1 个 Chrome 入口；
2. 启动 `--manage`，核对列出的窗口后输入大写 `MANAGE`；
3. 确认同步报告为 `SUCCESS`、`Tracked removals` 为 0，任务栏仍是 1 个入口；
4. 输入 `s`，确认没有新的 `DeleteTab`/`AddTab` 操作；
5. 输入 `q`，确认正常退出且任务栏仍为 1 个入口；
6. 记录退出码并结束 transcript：

   ```powershell
   $LASTEXITCODE
   Stop-Transcript
   ```

#### 三窗口轮次

1. 打开 3 个独立 Chrome 主窗口，确认任务栏最初有 3 个入口；
2. 启动 `--manage` 并输入 `MANAGE`，确认任务栏只剩 1 个入口；
3. 记录报告中的 `Fixed main entry` HWND，并确认 `Tracked removals after operation` 为 2；
4. 使用 WindowTabs 依次切换并操作全部 3 个 Chrome，确认都仍打开、可到达和可操作；
5. 切换若干次后回到控制台输入 `s`，确认主入口 HWND 不变、`Already removed` 为 2，且本次报告没有新的 `DeleteTab`/`AddTab`；
6. 输入 `r`，确认 3 个任务栏按钮都恢复且管理状态显示为 `paused`；
7. 输入 `a`，确认重新只剩 1 个入口，主入口 HWND 与之前一致；
8. 输入 `q`，确认程序退出后 3 个任务栏按钮全部恢复；
9. 记录 `$LASTEXITCODE`，再执行 `Stop-Transcript`。

#### 五窗口轮次

1. 打开 5 个独立 Chrome 主窗口，确认任务栏最初有 5 个入口；
2. 启动 `--manage` 并输入 `MANAGE`，确认只剩 1 个入口，报告中的跟踪移除数为 4；
3. 使用 WindowTabs 依次访问全部 5 个窗口，确认都可操作；
4. 切换后输入 `s`，确认固定主入口不变、`Already removed` 为 4，且没有重复任务栏 API 调用；
5. 输入 `q`，确认退出后 5 个任务栏按钮全部恢复；
6. 记录 `$LASTEXITCODE`，再执行 `Stop-Transcript`。

`phase3-*.txt` 可能包含窗口标题、HWND 和 PID，已由 `.gitignore` 排除，不会进入未来 remote。

### 异常时的安全恢复

- 输入 `MANAGE` 后不要关闭终端或强制结束进程；Ctrl+C 会被忽略，请输入 `r` 或 `q`。
- WindowTabs 意外退出后，输入 `s` 会触发前置检查、恢复全部并暂停；也可以直接输入 `r`。
- 若看到恢复警告或退出码 `5`，停止后续轮次，不要再次运行 `--manage`。保留 transcript 和默认日志，通过 `Ctrl+Shift+Esc` 打开任务管理器，重启“Windows 资源管理器”，然后告知 Codex。
- 任一同步报告为 `FAIL` 时，先输入 `r`；确认所有按钮恢复后再退出并把日志交给 Codex。

### 用户回报模板

```text
1 窗口：管理后入口数；重复扫描是否无调用；退出后入口数；退出码
3 窗口：管理后入口数；全部窗口可用 是/否；主入口是否固定；r 后入口数；a 后入口数；q 后入口数；退出码
5 窗口：管理后入口数；全部窗口可用 是/否；主入口是否固定；q 后入口数；退出码
WindowTabs 影响：
是否闪烁或被自动恢复：
是否出现 FAIL 或恢复警告：
其他现象：
```

通过标准：1、3、5 个窗口时分别保持恰好 1 个任务栏入口；所有 Chrome 均可由 WindowTabs 到达和操作；主入口不随切换变化；重复同步不重复调用 API；`r` 和 `q` 恢复本会话修改。上述标准已经满足，Phase 3 验收通过，Phase 4 允许开始。

## Phase 4：窗口生命周期自动维护

自动验收日期：2026-07-15。

- Debug 与 Release 均在 `/W4` 下无警告构建；
- Debug 与 Release CTest 均为 3/3 通过；
- 策略测试覆盖运行中新建窗口、关闭非主窗口、关闭主入口、全部关闭后用新 HWND/PID 重开、重复扫描、暂停后恢复管理和 HWND 身份复用；
- `ScanSchedule` 的可控时钟测试覆盖精确 2 秒周期、即时请求合并、手动扫描去抖和非法间隔保护；
- 主循环使用 `WaitForSingleObject` 等待控制台输入或扫描截止时间，暂停时无限期阻塞，没有后台线程或忙循环；
- 安全回归确认 `MANAGE` 前取消返回 0 且不修改任务栏，非法参数返回 2；当前没有足够实验窗口时，Phase 2 `--experiment` 在任何修改前按设计返回 4。

自动验收结论：`PASS`。以下真实任务栏视觉结果与空闲 CPU 仍需人工确认。

### 人工验收步骤

1. 确认 WindowTabs 正在运行，准备 3 个内容容易区分的 Chrome 主窗口；关闭其他不参与测试的重要 Chrome 窗口。
2. 在普通 PowerShell 控制台中运行（不要重定向输入）：

   ```powershell
   .\build\Debug\ChromeTaskbarMerger.exe --manage
   ```

3. 核对只读预览后输入大写 `MANAGE` 并按 Enter。此后 `s`、`a`、`r`、`h`、`q` 都是单键命令，**不要再按 Enter**。等待 3 秒，确认任务栏收敛为 1 个 Chrome 入口。
4. 新建 2 个 Chrome 主窗口。每次新建后等待 3 秒，确认任务栏仍只有 1 个 Chrome 入口；WindowTabs 中的窗口仍可打开和操作。
5. 按一次 `s`，在窗口清单中把固定主入口 HWND 与标题对应起来。先通过 WindowTabs 关闭一个非主窗口，等待 3 秒并确认仍只有 1 个入口；再关闭固定主入口对应的窗口，等待 3 秒并确认剩余 Chrome 自动选出新主入口，仍只有 1 个入口。
6. 关闭所有 Chrome 窗口并等待 3 秒；随后重新打开 3 个内容可区分的 Chrome 主窗口。最后一个窗口打开后等待 3 秒，确认规则重新生效并只保留 1 个入口。
7. 按单键 `r`。确认程序显示 `paused`，3 个 Chrome 任务栏按钮恢复。保持暂停超过 3 秒，确认按钮不会又被自动移除；可在暂停时再开一个窗口，确认它也保持可见。
8. 按单键 `a`，等待 3 秒，确认管理恢复且任务栏再次只保留 1 个 Chrome 入口。
9. 在窗口集合不变化时让程序空闲至少 15 秒，在任务管理器“详细信息”页观察 `ChromeTaskbarMerger.exe`：应大部分时间为 0% 或仅有短促采样波动，不应持续占用一个 CPU 核心。
10. 按单键 `q`，确认本会话移除的 Chrome 按钮全部恢复且程序正常退出；在 PowerShell 中执行 `$LASTEXITCODE`，预期为 `0`。
11. 将程序日志复制到仓库，供 Codex 分析：

    ```powershell
    Copy-Item "$env:LOCALAPPDATA\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log" .\phase4-validation.txt
    ```

如任何一步出现 `FAIL`、退出码 `5` 或退出后仍有按钮缺失，不要立即重跑；保留日志并通过任务管理器重启 Windows 资源管理器，然后告知 Codex。

### 请回报的观察结果

```text
初始 3 窗口在 3 秒内收敛为 1 个入口：
新建第 4/5 个窗口后仍各为 1 个入口：
关闭非主窗口后入口数：
关闭主入口后入口数：
全部关闭后再开 3 个窗口的最终入口数：
r 后恢复的入口数；保持暂停是否稳定：
a 后入口数：
空闲至少 15 秒时 CPU 观察：
q 后恢复的入口数；退出码：
是否出现 FAIL、恢复警告或其他异常：
```

通过标准：所有动态场景均在最多一个 2 秒扫描周期（人工观察允许等待 3 秒）后收敛到零个或一个任务栏入口；暂停和退出能恢复本会话修改；没有持续 CPU 异常占用、崩溃或恢复遗留。

### 人工验收结果（2026-07-15）

测试环境沿用前序阶段的真实 Chrome、WindowTabs 和 Windows 11 任务栏。程序会话从 17:36:32 开始，于 17:42:15 完成正常退出恢复。

| 验收项 | 结果 | 日志或人工证据 |
| --- | --- | --- |
| 从 1 个窗口新增到 2、3 个窗口 | PASS | 用户确认每次等待 3 秒内仍只有 1 个入口；新增窗口各有 1 次成功 `DeleteTab` |
| 关闭非主窗口 | PASS | 主入口保持 `0x0000000000020332`；已关闭身份安全跳过恢复，跟踪项由 2 降为 1 |
| 关闭主入口 | PASS | 主入口自动切换为 `0x0000000000060F40`；对应 `AddTab` 成功，跟踪项归零 |
| 全部关闭 | PASS | 同步报告 0 个可管理窗口、无主入口、0 个跟踪项 |
| 重新打开 3 个窗口 | PASS | 新 Chrome PID 为 25904；重建稳定主入口后，第 2、3 个窗口各成功执行 1 次 `DeleteTab` |
| `r` 暂停 | PASS | 两次 `AddTab` 成功，跟踪项归零；用户确认等待超过一个扫描周期后 3 个入口保持可见 |
| `a` 恢复管理 | PASS | 两次 `DeleteTab` 成功，重新收敛为 1 个入口 |
| 空闲 CPU | PASS | 用户观察至少 15 秒，CPU 正常且无持续异常占用 |
| `q` 正常退出恢复 | PASS | 两次 `AddTab` 成功，日志为 `Normal-exit restoration: SUCCESS`，跟踪项归零；用户确认 3 个入口全部恢复 |

日志汇总：11 次生命周期同步全部成功；6 次实际 `DeleteTab` 和 5 次实际 `AddTab` 均为 `HRESULT=0x00000000, Win32=0`；另有 1 次针对已关闭非主窗口的恢复协调被安全跳过（`Changed=no, Skipped=yes`），没有调用 `AddTab`。本次会话区间没有 `FAIL`、`ERROR`、`WARNING` 或恢复遗留。

PowerShell 退出码未被用户捕获，但日志明确记录正常退出恢复成功，程序正常结束，且用户确认全部按钮已恢复，因此不阻塞验收。

当前状态：`PASS`。Phase 4 验收完成；Phase 5/6 已按用户要求合并开发。

## Phase 5/6：恢复、托盘和便携发布（合并验收）

用户于 2026-07-15 要求 Phase 5 和 Phase 6 一次性开发，完成后再执行一次合并人工验收。人工验收通过前，产物版本为 `1.0.0-rc1`，不标记为最终 V1。

### 自动验收项目

| 项目 | 预期结果 | 实际结果 | 状态 |
| --- | --- | --- | --- |
| Debug/Release 增量构建 | 无编译、链接错误或警告 | 两种配置均成功，无警告 | PASS |
| Debug/Release CTest | 所有测试通过 | 两种配置均 4/4 通过 | PASS |
| 全新便携构建 | 独立构建树完成两种配置和测试 | `scripts/build-portable.ps1` 成功 | PASS |
| 配置解析 | 缺失使用默认值；合法值生效；非法值安全回退 | 单元测试覆盖 2000、1250 和越界值 | PASS |
| 恢复日志往返 | 身份及恢复义务完整保存 | 串行化、文件保存、加载和清空通过 | PASS |
| 损坏恢复日志 | 损坏、截断、重复记录整体拒绝 | 未采用任何部分记录 | PASS |
| 写前保护 | 恢复状态无法持久化时不得调用 `DeleteTab` | 伪控制器确认调用次数为 0 | PASS |
| 陈旧 HWND 防护 | PID/TID/创建时间/类名不匹配时不修改新窗口 | 进程创建时间不匹配路径安全跳过 | PASS |
| 恢复幂等性 | 重复恢复不产生额外修改或失败 | 核心和固定入口测试通过 | PASS |
| TaskbarCreated 状态重建 | 忘记旧 Shell 状态并重新应用规则 | 伪控制器恢复义务清空并重新 `DeleteTab` | PASS |
| 单实例互斥体 | 第一实例为主实例，第二实例识别已有实例 | 命名互斥体创建、检测、释放测试通过 | PASS |
| 实例通知消息 | 同步恢复和异步扫描消息可到达首实例 | Win32 消息窗口测试均通过 | PASS |
| 专用图标资源 | EXE 和托盘不再使用 Windows 占位图标 | SVG 源稿生成 16～256 像素共 9 个 ICO 图像；Debug/Release 资源 101 各尺寸均可加载 | PASS |
| 关于信息 | 菜单显示版本、开发人员和可点击项目地址 | 原生 Task Dialog、Common Controls v6 清单和浏览器打开回调已构建 | PASS |
| Release 命令回归 | `--version`、`--help`、`--list`、未知参数退出正确 | 退出码分别为 0、0、0、2 | PASS |
| 日志失败路径 | 日志目录不可创建时不崩溃 | `LOCALAPPDATA` 指向普通文件时 `--version` 仍返回 0 | PASS |
| Release PE | x64、Windows GUI、版本资源正确 | machine 8664、subsystem 2、`1.0.0-rc1` | PASS |
| 运行库依赖 | 便携 EXE 不依赖 MSVC 动态运行库 | 导入表无 VCRUNTIME/MSVCP | PASS |
| 交付文件 | EXE、INI、README、LICENSE 和 ZIP 齐全 | `dist` 目录及 ZIP 已生成 | PASS |

自动验收没有启动无参数托盘实例，也没有对真实 Chrome 执行 `--restore-all`，因为这两项会改变用户当前任务栏，必须结合视觉观察人工执行。

本次待验收产物指纹：

```text
EXE SHA-256: 415A890FCAC8C7D3F98FF0BB7099BFA0470D05CA0CEA25EED2B5160CB75B1403
ZIP SHA-256: 72A049AFD78ED3526E4B64BDA1F3B50F383CB41B5343B4EF68B8948F7A54F210
```

当前状态：`AUTOMATIC PASS / MANUAL PENDING`。

2026-07-15 第一轮人工观察：无控制台、托盘图标存在、单入口收敛和 WindowTabs 可操作性均符合预期；用户认为原先的 Windows 通用占位图标过于难看。随后已改为“三个彩色窗口汇聚成一个蓝色入口”的专用图标；用户继续检查时指出托盘菜单缺少“关于”。当前包已增加关于框、开发人员“杨云召”、可点击 GitHub 地址及对应 EXE/README/许可证元数据，等待人工复核。

### 合并人工验收前置条件

1. WindowTabs 正在运行；
2. 打开 3 个普通 Chrome 主窗口，每个窗口打开一个非关键网页；
3. 结束旧的 Debug `--manage` 会话，并确认任务管理器中没有 `ChromeTaskbarMerger.exe`；
4. 从仓库根目录操作，使用 `dist\ChromeTaskbarMerger\ChromeTaskbarMerger.exe`；
5. 如果任何步骤出现窗口无法到达、按钮无法恢复或与预期不符，立即停止后续步骤并保留日志。

### 人工验收步骤

#### A. 托盘、单入口和菜单

1. 双击便携版 `ChromeTaskbarMerger.exe`；
2. 确认没有普通主窗口或控制台，通知区域出现图标；
3. 等待不超过 3 秒，确认三个 Chrome 窗口仍可由 WindowTabs 操作，任务栏只剩一个 Chrome 入口；
4. 右键图标选择“立即重新扫描”，确认仍只有一个入口；
5. 选择“暂停管理”，确认三个 Chrome 入口全部恢复；等待至少 3 秒，确认不会重新隐藏；
6. 选择“恢复管理”，确认不超过 3 秒再次收敛为一个入口；
7. 选择“打开日志目录”，确认资源管理器打开 `%LOCALAPPDATA%\ChromeTaskbarMerger\logs`。
8. 选择“关于 ChromeTaskbarMerger”，确认显示 `1.0.0-rc1`、开发人员“杨云召”和 GitHub 地址；点击地址应由默认浏览器打开项目主页。

#### B. 第二实例

1. 保持首实例运行，再次双击同一个 EXE；
2. 在 PowerShell 执行：

   ```powershell
   @(Get-Process ChromeTaskbarMerger -ErrorAction SilentlyContinue).Count
   ```

3. 预期结果为 `1`；首实例显示已有实例/重新扫描提示，任务栏仍只有一个 Chrome 入口；
4. 日志应出现 `Second-instance synchronization` 或相应只读扫描记录。

#### C. Explorer 重建

1. 管理启用且任务栏为一个 Chrome 入口时，在任务管理器中对“Windows 资源管理器”选择“重新启动”；
2. 等待桌面和任务栏恢复；通知区域图标应重新出现；
3. 等待不超过一个配置扫描周期加 3 秒，确认任务栏重新收敛为一个 Chrome 入口；
4. 日志应包含 `TaskbarCreated received` 和后续成功同步。

#### D. 强制结束和持久恢复

1. 管理启用且任务栏为一个 Chrome 入口时，在任务管理器中强制结束 `ChromeTaskbarMerger.exe`；
2. 不关闭三个 Chrome 窗口；重新双击 EXE；
3. 程序应先处理上次恢复记录，再继续管理，最终在不超过 5 秒内保持一个入口；允许启动瞬间短暂出现多个按钮；
4. Chrome 窗口不得被关闭或变得无法通过 WindowTabs 到达；
5. 日志应包含 `Startup persisted-state restoration: SUCCESS`，随后为成功的 `Startup synchronization`，且没有恢复遗留。

#### E. 显式恢复命令和退出

1. 保持托盘实例运行并处于一个 Chrome 入口状态；
2. 在仓库根目录执行：

   ```powershell
   $process = Start-Process `
       -FilePath .\dist\ChromeTaskbarMerger\ChromeTaskbarMerger.exe `
       -ArgumentList '--restore-all' `
       -Wait -PassThru
   $process.ExitCode
   ```

3. 预期退出码为 `0`，三个 Chrome 入口全部恢复，首实例保持“已暂停”；等待至少 3 秒后仍保持三个入口；
4. 从托盘选择“恢复管理”，确认再次收敛为一个入口；
5. 从托盘选择“退出”，确认程序进程消失、三个 Chrome 入口全部恢复；
6. 阅读便携 README 的恢复和卸载步骤，确认描述与刚才的实际行为一致。无需在验收时真的删除仓库内 `dist` 目录。

### 失败时的安全恢复

先运行上面的 `--restore-all` 命令。如果退出码不是 `0` 或按钮仍不完整，在任务管理器中重启“Windows 资源管理器”，再运行一次恢复命令。保留以下日志，不要删除 `recovery-v1.tsv`：

```text
%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log
%LOCALAPPDATA%\ChromeTaskbarMerger\recovery-v1.tsv
```

人工步骤全部完成后，可把日志复制到仓库根目录，供 Codex 分析：

```powershell
Copy-Item `
    "$env:LOCALAPPDATA\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log" `
    .\phase56-validation.txt
```

### 阶段总表

| Phase | 主要人工观察内容 | 当前状态 |
| --- | --- | --- |
| Phase 2 | 任务栏按钮是否真实移除和恢复 | PASS：采用方法 A |
| Phase 3 | 多窗口时是否只保留一个入口 | PASS |
| Phase 4 | 新建、关闭窗口后的自动同步 | PASS |
| Phase 5 | Explorer 重启、异常恢复和单实例 | AUTOMATIC PASS / MANUAL PENDING |
| Phase 6 | 托盘菜单和 Release 便携体验 | AUTOMATIC PASS / MANUAL PENDING |
