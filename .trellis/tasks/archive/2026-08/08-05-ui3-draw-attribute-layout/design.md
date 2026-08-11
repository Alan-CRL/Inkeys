# 技术设计

## 1. 边界与复用策略

改动集中在 `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp`、`Bar.Main.cppm` 与 `Bar.State.cppm`。不修改绘制引擎、配置或传统 UI；现有 `barThicknessAdjust.svg`、`colorSelect.svg`、`barQuestion.svg` 及资源登记继续复用，因此预计无需修改 `Inkeys.rc`。

本任务保持现有“状态目标 -> `BarUiValueClass` 动画 -> 派生绝对几何 -> D2D 渲染/命中”的链路：

- 分割线沿用 `BarUiShapeClass + PointLight`，并与 `GeometryAttributeBar_Divider` 共用线宽、圆角和第三光强度常量。
- 笔类型菜单沿用颜色选择浮层的绝对坐标、方向判定、Back/Cubic 曲线和窗口内 top-layer 输入顺序。
- 标注线帮助继续使用现有 annotation hover/grace/pinned/close 状态、定时器、问号 SVG、标题/正文和 popup surface。
- “自由线”保持隐式当前模式，不新增业务枚举或持久化状态；只新增菜单开关、方向锁存、按压等瞬态 UI 状态。

## 2. 控件与状态拓扑

### 2.1 粗细分割线

在 `BarUISetShapeEnum` 中增加一个绘制属性分割线 Shape。初始化参数与几何选择分割线来自同一组共享常量：

- 厚度 `1 DIP`，圆角半径 `0.5 DIP`；
- fill/frame 为 `SurfaceFrame`；
- `frameRendering = PointLight`；
- `frameLightColor = Frame`；
- `framePrimaryLightEnabled = false`；
- 第三鼠标光强度 `0.30`。

`DrawAttributeBar_ThicknessSelect` 继续保存粗细区范围并供预览裁剪/命中推导，但其 fill/frame/frame-light 全部为零，不再绘制旧外框。

### 2.2 笔类型扩展菜单

使用一套跟随“当前选中且支持标注线”的笔型按钮移动的控件，而不是为硬笔、荧光笔各复制一套：

- 右侧扩展命中 Shape；
- 内部分割线 Shape；
- 复用 `barThicknessAdjust` 的小三角 SVG；
- 菜单 Surface；
- 自由线行命中/背景与复用 `colorSelect` 的对钩 SVG；
- 原 annotation label/info/info-hit 迁入禁用的标注线行；
- 原 annotation popup、关闭按钮和文案保持复用。

`Bar.State.cppm` 只增加菜单开关、展开方向锁存和触发器按压等瞬态原子状态。hover stage 和 press-scale 继续放在 `BarUISetClass`，与现有独立按钮一致。菜单不保存 line-mode 选择值。

## 3. 布局计算

### 3.1 分割线与控制行

将当前散落的笔类型列宽/左边界、粗细左区边界与分割线厚度提升为关系常量：

~~~text
penTypeLeft = panelWidth - panelGap - penTypeButtonWidth
dividerLeft = panelGap
dividerRight = penTypeLeft - panelGap
dividerWidth = dividerRight - dividerLeft
~~~

分割线纵坐标从颜色区域靠粗细一侧的边界推导。上下两种布局都按“颜色边界 -> 5 DIP -> 1 DIP 线 -> 5 DIP -> 控制行边界”计算；因为旧布局两侧内容之间只有 `10 DIP`，控制行会按展开方向移动 `1 DIP`，从而给 `1 DIP` 线保留真实且对称的两个 `5 DIP` 空隙。

分割线加入绘制属性现有展开/收起/换边同步动画批次，紧凑关键帧仍围绕 `60 x 30` 面板中心收拢；第三光不因动画阶段被关闭。

### 3.2 完整预览区

`CalculateBarThicknessPreviewGeometry` 改为从动画中的粗细区域、控制行和分割线几何计算可用预览上下边界：

- 外侧保留现有 `BarDrawAttributeGap`；
- 靠控制行一侧从控制行外缘再留 `BarDrawAttributeGap`；
- `previewCenterY` 是这两个边界的严格中点；
- `sliderCenterY = previewCenterY`；
- 预览左右范围不再为已删除的外框预留 frame inset，滑轨仍保留既有内容内边距。

渲染侧删除 `badgeProtectedDepth/centerShift`，普通预览和 Slider 都使用同一中心。hold-lock 文案从 `trackLeft`/内容内边距开始，圆环继续跟随测量后的文字宽度。

### 3.3 笔类型文字与扩展区

笔型图标继续位于按钮左内边距；文字改用 `DWRITE_TEXT_ALIGNMENT_LEADING`，其左边界由“按钮左缘 + 图标内边距 + 图标宽度 + 内容间距”计算。文字右边界在扩展入口显示时止于内部分割线，否则延伸到按钮右内边距，避免仅把原居中文字机械贴边。

扩展入口只根据 `selected && PenModeSupportsAnnotationLine(currentMode)` 派生。入口视觉可从当前动画值淡入/淡出；资格失效当帧即把命中尺寸清零并清除 hover/press。

## 4. Preview / Slider / Overflow 动画状态机

保留当前 Slider 顺序：进入时轨道先拉直、完成后圆点淡入；退出时圆点先淡出、完成后轨道恢复。

在它外层增加 Preview 专属溢出提示门控，使用现有 `BarUiValueClass` 曲线管理视觉进度，不新增业务状态：

~~~text
Preview + overflow
  -> Slider target established
  -> disable overflow hit/hover/pin/timer immediately
  -> overflow badge and popup animate to 0
  -> only after both are hidden, morph preview into slider track
  -> show thumb after track is flat

Slider target cleared
  -> hide thumb
  -> restore track to preview
  -> after preview is fully restored, animate overflow badge back in if still needed
~~~

反向切换只更新目标，所有进度从当前值续接。派生 hit Shape 在非 Preview 状态保持 `0 x 0`；tooltip availability 同时要求实际命中几何可用。进入 Slider 时复用 `CloseDrawAttributeTooltips()` 杀掉宽限定时器，防止旧 timer 回写 hover。

底层“真实粗细超出预览容量”的计算仍读取 `GetPenWidth()`；只改变提示的呈现资格，不钳制宽度，也不影响抬手一次性提交。

## 5. 菜单方向、动画与交互

### 5.1 方向

菜单打开时锁存：

~~~text
openBelow = widgetPosition.primaryBar
outwardDirection = openBelow ? +1 : -1
~~~

目标位置以当前小三角命中区为锚点，在对应边缘外留 `BarDrawAttributeGap`。与颜色选择浮层一致，菜单从较小比例和靠主栏一侧的位移起点，沿 `outwardDirection` 到达目标；打开使用现有 `EaseOutBack` 风格，关闭使用现有 `EaseInCubic` 风格。

若面板换边，先将菜单 open 目标置 false，并在退场期间继续使用旧锁存方向；进度归零后才允许下一次打开锁存新方向。这样最终位置与运动方向不会分叉。

### 5.2 菜单内容

- 自由线行：可点击，复用对钩 SVG，点击后关闭菜单，不写入任何新模式。
- 标注线行：文字与问号为灰色；行本体只消费点击并阻止穿透，不执行模式写入。
- 问号：继续走原 annotation tooltip 的 hover grace、固定、关闭与 popup 内容；popup 的外向位置改由菜单锁存方向推导，不再依赖粗细预览 `previewSide`。

菜单打开时位于绘制属性内容和粗细提示之上。点击外部先关闭菜单，再继续同一消息；点击菜单 Surface 内空白或禁用行消费消息。面板关闭、折叠、笔型切换、能力丢失和线程退出都调用统一关闭清理路径。

## 6. 动画批次、脏区与第三光

- 新增 Shape/SVG/Word 明确加入绘制属性同步动画更新，不依赖容易越界的旧枚举范围假设。
- 菜单、问号 popup 和退场旧边界加入 dirty rect 的新旧并集，避免分层窗口残影。
- 分割线通过现有 `spec.Shape()` 与 `SetFrameDiffuseMaskGeometryScale()` 路径渲染；不新增 effect、brush 生命周期或逐帧资源创建。
- 菜单使用现有全屏 floating window 的绝对逻辑坐标；命中与渲染共享同一派生 Shape，避免最终坐标和动画坐标分离。

## 7. 兼容与回滚

- 不修改配置、资源文件、公开 API 或画笔数据，回滚只需撤销三个 Bar 文件的 UI 对象/状态改动。
- 若新增 SVG 实例，仅从已登记资源重新初始化，继续受设备 epoch 与 `DiscardDeviceResources()` 现有缓存清理管理。
- 主要风险是枚举连续区间循环漏纳入或误纳入新对象、退场期间 hit-test 过早恢复，以及换边中使用目标方向替代锁存方向。实现检查将逐项覆盖这些路径。

## 8. 当前架构限制

仓库没有自动化 UI/动画测试 target；可自动执行的门禁是静态检查和完整 Solution 构建。视觉、鼠标/触摸、快速反向及主栏上下方向仍需在 UI3 运行态手工验证，本任务不会用新的测试框架替代该限制。
