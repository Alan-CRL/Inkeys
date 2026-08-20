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

## Background Decoration Layer

旧固定捕获提示由缓存的 premultiplied `ID2D1Bitmap1` 装饰层替代。该 bitmap 在 Bar 的 D2D device context 上作为小尺寸离屏 target 创建，并只在 device、DPI、像素尺寸或样式版本改变时重绘。

背景几何由底栏纯逻辑 helper 计算：取正常形态下主按钮、主栏和可见描边的联合外接矩形，再将左、右、上各扩大 10 DIP，底边保持不变。最终底边钳制到任务栏上沿。绘制 10 DIP 圆角、无描边、`#0078D4` 18% 峰值填充。

最终主目标每帧顺序为：

1. Clear 主目标。
2. 合成背景装饰 bitmap，并应用当前动画透明度。
3. 绘制全部主栏与面板。
4. 绘制前景交互指示器。
5. 绘制调试覆盖层。

装饰 bitmap 是 GPU/D2D 渲染资源，不是 CPU staging；仍只执行一次 GDI/ULW 提交。设备或 target 丢失时与现有 Bar 资源同时释放。

## Visual Lifecycles

底栏纯逻辑层增加两个独立透明度生命周期，复用现有 160 ms fade-in、200 ms fade-out 和全局动画开关。

- 背景板：真实 `Floating -> Capturing` 后目标透明度为 1；拖动结束且吸附回弹稳定前保持；捕获中途脱离则目标透明度为 0。
- 交互指示器：正在拖动并处于 Capturing 或 Docked 时目标透明度为 1；抬手或脱离底栏后目标透明度为 0。

快速捕获后立即脱离时，从当前透明度连续反向，不重置动画进度。

## Indicator Geometry And Text

指示器以背景板顶部为基准向下 5 DIP，水平居中，高 20 DIP，圆角 6 DIP。文本布局使用现有字体集合和 12 DIP 字号。

宽度解析 helper 接收实际标签宽度和预留标签宽度，结果为 `max(actual, reserved) + 24 DIP`。实际标签为 `UI.Bar.BottomDock.Mode`；预留测量串由 `Mode + " · " + Centered` 组成。该 helper 保持纯逻辑，便于 Headless 覆盖各语言宽度。

## Input Occlusion

成功呈现快照增加指示器的布局坐标边界及“仍有可见像素”标志。窗口消息已转换到布局空间后，输入管线必须先检查该快照，再进入颜色选择器、主按钮、普通按钮、面板和 hover 阶段。

命中指示器时消费鼠标、触摸、双击和滚轮消息，并调用现有取消/清理路径清除下层 hover、pressed 和捕获候选。淡出透明度大于可见阈值时仍发布遮挡；未成功呈现的新边界不参与命中。

## Internationalization

在 `zh-CN.jsonc`、`zh-TW.jsonc`、`en-US.jsonc` 添加两个源翻译键，然后运行 `Scripts/i18n.ps1 sync` 维护其他语言和生成产物。生成头文件不直接编辑。

## Compatibility And Rollback

- 保持现有 HWND、渲染线程和最终 ULW 调用数量。
- 局部 dirty 优化仍用于稳定映射帧，性能回退仅发生在真实映射变化时。
- 若装饰 bitmap 创建失败，本帧可跳过背景板但继续主栏呈现；设备恢复后重建。
- 各功能按呈现决策、背景层、指示器三处局部提交点组织，出现回归时可分别回滚而不触碰底栏状态机。
