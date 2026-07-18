<p align="center">
  <img src="assets/ChromeTaskbarMerger.svg" width="112" alt="ChromeTaskbarMerger 图标">
</p>

<h1 align="center">ChromeTaskbarMerger</h1>

<p align="center">
  将多个 Chrome 窗口组成原生标签组，同时让 Windows 任务栏只保留一个 Chrome 入口。
</p>

<p align="center">
  <a href="README.md">English</a> · <strong>简体中文</strong>
</p>

<p align="center">
  <img alt="平台" src="https://img.shields.io/badge/platform-Windows%2011%20x64-0078D4?logo=windows11&logoColor=white">
  <img alt="C++" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white">
  <img alt="CMake" src="https://img.shields.io/badge/CMake-3.24%2B-064F8C?logo=cmake&logoColor=white">
  <a href="https://github.com/yangyunzhao/ChromeTaskbarMerger/releases/latest"><img alt="最新版本" src="https://img.shields.io/github/v/release/yangyunzhao/ChromeTaskbarMerger?display_name=tag&sort=semver"></a>
  <a href="LICENSE"><img alt="许可证" src="https://img.shields.io/badge/license-MIT-green"></a>
</p>

## 项目简介

ChromeTaskbarMerger 是一款面向多 Chrome 窗口用户的轻量级 Windows 原生工具。V2
引入自研标签栏和同步窗口组，默认不再依赖 WindowTabs；V3.1 增加真实原生最大化/还原和
与 Chrome 视觉融合的紧凑抗锯齿标签，同时完整保留 V1 已实现的任务栏单入口能力。

程序常驻通知区域，不向 Chrome 注入代码，不开启远程调试，不关闭浏览器窗口，也不修改
Chrome 数据。

## 主要功能

- 将 1～5 个普通 Chrome 窗口组成带紧凑原生外部标签栏的窗口组；
- 统一切换、移动、缩放、最小化、恢复、最大化和 Snap；
- 管理期间让 Windows 任务栏只保留一个 Chrome 入口；
- 跟踪 Chrome 窗口的新建和关闭，并安全填充空闲组位置；
- 默认使用内置标签，也可与 WindowTabs 标签严格二选一；
- 支持标签栏左/中/右对齐、总宽度比例、单标签宽度、滚轮溢出和键盘循环切换；
- 双击内置标签可输入中文及其他 Unicode 自定义名称；
- 可选按唯一验证的本地 Chrome profile 恢复自定义名称；
- 提供扫描、暂停/恢复、恢复全部、日志、关于、登录启动和安全退出；
- Explorer 重启或进程异常结束后可恢复任务栏与窗口布局；
- 单实例运行，提供静态链接 MSVC 运行库的 x64 便携 GUI。

## 快速开始

打开 [Latest release](https://github.com/yangyunzhao/ChromeTaskbarMerger/releases/latest)，
下载最新的 `ChromeTaskbarMerger-*-portable-x64.zip`，解压到任意可写目录后运行
`ChromeTaskbarMerger.exe`，无需安装或管理员权限。

`main` 分支已包含验收完成的 3.1.0 源码基线；只有完成独立的 Release 发布步骤后，Releases 页面
才会出现 3.1.0 二进制包。

默认选择内置标签，不需要 WindowTabs。首次建立内置组时，请保留 1～5 个处于普通、非最小化
状态的 Chrome 窗口。

## 工作原理

1. 通过完整可执行文件路径、进程身份和 `Chrome_WidgetWin_*` 窗口类识别 Chrome；
2. 内置模式创建独立的原生标签栏，并在不使用 `SetParent` 的前提下同步成员窗口；
3. 通过 `ITaskbarList::DeleteTab/AddTab` 保留一个 Chrome 任务栏入口；
4. 每次修改任务栏或布局前先写入恢复记录；
5. 暂停、恢复全部、正常退出或下次启动时，只恢复完整身份仍匹配的窗口。

被管理的非入口窗口可能不会出现在 Alt+Tab 中；内置标签栏是到达全部成员的受支持方式。
选择 WindowTabs 模式时，由 WindowTabs 提供标签和窗口组，本程序只管理任务栏入口；两种
方式不会同时管理标签。

## 托盘菜单

| 操作 | 行为 |
| --- | --- |
| 立即重新扫描 | 立即同步 Chrome 窗口或检查当前标签提供器。 |
| 暂停/恢复管理 | 恢复当前组，或重新验证并启动所选提供器。 |
| 恢复全部任务栏和窗口布局 | 执行显式安全恢复，完成后保持暂停。 |
| 内置标签/WindowTabs 标签 | 二选一，完全重启程序后生效。 |
| 按 Chrome profile 保存标签名称 | 启用保守的 profile 名称持久化，重启生效。 |
| 随 Windows 登录自动启动 | 原子更新便携 INI 和当前用户 Run 启动项。 |
| 打开日志/关于 | 打开诊断目录，或显示版本、开发人员、许可证和项目地址。 |
| 退出 | 恢复任务栏和布局后安全退出。 |

## 配置

`ChromeTaskbarMerger.ini` 必须与 EXE 位于同一目录：

```ini
[ChromeTaskbarMerger]
scan_interval_ms=2000
tab_provider=builtin
persist_tab_names_by_profile=false
windowtabs_check_interval_ms=3000
tab_strip_alignment=right
tab_strip_width_percent=60
tab_width_px=180
start_with_windows=false
```

- 两个时间间隔允许 500～60000 毫秒；
- `tab_provider` 只接受 `builtin` 或 `windowtabs`；
- 对齐只接受 `left`、`center`、`right`；标签栏总宽度允许 25～100%，单标签宽度允许
  80～400 逻辑像素；
- 直接修改 INI、切换标签方式或 profile 持久化后，必须从托盘完全退出并重新启动；
- 登录启动只针对当前用户，无需管理员权限；移动便携目录后，从新位置手工运行一次即可修复路径。

### 自定义标签名称

在内置模式下双击标签主体，输入任意 Unicode 文本后按 `Enter`；按 `Esc` 取消，清空并确认则
恢复 Chrome 实时标题。名称至少会在当前程序进程内一直有效。

profile 持久化默认关闭。启用后，文件只保存 SHA-256 profile 键和自定义名称；只有目标窗口能与
本地 Chrome profile 元数据唯一验证时才加载。无痕、访客、歧义或识别失败会保留内存名称，程序
不会猜测或串用。

## 日志和异常恢复

运行数据保存在 `%LOCALAPPDATA%\ChromeTaskbarMerger`：

```text
logs\ChromeTaskbarMerger.log
recovery-v1.tsv
recovery-v2.tsv
profile-tab-names-v1.tsv
```

如果进程被强制结束，重新启动 EXE；程序会先恢复有效的上次状态，再开始新的管理会话。
也可以在 PowerShell 中显式恢复并读取退出码：

```powershell
$process = Start-Process `
    -FilePath .\ChromeTaskbarMerger.exe `
    -ArgumentList '--restore-all' `
    -Wait -PassThru
$process.ExitCode
```

任务栏按钮或布局仍被修改时，不要手工删除恢复记录。

## 构建和测试

构建要求：Windows 11 x64、CMake 3.24+，以及包含“使用 C++ 的桌面开发”的 Visual Studio
2022 或更高版本。

```powershell
git clone https://github.com/yangyunzhao/ChromeTaskbarMerger.git
cd ChromeTaskbarMerger
.\scripts\build-portable.ps1
```

脚本会执行全新 Debug/Release 构建和两套 CTest，并生成 `dist/ChromeTaskbarMerger`、便携 ZIP
及 `SHA256SUMS.txt`。构建树和 `dist/` 不提交到 Git。

常用诊断命令：

```text
ChromeTaskbarMerger.exe --help
ChromeTaskbarMerger.exe --version
ChromeTaskbarMerger.exe --list
ChromeTaskbarMerger.exe --v2-experiment
ChromeTaskbarMerger.exe --restore-all
```

## 验证状态与已知限制

3.1.0 保留 V2 全部自动测试，并增加原生最大化/还原状态、紧凑标签定位、窗口按钮保留区、
不可见窗口边框修正、逐像素透明、DPI 缩放和分层窗口恢复测试。全新 x64 Debug/Release 均为
15/15 通过。Windows 11 真实验收覆盖 V2 的 1/3/5 窗口生命周期与恢复矩阵，以及 V3.1 普通/
最大化视觉融合和 WindowTabs 参考图对比。

已知限制：

- 首次内置建组最多接受 5 个普通 Chrome 窗口；运行中新增的额外窗口保持独立，直到出现空位；
- Windows 10、其他 Chromium 浏览器，以及所有多显示器/DPI/虚拟桌面组合尚未完成正式验证；
- Chrome F11 真正全屏不属于 V3.1 范围；Chrome 原生网页标签很多时，右侧标签可能被内置窗口
  标签局部遮挡；
- Windows 设置或任务管理器可能禁用启动应用，系统层面的决定优先于 INI。

详细证据：[文档索引](docs/README.md)、[V2 需求](docs/V2_REQUIREMENTS.md)、
[profile 持久化评估](docs/V2_PROFILE_NAME_PERSISTENCE.md)和
[V2 人工验收记录](tests/manual_test_plan_v2.md)，以及
[V3.1 需求](docs/V3_1_REQUIREMENTS.md)和
[V3.1 验收记录](tests/manual_test_plan_v3_1.md)。

## 后续规划

V3 是持续性的易用性、视觉一致性和缺陷修复系列，原则上不增加新的产品能力。每个内聚优化形成
一个可独立验收和发布的小版本，不再使用一套长期固定的 Phase。V3.1 / 3.1.0 已于 2026-07-18
完成自动和真实 Chrome 验收并冻结，主要成果包括：

- Chrome 保持原生最大化，使最大化/还原图标与真实状态和操作语义一致；
- 统一内置标签栏在普通、最大化、Snap、DPI 和工作区变化后的目标位置；
- 最大化时标签栏在视觉上融入 Chrome，获得类似 WindowTabs 的统一窗口观感，但不复制或
  注入 WindowTabs；
- 保持恢复、焦点、资源占用和既有提供器能力不回归。

最大化视觉目标已通过用户提供的 WindowTabs 参考图片确认。V2 的恢复和窗口可达性仍是硬门槛；
不支持的视觉方案必须安全回退到已经验证的 2.0.0 布局。

最大化方案明确不处理与 Chrome 原生网页标签的碰撞：当原生标签很多时，右侧标签可能被内置窗口
标签局部遮挡，这与用户接受的 WindowTabs 表现一致；右上角三个窗口按钮必须始终避让。Chrome
F11 真正全屏不属于 V3 范围。

本次验收之后发现的新 BUG 或质量优化统一进入 V3.2，不再重新打开 V3.1。完整规则见
[V3 质量改进路线图](docs/V3_ROADMAP.md)，已完成范围见
[V3.1 顶部视觉融合与原生最大化](docs/V3_1_REQUIREMENTS.md)。会扩大产品能力的需求统一进入独立的
[V4 功能需求池](docs/V4_BACKLOG.md)，不扩大 V3 范围。

## 卸载

1. 在托盘取消“随 Windows 登录自动启动”；
2. 选择“退出”，确认 Chrome 任务栏按钮和窗口布局恢复；
3. 删除便携目录；
4. 确认恢复文件已不再需要后，可删除 `%LOCALAPPDATA%\ChromeTaskbarMerger`。

## 开发人员与许可证

**杨云召** · [github.com/yangyunzhao](https://github.com/yangyunzhao)

项目地址：[github.com/yangyunzhao/ChromeTaskbarMerger](https://github.com/yangyunzhao/ChromeTaskbarMerger)

本项目采用 [MIT License](LICENSE)。
