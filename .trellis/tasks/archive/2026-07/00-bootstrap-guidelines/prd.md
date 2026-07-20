# Bootstrap Guidelines

## Goal

基于当前源码、工程配置和现有架构说明，生成 Inkeys3-Draw3 第一版项目说明与 Trellis Spec，使后续开发和审查遵循真实的 Windows/C++20/D3D11 约束。

## Scope

- 扩充根目录 `README.md`。
- 用项目实际的 `native` 与 `shaders` 层替换初始化时生成的 Web 模板。
- 为模块边界、运行时数据流、资源生命周期、CPU/GPU 契约、构建和质量检查提供源码依据。
- 调整通用思考指南，使其覆盖本项目的跨 CPU/GPU、跨线程回调和透明呈现边界。
- 记录维护者确认的兼容目标、第三方边界、文档差异、实验参数、质量门槛、历史资产、renderer 暴露和 prediction 持久化规则。

## Out of Scope

- 修改业务源码、HLSL、工程行为或依赖。
- 安装、升级依赖或生成构建产物。
- 解决现有架构差异、补测试或执行提交。

## Evidence

- `AGENTS.md`
- `inkStrokeModelerTest.sln`
- `inkStrokeModelerTest/inkStrokeModelerTest.vcxproj`
- `inkStrokeModelerTest/main.cpp`
- `inkStrokeModelerTest/draw3/*.cppm`
- `inkStrokeModelerTest/draw3/*.cpp`
- `inkStrokeModelerTest/ink.hlsli`
- `inkStrokeModelerTest/inkVertexShader.hlsl`
- `inkStrokeModelerTest/inkPixelShader.hlsl`
- `Inkeys_D3D11_Ink_Renderer_Refactor_Plan_Phase1_Update.md`

第三方源码、参考工程、生成文件、构建输出、缓存和依赖目录仅用于识别边界，不作为本项目编码模式的主要依据。

## Status

- [x] 生成项目说明
- [x] 生成 native 层规范
- [x] 生成 shaders 层规范
- [x] 加入真实文件和符号示例
- [x] 移除不适用的 Web 模板
- [x] 将维护者确认的 8 项架构规则写入对应 Spec
- [x] 将无法由当前代码证明的能力保留为“待验证”
- [x] 将运行时自动化测试转交 `07-19-draw3-runtime-validation`，并把 Windows 7、D3D Debug Layer 安装和持久化协议明确保留为外部长期待办

## Acceptance Criteria

- `.trellis/spec/` 与当前仓库结构一致。
- 每项关键规则可追溯到源码、工程配置、AGENTS 或现有项目说明。
- Spec 索引与最终文件集合一致。
- 不保留未填写章节或数据库、React、TypeScript 等不适用规范。
- 已确认项目规则与待验证运行能力明确分开。
- 除文档与 Trellis 任务元数据外没有文件变化。
