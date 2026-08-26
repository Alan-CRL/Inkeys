# PPT 翻页控件主栏化：技术设计

## 设计目标

本轮不是继续修补 `BarSurfaceScene` 的近似按钮，而是把主栏当前真实使用的按钮行为抽成唯一共享运行时，再让 Main Bar 与 PageControl 同时成为它的调用方。独立 HWND、D2D target 和输入状态必须保留；独立资源不允许成为复制布局/交互/绘制算法的理由。

## 模块边界

- `Inkeys.UI.Bar`：拥有按钮/背景的尺寸解析、内部内容布局、hover/press 状态机、内容转换、动画推进、绘制、命中和 damage 算法。Main Bar 与 PageControl 必须调用相同导出入口。
- `Inkeys.UI.PageControl`：拥有四个共享 HWND/RenderPipeline 客户端、每窗资源、surface 拓扑与屏幕锚点、PPT 专属拖动条、页码内容请求、工作区状态机和业务回调路由；不得解释 Bar 按钮内部视觉状态。
- `Inkeys.UI.Ppt`：保留 COM/WPS 状态、页级墨迹、翻页命令、用户位置/缩放配置，只向 PageControl 发布不可变 PPT 快照和回调。
- `Inkeys.UI.Whiteboard`：保留页数据、事务语义和换页回调，只发布 Whiteboard 快照；不拥有分页 renderer，也不继承 PPT 输入能力。
- Window/RenderPipeline：继续保留 `PptBottomLeft`、`PptBottomRight`、`PptMiddleLeft`、`PptMiddleRight` 四个客户端；`PptExitShow` 保持删除。

`BarSurfaceScene` 不得继续作为第二套按钮引擎。它可以保存每窗稳定按钮实例、PPT-only DragHandle 和 `hovered/pressed` id 路由，但标准按钮的内部位置、hover/press、内容转换、动画值、draw 与圆角 hit 必须完全委托共享 Bar 入口；Scene 只负责把共享结果映射到本窗资源、damage 和 present。

## 共享 Bar 按钮运行时

Bar 层已落地以下共享边界；后续调整必须同步更新项目 code-spec，不能把入口重新藏回 Main RenderLoop/Interaction 的局部函数：

~~~cpp
BarButtonVisualMetrics ResolveBarButtonVisualMetrics(
    BarButtonVisualLayoutKind) noexcept;
BarButtonVisualPoint ResolveBarButtonChildTopLeft(...) noexcept;
BarButtonVisualInheritance PrepareBarButtonVisualInheritance(
    BarButtonClass&, const BarUiInheritClass&,
    BarUiWordClass* secondary = nullptr) noexcept;
bool StartBarButtonHoverVisual(BarButtonClass&) noexcept;
bool StopBarButtonHoverVisual(
    BarButtonClass&, bool immediate, bool preserveVisual = false) noexcept;
bool UpdateBarButtonHoverVisual(
    BarButtonClass&, bool visible, bool hoverAllowed,
    double fadeDurationSeconds) noexcept;
void SetBarButtonPressedVisual(BarButtonClass&, bool pressed) noexcept;
void RetargetBarButtonInteractionVisual(
    BarButtonClass&, bool visible, bool enabled, bool selected,
    double durationSeconds) noexcept;
bool DrawBarButtonVisual(
    BarUIRendering&, ID2D1DeviceContext*, BarButtonClass&,
    const BarUiInheritClass&, const BarButtonDrawOptions& = {});
bool DrawBarBackgroundVisual(
    BarUIRendering&, ID2D1DeviceContext*, BarUiShapeClass&,
    const BarUiInheritClass&, RECT* targetRect = nullptr, bool clip = false);
bool BarUiRoundedRectContainsPoint(
    int mx, int my, double zoom, double leftDip, double topDip,
    double widthDip, double heightDip, double radiusXDip,
    double radiusYDip, double epsilon = 1e-6) noexcept;
~~~

该边界必须包含并统一以下现有主栏合同：

- `oneOne/twoOne/twoTwo` 的按钮外框、圆角、标准图标/标签槽；
- `Showing -> Fading -> None` 悬停阶段、5 秒驻留淡出、移出快速退出；
- 按下从当前 hover 视觉连续接管、`pressScale`、拖出取消、抬起后等待新指针移动；
- `TransitionContent` 的淡出/资源替换/回弹和同批次重定向；
- Shape/SVG/Word/PointLight 绘制、内容缩放、命中与实际动画几何；
- 旧/新外框、光影、抗锯齿与内容转换共同产生的保守 damage。
- 显式按钮父级 `inherit` 先写回按钮继承坐标，再以同一个共享计算解析 SVG 与主/次文字；dirty 和 draw 不得各用一份父坐标。

Main Bar 必须先迁移到这些入口，再接入 PageControl；只让 PageControl 使用新 helper、而 Main Bar 继续走旧局部逻辑，仍视为两套实现。

## Surface 与稳定按钮模型

每个 surface 独立持有 Previous、Page、Next 三枚 `BarButtonClass` 和共享背景状态。Bottom surface 的三枚实例跨 `PptCompact`/`WhiteboardExpanded` 永久稳定；Middle surface 只进入 `Hidden/PptCompact`。DragHandle 是 PageControl 自有的独立 shape/hit region，不放入 Bar widget 集合。

PageControl 可以决定四个控件在 surface 内的顺序、横/竖方向、镜像与屏幕锚点，但必须把标准按钮尺寸和内部内容槽交给共享 Bar 运行时：

| 模式 | Surface | 拓扑 |
| --- | --- | --- |
| PPT | BottomLeft | `5 + Drag(10) + Previous(32.5) + 5 + Page(70) + 5 + Next(32.5) + 5` |
| PPT | BottomRight | `5 + Previous(32.5) + 5 + Page(70) + 5 + Next(32.5) + Drag(10) + 5`；只把 Drag 放在外侧，不反转按钮顺序 |
| PPT | MiddleLeft/Right | 上下外边距 `5`，Drag `10`，Previous `32.5`，间距 `5`，Page `70`，间距 `5`，Next `32.5`；两侧使用相同竖向顺序与箭头方向 |
| Whiteboard | BottomLeft/Right | `5 + Previous(70) + 5 + Page(70) + 5 + Next(70) + 5` |

Drag 是 divider lane，与相邻 Arrow 槽位直接相接；`5 DIP` 间距只存在于真实按钮之间，因此长边保持 `165 DIP`。背景外框由子控件联合目标加主栏内边距得到；紧凑/展开外框直接读取 `BarMainBarCornerRadiusDip`，按钮直接读取 `BarButtonCornerRadiusDip`，边框/内边距也使用主栏单一来源（当前为 `8/4/1/5 DIP`）。Background 与三个按钮共享一个 `BarUiTimelineClass`/批次上下文，PageControl 不复制这些数值。

第一/第三光源都由 Main Bar 发布最终屏幕像素快照。四个 PageControl HWND 只在真实鼠标进入/离开时通知主栏唯一 `Dormant/Inside/Grace` 状态机，并在成功窗口提交后发布可见屏幕边界；Raw Input、5 秒 timer、半径和生命周期强度均不在 Surface 内重建。

## 页码与 SVG 内容

- Page 保持同一实例。PPT 横向使用一个测量后的混合字重行；PPT 竖向使用上下两行；Whiteboard 使用标准 `2x2` 主内容/标签槽。PPT 未知值保留 `-`/`/-`，Bottom/Middle 分别保留 `9999/999` 显示上限。内容布局策略只能提供 text run、字重和槽类型，不能设置分页专用绝对偏移。
- Previous/Next 的 Arrow 统一保留同一 `barMore` SVG 对象；工作区切换只改变现有对象的尺寸、位置、角度和标签透明度，不调用资源替换。
- Whiteboard Next 的语义确实在 Arrow/Add 间变化时，才调用同一个按钮的内容转换切换 `barMore`/`barAdd` 与“右翻页”/“加页”。几何与内容共享批次并行推进；事务锁存保证未变化语义不重启。

## 输入所有权

| 能力 | PPT | Whiteboard |
| --- | --- | --- |
| 普通 click/tap 与标准 hover/press | 是 | 是 |
| Page 业务动作 | no-op | no-op |
| DragHandle 成对拖动 | 是，仅 DragHandle | 否 |
| 长按连续翻页 | 是 | 否 |
| 滚轮翻页 | 是 | 否 |
| 位置/缩放与持久化 | 是 | 否 |
| 翻页事务只锁 interactive | 不适用既有 PPT 命令门禁 | 是，视觉保持稳定 |

PageControl 先按当前成功呈现快照逆映射坐标；PptCompact 下 DragHandle 命中优先，其他区域委托共享 Bar 命中/指针入口。Whiteboard 不构造 DragHandle hit region。键盘闪按只对当前可见、可交互的 PPT Arrow 调用共享 pressed 入口。

## 工作区动画与窗口提交

稳定目标仍为 `Hidden/PptCompact/WhiteboardExpanded`，但动画所有权改为同一批次：背景、三枚按钮、DragHandle 槽位/透明度、SVG/文字内容和屏幕 bounds 从同一成功呈现基线重定向。

- Enter：立即撤销 capture、关闭 DragHandle 命中并锁输入；若 PPT 底栏可见，从当前实际 HWND 位置同步移动到 Whiteboard 固定角落并形变，DragHandle/槽位随批次淡出收拢。背景 `active` 仍只由 Draw3/Freeze readiness 发布。
- Enter 且 PPT 底栏不可见：直接以 Whiteboard 最终位置与几何渐显，不伪造紧凑起点。
- Exit：从 Whiteboard 当前值反向到 PPT 最新运行时位置/紧凑布局；DragHandle 槽位展开并淡入，只有成功呈现稳定 PptCompact 后恢复命中。
- Reverse：布局、屏幕位置、DragHandle、SVG/文字全部从当前动画值重新定向；共享 HWND 不经过隐藏 owner。

PageControl 仍负责 stable backing capacity、logical/presentation 坐标、ULW 提交和窗口 revision；共享 Bar 运行时只返回视觉状态、damage 与 animation-active，不直接调用 Window Service。

## PPT 碰撞与位置

`ResolveRuntimePageControlLayout` 不再接收 `mainBarObstacle`，PageControl 删除 `MainBarObstacle()`、Bar bounds 查询和主栏布局通知。Whiteboard 固定布局使用独立函数，不读取 `PptLayoutState`。

- 手动拖动：只更新命中 pair；与另一 pair 冲突时回退最近可行 candidate，不移动另一 pair。
- 显示/DPI/缩放纠偏：先约束 bottom pair，再让 middle pair 搜索最近可行位置；空间仍不足时只降低 middle pair 的运行时 scale。
- 保存：只有 PPT 用户拖动完成才写配置；自动纠偏、Whiteboard 位置和主栏移动均不得写入或改变保存值。

## 兼容、风险与回滚

- 保留 EndShow A2、旧 JSON 读写、PPT/WPS COM、`PptInfoStateBuffer`、页级墨迹、四 HWND owner/Z 序和 RenderPipeline 线程模型。
- 主要风险是从 Main RenderLoop/Interaction 抽取共享入口时改变主栏现有行为。迁移顺序必须先为 Main Bar 建立等价测试，再让 PageControl 接入；任一阶段失败可回滚到上一阶段而不触碰业务层。
- 不以继续修改 `BarSurfaceScene::ApplyButtonInteractionTargetsLocked`、分页 icon offset 或专用 duration 作为回滚/临时完成方案；这些路径本身就是要移除的错误所有权。

## 验证设计

- 共享运行时纯测试：相同 request/state/time 对 Main Bar 与 PageControl 产生相同外框、内容槽、hover/press、transform、hit 和 damage。
- 坐标/光源纯测试：非零 Surface 原点下 SVG/文字继承显式按钮父级；共享屏幕光源点按 logical bounds 与 presentation outset 映射，不读取分页本地指针。
- PageControl 状态测试：实例 ID 稳定、普通 Arrow 不换资源、Arrow/Add 单次同批转换、DragHandle 时序、反向重入和首次渐显。
- 输入矩阵测试：Whiteboard 拒绝 drag/wheel/long-press/persist，PPT 保留；切换期间 capture/input 门禁正确。
- 碰撞测试：无 Bar 参数/障碍，bottom 优先、middle 回退、手动 pair 不推动另一 pair、自动结果不写保存状态。
- 回归：EndShow/A2、窗口生命周期、owner/Z 序、COM buffer 与旧 JSON 静态/无窗口测试保持。
