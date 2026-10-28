# 当前白板打开失败的复审（2026-10-27）

## 范围与结论

本轮仅审阅当前工作树，包括上一轮未提交的异步激活、Settings Present 解锁、Shell 固定目标批次，以及 10-27 已实现的 Freeze 最小化拒绝和可见性/RECT 读回。源文件快照见 `review-20261027-source-hashes.json`。未修改产品代码、测试、shader 或已有验证记录；未启动 GUI、未运行编译或测试。

当前不能认定现场故障已修复。可以直接证明主栏反复收起的故障路径和一个最小化退出闭环；首次原生窗口事务究竟在哪个具体检查失败，现有持久日志尚不足以定论。不得继续把“首次最小化”当作已证实现场根因，也不能仅凭图标存在推断真正白板已显示。

## P1：同一进入意图的失败重试反复撤销主栏操作

- `Inkeys/IdtState.cpp:2562–2577`：Inactive 且 desired 时先将 phase 置 Entering，再 CollapseAuxiliaryPanels(true)；EnterWhiteboardWindowMode 返回 false 后退回 Inactive，但保留 desired。下一轮再次执行同样的收拢与取消。
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:3101–3111`：上述清理直接将 moreExpanded=false，并关闭绘制、几何、笔菜单、粗细、取色等面板。
- `Inkeys/IdtState.cpp:2719–2730`：Exiting 中反向进入失败也在每轮重试前再次清理。
- StateMonitoring 的 revision 等待上限为 250ms；有新 revision 时可以更快返回，所以用户刚打开的 More 可以立即被下一轮重试关闭。

这条路径能直接解释“主栏按钮不听使唤、更多展开立即关闭”。普通 Entering 等待 Draw3/背景不会重复清理；Bar::SetWhiteboardActive、Whiteboard::PublishActive 和 PageControl 同值发布均有去重，不应把它们当成同一原因。

方向：按真实意图/epoch 边沿执行一次交互清理，原生事务重试保留独立责任；还必须查明并修正使窗口事务持续失败的具体条件，不能只隐藏 UI 症状。

## 图标存在但白板不可见：透明 Freeze 与事务阶段不同步

- `Inkeys/IdtFreezeFrame.cpp:73,98` 提交清空后的透明初始 Freeze 表面；`Inkeys/Inkeys/Window/Window.Legacy.cpp:149` 在启动完成后显示它。
- `Inkeys/Inkeys/Window/Window.cpp:2293–2313` 在切入白板样式时保留原 visible 状态并设置 APPWINDOW。此时存在任务栏图标并不代表已经画出白板背景。
- Enter 在 `Window.cpp:2759` 将 ApplyWhiteboardWindowMode 与 RefreshOverlayGroup 串成提交；后者核验 owner/band/Z 序。任一步失败会进入 `2722–2727` 的快照恢复；恢复未完成时，`2684–2691` 的共享恢复屏障还会拒绝后续普通命令。
- 只有进入模式成功后，IdtState 才在 2585 请求白板 workspace，在 2619 发布背景 active，最终 2645 才排入一次激活。

因此末尾的异步激活无法修复更早的窗口模式提交失败；今天新增的 minimized false 和 Show/RECT 读回也处于更晚的背景提交阶段。它们可修独立漏洞，不能证明已命中当前入口故障。

尚未证实：首次失败究竟是某项 owner/band/Z 序读回、命令截止，还是遗留恢复屏障。Enter/Topmost/Visibility 不共享 ledger intent 键，不能猜定为周期刷新直接退休 Enter。需为 Enter 和快照恢复持久记录 stage、receipt phase、原始错误、实际失败成员/核验项；现有 Window 诊断走独立 stderr 通道，常规 idt 日志没有这些记录，不能以缺失推断从未发生该事件。

## 现场既有日志

检查 `Build/ARM64/Debug/log/idt1793062376164.log`：

- 08:53:06–13 的数次 UI3Diag 中，WhiteboardFreeze 都返回 Idle，attempt/commit 为 0；Bar 仍有回调，不能把现象直接认定为整个渲染线程永久死锁。
- 24–27 行（08:53:15.197、16.060、17.536）记录普通画布 surface 切换失败；firstFrame=true，requested/ready 输出版本相等，内容均为 0/0，主/辅助窗实际不可见且仍在 topmost band。
- 28–31 行在 08:53:41 后记录 surface 恢复，随后出现新的 primary 输出和内容；34 行才明确打印 workspace=1。日志不包含任务栏点击时间，不能把 41 秒直接定为用户点击时刻。

这些证据支持优先查早期窗口事务及其恢复，而非继续只调整末尾的激活。单个 Reconcile false 还可能由 guard 或恢复屏障拒绝产生，不能据此证明某一次 ShowWindow/SetWindowPos 原生调用失败。

## P1：已提交背景的最小化白板退出存在等待闭环

触发：正常进入白板并提交过背景 → 最小化窗口组 → 请求退出（例如关闭请求或配置禁用）。

1. `IdtState.cpp:172–175` 发布 active=false 并隐藏 Freeze；普通 Hide 不清 groupMinimized（Window.cpp:2891）。
2. `Whiteboard.cpp:146` 在检查 active=false 之前就因 groupMinimized 返回 Idle，跳过 147–150 的 committedBackgroundActive=false。
3. `IdtState.cpp:2772` 一直等待 BackgroundMatchesActive(false)。
4. 清除 minimized latch 的 LeaveWhiteboardWindowMode 在 2789，必须等上一步完成才会执行（Window.cpp:2198–2208）。

方向：最小化可以阻止绘制/重新显示，但不能阻止 inactive 清理和已经实际隐藏后的背景交接确认；保留真实窗口隐藏和失败重试条件。

## P2：验证覆盖与生产路径不等价

- 新增 minimized Freeze/restore 断言位于 `InkeysHeadlessTests/window_tests.cpp:1599–1624` 的 RunWindowTests；`animation_tests.cpp:1794` 的 --no-window 跳过该函数。因此记录的 headless PASS 没有执行新增原生可见性回归。
- 窗口 fixture 在 1554–1565 先显示 MagnifierHost、Freeze、分页、Bar 和主画布，再进入白板，绕过生产冷启动中的隐藏 owner、透明 Freeze、首次背景 ACK 和 Active 顺序；恢复测试在 1619 手工 SetBounds/Show，不证明生产恢复通知会自动驱动背景。
- fixture 在 window_tests.cpp:1175 创建局部 Service，Freeze 默认 WndProc 却在 Window.cpp:198 固定查询全局 GetService。测试 Freeze 不满足该全局实例的 whiteboardAnchor，因而即使运行 GUI 窗口测试，原生 WM_SIZE/SC_MINIMIZE/SC_RESTORE 回入也没有覆盖生产同一 Service 的消息路径。

后续应先修正 fixture 的真实 Service 归属，覆盖冷启动入口、失败重试不清用户面板、最小化退出和自动恢复；真实冷启动/任务栏/前后台行为仍须用户明确授权后的 GUI 验收。

## 本轮验证边界

本轮为只读源码/差异/已有日志审阅，未用重新构建或旧 PASS 替代现场复现。已有产品改动与 shader 保持原样，本任务仍在进行中，未提交 commit。
