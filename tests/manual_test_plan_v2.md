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
| 1 | 内部标签与任务栏单入口核心可行性 | 未执行 | 必须 | `NOT RUN` |
| 2 | 事件驱动窗口与标签生命周期 | 未执行 | 必须 | `NOT RUN` |
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

Phase 0 结论：自动验收全部通过，不需要人工验收，可以进入 Phase 1。Phase 1 尚未开始。

## Phase 1～7

尚未开始。进入每个 Phase 后，本文件将记录实际命令、自动结果、必要的人工步骤、用户
回报和恢复方法，不提前把计划项目写成已通过。
