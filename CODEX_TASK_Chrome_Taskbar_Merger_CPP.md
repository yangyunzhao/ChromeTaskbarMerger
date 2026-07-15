# Codex 开发任务：Chrome 多配置文件窗口的任务栏单入口工具（C++）

> 文档状态：开发计划已确认。Phase 0 和 Phase 1 已于 2026-07-15 完成验收；Phase 2 尚未开始。
>
> V1 范围修订（2026-07-15）：第一版以“任务栏只保留一个 Chrome 入口”为核心目标。任务栏入口不要求跟随当前活动 Chrome；WindowTabs 和 Alt+Tab 的完整兼容性降为尽力而为，但 Chrome 窗口必须保持打开、可到达和可操作，且所有修改必须能够恢复。

## 1. 任务背景

用户在 Windows 11 上同时运行多个 Chrome 配置文件。每个配置文件对应一个独立的 Chrome 顶层窗口。

目前已使用 WindowTabs 将这些 Chrome 窗口组合为顶部标签页，窗口切换已经正常，但 Windows 任务栏仍然显示多个独立的 Chrome 按钮。

需要开发一个轻量级 C++ 后台程序，在不关闭 Chrome、不改变 Chrome 配置文件的前提下，让任务栏中只保留一个 Chrome 入口。V1 尽量保持 WindowTabs 和 Alt+Tab 体验，但完整兼容不作为发布硬门槛。

该工具的核心思路不是修改 Chrome 本身，而是动态管理多个 Chrome 顶层窗口在 Windows 任务栏中的显示状态。

---

## 2. 项目名称建议

项目目录名：

```text
ChromeTaskbarMerger
```

可执行文件名：

```text
ChromeTaskbarMerger.exe
```

建议初始文件结构：

```text
ChromeTaskbarMerger/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── src/
│   ├── main.cpp
│   ├── app.h
│   ├── app.cpp
│   ├── chrome_window.h
│   ├── chrome_window.cpp
│   ├── taskbar_controller.h
│   ├── taskbar_controller.cpp
│   ├── win_event_monitor.h
│   ├── win_event_monitor.cpp
│   ├── tray_icon.h
│   ├── tray_icon.cpp
│   ├── logger.h
│   └── logger.cpp
└── tests/
    └── manual_test_plan.md
```

Phase 0～Phase 2 可以适当减少文件数量，但不要把全部逻辑长期堆积在 `main.cpp` 中。

---

## 3. 开发目标与 V1 范围

### 3.1 V1 核心目标

开发一个轻量级 Windows 原生程序，完成以下功能：

1. 自动发现当前系统中的 Chrome 顶层主窗口；
2. 识别真正可见、可交互、通常具有任务栏按钮的 Chrome 主窗口；
3. 当存在一个或多个可管理 Chrome 窗口时，任务栏中只保留一个 Chrome 入口；
4. V1 使用“固定主入口”：启动时选定一个主窗口，在其仍有效时不因前台窗口切换而更换任务栏入口；
5. Chrome 新建、关闭或重建窗口后，在允许的同步延迟内重新满足“只保留一个入口”；
6. 程序退出、暂停或执行恢复命令时，恢复由本程序修改过的窗口；
7. 提供清楚的日志、手动重新扫描和强制恢复能力；
8. 后续阶段再加入托盘、Explorer 重启恢复和更完善的生命周期处理。

### 3.2 V1 硬性要求

- 不关闭、最小化或隐藏 Chrome 窗口来实现任务栏效果；
- Chrome 窗口必须保持可到达和可操作；
- 不修改 Chrome 用户数据，不注入 Chrome、WindowTabs 或 Explorer；
- 至少一种方案能够在目标 Windows 11 环境中真实移除并恢复任务栏按钮；
- 正常退出必须恢复本程序实际修改过的窗口；
- API 返回成功不等于验收成功，任务栏视觉结果必须经过人工确认；
- 如果核心任务栏方案实测不可行，必须停止后续功能开发并记录结论。

### 3.3 V1 尽力而为但不阻塞发布的要求

- WindowTabs 标签切换完全无闪烁；
- Alt+Tab 仍列出所有独立 Chrome 窗口；
- 任务栏入口始终对应当前活动的 Chrome 窗口；
- Chrome 窗口变化后立即响应；
- Explorer 重启期间完全无状态抖动。

上述项目需要记录实际表现。如果某种方案导致其他 Chrome 窗口无法通过 WindowTabs、Alt+Tab、鼠标或其他正常方式到达和操作，则不满足“窗口保持可用”的硬性要求，不能采用。

---

## 4. 重要说明

这个工具并不是真正把多个 Chrome 窗口合并为一个窗口，也不修改 Chrome 的进程结构。

它只负责控制多个 Chrome 顶层窗口是否显示在 Windows 任务栏中。

预期效果：

```text
WindowTabs 顶部：不主动修改其分组；实际兼容性以测试记录为准
Windows 任务栏：只显示选定的固定 Chrome 主入口
```

V1 优先级从高到低为：

1. 任务栏只保留一个 Chrome 入口；
2. 不关闭或隐藏 Chrome 窗口，不修改用户数据；
3. 程序能够可靠恢复自身修改；
4. 其他 Chrome 窗口仍可到达和操作；
5. 尽量减少对 WindowTabs 和 Alt+Tab 的影响。

---

## 5. 分阶段开发与验收计划

不要一开始实现完整功能。必须按 Phase 0 到 Phase 6 顺序推进；Phase 7 属于 V1 之后的可选增强。前一 Phase 未通过时，不进入后一 Phase。

### 5.1 通用验收规则

每个 Phase 都按以下顺序验收：

1. Codex 先执行能够自动完成的构建、测试、静态检查和命令行验证；
2. Codex 在阶段报告中列出实际执行的命令、退出码和关键结果；
3. 只有 Windows 任务栏视觉变化、WindowTabs 行为等无法可靠自动观察的项目才请求用户手动验收；
4. 请求人工验收时，Codex 必须给出逐步操作、预期现象、安全恢复方法和回报模板；
5. 验收结果只能标记为“通过”“失败”或“阻塞”，不得把 HRESULT 成功等同于视觉效果通过；
6. 任一修改任务栏的测试都必须先准备恢复路径，并在测试结束后执行恢复；
7. 每个 Phase 完成后同步更新 `README.md` 和 `tests/manual_test_plan.md` 中的实际结果。

如果当前 Codex 运行环境缺少 MSVC、Chrome、WindowTabs 或交互式桌面，Codex 应先完成所有仍可自动完成的工作，再把剩余项目作为明确的人工验收项交给用户，而不是推测结果。

### Phase 0：工程骨架和可重复构建

目标：建立最小、可重复、尚不修改任何窗口的 CMake 工程。

实现内容：

- 创建 CMake C++20 x64 工程；
- 添加命令行入口、`--help`、版本输出和基础日志；
- 建立测试目标和 `tests/manual_test_plan.md`；
- Release 使用 Windows 子系统，Debug 允许控制台输出；
- 此阶段禁止调用任何修改任务栏或窗口样式的 API。

Codex 自动验收：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
cmake --build build --config Release
ctest --test-dir build -C Debug --output-on-failure
```

并验证 Debug 可执行文件执行 `--help` 后退出码为 `0`，未知参数返回非零退出码且给出说明。

人工验收：通常不需要。如果 Codex 环境缺少 Visual Studio 构建工具，则由 Codex 提供一条可复制的构建命令，并请用户回传完整失败或成功输出。

通过标准：Debug 和 Release 均能构建，测试通过，程序尚未改变任何窗口状态。

### Phase 1：Chrome 主窗口枚举与诊断

目标：可靠列出候选 Chrome 主窗口，但仍不修改任务栏。

实现内容：

- 实现集中式 `IsManageableChromeWindow(HWND)` 判断；
- 增加 `--list` 诊断命令；
- 输出 HWND、PID、进程路径、标题、类名、可见性、窗口样式、扩展样式以及接受或排除原因；
- 将可测试的识别规则与 Win32 枚举操作分离，以便使用合成输入做单元测试；
- 正确处理权限不足、进程在枚举中退出和无 Chrome 窗口等情况。

Codex 自动验收：

- 构建和 CTest 全部通过；
- 识别规则测试覆盖 `chrome.exe` 大小写、非 Chrome 进程、不可见窗口、空标题、工具窗口和典型 `Chrome_WidgetWin_*` 类名；
- `--list` 在零个 Chrome 窗口时正常退出且不误报错误；
- 日志包含每个候选窗口的接受或排除原因；
- 重复执行 `--list` 不崩溃、不修改窗口样式和任务栏状态。

人工验收步骤：

1. 分别打开三个 Chrome 配置文件窗口；
2. 运行 Codex 提供的 `ChromeTaskbarMerger.exe --list` 命令；
3. 将输出中的可管理窗口与肉眼可见的三个 Chrome 主窗口对照；
4. 告知 Codex 是否存在漏报或把非主窗口误报为主窗口，并附上对应输出行。

通过标准：自动测试全部通过；在真实 Chrome 环境中，所有计划管理的主窗口均被列出，且没有明显辅助窗口被纳入。真实窗口对照无法由 Codex 完成时，本 Phase 等待用户确认。

### Phase 2：任务栏 API 最小可行性实验（硬门槛）

目标：确认 Windows 11 上是否存在一种能够真实移除和恢复指定 Chrome 窗口任务栏按钮的方法。

实现一个最小交互式实验模式：

1. 枚举并打印 Chrome 主窗口；
2. 用户通过编号选择一个非唯一窗口；
3. 用户选择实验方法；
4. 执行“从任务栏移除”；
5. 等待用户观察并确认；
6. 执行“恢复到任务栏”；
7. 输出 API 返回值、HRESULT、`GetLastError`、修改前后样式和恢复结果；
8. 即使用户取消或正常退出实验，也要尝试恢复本次修改。

必须分别验证以下方法：

#### 方法 A：`ITaskbarList::DeleteTab` / `AddTab`

```cpp
ITaskbarList::DeleteTab(HWND hwnd);
ITaskbarList::AddTab(HWND hwnd);
```

#### 方法 B：窗口扩展样式

评估以下组合，并准确保存和恢复原始扩展样式：

```cpp
WS_EX_APPWINDOW
WS_EX_TOOLWINDOW
```

修改后调用：

```cpp
SetWindowPos(
    hwnd,
    nullptr,
    0,
    0,
    0,
    0,
    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
    SWP_NOACTIVATE | SWP_FRAMECHANGED
);
```

方法 B 允许影响 Alt+Tab，因此在修订后的 V1 中可以成为正式候选方案；但如果它导致 Chrome 窗口被隐藏、关闭或无法通过任何正常方式到达，则仍然失败。

Codex 自动验收：

- 两种实验路径均可执行并完整记录 API 结果；
- COM 初始化、`HrInit`、`DeleteTab`、`AddTab` 的 HRESULT 均被检查和记录；
- 方法 B 精确保存原始 `GWL_EXSTYLE`，恢复后数值与修改前一致；
- 无效 HWND、窗口在操作前关闭、用户取消等路径不会崩溃；
- 自动测试确认“只恢复本次实际修改的窗口”和重复恢复的幂等性；
- 实验程序正常退出后不留下它已知但尚未恢复的状态。

人工验收不可省略，因为程序无法可靠读取 Windows 11 任务栏最终绘制结果。Codex 届时必须指导用户按以下步骤操作：

1. 打开至少三个独立 Chrome 主窗口，并记下当前任务栏按钮数量；
2. 运行实验模式，选择一个非主入口窗口和方法 A；
3. 确认目标任务栏按钮是否消失，同时确认目标 Chrome 窗口仍然打开且可操作；
4. 简单尝试 WindowTabs 和 Alt+Tab，记录影响，但轻微闪烁或 Alt+Tab 缺项不阻塞 V1；
5. 触发恢复，确认按钮无需重启 Explorer 即重新出现；
6. 对方法 B 重复以上步骤；
7. 如任一步骤异常，立即使用程序提供的恢复命令；仍未恢复时，重启 Explorer，并把日志交给 Codex。

用户回报模板：

```text
Windows 版本：
Chrome 版本：
WindowTabs 版本（未使用则填无）：
方法 A：移除成功/失败；恢复成功/失败；窗口仍可用：是/否
方法 B：移除成功/失败；恢复成功/失败；窗口仍可用：是/否
Alt+Tab 影响：
WindowTabs 影响：
是否闪烁：
是否被 Chrome 或 Explorer 自动恢复：
其他现象：
```

通过标准：至少一种方法在实际任务栏上能够让指定按钮消失并恢复，且 Chrome 窗口保持打开、可到达和可操作。若两种方法均失败，项目在本 Phase 停止，记录实验结论，不继续堆积托盘或监控代码。

### Phase 3：固定主入口 MVP

目标：把已验证的方法扩展为“多个 Chrome 窗口、任务栏固定只保留一个入口”的最小常驻版本。

实现内容：

- 启动时若前台窗口是可管理 Chrome，则选它为固定主入口；否则按稳定、可解释的顺序选择一个；
- 主入口有效期间，不因 WindowTabs、Alt+Tab 或鼠标切换前台窗口而更换入口；
- 移除其他可管理 Chrome 窗口的任务栏按钮；
- 同步操作必须幂等，状态未变化时不得重复调用任务栏 API；
- 提供立即重新扫描、恢复全部和正常退出；
- 所有任务栏修改由同一线程串行执行；
- 此阶段可以先使用带控制台的常驻程序，不要求托盘图标。

Codex 自动验收：

- 使用伪窗口清单和伪任务栏控制器测试 0、1、3、5 个窗口；
- 测试固定主入口选择、重复同步、非 Chrome 前台窗口、主入口仍有效时不切换以及恢复全部；
- 测试只恢复本程序记录为已修改的窗口；
- 测试方法 B 修改后，即使窗口当前带有 `WS_EX_TOOLWINDOW`，也不会被后续扫描从本程序的已修改状态中遗忘；
- 测试窗口在同步过程中失效和 HWND 身份不匹配时安全跳过；
- 构建、CTest、`--help` 和诊断命令全部通过。

人工验收步骤：

1. 依次用 1、3、5 个 Chrome 主窗口启动程序；
2. 每次确认存在 Chrome 时任务栏恰好只保留一个 Chrome 入口；
3. 使用 WindowTabs、Alt+Tab 或鼠标访问其他 Chrome 窗口，确认它们仍打开且至少能通过一种正常方式到达和操作；
4. 连续切换窗口，确认任务栏入口不需要随当前窗口变化；
5. 执行“恢复全部”，确认所有按钮恢复；
6. 再次应用管理，然后正常退出，确认所有按钮再次恢复。

通过标准：任务栏在多个可管理 Chrome 窗口存在时只显示一个 Chrome 入口；窗口均保持可用；恢复全部和正常退出均恢复本程序修改。WindowTabs 闪烁、Alt+Tab 缺少部分窗口等现象记录为已知限制，但只要窗口仍可到达和操作，就不阻塞 V1。

### Phase 4：窗口生命周期自动维护

目标：在 Chrome 新建、关闭和重建窗口时自动维持单入口状态。

V1 优先采用简单、可靠的低频安全扫描；默认间隔建议为 2 秒，不要求先实现前台窗口事件跟随。后续如有必要，再用 `SetWinEventHook` 降低延迟。

实现内容：

- 新建 Chrome 窗口后自动纳入管理；
- 非主入口关闭后清理状态；
- 固定主入口关闭后，从仍有效的窗口中选择新主入口；
- 所有 Chrome 关闭后不做任务栏操作；重新打开后重新建立单入口；
- 对扫描和重复事件做去抖及幂等处理；
- 暂停时恢复本程序移除的按钮，恢复管理时重新扫描并应用规则。

Codex 自动验收：

- 策略测试覆盖新建窗口、关闭非主入口、关闭主入口、全部关闭、重新打开和句柄复用；
- 使用可控时钟测试扫描间隔和去抖，不依赖真实等待；
- 空状态和重复扫描不产生重复任务栏调用；
- Debug 与 Release 构建、CTest 全部通过。

人工验收步骤：

1. 程序运行时新建两个 Chrome 窗口，确认每次在最多一个扫描周期后仍只显示一个入口；
2. 分别关闭非主入口和主入口，确认剩余 Chrome 最终仍只有一个任务栏入口；
3. 关闭所有 Chrome，再重新打开三个窗口，确认规则重新生效；
4. 测试暂停与恢复管理；
5. 观察任务管理器中的空闲 CPU，记录是否存在持续异常占用。

通过标准：上述生命周期场景均能在规定同步延迟内恢复到零个或一个 Chrome 任务栏入口，且无明显忙循环、崩溃或无法恢复状态。

### Phase 5：恢复、Explorer 重建和单实例

目标：提高长期运行和故障后的可恢复性。

实现内容：

- 使用 `Local\ChromeTaskbarMerger.Singleton` 保证单实例；
- 第二实例只通知第一实例重新扫描后退出；
- 处理 `TaskbarCreated`，重新初始化任务栏对象、重新扫描并应用规则；
- 增加 `--restore-all`；
- 设计最小恢复日志，记录方法、HWND、PID、进程创建时间、类名及原始扩展样式；
- 启动恢复时必须验证窗口身份，不能仅凭可能复用的 HWND 修改窗口；
- 明确区分“恢复本程序有记录的修改”和用户显式请求的强制恢复。

Codex 自动验收：

- 单实例、通知消息和恢复日志序列化测试通过；
- 测试 PID/创建时间/类名不匹配时拒绝套用陈旧状态；
- 测试恢复操作幂等，损坏或截断的恢复日志不会导致盲目修改窗口；
- 模拟 `TaskbarCreated` 后会重建控制器并重新执行策略；
- 所有构建与 CTest 通过。

人工验收步骤：

1. 启动第二实例，确认没有出现两套管理逻辑，第一实例执行一次重新扫描；
2. 在任务管理器中重启 Windows Explorer，确认任务栏和后续单入口规则恢复；
3. 使用任务管理器强制结束程序，再重新启动，按 Codex 指引验证恢复日志；
4. 执行 `--restore-all`，确认 Chrome 任务栏按钮恢复；
5. 如果自动恢复失败，保留日志并重启 Explorer，避免继续重复实验。

通过标准：正常退出、第二实例、Explorer 重启和恢复命令均行为明确；崩溃恢复至少不会依据陈旧 HWND 误改无关窗口。强制结束后的任务栏视觉恢复仍需用户确认。

### Phase 6：托盘、文档和 V1 发布候选

目标：把已通过验证的核心功能包装为可长期使用的 V1。

实现内容：

- 系统托盘图标和状态提示；
- 立即重新扫描、暂停管理、恢复管理、恢复全部、查看日志目录和退出；
- Explorer 重启后重新注册托盘图标；
- Release 不显示控制台窗口；
- 完成 README、手工测试计划、构建脚本、许可证和 `.gitignore`；
- README 明确记录实际采用的方法、测试环境、兼容性影响和恢复步骤。

Codex 自动验收：

- 全新构建目录中的 Debug/Release 构建和 CTest 全部通过；
- 校验交付文件齐全；
- `--help`、`--list`、`--restore-all` 和未知参数行为正确；
- 日志目录创建失败等错误路径不会崩溃；
- Release 产物路径、文件名和架构符合要求。

人工验收步骤：

1. 启动 Release，确认没有普通主窗口或控制台，托盘图标存在；
2. 逐项点击托盘菜单并观察状态；
3. 用三个 Chrome 窗口完成一次管理、暂停、恢复和退出循环；
4. 重启 Explorer，确认托盘图标重新出现；
5. 按 README 的恢复和卸载步骤操作一次，确认无需重启电脑即可恢复任务栏。

通过标准：Phase 0 至 Phase 5 的门槛保持通过，托盘操作和文档步骤与实际行为一致，Release 可直接运行。完成后才称为 V1 发布候选。

### Phase 7：V1 之后的可选增强

以下功能不阻塞 V1：

- 使用 `SetWinEventHook` 代替或辅助低频扫描；
- 让任务栏入口跟随当前活动 Chrome；
- 优化 WindowTabs 切换闪烁和 Alt+Tab 兼容性；
- 开机启动；
- 指定管理某些 Chrome 配置文件；
- 忽略隐身或应用模式窗口；
- 配置日志等级和扫描间隔；
- 管理 Edge 等其他 Chromium 浏览器。

每一项增强都必须单独定义验收标准，不能破坏已经通过的单入口和恢复能力。

---

## 6. Chrome 窗口识别要求

不能只根据窗口标题包含 `Google Chrome` 判断。

建议按以下顺序识别：

1. 使用 `GetWindowThreadProcessId` 获取进程 ID；
2. 使用 `OpenProcess` 和 `QueryFullProcessImageNameW` 获取进程路径；
3. 判断可执行文件名是否为 `chrome.exe`；
4. 确认窗口是顶层窗口；
5. 排除不可见窗口；
6. 排除无标题、辅助、消息、工具、拖拽、隐藏渲染等非主窗口；
7. 检查窗口类名；
8. 记录窗口句柄、进程 ID、标题、类名、可见性、样式和扩展样式。

Chrome 主窗口通常可能使用类似以下类名：

```text
Chrome_WidgetWin_1
```

但不要把类名写死为唯一判断条件，应保留兼容空间。

建议实现统一函数：

```cpp
bool IsManageableChromeWindow(HWND hwnd);
```

该函数必须集中管理窗口识别逻辑，避免散落在多个文件中。

初次发现规则和“继续跟踪本程序已修改窗口”的规则必须区分。方法 B 可能主动为窗口设置 `WS_EX_TOOLWINDOW`；后续扫描不能因为这个由本程序造成的样式变化就忘记该窗口。对于状态表中仍通过身份校验的窗口，应结合保存的原始元数据继续跟踪，直到恢复成功或窗口销毁。

---

## 7. 事件监听要求

V1 为降低复杂度，允许以 2～5 秒的低频安全扫描作为主要生命周期检测方式，默认建议 2 秒。固定主入口模式不需要监听每一次前台窗口变化。

`SetWinEventHook` 属于 Phase 7 的优先增强；如果实测低频扫描无法满足新建、关闭窗口的体验，或资源占用不理想，可以提前引入，但必须保持任务栏修改仍由主线程串行执行。

使用事件钩子时建议调用：

```cpp
SetWinEventHook
```

按实际需要监听：

```text
EVENT_SYSTEM_FOREGROUND
EVENT_OBJECT_CREATE
EVENT_OBJECT_DESTROY
EVENT_OBJECT_SHOW
EVENT_OBJECT_HIDE
```

V1 不要求监听 `EVENT_SYSTEM_FOREGROUND` 来动态更换任务栏主入口。若仅处理生命周期，优先关注创建、销毁、显示和隐藏事件。

必要时可以监听窗口名称变化，但不应因为标题变化频繁执行完整扫描。

建议使用：

```cpp
WINEVENT_OUTOFCONTEXT
WINEVENT_SKIPOWNPROCESS
```

事件回调中不要执行耗时工作。事件回调只负责投递消息或更新轻量状态，真正扫描和更新任务栏的操作应交给主消息循环处理。

必须避免：

- 在事件回调中长时间阻塞；
- 每次收到任意窗口事件就全量扫描；
- 高频重复调用任务栏 API；
- 多线程同时修改同一个窗口状态；
- 低于 1 秒的固定轮询；
- 为实现固定主入口而无意义地处理高频前台事件。

---

## 8. 状态管理要求

程序必须记录它实际修改过哪些窗口，不能在退出时盲目恢复所有 Chrome 窗口。

建议维护类似结构：

```cpp
struct ChromeWindowState {
    HWND hwnd = nullptr;
    DWORD process_id = 0;
    DWORD thread_id = 0;
    std::wstring title;
    std::wstring class_name;
    bool managed = false;
    bool removed_from_taskbar = false;
    bool was_visible = false;
    LONG_PTR original_ex_style = 0;
    LONG_PTR applied_ex_style = 0;
};
```

需要注意：

- `HWND` 可能失效；
- Windows 可能复用句柄；
- 每次操作前使用 `IsWindow` 验证；
- 不能只以 `HWND` 作为永久身份；
- 可以结合 PID、进程创建时间、类名等信息辅助判断；
- 必须记录采用的方法和足以验证恢复目标的原始元数据；
- 由本程序修改为工具窗口的 Chrome 仍须保留在状态表中；
- 窗口销毁后应立即清理状态。

---

## 9. 主入口选择规则

V1 默认采用固定主入口规则：

1. 程序首次应用管理规则时，如果当前前台窗口是受管理的 Chrome，则选择它作为主入口；
2. 否则按稳定、可记录的顺序选择一个仍然有效且可见的 Chrome 主窗口；
3. 主入口仍然有效时，不因 WindowTabs、Alt+Tab、鼠标或非 Chrome 程序获得焦点而更换；
4. 其他受管理 Chrome 窗口从任务栏移除；
5. 主入口关闭或不再可管理时，优先选择当前前台 Chrome，否则从剩余有效窗口中选择新的固定主入口；
6. 如果没有可管理的 Chrome 窗口，则不做任何操作并清理失效状态。

不得在非 Chrome 程序激活时把所有 Chrome 都从任务栏移除。让任务栏入口动态跟随当前活动 Chrome 属于 Phase 7 可选功能。

---

## 10. WindowTabs 兼容要求

WindowTabs 会为多个独立窗口提供顶部标签，但底层窗口仍然独立存在。

本程序在所有版本中必须做到：

- 不修改 WindowTabs 进程；
- 不向 WindowTabs 注入代码；
- 不依赖 WindowTabs 内部实现；
- 不修改 Chrome 窗口的位置和大小；
- 不隐藏非活动 Chrome 窗口；
- 只改变任务栏注册状态或任务栏相关窗口样式；
- WindowTabs 存在或退出时不崩溃，不试图控制其内部状态。

V1 对 WindowTabs 和 Alt+Tab 的完整兼容属于尽力而为。切换闪烁、Alt+Tab 缺少部分 Chrome 窗口、任务栏入口不跟随当前标签都可以作为已知限制记录，不阻塞 V1。

但如果某种实现隐藏或关闭 Chrome 窗口，或者导致用户无法通过 WindowTabs、Alt+Tab、鼠标等任何正常方式到达其他 Chrome 窗口，则窗口已不再可用，必须停止采用该实现。

---

## 11. Explorer 重启处理

Windows Explorer 重启后，任务栏会重新创建，之前的任务栏注册状态可能失效。

程序应监听 Explorer 或任务栏重建事件。

建议注册并处理：

```cpp
RegisterWindowMessageW(L"TaskbarCreated")
```

收到 `TaskbarCreated` 后：

1. 重新初始化任务栏相关 COM 对象；
2. 重新扫描 Chrome 窗口；
3. 重新应用任务栏显示规则；
4. 重新注册托盘图标。

---

## 12. COM 使用要求

如果使用 `ITaskbarList`：

1. 在主线程调用 `CoInitializeEx`；
2. 创建 `CLSID_TaskbarList`；
3. 调用 `HrInit`；
4. 程序退出时释放 COM 对象；
5. 调用 `CoUninitialize`；
6. 所有 HRESULT 必须检查；
7. 错误日志中打印 HRESULT 十六进制值。

示例接口：

```cpp
class TaskbarController {
public:
    bool Initialize();
    bool RemoveWindow(HWND hwnd);
    bool RestoreWindow(HWND hwnd);
    void RestoreAll();
    void Shutdown();
};
```

---

## 13. 托盘菜单要求

托盘右键菜单至少包含：

```text
状态：运行中 / 已暂停
立即重新扫描
暂停管理
恢复管理
恢复所有 Chrome 任务栏按钮
查看日志目录
退出
```

双击托盘图标可以执行“立即重新扫描”，或打开一个简洁状态窗口。

第一版不要求复杂 GUI。

---

## 14. 日志要求

程序需要输出本地日志，便于排查不同 Windows 版本的行为。

建议日志路径：

```text
%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log
```

日志至少包括：

- 程序启动和版本；
- Windows 版本；
- 枚举到的 Chrome 窗口；
- 窗口句柄、PID、标题、类名；
- 任务栏移除和恢复结果；
- 前台窗口变化（仅在启用相关事件监听时）；
- Chrome 窗口创建和销毁；
- Explorer 重启；
- COM 错误；
- 程序退出时恢复结果。

不要在默认日志中频繁输出无意义的重复事件。

可以设计日志等级：

```text
ERROR
WARN
INFO
DEBUG
TRACE
```

默认使用 `INFO`。

---

## 15. 异常与恢复要求

程序必须尽量保证退出后系统恢复原状。

### 正常退出

退出前：

1. 停止 WinEventHook（如已启用）；
2. 恢复所有由本程序移除的任务栏按钮；
3. 删除托盘图标；
4. 释放 COM；
5. 关闭日志。

### 异常退出

普通进程崩溃时无法保证执行清理代码，因此需要尽量降低遗留影响。

建议：

- 每次启动时先重新扫描所有 Chrome 窗口；
- 读取上一实例留下的最小恢复记录；
- 只有 HWND、PID、进程创建时间、类名等身份信息仍匹配时，才恢复对应窗口；
- 对方法 B 只有在原始扩展样式已被可靠记录时才能恢复，不得猜测原始样式；
- 完成安全恢复后，再重新应用当前规则；
- 提供独立命令行参数用于恢复当前实例和恢复记录中所有已知修改：

```text
ChromeTaskbarMerger.exe --restore-all
```

`--restore-all` 中的“全部”指本程序当前状态或有效恢复记录所知的全部修改。对于方法 A，可以对当前可管理 Chrome 做安全的 `AddTab` 尝试；对于方法 B，不得在没有原始样式记录时盲目改写扩展样式。

也可以支持：

```text
ChromeTaskbarMerger.exe --scan
ChromeTaskbarMerger.exe --debug
```

不要依赖未处理异常回调完成系统恢复，因为进程异常终止时无法保证回调可靠执行。

---

## 16. 单实例要求

程序只能运行一个实例。

建议使用命名互斥体：

```text
Local\ChromeTaskbarMerger.Singleton
```

如果检测到已有实例：

- 第二个实例向第一个实例发送“立即重新扫描”消息；
- 然后退出；
- 不应再启动第二套事件钩子。

---

## 17. 构建要求

目标平台：

```text
Windows 11 x64
```

语言标准：

```text
C++17 或 C++20
```

推荐：

```text
C++20
```

构建系统：

```text
CMake
```

推荐编译器：

```text
Visual Studio 2022
MSVC x64
```

要求：

- 生成原生 x64 可执行文件；
- 不依赖 Qt、Electron、.NET；
- 尽量只使用 Win32 API 和标准库；
- Release 版本不弹出控制台窗口；
- Debug 版本可以保留控制台或日志输出；
- 编译警告等级至少 `/W4`；
- 不使用 `/WX` 作为默认设置，避免环境差异造成构建失败；
- 代码使用 Unicode API；
- 不使用 ANSI Win32 API。

示例构建命令：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

---

## 18. 编码要求

1. 不引入没有必要的第三方库；
2. 不使用全局可变状态堆积业务逻辑；
3. Win32 资源使用 RAII 封装；
4. COM 指针使用安全封装；
5. 每个 API 调用检查返回值；
6. 不忽略 `GetLastError`；
7. 使用 `std::wstring` 处理 Windows 字符串；
8. 关键逻辑写简洁英文注释；
9. 不要为简单逻辑设计复杂抽象；
10. 不要为了“可扩展性”引入大量接口和继承；
11. 优先清晰、稳定、便于调试；
12. 所有窗口操作必须在明确的线程模型下执行；
13. 不允许多个线程并发调用任务栏修改逻辑；
14. 事件去抖动和重复操作要有明确策略。

---

## 19. 推荐模块职责

### `main.cpp`

- Windows 程序入口；
- 单实例检查；
- 创建主应用对象；
- 启动消息循环；
- 正常退出。

### `app.*`

- 程序总控；
- 窗口扫描；
- 当前主入口选择；
- 状态同步；
- 托盘命令处理；
- Explorer 重启处理。

### `chrome_window.*`

- 枚举窗口；
- 获取进程信息；
- 判断是否为可管理 Chrome 主窗口；
- 保存窗口元数据。

### `taskbar_controller.*`

- 初始化任务栏 COM；
- 从任务栏移除窗口；
- 恢复窗口；
- 恢复全部；
- 保留备用实现接口。

### `win_event_monitor.*`（Phase 7 可选，或 Phase 4 实测需要时提前引入）

- 安装和卸载 WinEventHook；
- 接收创建、销毁、显示和隐藏事件；动态入口模式才需要前台窗口事件；
- 将事件投递到主线程。

### `tray_icon.*`

- 创建托盘图标；
- 右键菜单；
- 处理 `TaskbarCreated`；
- 更新状态提示。

### `logger.*`

- 文件日志；
- 日志等级；
- 时间戳；
- 线程安全输出。

---

## 20. 性能要求

该工具长期后台运行，资源占用应尽量低。

目标：

- 空闲 CPU 占用接近 0%；
- 常驻内存尽量控制在较低水平；
- V1 不使用小于 1 秒的固定轮询；
- V1 允许每 2～5 秒做一次低频安全扫描，并可将其作为主要生命周期检测方式；
- Phase 7 可以在确有收益时改为事件驱动或事件加低频扫描；
- 同一窗口状态没有变化时，不重复调用任务栏 API。

---

## 21. 测试场景

必须编写 `tests/manual_test_plan.md`，至少覆盖以下场景。

“覆盖”表示测试计划中必须列出操作步骤、预期结果、实际结果和状态，并不表示所有环境组合都阻塞 V1。Phase 0～Phase 6 各自列出的项目是对应阶段的门槛；WindowTabs 完整兼容、Alt+Tab 完整兼容、多显示器、不同缩放和虚拟桌面等属于扩展回归，可以标记为“未执行”并说明原因。任何导致 Chrome 窗口被隐藏、关闭或无法到达的问题仍然是阻塞缺陷。

### 基本场景

1. 启动一个 Chrome 窗口；
2. 启动三个不同 Chrome 配置文件窗口；
3. 启动五个 Chrome 窗口；
4. 使用 WindowTabs 将三个 Chrome 窗口组合；
5. 通过 WindowTabs 顶部标签连续切换；
6. 通过 Alt+Tab 切换；
7. 通过任务栏切换；
8. 通过鼠标点击窗口切换。

### 生命周期

1. 新建 Chrome 窗口；
2. 关闭当前主入口窗口；
3. 关闭非当前窗口；
4. Chrome 崩溃并恢复；
5. Chrome 自动更新后重启；
6. 关闭所有 Chrome；
7. 再重新打开 Chrome。

### Explorer 场景

1. 在任务管理器中重启 Windows Explorer；
2. 结束并重新启动 `explorer.exe`；
3. 验证托盘图标恢复；
4. 验证任务栏状态重新应用。

### WindowTabs 场景

1. WindowTabs 先启动，本程序后启动；
2. 本程序先启动，WindowTabs 后启动；
3. WindowTabs 退出；
4. WindowTabs 重新启动；
5. WindowTabs 中拆分标签组；
6. WindowTabs 中重新组合标签组。

### 显示环境

1. 单显示器；
2. 双显示器；
3. 不同缩放比例；
4. Windows 虚拟桌面切换；
5. Chrome 最大化；
6. Chrome 全屏；
7. Chrome 最小化。

### 恢复场景

1. 正常退出工具；
2. 托盘菜单执行“恢复全部”；
3. 使用 `--restore-all`；
4. 强制结束进程后重新启动工具；
5. 重启电脑后验证 Chrome 任务栏状态正常。

---

## 22. Phase 2 核心可行性硬门槛

进入固定主入口 MVP 开发前，必须同时满足：

1. 能列出并明确选择真实 Chrome 主窗口；
2. 方法 A 和方法 B 的代码路径都已经执行并记录 API 结果；
3. 至少一种方法经用户观察，能够让指定窗口的任务栏按钮真实消失；
4. Chrome 窗口本身仍然打开、可到达和可操作；
5. 能恢复该窗口的任务栏按钮，且不需要重启 Explorer；
6. 方法 B 如被采用，恢复后的扩展样式必须与原值一致；
7. 日志清楚记录操作结果、错误码和恢复结果；
8. README 和手工测试记录写明 Windows、Chrome、WindowTabs 版本及实际现象；
9. WindowTabs 和 Alt+Tab 的影响已记录，但不要求完全无影响；
10. 如果方案不可行，必须说明实际原因和实验结果，并停止后续功能开发。

---

## 23. V1 最终验收标准

1. 启动多个 Chrome 配置文件窗口后，工具能够自动发现可管理主窗口；
2. 存在一个或多个可管理 Chrome 窗口时，Windows 任务栏只保留一个固定 Chrome 入口；
3. 切换 WindowTabs 标签、Alt+Tab 或鼠标焦点时，不要求任务栏入口跟随当前窗口；
4. 所有 Chrome 窗口仍然打开，并且至少可以通过一种正常交互方式到达和操作；
5. Chrome 新建、关闭和重建窗口后，在规定扫描周期内重新达到零个或一个任务栏入口；
6. Explorer 重启后能够重新应用任务栏规则并恢复托盘图标；
7. 暂停、恢复全部和正常退出后，本程序修改过的任务栏按钮恢复；
8. 强制结束后的恢复策略不会依据陈旧 HWND 修改无关窗口；
9. WindowTabs、Alt+Tab 和闪烁方面的实际影响已记录为兼容性结论；
10. 不影响 Chrome 浏览、下载、开发者工具和网页标签等窗口内功能；
11. 空闲 CPU 占用接近 0%，没有忙循环或高频重复任务栏调用；
12. 提供完整源码、CMake 构建文件、README、手工测试计划和构建脚本；
13. 全新构建目录中的 Debug、Release 和自动测试通过；
14. Release 版本可直接运行，README 中有明确恢复和卸载步骤。

---

## 24. README 必须包含的内容

项目完成后，`README.md` 至少包含：

1. 项目用途；
2. 当前支持的 Windows 版本；
3. 与 WindowTabs 的关系；
4. 工作原理；
5. 已知限制；
6. 构建方法；
7. 使用方法；
8. 托盘菜单说明；
9. 命令行参数；
10. 日志位置；
11. 恢复方法；
12. 测试环境；
13. 已验证的方法；
14. 未验证或不可靠的方法；
15. 卸载和恢复步骤。

---

## 25. Codex 执行要求

Codex 开始开发前，应先完成以下工作：

1. 阅读本需求文档；
2. 检查当前仓库内容；
3. 给出简短实施计划；
4. 等待用户确认本开发计划后，从 Phase 0 开始；
5. 编译并运行最小验证程序；
6. 根据实际结果决定正式技术方案；
7. 不要在尚未验证任务栏 API 可行性之前一次性实现全部功能；
8. 每完成一个 Phase 都更新 README 和测试记录；
9. 对不确定的 Windows 行为进行实际验证；
10. 不要假设 API 调用成功，必须检查结果和界面实际表现。

Codex 必须严格执行第 5 节的 Phase 0～Phase 6，并在每个 Phase 结束时提供阶段报告：

```text
Phase：
实现范围：
自动验收命令与结果：
仍需人工验收的原因：
人工操作步骤：
安全恢复方法：
当前结论：通过 / 失败 / 阻塞
下一 Phase 是否允许开始：是 / 否
```

凡是需要用户观察任务栏、WindowTabs、Alt+Tab、托盘或 Explorer 重启结果的阶段，Codex 必须停在对应验收门槛，给出可复制的命令和逐步指导，等待用户回报后再继续。不得把“程序未报错”当作人工视觉验收通过。

---

## 26. Git 提交建议

建议按阶段提交，不要把全部功能放进一个提交。

示例提交信息：

```text
chore: initialize CMake project
feat: enumerate manageable Chrome windows
experiment: verify taskbar removal APIs on Windows 11
feat: keep a fixed Chrome taskbar entry
feat: track Chrome window lifecycle
feat: add tray controls and restore actions
fix: restore taskbar state after Explorer restart
docs: add usage and manual test plan
```

Phase 2 可行性验证提交中，应在提交说明或测试文档中明确写出：

- 操作系统版本；
- Chrome 版本；
- WindowTabs 版本；
- 实际验证结果；
- 使用的 API；
- 已知问题。

---

## 27. 已知风险

1. Windows 11 任务栏可能忽略或覆盖部分旧任务栏 API；
2. Chrome 可能重新注册自己的任务栏按钮；
3. 不同 Chrome 配置文件可能使用不同 AppUserModelID；
4. 修改窗口扩展样式可能影响 Alt+Tab；
5. WindowTabs 可能对窗口外观和激活流程进行额外处理；
6. Explorer 重启后需要重新应用状态；
7. Windows 更新可能改变任务栏行为；
8. 某些公司安全软件可能阻止窗口钩子或 COM 操作；
9. 管理员权限和普通权限窗口之间可能存在访问限制。

这些风险必须通过实际测试确认。不要通过修改系统文件、注入 Explorer、注入 Chrome 或安装内核驱动来规避。

---

## 28. 明确禁止的实现方式

不要采用以下方式：

- 修改 Chrome 源码；
- 修改 Chrome 用户数据目录；
- 修改 Chrome 快捷方式中的配置文件参数；
- 强制结束或重启 Chrome；
- 注入 Chrome 进程；
- 注入 Explorer 进程；
- 全局 DLL 注入；
- 安装驱动；
- 修改系统文件；
- 通过隐藏整个 Chrome 窗口实现；
- 通过频繁最小化窗口实现；
- 通过模拟鼠标点击 WindowTabs 实现；
- 依赖固定窗口标题；
- 依赖固定 Chrome 配置文件名称；
- 未经验证直接修改 AppUserModelID；
- 未经测试和记录就接受 Alt+Tab 或 WindowTabs 兼容性退化；
- 导致其他 Chrome 窗口无法通过任何正常方式到达和操作。

---

## 29. 最终交付物

Codex 最终需要交付：

```text
ChromeTaskbarMerger/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── src/
├── tests/
│   └── manual_test_plan.md
├── build-release.ps1
└── .gitignore
```

同时应提供：

1. 可编译源码；
2. Release 构建方法；
3. 实际测试记录；
4. 已知限制；
5. 恢复方法；
6. 可执行文件生成路径说明；
7. Phase 2 任务栏 API 可行性结论。

---

## 30. 开发原则

本项目的首要目标不是代码数量，而是验证 Windows 11 上该方案是否真实可靠。

开发时遵循以下原则：

```text
先验证任务栏 API
再实现固定单入口
先保证可恢复
再实现生命周期自动化
先由 Codex 完成可自动验收项
再请用户确认真实任务栏效果
先记录 WindowTabs 和 Alt+Tab 影响
再决定是否投入兼容性优化
```

任何阶段发现核心方法在当前 Windows 环境下不可行，都应及时记录并调整方案，不要用大量外围代码掩盖核心功能未验证的问题。
