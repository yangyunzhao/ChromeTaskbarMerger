# ChromeTaskbarMerger 1.0.0-rc1 portable x64

## 使用前提

- Windows 11 x64；
- WindowTabs 正在运行；
- Chrome 主窗口可由 WindowTabs 到达和操作。

本程序使用 `ITaskbarList::DeleteTab/AddTab`，只改变 Windows 任务栏注册状态，不关闭或修改 Chrome 配置文件。被合并的窗口可能不会出现在 Alt+Tab 中，这是当前版本的已知兼容性代价。

## 启动

双击 `ChromeTaskbarMerger.exe`。程序没有普通主窗口或控制台，会出现在系统托盘（通知区域）。再次双击 EXE 不会启动第二套管理逻辑，只会通知现有实例立即扫描。

右键托盘图标可以：

- 立即重新扫描；
- 暂停管理；
- 恢复管理；
- 恢复全部 Chrome 按钮；
- 打开日志目录；
- 查看“关于”、版本、开发人员和项目地址；
- 恢复本次修改并退出。

## 配置

`ChromeTaskbarMerger.ini` 必须与 EXE 放在同一目录。可修改：

```ini
[ChromeTaskbarMerger]
scan_interval_ms=2000
```

允许范围为 500～60000 毫秒，修改后重启程序生效。缺失或无效配置会安全回退到 2000 毫秒，并写入警告日志。

## 日志和崩溃恢复

运行数据不会写入便携目录，而是保存在：

```text
%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log
%LOCALAPPDATA%\ChromeTaskbarMerger\recovery-v1.tsv
```

每次移除按钮前，程序都会先原子写入恢复记录。恢复时会核对 HWND、PID、TID、进程创建时间和窗口类，拒绝把陈旧记录用于其他窗口。

如果程序被强制结束，重新启动 EXE；它会先恢复仍能精确识别的上次状态，再继续管理。

## 手动恢复

首选托盘菜单“恢复全部 Chrome 按钮”。也可在 PowerShell 中等待命令完成并读取退出码：

```powershell
$process = Start-Process `
    -FilePath .\ChromeTaskbarMerger.exe `
    -ArgumentList '--restore-all' `
    -Wait -PassThru
$process.ExitCode
```

如果按钮仍未恢复，请在任务管理器中重启“Windows 资源管理器”，然后再次执行恢复命令并保留日志。

## 开发人员与项目地址

- 开发人员：杨云召
- GitHub：[yangyunzhao/ChromeTaskbarMerger](https://github.com/yangyunzhao/ChromeTaskbarMerger)
- 许可证：MIT

## 正常退出和卸载

1. 使用托盘菜单“退出”，确认 Chrome 按钮全部恢复；
2. 删除整个便携目录即可卸载；
3. 确认不再需要日志后，可删除 `%LOCALAPPDATA%\ChromeTaskbarMerger`。

不要在仍有按钮被隐藏时直接删除恢复日志。
