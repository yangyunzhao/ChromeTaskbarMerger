# ChromeTaskbarMerger v1.0.0

English | [简体中文](#简体中文)

ChromeTaskbarMerger 1.0.0 is the first stable release of the lightweight native
Windows utility that keeps one fixed Chrome entry on the taskbar while multiple
Chrome windows remain open and reachable through WindowTabs.

## Highlights

- Keeps zero or one stable Chrome taskbar entry as Chrome windows open and close.
- Preserves every Chrome window and profile without browser injection or data changes.
- Runs in the notification area with rescan, pause/resume, restore-all, log-folder,
  About, login-startup, and safe-exit actions.
- Uses explicit states for management, WindowTabs waiting, user pause, error pause,
  and recovery-required conditions.
- Waits continuously when WindowTabs is unavailable, restores taskbar buttons if
  WindowTabs exits, and automatically resumes after it starts or restarts.
- Supports current-user Windows-login startup through the tray menu or portable INI.
- Re-registers the tray icon and reapplies management after Windows Explorer restarts.
- Writes recovery intent before each taskbar removal and validates HWND/PID/TID,
  process creation time, and window class before restoration.
- Ships as a portable Windows x64 package with the MSVC runtime linked statically.

## Configuration

`ChromeTaskbarMerger.ini` is stored beside the EXE:

```ini
[ChromeTaskbarMerger]
scan_interval_ms=2000
windowtabs_check_interval_ms=3000
start_with_windows=false
```

Both intervals accept 500–60000 milliseconds. Direct INI edits take effect after
the tray application is fully exited and restarted.

## Install or upgrade

1. Download `ChromeTaskbarMerger-1.0.0-portable-x64.zip` and extract it to a
   writable directory.
2. When upgrading, exit the older tray instance first and preserve your existing
   `ChromeTaskbarMerger.ini` if it contains custom settings.
3. Run `ChromeTaskbarMerger.exe`; no installer or administrator rights are required.
4. If the portable directory moved, run the EXE once from its new location so the
   optional Windows-login startup path can be repaired.

WindowTabs may start after ChromeTaskbarMerger. Management begins automatically
when WindowTabs is detected.

## Validation

- Clean Debug and Release builds completed successfully.
- All 6/6 CTest tests passed in both configurations.
- Real taskbar testing covered 1, 3, and 5 Chrome windows, window creation and
  closure, entry replacement, pause/resume, Explorer restart, forced-termination
  recovery, normal exit, portable use, and idle CPU behavior.
- Automated integration and final user testing with real WindowTabs confirmed
  `waiting → managing → waiting`, automatic restart recovery, and sticky user pause.
- The Release executable is an x64 Windows GUI application with no dynamic
  VCRUNTIME, MSVCP, or ucrtbase dependency.

## Known limitations

- Managed non-entry Chrome windows may be absent from Alt+Tab; WindowTabs is the
  supported way to reach all managed windows.
- The retained taskbar entry is fixed and does not follow the active Chrome window.
- Windows 10, other Chromium browsers, multiple virtual desktops, and every
  multi-monitor/DPI combination have not been release-qualified.
- WindowTabs readiness is detected by low-frequency process checks, so management
  may begin one configured check interval after the process appears.

## SHA-256

```text
B562958AA93384FDB2547B39B50AB0580B082CD6BD0D28CB588E02246585FBA9  ChromeTaskbarMerger-1.0.0-portable-x64.zip
1F0F1598EC5604D3E269DE0DA20048FE301EFC3C1BA6A7910430AAAAEB7D1944  ChromeTaskbarMerger.exe
```

---

## 简体中文

ChromeTaskbarMerger 1.0.0 是首个正式版本。这是一款轻量级 Windows 原生工具，
可在多个 Chrome 窗口保持打开并由 WindowTabs 到达时，让任务栏只保留一个固定的
Chrome 入口。

### 主要功能

- 随 Chrome 窗口的新建和关闭，将任务栏稳定收敛到零个或一个 Chrome 入口。
- 保留所有 Chrome 窗口和配置文件，不注入浏览器，也不修改浏览器数据。
- 常驻通知区域，提供立即扫描、暂停/恢复、恢复全部、打开日志、关于、登录启动和安全退出。
- 明确区分管理中、等待 WindowTabs、用户暂停、异常暂停和需要恢复等状态。
- WindowTabs 不可用时持续等待；WindowTabs 退出时恢复任务栏按钮，启动或重启后自动恢复管理。
- 可通过托盘菜单或便携 INI 配置当前用户登录 Windows 后自动启动。
- Windows 资源管理器重启后重新注册托盘图标并再次应用管理规则。
- 每次移除按钮前写入恢复意图；恢复前校验 HWND、PID、TID、进程创建时间和窗口类。
- 提供静态链接 MSVC 运行库的 Windows x64 便携包。

### 配置

`ChromeTaskbarMerger.ini` 与 EXE 位于同一目录：

```ini
[ChromeTaskbarMerger]
scan_interval_ms=2000
windowtabs_check_interval_ms=3000
start_with_windows=false
```

两个时间间隔均允许 500～60000 毫秒。直接编辑 INI 后，需要从托盘彻底退出并重新运行。

### 安装或升级

1. 下载 `ChromeTaskbarMerger-1.0.0-portable-x64.zip`，解压到任意可写目录。
2. 升级时先从托盘退出旧版本；如果已有自定义设置，请保留原来的
   `ChromeTaskbarMerger.ini`。
3. 运行 `ChromeTaskbarMerger.exe`，无需安装或管理员权限。
4. 如果移动了便携目录，请从新位置运行一次 EXE，以修复可选的 Windows 登录启动路径。

WindowTabs 可以晚于 ChromeTaskbarMerger 启动；检测到 WindowTabs 后会自动开始管理。

### 验证结果

- Debug 和 Release 全新构建均成功完成。
- 两种配置下均为 6/6 CTest 通过。
- 真实任务栏测试覆盖 1、3、5 个 Chrome 窗口，以及窗口新建/关闭、入口替换、
  暂停/恢复、Explorer 重启、强制结束恢复、正常退出、便携使用和空闲 CPU。
- 自动联调和用户使用真实 WindowTabs 的最终测试均确认“等待 → 管理 → 等待”、
  重启后自动恢复以及用户暂停粘性。
- Release 是 x64 Windows GUI 程序，不动态依赖 VCRUNTIME、MSVCP 或 ucrtbase。

### 已知限制

- 被管理的非入口 Chrome 窗口可能不会出现在 Alt+Tab 中；本版以 WindowTabs
  作为到达所有被管理窗口的受支持方式。
- 保留的任务栏入口固定，不会跟随当前活动 Chrome 窗口。
- Windows 10、其他 Chromium 浏览器、多虚拟桌面及全部多显示器/DPI 组合尚未完成发布验证。
- WindowTabs 就绪状态通过低频进程检测判断，因此最多可能在一个配置检测周期后开始管理。

### SHA-256

```text
B562958AA93384FDB2547B39B50AB0580B082CD6BD0D28CB588E02246585FBA9  ChromeTaskbarMerger-1.0.0-portable-x64.zip
1F0F1598EC5604D3E269DE0DA20048FE301EFC3C1BA6A7910430AAAAEB7D1944  ChromeTaskbarMerger.exe
```
