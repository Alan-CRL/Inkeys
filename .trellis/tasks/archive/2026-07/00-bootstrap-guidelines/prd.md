# Bootstrap Task: Establish Inkeys Trellis Specs

## Goal

基于 Inkeys 仓库的实际 Solution、项目文件、配置、第一方源码和资源，建立第一版可供未来 AI 开发任务使用的 Trellis Spec，并移除初始化模板中不适用于本项目的 Web backend/frontend 层。

## Scope

- 调研 `InkeysRepo.sln`、`Inkeys/Inkeys.vcxproj`、`PptCOM/PptCOM.csproj`、`Timeout/InkeysTimeout.sln`、构建配置和第一方源码。
- 记录原生桌面程序、PPT/WPS COM 互操作、共享思考指南及仓库研究结论。
- 审计架构与兼容性结论，区分直接确认、合理推断、待确认和历史/兼容代码。
- 整理 Bootstrap 任务元数据与 context manifests，使其只引用实际存在的 Spec/research。
- 删除没有项目规则且不再被任务上下文引用的 backend/frontend 模板层。

## Out of Scope

- 修改任何 C++、C#、Solution、vcxproj、csproj、依赖、资源产物或构建行为。
- 安装依赖、递归分析 `Vcpkg/`、执行产品构建或运行测试。
- 解决审计中发现的潜在工程、线程、COM 或资源生命周期风险。
- Git staging、commit、push 或任务归档。

## Evidence Rules

- 每个重要结论必须指向实际源码、Solution、项目或配置文件。
- “代码当前这样写”不能自动升级为未来强制规范。
- 工程配置存在不能写成平台已经运行验证。
- 不确定内容必须标为待确认；历史/兼容路径不得与当前推荐路线混写。
- 涉及 UI 路线、配置系统、平台支持或 PPT/WPS 支持范围的未决事项，按 Spec 中的实施前决策门处理。

## Deliverables

- `.trellis/tasks/00-bootstrap-guidelines/research/repository-overview.md`
- `.trellis/spec/index.md`
- `.trellis/spec/native-desktop/`
- `.trellis/spec/ppt-interop/`
- `.trellis/spec/guides/`
- 本任务的 `task.json`、`implement.jsonl` 和 `check.jsonl`

## Acceptance Criteria

- [x] 仓库概览覆盖用途、目录、项目职责、入口、核心模块、图形、输入、墨迹、构建、依赖和代码习惯。
- [x] 关键架构结论具有可追溯的文件、工程、类或函数证据。
- [x] Spec 明确区分直接确认、合理推断、待确认和历史/兼容内容。
- [x] native-desktop 与 ppt-interop 索引覆盖全部有效专题文档。
- [x] UI、配置、平台支持及 PPT/WPS 范围具有阻塞式实施前决策门。
- [x] Bootstrap task context 只引用有效 Spec 和 research 文件。
- [x] 不适用且无有效运行上下文引用的 backend/frontend 空层已删除。
- [x] `get_context.py --mode packages` 只暴露有效 Spec 层，且任务、链接和 JSONL 校验通过。
- [x] 未修改产品源码、工程、依赖或构建行为，未暂存或提交 Git。

## Context

- Spec 根入口：`.trellis/spec/index.md`
- 原生桌面入口：`.trellis/spec/native-desktop/index.md`
- PPT/WPS 互操作入口：`.trellis/spec/ppt-interop/index.md`
- 通用思考指南：`.trellis/spec/guides/index.md`
- 完整调研：`.trellis/tasks/00-bootstrap-guidelines/research/repository-overview.md`

## Completion Note

本任务保持 `in_progress`，等待开发者人工审阅并自行决定暂存、提交和归档时机。本轮不得执行 Git commit。
