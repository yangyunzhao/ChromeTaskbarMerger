# ChromeTaskbarMerger V1 需求与设计

> 状态：`1.0.0` 已于 2026-07-16 完成开发、自动验收、真实环境人工验收和正式发布。
>
> 本文档是 V1 的稳定设计基线。逐步命令、日志和人工回报保存在
> [V1 手工测试计划](../tests/manual_test_plan.md)；V2 设计见
> [V2 需求与设计](V2_REQUIREMENTS.md)。

## 1. 背景

用户在 Windows 11 上同时运行多个 Chrome 配置文件，每个配置文件对应独立的
Chrome 顶层主窗口。WindowTabs 为这些窗口提供顶部标签切换，但 Windows 任务栏
仍显示多个 Chrome 按钮。

V1 的任务是开发一个轻量级 Windows 原生工具，只管理 Chrome 顶层窗口的任务栏
注册状态，不改变 Chrome 进程、网页标签、用户配置文件或窗口内容。

## 2. V1 核心目标

1. 自动发现当前桌面中的可管理 Chrome 顶层主窗口；
2. 存在一个或多个可管理窗口时，Windows 任务栏只保留一个固定 Chrome 入口；
3. Chrome 新建、关闭和重建窗口后，在配置的扫描周期内重新收敛到零个或一个入口；
4. 暂停、显式恢复、正常退出和下次启动恢复时，只恢复本程序实际修改的状态；
5. WindowTabs 启动顺序或临时退出不会造成永久误暂停或不可恢复状态；
6. 提供托盘菜单、日志、单实例、Explorer 重建处理、登录启动和便携发布能力。

## 3. 范围与优先级

V1 的优先级从高到低为：

1. 任务栏只保留一个 Chrome 入口；
2. 不关闭或隐藏 Chrome 窗口，不修改用户数据；
3. 所有修改具有可靠、幂等且身份安全的恢复路径；
4. 其他 Chrome 窗口仍可由 WindowTabs 到达和操作；
5. 尽量减少 WindowTabs、Alt+Tab 和 Explorer 重建期间的兼容性影响。

### 3.1 硬性要求

- 不通过关闭、最小化或隐藏 Chrome 窗口实现任务栏效果；
- 不注入 Chrome、WindowTabs 或 Explorer；
- 不修改 Chrome 用户数据、快捷方式、配置文件参数或系统文件；
- 任务栏修改前后检查 HRESULT、Win32 错误和窗口身份；
- API 返回成功不等于视觉验收通过；真实任务栏结果必须人工确认；
- 无法保证被移除窗口可达时，必须恢复任务栏按钮并停止管理；
- 正常退出必须恢复本程序仍在跟踪的任务栏修改；
- 陈旧或复用 HWND 不得导致无关窗口被修改。

### 3.2 非阻塞兼容目标

以下内容需要记录实际表现，但不阻塞 V1 正式发布：

- 被移除窗口继续出现在 Alt+Tab；
- 任务栏入口跟随当前活动 Chrome；
- WindowTabs 切换完全无闪烁；
- Chrome 窗口变化后立即而非在扫描周期内响应；
- 多显示器、虚拟桌面和所有 DPI 组合均完成验证。

## 4. 已验证的任务栏方案

### 4.1 正式方案：`ITaskbarList::DeleteTab/AddTab`

V1 使用：

```cpp
ITaskbarList::DeleteTab(HWND hwnd);
ITaskbarList::AddTab(HWND hwnd);
```

真实 Windows 11 验收确认：

- `DeleteTab` 返回成功后，目标 Chrome 按钮真实从任务栏消失；
- Chrome 窗口保持打开并可由 WindowTabs 操作；
- 目标窗口会暂时从 Alt+Tab 消失；
- `AddTab` 可以在不重启 Explorer 的情况下恢复按钮。

### 4.2 已拒绝方案：扩展窗口样式

实验曾评估 `WS_EX_APPWINDOW/WS_EX_TOOLWINDOW`。它能移除任务栏按钮并精确恢复
原始样式，但目标 Chrome 在测试环境中无法由 WindowTabs、Alt+Tab 或正常鼠标路径
到达，因此不作为正式实现或自动回退方案。

## 5. Chrome 窗口识别

不能仅根据标题包含 `Google Chrome` 判断。可管理窗口至少经过以下检查：

1. `GetWindowThreadProcessId` 获取 PID/TID；
2. `OpenProcess` 和 `QueryFullProcessImageNameW` 验证映像名为 `chrome.exe`；
3. 确认是可见、非子窗口、非工具窗口、非 `WS_EX_NOACTIVATE` 的顶层窗口；
4. 排除空标题、隐藏渲染、拖拽和其他辅助窗口；
5. 获取并记录类名、样式、扩展样式和 DWM cloaked 状态；
6. 接受经过验证的 `Chrome_WidgetWin_*` 主窗口类，同时保留兼容空间。

集中入口为：

```cpp
bool IsManageableChromeWindow(HWND hwnd);
```

首次发现规则和持续跟踪规则分开处理。窗口身份由 HWND、PID、TID、进程创建时间和
类名共同构成，不能把 HWND 当作永久标识。

## 6. 固定任务栏入口

V1 使用固定主入口：

1. 首次应用管理时，优先选择当前前台的可管理 Chrome；
2. 否则按稳定顺序选择一个可管理窗口；
3. 主入口身份仍有效时，不随前台窗口变化；
4. 其他可管理窗口通过 `DeleteTab` 从任务栏移除；
5. 主入口关闭后，从剩余窗口中提升新的固定主入口；
6. 所有 Chrome 关闭时清空入口和失效状态；重新打开后重新建立入口。

只有 `DeleteTab` 成功且恢复意图已经持久化的窗口才进入已修改状态表。重复扫描和
重复恢复不得产生重复任务栏调用。

## 7. 生命周期与 WindowTabs 前置条件

- 默认使用 2000 毫秒低频扫描发现 Chrome 新建、关闭和重建；
- 手动扫描会推迟紧邻的周期扫描，避免重复操作；
- 无变化周期保持安静，不产生忙循环；
- WindowTabs 不运行时不移除任何新按钮，并持续低频等待；
- WindowTabs 出现后自动进入管理；
- 管理期间 WindowTabs 退出时，先恢复按钮，再回到等待；
- 用户主动暂停具有粘性，不能被 WindowTabs 启停覆盖；
- 恢复失败进入 `RecoveryRequired`，普通恢复管理不能绕过。

V1 状态机：

```text
Initializing
WaitingForWindowTabs
Managing
PausedByUser
PausedByError
RecoveryRequired
```

只有 `Managing` 状态可以执行新的 `DeleteTab`。

## 8. 恢复与故障安全

恢复记录位于：

```text
%LOCALAPPDATA%\ChromeTaskbarMerger\recovery-v1.tsv
```

要求：

- 每次任务栏移除前先原子写入恢复意图；
- 写入失败时禁止调用 `DeleteTab`；
- 严格解析整个文件，损坏、截断、重复或超限记录不被部分采用；
- 恢复前重新验证 HWND、PID、TID、进程创建时间和类名；
- 恢复成功后原子清理对应记录；
- 重复恢复保持幂等；
- `--restore-all` 可以处理当前实例和有效恢复记录已知的修改；
- 进程崩溃后由下次启动读取日志并进行安全恢复，不能依赖异常回调清理。

## 9. Explorer、单实例和线程模型

- 使用 `Local\ChromeTaskbarMerger.Singleton` 保证单实例；
- 第二实例向第一实例发送重新扫描或恢复请求后退出；
- 注册 `TaskbarCreated`，Explorer 重建后重新初始化 `ITaskbarList`、注册托盘并重新同步；
- COM 初始化、窗口枚举、状态机和任务栏修改都由主消息线程串行处理；
- 定时器和实例消息只触发主线程同步，不并发修改同一窗口状态。

## 10. 托盘、配置和登录启动

托盘菜单提供：

```text
当前状态
立即重新扫描
暂停管理 / 恢复管理
恢复全部 Chrome 按钮
随 Windows 登录自动启动
打开日志目录
关于
退出
```

“关于”显示版本、开发人员杨云召和项目 GitHub 地址。

便携配置位于 EXE 同目录：

```ini
[ChromeTaskbarMerger]
scan_interval_ms=2000
windowtabs_check_interval_ms=3000
start_with_windows=false
```

- 两个时间间隔允许 500～60000 毫秒；
- 直接编辑 INI 只在完全退出并重启后生效；
- 登录启动使用当前用户的
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`；
- 配置是期望状态，注册表是实际入口；写入失败时回滚配置；
- 注册命令使用带引号的完整便携路径和内部 `--autostart` 标记；
- 移动便携目录后，从新位置手动运行一次可修复注册路径。

## 11. 构建、日志与性能

- Windows 11 x64；
- C++20、CMake、MSVC x64、Unicode Win32 API；
- `/W4` 且 Debug/Release 无警告；
- Release 使用 Windows GUI 子系统；
- 静态链接 MSVC 运行库；
- 不依赖 Qt、Electron 或 .NET；
- 日志写入 `%LOCALAPPDATA%\ChromeTaskbarMerger\logs`；
- 默认 INFO 日志不重复输出无变化扫描；
- 空闲 CPU 接近 0%，无小于 1 秒的固定轮询和忙循环。

## 12. 主要模块

| 模块 | V1 职责 |
| --- | --- |
| `chrome_window.*` | Chrome 枚举、身份和可管理性判断 |
| `taskbar_controller.*` | `ITaskbarList` 初始化、移除、恢复与实验路径 |
| `fixed_entry_manager.*` | 固定入口选择、幂等同步和已修改状态 |
| `recovery_journal.*` | 写前恢复日志、严格解析和原子更新 |
| `management_state.*` | WindowTabs 等待、管理、暂停和恢复门禁 |
| `tray_app.*` | 主消息循环、定时器、托盘、Explorer 和生命周期总控 |
| `auto_start.*` | INI 与当前用户 Run 注册表收敛 |
| `single_instance.*` | 互斥体和现有实例通知 |
| `app_config.*` / `app_paths.*` | 便携配置和运行路径 |
| `logger.*` | UTF-8 文件日志和错误证据 |

## 13. V1 阶段记录

详细步骤和实际结果见
[tests/manual_test_plan.md](../tests/manual_test_plan.md)。需求文档只保留阶段结论：

| Phase | 目标 | 最终状态 |
| --- | --- | --- |
| 0 | CMake/C++20 工程、Debug/Release 和基础测试 | PASS |
| 1 | Chrome 主窗口枚举与诊断 | PASS |
| 2 | 任务栏 API 可行性硬门槛 | PASS；采用方法 A，拒绝方法 B |
| 3 | 固定 Chrome 任务栏入口 MVP | PASS |
| 4 | Chrome 窗口生命周期自动维护 | PASS |
| 5 | 恢复日志、Explorer 重建和单实例 | PASS |
| 6 | 托盘、文档与便携发布 | PASS |
| 7 | 当前用户随 Windows 登录自动启动 | PASS；等待模型由 Phase 8 统一 |
| 8 | WindowTabs 持续等待、状态机和自动恢复 | PASS |

最终全新 `build-portable` 中 Debug 和 Release 均完成构建，两种配置均为 6/6
CTest 通过。真实 Chrome、WindowTabs 和 Windows 任务栏验收由用户确认通过。

## 14. V1 最终验收标准

正式版已经满足：

1. 1、3、5 个 Chrome 窗口均可收敛到一个任务栏入口；
2. 新建、关闭主入口、关闭非主窗口、全部关闭和重新打开均能自动同步；
3. 暂停、恢复全部、正常退出和 `--restore-all` 能恢复按钮；
4. Explorer 重启后托盘和管理规则重新建立；
5. 强制结束后的恢复不会依据陈旧 HWND 修改无关窗口；
6. WindowTabs 延迟启动、退出和重启采用统一状态机；
7. 用户暂停不会被依赖启停覆盖；
8. 登录启动、便携目录移动和配置重启生效行为明确；
9. Chrome 窗口和用户数据未被关闭、注入或修改；
10. Release 可直接运行，空闲 CPU 正常。

## 15. 已知限制

- 被任务栏移除的 Chrome 窗口可能不出现在 Alt+Tab；
- V1 必须依靠 WindowTabs 访问这些非入口窗口；
- 固定任务栏入口不跟随当前 WindowTabs 标签；
- Windows 10、其他 Chromium 浏览器、多虚拟桌面和全部多显示器/DPI 组合未正式覆盖；
- WindowTabs 就绪使用低频进程检测，最多会延迟一个配置周期开始管理。

这些限制是 V2 设计不再依赖 WindowTabs、提供内置标签栏的主要原因。

## 16. 正式发布

- 版本：`v1.0.0`
- Release：<https://github.com/yangyunzhao/ChromeTaskbarMerger/releases/tag/v1.0.0>
- 提交：`800f21d7c356c7674cc56cbd8f2b9825c7717817`

```text
ZIP SHA-256: B562958AA93384FDB2547B39B50AB0580B082CD6BD0D28CB588E02246585FBA9
EXE SHA-256: 1F0F1598EC5604D3E269DE0DA20048FE301EFC3C1BA6A7910430AAAAEB7D1944
```
