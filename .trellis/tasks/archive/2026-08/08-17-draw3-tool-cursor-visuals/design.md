# 技术设计

## Current Data Flow

```text
stateMode / UI3 Bar
  -> IdtState::PublishDraw3State
  -> Bridge::ProductState { tool, colorRgba, widthDip, revision }
  -> Host::PumpBridgeState (Draw3 drawing thread)
  -> WindowController active tool + ProductVisualStyle atomics
  -> DrawingController frame snapshot
  -> DrawingCursorAppearance + pointer sample/authority resolver
  -> transient cursor shader -> final backbuffer
```

活动笔画在 Down 时锁存 `ProductVisualStyle`，因此改色/改粗细只应影响后续笔画和当前 Hover 光标，不应重染正在进行的笔画。

## Root Cause

1. `WindowController::SetProductVisualStyle` 已发布最新颜色/宽度并请求光标重绘，但 `DrawingController` 只在构造函数中调用一次 `ConfigureDrawingCursor`，所以请求触发后仍读取旧 appearance。
2. `ResolvePrimaryDrawingCursorVisual` 把所有 Pen 样本在默认模式下统一改成全不透明，没有区分普通笔、部分透明荧光笔和橡皮 Hover。
3. Bar 的显示值写死为旧 Draw2 `130/255`，没有读取 Draw3 的有效工具透明度。
4. Laser 没有缺陷：笔迹基准直径和 Hover 光标已经共用 `kLaserSolidDiameterAt96Dpi * dpiScale`。

## Proposed Changes

### Shared Effective Opacity

- 在现有 Draw3 bridge/产品状态边界提供单一的荧光笔有效透明度常量 `0.35f`，Draw3 合成与传统 `IdtState` 的 UI 查询都使用该值。
- `IdtState` 暴露只读的“当前工具有效透明度”查询：普通笔/Shape 返回 `1.0f`，荧光笔返回共享常量。该查询只描述最终呈现，不新增可编辑 state。
- UI3 Bar 使用该查询生成百分比，删除旧 Draw2 `130/255` 假设。

### Dynamic Cursor Appearance Refresh

- 把普通笔与荧光笔 appearance 构造收敛为 `DrawingController` 内部的单一 helper，构造函数和每帧样式变化路径复用。
- 每帧读取一次 `ProductVisualStyleSnapshot()`；仅当 `colorRgba` 或 `widthDip` 变化时重新配置普通笔/荧光笔光标，避免无意义的持续写入和重绘请求。
- 更新发生在本帧解析光标 visual 之前，`ConfigureDrawingCursor` 的既有 render request 与 previous/current dirty bounds 负责清除旧光标并绘制新光标。
- 普通笔直径使用 `max(widthDip, 5.0f * dpiScale)`；荧光笔实际几何当前固定为 `6.25 × 50 px`，光标复用该尺寸，不改变现有笔迹几何。

### Alpha Resolution

- 普通不透明 Pen/Shape 可继续在默认模式使用全 alpha 光标。
- resolver 不再用普通 Pen 规则覆盖部分透明 appearance；荧光笔保持 `opacity=0.35, fillAlpha=1.0`，使 shader 最终 alpha 为 `0.35`。
- resolver 不再覆盖 Eraser Hover 的 `opacity=0.5`。所有 Eraser Contact 路径仍显式提升到 `1.0`，包括倒转笔、鼠标和 Touch 接触。
- 不改变 `translucentInkCursorEnabled` 对普通笔半透明预览的既有含义。

### Laser Verification

- 保持 `DiameterForTool(Laser) == kLaserSolidDiameterAt96Dpi`，实际笔画 Down 时乘 `dpiScale`；laser appearance 构造使用同一常量乘 `dpiScale`。
- 不把当前 Pen 的 `widthDip` 用于 Laser，也不扩展 `ProductState`。测试/静态检查记录此一致性，防止未来两处改成不同常量。

## State Design Notes

- `StateModeClass` 是产品层当前工具状态：`StateModeSelect` 选择 Pen/Eraser/Shape/Selection，`Pen.ModeSelect` 区分普通笔与荧光笔，`laserActive` 作为独立覆盖位，避免把 Laser 塞进旧笔型枚举。
- `CurrentDraw3Tool()` 按 `laserActive -> eraser -> shape -> highlighter -> pen` 的优先级映射到稳定的 `Bridge::Tool`。
- bridge 传递当前工具通用的 `colorRgba/widthDip`，而不是把旧 `StateModeClass` 跨线程暴露给 Draw3；Host 在绘制线程按 revision 消费完整快照。
- `WindowController` 原子发布活动工具和视觉样式；`DrawingController` 在帧边界采样。活动笔画在 Down 时锁存样式，Hover 光标则跟随最新样式。
- Eraser 宽度模式由独立 revision 编码；Laser 当前没有独立宽度 state，实际与光标都使用 Draw3 的 DPI-aware 固定直径。

## Compatibility And Risks

- 风险最高的是 resolver alpha 分支：修改必须同时覆盖 Pen Hover、inverted eraser Hover/Contact、mouse eraser 和 Touch eraser，避免只修一种设备。
- appearance 改变必须触发旧/新 dirty bounds 合并；继续复用 `ConfigureDrawingCursor` 和当前 previous/current visual 比较，不新增跨线程直接渲染。
- 共享透明度常量只替换现有固定值，不改变配置格式或用户数据。
- 若无窗口测试无法直接覆盖每帧 style refresh，则以可测试的 appearance/resolver 纯逻辑加隐藏 Host smoke test和静态数据流审计共同验证。

## Rollback

- 变更局限于 opacity 常量/查询、appearance refresh、resolver 条件和 Bar 显示公式，可按文件回退；不包含文档格式、持久化 schema、shader buffer 或 bridge 结构迁移。
