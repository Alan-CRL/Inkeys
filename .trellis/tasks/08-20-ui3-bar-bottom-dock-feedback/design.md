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

交互指示器使用独立透明度生命周期，复用现有 160 ms fade-in、200 ms fade-out 和全局动画开关。正在拖动并处于 Capturing 或 Docked 时目标透明度为 1；抬手或脱离底栏后目标透明度为 0。

快速捕获后立即脱离时，从当前透明度连续反向，不重置动画进度。

## Indicator Geometry And Text

指示器 helper 直接接收正常形态下主按钮和主栏的可见描边边界：取两者联合外框的水平中心，并将顶部放在联合外框顶部上方 5 DIP；高度固定为 20 DIP，圆角 6 DIP。文本布局使用现有字体集合和 12 DIP 字号。

宽度解析 helper 接收实际标签宽度和预留标签宽度，结果为 `max(actual, reserved) + 24 DIP`。实际标签为 `UI.Bar.BottomDock.Mode`；预留测量串由 `Mode + " · " + Centered` 组成。该 helper 保持纯逻辑，便于 Headless 覆盖各语言宽度。

## Input Occlusion

成功呈现快照增加指示器的布局坐标边界及“仍有可见像素”标志。窗口消息已转换到布局空间后，输入管线必须先检查该快照，再进入颜色选择器、主按钮、普通按钮、面板和 hover 阶段。

命中指示器时消费鼠标、触摸、双击和滚轮消息，并调用现有取消/清理路径清除下层 hover、pressed 和捕获候选。淡出透明度大于可见阈值时仍发布遮挡；未成功呈现的新边界不参与命中。

## Internationalization

在 `zh-CN.jsonc`、`zh-TW.jsonc`、`en-US.jsonc` 添加两个源翻译键，然后运行 `Scripts/i18n.ps1 sync` 维护其他语言和生成产物。生成头文件不直接编辑。

## Compatibility And Rollback

- 保持现有 HWND、渲染线程和最终 ULW 调用数量。
- 局部 dirty 优化仍用于稳定映射帧，性能回退仅发生在真实映射变化时。
- 底栏反馈直接绘制在主 target，不创建或切换额外 D2D target。
- 呈现决策与指示器保持独立局部提交点，出现回归时可分别回滚而不触碰底栏状态机。
