# UI3 边框点光追光技术设计

## Architecture

- `BarUiShapeClass` 与 `BarUiSuperellipseClass` 继续使用默认 `Solid` 的 `BarUiFrameRenderingEnum`；PointLight 启用范围不变。
- `BarUISetClass` 在同一互斥锁下保存交互点、交互更新序号、鼠标点、鼠标更新序号和 Raw Input 可用状态；窗口回调与交互线程只更新状态并唤醒渲染。
- `BarUIRendering` 每帧从主按钮当前 `x.val/y.val` 计算常驻光位置，取得一致输入快照，并用帧间 `dt × BarUiAnimationSpeedRate` 推进交互、主光脉冲和鼠标淡入。
- UI3 窗口以 `RIDEV_INPUTSINK` 注册鼠标 Raw Input；`WM_INPUT` 只负责通过 `GetCursorPos` 取得屏幕位置并转换为窗口客户区坐标，不解析或持有 Raw Input 缓冲区。

## Rendering

- `Solid` 分支保持现有纯色画刷和 D2D 绘制调用。
- `PointLight` 分支使用 `ID2D1RadialGradientBrush`：常驻光衰减为 0%:100%、25%:72%、65%:20%、100%:4%；交互光和鼠标光末端均为 0%。
- 每个光源先以 `strokeWidth + 2px` 绘制 12% 强度扩散层，再以原 `strokeWidth` 绘制清晰层；全部使用 `SOURCE_OVER`。
- 内部光源类型改为 `Primary`、`Interaction`、`Cursor`；同一帧按最终 RGB 与光源类型缓存渐变画刷，绘制控件前只调整透明度。
- 非穿透绘制模式下，主光使用 `logoInk->color1.val` 的动画画笔色；鼠标光和交互光继续使用边框色，因此不同来源可在 `SOURCE_OVER` 下自然混合。
- 创建渐变失败时回退到现有纯色路径并只记录一次错误。
- `Bar.RenderingAttribute` 对 `PointLight` 使用扩散层宽度计算脏区外延。

## Interaction Timeline

- 新按下更新交互点及更新序号并启动一次主光脉冲；拖动只更新交互序号，不重启主光脉冲。
- 交互强度为 `0.40 × (1 - smoothstep(progress))`，`progress` 在 1× 速度下 1.8 秒到达 1。
- 主光在 0~0.30s 由 1 平滑降至 0.60，在 0.30~1.80s 由 0.60 平滑恢复至 1。
- 鼠标光强度为 `0.60 × smoothstep(enableProgress)`；启用进度仅在动画从关闭切换到开启时用 300ms 推进，位置变化不重置。
- 渲染侧在输入序号变化或任一时间过程活动时请求下一帧；首次进入静止状态时再绘制一次最终帧。
- 动画关闭时将交互进度、主光脉冲和鼠标淡入直接归位，同时消费旧交互序号，避免重新启用后恢复历史残光。

## Compatibility and Failure Handling

- 复用现有 `d2d1_1.h`、`ID2D1DeviceContext`、WARP 和预乘 Alpha target，不新增工程项或第三方依赖。
- Raw Input 使用 Windows 自带 `RegisterRawInputDevices`，不新增链接库、线程或低级钩子；注册失败后第三光源保持关闭并只记录一次错误。
- 径向渐变通过 `ID2D1RenderTarget` 继承接口创建，符合 Windows 7 SP1 Platform Update 的 D2D 1.1 路径。
- 不使用 Direct2D Effects、D3D 11.1 专属 feature level 或 DirectComposition。
- 任务完成静态和后台构建后保持 `in_progress`，由维护者运行 UI3 做最终视觉验收。
