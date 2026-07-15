# ChromeTaskbarMerger

ChromeTaskbarMerger 是一个 Windows 原生便携工具：当多个 Chrome 主窗口同时存在时，让 Windows 任务栏只保留一个固定 Chrome 入口。

## 当前状态

- Phase 0～Phase 4 已完成自动与真实任务栏验收；
- Phase 5、Phase 6 的实现和自动验收已完成；
- 当前产物为 `1.0.0-rc1`，等待 Phase 5/6 合并人工验收后再标记为 V1 发布候选。

当前版本提供托盘常驻、可配置扫描间隔、单实例、Explorer 重建处理、异常退出恢复记录、显式 `--restore-all`、日志和便携发布包。任务栏实现采用已经过真实验证的 `ITaskbarList::DeleteTab/AddTab` 方法，不关闭 Chrome，也不修改 Chrome 配置。

完整开发计划和验收门槛见 [CODEX_TASK_Chrome_Taskbar_Merger_CPP.md](CODEX_TASK_Chrome_Taskbar_Merger_CPP.md)，逐项证据见 [tests/manual_test_plan.md](tests/manual_test_plan.md)。

## 直接使用便携版

本地已生成：

```text
dist\ChromeTaskbarMerger\
dist\ChromeTaskbarMerger-1.0.0-rc1-portable-x64.zip
```

便携目录包含：

```text
ChromeTaskbarMerger.exe
ChromeTaskbarMerger.ini
README.md
LICENSE
```

使用前请先启动 WindowTabs，并确保 Chrome 主窗口可通过 WindowTabs 到达和操作。双击 `ChromeTaskbarMerger.exe` 后程序没有普通主窗口或控制台，而是驻留在通知区域。再次启动 EXE 不会创建第二套管理逻辑，只会通知已有实例立即扫描。

右键托盘图标可以立即扫描、暂停管理、恢复管理、恢复全部 Chrome 按钮、打开日志目录、查看“关于”和正常退出。“关于”窗口显示版本、开发人员和可点击的项目地址。正常退出会先恢复本程序移除的按钮；恢复失败时程序会拒绝退出并保留恢复记录。

已知兼容性影响：被合并的 Chrome 窗口可能不会出现在 Alt+Tab 中。这不阻塞当前 V1 目标，因为窗口仍可通过 WindowTabs 到达。WindowTabs 停止或无法检测时，程序会恢复已跟踪按钮并暂停管理。

## 配置

`ChromeTaskbarMerger.ini` 与 EXE 位于同一目录：

```ini
[ChromeTaskbarMerger]
scan_interval_ms=2000
```

允许范围为 500～60000 毫秒，重启程序后生效。配置文件缺失、不可读或数值无效时，程序使用 2000 毫秒默认值并记录警告。

## 日志与异常恢复

运行数据保存在当前用户的本地应用数据目录，不写入便携目录：

```text
%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log
%LOCALAPPDATA%\ChromeTaskbarMerger\recovery-v1.tsv
```

每次调用 `DeleteTab` 前，程序会先原子写入恢复记录。恢复时同时校验 HWND、PID、TID、进程创建时间和窗口类，避免把陈旧记录应用到复用同一 HWND 的其他窗口。

如果程序被强制结束，重新启动 EXE；程序会先恢复仍能精确识别的上次状态，再重新应用单入口规则。也可以从托盘选择“恢复全部 Chrome 按钮”，或在 PowerShell 中等待恢复命令结束并读取退出码：

```powershell
$process = Start-Process `
    -FilePath .\ChromeTaskbarMerger.exe `
    -ArgumentList '--restore-all' `
    -Wait -PassThru
$process.ExitCode
```

如果仍未恢复，请保留日志，在任务管理器中重启“Windows 资源管理器”，再执行一次恢复命令。不要在仍有按钮被移除时手工删除恢复记录。

## 命令行诊断

```text
ChromeTaskbarMerger.exe --help
ChromeTaskbarMerger.exe --version
ChromeTaskbarMerger.exe --list
ChromeTaskbarMerger.exe --experiment
ChromeTaskbarMerger.exe --manage
ChromeTaskbarMerger.exe --restore-all
```

- `--list` 只读枚举并解释 Chrome 窗口的接受/排除原因；
- `--experiment` 保留 Phase 2 的交互式 A/B 诊断路径；
- `--manage` 是 Debug/诊断用控制台生命周期监控；
- `--restore-all` 会恢复有效记录和当前可识别 Chrome 的任务栏注册；若托盘实例正在运行，则命令通知该实例完成恢复并保持暂停；
- 未知参数返回退出码 `2`。

Release 是 Windows GUI 子系统程序。需要可靠等待命令完成或读取退出码时，请使用上面的 `Start-Process -Wait -PassThru` 形式。

## 构建与测试

要求 Windows 11 x64、CMake 3.24 或更高版本，以及带“使用 C++ 的桌面开发”组件的 Visual Studio 2022 或更高版本。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
cmake --build build --config Release
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

生成经过全新 Debug/Release 构建和测试的便携包：

```powershell
.\scripts\build-portable.ps1
```

脚本会清理仓库内的专用 `build-portable` 和 `dist` 输出，分别执行 Debug/Release 构建与 CTest，再安装四个交付文件并创建 ZIP。MSVC 运行库静态链接，因此便携包不依赖额外的 VCRUNTIME/MSVCP DLL。

专用图标的矢量源稿和已生成的多尺寸 ICO 位于 `assets/`。正常构建直接使用已提交的 ICO；修改 SVG 后，可在安装了 Chrome 和 FFmpeg 的开发环境中执行 `.\scripts\build-icon.ps1` 重新生成 16～256 像素资源。

当前自动结果：Debug/Release 均无编译警告，CTest 均为 4/4。测试覆盖命令行解析、窗口身份、固定入口生命周期、恢复幂等性、写前日志失败保护、损坏/截断日志整体拒绝、PID/创建时间不匹配保护、TaskbarCreated 状态重建、单实例互斥体、同步/异步实例通知、配置解析和扫描调度。

## 已完成的真实任务栏验证

2026-07-15 使用 Windows 11 Pro 10.0.26200、Chrome 150.0.7871.115 和 WindowTabs `ss_2026.07.14` 完成 Phase 1～4 验收：

- Chrome 窗口识别与用户实际打开的三个主窗口一致；
- 方法 A `DeleteTab/AddTab` 能移除并恢复按钮，Chrome 和 WindowTabs 仍可操作；方法 B 因窗口可达性较差被淘汰；
- 1、3、5 个 Chrome 窗口均能固定保留一个入口；
- 新建、关闭非主窗口、关闭主入口、全部关闭后重开均能自动收敛；
- 暂停、恢复管理和正常退出行为正确，空闲 CPU 正常，日志无恢复遗留。

Phase 5/6 中 Explorer 重启、强制结束后的持久恢复、托盘菜单和 Release 便携体验仍需要真实桌面人工确认。

## 开发人员与项目地址

- 开发人员：杨云召
- GitHub：[yangyunzhao/ChromeTaskbarMerger](https://github.com/yangyunzhao/ChromeTaskbarMerger)
- 许可证：MIT

## 正常卸载

1. 从托盘菜单选择“退出”，确认所有 Chrome 按钮恢复；
2. 删除便携目录；
3. 确认不再需要日志后，可删除 `%LOCALAPPDATA%\ChromeTaskbarMerger`。

`dist/` 是可重复生成的本地交付目录，已在 `.gitignore` 中忽略；未来推送 remote 时提交源代码、配置模板和打包脚本即可，ZIP 可作为发布附件上传。
