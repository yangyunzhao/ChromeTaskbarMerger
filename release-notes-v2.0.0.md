# ChromeTaskbarMerger v2.0.0

English | [简体中文](#简体中文)

ChromeTaskbarMerger 2.0.0 removes the mandatory WindowTabs dependency by adding
a built-in native tab strip and synchronized Chrome window group, while keeping
the single-Chrome-entry taskbar behavior from version 1.

## Highlights

- Groups 1–5 normal Chrome windows behind a native external tab strip without
  browser injection, remote debugging, `SetParent`, or Chrome data changes.
- Moves, resizes, minimizes, restores, maximizes, Snaps, and switches the group
  while retaining one Chrome taskbar entry.
- Tracks Chrome window creation and closure; later overflow windows remain safe
  and independent until a group slot becomes available.
- Provides built-in tabs by default or WindowTabs integration as a mutually
  exclusive user choice.
- Adds left/center/right tab alignment, strip-width percentage, per-tab width,
  mouse-wheel overflow, and `Ctrl+Alt+PageUp/PageDown` navigation.
- Adds Unicode custom tab names. Optional profile persistence applies names only
  to uniquely verified normal local Chrome profiles and strictly falls back to
  process-memory names for Incognito, Guest, ambiguous, or failed matches.
- Preserves recovery journals, Explorer rebuild handling, single instance,
  current-user login startup, tray controls, logs, About, and safe exit.
- Ships as a portable Windows x64 GUI with the MSVC runtime linked statically.

## Install or upgrade

1. Exit any running ChromeTaskbarMerger instance from its tray menu and verify
   Chrome taskbar buttons and layouts have returned.
2. Download `ChromeTaskbarMerger-2.0.0-portable-x64.zip` and extract it to a
   writable directory.
3. Run `ChromeTaskbarMerger.exe`; no installer or administrator rights are needed.
4. Version 1 INI files are migrated safely. The default provider is `builtin`;
   select WindowTabs from the tray only if you prefer its tab style.
5. If the portable directory moved, launch the EXE once from the new location to
   repair an enabled current-user login-startup path.

Profile-based custom-name persistence is disabled by default. Enable it from the
tray or set `persist_tab_names_by_profile=true`, then fully restart the program.

## Validation

- Clean x64 Debug and Release builds completed with `/W4` and no warnings.
- All 15/15 CTest tests passed in both configurations.
- Release is version 2.0.0, PE x64, Windows GUI subsystem, with no dynamic
  VCRUNTIME, MSVCP, or ucrtbase dependency.
- The portable ZIP contains only the EXE, default INI, portable README, and MIT
  license under one `ChromeTaskbarMerger` directory.
- Real Windows 11 validation covered 1/3/5 Chrome windows, lifecycle changes,
  movement, maximize/minimize/restore, Snap, F11, pause/resume, Explorer restart,
  forced-termination recovery, modal dialogs, profile names, real logout/login
  startup, singleton behavior, normal exit, and idle resources.

## Known limitations

- Initial built-in grouping accepts at most 5 normal, non-minimized Chrome
  windows. Additional windows opened later remain independent until a slot opens.
- Managed non-entry windows may be absent from Alt+Tab; use the built-in tab strip
  or select the WindowTabs provider to reach members.
- The managed maximize button may retain the maximize glyph instead of the usual
  restore glyph. Maximize and restore behavior is functional; the visual issue is
  deferred to V3.
- Windows 10, other Chromium browsers, and every multi-monitor/DPI/virtual-desktop
  combination have not been release-qualified.

## SHA-256

```text
424D6113B049B83FF5319B42D8BE93FEC53D6C27CFD66CEC1C32DBB1B490039B  ChromeTaskbarMerger-2.0.0-portable-x64.zip
C2A40DCA6E27A6156DC88D55AD409DBB02097CEFAE53A6D7DED95956512F795A  ChromeTaskbarMerger.exe
```

---

## 简体中文

ChromeTaskbarMerger 2.0.0 新增自研原生标签栏和同步 Chrome 窗口组，取消 WindowTabs
强制依赖，同时完整保留 V1 的任务栏单入口能力。

### 主要更新

- 将 1～5 个普通 Chrome 窗口组成原生外部标签组，不注入浏览器，不开启远程调试，不使用
  `SetParent`，也不修改 Chrome 数据；
- 统一移动、缩放、最小化、恢复、最大化、Snap 和标签切换，同时保留一个 Chrome 任务栏入口；
- 跟踪 Chrome 窗口新建和关闭；运行中超出上限的窗口安全保持独立，出现空位后再补入；
- 默认使用内置标签，也允许用户选择 WindowTabs 风格，两种方式严格二选一；
- 新增左/中/右对齐、标签栏宽度比例、单标签宽度、滚轮溢出和
  `Ctrl+Alt+PageUp/PageDown` 循环切换；
- 新增 Unicode 自定义标签名称。可选 profile 持久化只对唯一验证的普通本地 Chrome profile
  加载名称；无痕、访客、歧义或识别失败严格回退到进程内存名称；
- 保留恢复日志、Explorer 重建、单实例、当前用户登录启动、托盘控制、日志、关于和安全退出；
- 提供静态链接 MSVC 运行库的 Windows x64 便携 GUI。

### 安装或升级

1. 从托盘退出正在运行的 ChromeTaskbarMerger，确认 Chrome 任务栏按钮和布局已恢复；
2. 下载 `ChromeTaskbarMerger-2.0.0-portable-x64.zip` 并解压到任意可写目录；
3. 运行 `ChromeTaskbarMerger.exe`，无需安装或管理员权限；
4. V1 INI 会安全迁移，默认提供器为 `builtin`；只有偏好 WindowTabs 风格时才从托盘切换；
5. 如果移动了便携目录，请从新位置手工运行一次，以修复已启用的当前用户登录启动路径。

按 profile 保存自定义名称默认关闭。从托盘启用，或设置
`persist_tab_names_by_profile=true` 后完全重启程序才会生效。

### 验证结果

- 全新 x64 Debug/Release 均在 `/W4` 下无警告构建；
- 两种配置均为 15/15 CTest 通过；
- Release 版本为 2.0.0、PE x64、Windows GUI，且不动态依赖 VCRUNTIME、MSVCP 或 ucrtbase；
- 便携 ZIP 的单一 `ChromeTaskbarMerger` 目录中仅含 EXE、默认 INI、便携 README 和 MIT 许可证；
- Windows 11 真实验收覆盖 1/3/5 个 Chrome、生命周期变化、移动、最大化/最小化/恢复、Snap、
  F11、暂停/恢复、Explorer 重启、强制结束恢复、模态对话框、profile 名称、真实注销/登录启动、
  单实例、正常退出和空闲资源。

### 已知限制

- 首次内置建组最多接受 5 个普通、非最小化 Chrome 窗口；运行中新增的额外窗口保持独立，
  直到出现空位；
- 被管理的非入口窗口可能不会出现在 Alt+Tab 中；请使用内置标签栏，或选择 WindowTabs 提供器；
- 受管最大化按钮可能仍显示“最大化”而不是传统“还原”图标，但最大化和恢复行为正常；该视觉
  问题已移至 V3；
- Windows 10、其他 Chromium 浏览器，以及所有多显示器/DPI/虚拟桌面组合尚未完成正式验证。

### SHA-256

```text
424D6113B049B83FF5319B42D8BE93FEC53D6C27CFD66CEC1C32DBB1B490039B  ChromeTaskbarMerger-2.0.0-portable-x64.zip
C2A40DCA6E27A6156DC88D55AD409DBB02097CEFAE53A6D7DED95956512F795A  ChromeTaskbarMerger.exe
```
