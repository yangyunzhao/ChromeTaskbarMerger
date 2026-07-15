# ChromeTaskbarMerger

ChromeTaskbarMerger 是一个计划中的 Windows 原生工具，目标是在多个 Chrome 主窗口同时存在时，让 Windows 任务栏只保留一个固定 Chrome 入口。

## 当前状态

**Phase 0：工程骨架和可重复构建** 已完成并通过自动验收。Phase 1 尚未开始。

此阶段只提供：

- CMake C++20 工程；
- `--help` 和 `--version` 命令行入口；
- `%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log` 基础日志；
- 无第三方测试框架的命令行解析测试；
- Debug 控制台程序和 Release Windows 子系统程序。

当前版本不会枚举或修改 Chrome 窗口，也不会调用任务栏修改 API。

完整开发计划见 [CODEX_TASK_Chrome_Taskbar_Merger_CPP.md](CODEX_TASK_Chrome_Taskbar_Merger_CPP.md)。

## 构建要求

- Windows 11 x64；
- CMake 3.24 或更高版本；
- Visual Studio 2022 或更高版本，包含“使用 C++ 的桌面开发”组件。

## 构建

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
cmake --build build --config Release
```

## 自动测试

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

## Phase 0 命令行

```powershell
.\build\Debug\ChromeTaskbarMerger.exe --help
.\build\Debug\ChromeTaskbarMerger.exe --version
.\build\Debug\ChromeTaskbarMerger.exe
```

未知参数会返回退出码 `2`：

```powershell
.\build\Debug\ChromeTaskbarMerger.exe --unknown
$LASTEXITCODE
```

## 日志

默认日志文件：

```text
%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log
```

日志目录无法创建时，程序会输出警告，但不会崩溃。

## Phase 0 验收记录

验收日期：2026-07-15。

验证环境：

- CMake 4.3.0；
- Visual Studio 18 2026 Community；
- MSVC 19.51.36246；
- Windows SDK 10.0.26100.0；
- x64 目标平台。

| 验收项 | 结果 |
| --- | --- |
| CMake x64 配置 | PASS |
| Debug 构建 | PASS，无编译警告 |
| Release 构建 | PASS，无编译警告 |
| Debug CTest | PASS，1/1 |
| Release CTest | PASS，1/1 |
| `--help` | PASS，退出码 0 |
| `--version` | PASS，退出码 0 |
| 未知参数 | PASS，退出码 2 |
| 无参数启动与日志写入 | PASS |
| Debug PE | PASS，x64 / Windows CUI |
| Release PE | PASS，x64 / Windows GUI |
| Phase 0 任务栏 API 安全扫描 | PASS，未发现任务栏修改 API |

Phase 0 不涉及任务栏视觉变化，因此不需要用户手动验收。
