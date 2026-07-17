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
| 3 | 窗口位置、状态与 DPI | 通过 | 通过（跨显示器/DPI `NOT RUN`） | `PASS` |
| 4 | 原子恢复、异常终止与故障注入 | 通过 | 通过 | `PASS` |
| 5 | 正式状态机、托盘、配置与登录启动迁移 | 通过 | 通过（实际注销移至 Phase 7） | `PASS` |
| 6 | Explorer、显示环境、交互与仅内存自定义名称 | 通过 | 通过（扩展显示矩阵 `NOT RUN`） | `PASS` |
| 7 | Chrome profile 名称持久化评估、2.0.0 发布 | 15/15 及 5/5 只读探测通过 | 通过 | `PASS / RELEASED` |

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

## Phase 3：窗口位置、状态与 DPI

当前状态：`PASS`。

### 实现范围

- `EVENT_OBJECT_LOCATIONCHANGE`、最小化开始和结束事件从生命周期扫描中分离，进入 16ms
  去抖、32ms 最大延迟的高频几何队列；只处理当前注册表中的完整窗口身份；
- 拖动或缩放任意当前成员时，读取该成员的真实矩形，按所在显示器工作区和 DPI 计算新的
  组矩形；全部 Chrome 通过一次 `DeferWindowPos` 提交共同位置、大小和 Z-order，外部标签栏
  随后移动到组顶部；程序自身产生的位置事件会因为矩形未变化而幂等跳过；
- Snap 或靠近屏幕边缘的矩形会被约束到当前工作区，始终为外部标签栏保留可见空间；负坐标
  显示器使用原生虚拟桌面坐标，不假设左上角为 `(0, 0)`；
- 组状态覆盖 normal、minimized、maximized 和 fullscreen：最小化/恢复应用到全部成员；
  最大化请求转换为受管最大化：Chrome 保持原生 normal 状态并铺在外部标签栏下方，避免原生
  最大化矩形重新覆盖顶部空间；恢复到最大化前组矩形；F11 显示器全屏时隐藏标签栏，退出后
  恢复之前组状态；
- manifest 升级到 Per-Monitor V2；38px 基准标签高度、布局间距、关闭命中、文本边距和字体
  按窗口 DPI 缩放；`WM_DPICHANGED` 会重建 DPI 字体和命中布局；
- Phase 1/2 捕获的每个成员原始 `WINDOWPLACEMENT` 和矩形不因后续移动、缩放或状态切换被
  覆盖；`p`、`q` 和失败回滚仍恢复加入组之前的独立布局。

### 自动验收项目

| 项目 | 预期结果 | 实际结果 | 状态 |
| --- | --- | --- | --- |
| Debug CTest | V1、Phase 0～2 回归及 Phase 3 测试通过 | 12/12 通过 | PASS |
| Release CTest | V1、Phase 0～2 回归及 Phase 3 测试通过 | 12/12 通过 | PASS |
| 高频几何合并 | 16ms 去抖、32ms 上限并保留最新移动/最小化提示 | 可控时钟测试通过 | PASS |
| 普通与负坐标几何 | 内容矩形可重建组，负坐标不被错误截断 | 纯几何测试通过 | PASS |
| Snap/安全工作区 | 标签栏和内容始终位于选定工作区 | 边缘及工作区约束测试通过 | PASS |
| DPI 矩阵 | 100%/125%/150%/200% 标签高度与命中按比例缩放 | 38/48/57/76px 及关闭命中通过 | PASS |
| 全屏判定 | 显示器矩形与工作区矩形可区分 | 负坐标显示器测试通过 | PASS |
| 临时窗口移动/缩放 | 驱动成员改变后其余成员与标签栏跟随 | 三个真实临时 HWND 矩形一致 | PASS |
| 临时窗口状态 | 整组最小化/恢复、最大化/恢复可逆 | 三个真实临时 HWND 全部通过 | PASS |
| 原始布局恢复 | 多次几何和状态变化后恢复各自原矩形 | 3/3 精确恢复 | PASS |
| manifest/PE | x64、PerMonitorV2、`asInvoker` 嵌入最终 EXE | 资源级检查通过 | PASS |
| 全新便携构建 | 从空构建树完成双配置、测试、安装和 ZIP | 四文件目录与 ZIP 成功 | PASS |
| 任务栏安全 | 自动测试不修改真实 Chrome 或真实任务栏 | 仅本进程临时窗口 | PASS |

核心命令：

```powershell
cmake --build .\build-portable --config Debug
cmake --build .\build-portable --config Release
ctest --test-dir .\build-portable -C Debug --output-on-failure
ctest --test-dir .\build-portable -C Release --output-on-failure
```

### 必需人工验收

验收目标：确认真实 Chrome 在连续拖动、缩放和常见窗口状态下表现为一个组，并能完整恢复。

前置条件：

1. 保存 Chrome 中正在编辑的内容；
2. 完全退出托盘版 ChromeTaskbarMerger 和 WindowTabs；
3. 保留 3 个普通、未 Snap、未最大化的 Chrome 主窗口，放在三个明显不同的位置；
4. 单显示器是必测基线；只有具备第二显示器时才执行跨屏步骤；
5. 测试期间不要新建或关闭窗口，以便只隔离 Phase 3 几何行为。

运行：

```powershell
.\build-portable\Debug\ChromeTaskbarMerger.exe --v2-experiment
```

确认清单中三个窗口均为 `[normal]`，输入 `V2-START`，然后依次执行：

1. 拖动当前 Chrome 的标题栏做慢速小范围移动，再做一次快速大范围移动；确认另外两个
   Chrome 和外部标签栏连续跟随，没有停留在旧位置；
2. 点击第二个外部标签，拖动现在显示的第二个 Chrome；确认它也能驱动整个组；
3. 连续拖动窗口右边缘、下边缘和右下角进行缩放；确认三个 Chrome 始终同尺寸、同位置，
   标签栏宽度同步且标签仍可点击；
4. 点击 Chrome 最大化按钮；确认整个组占用当前显示器工作区，外部标签栏位于 Chrome 上方，
   Chrome 自身网页标签栏没有被遮挡，右上角最小化/最大化/关闭按钮全部可见且可用，三个外部
   标签均可切换；再次点击同一 Chrome 最大化按钮，确认回到最大化前的组位置和大小；
5. 点击最小化按钮；确认整个组和标签栏一起消失，任务栏仍只有一个 Chrome 入口；点击该
   任务栏入口恢复，确认三个窗口与标签栏一起返回；
6. 将当前窗口拖到屏幕左边缘完成 Snap；确认组位于左侧工作区且标签栏没有跑出屏幕；再拖离
   Snap，确认回到普通可移动状态。然后对右侧 Snap 重复一次；
7. 在当前 Chrome 按 `F11`；确认外部标签栏隐藏且 Chrome 正常全屏。再次按 `F11`，确认
   标签栏和全屏前组矩形恢复；
8. 如果有第二显示器，将组拖到另一显示器，确认成员与标签栏跨屏跟随；如果两个显示器 DPI
   不同，确认标签高度和关闭区域随 DPI 调整且没有明显错位；没有第二显示器则记录“未执行”；
9. 回到 PowerShell 按 `p`；确认标签栏消失、三个任务栏入口恢复、三个 Chrome 分别回到测试
   前三个不同的位置，没有 `RECOVERY REQUIRED`；
10. 按 `q` 退出，运行 `$LASTEXITCODE`，预期为 `0`。

### 失败时立即恢复

- 先按 `p`；如果显示 `RECOVERY REQUIRED`，按 `r` 重试；
- 如果 Chrome 正处于 F11，请先按 `F11` 退出全屏，再执行恢复；
- 如果程序意外退出且任务栏入口没有恢复，运行：

  ```powershell
  .\build-portable\Debug\ChromeTaskbarMerger.exe --restore-all
  ```

- Phase 3 版本当时尚未实现 Phase 4 的持久化窗口布局恢复；当前 Phase 4 版本应优先运行
  `--restore-all`，不要删除恢复日志，也不要结束 Chrome 进程。

用户回报模板：

```text
慢速/快速拖动时其余窗口与标签栏连续跟随：是/否
切换到第二个标签后也能驱动整个组：是/否
连续缩放时三个窗口和标签栏同步：是/否
最大化、切换标签、还原均正常：是/否
最小化后整组消失，任务栏恢复后整组返回：是/否
左/右 Snap 与脱离 Snap 正常：是/否
F11 隐藏标签栏，退出 F11 后恢复：是/否
跨显示器/DPI：通过/失败/未执行
按 p 后三个任务栏入口和原始独立位置恢复：是/否
退出码：
是否出现闪烁、屏外、焦点错误或 RECOVERY REQUIRED：
其他现象：
```

### 人工验收进度（2026-07-16）

第一轮真实 Chrome 验收已通过：

| 项目 | 实际结果 | 状态 |
| --- | --- | --- |
| 启动清单 | 3 个 Chrome 主窗口均显示为 `[normal]` | PASS |
| 标签与任务栏 | 建立 3 个外部标签，Windows 任务栏只保留 1 个 Chrome 入口 | PASS |
| 慢速/快速拖动 | 用户确认其余窗口与标签栏连续跟随；日志持续记录 `GROUP_GEOMETRY reason=move-or-resize` | PASS |
| 切换驱动成员 | 日志记录三个不同 HWND 的多次 `TAB_ACTIVATED`，切换后的成员可继续驱动组 | PASS |
| 连续缩放 | 用户确认三个窗口和标签栏同步，未出现停留、屏外或不可达窗口 | PASS |
| 暂停恢复 | 输入 `p` 后两个已删除入口均成功 `AddTab`，标签栏关闭且三个窗口恢复测试前独立位置 | PASS |
| 错误与退出 | 未出现 `[ERROR]` 或 `RECOVERY REQUIRED`；随后输入 `q` 正常结束 | PASS |

第一轮日志区间为 23:52:13～23:53:08。仍需执行最大化/还原、最小化/恢复、左/右 Snap、
F11；跨显示器与不同 DPI 只在具备相应硬件时执行，否则记录 `NOT RUN`。

第二轮窗口状态验收（2026-07-17）在最大化步骤发现两个阻断问题：外部标签栏与 Chrome
原生网页标签栏重叠，且 Chrome 右上角最小化、最大化、关闭按钮不可见，因此本轮立即判定
未通过，后续最小化、Snap 和 F11 结果不计入验收。根因是原实现保留了 Windows 原生
maximized 状态，同时试图用 `SetWindowPos` 缩小 Chrome；系统随后重新应用原生最大化矩形，
覆盖了为外部标签栏预留的顶部区域。

修复后最大化改为受管最大化：收到原生最大化请求后，先为全部成员建立恢复义务，再通过
`SetWindowPlacement` 清除 minimized/maximized 状态且不激活窗口，最后以
`DeferWindowPos` 将全部 Chrome 放到外部标签栏下方。再次点击最大化按钮会被解释为受管
还原；从受管最大化拖动标题栏则按普通拖动还原。最大化前、最小化前和 F11 前的组矩形使用
三个独立快照，避免“最大化后最小化”覆盖最终还原所需的普通矩形。临时顶层窗口自动测试
已经验证成员均为 native-normal、矩形位于标签栏下方、原生
caption/system menu/minimize/maximize 样式完整，并覆盖最大化、最小化、恢复以及再次点击后
的原矩形恢复。当时真实 Chrome 复测尚未完成。

同日新增的标签栏对齐、总宽度百分比、单标签宽度和自定义显示名称需求已写入 V2 设计：
Phase 5 实现配置模型、校验、迁移、持久化和基础布局，Phase 6 实现溢出及名称编辑交互；
这些外观配置不扩大 Phase 3 的验收范围。

修复后真实 Chrome 复测进度（2026-07-17）：用户确认整体表现正常，最大化覆盖、Chrome
原生网页标签不可用以及右上角按钮消失的问题均未再出现。受管最大化时按钮仍显示“最大化”
图标而不是原生“还原”图标，不影响再次点击执行还原；该非关键视觉差异后由用户决定移至 V3，
不再是 V2 发布门禁。用户在理解 Windows Snap 是窗口贴靠/分屏后，补充
确认左/右 Snap 也已通过。

最终日志证据（2026-07-17 00:46:55～00:50:26）：受管最大化三次进入和还原均回到同一普通
组矩形；左侧 Snap 收敛到约 `x=0～1294`，右侧 Snap 收敛到约 `x=1266～2560`，拖离后继续
正常同步。用户确认最大化/还原、整组最小化/恢复、F11 隐藏/恢复、左/右 Snap 与脱离 Snap
整体正常；Chrome 原生网页标签和系统按钮保持可用。输入 `p` 后 `AddTab`、标签栏关闭和三个
原始独立窗口矩形恢复成功，随后输入 `q` 正常结束，没有 `[ERROR]` 或
`RECOVERY REQUIRED`。跨显示器/DPI 未收到执行结果，记录为 `NOT RUN`。

Phase 3 当前结论：Debug/Release 12/12 自动测试及单显示器全部必需人工场景通过，状态标记为
`PASS`。受管最大化按钮图标差异不影响功能，已登记到 `docs/V3_BACKLOG.md`，不阻塞 V2。

## Phase 4：原子恢复、异常终止和故障注入

当前状态：`PASS`。

### 实现范围

- 新增独立于 V1 的版本化 `GroupRecoveryJournal`。V2 恢复日志位于
  `%LOCALAPPDATA%\ChromeTaskbarMerger\recovery-v2.tsv`，以临时文件和原子替换提交；只有磁盘
  提交成功后才更新内存状态；
- 在创建标签栏/成员标签、首次或后续移动窗口以及任务栏 `DeleteTab` 前持久化恢复义务；恢复
  每完成一个成员就持久化完成状态，任何一步失败都保留足够信息供下次重试；
- 每个成员记录 HWND、PID、TID、进程创建时间、类名、原始 `WINDOWPLACEMENT`、精确矩形、
  显示器设备名、显示器矩形和工作区矩形。恢复前再次验证全部身份及显示环境，陈旧或无关窗口
  不会被移动；
- 启动 V2 实验前先恢复可信旧会话；`--restore-all` 同时处理 V1 任务栏日志和可信 V2 任务栏/
  布局日志。损坏、不可信或显示环境不匹配的 V2 日志会阻止新管理并保留日志；
- 回退固定按“任务栏入口 → 原始窗口布局 → 外部标签栏”执行。正常恢复、异常清理和重复恢复均
  使用同一顺序；
- `--help`、实验输出及文件日志已明确 Phase 4 恢复路径和 V2 日志位置。

### 自动验收结果

| 项目 | 预期结果 | 实际结果 | 状态 |
| --- | --- | --- | --- |
| Debug CTest | V1、Phase 0～3 回归及 Phase 4 测试通过 | 13/13 通过 | PASS |
| Release CTest | V1、Phase 0～3 回归及 Phase 4 测试通过 | 13/13 通过 | PASS |
| 日志格式与边界 | 完整往返；拒绝未知版本、截断、重复、成员/文件超限 | 全部测试通过 | PASS |
| 原子持久化 | 替换失败不污染已提交内存状态，清理失败保留恢复义务 | 故障注入和重试通过 | PASS |
| 部分事务 | 标签栏、成员标签、布局意图和布局完成任一点失败均可继续恢复 | 逐点故障注入通过 | PASS |
| 身份安全 | HWND/PID/TID/创建时间/类名任一不符时不移动窗口 | 全部拒绝路径通过 | PASS |
| 显示环境安全 | 设备、显示器矩形或工作区不符时拒绝布局恢复 | 显示环境不匹配测试通过 | PASS |
| 临时 HWND 布局 | 精确恢复原始 placement/矩形；陈旧窗口安全跳过 | 真实临时顶层窗口通过 | PASS |
| 启动恢复与幂等 | 模拟崩溃后下次启动恢复；重复恢复为无操作 | 集成测试通过 | PASS |
| 安全真实预检 | 取消实验不修改真实 Chrome、任务栏或恢复日志 | 3 个真实 Chrome 列出后 `CANCEL`，退出码 0 | PASS |
| 全新便携构建 | 双配置、测试、安装和 ZIP 均成功 | EXE、INI、README、LICENSE 及 ZIP 完整 | PASS |

核心命令：

```powershell
cmake --build .\build-portable --config Debug
cmake --build .\build-portable --config Release
ctest --test-dir .\build-portable -C Debug --output-on-failure
ctest --test-dir .\build-portable -C Release --output-on-failure
```

### 必需人工验收：真实 Chrome 强制结束恢复

验收目标：确认程序在管理 3 个真实 Chrome 时被强制结束，任务栏和每个 Chrome 的原始独立
位置仍能由显式命令及下次启动安全恢复。

前置条件：

1. 保存 Chrome 中正在编辑的内容；完全退出托盘版 ChromeTaskbarMerger 和 WindowTabs；
2. 保留恰好 3 个普通、未最小化、未最大化、未 Snap 的 Chrome 主窗口，把它们放在三个明显
   不同的位置；
3. 测试中不要关闭 Chrome。任务管理器中只结束 `ChromeTaskbarMerger.exe`，绝对不要结束
   Chrome 或 Explorer；
4. 如果窗口处于 F11 全屏，先按 `F11` 退出后再开始。

第一轮——显式恢复：

1. 运行：

   ```powershell
   .\build-portable\Debug\ChromeTaskbarMerger.exe --v2-experiment
   ```

2. 确认三个窗口均显示 `[normal]`，输入 `V2-START`。确认出现 3 个外部标签、任务栏只保留
   1 个 Chrome 入口，并确认以下文件已经存在：

   ```powershell
   Test-Path "$env:LOCALAPPDATA\ChromeTaskbarMerger\recovery-v2.tsv"
   ```

   预期输出 `True`。不要编辑或删除该文件；
3. 打开任务管理器的“详细信息”，只结束当前测试的 `ChromeTaskbarMerger.exe`。控制台和外部
   标签栏消失是预期现象；此时 Chrome 可能仍重叠、任务栏暂时仍只有一个入口；
4. 在 PowerShell 运行：

   ```powershell
   .\build-portable\Debug\ChromeTaskbarMerger.exe --restore-all
   $LASTEXITCODE
   ```

   预期退出码为 `0`，3 个 Chrome 任务栏入口恢复，三个窗口分别回到测试前的三个独立位置；
5. 原样再运行一次 `--restore-all`。预期仍为 `0`，窗口位置不再变化；
6. 检查日志已清空活动义务：

   ```powershell
   Get-Content "$env:LOCALAPPDATA\ChromeTaskbarMerger\recovery-v2.tsv"
   ```

   预期只剩版本头和 `session\t0\t0`，没有成员恢复项。

第二轮——下次启动自动恢复：

1. 再次运行实验，输入 `V2-START`，确认任务栏只保留一个 Chrome 入口；
2. 再次通过任务管理器只强制结束 `ChromeTaskbarMerger.exe`；
3. 重新运行 `--v2-experiment`。预期程序先报告旧 V2 会话的任务栏和布局恢复成功，再列出三个
   位于原始独立位置的 `[normal]` Chrome；
4. 在确认提示输入 `CANCEL`。预期退出码为 `0`，不创建新组，三个任务栏入口和独立位置保持
   正常。

用户回报模板：

```text
第一轮强制结束后，--restore-all 退出码：
三个任务栏入口恢复：是/否
三个 Chrome 分别回到测试前独立位置：是/否
第二次 --restore-all 幂等且退出码为 0：是/否
恢复日志只剩空会话：是/否
第二轮重新启动时自动恢复旧会话：是/否
输入 CANCEL 后退出码为 0 且无残留：是/否
是否出现 RECOVERY REQUIRED、不可达窗口或无关窗口被移动：
其他现象：
```

### 异常时安全兜底

- 若 `--restore-all` 返回非 0，不要删除或编辑 `recovery-v2.tsv`；保留 Chrome 和原显示器布局，
  再运行一次同一命令；
- 若 Chrome 正处于 F11，先退出 F11 后重试；若测试中拔插显示器或任务栏工作区改变，请恢复到
  原显示环境后重试，严格显示校验会主动拒绝不安全移动；
- 若任务栏入口仍缺失，可在任务管理器中重启“Windows 资源管理器”，随后再次运行
  `--restore-all`；
- 日志位于 `%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log`。发生失败时保留
  该日志和 `recovery-v2.tsv`，不要结束 Chrome；只有在所有自动恢复尝试失败后才手动分开窗口。

### 人工验收实际结果（2026-07-17）

用户确认上述两轮全部符合预期：

| 检查项 | 实际结果 | 状态 |
| --- | --- | --- |
| 实验管理状态 | 3 个真实 Chrome 建立外部标签组，任务栏只保留 1 个入口 | PASS |
| 第一轮强制结束 | 只结束 `ChromeTaskbarMerger.exe` 后进入预期的待恢复状态 | PASS |
| 显式恢复 | `--restore-all` 退出码为 `0`，三个任务栏入口恢复 | PASS |
| 原始布局 | 三个 Chrome 分别回到测试前的三个独立位置 | PASS |
| 重复恢复 | 第二次 `--restore-all` 退出码仍为 `0`，窗口位置不再变化 | PASS |
| 恢复日志 | 完成后收敛为无成员恢复义务的空会话 | PASS |
| 启动恢复 | 第二轮强制结束后，重新运行实验会先自动恢复旧任务栏和布局 | PASS |
| 取消新会话 | 自动恢复后输入 `CANCEL`，退出码为 `0` 且无残留 | PASS |
| 安全性 | 未出现 `RECOVERY REQUIRED`、不可达窗口或无关窗口被移动 | PASS |

Phase 4 结论：Debug/Release 13/13 自动测试及两轮真实 Chrome 强制结束恢复均通过，状态标记为
`PASS`。Phase 5 的自动与调整后人工验收结果见下节。

## Phase 5：正式状态机、托盘、配置与登录启动迁移

当前状态：`PASS`。

新增需求登记（2026-07-17）：V2 必须提供严格二选一的标签提供方式。默认 `builtin` 使用
本项目自研标签栏和窗口组；可选 `windowtabs` 保留部分用户偏好的 WindowTabs 标签风格，本
项目只负责 V1 已验证的任务栏单入口。配置键为 `tab_provider=builtin|windowtabs`，可直接编辑
INI 或通过托盘单选菜单保存，完全退出并重启后生效。内置模式不得等待 WindowTabs；WindowTabs
模式不得同时创建内置标签或重排 Chrome 布局，WindowTabs 未就绪时任务栏入口保持可见并自动
等待。配置、状态机、托盘、迁移和两种真实运行模式均属于 Phase 5 的阻塞验收范围。

### 实现范围

- 无参数启动根据 `tab_provider=builtin|windowtabs` 严格选择一个托盘后端；默认内置模式托管
  Phase 4 标签、窗口组、生命周期、几何和原子恢复，不再显示控制台或要求 `V2-START`；
- WindowTabs 模式不创建内置标签、不重排 Chrome，只在 WindowTabs 就绪后应用 V1 任务栏
  单入口；缺失时恢复所有入口并自动等待；
- 两种托盘均显示当前提供方式，提供扫描、暂停、恢复、恢复全部、标签方式单选、登录启动、
  日志、关于和退出；标签方式切换先恢复当前会话，原子保存后重启生效；
- 状态机覆盖初始化、准备、提供器等待、管理、用户暂停、冲突、错误和恢复门禁；用户暂停具有
  粘性，恢复未完成时不能切换模式或普通恢复；
- 配置新增提供器、标签栏左/中/右对齐、总宽度百分比和单标签宽度；V1 缺键配置迁移为内置
  默认，旧键、注释和其他节保留；
- 新增版本化 `tab-names-v1.tsv` 技术原型。名称规则不使用 HWND，仅在进程路径、窗口类和精确
  标题唯一匹配时应用；歧义时使用 Chrome 原标题。该原型不是 profile 关联功能的完成证据；
  Phase 6 实现仅内存编辑，Phase 7 编码前先评估 profile 持久化可行性；
- 内置和 WindowTabs 后端共享单实例通知、启动旧会话恢复、`--restore-all` 和当前用户登录
  启动路径修复。

### 自动验收结果

| 项目 | 预期结果 | 实际结果 | 状态 |
| --- | --- | --- | --- |
| Debug CTest | V1、Phase 0～4 回归及 Phase 5 测试通过 | 14/14 通过 | PASS |
| Release CTest | V1、Phase 0～4 回归及 Phase 5 测试通过 | 14/14 通过 | PASS |
| 提供器状态机 | 内置独立准备/冲突；WindowTabs 等待/就绪/退出；用户暂停粘性 | 全部转换测试通过 | PASS |
| V1 配置迁移 | 缺少 V2 键时默认内置，旧键及有效值保留 | 测试通过 | PASS |
| 配置校验 | 提供器、对齐、百分比、像素的合法/非法/重复值 | 范围与逐项警告测试通过 | PASS |
| 原子配置保存 | 切换提供器去除重复项、保留注释和其他节；失败明确返回 | 成功与失败路径通过 | PASS |
| 标签栏配置 | 左/中/右和 60% 几何正确，180px 上限及 DPI 缩放正确 | 纯布局测试通过 | PASS |
| 自定义名称模型 | 版本/UTF-8/重复拒绝/原子保存；不使用 HWND；歧义不套用 | 新增名称规则测试通过 | PASS |
| 登录启动 | 临时注册表覆盖中的启用、关闭、路径修复和失败回滚 | 既有回归继续通过 | PASS |
| 单实例命令 | 同步恢复与异步扫描消息到达同一托盘实例 | 既有回归继续通过 | PASS |
| 启动双日志恢复 | 先恢复 V1 任务栏日志，再恢复 V2 任务栏和布局；陈旧 HWND 安全跳过且重复执行幂等 | 新增启动恢复测试通过 | PASS |
| 全新便携构建 | 双配置、测试、安装、配置清单和 ZIP 成功 | 四文件目录及 ZIP 完整 | PASS |
| 源码检查 | `/W4` 无警告，`git diff --check` 通过 | 两种配置均无警告，差异检查通过 | PASS |

核心命令：

```powershell
.\scripts\build-portable.ps1
ctest --test-dir .\build-portable -C Debug --output-on-failure
ctest --test-dir .\build-portable -C Release --output-on-failure
```

### 必需人工验收

测试前保存 Chrome 中正在编辑的内容，完全退出已安装/便携版 ChromeTaskbarMerger 和
WindowTabs。保留 3 个普通、未最小化、未最大化、未 Snap 的 Chrome 主窗口，分别放在三个
明显不同的位置。测试过程中若发生异常，不要结束 Chrome。

先把开发配置复制到 Debug EXE 旁边：

```powershell
Copy-Item .\config\ChromeTaskbarMerger.ini `
  .\build-portable\Debug\ChromeTaskbarMerger.ini -Force
```

#### A. 默认内置标签模式

1. 确认 Debug 目录 INI 包含：

   ```ini
   tab_provider=builtin
   tab_strip_alignment=center
   tab_strip_width_percent=60
   tab_width_px=180
   start_with_windows=false
   ```

2. 运行：

   ```powershell
   .\build-portable\Debug\ChromeTaskbarMerger.exe
   ```

3. 预期没有控制台和确认提示，通知区域出现一个图标；右键状态为“管理中”，标签提供方式勾选
   “内置标签（默认）”；三个 Chrome 自动成为内置标签组，任务栏只剩一个 Chrome 入口；
4. 确认标签栏在组顶部居中，约占组宽 60%，每个标签明显短于 Phase 4 的旧默认长标签；逐个切换
   三个标签，拖动/缩放任意活动 Chrome，确认组继续同步；
5. 右键选择“暂停管理”，预期内置标签栏消失、三个任务栏入口和三个测试前独立位置恢复，状态
   为“已暂停（用户）”；等待数秒，确认不会自行恢复；
6. 选择“恢复管理”，预期重新建立内置标签和一个任务栏入口；再次双击 EXE，预期不会出现第二
   个托盘图标，现有实例收到重新扫描通知；
7. 在内置模式运行时启动 WindowTabs。预期本程序先恢复任务栏和独立位置，状态变成
   “已暂停（WindowTabs 冲突）”，不会同时保留两套标签；完全退出 WindowTabs 后，内置模式
   自动重新建立组。

#### B. WindowTabs 标签模式

1. 在托盘“标签提供方式（重启生效）”选择“WindowTabs 标签”。预期当前内置会话先完整恢复，
   INI 写成 `tab_provider=windowtabs`，并提示重启生效；
2. 从托盘正常退出，先启动 WindowTabs，再重新运行 Debug EXE；
3. 预期托盘状态为“管理中”，只看到 WindowTabs 标签，不出现本程序内置标签，Chrome 不被本
   程序重排，任务栏仍只有一个 Chrome 入口；
4. 完全退出 WindowTabs。预期本程序恢复三个任务栏入口并进入“等待 WindowTabs”；重新启动
   WindowTabs 后自动回到管理中和一个任务栏入口；
5. 用户主动“暂停管理”后，再退出/启动 WindowTabs，预期状态保持用户暂停，不自动覆盖；选择
   “恢复管理”后才重新应用任务栏规则；
6. 从托盘把标签提供方式重新选回“内置标签（默认）”，正常退出并重启，确认回到 A 模式。

#### C. 布局配置和登录启动

1. 暂停并退出程序，把 INI 改成 `tab_strip_alignment=left`、
   `tab_strip_width_percent=40`、`tab_width_px=120`，重新运行；预期标签栏靠左且变窄，标签仍可
   点击，Chrome 原生标签和右上角系统按钮无遮挡；也可改 `right` 重启验证靠右；
2. 托盘勾选“随 Windows 登录自动启动”，确认 INI 变成 `start_with_windows=true`，并运行：

   ```powershell
   Get-ItemProperty `
     HKCU:\Software\Microsoft\Windows\CurrentVersion\Run `
     -Name ChromeTaskbarMerger
   ```

   预期指向当前 Debug EXE 并带 `--autostart`；
3. 注销并重新登录 Windows。预期程序自动出现于通知区域，并按当前选择的标签方式进入正确状态；
   用户因当前不便注销，将此项明确移到 Phase 7 最终便携包验收；
4. 验收后取消托盘勾选，再次运行上面的注册表命令应提示该值不存在；确认 INI 恢复
   `start_with_windows=false`，最后从托盘正常退出。

用户回报模板：

```text
内置模式无控制台自动建组、任务栏 1 个入口：是/否
内置标签居中 60%，切换、拖动、缩放正常：是/否
暂停恢复三个入口和三个独立位置，且保持用户暂停：是/否
恢复管理及第二实例重新扫描正常：是/否
内置模式启动 WindowTabs 后安全冲突暂停，退出后自动恢复：是/否
WindowTabs 模式无内置标签/不重排，任务栏 1 个入口：是/否
WindowTabs 退出后恢复入口并等待，重启后自动管理：是/否
WindowTabs 模式用户暂停具有粘性：是/否
标签提供方式切换、保存、重启和回切正常：是/否
left/right、40%、120px 配置正常且不遮挡 Chrome：是/否
登录启动项启用、路径/参数、INI 同步和关闭清理可逆：是/否
实际注销/重新登录（移至 Phase 7）：未执行/是/否
是否出现 RECOVERY REQUIRED、不可达窗口、双重标签或退出残留：
其他现象：
```

### 最终人工验收结果（2026-07-17）

| 项目 | 实际结果 | 状态 |
| --- | --- | --- |
| 默认内置模式 | 无控制台自动建立 3 标签，任务栏 1 个入口，切换、拖动、缩放正常 | PASS |
| 暂停、恢复与单实例 | 恢复 3 个入口和原独立位置；用户暂停保持；恢复管理及第二实例扫描正常 | PASS |
| WindowTabs 模式 | 无内置标签、不重排；退出后恢复入口并等待，重启后自动管理 | PASS |
| 提供器二选一 | 切换前完整恢复，INI 原子保存，重启生效并可回切内置模式 | PASS |
| 布局配置 | 两轮 `40%/120px` 与 left/right 后恢复默认 `center/60%/180px`，Chrome 原生 UI 无遮挡 | PASS |
| 初始非普通窗口 | 切回内置模式时至少一个 Chrome 非 normal，安全拒绝建组；恢复普通状态后成功，不是崩溃或恢复损坏 | PASS（安全门禁） |
| 垂直拖动缺陷 | 首轮发现工作区边界约束时驱动 Chrome 可能与另两个成员错位；修复并增加最终移动事件与纠偏日志 | FIXED |
| 拖动修复复测 | 三个 Chrome 分别完成上下、边界、快速、斜向及左右拖动；用户确认未再错位，日志显示精确纠偏 | PASS |
| 登录启动开关 | Run 值为带引号的当前便携 EXE 加 `--autostart`，INI 同步 `true`；取消后 Run 删除且 INI 为 `false` | PASS |
| 实际注销/重新登录 | 本 Phase 当时延后；已于 Phase 7 使用最终候选目录完成真实注销、登录和清理 | PASS IN PHASE 7 |
| 安全性 | 未出现 `RECOVERY REQUIRED`、不可达窗口、双重标签或无法清理的 Run 项 | PASS |

垂直拖动根因：组高度接近工作区高度时，上下拖动会被约束到上下边界；约束后的组矩形可能与
当前组矩形相同，旧逻辑因此跳过同步，却没有检查鼠标驱动的 Chrome 实际矩形是否仍偏移。修复
后 `WindowGroupArrangementRequired` 同时比较组矩形和驱动窗口实际内容矩形，并监听
`EVENT_SYSTEM_MOVESIZEEND` 在松开鼠标后做最终同步。`GROUP_GEOMETRY_CORRECTION` 只在检测到
偏移时记录 HWND/身份、实际矩形和目标矩形。新增合成边界与移动结束测试后，全新 Debug/Release
14/14 CTest 通过。

Phase 5 结论：调整后的全部自动与人工验收通过，状态标记为 `PASS`。当时延后的实际注销自动启动
观察已在 Phase 7 使用最终候选目录完成并通过。

### 异常时安全恢复

- 首选托盘“恢复全部任务栏和窗口布局”；若托盘不可用，运行：

  ```powershell
  .\build-portable\Debug\ChromeTaskbarMerger.exe --restore-all
  $LASTEXITCODE
  ```

- 预期退出码为 `0`。不要删除 `%LOCALAPPDATA%\ChromeTaskbarMerger\recovery-v1.tsv` 或
  `recovery-v2.tsv`；不要结束 Chrome；
- 若任务栏入口仍缺失，在任务管理器中重启“Windows 资源管理器”，再运行一次
  `--restore-all`；
- 若登录启动测试后程序路径失效，可执行以下命令关闭当前用户启动项：

  ```powershell
  Remove-ItemProperty `
    HKCU:\Software\Microsoft\Windows\CurrentVersion\Run `
    -Name ChromeTaskbarMerger -ErrorAction SilentlyContinue
  ```

- 日志位于 `%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log`。失败时保留日志和
  两个恢复文件并告诉 Codex，不要强制结束 Chrome。

## Phase 6：自定义名称内存编辑及交互完善

当前状态：`PASS`。

新增需求登记（2026-07-17）：双击内置标签主体后允许输入自定义字符串，完整支持中文。
`Enter` 确认、`Esc` 取消，空字符串恢复 Chrome 实时标题；不修改 Chrome 窗口标题。名称只保存
在 ChromeTaskbarMerger 当前进程的内存中，只要程序不重启且窗口完整身份仍有效，就不能被网页
标题变化、暂停/恢复或重新建组覆盖；重启程序后恢复实时标题。WindowTabs 提供器不使用此功能。

自动验收至少覆盖双击命中、Unicode/中文、确认/取消、空值、长度上限、标题变化、身份失效、
进程级模型重建和“不写持久化文件”。人工验收使用三个真实 Chrome 输入英文、中文和混合名称，
验证暂停/恢复后保留、程序重启后清除。名称编辑严格只属于 `tab_provider=builtin`；选择
`windowtabs` 时不显示内置标签，因此不提供、也不尝试模拟名称编辑或保存。

实现与自动验收（2026-07-17）：Debug/Release 均为 14/14 CTest 通过。已覆盖内存名称的中文、
长度、清空、标题变化和完整身份隔离；原生编辑框的双击、确认和取消；窄栏滚动溢出；键盘前后
循环；`TaskbarCreated` 清理/重应用及重复消息幂等；已有负坐标显示器、100%～200% DPI、事件
风暴和动态成员回归继续通过。候选包统一位于 `dist/ChromeTaskbarMerger`。

首次窄栏人工测试记录（2026-07-17）：用户认为从 3 个增加到 5 个后显示异常且程序退出。日志和
随后只读 `--list` 证明系统中实际存在 6 个可见 `Chrome_WidgetWin_1` 主窗口（2 个网页窗口、4 个
`New tab`），旧候选版在识别第 6 个后执行了完整安全回滚；恢复日志已清空，托盘进程仍在异常暂停，
不是进程崩溃。该体验已修正：运行期第 6 个及后续窗口不再解散原有 5 窗口组，而是保持独立任务栏
入口并提示；纯选择策略覆盖“保留原组、前台优先填空位、数量回落”和零上限边界，Debug/Release
14/14 回归通过。初始启动超过 5 个时仍安全拒绝建组。修正版通过验收后归入唯一标准便携目录
`dist/ChromeTaskbarMerger`，不保留按 Phase 命名的分发目录。

人工验收按以下顺序执行；任何一步出现窗口不可达或无法恢复时立即停止，使用托盘“恢复全部”并
保留日志：

1. 完全退出当前 ChromeTaskbarMerger，确认 Chrome 原位置和任务栏按钮恢复；运行 Phase 6 候选
   包，使用三个普通 Chrome 窗口建立内置组；
2. 分别双击三个标签主体，输入 `Work`、`工作账户`、`Work 工作`。验证真实中文输入法候选和上屏
   正常，`Enter` 保存；另做一次编辑后按 `Esc`，名称应不变；清空其中一个名称并确认，应恢复
   当前 Chrome 标题；双击关闭按钮区域不得打开编辑框；
3. 改变三个 Chrome 当前网页标题，随后从托盘暂停再恢复管理；三个已保存名称应继续存在，清空的
   标签应继续跟随实时标题；`Ctrl+Alt+PageUp/PageDown` 应循环切换三个 Chrome；暂停管理期间
   快捷键不得被本程序占用；
4. 临时把 `tab_strip_width_percent=25`、`tab_width_px=80` 后重启候选包。若标签发生溢出，在标签
   栏上滚动鼠标滚轮应能看到并点击全部标签，活动标签应自动滚入可见区域；请先用 `--list` 或
   肉眼确认恰好 5 个主窗口。可选再创建第 6 个，预期原有 5 窗口组不解散，第 6 个保持独立任务栏
   入口并出现说明；关闭任一受管成员后，空位可由独立窗口补入。随后恢复原配置；
5. 管理中从任务管理器“Windows 资源管理器”选择“重新启动”。预期托盘图标和 Chrome 单任务栏
   入口自动恢复，标签切换、拖动、最小化/恢复和“恢复全部”仍正常；日志应出现
   `TASKBAR_CREATED_REBUILT`；
6. 验证任务栏单入口点击最小化/恢复整组；打开 Chrome 下载/上传文件选择框或其他模态窗口，关闭
   对话框后标签切换和窗口组仍正常；若有双显示器、不同缩放或虚拟桌面，再执行移动/切换观察；
   没有相应环境则记录 `NOT RUN`；
7. 再次设置一个中文名称后，彻底退出并重新运行 ChromeTaskbarMerger。名称必须消失并恢复动态
   Chrome 标题；`tab-names-v1.tsv` 的时间和内容不得因本轮交互式改名而改变；
8. 空闲观察至少 5 分钟，确认 CPU 正常、内存和句柄/GDI 没有持续单调增长；最后使用托盘退出，
   确认所有窗口位置和任务栏按钮恢复。

### 最终人工验收结果（2026-07-17）

| 项目 | 实际结果 | 状态 |
| --- | --- | --- |
| Unicode 名称编辑 | 三个真实标签完成英文、中文和混合名称输入；确认、取消、清空和关闭区隔离符合预期 | PASS |
| 名称生命周期 | 网页标题变化和暂停/恢复后内存名称保留；进程重启后恢复动态标题 | PASS |
| 不写持久化 | 交互式改名后 `tab-names-v1.tsv` 仍不存在，日志只记录名称长度而不记录内容 | PASS |
| 快捷键 | 管理中 `Ctrl+Alt+PageUp/PageDown` 循环切换，暂停后不再由本程序占用 | PASS |
| 5 窗口与溢出 | `25%/80px` 下 3 个可见标签可用滚轮访问全部 5 个；恢复正常宽度后 5 个同时可见 | PASS |
| Explorer 与任务栏 | Explorer 重建后托盘和 Chrome 单入口恢复；单入口可最小化/恢复整组 | PASS |
| 模态窗口 | Chrome 文件选择框期间安全延迟同步，关闭后自动恢复，标签和窗口组继续正常 | PASS |
| 资源与退出恢复 | 空闲 CPU、内存和对象计数观察正常；托盘退出后窗口位置和任务栏按钮完整恢复 | PASS |
| 扩展显示矩阵 | 本轮未额外配置双显示器、跨 DPI 和虚拟桌面组合；合成坐标/DPI 自动回归通过 | NOT RUN（不阻塞） |
| 安全性 | 修正版未再出现原 6 窗口导致整组回滚的问题；无窗口不可达或 `RECOVERY REQUIRED` | PASS |

Phase 6 结论：Debug/Release 14/14 自动测试及必要真实桌面验收全部通过，状态标记为 `PASS`。
后续便携构建只使用 `dist/ChromeTaskbarMerger`，不再创建按 Phase 命名的子目录。

## Phase 7：Chrome profile 名称持久化与正式发布

当前状态：`PASS / RELEASED AS 2.0.0`（2026-07-17）。profile 持久化、实际注销/重新登录启动和
最终便携候选均通过。最大化按钮图标专项已移至 V3，不阻塞本 Phase。

第二阶段属于高级功能，编码前必须先评估并向用户报告顶层 Chrome HWND 与本地 profile 的可靠
关联能力，包括本地 profile 与 Google 账户差异、同 profile 多窗口、访客/无痕、多个数据目录、
重命名/删除和隐私边界。只有用户确认评估结论和降级方案后才能开发。不得用 HWND、网页标题、
进程路径或窗口顺序伪造 profile 关联；若无法可靠实现，保留 Phase 6 的仅内存名称并明确记录。

若批准实现，需验证至少两个真实 Chrome profile 的中文名称在 ChromeTaskbarMerger/Chrome
重启后正确恢复且不串用。本 Phase 同时执行 Phase 5 延后的实际注销/重新登录启动观察，并完成
其余 `2.0.0` 最终便携包验收与发布门禁。

### 已实现与自动验收（2026-07-17）

- 用户确认“有限可靠、严格回退”方案后完成开发；详细评估见
  `docs/V2_PROFILE_NAME_PERSISTENCE.md`；
- 新增 `persist_tab_names_by_profile=false`，可由托盘或 INI 修改，重启生效，仅适用于内置标签；
- 唯一匹配链为 Chrome User Data 目录、目标 HWND 的 UIA 头像按钮、Local State profile 显示名和
  Preferences 创建时间；任一步失败都保留 Phase 6 内存名称；
- 同 profile 多窗口共享名称；profile 删除重建因创建时间变化不会继承旧名称；
- `profile-tab-names-v1.tsv` 只保存 SHA-256 键和 Unicode 自定义名，采用版本头与原子替换；损坏
  文件整份拒绝且本次运行不覆盖；
- 合成解析、哈希隔离、Unicode、原子存储、损坏/重复键、配置迁移和回退测试通过；Debug CTest
  15/15 通过；生产解析器对当前 5 个真实 Chrome 窗口执行只读探测，5/5 为 `matched`，未输出或
  修改 profile 名称、路径和账户数据。

### 本轮人工验收步骤（profile 名称持久化）

1. 从托盘完全退出旧程序，打开便携目录 `ChromeTaskbarMerger.ini`，确认 `tab_provider=builtin`，将
   `persist_tab_names_by_profile=true`；也可以启动候选版后从托盘勾选“按 Chrome profile 保存标签
   名称（重启生效）”，然后再次完全退出并启动；
2. 打开至少两个**显示名称不同**的普通 Chrome profile，各保留一个普通窗口；不要使用无痕或访客
   窗口作为本步骤样本；
3. 启动候选版，确认窗口组成内置标签组且任务栏只有一个 Chrome 入口；分别双击两个标签，输入
   两个不同的中文名称并按 `Enter`；
4. 完全退出 ChromeTaskbarMerger 后重新启动，确认两个名称恢复且没有互换；再关闭并从相同 profile
   重建 Chrome 窗口，确认对应名称仍恢复；
5. 若同一个 profile 有两个窗口，修改其中一个名称，确认两个标签同步为相同名称；清空任一标签并
   确认后，两个窗口都恢复各自实时 Chrome 标题，重启程序后仍不再加载旧名称；
6. 临时把两个普通 profile 改成相同显示名称，或打开一个无痕/访客窗口；改名应仍在当前程序内存中
   有效，程序不得退出。日志应出现 `TAB_NAME_PROFILE_FALLBACK status=...`，不得出现实际 profile
   名称、路径、邮箱或用户输入名称；
7. 退出程序，将 `persist_tab_names_by_profile=false` 后重启；双击改名仍应可用，但再次重启后名称
   清除。选择 `tab_provider=windowtabs` 时不应出现内置名称编辑或读写 profile 名称文件。

### profile 名称持久化人工验收结果（2026-07-17）

| 项目 | 结果 | 状态 |
| --- | --- | --- |
| 两个普通 profile | 两个不同中文名称保存成功，完全退出并重启后正确恢复且未互换 | PASS |
| 无痕回退 | 内存改名当次有效；日志为 `profile-not-matched`；重启不恢复且文件仍为两个条目 | PASS |
| 关闭持久化 | 普通 profile 名称不加载，内存改名重启清除，持久化文件内容和时间未变化 | PASS |
| 重新启用 | 原两个普通 profile 名称恢复，无痕窗口继续显示实时标题 | PASS |
| 隐私 | 日志只记录状态和长度，未出现用户输入名称、profile 名称、路径或账户信息 | PASS |
| 同 profile 多窗口 | 当前 Chrome 操作方式无法建立第二个样本；共享键语义有自动覆盖，不阻塞核心门禁 | N/A |

本功能人工验收结论：`PASS`。日志证据显示启动时两个普通窗口为 `matched`、无痕窗口为
`profile-not-matched`；持久化文件版本头正确，始终只有两个不同的合法 SHA-256 键。

上述 profile 项通过后，已继续完成最终便携包和登录启动验收；最大化图标专项已移至 V3。

### Phase 7 实际注销/重新登录启动验收

重要：注销 Windows 会关闭当前用户的全部应用，无痕窗口内容也会永久丢失。执行前保存所有文档、
网页表单和下载进度，并关闭无痕窗口。本测试使用最终候选目录
`dist/ChromeTaskbarMerger/ChromeTaskbarMerger.exe`，不要改用 Debug EXE。

#### 注销前准备

1. 确认 `tab_provider=builtin`，完全退出 WindowTabs；Chrome profile 名称持久化可保持当前设置；
2. 从托盘勾选“随 Windows 登录自动启动”，确认通知提示启用成功；
3. 在仓库根目录运行只读验收脚本，预期全部为 `PASS`：

   ```powershell
   .\scripts\verify-login-startup.ps1 -Stage Enabled
   ```

4. 从托盘正常退出 ChromeTaskbarMerger，确认 Chrome 任务栏入口和窗口位置恢复；再次运行上面的
   脚本但改用 `-Stage ReadyToLogout`，预期 Run 项仍正确且进程数为 0；
5. 脚本通过后，从 Windows 开始菜单选择当前用户的
   “注销”。不要由 Codex 或脚本远程执行注销。

#### 重新登录后观察

1. 登录同一个 Windows 用户，等待 10～30 秒；通知区域应自动出现 ChromeTaskbarMerger，Release
   版本不应出现控制台窗口；
2. 在仓库根目录运行：

   ```powershell
   .\scripts\verify-login-startup.ps1 -Stage AfterLogin
   ```

   预期全部为 `PASS`：只有一个进程、路径指向 `dist/ChromeTaskbarMerger`，命令行包含
   `--autostart`；
3. 如果 Chrome 尚未启动，托盘状态应为“管理中（等待 Chrome）”，而不是用户暂停或错误暂停；
4. 启动两个之前已命名的普通 Chrome profile。预期自动建立内置窗口组，任务栏只有一个 Chrome
   入口，两个持久化中文名称正确恢复且不互换，无需 WindowTabs；
5. 再次手工双击候选 EXE，预期不会出现第二个托盘实例，进程数量仍为 1，现有实例只执行重新扫描；
6. 检查日志没有 `RECOVERY REQUIRED`、启动恢复失败或持续重复启动记录。

#### 实际结果（2026-07-17）

| 项目 | 结果 | 状态 |
| --- | --- | --- |
| 启用与注销前门禁 | `Enabled` 与 `ReadyToLogout` 脚本均全部通过；Run 命令和候选路径正确，退出后进程为 0 | PASS |
| 真实登录启动 | 注销并重新登录同一用户后，通知区域自动出现 Release GUI，无控制台窗口 | PASS |
| 启动实例 | `AfterLogin` 全部通过；路径为 `dist/ChromeTaskbarMerger`，命令含 `--autostart`，进程数为 1 | PASS |
| 无 Chrome 等待 | 启动时没有 Chrome，程序保持管理中并等待后续窗口，没有进入用户暂停或错误暂停 | PASS |
| 后续自动管理 | 打开两个普通 profile 后自动建立内置组，任务栏只有一个 Chrome 入口，两个持久化名称正确恢复且未互换 | PASS |
| 单实例 | 再次启动候选 EXE 没有创建第二个进程，现有实例收到重新扫描通知 | PASS |
| 日志与 profile 探测 | 无恢复失败；首个窗口有一次 UIA 头像未就绪的瞬时回退，随后只读探测为 2/2 `matched` | PASS |
| 验收后清理 | `Cleanup` 全部通过；INI 为 `start_with_windows=false`，当前用户 Run 项不存在 | PASS |

登录启动专项结论：`PASS`。瞬时 UIA 回退没有被缓存，窗口就绪后自动重试成功，符合保守失败
回退设计；启用、真实注销/登录、后续自动管理、单实例和关闭清理的完整链路均通过。

### 最终便携包自动门禁（2026-07-17）

| 项目 | 实际结果 | 状态 |
| --- | --- | --- |
| 全新构建 | 独立空 `build-portable` 完成 x64 Debug/Release `/W4` 构建，无警告 | PASS |
| 自动测试 | Debug 与 Release 均为 15/15 CTest 通过 | PASS |
| 版本与 PE | 文件/产品版本 2.0.0；machine `8664`；Windows GUI subsystem `2` | PASS |
| 静态运行库 | 导入表没有 VCRUNTIME、MSVCP 或 ucrtbase | PASS |
| 便携目录 | 唯一 `dist/ChromeTaskbarMerger`，只含 EXE、默认 INI、README、LICENSE | PASS |
| ZIP | `ChromeTaskbarMerger-2.0.0-portable-x64.zip` 只含单一目录下的上述四个文件 | PASS |
| 默认配置 | `builtin`、profile 持久化关闭、登录启动关闭，其余 V2 默认值正确 | PASS |
| 哈希 | ZIP 与 EXE SHA-256 重新计算后均与 `SHA256SUMS.txt` 一致 | PASS |
| 发布文档 | 默认英文及中文 README、便携说明、双语 Release Notes 已更新 | PASS |

自动发布门禁结论：`PASS`。最终候选随后执行下一节真实便携人工抽查。

#### 登录启动验收后清理（已通过）

1. 从托盘取消勾选“随 Windows 登录自动启动”；
2. 确认 INI 为 `start_with_windows=false`，并运行：

   ```powershell
   .\scripts\verify-login-startup.ps1 -Stage Cleanup
   ```

   预期全部为 `PASS`，Run 项不存在；
3. 从托盘正常退出，确认任务栏和窗口布局恢复。若登录后未自动启动，先手工运行候选 EXE 并保留
   `%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log`，不要删除恢复日志；必要时执行
   `ChromeTaskbarMerger.exe --restore-all` 后再报告现象。

### 2.0.0 最终便携候选人工抽查

本轮只验证版本升级和重新打包没有引入发布级回归。WindowTabs、Explorer 重建、强制结束恢复、
登录启动、profile 持久化及长时间资源观察已经使用同一份功能代码分别通过，不重复执行。

1. 完全退出 WindowTabs，准备三个可安全移动的普通、非最小化 Chrome 窗口；确认没有旧的
   ChromeTaskbarMerger 实例；
2. 运行 `dist/ChromeTaskbarMerger/ChromeTaskbarMerger.exe`。预期没有控制台，托盘“关于”显示
   `2.0.0`、开发人员“杨云召”和正确的 GitHub 地址；
3. 等待最多两个扫描周期。预期出现三个内置标签，任务栏只有一个 Chrome 入口；逐个点击标签，
   再拖动任一 Chrome，并做一次最小化/恢复和最大化/还原，组行为应正常；最大化按钮视觉图标
   不作为 V2 阻塞项；
4. 双击一个标签输入中文名称并确认；从托盘暂停，预期三个任务栏入口和独立布局恢复；再恢复管理，
   预期重新建组三标签且本进程内名称仍在；
5. 再次启动同一 EXE，预期仍只有一个托盘进程，现有实例只重新扫描；
6. 从托盘正常退出。预期三个 Chrome 的任务栏入口和组前布局完整恢复，进程数为 0，日志没有
   `RECOVERY REQUIRED` 或恢复失败。

### 最终便携候选人工结果（2026-07-17）

| 项目 | 实际结果 | 状态 |
| --- | --- | --- |
| 版本与 About | Release 无控制台；About 为 2.0.0、杨云召和正确 GitHub 地址 | PASS |
| 三窗口核心 | 三个内置标签、任务栏单入口、切换与整组拖动正常 | PASS |
| 窗口状态 | 最小化/恢复和最大化/还原正常；图标视觉差异按计划移至 V3 | PASS |
| 名称与暂停 | 中文名称可用；暂停恢复三个入口与独立布局，恢复管理后进程内名称仍在 | PASS |
| 单实例 | 再次启动 EXE 没有第二个实例，现有实例正常重新扫描 | PASS |
| 正常退出 | 三个任务栏入口和组前布局完整恢复，无发布阻塞恢复错误 | PASS |

用户确认以上全部符合预期。Phase 7 结论：`PASS / APPROVED FOR 2.0.0 RELEASE`。结合 Phase 0～6、
profile 持久化、真实登录启动和自动发布门禁结果，V2 已满足正式发布标准。

发布后复核：带注释标签 `v2.0.0` 剥离后指向提交 `27dde1c`；正式 Release 非草稿、非预发布并为
Latest；ZIP 和 SHA256 清单资产状态均为 `uploaded`，GitHub ZIP 摘要与本地 SHA-256 一致。发布地址：
<https://github.com/yangyunzhao/ChromeTaskbarMerger/releases/tag/v2.0.0>。
