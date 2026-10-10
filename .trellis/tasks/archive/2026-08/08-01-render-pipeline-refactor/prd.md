# 渲染管线重构

## Goal

作为**未来预定的大型架构任务**，统一并重构 Inkeys 桌面渲染与窗口管线：弃用 UI2/旧悬浮栏路径，拆分更多独立窗口，把窗口管理从 `IdtWindows.cpp` 模块化迁到 C++20 module，把 PPT 控件迁入 UI3 体系，最终让 **UI3 Bar、Settings、PPT 控件、以及未来的背景窗口（白板/定格等）** 共享同一 **D3D11.1 WARP device**，并通过**公共串行绘制调度**依次渲染各模块。

本任务**不在当前迭代立即实施**；属于 epic 级预定工作。完成后才推进既有调查任务 `07-17-investigate-unified-ui-d3d11-pipeline` 的后续实施。

## Status

- **预定 / Future epic**
- 当前状态：`planning`（`--no-start`，不激活为本会话当前任务）
- 预计启动：整体性能优化与相关 UI3 收口完成之后

## Background

- UI3 Bar 已走共享 D3D11/D2D 设备与 layered 呈现；Settings 已有独立 D3D11/ImGui 路径演进；PPT 控件、白板/定格等仍分散在传统窗口与绘制路径中。
- 旧 UI2 / `IdtFloating` 等路径仍可能与实验开关并存，增加双维护成本。
- 统一 device 后的核心难点不只是“创建同一个 D3D 设备”，更在于：
  - 多 HWND 的 **唤醒 / 休眠 / 脏区**
  - 各模块 **消息线程 vs 渲染线程** 归属
  - **串行调度** 下的优先级、跳帧与阻塞隔离
  - 设备丢失、DPI、resize、最小化与退出生命周期

## Scope（目标阶段，未来实施）

按用户给定主线拆为五段（可再拆子任务）：

1. **弃用 Inkeys2 / UI2**  
   收敛或移除旧悬浮栏/UI2 路径，避免与 UI3 双轨长期并存。

2. **拆分为更多窗口**  
   将功能面拆到更清晰的独立 HWND（Bar / Settings / PPT / 背景窗等），明确每个窗口的职责与生命周期。

3. **适配并模块化窗口管理**  
   以现有 `IdtWindows.cpp` 为基线，适配多窗口管理，并迁移到 `.cppm` module 边界；统一注册、显隐、Z 序、DPI、关闭语义。

4. **PPT 控件迁移到 UI3**  
   PPT 放映相关控件进入 UI3 渲染/交互约定，不再长期游离于旧绘制栈。

5. **统一 D3D11.1 WARP 串行渲染**  
   - 共享同一 D3D11.1 WARP device（及约定的 context 使用规则）  
   - 覆盖：UI3 Bar、Settings、PPT 控件、未来背景窗口（白板/定格等）  
   - 提供公共绘制入口，**串行**依次调用各模块绘制  
   - 重点设计：模块唤醒、脏区合并、帧节奏、跨模块状态隔离

## Hard Problems（必须先设计）

- 各窗口/模块的 **唤醒条件** 与 **空闲休眠**，避免一个模块拖垮全局帧率
- 串行绘制顺序、超时/降级、Present 与 D2D `BeginDraw/EndDraw` / ImGui 帧边界协调
- 多模块共享 device 时的 **线程归属** 与禁止并发路径
- 背景窗口（白板/定格）与交互窗的层级、命中、穿透关系
- Win 现代 ARM64 与历史兼容边界（若仍需声明）

## Dependencies / Ordering

| 相关任务 | 关系 |
|---------|------|
| 全局性能优化（待开或并行） | **先于** 本 epic 大规模开工 |
| `07-26-button-cursor-lighting` | 可在本 epic 前收口；用户要求先做性能优化再完成其余 polish |
| `07-17-investigate-unified-ui-d3d11-pipeline` | **本 epic 完成后再推进/承接** 其调查结论的实施；调查本身仍可提前补齐文档 |
| `07-26-draw-attribute-pen-type-buttons` | 与本 epic 无强依赖，可独立收口 |

## Requirements（预定）

- R1：给出并执行 UI2/旧路径弃用清单与兼容开关策略。
- R2：完成多窗口拆分的窗口清单、所有权模型与 Z 序/任务栏语义。
- R3：`IdtWindows` 能力迁入 module，并支撑 UI3/Settings/PPT/背景窗注册。
- R4：PPT 控件迁入 UI3 渲染与输入约定，保留放映联动必要行为。
- R5：建立共享 D3D11.1 WARP device 与公共串行 `RenderFrame`（名称可调整）调度。
- R6：为每个客户端定义：何时请求渲染、如何合并脏区、如何在无输入时休眠。
- R7：覆盖 resize/DPI/设备丢失/退出等生命周期。
- R8：完整 Solution 目标平台构建与关键路径手工验收。

## Acceptance Criteria（预定，实施阶段再细化）

- [ ] UI2/旧双轨路径有明确弃用或隔离策略，默认产品路径不再依赖其渲染。
- [ ] 多窗口拆分后，各 HWND 职责清晰，窗口管理走 module 化 API。
- [ ] PPT 控件在 UI3 约定下可显示与交互（范围以子任务 PRD 为准）。
- [ ] UI3、Settings、PPT、背景窗共享同一 D3D11.1 WARP device 约定，串行绘制可运行。
- [ ] 唤醒/休眠机制可证明：单模块活动不会无界拉高全局占用；空闲可静默。
- [ ] 生命周期与失败路径（设备丢失、关闭顺序）有测试/验收记录。
- [ ] 完整 `InkeysRepo.sln` 目标配置构建通过。

## Out of Scope（当前）

- 本迭代不开始大规模改窗口/设备代码。
- 不在本任务内完成全部性能优化（性能优化作为前置/并行工作）。
- 不在本任务内替代 `07-17` 的调查文档产出；但实施应消费其结论。

## Notes

- 这是 **巨大任务**，实施前应拆成多个可独立验收的子任务（弃用 UI2、窗口管理 module、PPT 迁入、共享 device、串行调度、背景窗等）。
- 当前仅预定与写清边界；`task.py start` 前补齐 `design.md` / `implement.md` 与子任务图。
