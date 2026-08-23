# Design

## Boundaries

本任务限定在 UI3 Bar 的底栏纯逻辑、渲染循环、窗口呈现事务、输入分发、i18n 资源和 Headless 测试。竖向底栏状态机保持 20 DIP 阈值不变，正交的水平居中状态单独使用 40 DIP 阈值；弹簧常量及窗口唯一 ULW 提交链继续复用。

## Presentation Transaction

扩展“最后一次成功呈现”快照，保存 `pptSrc`、`psize`、target capacity 和 device generation。为本帧 tuple 增加纯逻辑决策：

- 无成功快照，或任一 tuple 字段变化：整窗替换。
- tuple 完全稳定：允许使用本地 dirty。
- 整窗替换帧先清除完整候选 viewport，再令 `UPDATELAYEREDWINDOWINFO::prcDirty = nullptr`。
- 只有 `UpdateLayeredWindowIndirect` 成功后才能原子推进呈现 tuple 和交互指示器快照；失败保留上一成功状态供重试。

该合同直接解决窗口大小或源映射变化时旧分层窗口像素未被覆盖的问题，而不是扩大局部 dirty 来掩盖症状。

## Visual Lifecycles

交互指示器使用单一 0–1 视觉进度同时驱动透明度和中心等比缩放，动画时长直接复用绘制属性笔类型菜单中问号提示浮窗使用的 `BarUiDefaultOperationDur`，并服从全局动画开关。手势资格初始为 false；浮动主栏真实进入底栏，或普通/居中模式发生任意双向切换时置 true。资格在仍竖向吸附且持续拖动期间保持，只按当前模式双向切换文案；抬手、取消、折叠或竖向脱离时清除，同一手势重新进入底栏可重新建立。底栏内起拖但未发生上述转换、启动和程序化自动居中不建立资格。进入使用 `EaseOutBack`，退出使用 `EaseInBack`，透明度单独裁剪到 `[0, 1]`，缩放保留 Back 上溢。

快速捕获后立即脱离或动画中途收起主栏时，从当前视觉进度连续反向，不重置动画进度；全局动画关闭时直接切换到完整尺寸或零尺寸。

## Indicator Geometry And Text

指示器 helper 直接接收正常形态下主按钮和主栏的可见描边边界及实际文字宽高：取两者联合外框的水平中心，竖直中心取经过底栏形变映射后的主栏可见上边框；高度固定为 30 DIP，圆角 6 DIP。文本布局使用现有字体集合和主栏标准 2x2 按钮的 13 DIP 字号。

单侧 padding 为 `max(0, (30 DIP - actualTextHeight) / 2)`，最终宽度为 `actualTextWidth + 2 * padding`；中心缩放 helper 围绕完整几何中心生成实际绘制和命中边界。渲染线程持有一个持久 `BarUiWordClass`：可见状态在“底栏模式”与“底栏模式 · 居中”之间复用 `TransitionToString`，变宽在换字中点前完成、变窄在中点后完成；隐藏时直接准备下次正确文案。helper 保持纯逻辑，便于 Headless 覆盖语言宽度、异常 metrics、100%/150% 缩放和 Back 上溢。

## Horizontal Center State

水平状态由 `BarBottomDockCenterMode { Free, Centered }` 与独立阶段组成，和竖向模式在同一个 transition serial 中发布。交互侧以最后成功呈现的主按钮/主栏联合外框中心为基准：竖向已 Docked、主栏展开且原始外框中心进入 `monitorBounds` 中点左右各 40 DIP 时捕获；严格越界时脱离。竖向脱离或折叠会强制水平进入 Free 恢复。

渲染侧持有主按钮刚性抓手偏移和主栏远端弹簧。逻辑基础外框仍锚到显示器中心，但主按钮及 Logo 的水平比例始终为 1，只按原始抓手偏移刚性跟手；主栏背景及其普通按钮、图标和文字则以近端随抓手、远端趋向稳定居中边界的仿射映射绘制。捕获与脱离均从最后成功呈现的实际远端按新旧 HWND 位移反推弹簧初值，恢复中重新捕获也不回到理论位置。越过中心时同一映射由拉伸连续转为压缩，左右展开镜像；两种水平变换分别与既有竖向果冻组合。绘制属性、几何属性和 More 等根面板按各自按钮锚点的映射差值做刚性平移。

折叠目标发布时水平模式退出但主按钮 X 不变。底栏重新展开时，渲染线程用最终展开联合外框判断 40 DIP 捕获带；符合时建立无提示的程序化 Capturing。启动已有的中置展开布局直接发布 `Centered / Stable`。

## Two-Axis Presentation

成功呈现快照加入水平模式、阶段、主按钮刚性偏移、未形变主体中心、主栏实际端点、远端弹簧映射、本帧居中布局补偿和实际联合外框中心。tracker 只消费指针驱动的未形变主体中心；普通形态 barrier 仅确认窗口位移已提交，不得重设抓手或 tracker 基准，只有 DPI/显示环境变化可用未形变快照重基准。直接移动失败回滚和 ULW 失败重试都必须原子处理两轴 tuple。

主栏方向由四个渲染线程状态共同管理：当前布局目标、最后成功稳定呈现方向、居中会话锁存方向和显式换向批次。桌面首次放置在任何方向分类前把前三者初始化为向右展开，白板入口则从其已有方向初始化。进入居中会话时只锁存最后成功稳定方向；`PositionUpdate()` 的中线结果仍可作为请求产生，但不能覆盖有效方向。非居中真实换向必须等时间线、主栏 `x/w` 和全部换向中点收敛并成功呈现后，才更新稳定方向。

居中展开帧在布局动画推进前检查显式换向批次，以及主栏、按钮位置、图标/文字透明度和边框光透明度上的完整换向中点。发现遗留批次时，当前视觉值成为普通布局批次的新起点，主栏 `x/w/pct/framePct`、按钮位置、按钮/图标/文字透明度及边框光透明度均以 `forceRestart + nullopt` 清除中点；展开沿用 `EaseOutBack`，收缩沿用 `EaseOutCubic`。该处理只在主栏布局提交点执行，不修改 `BarUiValueClass` / `BarUiPctClass` 的同目标行为，主按钮点击脉冲等独立关键帧保持不变。

非拖动居中态先按当前插值布局计算实际联合中心，再把差值作为统一刚性平移叠加到水平映射。“可重基准”同时要求主栏时间线、主栏 `x/w`、显示位置动画和水平弹簧全部收敛，且没有 pending/in-flight 事务。补偿帧成功后只记录 pending；下一强制帧在布局提交前把补偿吸收到主按钮与 display center，并同步重基准 committed anchor，使零补偿映射与上一成功帧落在相同屏幕像素。该帧强制完整 dirty、空 `prcDirty` 和整窗 ULW 替换。失败时恢复主按钮锚点、display center、水平映射、补偿、committed anchor 和方向 tuple，保留 pending 重试；成功后才发布新的命中/映射快照并清除 pending。pending/in-flight 始终属于活跃渲染，因此成功后即使没有输入，也会再运行一次最终空闲帧把 viewport 收紧。dirty、viewport、PointLight 逆映射、第三光源和命中仍须区分主按钮刚性变换与主栏主体映射；容量包络同时预留横纵 24 DIP 形变及 Gaussian 外扩。

## Indicator Rendering And Lighting

指示器仍在主栏及面板内容之后、调试覆盖层之前绘制。Surface 使用主栏 `Surface` 色、80% 峰值不透明度，1 DIP 边框使用 `SurfaceFrame` 色、18% 峰值不透明度，文字使用未选中按钮的 `TextPrimary` 色和粗体字重。边框走 `BarUiFrameRenderingEnum::PointLight`，其 `framePct`、`frameLightPct`、第一光源、第三光源比例和光色与主栏本体保持一致；中心缩放时复用完整尺寸的 diffuse mask 归一逻辑。

指示器不加入长期 `shapeMap`，而是在单帧内构造瞬态 Shape 并沿用唯一主 target。首次激活帧在缩放仍为零时就显式把上次成功外框、当前外框及完整 Back 峰值包络加入业务 damage；包络覆盖填充、描边、第一/第三光源、固定 Gaussian 和抗锯齿余量。后续 dirty 与 viewport/capacity 继续使用同源几何，失败呈现保留完整 damage，禁止吸附首帧裁掉最上缘。成功呈现快照发布实际缩放后的命中边界。第三光源可见区域列表从同一成功快照追加指示器区域，失败帧不得提前发布。

指示器的竖直中心读取形变后的主栏上边框，因此底栏横纵映射也是该瞬态视觉的上游变化源。映射改变时，只要指示器当前进度非零、目标仍可见或上次成功边界仍存在，就必须标记指示器稳定视觉键；随后由 tracker 观察同源完整包络并合并旧新边界，不能只依赖文字、宽度或显隐动画的变化通知。

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
