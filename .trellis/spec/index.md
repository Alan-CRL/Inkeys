# Inkeys3-Draw3 Trellis Spec

本目录记录 Inkeys3-Draw3 当前已经存在的工程边界与开发约束。项目是单仓库、单 Visual Studio C++ 应用，不存在 Web frontend/backend、数据库或服务端层。

## Project Snapshot

- 平台：Windows 桌面，原生 Win32 窗口；项目级正式兼容目标包含 Windows 7 SP1 + KB2670838。
- 语言：C++20 modules 与 HLSL Shader Model 5.0。
- 图形：D3D11、DXGI、DirectComposition、DWM、UpdateLayeredWindow。
- 墨迹：Google Ink Stroke Modeler 与 Kalman prediction。
- 入口：`inkStrokeModelerTest/main.cpp`。
- 主工程：`inkStrokeModelerTest/inkStrokeModelerTest.vcxproj`。
- 当前输入范围：鼠标左键单笔。
- 当前验证方式：解决方案/Shader 构建、D3D Debug Layer 检查和基础人工场景；没有自动化测试工程。

> **待验证**：项目级 Windows 兼容目标不代表当前测试程序的 DirectComposition、DWM 或 ULW 路径已经在该系统上完成验证。

## Evidence And Divergence Handling

不同材料承担不同职责：

- 当前编入解决方案的源码、HLSL 和 `.vcxproj/.rc` 配置说明当前测试程序的现有行为。
- `AGENTS.md` 和 `.trellis/spec/` 记录当前开发约束与已确认项目规则。
- `README.md` 提供项目概览。
- `Inkeys_D3D11_Ink_Renderer_Refactor_Plan_Phase1_Update.md` 记录历史设计、阶段计划和当时决策。
- 历史入口、旧 renderer 与 `ResTest/` 只作为历史或参考材料。

源码和阶段说明不一致时，不得自动选择其中一方作为最终规范。必须在任务或 [Bootstrap Decisions](./open-questions.md) 中标记差异，并由明确需求或专门架构决定确认下一步。

## Spec Layers

| Layer | Scope |
|---|---|
| [Native](./native/index.md) | C++ 模块、Win32 输入、墨迹建模、D3D11 资源、三层画布和 presenter |
| [Shaders](./shaders/index.md) | CPU/GPU 结构布局、寄存器、HLSL、混合语义和 shader 构建链 |
| [Guides](./guides/index.md) | 跨层影响分析与搜索复用检查 |
| [Bootstrap Decisions](./open-questions.md) | 已确认项目规则、待验证能力与后续架构任务 |

## Repository Boundaries

- `inkStrokeModelerTest/draw3/` 是当前自研核心，规范示例优先来自这里。
- `inkStrokeModelerTest/additional/` 与 `inkStrokeModelerTest/lib/` 默认属于第三方/外部来源；原则上不直接修改，必要补丁必须独立记录原因和上游来源。
- `ResTest/DirectInkPresenter/` 是独立参考解决方案，不参与主解决方案构建；暂时保留，普通功能任务不得顺手删除。
- `.vcxproj` 中 `main2.cpp`、`main3.cpp`、`renderer2.h`、`shader.hlsl` 是不参与编译的残留 `None` 引用，对应文件当前不存在；用途和清理时机均待专门任务确认。
- `.cso`、`.aps`、平台输出目录、IDE 缓存和中间 HLSL 副本属于生成或构建产物，不作为手工编辑目标。
