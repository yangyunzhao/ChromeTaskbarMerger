# ChromeTaskbarMerger V3.1 测试记录

> 目标版本：`3.1.0`
>
> 当前状态：`RELEASED / FROZEN`

本文档只记录 V3.1 的实际命令、环境、结果和人工回报。稳定范围与门禁见
[V3.1 需求](../docs/V3_1_REQUIREMENTS.md)，滚动版本规则见
[V3 路线图](../docs/V3_ROADMAP.md)。

## 1. 视觉输入

2026-07-17 用户提供三张本地参考图片：普通顶部吸附、最大化顶部融合，以及原生网页标签较多时
允许被 WindowTabs 标签局部覆盖的效果。图片包含网页标题，当前不加入 Git；测试记录只保存已经
确认的几何和交互结论。

用户明确排除 F11；V3.1 的“全屏”口语统一解释为 Windows 窗口最大化。

## 2. 问题项总览

| 问题项 | 自动验收 | 人工验收 | 当前状态 |
| --- | --- | --- | --- |
| V3.1-A 视觉/状态/布局模型 | PASS | 不需要真实修改 | `PASS` |
| V3.1-B 普通顶部吸附 | PASS | PASS（图 10） | `PASS` |
| V3.1-C 原生最大化与组状态 | PASS | PASS | `MANUAL PASS` |
| V3.1-D 最大化顶部叠加 | PASS | PASS（图 11） | `PASS` |
| V3.1-E 恢复、回归与候选 | PASS | 用户批准收口 | `PASS` |

## 3. V3.1-A 基线与可行性记录

当前 2.0.0 代码在检测到成员原生最大化后，通过 `SetWindowPlacement` 把所有成员标准化为
`SW_SHOWNOACTIVATE`，要求 `IsZoomed == FALSE`，再手工排列到为外部标签栏预留高度的工作区。
因此 Chrome 正确地认为窗口处于普通状态，并显示“最大化”图标。V3.1 首选方案是不再进行该
标准化，让成员保持真实原生最大化并把标签叠加到顶部安全区域。

### 自动验收结果

2026-07-17 完成：

- 纯布局测试覆盖普通顶部吸附、最大化叠加、负坐标、100%/200% DPI 输入、右上角按钮保留区、
  left/center/right 标签内容对齐和 1/2/3/5 标签滚动；
- 本进程临时顶层窗口验证三个成员同时保持原生 `IsZoomed != FALSE`，切换活动 owner 不会把其他成员
  标准化，最小化后恢复仍为原生最大化，`SW_RESTORE` 后全组回到最大化前矩形；
- 临时标签窗口使用逐像素透明 `WS_EX_LAYERED` 表面且不再使用一位 Win32 region；透明外角不参与
  命中，最大化叠加右边缘小于工作区右边缘；
- Debug 与 Release 各执行 15/15 CTest，通过命令解析、配置迁移、启动项、profile 名称、生命周期、
  任务栏事务、持久恢复、manifest 和窗口组全部回归；
- Release 候选 `ProductVersion=3.1.0`，静态运行库便携目录、ZIP 与 SHA-256 已生成。

候选文件：

```text
dist\ChromeTaskbarMerger\ChromeTaskbarMerger.exe
dist\ChromeTaskbarMerger-3.1.0-portable-x64.zip
dist\SHA256SUMS.txt
```

当前 EXE SHA-256：
`14F9FF84F55128834DB5539403AA68D059A940A27F5500452AF0736660614F8B`。

最终 ZIP SHA-256：
`5C02F80FF2364FA06C103EE1C63813E89B12325C74B3D5FC364DD3F9EF6B3F16`。

2026-07-18 发布 [`v3.1.0`](https://github.com/yangyunzhao/ChromeTaskbarMerger/releases/tag/v3.1.0)：
非草稿、非预发布并标记为 Latest；远端 ZIP 摘要与本地最终摘要一致，带注释标签指向
`ac34732d91a6a31a91f34039cf54bb328b086609`。

### 恢复边界

自动集成测试只操作本进程创建的临时窗口，不修改真实 Chrome、真实任务栏或启动注册表。真实
Chrome 测试必须使用托盘“恢复全部任务栏和窗口布局”或候选 EXE 的 `--restore-all`；失败时保留
`%LOCALAPPDATA%\ChromeTaskbarMerger` 下的日志和恢复文件。

## 4. 人工验收步骤

### 4.1 首轮结果：功能通过，视觉未通过

2026-07-18 用户完成真实 Chrome 验收并确认功能没有问题。图 4、5 与 WindowTabs 图 1、2 对比后，
标签边缘明显模糊/马赛克，标签高度、完整硬边框和间距使其更像独立按钮组，精细度不足。根因是：

- `SetWindowRgn` 只提供整数像素的一位裁剪，圆角没有半透明抗锯齿；
- GDI `FillRgn`/`FrameRgn` 和字体关闭符号无法达到 Chrome/WindowTabs 的合成质量；
- 16 像素小图标在高 DPI 下可能被直接放大；
- 38 逻辑像素高度和逐标签完整轮廓偏厚。

修正版使用 Direct2D/DirectWrite/WIC，关闭按钮使用矢量线，标签高度改为 26 逻辑像素，标签组按
实际标签数和单标签最大宽度收紧；Debug/Release 15/15 回归通过。参考截图仍只保存在本地
`screenshoots/`，不加入 Git。

图 6、7 的第二轮复验确认清晰度和整体外观已有明显改善，但图 1 与图 6 对比仍有两项未通过：

- 两个标签之间仍露出白色不透明底板和一段水平底边，不是 WindowTabs 的自然透明缝隙；
- 标签窗口的 DWM 外层阴影向右超过 Chrome 窗口边缘。

第二修正版把标签窗口改为逐像素透明分层窗口，非标签像素的 alpha 为零；标签使用 Chrome 风格
肩部曲线，只描绘顶部和两侧轮廓，不绘制逐标签底边，并由自身透明边缘完成抗锯齿。DWM 外层
阴影和窗口矩形圆角已关闭，标签可见像素不得超出 Chrome 右边界。Debug/Release 15/15 自动回归
通过，集成测试额外确认 `WS_EX_LAYERED` 生效且未恢复一位 Win32 region。

图 8、9 的第三轮截图判定第二修正版失败：尖锐肩部和缺少层次感使其明显不如图 6、7，而且右侧
依旧越过 Chrome。分析确认 Chrome 普通窗口的 `GetWindowRect` 含约数个像素的不可见缩放边框，
此前将越界归因于标签 DWM 阴影并不准确。第三修正版撤销肩部曲线，恢复图 6、7 的紧凑圆角，
保留逐像素透明和无底边描边并加入轻微内部阴影；标签横向范围改用
`DWMWA_EXTENDED_FRAME_BOUNDS` 修正。Debug/Release 15/15 回归通过，新增纯布局与真实临时窗口
测试覆盖普通窗口不可见边框和最大化安全区。

### 4.2 视觉修正复验：普通窗口与原生最大化

1. 完全退出当前 ChromeTaskbarMerger 和 WindowTabs，保留 1～3 个**普通、非最小化、非最大化**的
   Chrome 主窗口；
2. 运行 `dist\ChromeTaskbarMerger\ChromeTaskbarMerger.exe`；
3. 普通状态检查标签靠右、圆角标签本身紧贴 Chrome；两个标签之间和标签外侧应真正透明，不得有
   白色矩形底板或逐标签水平底边，最右侧可见像素不得越过 Chrome 右边缘；随后切换标签并左右、
   上下拖动组窗口；
4. 点击当前 Chrome 的最大化按钮。预期所有成员都最大化，标签进入 Chrome 顶部靠右位置，右上角
   三个按钮均能看到和点击，最大化按钮自然显示 Windows/Chrome 的“还原”图标；
5. 依次点击内置标签切换成员，再执行最小化并从任务栏恢复。预期仍为最大化组且任务栏只有一个
   Chrome 入口；
6. 点击原生还原按钮。预期全部成员回到最大化前的统一普通布局，标签回到 Chrome 上边缘外侧；
7. 在最大化状态从标题区域向下拖动还原，再继续拖动。预期组回到普通状态并继续跟随，无持续跳动。

如任一步异常，先右键托盘选择“恢复全部任务栏和窗口布局”；若托盘不可用，在仓库根目录执行：

```powershell
.\dist\ChromeTaskbarMerger\ChromeTaskbarMerger.exe --restore-all
```

保留 `%LOCALAPPDATA%\ChromeTaskbarMerger\logs\ChromeTaskbarMerger.log`，暂不继续后续轮次。

### 4.3 最终结果与冻结

2026-07-18，用户提交图 10、11 并确认第三修正版“好看很多”。与 WindowTabs 图 1、2 对比后：

- 普通窗口的标签间隙透明、无白色底板或底边横线，右侧与 Chrome 实际可见边界对齐；
- 最大化标签进入 Chrome 顶部并正确避让最小化、还原和关闭按钮；
- 圆角、轻阴影、文字和图标的整体精细度达到 V3.1 接受标准；
- 先前真实 Chrome 功能测试无异常，最终干净 Debug/Release 各 15/15 通过。

用户随后明确决定 V3.1 到此完成。验收状态先更新为 `PASS / FROZEN`，正式发布后更新为
`RELEASED / FROZEN`；以后发现的新 BUG 或视觉/易用性优化统一进入 V3.2。F11 仍不在 V3.1
范围；原生网页标签很多时被内置标签局部覆盖仍是
已接受限制。截图 1～11 含桌面和网页标题，只作为本地验收输入，不提交 Git。
