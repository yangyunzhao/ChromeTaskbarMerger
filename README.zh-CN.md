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
新增自研标签栏和同步窗口组，默认不再依赖 WindowTabs，同时完整保留 V1 已实现的任务栏
单入口能力。

程序常驻通知区域，不向 Chrome 注入代码，不开启远程调试，不关闭浏览器窗口，也不修改
Chrome 数据。

## 主要功能

- 将 1～5 个普通 Chrome 窗口组成带原生外部标签栏的窗口组；
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

下载
[`ChromeTaskbarMerger-2.0.0-portable-x64.zip`](https://github.com/yangyunzhao/ChromeTaskbarMerger/releases/download/v2.0.0/ChromeTaskbarMerger-2.0.0-portable-x64.zip)，
解压到任意可写目录后运行 `ChromeTaskbarMerger.exe`，无需安装或管理员权限。

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
tab_strip_alignment=center
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

2.0.0 的自动测试覆盖命令解析、配置与启动迁移、profile 匹配和存储、任务栏/布局恢复、窗口
生命周期和几何同步、标签编辑与导航、Explorer 重建及单实例。Windows 11 真实验收覆盖 1/3/5
个 Chrome、移动、最大化/最小化/恢复、Snap、F11、暂停/恢复、Explorer 重启、强制结束恢复、
登录启动、profile 名称、模态对话框、正常退出和空闲资源。

已知限制：

- 首次内置建组最多接受 5 个普通 Chrome 窗口；运行中新增的额外窗口保持独立，直到出现空位；
- 受管最大化按钮可能仍显示“最大化”而不是传统“还原”图标，但最大化和恢复行为正常；该视觉
  专项已登记到 V3；
- Windows 10、其他 Chromium 浏览器，以及所有多显示器/DPI/虚拟桌面组合尚未完成正式验证；
- Windows 设置或任务管理器可能禁用启动应用，系统层面的决定优先于 INI。

详细证据：[文档索引](docs/README.md)、[V2 需求](docs/V2_REQUIREMENTS.md)、
[profile 持久化评估](docs/V2_PROFILE_NAME_PERSISTENCE.md)和
[V2 人工验收记录](tests/manual_test_plan_v2.md)。

## 卸载

1. 在托盘取消“随 Windows 登录自动启动”；
2. 选择“退出”，确认 Chrome 任务栏按钮和窗口布局恢复；
3. 删除便携目录；
4. 确认恢复文件已不再需要后，可删除 `%LOCALAPPDATA%\ChromeTaskbarMerger`。

## 开发人员与许可证

**杨云召** · [github.com/yangyunzhao](https://github.com/yangyunzhao)

项目地址：[github.com/yangyunzhao/ChromeTaskbarMerger](https://github.com/yangyunzhao/ChromeTaskbarMerger)

本项目采用 [MIT License](LICENSE)。
