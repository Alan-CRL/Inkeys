# 设置窗口随绘制模式加入画布 Owner 链

## Goal

解决绘制模式下顶层画布遮挡设置窗口、导致设置窗口无法点击的问题；设置窗口在绘制时加入 Drawpad owner 链并位于画布之上，在选择模式下恢复为独立普通窗口。

## Background

- 设置窗口当前由 Window Service 的独立线程创建，是无 owner 的顶层 `WS_POPUP`。
- 设置窗口固定保留 `WS_EX_APPWINDOW`，并清除 `WS_EX_NOACTIVATE`、`WS_EX_TOOLWINDOW`、`WS_EX_TOPMOST` 等不符合普通可交互窗口的样式。
- 画笔、橡皮、图形和选择等工具切换最终统一经过 `SyncDraw3State()`。

## Requirements

- 绘制模式定义为 `StateModeSelect != StateModeSelectEnum::IdtSelection`，覆盖画笔、橡皮和图形等所有非选择工具。
- 绘制模式下，把设置窗口设置为 Drawpad 的 owned top-level popup；不得改为 `WS_CHILD`。
- 选择模式下清除设置窗口 owner，并使其退出画布的 topmost owner 链。
- owner 切换必须在设置窗口所属线程内执行，保持 Window Service 的线程所有权约束。
- owner 切换应幂等；失败时不得遗留半切换状态，并沿用 Window Service 的失败诊断。
- 设置窗口始终保持可激活、可获取焦点和任务栏入口；不得引入 `WS_EX_NOACTIVATE`、`WS_EX_TOOLWINDOW` 或独立 `WS_EX_TOPMOST`。
- 模式同步只接入统一状态路径，不在按钮、快捷键等入口重复实现。

## Acceptance Criteria

- [x] 初始选择模式下，设置窗口的 `GW_OWNER` 为空。
- [x] 切换到任意非选择工具后，设置窗口的 `GW_OWNER` 为 Drawpad，且位于画布 owner 链之上。
- [x] 返回选择模式后，设置窗口的 `GW_OWNER` 恢复为空并退出 topmost 链。
- [x] 两个方向重复切换均幂等；必要 HWND 缺失时安全失败，窗口关系可保持或回滚到切换前状态。
- [x] 切换前后设置窗口保留 `WS_EX_APPWINDOW`、应用图标和独立 owner thread，且不含 `WS_EX_NOACTIVATE`、`WS_EX_TOOLWINDOW`、`WS_EX_TOPMOST`。
- [ ] `InkeysRepo.sln` 的 `Debug | ARM64` 构建通过，包含窗口测试的 `InkeysHeadlessTests.exe` 通过。

## Out of Scope

- 不改变设置窗口布局、渲染、显示/隐藏行为或业务逻辑。
- 不调整 Drawpad、Bar、PPT 和白板既有 owner 链结构。
- 不创建 commit、push 或执行会自动提交的 Trellis 归档。

## Technical Notes

- 任务按轻量修复处理，仅维护本 PRD；实现采用 `Service::SetSettingOwnedByDrawpad(bool)` 和 `GWLP_HWNDPARENT`。
- 解除 owner 后使用 `HWND_NOTOPMOST` 恢复普通 Z 序，同时不修改设置窗口既有扩展样式。
- 2026-09-20 已完成人工验收；任务按要求继续保持活动状态，不执行归档。
