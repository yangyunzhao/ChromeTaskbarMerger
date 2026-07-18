# ChromeTaskbarMerger 3.1.0 便携版（x64）

## 使用前提

- Windows 11 x64；
- 默认内置标签模式不需要 WindowTabs；
- 也可选择 WindowTabs 标签模式，保留其标签风格。

本程序使用 `ITaskbarList::DeleteTab/AddTab`，只改变 Windows 任务栏注册状态，不关闭或修改 Chrome 配置文件。被合并的窗口可能不会出现在 Alt+Tab 中，这是当前版本的已知兼容性代价。

3.1.0 在普通窗口下只显示紧贴 Chrome 上边缘的圆角标签形状；Windows 最大化后，Chrome
保持真实原生最大化，标签进入顶部靠右区域并避让最小化、还原和关闭按钮。新配置默认右对齐；已有
`left` 或 `center` 配置继续生效。F11 真正全屏不在本版本支持范围内；Chrome 原生网页标签很多时
允许被内置窗口标签局部覆盖。

## 启动

双击 `ChromeTaskbarMerger.exe`。程序没有普通主窗口或控制台，会出现在系统托盘（通知区域）。再次双击 EXE 不会启动第二套管理逻辑，只会通知现有实例立即扫描。

右键托盘图标可以：

- 立即重新扫描；
- 暂停管理；
- 恢复管理；
- 恢复全部 Chrome 按钮；
- 在“内置标签”和“WindowTabs 标签”之间二选一（重启生效）；
- 配置“随 Windows 登录自动启动”；
- 打开日志目录；
- 查看“关于”、版本、开发人员和项目地址；
- 恢复本次修改并退出。

## 配置

`ChromeTaskbarMerger.ini` 必须与 EXE 放在同一目录。可修改：

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

`tab_provider` 只接受 `builtin` 或 `windowtabs`。`builtin` 是默认值，使用本程序自研标签和窗口组；`windowtabs` 不创建内置标签或重排 Chrome，由 WindowTabs 提供标签，本程序只负责任务栏单入口。托盘选择或直接编辑后，都必须完全退出并重新启动才能切换模式。

仅在 `builtin` 模式下，可以双击内置标签主体输入名称，支持中文及其他 Unicode 字符；`Enter`
保存、`Esc` 取消，清空后确认可恢复 Chrome 实时标题。默认
`persist_tab_names_by_profile=false`，名称只在当前 ChromeTaskbarMerger 进程中有效；设为 `true`
并完全重启后，程序只在能够唯一验证普通本地 Chrome profile 时保存并恢复名称。同一 profile
的多个窗口共享名称；歧义、无痕/访客、识别失败或读写失败时安全回退为内存名称。
`windowtabs` 模式没有内置标签，也不提供、加载或保存此名称编辑功能。托盘中的 profile 名称
持久化开关只保存配置，必须完全重启后生效。

内置标签发生溢出时可在标签栏上滚动鼠标滚轮浏览；管理期间可用
`Ctrl+Alt+PageUp/PageDown` 循环切换前后标签，暂停管理后本程序会注销这组快捷键。
内置组最多管理 5 个 Chrome 主窗口；运行中出现第 6 个时，原有组保持不变，额外窗口保留独立
任务栏入口并显示通知。若程序启动前已经有 6 个或更多窗口，会安全暂停并要求先关闭多余窗口。

两个时间间隔均允许 500～60000 毫秒：`scan_interval_ms` 控制 Chrome 扫描，`windowtabs_check_interval_ms` 仅在选择 WindowTabs 时控制等待检测。内置标签栏支持 `left/center/right` 对齐、25～100 的总宽度百分比以及 80～400 逻辑像素的单标签最大宽度。非法值会回退安全默认值并记录警告。

`start_with_windows` 接受 `true` 或 `false`，默认关闭。托盘修改会原子保存并同步当前用户 Run 项；直接编辑文件后必须从托盘彻底退出并重新运行。手工改为 `true` 后需要手动重新运行一次，程序才能创建当前用户的 Windows 启动项。

内置模式完全不等待 WindowTabs；如果检测到 WindowTabs 同时运行，会先恢复任务栏和窗口布局，再进入冲突暂停，退出 WindowTabs 后自动重新准备。WindowTabs 模式在其尚未运行时保持所有任务栏入口可见并持续低频等待，就绪后自动进入管理；运行期间 WindowTabs 退出时先恢复按钮，再等待其重启。只有用户主动选择“暂停管理”后才会保持暂停。移动便携目录后，需要从新位置手动运行一次以修复启动路径。

## 日志和崩溃恢复

运行数据不会写入便携目录，而是保存在：

```text
%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log
%LOCALAPPDATA%\ChromeTaskbarMerger\recovery-v1.tsv
%LOCALAPPDATA%\ChromeTaskbarMerger\recovery-v2.tsv
%LOCALAPPDATA%\ChromeTaskbarMerger\tab-names-v1.tsv
%LOCALAPPDATA%\ChromeTaskbarMerger\profile-tab-names-v1.tsv
```

每次移除按钮或修改内置组布局前，程序都会先原子写入恢复记录。恢复时会核对 HWND、PID、TID、进程创建时间、窗口类和原显示环境，拒绝把陈旧记录用于其他窗口。`tab-names-v1.tsv` 仅是 Phase 5 内部技术原型，不参与交互式名称。启用 profile 名称持久化后，`profile-tab-names-v1.tsv` 只保存 SHA-256 profile 键和用户输入名称，不保存 Chrome profile 名称、账户、邮箱、路径、网页标题或 HWND；文件损坏时整份拒绝并回退内存名称。

如果程序被强制结束，重新启动 EXE；它会先恢复仍能精确识别的上次状态，再继续管理。

## 手动恢复

首选托盘菜单“恢复全部任务栏和窗口布局”。也可在 PowerShell 中等待命令完成并读取退出码：

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

1. 取消勾选托盘菜单“随 Windows 登录自动启动”；
2. 使用托盘菜单“退出”，确认 Chrome 按钮全部恢复；
3. 删除整个便携目录即可卸载；
4. 确认不再需要日志后，可删除 `%LOCALAPPDATA%\ChromeTaskbarMerger`。

不要在仍有按钮被隐藏时直接删除恢复日志。
