# C1 主栏布局模型、持久化与旧开关迁移

## Goal

让主栏布局成为可编辑、可持久化的四段模型（A1 / B1 / More / A2），并让真实主栏按持久化配置显示。是 C3、C4 的数据基础。总体设计见父任务 `design.md` §3。

## Requirements

- B 区运行时改读 `UI.Bar.ExtensionButtons`：`MoreBoundary` 之前为 B1（全部上栏，不设两项上限），之后为 More。
- 新增可重复的用户分割线，可出现在 A1、B1、A2，不折叠相邻项；交界分割线保持运行时注入。
- A 区校验：剥离用户分割线后非分割线成员仍须恰为默认集合，否则整区回默认；通过时保留用户分割线位置；校验表达方式为后续“A 区多支笔”预留。
- 16 个组件允许重复；每个条目有独立的按钮实例与本地状态。
- Setting 在 B1 + More 中恰好一次，缺失补到 More 末尾。
- 16 个 `ShortcutButton.*` 开关一次性导入：为 true 的按注册顺序，前两个进 B1、其余进 More；导入后开关写回 false 并保存；之后不再读取。
- 提供纯函数编辑操作与约束判定（父 design §3.3 约束表）、`AutoAdd`（B1 组件数 <2 进 B1，否则进 More，分割线与 Setting 不计数）、`RemoveAllOf`、`RemoveUserDividers`、`Reset`。
- 应用入口：写三段配置 → `config.Write()` → 唤醒真实主栏重新 `Load()` 并渲染。
- 组件页旧开关 UI 在 C4 替换之前临时保留可用（开关改为调用新的添加/删除入口，或在 C1 内保持旧行为直到 C4），以保证中间状态可构建可运行。

## Acceptance Criteria

- [ ] 默认配置下真实主栏外观不变：Bar golden 与基线逐字节一致。
- [ ] headless 单测覆盖：约束表全部组合、`AutoAdd` 阈值、Setting 唯一性、A 区用户分割线校验与整区回退、开关导入幂等、重置结果等于新装默认。
- [ ] 手工改写配置为 B1 含 4 个组件 + 2 条相连分割线、More 含重复组件时，真实主栏按配置显示，重启后保持。
- [ ] 原“最多两项上栏”相关离屏断言按新合同更新并通过。
- [ ] `.trellis/spec/native-desktop/configuration-i18n-and-assets.md` 的“UI3 Bar 分区布局配置合同”同步更新。
- [ ] `InkeysRepo.sln` `Debug | ARM64` 构建通过。

## Dependencies

无前置。C3、C4 依赖本任务。
