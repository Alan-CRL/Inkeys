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
- 每个 PointLight 控件把主光和鼠标光的同色 1px 描边记录到一份临时 `ID2D1CommandList`；输入已经包含 480px 点光空间衰减，再通过复用的内置 `D2D1GaussianBlur` 以 `1px × zoom` 标准差和 Soft Border 向轮廓内外连续扩散约 3px。
- Gaussian 输入透明度补偿 1px 线源在标准差 1px 下的中心衰减，使单次输出的默认灰光在中心线附近保持约 30%、画笔光约 20%；同一模糊输出以 `SOURCE_OVER` 合成两次，使有效透明度按 `1-(1-a)^2` 非线性增强（近端约 51%/36%），而远端继续沿同一 Gaussian 尾部连续归零；最后分别绘制主光 100% 与鼠标光 60% 的清晰 1px 边框。
- 每个控件单独记录一份包含两束同色光的 CommandList，避免画笔色外框与 `SwatchFrame` 色块进入同一个模糊输入；Gaussian Effect 复用，CommandList 由局部 `ComPtr` 管理。
- 内部光源类型缩减为 `Primary`、`Cursor`；同一帧按最终 RGB 与光源类型缓存渐变画刷，绘制控件前只调整透明度。
- 渲染每条边框时先按颜色策略解析唯一光色和扩散强度：非穿透画笔模式的 `PenWhenDrawing` 控件使用 `logoInk->color1.val` 与 20%，其余使用自身边框色与 30%；主光和鼠标光共用该结果，不创建另一套异色画刷。
- 创建渐变失败时回退到现有纯色路径；CommandList、Effect 或 Effect 参数失败时只跳过柔光并保留基础边和清晰追光，分别只记录一次错误。
- 扩散直径额外宽度由 `Bar.RenderingAttribute` 提供单一内部常量；Gaussian 标准差由其一半再按 3σ 换算，脏区继续复用该常量，避免参数漂移。
- 双次合成复用同一个 CommandList 和 Gaussian Effect 输出，不扩大标准差、点光半径或脏区；两次均保持 `SOURCE_OVER`，不新增中间纹理或第二套颜色资源。

## Input and Animation

- 删除所有点击、双击和拖动的边框光注册；保留普通按钮按压和主按钮尺寸/图标动画。
- 主光固定为 100%，不再维护点击脉冲时间线。
- 鼠标光强度为 `0.60 × smoothstep(enableProgress)`；启用进度仅在动画从关闭切换到开启时用 300ms 推进，位置变化不重置。
- 颜色块填充继续使用 `pct` 的 0%/100% 目标，点光显现度通过默认 `FramePct`、色块显式 `ObjectPct` 的内部策略复用该进度；独立 `framePct` 使用 0%/18% 目标，并进入现有属性栏的持续时间同步、可见性重建和换边中间关键帧路径。
- 渲染侧只在鼠标位置变化或鼠标淡入活动时请求边框光下一帧；首次进入静止状态时再绘制一次最终帧。
- 动画关闭时立即隐藏鼠标光，主光和基础灰边保持稳定。

## Compatibility and Failure Handling

- 复用现有 `d2d1_1.h`、`ID2D1DeviceContext`、WARP 和预乘 Alpha target，并使用 Windows SDK 的 `d2d1effects.h`、`ID2D1CommandList` 与内置 `CLSID_D2D1GaussianBlur`；不新增工程项、第三方依赖、自定义 Effect 或显式离屏位图。
- Raw Input 使用 Windows 自带 `RegisterRawInputDevices`，不新增链接库、线程或低级钩子；注册失败后鼠标光保持关闭并只记录一次错误。
- 径向渐变通过 `ID2D1RenderTarget` 继承接口创建，符合 Windows 7 SP1 Platform Update 的 D2D 1.1 路径。
- 内置 Direct2D 1.1 Gaussian Blur 和 CommandList 均沿用 Windows 7 Platform Update 链；不使用自定义 Effect、D3D 11.1 专属 feature level 或 DirectComposition。
- 任务完成静态和后台构建后保持 `in_progress`，由维护者运行 UI3 做最终视觉验收。
