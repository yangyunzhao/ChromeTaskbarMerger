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

## 后续 Phase

| Phase | 主要人工观察内容 | 当前状态 |
| --- | --- | --- |
| Phase 2 | 任务栏按钮是否真实移除和恢复 | NOT RUN |
| Phase 3 | 多窗口时是否只保留一个入口 | NOT RUN |
| Phase 4 | 新建、关闭窗口后的自动同步 | NOT RUN |
| Phase 5 | Explorer 重启、异常恢复和单实例 | NOT RUN |
| Phase 6 | 托盘菜单和 Release 使用体验 | NOT RUN |
