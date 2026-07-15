# ChromeTaskbarMerger 手工测试计划

## 状态说明

- `PASS`：已经执行并符合预期；
- `FAIL`：已经执行但不符合预期；
- `BLOCKED`：环境不足或等待人工观察；
- `NOT RUN`：尚未进入对应 Phase。

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

当前状态：`BLOCKED`，仅等待用户完成真实任务栏视觉验收。实现和不触碰真实 Chrome 的自动验收已经完成，但 API 返回成功不能证明 Windows 11 任务栏按钮真的消失。

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
| 真实任务栏视觉结果 | A、B 均实际执行并由用户观察 | 等待用户 | BLOCKED |

自动验收日期：2026-07-15。自动测试没有对真实 Chrome 调用 `DeleteTab`、`AddTab` 或 `SetWindowLongPtrW`；实际修改只允许在下面的人工步骤中，由用户输入 `APPLY` 后发生。

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

当前结论：自动验收通过，人工视觉硬门槛尚未完成，因此 Phase 2 暂未通过，Phase 3 不允许开始。

## 后续 Phase

| Phase | 主要人工观察内容 | 当前状态 |
| --- | --- | --- |
| Phase 2 | 任务栏按钮是否真实移除和恢复 | BLOCKED：等待人工视觉验收 |
| Phase 3 | 多窗口时是否只保留一个入口 | NOT RUN |
| Phase 4 | 新建、关闭窗口后的自动同步 | NOT RUN |
| Phase 5 | Explorer 重启、异常恢复和单实例 | NOT RUN |
| Phase 6 | 托盘菜单和 Release 使用体验 | NOT RUN |
