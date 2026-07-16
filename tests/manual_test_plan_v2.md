# ChromeTaskbarMerger V2 测试计划与实际结果

本文档只记录 V2 的实际验收证据。稳定需求、安全不变量和各 Phase 的通过门槛以
[V2 需求与设计](../docs/V2_REQUIREMENTS.md)为准；通用执行规则以
[Codex 开发与验收规范](../docs/CODEX_DEVELOPMENT_GUIDE.md)为准。

## 状态说明

- `PASS`：自动验收及本 Phase 要求的人工验收均通过；
- `FAIL`：存在明确反例；
- `BLOCKED`：缺少必要环境、权限或决定，无法安全继续；
- `NOT RUN`：尚未执行；
- `AUTOMATIC PASS / MANUAL PENDING`：自动项目通过，仍等待必要的真实桌面观察。

## Phase 总览

| Phase | 目标摘要 | 自动验收 | 人工验收 | 当前状态 |
| --- | --- | --- | --- | --- |
| 0 | 保护 V1 基线，建立 V2 纯模型与测试接缝 | 通过 | 不需要 | `PASS` |
| 1 | 内部标签与任务栏单入口核心可行性 | 通过 | 通过 | `PASS` |
| 2 | 事件驱动窗口与标签生命周期 | 通过 | 通过 | `PASS` |
| 3 | 窗口位置、状态与 DPI | 未执行 | 必须 | `NOT RUN` |
| 4 | 原子恢复、异常终止与故障注入 | 未执行 | 必须 | `NOT RUN` |
| 5 | 正式状态机、托盘、配置与登录启动迁移 | 未执行 | 必须 | `NOT RUN` |
| 6 | Explorer、显示环境与交互完善 | 未执行 | 必须 | `NOT RUN` |
| 7 | 2.0.0 发布候选与正式发布 | 未执行 | 必须 | `NOT RUN` |

## Phase 0：V1 基线保护与 V2 测试骨架

当前状态：`PASS`。

### 实现和安全边界

Phase 0 不改变 `1.0.0` 的默认启动、WindowTabs 检测、托盘、任务栏管理、恢复、配置或
登录启动行为。新增代码尚未连接到生产消息循环，只建立以下可独立测试的边界：

| 边界 | Phase 0 实现 | 允许的行为 |
| --- | --- | --- |
| 窗口注册与识别 | 复用 `chrome_window.*` 的只读枚举结果；抽取完整 `WindowIdentity` 值类型 | 只读取和校验 HWND、PID、TID、进程创建时间及窗口类 |
| 标签组控制 | `TabGroupModel` | 同步成员、稳定排序、活动成员、句柄复用保护及可达性资格判断 |
| 窗口协调 | `IWindowActivationGateway` 与 `TabActivationCoordinator` | 只调用注入的激活器；成功前不改变活动成员 |
| 标签栏健康 | `TabGroupModel::SetTabStripHealthy` | 作为任务栏移除的组级安全门禁，不创建真实标签栏 |
| 任务栏网关 | 复用 `ITaskbarMutationController` 与现有伪控制器 | Phase 0 不增加任何生产调用点 |
| 时间与故障 | 复用 `ScanSchedule` 的可控时间点、伪任务栏失败和恢复写前失败；新增伪激活失败 | 只在合成测试中触发 |

任务栏移除资格在纯模型中必须同时满足：标签已创建、标签栏健康、激活路径已验证、恢复
意图已持久化。任何身份不完整、同一轮重复 HWND 或 HWND 被新进程复用的成员都不能继承
旧的可达性证据。

### 自动验收项目

| 项目 | 预期结果 | 实际结果 | 状态 |
| --- | --- | --- | --- |
| 全新 Debug 构建 | MSVC x64、`/W4`，无错误和警告 | 独立 `build-portable` 配置和构建成功，无警告 | PASS |
| 全新 Release 构建 | MSVC x64、`/W4`，无错误和警告 | 独立 `build-portable` 构建成功，无警告 | PASS |
| Debug CTest | V1 回归与 V2 模型测试全部通过 | 7/7 通过 | PASS |
| Release CTest | V1 回归与 V2 模型测试全部通过 | 7/7 通过 | PASS |
| V2 纯模型覆盖 | 空组、1/3/5、重复、关闭、HWND 复用、稳定顺序与四项门禁 | 合成模型测试全部通过 | PASS |
| 伪激活器 | 未就绪不调用；成功才切换；失败保留原活动成员和错误码 | 三条路径及 `ERROR_ACCESS_DENIED` 传播均通过 | PASS |
| V1 命令行基线 | `--help`、`--version`、未知参数和只读 `--list` 退出码符合预期 | 依次为 0、0、2、0；版本 `1.0.0` | PASS |
| V1 便携包基线 | 仍生成 `1.0.0` x64 便携目录和 ZIP，文件清单正确 | EXE、INI、README、LICENSE 四文件及 ZIP 完整 | PASS |
| 恢复与配置基线 | V1 恢复、配置、登录启动测试继续通过且无真实系统残留 | 对应回归通过；临时注册表子键 0，测试后进程 0 | PASS |
| 新增 API 安全扫描 | 新增 V2 文件不调用窗口位置、显示、重父化或任务栏修改 API | 未发现相关 API；未发现生产调用点 | PASS |
| 源码检查 | `git diff --check` 和文档相对链接检查通过 | 空白检查通过；10 个 Markdown 文件无失效相对链接 | PASS |

### 自动验收记录

验收日期：2026-07-16。

环境：Windows 11 `10.0.26200`、CMake `4.3.0`、Visual Studio 18 2026 Community、
MSVC `19.51.36246`、Windows SDK `10.0.26100.0`、x64。

核心命令：

```powershell
.\scripts\build-portable.ps1
.\build-portable\Debug\ChromeTaskbarMerger.exe --help
.\build-portable\Debug\ChromeTaskbarMerger.exe --version
.\build-portable\Debug\ChromeTaskbarMerger.exe --phase0-unknown
.\build-portable\Debug\ChromeTaskbarMerger.exe --list
git diff --check
```

`build-portable.ps1` 从空的独立构建树配置项目，依次构建 Debug/Release、运行两个配置的
CTest、安装 Release 便携目录并生成 ZIP。本次两个配置均为 7/7 通过，构建输出没有
MSVC 警告。

只读 `--list` 实际扫描 490 个顶层窗口，进程查询失败为 0；识别到 11 个 Chrome 候选，
其中 1 个可管理、10 个排除，退出码为 0。此命令没有修改 Chrome、窗口或任务栏。

便携产物验证结果：

- Release PE machine 为 `8664 (x64)`，subsystem 为 `2 (Windows GUI)`；
- 文件版本和产品版本均为 `1.0.0`；
- 导入表只有 Windows 系统 DLL，无 `VCRUNTIME`、`MSVCP` 或 `ucrtbase` 动态依赖；
- 便携目录与 ZIP 均只有 `ChromeTaskbarMerger.exe`、`ChromeTaskbarMerger.ini`、
  `README.md`、`LICENSE`；
- EXE SHA-256：`2ED7B22C8F51549A46B637A7FA26B9F476DEBE382D3E7A96598EB79FF9AA9935`；
- ZIP SHA-256：`327B71590E18C02E109985312D8B471F5DEFD8146266230DBBDAA4C92B7D1443`。

安全边界扫描只检查本 Phase 新增的 V2 源文件，没有发现 `SetWindowPos`、
`DeferWindowPos`、`SetForegroundWindow`、`ShowWindow`、`SetParent`、
`SetWindowLongPtr`、`DeleteTab` 或 `AddTab`。新增模型和协调器也没有生产调用点；它们只由
新单元测试调用。V1 的 `FixedEntryManager`、恢复日志和托盘生产路径仍保留且通过原有回归。

### 人工验收

通常不需要。Phase 0 没有创建真实标签栏，也没有新增真实窗口或任务栏修改路径，因此
不存在需要用户观察的 V2 界面。首次必须由用户观察的硬门槛是 Phase 1，届时会同时验收
内部标签和 V1 已有的任务栏单入口能力。

安全恢复方法：Phase 0 的新增代码没有连接到生产运行路径，无需对 Chrome、任务栏或
窗口布局执行恢复。便携基线验收只运行只读命令，不启动 `--manage`。

Phase 0 结论：自动验收全部通过，不需要人工验收，随后已进入 Phase 1；结果见下节。

## Phase 1：内部标签 + 任务栏单入口核心可行性硬门槛

当前状态：`PASS`。

### 实现范围

- 新增隔离命令 `--v2-experiment`；无参数启动、`--autostart`、`--manage` 和 V1 托盘行为
  均未切换到 V2；
- 使用纯 C++/Win32 顶层工具窗口绘制最小标签栏，不使用 `SetParent`，不注入 Chrome；
- 标签栏通过 owner 跟随活动 Chrome；组内 Chrome 保持独立顶层窗口、共享一个矩形，并
  通过 Z-order 切换，不隐藏、不最小化、不移到屏外；
- 实验只接受 1～5 个普通、可见、非最小化/最大化 Chrome 窗口；Phase 1 人工测试还要求
  使用非 Snap 的普通浮动窗口；
- 每个标签必须先创建并完成身份/激活路径验证，任务栏恢复写前记录成功后，
  `FixedEntryManager` 才允许 `DeleteTab`；
- WindowTabs 在确认前或实验运行期间出现时立即拒绝或回退；
- `p` 暂停、`q` 退出、激活失败、标签栏失效和窗口身份变化均按“停止新移除 → AddTab →
  原矩形恢复 → 销毁标签栏”的顺序处理；恢复失败时保留标签栏并进入可重试状态；
- Phase 1 的关闭区域只完成布局和命中测试，点击 `×` 不会关闭 Chrome。窗口关闭生命周期
  属于 Phase 2。

### 自动验收项目

| 项目 | 预期结果 | 实际结果 | 状态 |
| --- | --- | --- | --- |
| 全新 Debug/Release | x64、`/W4`，无错误和警告 | 两种配置均成功，无警告 | PASS |
| Debug CTest | V1 回归及 Phase 1 测试全部通过 | 9/9 通过 | PASS |
| Release CTest | V1 回归及 Phase 1 测试全部通过 | 9/9 通过 | PASS |
| 标签布局 | 1、3、5、窄宽度、间隙、标题和关闭命中 | 全部合成布局测试通过 | PASS |
| 激活模型 | 点击只发送对应完整身份；失败不改变活动成员 | 伪激活成功/失败/陈旧身份全部通过 | PASS |
| 任务栏事务顺序 | 写前记录、完整标签门禁先于任务栏控制器 | 共享事件序列断言通过 | PASS |
| 标签创建故障 | 标签未创建时不得调用任务栏移除 | 真实模型门禁 + 伪任务栏通过，移除调用 0 | PASS |
| 激活路径故障 | 未验证时不得调用任务栏移除 | 真实模型门禁 + 伪任务栏通过，移除调用 0 | PASS |
| 恢复故障 | 写前回滚保存失败仍保留可恢复义务 | 故障注入后 AddTab/清空重试通过 | PASS |
| 临时窗口集成 | 三个本进程顶层窗口同矩形、标签切换、Z-order、owner 和恢复 | 全部通过，原矩形逐个精确恢复 | PASS |
| 真实网关安全 | 当前身份可验证，陈旧身份拒绝；前台拒绝不误报成功 | 通过 | PASS |
| 隔离命令取消 | 非 `V2-START` 确认不修改并退出 0 | 实际取消路径退出码 0 | PASS |
| 禁止实现扫描 | 无 `SetParent`、`SW_HIDE`、屏外隐藏或 Chrome 数据修改 | 未发现 | PASS |
| 便携构建 | 原 `1.0.0` 便携目录与 ZIP 仍可生成 | 成功 | PASS |

### 自动验收记录

验收日期：2026-07-16。

环境：Windows 11 `10.0.26200`、CMake `4.3.0`、Visual Studio 18 2026、
MSBuild `18.7.8`、MSVC `19.51.36248`、Windows SDK `10.0.26100.0`、x64。

核心命令：

```powershell
.\scripts\build-portable.ps1
ctest --test-dir .\build-portable -C Debug --output-on-failure
ctest --test-dir .\build-portable -C Release --output-on-failure
'CANCEL' | .\build-portable\Debug\ChromeTaskbarMerger.exe --v2-experiment
git diff --check
```

自动集成测试创建和销毁本进程自己的三个临时顶层窗口及一个标签栏，不调用真实
`ITaskbarList::DeleteTab/AddTab`。非交互式测试进程没有真实鼠标输入授权，Windows 可能
拒绝 `SetForegroundWindow`；因此自动测试使用可注入激活器确定点击身份和真实 Z-order，
同时单独验证生产 Win32 网关的身份保护与前台拒绝错误。真实点击、输入焦点和任务栏数量
必须由下面的人工验收确认，不能由自动测试虚构通过。

### 必需人工验收

验收目标：在完全不运行 WindowTabs 时，确认三个真实 Chrome 同时拥有三个内部标签、
恰好一个任务栏入口，并能由标签可靠切换；暂停和直接退出均完整恢复。

前置条件：

1. 保存 Chrome 中正在编辑的内容；本实验不关闭网页，但仍属于开发版本测试；
2. 从托盘右键完全退出已安装的 ChromeTaskbarMerger；
3. 完全退出 WindowTabs，并确认通知区域没有其图标；
4. 只保留 3 个可见 Chrome 主窗口；三个窗口都调整为普通浮动状态，不要最小化、最大化
   或 Snap，建议先摆在三个明显不同的位置；
5. 不要在测试过程中关闭 Chrome、移动/缩放窗口、按 F11 或启动 WindowTabs，这些生命周期
   属于后续 Phase。

使用的 EXE：

```text
.\build-portable\Debug\ChromeTaskbarMerger.exe
```

#### 第一轮：标签切换和暂停恢复

1. 在仓库根目录的 PowerShell 运行：

   ```powershell
   .\build-portable\Debug\ChromeTaskbarMerger.exe --v2-experiment
   ```

2. 确认列表显示 `Manageable Chrome windows: 3`，且三行标题分别对应三个窗口；如果不是
   3，不要继续，输入任意其他文字取消并回报列表数量；
3. 输入精确的大写确认词 `V2-START`；
4. 预期三个 Chrome 收敛到同一位置，上方出现三个深色内部标签，当前标签为蓝色；Windows
   任务栏恰好只剩 1 个 Chrome 入口；
5. 分别点击三个标签的标题区域，避开右侧 `×`；每次确认目标 Chrome 成为前台、网页可
   点击、地址栏或网页输入框可以输入；
6. 回到 PowerShell 窗口按单键 `p`（不用 Enter）；
7. 预期标签栏消失，三个任务栏按钮恢复，三个 Chrome 回到测试前各自不同的原位置；控制台
   显示 `State: PAUSED`，且没有 `RECOVERY REQUIRED`；
8. 按单键 `q` 退出。

#### 第二轮：直接退出恢复

1. 不改变三个普通浮动 Chrome 的数量，再次运行同一命令并输入 `V2-START`；
2. 确认再次出现三个标签和一个任务栏入口；
3. 直接回到 PowerShell 按单键 `q`，不要先按 `p`；
4. 预期进程退出，标签栏消失，三个任务栏按钮和三个原位置全部恢复；
5. 命令返回后运行 `$LASTEXITCODE`，预期为 `0`。

### 人工验收实际结果

验收日期：2026-07-16。用户完成了上述第一轮和第二轮测试，并明确确认所有步骤完全符合
预期。该确认对应的逐项结果如下：

| 检查项 | 实际结果 | 状态 |
| --- | --- | --- |
| 第一轮列出 3 个真实 Chrome 窗口 | 符合预期 | PASS |
| 出现 3 个内部标签 | 符合预期 | PASS |
| Windows 任务栏恰好保留 1 个 Chrome 入口 | 符合预期 | PASS |
| 标签 1/2/3 均可点击、输入和操作 | 符合预期 | PASS |
| 按 `p` 后 3 个任务栏按钮恢复 | 符合预期 | PASS |
| 按 `p` 后三个窗口原位置恢复 | 符合预期 | PASS |
| 第一轮未出现 `RECOVERY REQUIRED` 或其他错误 | 符合预期 | PASS |
| 第二轮直接按 `q` 后标签栏、按钮和位置恢复 | 符合预期 | PASS |
| 第二轮进程退出码为 `0` | 符合预期 | PASS |

### 失败时立即恢复

- 如果控制台显示 `RECOVERY REQUIRED`，不要关闭窗口；按单键 `r` 重试，直到显示恢复完成，
  然后按 `q`；
- 如果程序仍在运行但标签无法点击，先在控制台按 `p`；若未恢复，按 `r`；
- 如果程序意外结束且 Chrome 任务栏按钮没有全部恢复，在仓库根目录运行：

  ```powershell
  .\build-portable\Debug\ChromeTaskbarMerger.exe --restore-all
  ```

- `--restore-all` 恢复任务栏入口；Phase 1 尚未持久化窗口布局日志。如果异常结束后窗口仍
  重叠，任务栏入口恢复后可手动把三个 Chrome 移回原位；不要结束 Chrome 进程；
- 日志位置：`%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log`。

用户回报模板：

```text
第一轮列出 3 个窗口：是/否
出现 3 个内部标签：是/否
任务栏恰好 1 个 Chrome：是/否
标签 1/2/3 均可点击、输入和操作：是/否
按 p 后 3 个任务栏按钮恢复：是/否
按 p 后三个原位置恢复：是/否
第二轮按 q 后按钮和位置恢复：是/否
第二轮退出码：
是否出现 RECOVERY REQUIRED 或其他错误：
其他现象：
```

Phase 1 结论：自动验收和真实 Chrome/任务栏人工验收均已通过，状态为 `PASS`。核心
可行性硬门槛已满足，随后已进入 Phase 2；结果见下节。

## Phase 2：事件驱动的窗口与标签生命周期

当前状态：`PASS`。

### 实现范围

- 新增主线程 `ChromeWindowRegistry`，以完整窗口身份保持稳定顺序，处理新增、关闭、标题
  更新、活动成员回退和 HWND 复用；非 owner 线程的修改会被拒绝；
- 新增 `WINEVENT_OUTOFCONTEXT` 监视器，监听创建、销毁、显示、隐藏、名称、前台、最小化
  开始和结束事件；回调只用 `PostThreadMessageW` 投递事件类型与 `HWND`；
- 主线程以 100ms 去抖合并事件风暴，连续事件最多延迟 500ms；每 2 秒执行一次完整扫描
  兜底。事件仅触发同步，完整 Chrome 扫描始终是真实状态来源；
- Chrome 新窗口创建或 HWND 替换期间若短暂处于 disabled 状态，保留尚未处理的事件、销毁
  和前台证据，每 250ms 延迟重试；等待期间不把新窗口加入布局或移除其任务栏入口，也不因
  该瞬态退出整个实验；
- 没有模型变化的事件批次或兜底扫描不会调用标签、布局或任务栏修改 API；标题变化只更新
  标签，前台变化只更新活动标签和 owner，只有成员变化才进入布局和任务栏事务；
- 新窗口先捕获自己的原始位置、加入标签栏并验证激活路径，然后由写前恢复门禁决定是否
  `DeleteTab`；任何准备失败都会保留该新窗口的任务栏入口并回退整个实验；
- 关闭非活动或活动窗口会清理标签、布局参与状态和任务栏恢复记录；活动窗口关闭后选择
  当前前台或稳定的剩余成员；
- 所有 Chrome 窗口关闭后标签栏消失，任务栏恢复记录清空，但实验继续等待；重新打开
  Chrome 后会自动重建标签栏和任务栏单入口；
- 收到托管 HWND 的 `DESTROY` 时，先恢复并使旧身份失效，再处理扫描中的当前身份，防止
  快速复用继承旧标签可达性、布局或任务栏状态；
- Phase 2 的标签 `×` 会在重新核验完整身份后向对应 Chrome 主窗口投递 `WM_CLOSE`；
- Phase 2 所有关键控制台信息同时写入日志，包括日志路径、窗口清单、确认提示、单键命令、
  生命周期结果和错误；窗口清单明确显示 `normal/minimized/maximized`，非普通状态会在确认
  和任何窗口或任务栏修改之前被预检拒绝；真实控制台通过 `WriteConsoleW` 输出中文标题，
  重定向输出显式使用 UTF-8，状态放在标题之前，格式为 `1. [normal] 标题 (...)`；
- 本 Phase 仍限定同时管理 1～5 个普通浮动窗口。最小化、最大化、Snap、DPI 和跨屏幕
  状态属于 Phase 3。

### 自动验收项目

| 项目 | 预期结果 | 实际结果 | 状态 |
| --- | --- | --- | --- |
| 全新 Debug/Release | x64、`/W4`，无错误和警告 | 两种配置均成功，无警告 | PASS |
| Debug CTest | V1、Phase 0/1 回归和 Phase 2 测试通过 | 11/11 通过 | PASS |
| Release CTest | V1、Phase 0/1 回归和 Phase 2 测试通过 | 11/11 通过 | PASS |
| 合成生命周期 | 创建、关闭非活动/活动/全部、重开、标题和身份复用 | 全部注册器场景通过 | PASS |
| 事件风暴 | 100ms 去抖、500ms 上限，只形成一次到期同步 | 可控时钟测试通过 | PASS |
| 瞬态窗口延迟 | disabled 窗口延迟重试且保留事件、销毁和前台证据 | 可控时钟测试通过 | PASS |
| 扫描兜底 | 2 秒到期且同步后重新计时，不真实等待 | 可控时钟测试通过 | PASS |
| WinEvent 接缝 | 只翻译指定事件，过滤子对象噪声，轻量消息无损 | Hook 安装、投递、解码和停止通过 | PASS |
| 主线程串行 | 临时顶层窗口在 owner 线程更新，其他线程拒绝 | `ERROR_INVALID_THREAD_ID` 路径通过 | PASS |
| 临时窗口动态布局 | 运行中加入第三个、关闭一个、剩余恢复原位置 | 2 个恢复、1 个安全跳过，无残留 | PASS |
| 新标签故障注入 | 未创建/验证标签时新任务栏入口必须保持可见 | `DeleteTab` 调用 0，准备完成后才调用 1 次 | PASS |
| 无变化幂等 | 相同快照不产生视觉或成员工作，重复同步不重复移除 | 注册器和任务栏回归通过 | PASS |
| 隔离命令取消 | 非 `V2-START` 不修改并退出 | 实际退出码 0，残留进程 0 | PASS |
| 控制台与日志双写 | 启动、清单、状态、提示、命令和错误均可从日志复原 | 实际日志路径和最小化预检完整落盘 | PASS |
| 初始窗口状态预检 | 非普通窗口在确认和修改前拒绝，并指出具体状态 | 真实最小化 Chrome 标为 `[minimized]`，退出码 4 | PASS |
| 便携构建 | 原 `1.0.0` 便携目录和 ZIP 继续生成 | 成功 | PASS |

### 自动验收记录

验收日期：2026-07-16。

环境：Windows 11 `10.0.26200`、CMake `4.3.0`、Visual Studio 18 2026、
MSBuild `18.7.8`、MSVC `19.51.36248`、Windows SDK `10.0.26100.0`、x64。

核心命令：

```powershell
.\scripts\build-portable.ps1
ctest --test-dir .\build-portable -C Debug --output-on-failure
ctest --test-dir .\build-portable -C Release --output-on-failure
'CANCEL' | .\build-portable\Debug\ChromeTaskbarMerger.exe --v2-experiment
```

全新构建产生 11 个测试目标；Debug 和 Release 均为 11/11。自动窗口测试只创建、更新、
排列和销毁测试进程自己的临时顶层窗口；任务栏生命周期测试使用伪控制器，没有调用真实
`ITaskbarList::DeleteTab/AddTab`。`--v2-experiment` 的取消路径在确认词之前退出，不修改
窗口、布局或任务栏。

### 必需人工验收

验收目标：在完全不运行 WindowTabs 时，集中验证真实 Chrome 的 1/3/5 个窗口、新建、
标题更新、关闭非活动、关闭活动、全部关闭、重开、标签切换、任务栏数量和最终恢复。

前置条件：

1. 保存 Chrome 中正在编辑的内容；Phase 2 会实际关闭测试窗口；
2. 从托盘右键完全退出已安装的 ChromeTaskbarMerger；
3. 完全退出 WindowTabs，并确认通知区域没有其图标；
4. 关闭多余 Chrome 窗口，只保留 1 个普通浮动 Chrome 主窗口；不要最小化、最大化或 Snap；
5. 建议让初始窗口打开一个容易识别的普通网页，测试期间最多保留 5 个 Chrome 主窗口；
6. Phase 2 只验收生命周期，测试期间不要拖动或缩放 Chrome。外部标签栏与其他成员跟随
   任意 Chrome 整体移动属于 Phase 3，当前版本不会跟随。

程序启动后会显示实际日志路径，并在窗口清单中把每个窗口标为 `[normal]`、`[minimized]`
或 `[maximized]`。只有所有窗口均为 `[normal]` 时才能进入确认；否则先把对应 Chrome
还原为普通窗口，再重新运行命令。

使用的 EXE：

```text
.\build-portable\Debug\ChromeTaskbarMerger.exe
```

#### 第一轮：完整生命周期和暂停恢复

1. 在仓库根目录的 PowerShell 运行：

   ```powershell
   .\build-portable\Debug\ChromeTaskbarMerger.exe --v2-experiment
   ```

2. 确认显示 `Manageable Chrome windows: 1`，输入精确的大写确认词 `V2-START`；
3. 确认出现 1 个内部标签，任务栏只有 1 个正在运行的 Chrome 入口；
4. 在 Chrome 中按两次 `Ctrl+N` 新建两个普通窗口；每次最多等待 2 秒，最终确认有 3 个内部
   标签、任务栏仍恰好 1 个 Chrome 入口；
5. 让三个窗口分别打开标题明显不同的网站，确认对应标签标题在最多 2 秒内更新；依次点击
   三个标签，确认每个窗口都可到达、输入和操作；
6. 激活标签 1，然后点击非活动标签 3 右侧的 `×`；确认关闭的是窗口 3，剩 2 个标签且任务栏
   仍为 1 个 Chrome 入口；
7. 激活标签 2，再点击标签 2 的 `×`；确认活动窗口关闭后自动选择剩余窗口，剩 1 个标签，
   网页仍可输入和操作；
8. 在剩余 Chrome 中按四次 `Ctrl+N`，每次等待标签出现后再继续；最终确认有 5 个内部标签、
   任务栏仍恰好 1 个 Chrome 入口，并依次点击五个标签确认均可到达；
9. 依次点击标签右侧 `×` 关闭全部五个窗口；确认最后一个关闭后标签栏消失，没有正在运行的
   Chrome 窗口入口，控制台仍保持 `MANAGING` 且没有 `RECOVERY REQUIRED`；
10. 从开始菜单或桌面快捷方式重新打开 Chrome；最多等待 2 秒，确认 1 个标签栏自动重建；
    再按两次 `Ctrl+N`，确认重新收敛为 3 个标签和 1 个任务栏入口；
11. 回到 PowerShell 按单键 `p`；确认标签栏消失、3 个 Chrome 任务栏按钮恢复、三个重新打开
    的窗口恢复到加入组之前各自的位置，没有 `RECOVERY REQUIRED`；
12. 按单键 `q` 退出。

#### 第二轮：动态变化后直接退出恢复

1. 保留上轮的 3 个普通浮动 Chrome，再次运行同一命令并输入 `V2-START`；
2. 按一次 `Ctrl+N`，确认从 3 个标签自动变为 4 个，任务栏仍为 1 个入口；
3. 关闭一个非活动标签，确认收敛为 3 个标签；
4. 不先暂停，回到 PowerShell 直接按单键 `q`；
5. 确认进程退出、标签栏消失、剩余 3 个任务栏按钮和原位置恢复；
6. 运行 `$LASTEXITCODE`，预期为 `0`。

### 人工验收实际结果

2026-07-16 首轮真实 Chrome 测试发现：

- 新建窗口及 HWND 替换期间，一个 Chrome 主窗口短暂处于 disabled 状态；原实现将其视为
  致命错误，日志记录 `Capturing the changed Chrome membership failed: The target window is
  disabled.`，随后安全恢复任务栏和原布局并以错误码退出。Windows Application 日志没有
  崩溃记录。该问题已改为保留生命周期证据并延迟 250ms 重试；最终真实复测在
  `23:19:53.964` 记录 `LIFECYCLE_DEFER`、在 `23:19:58.018` 记录 `LIFECYCLE_RESUME`，新 HWND
  随后成功加入且程序没有退出，修复通过；
- 用户拖动 Chrome 后，非活动窗口和外部标签栏没有跟随。该行为不属于 Phase 2 生命周期
  范围；当前布局只在成员变化时应用一次共享矩形。任意成员带动其余成员和标签栏整体移动
  已明确列为 Phase 3 的实现内容与通过标准；
- 用户确认五个 Chrome 在不拖动时保持可操作，Windows 任务栏正确维持一个入口；累计真实
  日志还覆盖 1/3/5 个窗口、标题更新、三个标签逐一激活、关闭非活动和活动窗口、空组、
  重开及多次正常退出恢复；
- 最终复测按 `q` 后记录 `Normal exit`，`ADD_TAB` 成功，任务栏和原布局恢复，进程退出且没有
  `RECOVERY REQUIRED`。Phase 1 已人工验证 `p`，而 Phase 2 的 `p` 与 `q` 使用同一个
  `RestoreSession` 事务；动态成员后的 `q` 已重复验证该恢复事务。

### 失败时立即恢复

- 如果控制台显示 `RECOVERY REQUIRED`，不要关闭控制台；按单键 `r` 重试恢复；
- 如果标签数在 2 秒后仍不正确，先停止新建或关闭操作，记录当前窗口数、标签数和任务栏
  入口数，然后按 `p`；
- 如果程序意外结束且任务栏按钮未恢复，在仓库根目录运行：

  ```powershell
  .\build-portable\Debug\ChromeTaskbarMerger.exe --restore-all
  ```

- 日志位置：`%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log`；日志中的
  `LIFECYCLE_SYNC` 行会记录同步原因及新增、移除和标题更新数量；
- Phase 2 仍未持久化窗口布局日志。如果进程被强制结束后窗口仍重叠，先恢复任务栏按钮，
  再手动移动窗口；不要结束 Chrome 进程来尝试恢复。

用户回报模板：

```text
初始 1 个标签 / 1 个任务栏入口：是/否
新建到 3 个后标签为 3、任务栏为 1：是/否
三个标题均自动更新：是/否
三个标签均可点击、输入和操作：是/否
关闭非活动后正确剩 2 个：是/否
关闭活动后正确选择剩余窗口：是/否
新建到 5 个后标签为 5、任务栏为 1，且均可到达：是/否
全部关闭后标签栏消失、无运行窗口入口、控制台仍管理中：是/否
重开后自动恢复到 1，再新建到 3：是/否
按 p 后 3 个按钮和窗口原位置恢复：是/否
第二轮 3→4→3 正确收敛：是/否
第二轮直接 q 后按钮和位置恢复：是/否
第二轮退出码：
是否出现 RECOVERY REQUIRED 或其他错误：
其他现象：
```

Phase 2 结论：自动验收和真实 Chrome/任务栏人工验收均已通过，状态为 `PASS`。窗口移动、
缩放和整个组跟随属于 Phase 3，不作为 Phase 2 失败项。

## Phase 3～7

Phase 3 尚未开始。进入每个 Phase 后，本文件将记录实际命令、自动结果、必要的人工步骤、用户
回报和恢复方法，不提前把计划项目写成已通过。
