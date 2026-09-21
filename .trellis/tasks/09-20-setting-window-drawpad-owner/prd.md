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
- 画布及主栏的基础 Owner 链应为 `Freeze -> DrawpadPresentation -> Drawpad -> Bar/PPT`；其中 Drawpad 仍是顶层 owned popup，不得改为 `WS_CHILD`。
- 在 Presentation/Primary 两种画布表面切换、白板模式和 root topmost 传播中，Bar/PPT 以及绘制模式下的 Setting 都必须通过 Owner 链位于两套画布表面之上。
- `SyncDraw3State()` 必须保存最新期望 owner 状态；若 Window Service 提交失败，现有状态监控应周期重试直至收敛，且较旧请求的完成不得清除较新状态的重试需求。
- 设置窗口始终保持可激活、可获取焦点和任务栏入口；不得引入 `WS_EX_NOACTIVATE`、`WS_EX_TOOLWINDOW` 或独立 `WS_EX_TOPMOST`。
- 模式同步只接入统一状态路径，不在按钮、快捷键等入口重复实现。
- 恢复自绘标题栏空白区域的系统拖窗命中；拖动必须继续走 Win32 `HTCAPTION` 非客户区移动循环，不自行实现鼠标位移循环。
- 标题栏右侧关闭按钮区域不得返回 `HTCAPTION`，其既有点击隐藏行为保持不变。
- 拖窗能力在 Setting 有无 Drawpad owner 的两种状态下均保持有效，不改变固定窗口尺寸、位置持久化或 ImGui 内容交互。
- 主栏设置按钮在窗口隐藏时显示并激活 Setting；窗口已显示但未拥有前台/输入焦点时只把 Setting 恢复到前台并获取焦点，不关闭窗口。
- 只有 Setting 已显示且当前拥有焦点时，再次点击主栏设置按钮才隐藏窗口。
- 焦点判断和激活必须使用 Setting 的真实 HWND，并复用 Window Service 所属线程命令，不在 Bar 输入线程直接修改窗口状态。
- 保留 `Save.Enable` 对 Draw3 桌面画布自动保存的控制，只把设置文案修正为当前真实语义。
- 常规页把启动加载动画移到启动行为末尾，并按“启用动画、动画速率、边缘光影、动态边缘光影、主栏缩放、设置缩放”的顺序整理外观卡片。
- “边缘光影”关闭时隐藏“动态边缘光影”，“启用动画”关闭时隐藏“动画速率”；隐藏不得覆盖对应持久化值，重新开启后恢复显示与原配置。
- 删除避免全屏、端点吸附、抬笔平滑、绘图性能、绘制页实验选项、预设页和 PPT 墨迹固定等已废弃设置；未编译的 Draw2 源码不纳入修改范围。
- 修正橡皮擦文字样式、触摸面积辅助文案和区块高度；修正 PPT 控件显示区块的多余空白。
- 顶层实验选项保留，并把触摸面积、PptCOM、Draw3 三个控制台输出卡片连续排列。
- 停止读取/写入被删除的旧配置，并在传统配置保存时显式清理遗留 JSON，不依赖 `Config.AutoClean`。
- 本次新增或变更的画布保存、启动、外观、橡皮擦及控制台文案必须覆盖简中、繁中、英文 i18n；组件页和调试软件页不在本轮国际化范围。

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
- [x] `InkeysRepo.sln` 的 `Debug | ARM64` 构建通过，包含窗口测试的 `InkeysHeadlessTests.exe` 通过。
- [x] 设置页删除项不再显示，预设导航隐藏，剩余卡片顺序、文案和区块高度符合本轮需求。
- [x] `Regular.AvoidFullScreen`、`PointAdsorption`、`SmoothWriting`、`HideTouchPointerBeta`、`Performance`、`Preset` 与 PPT `FixedHandWriting` 不再读取或写入；传统配置保存会清理旧键。
- [x] 本轮受影响文案在简中、繁中、英文下完整显示，i18n 同步和检查通过。
- [x] “启用边缘光源”在三种语言中改为“边缘光影”的对应译文。
- [x] 动画速率和动态边缘光影只在各自总开关开启时显示，外观容器高度随可见卡片数收敛且隐藏不改写配置。
- [x] owner 切换发生短暂失败时会按现有 250ms 状态节拍重试；模式在提交期间再次变化时最终 owner 与最新模式一致。
- [x] `GW_OWNER(DrawpadPresentation) == Freeze` 且 `GW_OWNER(Drawpad) == DrawpadPresentation`，静态与动态创建路径一致。
- [x] Presentation/Primary 表面切换后 Bar 保持可见，并且 Z 序始终高于 DrawpadPresentation 和 Drawpad。
- [x] 新 Owner 链不改变 Drawpad 的顶层 popup 样式、输入激活和白板行为，root topmost 传播与销毁顺序继续通过隐藏 HWND 测试。

## Out of Scope

- 不修改已排除编译的 Draw2 源文件，也不重新启用任何 Draw2 功能。
- 不重构设置窗口渲染后端、配置体系或未涉及的组件/调试页面。
- 除将 Drawpad 改为 DrawpadPresentation 的 owned popup 外，不调整 Bar、PPT、Setting 和白板的角色与样式。
- 不创建 commit、push 或执行会自动提交的 Trellis 归档。

## Technical Notes

- 任务按轻量修复处理，仅维护本 PRD；实现采用 `Service::SetSettingOwnedByDrawpad(bool)` 和 `GWLP_HWNDPARENT`。
- 解除 owner 后使用 `HWND_NOTOPMOST` 恢复普通 Z 序，同时不修改设置窗口既有扩展样式。
- 2026-09-20 已完成人工验收；任务按要求继续保持活动状态，不执行归档。
- 2026-09-20 已人工确认标题栏拖动、关闭按钮及 owned/unowned 状态下的窗口移动均生效。
- 2026-09-20 已人工确认主栏设置按钮的显示、失焦恢复与聚焦关闭三态行为均生效。
- 2026-09-20 任务继续承载设置页条目整理与旧配置清理；此前三个窗口行为保持不变。
- 2026-09-20 接受 PR #212 的 CodeRabbit 收敛性建议：补充 Setting owner 期望状态持久化和失败重试，不扩大 Window Service 公共接口。
- 2026-09-21 继续收敛双画布层级：Drawpad 改为 DrawpadPresentation 的顶层 owned popup，使 Bar/PPT/owned Setting 在两种表面上方的关系由 Owner 链直接保证。
- 2026-09-21 ARM64 `Debug` Solution 构建和 `InkeysHeadlessTests.exe --no-window` 通过；含隐藏 HWND 的 Window 测试通过，完整测试仅剩既有 MessageBox GDI baseline 波动（initial=45, final=49）。
