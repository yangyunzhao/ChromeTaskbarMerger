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

## 后续 Phase

| Phase | 主要人工观察内容 | 当前状态 |
| --- | --- | --- |
| Phase 1 | Chrome 主窗口枚举结果是否准确 | NOT RUN |
| Phase 2 | 任务栏按钮是否真实移除和恢复 | NOT RUN |
| Phase 3 | 多窗口时是否只保留一个入口 | NOT RUN |
| Phase 4 | 新建、关闭窗口后的自动同步 | NOT RUN |
| Phase 5 | Explorer 重启、异常恢复和单实例 | NOT RUN |
| Phase 6 | 托盘菜单和 Release 使用体验 | NOT RUN |
