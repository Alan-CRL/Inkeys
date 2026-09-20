# 设置窗口随绘制模式加入画布 Owner 链

## Goal

解决绘制模式下顶层画布遮挡设置窗口、导致设置窗口无法点击的问题；设置窗口在绘制时加入 Drawpad owner 链并位于画布之上，在选择模式下恢复为独立普通窗口。

## Background

- 设置窗口当前由 Window Service 的独立线程创建，是无 owner 的顶层 `WS_POPUP`。
- 设置窗口固定保留 `WS_EX_APPWINDOW`，并清除 `WS_EX_NOACTIVATE`、`WS_EX_TOOLWINDOW`、`WS_EX_TOPMOST` 等不符合普通可交互窗口的样式。
- 画笔、橡皮、图形和选择等工具切换最终统一经过 `SyncDraw3State()`。
- 设置窗口使用自绘无框标题栏；历史版本曾通过 `WM_NCHITTEST` 把非交互标题栏区域映射为 `HTCAPTION`，当前实现缺失该路径，导致窗口无法拖动。
- 主栏设置按钮当前按可见性直接切换显示/隐藏，无法区分“设置已打开但焦点在其他窗口”的场景。

## Requirements

- 绘制模式定义为 `StateModeSelect != StateModeSelectEnum::IdtSelection`，覆盖画笔、橡皮和图形等所有非选择工具。
- 绘制模式下，把设置窗口设置为 Drawpad 的 owned top-level popup；不得改为 `WS_CHILD`。
- 选择模式下清除设置窗口 owner，并使其退出画布的 topmost owner 链。
- owner 切换必须在设置窗口所属线程内执行，保持 Window Service 的线程所有权约束。
- owner 切换应幂等；失败时不得遗留半切换状态，并沿用 Window Service 的失败诊断。
- 设置窗口始终保持可激活、可获取焦点和任务栏入口；不得引入 `WS_EX_NOACTIVATE`、`WS_EX_TOOLWINDOW` 或独立 `WS_EX_TOPMOST`。
- 模式同步只接入统一状态路径，不在按钮、快捷键等入口重复实现。
- 恢复自绘标题栏空白区域的系统拖窗命中；拖动必须继续走 Win32 `HTCAPTION` 非客户区移动循环，不自行实现鼠标位移循环。
- 标题栏右侧关闭按钮区域不得返回 `HTCAPTION`，其既有点击隐藏行为保持不变。
- 拖窗能力在 Setting 有无 Drawpad owner 的两种状态下均保持有效，不改变固定窗口尺寸、位置持久化或 ImGui 内容交互。
- 主栏设置按钮在窗口隐藏时显示并激活 Setting；窗口已显示但未拥有前台/输入焦点时只把 Setting 恢复到前台并获取焦点，不关闭窗口。
- 只有 Setting 已显示且当前拥有焦点时，再次点击主栏设置按钮才隐藏窗口。
- 焦点判断和激活必须使用 Setting 的真实 HWND，并复用 Window Service 所属线程命令，不在 Bar 输入线程直接修改窗口状态。

## Acceptance Criteria

- [x] 初始选择模式下，设置窗口的 `GW_OWNER` 为空。
- [x] 切换到任意非选择工具后，设置窗口的 `GW_OWNER` 为 Drawpad，且位于画布 owner 链之上。
- [x] 返回选择模式后，设置窗口的 `GW_OWNER` 恢复为空并退出 topmost 链。
- [x] 两个方向重复切换均幂等；必要 HWND 缺失时安全失败，窗口关系可保持或回滚到切换前状态。
- [x] 切换前后设置窗口保留 `WS_EX_APPWINDOW`、应用图标和独立 owner thread，且不含 `WS_EX_NOACTIVATE`、`WS_EX_TOOLWINDOW`、`WS_EX_TOPMOST`。
- [x] 设置窗口标题栏除关闭按钮外的非交互区域返回 `HTCAPTION`，可以通过系统移动循环拖动窗口。
- [x] 关闭按钮仍可点击隐藏设置窗口，正文内容区域仍返回普通客户区命中。
- [x] 选择态与所有非选择态下均可拖动设置窗口，`WM_MOVE` 继续更新持久化位置。
- [x] Setting 隐藏时点击主栏设置按钮会显示并激活窗口。
- [x] Setting 已显示但未拥有焦点时点击按钮只恢复其焦点，不关闭窗口。
- [x] Setting 已显示且拥有焦点时点击按钮隐藏窗口。
- [ ] `InkeysRepo.sln` 的 `Debug | ARM64` 构建通过，包含窗口测试的 `InkeysHeadlessTests.exe` 通过。

## Out of Scope

- 不改变设置窗口布局、渲染、显示/隐藏行为或业务逻辑。
- 不调整 Drawpad、Bar、PPT 和白板既有 owner 链结构。
- 不创建 commit、push 或执行会自动提交的 Trellis 归档。

## Technical Notes

- 任务按轻量修复处理，仅维护本 PRD；实现采用 `Service::SetSettingOwnedByDrawpad(bool)` 和 `GWLP_HWNDPARENT`。
- 解除 owner 后使用 `HWND_NOTOPMOST` 恢复普通 Z 序，同时不修改设置窗口既有扩展样式。
- 2026-09-20 已完成人工验收；任务按要求继续保持活动状态，不执行归档。
- 2026-09-20 已人工确认标题栏拖动、关闭按钮及 owned/unowned 状态下的窗口移动均生效。
- 2026-09-20 已人工确认主栏设置按钮的显示、失焦恢复与聚焦关闭三态行为均生效。
