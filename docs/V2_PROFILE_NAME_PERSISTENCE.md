# V2 Chrome profile 标签名称持久化评估与实现

评估日期：2026-07-17
结论：**只能有限实现，但可以在严格失败回退的前提下作为可选功能提供。** 该结论与降级方案已向用户说明并获得确认，随后才进入 Phase 7 编码。

## 1. 身份语义

本功能关联的是当前 Windows 用户下的**本地 Chrome profile**，不是 Google 登录账户。同一个 Google 账户可能出现在多个本地 profile，本地 profile 也可能未登录 Google；程序不读取或保存邮箱、GAIA ID、同步状态或浏览数据。

Chrome 官方文档说明每个 profile 是 User Data 目录下的一个子目录：[User Data Directory](https://chromium.googlesource.com/chromium/src/+/main/docs/user_data_dir.md)。Chromium 源码也会持久化 `profile.creation_time`：[profile_impl.cc](https://chromium.googlesource.com/chromium/src/+/HEAD/chrome/browser/profiles/profile_impl.cc)。

## 2. 有限可靠的映射链

仅当以下链路全部成功且结果唯一时，程序才认为窗口已匹配：

1. 对 Chrome 浏览器进程只读查询命令行；存在 `--user-data-dir` 时使用其绝对路径，否则只接受已知 Chrome/Chromium 渠道的默认 User Data 目录；
2. 通过 Windows UI Automation 在目标顶层 HWND 内查找唯一、可见的 `AvatarToolbarButton`，只读取其可访问名称；
3. 只读解析该 User Data 目录的 `Local State`，要求可访问名称与 `profile.info_cache` 中恰好一个非临时 profile 的显示名称完全相同；
4. 只读解析匹配 profile 的 `Preferences`，取得持久的 `profile.creation_time`；
5. 使用 `SHA-256(normalized user-data-root + profile-directory + creation-time)` 生成不透明的 64 字符十六进制键。

Chrome UIA 暴露属于可能随 Chrome 更新变化的接口，Chromium 也将 Windows UIA 支持描述为持续开发中的能力：[UI Automation](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/accessibility/browser/uiautomation.md)。因此此功能不能被描述为无条件可靠；它的可靠性来自“唯一匹配才使用，其他情况全部回退”，而不是猜测窗口顺序或标题。

## 3. 支持与回退矩阵

| 场景 | 行为 |
| --- | --- |
| 普通 profile，UIA 名称唯一，元数据完整 | 使用持久化名称 |
| 同一 profile 的多个窗口 | 共享一个名称；任一窗口改名后，本次运行内其他同 profile 窗口同步 |
| profile 仅重命名 | 哈希键不变，名称继续关联 |
| profile 删除后以同目录重建 | `creation_time` 改变，旧名称不会套用 |
| 两个本地 profile 显示名称相同 | 判定歧义，只使用 Phase 6 内存名称 |
| 无痕、访客、临时 profile | 不持久化，只使用内存名称 |
| UIA、命令行、Local State 或 Preferences 不可读 | 不持久化，只使用内存名称 |
| 未知 Chrome 渠道、相对自定义数据目录 | 不猜测，只使用内存名称 |
| 持久化文件损坏或加载失败 | 整份拒绝，不覆盖原文件；本次运行只使用内存名称 |
| 保存失败 | 当前改名仍保留在内存中；日志记录不含 profile 信息的失败原因 |
| `tab_provider=windowtabs` | 不启用名称编辑，也不加载或写入 profile 名称 |

## 4. 配置、存储和隐私

功能默认关闭，只有内置标签提供器支持：

```ini
persist_tab_names_by_profile=false
```

可以直接修改 INI，或从托盘勾选“按 Chrome profile 保存标签名称（重启生效）”。必须完全退出并重启 ChromeTaskbarMerger 后生效。

启用后使用独立版本化文件：

```text
%LOCALAPPDATA%\ChromeTaskbarMerger\profile-tab-names-v1.tsv
```

文件仅包含版本头、SHA-256 profile 键和用户输入的 Unicode 名称，采用 UTF-8 与原子替换。文件不包含 User Data 路径、profile 目录名、Chrome profile 显示名、Google 账户、网页标题或 HWND。日志只记录状态码、名称长度和同 profile 窗口数量，不记录名称内容或 profile 身份。

旧的 `tab-names-v1.tsv` 是 Phase 5 技术原型，不参与 Phase 7 交互式名称的读取或写入。

## 5. 已完成自动证据

- 合成 Local State/Preferences 覆盖 Unicode、临时 profile、无效 JSON、创建时间校验；
- 哈希测试覆盖路径大小写稳定性、不同 profile 与删除重建隔离；
- 版本化存储覆盖中文往返、重复键整份拒绝、原始路径拒绝、原子保存和清除；
- 配置覆盖默认关闭、大小写布尔值、非法值回退、去重和原子更新；
- 2026-07-17 在当前真实 Chrome 环境执行只读探测，5/5 个可管理窗口均返回 `matched`；探测未输出或修改 profile 名称、路径和账户数据；
- Debug/Release 全量 CTest 均为 15/15 通过，`/W4` 无警告。

## 6. 人工验收结果

2026-07-17 用户使用两个显示名称不同的真实普通 Chrome profile 完成验收：两个不同中文名称保存成功，完全退出并重新启动 ChromeTaskbarMerger 后均正确恢复且未互换。无痕窗口改名仅在内存中有效，日志记录 `profile-not-matched`，重启后未恢复且持久化文件仍只有两个普通 profile 条目。关闭持久化后，内存改名在本次运行有效并在重启后清除，文件内容和修改时间保持不变；重新启用并重启后，原两个普通 profile 名称正确恢复，无痕窗口仍回退实时标题。

同一 profile 多窗口在用户当前操作方式下无法建立第二个样本，且不属于不同 profile 不串用的核心发布门禁，人工结果记为 `N/A`；哈希键共享语义已有自动测试和代码路径覆盖，不阻塞本功能验收。profile 名称持久化功能的自动与必要人工验收均通过。
