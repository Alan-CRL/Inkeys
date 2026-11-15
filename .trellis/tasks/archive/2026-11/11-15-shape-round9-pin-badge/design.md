# 设计

## 1. 通用角标 BarButtonBadge（R1）
- 新增轻量结构 `BarButtonBadgeVisual`：resource（SVG，复用 `barStatusPin`）、`progress`（0..1，可见度驱动缩放）、`spin`（旋转角，进场从 -45°→0°，离场 0°→+45°）、`below`（0..1 换边过渡）。
- 挂在 `BarButtonClass` 上的可选成员（默认不启用，不影响其他按钮）；`DrawBarButtonVisual` 在 icon/文字之后、按压变换之内绘制。
- 位置：按钮外框右上角向内 4DIP，直径 10DIP；倒转时按 `below` 在右上与右下之间插值。以角标中心为缩放与旋转中心，按钮位置/尺寸动画只平移角标，不改变角标自身缩放。
- 动画：时长、曲线沿用主栏图标显隐的 `BarUiAnimation` 参数（实现时取 `TransitionContent` 使用的同一组），进场 EaseOutBack，离场 EaseInBack；关闭动画时直接跳到终态。
- 脏区：角标包络并入按钮视觉 bounds（`ComputeBarButtonVisualBounds`），旋转按外接圆计算。
- 主栏几何按钮：`UpdateGeometryButtonStyle` 中按 `selected && IsShapeToolPinned(tool)` 设置角标目标；styleKey 纳入固定位。

## 2. 固定状态（R2/R3）
- `StateModeClass` 增加会话内字段 `shapePinnedPreset`（optional PresetKind），不持久化。`IsShapeToolPinned(tool)` = Line/Arrow，或 `shapePinnedPreset == tool.preset`。
- 切换图形类别时，若新 preset ≠ `shapePinnedPreset` 则清除。仅改变同一图形的参数（边数、模式等）不清除。
- 固定位只在 UI 侧使用，不下发 Draw3。

## 3. 长按（R2）
- 几何面板 Pointer：Down 命中类别按钮时记录 `holdItem_`、`holdStart_`；Move 移出按钮或超出触摸移动阈值即取消长按（仍可按原逻辑判定短按）。
- deadline 沿用面板现有 `ConsumeCarouselWake` 模式，由交互 owner 在 0.5s/2.0s 唤醒，不依赖帧率。
- 2.0s 到达：提交"选中 + 固定（或取消固定）"业务动作，标记 `holdConsumed_` 使随后的 Up 不再触发短按。
- 进度环：抽取 `Bar.Scene.cpp` 页码 loading 环的路径构造为共享函数（圆角矩形内侧路径 + 缓存），新增 progress 模式（从顶边中点起 dash 长度 = progress×周长）；页码环改为调用共享函数，行为不变。

## 4. 框外接触（R3，已收窄）
- Draw3 不改：框外首个接触仍由 `beginShapeContact` 结束编辑并吞笔，照常发出 finishedEditing。
- UI 侧 `RestoreToolAfterShapeEditing`（IdtState.cpp）在当前图形工具已固定时跳过恢复入口工具并保留 `shapeReturnMode`（entryEpoch/workspace 守卫不变），取消固定后的下一次结束编辑照常恢复（审查后修正）。
- 固定状态不需要下发 Draw3。原"延迟起笔 + 入口工具快照"方案见 research/r3-outside-contact.md，暂不实施。
- 后续方向（用户 2026-11-15）：未来 Drawpad3 会让上方控件与画布在同一画布窗口渲染，届时框外接触可在同一窗口内直接分流，再实现"拖动直接开画"。

## 5. 兼容与回滚
- 不改存储格式、不改 UInk。角标、长按、框外判定可分别回滚：角标为纯 UI；长按只新增会话状态；框外判定集中在 `beginShapeContact` 一处。
