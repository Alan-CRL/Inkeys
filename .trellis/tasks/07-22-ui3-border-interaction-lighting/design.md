# UI3 边框点光追光技术设计

## Architecture

- `BarUiShapeClass` 与 `BarUiSuperellipseClass` 增加内部 `BarUiFrameRenderingEnum` 字段，默认 `Solid`。
- `BarUISetClass` 保存受互斥锁保护的交互点、主光脉冲起点和交互光最后更新时间；输入线程只更新状态并唤醒渲染。
- `BarUIRendering` 每帧从主按钮当前 `x.val/y.val` 计算常驻光位置，并取得交互状态快照，所有强度只由 `steady_clock` 计算。

## Rendering

- `Solid` 分支保持现有纯色画刷和 D2D 绘制调用。
- `PointLight` 分支使用 `ID2D1RadialGradientBrush`：常驻光衰减为 0%:100%、25%:72%、65%:20%、100%:4%；交互光末端为 0%。
- 每个光源先以 `strokeWidth + 2px` 绘制 12% 强度扩散层，再以原 `strokeWidth` 绘制清晰层；全部使用 `SOURCE_OVER`。
- 同一帧按 RGB 与光源类型缓存渐变画刷，绘制控件前只调整透明度；缓存仅属于渲染线程和当前 D2D device context。
- 创建渐变失败时回退到现有纯色路径并只记录一次错误。
- `Bar.RenderingAttribute` 对 `PointLight` 使用扩散层宽度计算脏区外延。

## Interaction Timeline

- 新按下同时更新交互点、交互光时间和主光脉冲起点。
- 拖动只更新交互点及交互光时间，不重启主光脉冲。
- 交互强度为 `1 - smoothstep(t / 0.45s)`。
- 主光在 0~0.10s 由 1 平滑降至 0.35，在 0.10~0.45s 由 0.35 平滑恢复至 1。
- 渲染侧在任一时间过程活动时请求下一帧；首次进入静止状态时再绘制一次最终帧。

## Compatibility and Failure Handling

- 复用现有 `d2d1_1.h`、`ID2D1DeviceContext`、WARP 和预乘 Alpha target，不新增工程项或第三方依赖。
- 径向渐变通过 `ID2D1RenderTarget` 继承接口创建，符合 Windows 7 SP1 Platform Update 的 D2D 1.1 路径。
- 不使用 Direct2D Effects、D3D 11.1 专属 feature level 或 DirectComposition。
- 任务完成静态和后台构建后保持 `in_progress`，由维护者运行 UI3 做最终视觉验收。
