# UI3 边框点光追光技术设计

## Architecture

- `BarUiShapeClass` 与 `BarUiSuperellipseClass` 继续使用默认 `Solid` 的 `BarUiFrameRenderingEnum`；PointLight 启用范围不变。
- `BarUiShapeClass` 与 `BarUiSuperellipseClass` 增加默认 `Frame` 的内部光照颜色策略；只有主按钮、主栏和绘制属性栏设为 `PenWhenDrawing`。
- `BarUISetClass` 只在互斥锁下保存鼠标点、更新序号和 Raw Input 可用状态；窗口回调更新状态并唤醒渲染。
- `BarUIRendering` 每帧从主按钮当前 `x.val/y.val` 计算稳定主光位置，并用帧间 `dt × BarUiAnimationSpeedRate` 仅推进鼠标淡入。
- UI3 窗口以 `RIDEV_INPUTSINK` 注册鼠标 Raw Input；`WM_INPUT` 只负责通过 `GetCursorPos` 取得屏幕位置并转换为窗口客户区坐标，不解析或持有 Raw Input 缓冲区。

## Rendering

- `Solid` 分支保持现有纯色画刷和 D2D 绘制调用。
- `PointLight` 分支先绘制使用现有 `frame` 与 `framePct` 的完整纯色底边，再使用 `ID2D1RadialGradientBrush` 叠加主光和鼠标光；两束光均按 0%:100%、25%:72%、65%:20%、100%:0% 衰减。
- 每个光源先以 `strokeWidth + 2px` 绘制 12% 强度扩散层，再以原 `strokeWidth` 绘制清晰层；全部使用 `SOURCE_OVER`。
- 内部光源类型缩减为 `Primary`、`Cursor`；同一帧按最终 RGB 与光源类型缓存渐变画刷，绘制控件前只调整透明度。
- 渲染每条边框时先按颜色策略解析唯一光色：非穿透画笔模式的 `PenWhenDrawing` 控件使用 `logoInk->color1.val`，其余使用自身边框色；主光和鼠标光共用该颜色。
- 创建渐变失败时回退到现有纯色路径并只记录一次错误。
- `Bar.RenderingAttribute` 对 `PointLight` 使用扩散层宽度计算脏区外延。

## Input and Animation

- 删除所有点击、双击和拖动的边框光注册；保留普通按钮按压和主按钮尺寸/图标动画。
- 主光固定为 100%，不再维护点击脉冲时间线。
- 鼠标光强度为 `0.60 × smoothstep(enableProgress)`；启用进度仅在动画从关闭切换到开启时用 300ms 推进，位置变化不重置。
- 渲染侧只在鼠标位置变化或鼠标淡入活动时请求边框光下一帧；首次进入静止状态时再绘制一次最终帧。
- 动画关闭时立即隐藏鼠标光，主光和基础灰边保持稳定。

## Compatibility and Failure Handling

- 复用现有 `d2d1_1.h`、`ID2D1DeviceContext`、WARP 和预乘 Alpha target，不新增工程项或第三方依赖。
- Raw Input 使用 Windows 自带 `RegisterRawInputDevices`，不新增链接库、线程或低级钩子；注册失败后鼠标光保持关闭并只记录一次错误。
- 径向渐变通过 `ID2D1RenderTarget` 继承接口创建，符合 Windows 7 SP1 Platform Update 的 D2D 1.1 路径。
- 不使用 Direct2D Effects、D3D 11.1 专属 feature level 或 DirectComposition。
- 任务完成静态和后台构建后保持 `in_progress`，由维护者运行 UI3 做最终视觉验收。
