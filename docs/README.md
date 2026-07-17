# ChromeTaskbarMerger 文档索引

本目录保存项目的开发规范、版本需求和随便携包分发的说明。用户使用方法仍以
仓库根目录的 [README.md](../README.md) 和
[README.zh-CN.md](../README.zh-CN.md) 为准。

## 核心文档

| 文档 | 用途 | 当前状态 |
| --- | --- | --- |
| [Codex 开发与验收规范](CODEX_DEVELOPMENT_GUIDE.md) | 规定 Codex 在各版本中的开发、自动验收、人工验收、记录和 Git 行为 | 生效 |
| [V1 需求与设计](V1_REQUIREMENTS.md) | 记录任务栏单入口版本的范围、技术决策、阶段结果和正式版基线 | `1.0.0` 已发布 |
| [V2 需求与设计](V2_REQUIREMENTS.md) | 设计默认不依赖 WindowTabs 的内置窗口标签组，也允许选择 WindowTabs 标签，并保留 V1 任务栏能力 | Phase 0～5 已通过；Phase 6～7 计划中 |
| [便携版说明](PORTABLE_README.md) | 随 Release 便携包分发的简要使用与恢复说明 | `1.0.0` |

## 测试证据

- [V1 手工测试计划与已记录结果](../tests/manual_test_plan.md)
- [V2 测试计划与实际结果](../tests/manual_test_plan_v2.md)

## 信息边界

- 版本需求文档描述稳定目标、设计约束和阶段验收门槛。
- 测试计划保存实际命令、环境、现象、退出码和人工回报。
- README 面向最终用户，不承担完整开发日志的职责。
- Git 历史保存已经完成的逐步实施细节；不在需求文档中重复粘贴全部历史日志。
