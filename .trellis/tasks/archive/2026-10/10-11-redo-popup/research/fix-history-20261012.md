# 2026-10-12 Undo/Redo 修复追溯

## 历史与需求来源

- `26131e75`：主栏 Undo 打开单步 Redo 属性弹窗，复用橡皮属性动画。
- `4476fe3f`：弹窗关闭后进入上 Redo、下 Undo 的 2×1 布局，以 wall-clock 唤醒超时关闭。
- `0468b91a`：加入 Draw3 真实 canUndo/canRedo 发布及 5 秒 hold、禁用灰色；本轮从该 HEAD 修复。
- 原任务仍是 `10-11-redo-popup`、`in_progress`，已恢复为本次 session 活动任务。早期 PRD/设计里的不弹窗、UI 锁存可用性及不包含变灰等规则已被本轮顶部补充需求覆盖。
- 已通过 trellis mem 检索本项目历史；2026-10-11 可索引 Codex 对话没有匹配 Redo 的记录，三个相关 commit 均注明 Cursor 协作。原后续提示词以用户本轮完整引用为需求依据，不推测缺失对话。

## 根因证据

1. `EngageRecallCommand` 在点击前确认可用后先发布 Draw3 命令，然后调用 `CollapseAuxiliaryPanels`；该关闭入口重新读异步更新后的 availability。最后一次 Redo/Undo 执行完成后变为不可用，后续 reducer 的 click 门禁会丢掉应有的展开反馈。
2. `ResolveRecallChrome` 在 hold=None 时直接从实时 availability 推导尺寸/显隐，故无 hold 时最后 Redo、新笔迹或其他来源会即时变换布局。
3. `Dismiss`、`ReturnToDesktop` 直接清 hold/disarm timer，绕过连续点击保护；popup 动作直接复用主栏激活逻辑会即时交换两个位置的命令身份。
4. 主栏每轮布局在前段/各尺寸分支提交正常颜色，尾部再提交禁用灰色；`Bar.Scene.cpp::RetargetBarButtonInteractionVisual` 还会再次覆写正常颜色，弹窗调用也存在这一断点。`BarUiColorClass::SetTar` 在目标变化时重置 progress/startColor，同帧相反目标反复中断动画，正常↔禁用不能收敛。
5. 原纯状态测试构造稳定 availability 后才调用 click，未覆盖命令发送到关闭再到 click 的异步末步顺序；hold None 的 availability 变化和 dismiss 保护也缺少回归。
6. 独立审查发现 `BarRecallAttributePanel::Advance` 始终以 Recall 按钮的可见性判定关闭，并固定取其锚点。主栏 Redo 变成 2×2 后 Recall 被隐藏，下一帧将错误关闭 popup Undo。已改为同份 chrome 快照选当前主栏按钮并贯穿门禁、几何和弹窗身份，新增生产源级接线断言补足纯状态测试盲区。

## 修复边界和防复发

- 改动留在 Bar 的布局策略、命令入口、颜色目标和现有回归用例；当前 Draw3 发布链已存在，除非出现直接断点不扩绘制历史改动。
- 命令执行前的可用性/成功接收决定本次动作，动作之后的事实仅控制新的可用性。布局锁存与可用性分开，关闭只能关面板不能跳过保护。
- 一个颜色每帧只提交一个最终目标；回归使用生产策略与现有颜色动画，检查中间帧和终态。
- 完整 Debug|ARM64 Solution 编译、`InkeysHeadlessTests --no-window`、独立 trellis-check 和编码/换行/diff 检查；GUI 视觉验收不在本轮授权内。
