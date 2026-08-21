# Design

## Boundaries

本任务限定在 UI3 Bar 的底栏纯逻辑、渲染循环、窗口呈现事务、输入分发、i18n 资源和 Headless 测试。底栏状态机、20 DIP 捕获阈值、弹性形变算法及窗口唯一 ULW 提交链保持不变。

## Presentation Transaction

扩展“最后一次成功呈现”快照，保存 `pptSrc`、`psize`、target capacity 和 device generation。为本帧 tuple 增加纯逻辑决策：

- 无成功快照，或任一 tuple 字段变化：整窗替换。
- tuple 完全稳定：允许使用本地 dirty。
- 整窗替换帧先清除完整候选 viewport，再令 `UPDATELAYEREDWINDOWINFO::prcDirty = nullptr`。
- 只有 `UpdateLayeredWindowIndirect` 成功后才能原子推进呈现 tuple 和交互指示器快照；失败保留上一成功状态供重试。

该合同直接解决窗口大小或源映射变化时旧分层窗口像素未被覆盖的问题，而不是扩大局部 dirty 来掩盖症状。

## Visual Lifecycles

交互指示器使用单一 0–1 视觉进度同时驱动透明度和中心等比缩放，动画时长直接复用绘制属性笔类型菜单中问号提示浮窗使用的 `BarUiDefaultOperationDur`，并服从全局动画开关。正在拖动、处于 Docked 且主栏已展开时目标进度为 1；主栏收起、抬手或脱离底栏后目标进度为 0。进入使用 `EaseOutBack`，退出使用 `EaseInBack`，透明度单独裁剪到 `[0, 1]`，缩放保留 Back 上溢。

快速捕获后立即脱离或动画中途收起主栏时，从当前视觉进度连续反向，不重置动画进度；全局动画关闭时直接切换到完整尺寸或零尺寸。

## Indicator Geometry And Text

指示器 helper 直接接收正常形态下主按钮和主栏的可见描边边界及实际文字宽高：取两者联合外框的水平中心，竖直中心取经过底栏形变映射后的主栏可见上边框；高度固定为 30 DIP，圆角 6 DIP。文本布局使用现有字体集合和主栏标准 2x2 按钮的 13 DIP 字号。

宽度解析不再读取预留标签。单侧 padding 为 `max(0, (30 DIP - actualTextHeight) / 2)`，最终宽度为 `actualTextWidth + 2 * padding`；中心缩放 helper 围绕完整几何中心生成实际绘制和命中边界。两个 helper 保持纯逻辑，便于 Headless 覆盖语言宽度、异常 metrics、100%/150% 缩放和 Back 上溢。

## Indicator Rendering And Lighting

指示器仍在主栏及面板内容之后、调试覆盖层之前绘制。Surface 使用主栏 `Surface` 色、80% 峰值不透明度，1 DIP 边框使用 `SurfaceFrame` 色、18% 峰值不透明度，文字使用未选中按钮的 `TextPrimary` 色和粗体字重。边框走 `BarUiFrameRenderingEnum::PointLight`，其 `framePct`、`frameLightPct`、第一光源、第三光源比例和光色与主栏本体保持一致；中心缩放时复用完整尺寸的 diffuse mask 归一逻辑。

指示器不加入长期 `shapeMap`，而是在单帧内构造瞬态 Shape 并沿用唯一主 target。dirty 事务显式合并缩放前后外框以及第一/第三光源影响范围；viewport 预测使用同一 Back 极值，并额外覆盖随缩放变化的描边、固定 Gaussian 外扩与抗锯齿余量，禁止吸附首帧裁掉最上缘。成功呈现快照发布实际缩放后的命中边界。第三光源可见区域列表从同一成功快照追加指示器区域，失败帧不得提前发布。

## Input Occlusion

成功呈现快照增加指示器实际缩放后的布局坐标边界及“仍有可见像素”标志。窗口消息已转换到布局空间后，输入管线必须先检查该快照，再进入颜色选择器、主按钮、普通按钮、面板和 hover 阶段。

命中指示器时消费鼠标、触摸、双击和滚轮消息，并调用现有取消/清理路径清除下层 hover、pressed 和捕获候选。淡出透明度大于可见阈值时仍发布遮挡；未成功呈现的新边界不参与命中。

## Internationalization

在 `zh-CN.jsonc`、`zh-TW.jsonc`、`en-US.jsonc` 添加两个源翻译键，然后运行 `Scripts/i18n.ps1 sync` 维护其他语言和生成产物。生成头文件不直接编辑。

## Compatibility And Rollback

- 保持现有 HWND、渲染线程和最终 ULW 调用数量。
- 局部 dirty 优化仍用于稳定映射帧，性能回退仅发生在真实映射变化时。
- 底栏反馈直接绘制在主 target，不创建或切换额外 D2D target。
- 呈现决策与指示器保持独立局部提交点，出现回归时可分别回滚而不触碰底栏状态机。
