<p align="center">
  <img src="assets/ChromeTaskbarMerger.svg" width="112" alt="ChromeTaskbarMerger icon">
</p>

<h1 align="center">ChromeTaskbarMerger</h1>

<p align="center">
  Group multiple Chrome windows behind native tabs and keep one Chrome entry on the Windows taskbar.
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

ChromeTaskbarMerger is a lightweight native Windows utility for people who keep
several Google Chrome windows open. Version 2 adds a built-in tab strip and a
synchronized window group, so WindowTabs is no longer required. The original
single-taskbar-entry behavior remains part of the product.

The application runs in the notification area. It does not inject code into
Chrome, enable remote debugging, close browser windows, or modify Chrome data.

## Features

- Groups 1–5 normal Chrome windows behind a native external tab strip.
- Switches, moves, resizes, minimizes, restores, maximizes, and Snaps the group.
- Keeps exactly one Chrome entry on the Windows taskbar while a group is managed.
- Tracks Chrome window creation and closure and safely fills available group slots.
- Offers built-in tabs by default or WindowTabs integration as a mutually exclusive option.
- Supports left, centered, or right tab-strip alignment, total-width percentage,
  per-tab width, mouse-wheel overflow, and keyboard tab cycling.
- Lets users double-click a built-in tab to enter a Unicode custom name.
- Optionally restores custom names for uniquely verified local Chrome profiles.
- Provides rescan, pause/resume, restore-all, logs, About, login startup, and safe exit.
- Recovers taskbar and group layout after Explorer restart or abnormal termination.
- Runs as a single-instance, portable x64 GUI with the MSVC runtime linked statically.

## Quick start

Download
[`ChromeTaskbarMerger-2.0.0-portable-x64.zip`](https://github.com/yangyunzhao/ChromeTaskbarMerger/releases/download/v2.0.0/ChromeTaskbarMerger-2.0.0-portable-x64.zip),
extract it to a writable directory, and run `ChromeTaskbarMerger.exe`. No installer
or administrator privileges are required.

The built-in provider is selected by default and does not need WindowTabs. Keep
1–5 normal, non-minimized Chrome windows open when creating the initial group.

## How it works

1. Chrome windows are identified by full executable path, process identity, and
   `Chrome_WidgetWin_*` window class.
2. The built-in provider creates a separate native tab strip and synchronizes the
   member windows as one group without using `SetParent`.
3. The taskbar controller uses `ITaskbarList::DeleteTab/AddTab` to retain one
   Chrome entry.
4. Write-ahead recovery journals are saved before taskbar or layout changes.
5. Pause, restore-all, normal exit, and the next launch restore only windows whose
   complete identities still match.

Managed non-entry windows may be absent from Alt+Tab; the built-in tab strip is
the supported way to reach every member. The optional WindowTabs provider uses
WindowTabs for tabs and grouping while ChromeTaskbarMerger only manages the
taskbar entry. The two providers never manage tabs at the same time.

## Tray menu

| Action | Behavior |
| --- | --- |
| Rescan now | Synchronizes Chrome windows or checks the selected provider immediately. |
| Pause / Resume management | Restores the current group, or validates and resumes the selected provider. |
| Restore all taskbar buttons and layouts | Performs explicit safe recovery and remains paused. |
| Built-in tabs / WindowTabs tabs | Selects one tab provider; fully restart the app to apply. |
| Remember names by Chrome profile | Enables conservative profile-based name persistence; restart required. |
| Start when signing in to Windows | Updates the portable INI and current-user Run registration atomically. |
| Open log folder / About | Opens diagnostics or shows version, developer, license, and project link. |
| Exit | Restores taskbar and layout state before exiting. |

## Configuration

Place `ChromeTaskbarMerger.ini` beside the executable:

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

- Intervals accept 500–60000 milliseconds.
- `tab_provider` accepts `builtin` or `windowtabs`.
- Alignment accepts `left`, `center`, or `right`; strip width accepts 25–100%;
  tab width accepts 80–400 logical pixels.
- Direct INI edits and provider/profile-persistence changes take effect after the
  tray application is fully exited and restarted.
- Login startup is per-user, requires no administrator rights, and repairs its
  path after the portable EXE is manually launched from a new directory.

### Custom tab names

In built-in mode, double-click a tab body, type any Unicode text, and press
`Enter`; press `Esc` to cancel or confirm an empty value to return to the live
Chrome title. Names remain in memory for the process lifetime.

Profile persistence is disabled by default. When enabled, it stores only a
SHA-256 profile key and the custom name. It applies a name only when the target
window can be uniquely verified against local Chrome profile metadata. Incognito,
Guest, ambiguous, or failed matches keep the in-memory name and never guess.

## Logs and recovery

Runtime state is stored under `%LOCALAPPDATA%\ChromeTaskbarMerger`:

```text
logs\ChromeTaskbarMerger.log
recovery-v1.tsv
recovery-v2.tsv
profile-tab-names-v1.tsv
```

If the process is forcibly terminated, start the EXE again. It restores valid
recorded state before beginning a new session. For explicit recovery:

```powershell
$process = Start-Process `
    -FilePath .\ChromeTaskbarMerger.exe `
    -ArgumentList '--restore-all' `
    -Wait -PassThru
$process.ExitCode
```

Do not delete recovery journals while taskbar buttons or layouts are still modified.

## Build and test

Requirements: Windows 11 x64, CMake 3.24+, and Visual Studio 2022+ with
**Desktop development with C++**.

```powershell
git clone https://github.com/yangyunzhao/ChromeTaskbarMerger.git
cd ChromeTaskbarMerger
.\scripts\build-portable.ps1
```

The script creates a clean Debug and Release build, runs both CTest suites, and
generates `dist/ChromeTaskbarMerger`, the portable ZIP, and `SHA256SUMS.txt`.
Build trees and `dist/` are intentionally not committed.

Useful diagnostics:

```text
ChromeTaskbarMerger.exe --help
ChromeTaskbarMerger.exe --version
ChromeTaskbarMerger.exe --list
ChromeTaskbarMerger.exe --v2-experiment
ChromeTaskbarMerger.exe --restore-all
```

## Validation and limitations

Version 2.0.0 has automated coverage for command parsing, configuration and
startup migration, profile matching and storage, taskbar/layout recovery,
window lifecycle and geometry, tab editing/navigation, Explorer rebuild, and
single-instance behavior. Real Windows 11 validation covered 1/3/5 Chrome
windows, movement, maximize/minimize/restore, Snap, F11, pause/resume, Explorer
restart, forced termination recovery, login startup, profile names, modal dialogs,
normal exit, and idle resources.

Known limits:

- Initial built-in grouping accepts at most 5 normal Chrome windows. Additional
  windows opened later remain independent until a slot is available.
- The managed maximize button may retain the maximize glyph instead of the usual
  restore glyph; maximize and restore behavior still works. This is tracked for V3.
- Windows 10, other Chromium browsers, and every multi-monitor/DPI/virtual-desktop
  combination have not been release-qualified.
- Windows Settings or Task Manager may disable a registered startup app; that
  system-level choice takes precedence over the INI setting.

Detailed evidence: [documentation index](docs/README.md),
[V2 requirements](docs/V2_REQUIREMENTS.md),
[profile persistence assessment](docs/V2_PROFILE_NAME_PERSISTENCE.md), and
[V2 manual results](tests/manual_test_plan_v2.md).

## Uninstall

1. Clear **Start when signing in to Windows** in the tray menu.
2. Select **Exit** and verify that Chrome taskbar buttons and layouts return.
3. Delete the portable directory.
4. Optionally delete `%LOCALAPPDATA%\ChromeTaskbarMerger` after confirming its
   recovery files are no longer needed.

## Developer and license

**杨云召** · [github.com/yangyunzhao](https://github.com/yangyunzhao)

Project: [github.com/yangyunzhao/ChromeTaskbarMerger](https://github.com/yangyunzhao/ChromeTaskbarMerger)

Licensed under the [MIT License](LICENSE).
