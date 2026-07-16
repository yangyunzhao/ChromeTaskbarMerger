<p align="center">
  <img src="assets/ChromeTaskbarMerger.svg" width="112" alt="ChromeTaskbarMerger 图标">
</p>

<h1 align="center">ChromeTaskbarMerger</h1>

<p align="center">
  在多个 Chrome 窗口保持打开的同时，让 Windows 任务栏只保留一个固定的 Chrome 入口。
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

ChromeTaskbarMerger 是一个轻量级 Windows 原生工具，面向同时使用多个 Chrome 窗口和 WindowTabs 的用户。它不会关闭 Chrome 窗口或更改 Chrome 配置文件，而是在任务栏上保留一个固定的 Chrome 按钮。

程序常驻通知区域，持续处理 Chrome 窗口的新建和关闭；暂停或退出时恢复任务栏按钮；异常结束后通过持久恢复记录修复上次状态。

> [!IMPORTANT]
> 启用管理时必须保持 WindowTabs 运行。被任务栏合并的 Chrome 窗口也可能不会出现在 Alt+Tab 中，因此本版本以 WindowTabs 作为到达所有窗口的受支持方式。

## 主要功能

- 随 Chrome 窗口变化，将任务栏稳定收敛到零个或一个 Chrome 入口。
- 保留全部 Chrome 窗口和浏览器数据。
- Release 版本不显示控制台，常驻 Windows 通知区域。
- 提供立即扫描、暂停、恢复管理、恢复全部、打开日志、关于和安全退出。
- Windows 资源管理器重启后重新注册托盘图标并应用规则。
- 确保同一时间只有一套管理实例运行。
- 支持可配置的低频扫描，不使用忙循环。
- 支持当前用户登录 Windows 时自动启动，可从托盘或便携 INI 配置。
- WindowTabs 不可用时持续等待，并在其启动或重启后自动恢复管理。
- 每次移除任务栏按钮前先写入恢复意图。
- 恢复前验证 HWND、PID、TID、进程创建时间和窗口类。
- 提供独立的 `--restore-all` 恢复命令。
- 提供静态链接 MSVC 运行库的 x64 便携版。

## 工作原理

1. 枚举系统顶层窗口，通过完整可执行文件路径和 `Chrome_WidgetWin_*` 类识别 Chrome。
2. 首次启动时保留前台 Chrome 窗口，否则选择一个稳定的后备入口。
3. 对其他可管理 Chrome 窗口调用 `ITaskbarList::DeleteTab`。
4. 跟踪窗口生命周期，在配置的扫描周期后重新收敛为一个入口。
5. 暂停、显式恢复或正常退出时，使用 `ITaskbarList::AddTab` 恢复已跟踪按钮。

程序不会注入代码、关闭窗口、编辑 Chrome 首选项或修改 Chrome 配置文件数据。

## 运行要求

- Windows 11 x64。
- Google Chrome。
- WindowTabs 能够到达要管理的 Chrome 窗口；它可以晚于本程序启动，但进入管理后必须保持运行。
- 构建要求：CMake 3.24 或更高版本，以及包含“使用 C++ 的桌面开发”组件的 Visual Studio 2022 或更高版本。

## 快速开始

下载
[`ChromeTaskbarMerger-1.0.0-portable-x64.zip`](https://github.com/yangyunzhao/ChromeTaskbarMerger/releases/download/v1.0.0/ChromeTaskbarMerger-1.0.0-portable-x64.zip)，
解压到任意可写目录后运行 `ChromeTaskbarMerger.exe`，无需安装或管理员权限。

如需从源码生成相同的便携包：

```powershell
git clone https://github.com/yangyunzhao/ChromeTaskbarMerger.git
cd ChromeTaskbarMerger
.\scripts\build-portable.ps1
```

然后运行本地构建：

```powershell
.\dist\ChromeTaskbarMerger\ChromeTaskbarMerger.exe
```

程序会出现在 Windows 通知区域。再次启动 EXE 不会创建第二套管理逻辑，而是通知已有实例立即扫描。

## 托盘菜单

| 操作 | 行为 |
| --- | --- |
| 立即重新扫描 | 暂停时只读枚举 Chrome，管理中执行同步，等待时立即检查 WindowTabs。 |
| 暂停管理 | 恢复已跟踪按钮或取消依赖等待，并记录为明确的用户暂停。 |
| 恢复管理 | 重新检查前置条件；WindowTabs 已运行时立即管理，否则进入持续等待。 |
| 恢复全部 Chrome 按钮 | 对已跟踪和当前可识别的 Chrome 窗口执行安全恢复，随后保持暂停。 |
| 随 Windows 登录自动启动 | 原子更新便携配置和当前用户的 Windows 启动注册项。 |
| 打开日志目录 | 打开当前用户的日志目录。 |
| 关于 ChromeTaskbarMerger | 显示版本、开发人员、许可证和可点击的 GitHub 项目链接。 |
| 退出 | 恢复已跟踪按钮后退出；无法确认恢复时拒绝退出。 |

## 配置

将 `ChromeTaskbarMerger.ini` 放在 EXE 同一目录：

```ini
[ChromeTaskbarMerger]
scan_interval_ms=2000
windowtabs_check_interval_ms=3000
start_with_windows=false
```

两个时间间隔均允许 500～60000 毫秒：`scan_interval_ms` 控制管理中的 Chrome
扫描，`windowtabs_check_interval_ms` 控制等待期间的 WindowTabs 检测；
`start_with_windows` 接受 `true` 或 `false`，默认关闭。

通过托盘修改会立即生效并保存 INI。直接编辑文件只在进程启动时读取：必须从托盘彻底退出后重新运行。已有实例运行时再次双击 EXE 只会请求重新扫描，不算重启。手工将 `false` 改为 `true` 后，需要手动重新运行一次，程序才能创建当前用户的 Windows `Run` 启动项。

这里的自动启动是“当前用户登录后启动”，不是系统服务，无需管理员权限。手动启动和登录启动使用同一套前置条件逻辑：WindowTabs 不可用时，程序会持续显示“等待 WindowTabs”，不移除 Chrome 按钮；WindowTabs 出现后自动进入管理。管理期间 WindowTabs 退出时会先恢复按钮，重启后自动恢复管理。用户主动暂停则始终保持暂停。移动便携目录后，需要从新位置手动运行一次以修复注册的 EXE 路径。

## 日志和异常恢复

运行数据保存在便携目录之外：

```text
%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log
%LOCALAPPDATA%\ChromeTaskbarMerger\recovery-v1.tsv
```

如果程序被强制结束，请重新启动。程序会先恢复身份仍然精确匹配的上次状态，再应用当前规则。

在 PowerShell 中请求显式恢复并等待结果：

```powershell
$process = Start-Process `
    -FilePath .\ChromeTaskbarMerger.exe `
    -ArgumentList '--restore-all' `
    -Wait -PassThru
$process.ExitCode
```

退出码 `0` 表示恢复完成。如果按钮仍未返回，请保留恢复记录和日志，在任务管理器中重启“Windows 资源管理器”，然后再次执行 `--restore-all`。

> [!WARNING]
> 仍有 Chrome 任务栏按钮被移除时，不要手工删除 `recovery-v1.tsv`。

## 命令行诊断

```text
ChromeTaskbarMerger.exe --help
ChromeTaskbarMerger.exe --version
ChromeTaskbarMerger.exe --autostart
ChromeTaskbarMerger.exe --list
ChromeTaskbarMerger.exe --experiment
ChromeTaskbarMerger.exe --manage
ChromeTaskbarMerger.exe --restore-all
```

- `--list` 只读分类 Chrome 窗口。
- `--experiment` 保留 Phase 2 的交互式任务栏方法诊断。
- `--manage` 启动诊断用控制台生命周期监控。
- `--restore-all` 恢复有效记录及当前可识别 Chrome 的任务栏注册。
- `--autostart` 是 Windows 登录启动项使用的内部标记；如果 INI 已关闭自动启动，陈旧调用会删除注册项后退出。
- 未知参数返回退出码 `2`。

Release 是 Windows GUI 子系统程序。脚本需要等待完成或读取退出码时，请使用 `Start-Process -Wait -PassThru`。

## 构建和测试

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
cmake --build build --config Release
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

在独立的全新构建目录中生成便携目录和 ZIP：

```powershell
.\scripts\build-portable.ps1
```

构建目录和 `dist/` 已有意通过 Git 忽略。发布 ZIP 应作为 GitHub Release 附件上传，不应提交到源码仓库。

应用图标源稿和多尺寸 ICO 位于 `assets/`。修改 SVG 后，安装了 Chrome 和 FFmpeg 的开发环境可执行：

```powershell
.\scripts\build-icon.ps1
```

## 验证状态

版本 `1.0.0` 在 Debug 和 Release 配置下均通过全新构建和 6/6 CTest。自动测试覆盖命令解析、Chrome 身份、固定入口生命周期、恢复幂等性、写前失败保护、损坏日志拒绝、陈旧 HWND/PID 防护、任务栏重建、单实例消息、配置原子更新、临时注册表启动生命周期、便携路径修复、配置校验和管理状态转换。

真实任务栏已经验证 1、3、5 个 Chrome 窗口、窗口新建和关闭、主入口替换、暂停/恢复、Explorer 重启、强制结束恢复、正常退出恢复、便携托盘体验和空闲 CPU。自动进程级联调和用户使用真实 WindowTabs 的最终验收均确认了“等待 → 管理 → 等待”生命周期及用户暂停粘性。

详细工程证据：

- [开发计划和阶段报告](CODEX_TASK_Chrome_Taskbar_Merger_CPP.md)
- [手工测试计划和已记录结果](tests/manual_test_plan.md)

## 已知限制

- 被管理的非入口 Chrome 窗口可能不会出现在 Alt+Tab 中。
- 因此 WindowTabs 是启用管理的运行前置条件。
- 任务栏入口保持固定，不会跟随当前活动 Chrome 窗口。
- Windows 10、其他 Chromium 浏览器、多虚拟桌面及所有多显示器/DPI 组合尚未完成发布验证。
- Windows 设置或任务管理器可以禁用已注册的启动应用；系统层面的选择优先于 INI 设置。
- WindowTabs 可用性通过低频进程检测判断；没有独立的就绪 API，因此从进程出现到开始管理最多可能相差一个配置的检测周期。

## 项目结构

```text
assets/   应用图标源稿和 ICO
config/   便携配置模板
docs/     便携用户说明
scripts/  图标和便携包构建脚本
src/      C++ 实现和 Windows 资源
tests/    自动测试和人工验收记录
```

## 卸载

1. 在托盘菜单中取消勾选“随 Windows 登录自动启动”。
2. 选择“退出”，确认所有 Chrome 按钮恢复。
3. 删除便携程序目录。
4. 确认不再需要日志和恢复记录后，可删除 `%LOCALAPPDATA%\ChromeTaskbarMerger`。

## 开发人员

**杨云召** · [github.com/yangyunzhao](https://github.com/yangyunzhao)

项目地址：[github.com/yangyunzhao/ChromeTaskbarMerger](https://github.com/yangyunzhao/ChromeTaskbarMerger)

## 许可证

ChromeTaskbarMerger 使用 [MIT License](LICENSE)。
