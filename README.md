<p align="center">
  <img src="assets/ChromeTaskbarMerger.svg" width="112" alt="ChromeTaskbarMerger icon">
</p>

<h1 align="center">ChromeTaskbarMerger</h1>

<p align="center">
  Keep one stable Chrome entry on the Windows taskbar while multiple Chrome windows remain open.
</p>

<p align="center">
  <strong>English</strong> · <a href="README.zh-CN.md">简体中文</a>
</p>

<p align="center">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Windows%2011%20x64-0078D4?logo=windows11&logoColor=white">
  <img alt="C++" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white">
  <img alt="CMake" src="https://img.shields.io/badge/CMake-3.24%2B-064F8C?logo=cmake&logoColor=white">
  <a href="https://github.com/yangyunzhao/ChromeTaskbarMerger/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/yangyunzhao/ChromeTaskbarMerger?display_name=tag&sort=semver"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-MIT-green"></a>
</p>

## Overview

ChromeTaskbarMerger is a lightweight native Windows utility for people who use multiple Chrome windows together with WindowTabs. It keeps a single fixed Chrome button on the taskbar without closing Chrome windows or changing Chrome profiles.

The application runs in the notification area, continuously handles Chrome window creation and closure, restores taskbar buttons when paused or exited, and keeps a persistent recovery journal for abnormal termination.

> [!IMPORTANT]
> WindowTabs must be running while management is enabled. Chrome windows hidden from the taskbar may also be absent from Alt+Tab, so WindowTabs provides the supported way to reach every window in this release.

## Features

- Keeps zero or one stable Chrome taskbar entry as Chrome windows change.
- Preserves all Chrome windows and browser data.
- Runs as a notification-area application with no console window in Release builds.
- Provides rescan, pause, resume, restore-all, log-folder, About, and safe-exit actions.
- Re-registers its tray icon and reapplies the rule after Windows Explorer restarts.
- Enforces a single running manager instance.
- Uses a configurable low-frequency scan interval without a busy loop.
- Optionally starts when the current user signs in to Windows, configured from
  either the tray menu or the portable INI file.
- Waits continuously when WindowTabs is unavailable and automatically resumes
  after it starts or restarts.
- Writes recovery intent before every taskbar removal.
- Validates HWND, PID, TID, process creation time, and window class before recovery.
- Provides a standalone `--restore-all` recovery command.
- Ships as a portable x64 application with the MSVC runtime linked statically.

## How it works

1. Enumerate top-level windows and identify Chrome by its full executable path and `Chrome_WidgetWin_*` class.
2. Keep the foreground Chrome window at startup, or choose a stable fallback entry.
3. Call `ITaskbarList::DeleteTab` for the other manageable Chrome windows.
4. Track window lifecycle changes and converge back to one entry after the configured interval.
5. Restore tracked buttons with `ITaskbarList::AddTab` when paused, explicitly restored, or normally exited.

The application does not inject code, close windows, edit Chrome preferences, or alter Chrome profile data.

## Requirements

- Windows 11 x64.
- Google Chrome.
- WindowTabs able to reach the managed Chrome windows. It may start later than
  ChromeTaskbarMerger, but must remain running while management is active.
- For building: CMake 3.24 or newer and Visual Studio 2022 or newer with **Desktop development with C++**.

## Quick start

Download
[`ChromeTaskbarMerger-1.0.0-portable-x64.zip`](https://github.com/yangyunzhao/ChromeTaskbarMerger/releases/download/v1.0.0/ChromeTaskbarMerger-1.0.0-portable-x64.zip),
extract it to a writable directory, and run `ChromeTaskbarMerger.exe`. No
installer or administrator privileges are required.

To build the same portable package from source:

```powershell
git clone https://github.com/yangyunzhao/ChromeTaskbarMerger.git
cd ChromeTaskbarMerger
.\scripts\build-portable.ps1
```

Then run the local build:

```powershell
.\dist\ChromeTaskbarMerger\ChromeTaskbarMerger.exe
```

The application appears in the Windows notification area. Starting the EXE again does not create another manager; it asks the existing instance to rescan.

## Tray menu

| Action | Behavior |
| --- | --- |
| Rescan now | Enumerates Chrome while paused, synchronizes while managing, or checks WindowTabs immediately while waiting. |
| Pause management | Restores tracked Chrome buttons or cancels prerequisite waiting, then records an explicit user pause. |
| Resume management | Revalidates prerequisites; manages immediately when WindowTabs is present or enters continuous waiting otherwise. |
| Restore all Chrome buttons | Explicitly calls safe restoration for tracked and currently identifiable Chrome windows, then remains paused. |
| Start when signing in to Windows | Atomically updates the portable configuration and the current-user Windows startup registration. |
| Open log folder | Opens the per-user log directory. |
| About ChromeTaskbarMerger | Shows the version, developer, license, and clickable GitHub project link. |
| Exit | Restores tracked buttons before exiting; refuses to exit if restoration cannot be confirmed. |

## Configuration

Place `ChromeTaskbarMerger.ini` beside the executable:

```ini
[ChromeTaskbarMerger]
scan_interval_ms=2000
windowtabs_check_interval_ms=3000
start_with_windows=false
```

Both intervals support 500–60000 milliseconds. `scan_interval_ms` controls Chrome
scans while managing; `windowtabs_check_interval_ms` controls prerequisite checks
while waiting. `start_with_windows` accepts `true` or `false` and defaults to
`false`.

The tray setting takes effect immediately and saves the INI file. Direct file
edits are read only at process startup: fully exit the running tray instance and
launch it again. Starting the EXE while an instance already exists only requests
a rescan and is not a restart. After changing `false` to `true` by hand, one
manual relaunch is required so the application can create its per-user Windows
`Run` entry.

Automatic launch means launch after the current user signs in, not a system
service at boot, and requires no administrator rights. Manual and login launches
share the same prerequisite behavior: if WindowTabs is unavailable,
ChromeTaskbarMerger waits in the tray indefinitely without removing Chrome
buttons. It starts management automatically after WindowTabs appears. If
WindowTabs exits during management, tracked buttons are restored and management
resumes automatically after WindowTabs restarts. An explicit user pause remains
paused. Moving the portable directory requires one manual launch from the new
location to repair the registered executable path.

## Logs and recovery

Runtime data is stored outside the portable directory:

```text
%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log
%LOCALAPPDATA%\ChromeTaskbarMerger\recovery-v1.tsv
```

If the process is forcibly terminated, start it again. It first restores any previous state that still matches the exact window identity, then reapplies the current rule.

To request explicit recovery from PowerShell and wait for its result:

```powershell
$process = Start-Process `
    -FilePath .\ChromeTaskbarMerger.exe `
    -ArgumentList '--restore-all' `
    -Wait -PassThru
$process.ExitCode
```

Exit code `0` means restoration completed. If buttons still do not return, keep the recovery journal and log, restart **Windows Explorer** from Task Manager, and run `--restore-all` again.

> [!WARNING]
> Do not manually delete `recovery-v1.tsv` while any Chrome taskbar button is still removed.

## Command-line diagnostics

```text
ChromeTaskbarMerger.exe --help
ChromeTaskbarMerger.exe --version
ChromeTaskbarMerger.exe --autostart
ChromeTaskbarMerger.exe --list
ChromeTaskbarMerger.exe --experiment
ChromeTaskbarMerger.exe --manage
ChromeTaskbarMerger.exe --restore-all
```

- `--list` performs a read-only Chrome window classification.
- `--experiment` retains the interactive Phase 2 taskbar-method diagnostic.
- `--manage` runs the diagnostic console lifecycle monitor.
- `--restore-all` restores valid recorded state and currently identifiable Chrome taskbar registrations.
- `--autostart` is the internal marker used by the Windows login entry. If the
  INI setting is disabled, a stale invocation removes its registration and exits.
- An unknown option returns exit code `2`.

Release is a Windows GUI-subsystem executable. Use `Start-Process -Wait -PassThru` when a script must wait for completion or read the exit code.

## Build and test

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
cmake --build build --config Release
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

Create the portable directory and ZIP from a clean dedicated build tree:

```powershell
.\scripts\build-portable.ps1
```

Generated build trees and `dist/` are intentionally ignored by Git. Release archives should be attached to GitHub Releases rather than committed to source control.

The application icon source and generated multi-size ICO are stored under `assets/`. After editing the SVG, developers with Chrome and FFmpeg installed can regenerate the ICO with:

```powershell
.\scripts\build-icon.ps1
```

## Validation status

Version `1.0.0` passes clean Debug and Release builds and 6/6 CTest tests in both configurations. Automated coverage includes command parsing, Chrome identity, fixed-entry lifecycle, recovery idempotence, write-ahead failure safety, corrupt journal rejection, stale HWND/PID protection, taskbar recreation, single-instance messaging, atomic configuration updates, temporary-registry startup lifecycle, portable-path repair, configuration validation, and management-state transitions.

Real taskbar validation has covered 1, 3, and 5 Chrome windows, window creation and closure, main-entry replacement, pause/resume, Explorer restart, forced-termination recovery, normal-exit restoration, the portable tray experience, and idle CPU behavior. Process-level integration and final user validation with the real WindowTabs application both confirmed the `waiting → managing → waiting` lifecycle and sticky user pause behavior.

Detailed engineering evidence:

- [Development plan and phase reports](CODEX_TASK_Chrome_Taskbar_Merger_CPP.md)
- [Manual test plan and recorded results](tests/manual_test_plan.md)

## Known limitations

- Managed non-entry Chrome windows may be absent from Alt+Tab.
- WindowTabs is therefore a runtime prerequisite for enabling management.
- The taskbar entry remains fixed; it does not follow the currently active Chrome window.
- Windows 10, other Chromium browsers, multiple virtual desktops, and all multi-monitor/DPI combinations have not yet been release-qualified.
- Windows Settings or Task Manager can disable a registered startup app; that
  system-level decision takes precedence over the INI setting.
- WindowTabs availability is detected by low-frequency process checks; there is
  no readiness API, so management may begin one configured check interval after
  the process appears.

## Project layout

```text
assets/   Application icon source and ICO
config/   Portable configuration template
docs/     Portable-user documentation
scripts/  Icon and portable-package build scripts
src/      C++ implementation and Windows resources
tests/    Automated tests and manual validation records
```

## Uninstall

1. Clear **Start when signing in to Windows** in the tray menu.
2. Select **Exit** and verify that all Chrome buttons return.
3. Delete the portable application directory.
4. Optionally delete `%LOCALAPPDATA%\ChromeTaskbarMerger` after confirming its logs and recovery journal are no longer needed.

## Developer

**杨云召** · [github.com/yangyunzhao](https://github.com/yangyunzhao)

Project: [github.com/yangyunzhao/ChromeTaskbarMerger](https://github.com/yangyunzhao/ChromeTaskbarMerger)

## License

ChromeTaskbarMerger is available under the [MIT License](LICENSE).
