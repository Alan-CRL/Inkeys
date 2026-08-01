# UI3 同窗简易颜色选择器 — Technical Design

## Boundaries

- `Bar.Main.cpp/.cppm` 负责第 12 色块、面板布局、命中、动画、输入队列、脏区和设备资源。
- `Bar.State.cppm` 保存渲染线程与交互线程共享的打开、色系、选点、保持及预览状态。
- `IdtDrawpad.cpp` 的现有低级键盘钩子只负责在面板打开时调用 `TryQueueColorPickerKeyboardInput(BYTE, bool)`；颜色计算和状态写入仍在 Bar 交互线程串行完成。
- 不修改 `IdtFloating.cpp`，不创建新窗口或新配置模型。

## State Model

颜色选择器状态分为四组：

1. 面板状态：打开目标、亮/暗色系、打开/关闭动画进度。
2. 选点状态：归一化色相坐标、纵向坐标、当前色板是否可精确表示。
3. 指针状态：按压、最后稳定屏幕坐标、稳定起始时间、保持提示进度、锁定标志。
4. 预览状态：来源（指针/键盘）、当前颜色、锚点、可见目标、最后键盘输入时间与短入退场动画。

跨线程字段使用现有 `IdtAtomic` 约定。所有画笔颜色写入都在 Bar 交互线程发生，避免钩子线程直接调用 `SetPenColor`。

## Color Mapping

- `x ∈ [0,1)` 映射完整 HSV 色相。
- 亮色系：由 `white` 与纯色按 `y` 线性混合，即饱和度为 `y`、明度为 1。
- 暗色系：由纯色与 `black` 按 `y` 线性混合，即饱和度为 1、明度为 `1-y`。
- RGB 反投影时，亮色系仅在 HSV 明度为 1 时精确；暗色系仅在 HSV 饱和度为 1 时精确。否则只保存最近点并隐藏标记。
- 横向键盘移动按色板实际宽度换算 2 DIP 并循环；纵向同样换算但夹紧。

## Input Flow

```text
Low-level keyboard hook ──TryQueue──▶ Bar message queue ──▶ picker state ──▶ SetPenColor
Pointer/touch message ────────────────────────────────────▶ picker state ──▶ SetPenColor
                                                              │
                                                              └──▶ request bounded render
```

- 指针按下/移动与键盘按下调用 `SetPenColor(color, false)`。
- 指针松手或最后一个方向/WASD 键抬起调用 `SetPenColor(color, true)`。
- 颜色锁定只影响当前指针按压；捕获结束统一清除保持状态并完成一次提交。
- 按键钩子在排队成功时消费事件，因此面板打开期间不会触发 PPT/快捷键；关闭时返回 false，保持原路径。

## Rendering and Resources

- 复用 Bar 的共享 D3D11/D2D 渲染租约和同一个 layered `floating_window`。
- 缓存三个设备资源：横向色相渐变、亮色纵向透明白覆盖、暗色纵向透明黑覆盖。只在 device generation 变化时创建，在 `DiscardDeviceResources` 中释放。
- 第 12 色块使用同一色相画刷绘制静态圆角外圈；色板先绘制横向色相，再绘制纵向覆盖。
- 保持进度抽取为通用圆环绘制帮助函数，粗细与颜色选择器共用几何语义。
- 面板、色板、提示和预览在变化时将旧/新边界加入 dirty union，并加入第三光源可见区域；静止时不设置 sustain 标志。

## Animation

- 面板：短距离位移、缩放和淡入淡出。
- 色系切换：前后色板短交叉淡变；当前画笔不变。
- 选点、颜色和跟手位置：直接更新，无插值。
- 预览：固定在顶部色系切换右侧预览槽，作为面板本体一部分随面板进度显示；颜色直接跟随当前画笔，不显示文字，无独立入退场或 3 秒键盘淡出。
- `BarUiAnimationEnabled == false` 时所有动画直接到达目标状态。

## Compatibility and Rollback

- 现有 11 色块枚举顺序与点击行为保持不变，第 12 槽只追加新命中对象。
- 键盘钩子只有在 `TryQueueColorPickerKeyboardInput` 返回 true 时提前返回，面板关闭时零行为变化。
- 设备资源创建失败时本帧跳过选择器绘制并沿用 Bar 的重建路径，不影响主栏其他控件。
- 回滚可按 `IdtDrawpad.cpp` 键盘入口、Bar 交互状态、Bar 渲染资源三个边界逆序移除，无配置迁移成本。
