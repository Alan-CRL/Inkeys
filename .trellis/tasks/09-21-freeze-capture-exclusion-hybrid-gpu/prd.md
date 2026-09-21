# 定格 Magnification 排除列表修复

## Background

- 当前桌面定格使用 Windows Magnification API，并通过 `MagSetWindowFilterList(MW_FILTERMODE_EXCLUDE)` 排除 Inkeys 自身窗口。
- 已出现主栏被固化进定格画面的现象；现有列表只在 Magnifier 线程启动时提交一次，并混用 Window Service 与 legacy 全局 HWND。
- 用户决定先单独修复排除列表；修复后的实际画面效果由用户人工测试。

## Goal

保留现有 Magnification 捕获与窗口层级，只修正排除列表的来源、有效性和刷新时机，使每次抓帧使用 Window Service 当前有效的 Inkeys 顶层窗口句柄。

## Requirements

- 排除列表的唯一事实源改为 `Inkeys::Window::Service`，不再读取 `floating_window`、`drawpad_window`、`freeze_window`、`setting_window` 等 legacy 镜像。
- 排除角色覆盖 `MagnifierHost`、`Freeze`、`DrawpadPresentation`、`Drawpad`、四个 PPT/PageControl 窗口、`Bar` 和存在时的 `Setting`。
- `MagnifierChild` 是 child/magnification control，不作为顶层排除项；`DisplayObserver` 是 message-only window，不加入列表。
- 加入列表前必须确认 HWND 有效、属于当前进程、不是 `WS_CHILD`，并按 HWND 去重。
- Magnifier 初始化完成时提交一次列表；每次 `UpdateMagWindow()` 调用 `MagSetWindowSource` 前重新构建并提交列表，以覆盖设置窗口生命周期或窗口重建后的句柄变化。
- 如果过滤列表提交失败，本次不更新 Magnifier source，沿用现有低频失败日志，不引入新的诊断系统。
- 修改保持幂等，不改变窗口显示、owner、topmost、焦点、任务栏或捕获尺寸。

## Non-goals

- 不处理或诊断混合显卡、dGPU/iGPU、黑屏和 capture backend 选择。
- 不替换 Magnification API，不引入 Desktop Duplication、Windows.Graphics.Capture 或 GDI 捕获。
- 不修改当前双捕获、线程同步、透明度或请求状态逻辑。
- 不承诺本轮自动验证能够证明 Win7/驱动实际遵守排除列表；视觉结果由用户人工验收。

## Acceptance Criteria

- [x] 生产代码中 Magnification 排除列表不再读取 legacy HWND。
- [x] 每次 Magnifier source 更新前都重新取得、校验、去重并提交当前 Window Service 角色 HWND。
- [x] 排除集覆盖所有当前可捕获的 Inkeys 顶层 UI，包括 Magnifier Host 和可选 Setting。
- [x] 空值、已销毁、其他进程、child 和重复 HWND 不会进入提交列表。
- [x] `MagSetWindowFilterList` 失败时不继续更新本次 source，并保留清晰但不过度重复的失败日志。
- [x] 不改变现有 owner 链、样式、显示状态和定格请求语义。
- [x] `InkeysRepo.sln` 的 `Debug | ARM64` 完整构建、`InkeysHeadlessTests.exe --no-window` 和 `git diff --check` 通过。
- [ ] 实际定格画面是否仍包含 Inkeys 窗口由用户人工验收，自动验证不替代该结论。

## Deferred Investigation

此前关于 Magnification/WOW64、Win7、混合显卡和替代捕获后端的调查保留在 `research/`。若本轮人工验证仍失败，再由用户决定是否恢复后端替换或专项诊断。
