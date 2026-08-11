# UI3 基于变化的脏区系统

## Goal

将 UI3 Bar 的脏区从“所有可见内容边界”改为“自上次成功呈现后实际发生视觉变化的区域”，缩小 D2D 清除/绘制与 `UpdateLayeredWindowIndirect` 的提交范围，同时保持动画、动态边缘光和调试覆盖层无残影、无漏刷。

本任务是 `.trellis/tasks/08-01-render-pipeline-refactor` 之前可独立交付的性能优化，不属于该 future epic 的子任务。

## Background

- 当前 `Bar.RenderLoop.cpp` 每帧收集所有可见 Shape/SVG/PNG/Word 为 `predicted`，再与上次成功呈现边界合并，因此单个控件动画也可能刷新整套可见 UI。
- UI3 Bar 使用全屏大小分层窗口、D2D 全局 dirty clip 与 `UpdateLayeredWindowIndirect::prcDirty`；后两者最终都只能消费一个合并后的矩形。
- 当前调试模式绘制 FPS 文字和红色脏区框；它们自身也必须纳入清除和提交，但红框不能反向污染所展示的业务脏区。

## Requirements

- R1：新增内部、可 headless 测试的 `BarDirtyRegionTracker`，按稳定视觉键保存上次成功呈现边界、本帧边界、变化标记和未提交 damage。
- R2：标准 Shape/SVG/PNG/Word 按控件追踪；父布局、自绘预览/弹窗/色板等使用功能组兜底。
- R3：变化项的 damage 必须覆盖旧边界与新边界，正确处理移动、缩放、出现和消失；多个区域最终合并、裁剪到窗口范围。
- R4：动画推进结果与所属控件/功能组关联；直接拖动和非标准连续视觉显式标脏。存在呈现请求但没有登记 damage 时，回退为全窗口脏区。
- R5：主光和鼠标光按旧/新影响范围标脏，包含渐变半径、Gaussian 外扩和抗锯齿余量。
- R6：dirty tracker 只在 GetDC、ULW、ReleaseDC、EndDraw 全部成功后提交；租约跳帧和普通重试保留累计 damage，设备切换或呈现失败要求全窗口恢复。
- R7：FPS 文字与红框作为独立调试视觉追踪；关闭调试时清除旧内容，红框不参与业务 damage 的计算。
- R8：保留现有可见内容边界收集逻辑，停用其“每帧默认脏区”用途并注明未来动态窗口尺寸用途；本任务不改变窗口尺寸、窗口原点或命中坐标。
- R9：保持 `BarPresentDecision` 的需求、优先级和失败退避职责，不新增 UI3 Bar 对外 API。

## Acceptance Criteria

- [ ] 单个标准控件动画的调试红框只覆盖该控件的旧/新范围，不再默认覆盖全部可见 UI。
- [ ] 面板展开/收起、换边、粗细/色板自绘内容和整栏拖动均无残影、无漏刷。
- [ ] 动态主光/鼠标光的 damage 跟随旧/新影响范围，静止内容不被无条件并入。
- [ ] 调试 FPS 文字每帧进入 damage；开关调试后旧文字和红框能被清除。
- [ ] 首帧、未分类请求、device generation 变化和呈现失败均能安全全脏恢复。
- [ ] headless 测试覆盖 tracker 的边界、事务和回退行为，现有动画/帧节奏/PresentDecision 测试继续通过。
- [ ] ARM64 Host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64` 通过。

## Out of Scope

- 调整 UI3 Bar HWND 尺寸、窗口原点、Z 序、命中坐标或多显示器策略。
- 修改传统 UI2/`IdtFloating`、Settings、PPT 控件或主画板的脏区系统。
- 将 ULW 单矩形提交改造为多矩形呈现协议。

