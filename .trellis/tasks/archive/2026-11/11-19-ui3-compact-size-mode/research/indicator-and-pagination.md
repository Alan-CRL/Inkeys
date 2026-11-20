# 调研：状态指示器 + 翻页控件（小尺寸 UI 模式）

> 基于 `refactoring/drawpad-ui` @ 2f2adf9b，只读调研。行号以当前工作树为准。
> 尺寸基准：`ButtonOneSideDip=32.5`、`ButtonGapDip=5`、`ButtonTwoSideDip=70`（`UI3/UI3.Metrics.cppm:11-14`），主栏/主按钮 80（`Bar/Bar.Metrics.cppm:30-38`）。
> 文字宽度为用仓库自带 `Inkeys/src/ttf/HarmonyOS_Sans*_SC` 经 GDI+ Typographic 测得的**近似值**（产品走 DirectWrite，非中文 locale 可能用 Noto 字族，误差约 ±1–2 DIP）。

---

## Part A　状态指示器

### A1. 现状机制

**模块分层**
- 纯模型 + 纯布局：`Inkeys/Inkeys/UI/Bar/Bar.StatusIndicatorModel.cppm`（无 D2D/HWND，`InkeysHeadlessTests/status_indicator_tests.cpp` 直接测）。设计文档里写的 `Bar.StatusIndicator.h` 实际已是这个 module。
- 生产接线、绘制、命中：`Bar/Bar.StatusIndicator.cpp`；类声明 `BarStatusIndicatorPanel` 在 `Bar/Bar.Main.cppm:111`。
- 跨线程：发布 API 只入 mutex 命令队列并唤醒；渲染线程在 `Advance` drain（`Bar.StatusIndicator.cpp:131-148`）。输入线程只读成功呈现后的命中快照（`CommitPresented`，`:256-262`）。

**条目状态机**（`StatusIndicatorPhase{Expanded,Collapsing,Docked,Dismissing,Exiting}`，`Model.cppm:21`）
- `Publish`（`Model.cppm:310-339`）：新 key → `Expanded`（可见时）并 `DismissSelection()` 收回正在看的重新展开/同心弧；同 key → 只更新文字/颜色/动作，不重放。不可见（类型被关）→ 直接 `SnapDocked` 且 presence=0。
- 队首计时：`Front()` = 第一个可见 `Expanded`（`:563-567`）；`RefreshFront`（`:620-625`）只给队首设 `holdStart/holdEnd=now+Hold(3s)`。`Advance`（`:447-498`）里队首到期 → `Expire`（`:626-632`）：通知 → `Dismissing`；持久 → `Collapsing`，`morphTarget=1`、`flightTarget=1`、`flightDelay=0.1s`。
- `AdvanceMorph`（`:645-673`）：morph/flight 弹簧推进；flight 越过 `LandAt=0.97` 时才分配 `dockSerial` 占槽并给 bump 冲量；全部落定后 `Collapsing→Docked`。
- 交互：`ClickExpanded`（`:423-438`，胶囊点击 → Expire）；`ClickDocked`（`:397-422`）：未开 Fan 时单个方块 → 有动作（新版本）返回动作，否则"就地重新展开"（flight/morph 目标回 0）；≥2 个 → `fanOpen_=true`，3s 自动关闭；Fan 中点选 → `selected_`（弧上展开胶囊）；`ClickOutside`（`:439-445`）。
- 环（单通道，v4）：`DesiredRing`（`:746`）/`AdvanceRing`（`:759`），双层 outgoing/current 淡入淡出；绘制态（`IdtPen/IdtShape` 或 `drawingPenColorBlend>0`，由 `Bar.RenderLoop.cpp:7899-7904` 传入）环目标为空。

**几何常量**（`Model.cppm:44-61`）
- 胶囊：`CapsuleHeight=30, CapsuleRadius=6, CapsuleIcon=18, CapsuleGap=6, TextSize=13, CapsuleMaxWidth=160`，文字上限 `StatusIndicatorTextLimit()=160-2*6.5-18-6=123`（`:231-235`），超限滚动 `StatusIndicatorScrollOffset`（`:247-252`，3s 内两端各停 0.3s）。
- 停靠方块：`DockSize=18, DockRadius=4, DockIcon=12, DockStep=6, DockInset=14, DockOpacity=.5, DockHitMin=24, MainButtonYield=3`。
- 同心弧（Fan）：`FanSize=28, FanRadius=6, FanIcon=17, FanGap=4, FanClear=6, FanLayerGap=32, FanSpanDegrees=130`。
- 环：`RingWidth=1.5`，边框 `FrameWidth=1`。

**纯布局** `ResolveStatusIndicatorLayout`（`Model.cppm:1088-1288`），输入 `StatusIndicatorLayoutInput`（`:932-938`：主按钮中心/宽/高/n、`below`、`baseBounds`、`work`）：
- 胶囊竖直中心 `StatusIndicatorEdgeY`（`:986-990`）= 主按钮上边框（倒转时下边框），水平居中主按钮；后排向外每层错 `StackStep=4`，最多 3 层（`:1139-1153`）。
- 停靠位 `dockPoint`（`:1101-1112`）：`x = cx + w/2 - DockInset(14)`，`y` = 超椭圆在该 x 处的上边框（`StatusIndicatorSuperHalfHeight`），旧的向左 `DockStep` 错开；**整体夹进 `baseBounds`**（80 高时实际被夹到 `base.top+9`）。
- 收起飞行：`ResolveFlightPath`（`:1047-1059`）三次贝塞尔，起点 = 胶囊左端图标中心，先向内沉 `FlightSink=44`，再到停靠槽；尺寸 30→18、圆角 6→4、透明度后段降到 0.5（`:1154-1180`）。
- Fan：`ResolveStatusIndicatorFanPlacement`（`:993-1024`），以**主按钮中心**为圆心；首层半径 `StatusIndicatorFanLayerRadius`（`:265-271`）= 超椭圆 50° 方向外径 + `FanClear` + `FanSize/2`，层距 32；每层容量 `StatusIndicatorFanLayerCapacity`（`:272-278`）；朝向侧工作区不足时整体镜像；index 0（最新）在最内层最右。Fan 项 18→28 DIP、不透明（`:1186-1196`）；弧上选中再以方块为中心展开为 30 高胶囊（`:1197-1211`）。
- 输出：`entries`（按 z 升序）、`rings[2]`、`normalBounds/overflowBounds/reserveBounds`（`:967-975`）。`reserveBounds` 首帧预留出现回弹峰值、飞行弧线与整个 Fan（`:1243-1275`）。

**接线与绘制**
- `Bar.RenderLoop.cpp:10903-10924`：用 `mainButton->x/y/w/h/n` 与"不含指示器的 visibleContentBounds 反映射"作为 `baseBounds`，调用 `statusIndicator.Layout`，再把 `OverflowBounds()` 并入 `visibleContentBounds`（扩窗只来自越界项）。
- `Bar.RenderLoop.cpp:9579-9584`：在主按钮 + Logo 之后、同一 `SetGripTransform()` 下 `statusIndicator.Draw(...)`。
- 环：`DrawRing`（`Bar.StatusIndicator.cpp:372-417`）按 `main.w/h/n` 生成超椭圆路径（缓存键含宽高），进度段从顶部中点顺时针按路径长度截取；颜色经 OKLCH 校正 `StatusIndicatorColor::Display`。**环几何完全由主按钮当前 w/h 决定**，主按钮变宽后自动跟随。
- 胶囊/方块：`Draw`（`:419-544`），主栏材质 Surface + 1 DIP 强调色点光边框 + 强调色图标；文字 `Word`（13 DIP 粗体，`TextSize*textScale`），裁入胶囊内侧圆角并两端渐隐（`:507-530`）。测宽：`owner.spec.MeasureText(..., 13, BOLD)`（`:174-175`）。
- 命中：`Pointer`（`:295-341`）倒序遍历呈现快照，Dock 命中外扩到 `DockHitMin`，且伸进主按钮超椭圆超过 `MainButtonYield` 的部分让给主按钮（`StatusIndicatorInsideMainButton`，`Model.cppm:977-982`）；在 `Bar.Interaction.cpp:8270-8278` 于形状/橡皮属性之前、主按钮之前消费。
- 窗口合同（spec `rendering-and-ui.md:2910+`，"用户确认 2026-11-18"）：**停靠方块不得影响窗口大小**，稳态 `OverflowBounds()` 为空；胶囊、飞行、Fan 才能越界预留。

### A2. 关键代码位置

| 内容 | 位置 |
|---|---|
| 样式常量 | `Bar/Bar.StatusIndicatorModel.cppm:42-77` |
| 文字宽度/胶囊宽度/滚动 | `Bar.StatusIndicatorModel.cppm:231-252` |
| Fan 半径与容量 | `Bar.StatusIndicatorModel.cppm:254-278` |
| 状态机 Publish/Retire/Click/Advance | `Bar.StatusIndicatorModel.cppm:283-498` |
| 队首、Expire、重新展开、Morph | `Bar.StatusIndicatorModel.cppm:563-673` |
| 布局输入/输出结构 | `Bar.StatusIndicatorModel.cppm:932-975` |
| Fan 放置 | `Bar.StatusIndicatorModel.cppm:993-1024` |
| 主布局（胶囊/飞行/停靠/Fan/包络） | `Bar.StatusIndicatorModel.cppm:1088-1288` |
| 面板 Advance / Layout / Bounds | `Bar/Bar.StatusIndicator.cpp:131-255` |
| 命中 | `Bar.StatusIndicator.cpp:295-341` |
| 环/进度环绘制 | `Bar.StatusIndicator.cpp:343-417` |
| 胶囊/方块绘制 | `Bar.StatusIndicator.cpp:419-544` |
| RenderLoop 布局接线（baseBounds） | `Bar/Bar.RenderLoop.cpp:10903-10924` |
| RenderLoop 绘制位置 | `Bar.RenderLoop.cpp:9564-9584` |
| 主按钮尺寸常量（宽高同一个值） | `Bar.RenderLoop.cpp:932-933`（`mainButtonScale=1.05`、`mainButtonBaseSize=80`） |
| 主按钮 w/h 目标（含点击脉冲、底栏锁定） | `Bar.RenderLoop.cpp:2679-2704` |
| 主栏 x 依赖主按钮宽度 | `Bar.RenderLoop.cpp:3917-3921`（`GetW()/2 + w/2 + 10`）；折叠 `w=80`：`:3902`；换边关键帧 `80.0`：`:3910,3913` |
| Logo 居中于主按钮 | `Bar.RenderLoop.cpp:9573`（`Inherit(Center, MainButton)`） |
| 交互入口 | `Bar/Bar.Interaction.cpp:8270-8278` |
| 历史任务 | `.trellis/tasks/11-02-ui3-status-indicator/{prd,design}.md`（v2/v3/v4 决策覆盖 R2–R7.1） |

### A3. 小尺寸改造切入点

需求：不显示胶囊 → 主按钮变宽、主图标右侧显示文字（边框颜色/进度条照旧）→ 完成后主按钮右上角出现缩小版指示器按钮 → 点击变大（与大尺寸同大），扇形位置重算。

1. **模型保持不变的部分**：注册表、去重、过滤、队首 3s 计时、`Expire`、环单通道/进度、Fan 状态（`fanOpen_/selected_`）均与外观无关，可直接复用。建议给 `StatusIndicatorLayoutInput` 增加 `compact` 标志（或独立的 compact 样式参数组），由 `ResolveStatusIndicatorLayout` 分支，不改状态机。
2. **"主按钮变宽 + 文字"取代胶囊**：
   - 布局层：compact 下 `Expanded` 队首不再产出 Capsule 项，改为新增输出（例如 `mainText{key, accent, textWidth, scroll, opacity}`，以及"期望主按钮额外宽度"）。后排 `Expanded` 不显示（大尺寸只露一条边，compact 无处可露）。
   - 主按钮宽度：`mainButtonBaseSize` 是 `static constexpr` 且 w/h 共用（`RenderLoop.cpp:933, 2683-2704`）。需拆成 `baseW/baseH`，宽度目标 = compact 主按钮宽 + 文字段（图标后间距 + 文字宽 + 右内边距，文字宽上限可沿用 123 DIP 并沿用滚动函数）。注意点击脉冲 `mainButtonScale` 对宽高等比放大、底栏锁定时 `SetDirect`。
   - **反向依赖**：宽度来源于指示器模型（渲染线程 Advance 之后才知道队首），而主按钮/主栏布局在 RenderLoop 前段（`~2679`、`~3917`）已提交目标。需要确定一帧内顺序：`statusIndicator.Advance` 在 `:7899`，晚于主按钮目标设置 → 要么把宽度需求放到下一帧，要么把"队首文字宽"提前读取。**这是最大的结构难点。**
   - 主栏位置：主栏 `targetX` 用 `MainButton->GetW()`，主按钮变宽会推动主栏、改变整体宽度、居中/吸附位置与窗口包络；`BarMainButtonToMainBarGapDip`、橡皮/撤回属性面板锚点（`Bar.EraserAttributeLayout.cppm:141`、`Bar.RecallAttributeLayout.cppm:82`）也以主按钮为基准。需确认变宽方向（见待确认）。
   - Logo：当前 `Inherit(Center, MainButton)`，变宽时需改为左侧固定在原 compact 位置。
   - 文字绘制：可复用 `Draw` 里的 Word + 渐隐裁剪逻辑（`StatusIndicator.cpp:507-530`），裁剪区改为主按钮内侧文字区。颜色沿用 `StatusIndicatorColor::Display(accent, material)`。
   - 边框颜色/进度条：`DrawRing` 已按 `main.w/h/n` 取路径，主按钮变宽后自动画在宽超椭圆上；仅需确认宽高比大时超椭圆 n 与圆角观感（`ringKey_` 会随宽度动画每帧重建路径——见风险）。
3. **收起动画**：大尺寸是"胶囊收进左端图标 → 沿弧飞到停靠槽"（`Model.cppm:1154-1180`）。compact 需重新定义：主按钮宽度收回 + 文字淡出，同时从主按钮右端/文字区"飞出"缩小版方块到右上角。`ResolveFlightPath` 起点需改为 compact 起点；`FlightSink=44` 对 35 高主栏过大（会飞到主栏下方很远），需按 compact 尺寸参数化。
4. **缩小版停靠方块**：`DockInset=14`、`DockSize=18` 是为 80 主按钮设计。若 compact 主按钮约 35×35：`dockX0=cx+17.5-14=cx+3.5`，几乎在中心；18 DIP 方块夹进 baseBounds 后会盖住主按钮约一半高度。需要 compact 专用 `DockSize/DockIcon/DockInset/DockStep`（"缩小版"），并重新评估 `MainButtonYield` 让位与 `DockHitMin=24` 外扩（方块太小、主按钮太小，两者命中区会大面积重叠）。
5. **点击变大（与大尺寸同大）**：Fan 尺寸 `FanSize=28`、选中胶囊 30 高本身是常量，不随主按钮变化，满足"同大"。需要改的是位置：
   - `StatusIndicatorFanLayerRadius` 依赖 `mainWidth/mainHeight/n`，compact 35×35 时首层半径 ≈ 19.6+6+14 ≈ 39.6 DIP（大尺寸 ≈ 64.7），每层容量从 5 降到 3（按 `floor(130°/(32/r))+1` 估算），层数增多、向外延伸更快；且以主按钮中心为圆心、正上方对称，会覆盖主栏上方区域。若用户期望"与大尺寸展开后位置/半径一致"，则应在 compact 下给 Fan 传入虚拟的 80×80 主按钮尺寸或单独半径参数。
   - 单个方块点击：大尺寸是"就地重新展开胶囊"（飞回主按钮上边框中心、30 高胶囊）。compact 下是重新展开胶囊，还是让主按钮再次变宽显示文字，需确认。
   - 镜像判定 `work.top/bottom` 与 `below`（主栏在屏幕上方时向下）逻辑可复用。
6. **窗口/脏区**：主按钮变宽属于主按钮几何（进入 baseBounds、扩窗），不是指示器 overflow；Fan/选中胶囊继续走 `reserveBounds`。compact 下若方块想探出主栏上沿，会与"停靠方块不得影响窗口大小"合同冲突（会被夹回或被窗口裁掉）。
7. **命中**：主按钮命中用呈现形状（`Bar.Interaction.cpp:1194-1196`），变宽后自动覆盖文字区；但点击"变宽的主按钮"是否应当等同大尺寸"点胶囊收起"（`ClickExpanded`）还是正常主按钮功能（折叠/展开），需确认；若前者，需在指示器 `Pointer` 中新增 MainText 角色或在主按钮点击路径中先询问指示器。

### A4. 难点与风险

- **帧内顺序反转**：主按钮目标（`RenderLoop.cpp:2679-2704`）与主栏 x（`:3917`）先于 `statusIndicator.Advance`（`:7899`）。宽度改由指示器驱动需要重排或延迟一帧，否则首帧文字已出现而主按钮尚未变宽。
- **主按钮宽高拆分的波及面**：`mainButtonBaseSize` 被底栏 dock 线计算使用（`RenderLoop.cpp:2317, 2393, 2456, 2523`）、点击脉冲、主栏换边关键帧常量 80（`:3902/3910/3913`）、启动预览 `NotifyBarFirstCommittedFrame(mainButtonTargetWidthDip)`（`StartupPreview.cpp:991`，变宽中的值不应写入缓存宽度）、`Bar.Initialization.cpp:336-337`、测试 `Bar.EraserAttribute.Test.cpp:1140,3186-3211`。
- **主栏随主按钮变宽移动**：整体宽度变化会触发居中/吸附/视口重算、主栏换边逻辑、橡皮/撤回属性面板锚点移动；PPT 闲置淡化也读主组。
- **环几何缓存抖动**：`DrawRing` 以 `(w,h,n)` 为键重建 `CreateSuperellipsePath(...,128)`（`StatusIndicator.cpp:375-383`），宽度动画期间每帧重建；进度段每帧 `ComputePointAtLength` 192 段。光晕遮罩 `GetGeometryDiffuseMask` 的 variant 只含 `n` 与段长，不含宽高（`:394, 413`）——宽度动画时遮罩缓存键是否会命中旧尺寸需核实（**待确认**，可能产生错位光晕）。
- **停靠方块与小主按钮的命中冲突**：`DockHitMin=24` 外扩 + `MainButtonYield=3` 让位在 35 DIP 主按钮上几乎把方块命中区全部让给主按钮或反之。
- **测试耦合**：`status_indicator_tests.cpp` 硬编码 80×80 主按钮（如 `:41, 492-497`）；spec `rendering-and-ui.md:2910-2975` 的合同条款（胶囊压边框、停靠 18 DIP、Fan 28 DIP）需追加 compact 分支说明。
- **设置切换时的过渡**：大↔小切换时若有胶囊/Fan/重新展开正在进行，需定义归一规则（类似 `SetOptions` 里的 `SnapDocked`）。

### A5. 待确认（产品/设计）

1. compact 主按钮的基础尺寸是多少（35×35？还是其他）？主按钮变宽时向哪侧扩展：仅向主栏方向（推动主栏）、向远离主栏方向（主栏不动，主按钮与主栏重叠/间距变化）、还是两侧对称？折叠态下又如何？
2. 变宽文字的字号/字重与上限宽度：沿用 13 DIP 粗体、文字上限 123 DIP 并滚动？（实测 13 粗体："放映模式"≈52、"New version"≈77、俄文"Показ слайдов"≈95 DIP。）
3. 多个状态排队时，compact 下是否只显示队首文字（大尺寸后排会露出一条强调色边）？队首切换时文字如何过渡？
4. 绘制态（画笔/形状）下是否仍允许主按钮变宽显示文字？大尺寸下胶囊不受绘制门禁，只有环受门禁。
5. 点击变宽中的主按钮：等同"点胶囊立即收起"，还是执行主按钮原功能？
6. "缩小版指示器按钮"具体尺寸（建议 12–14 DIP）？是否允许探出主栏上沿（会与"停靠方块不影响窗口大小"合同冲突，需要放宽或改为完全压在主按钮右上角内侧）？
7. "点击时变大与大尺寸一样大"：单个方块点击后是重新展开 30 高胶囊（大尺寸行为），还是让主按钮再次变宽显示文字？多个方块的同心弧是以 compact 主按钮的实际尺寸计算半径（更贴近、每层更少），还是沿用大尺寸 80×80 时的半径与层容量？
8. 收起动画是否保留"灵动飞行 + 着陆轻弹"，起点是主按钮右端还是文字区？

---

## Part B　翻页控件

### B1. 现状机制

**四个 PPT Surface + 白板**
- Surface：`BottomLeft(0) / BottomRight(1) / MiddleLeft(2) / MiddleRight(3)`；模式 `Hidden / PptCompact / WhiteboardExpanded`（注意：`PptCompact` 只是"PPT 模式"的名字，与本任务的 compact 尺寸无关）。Middle 只切 `Hidden/PptCompact`；Bottom 两个 HWND 在 PPT 与白板之间复用同一组按钮实例形变（spec `rendering-and-ui.md:438`）。
- 每个 HWND 有独立 `BarPageSurface`（`Bar/Bar.Scene.cpp`）持有 DragHandle、Previous、Page、Next、More（`pageButtons[0..4]`）及白板菜单/主题面板。

**标准（当前生产）几何**（`Bar.Metrics.cppm:12-29, 87-128`）
- 按钮全部 2×2（70×70），`Bar.Scene.cpp:567-569` 把 `pageButtons[i]->size` 设为 `twoTwo`，再用 `ResolveBarButtonListLayout(buttonRow, SingleRow)`（`Bar.Scene.cpp:572-590`）排成一行，竖直 surface 把结果转置（`rects[i+1]=vertical?{top,left,bottom,right}:rect`）。DragHandle 是 10 DIP（`BarPaginationDragHandleSlotDip = ButtonGapDip*2`）无额外间距的 lane。
- 外框：PPT 横 `240×80`、竖 `80×240`；白板左 `305×80`（含 More）、右 `230×80`。
- 命中合同另有一份几何：`ResolvePptWidgetContracts`（`PageControl/PageControl.cppm:363-422`）→ `ResolveBarPptStandardPaginationGeometry`（`Bar.Metrics.cppm:95-128`）。离屏测试 `PageControl.cpp:4072-4086` 比对 Scene 实际呈现与该合同。
- `ResolveBarButtonListLayout` 的 `spec.height = BarMainBarHeightDip(80)`（`Bar/Bar.Button.cpp:2033`），SingleRow 行高与主栏共用此常量——compact 需要能传入 35（与主栏调研共用切入点）。

**页码按钮内容**（`UI3.Metrics.cppm:87-169` `ResolveButtonVisualMetrics`）
- `PageTwoTwo`（当前生产）：主数字 28 DIP（icon 槽位，y-10），次行 "/总页" 13 DIP（y+20，宽 70）。
- `PageHorizontal`（旧 compact 横向，70×32.5）：主 15 DIP 粗体 + 次 13 DIP 常规，同一行；`UpdateHorizontalPageWidths`（`Bar.Scene.cpp:442-450`）测量两段宽度后 `name.x=-secondaryW/2`、`total.x=primaryW/2`，整体居中。
- `PageVertical`（旧 compact 竖向，32.5×70）：主 15 粗体 y-10、次 13 常规 y+10，槽宽 32.5。
- 文字：`BuildPageContent`（`PageControl.cpp:1124-1174`），白板 `to_wstring` 无上限；PPT `PageNumber(v, index>=2?999:9999)`（`:967-971, 1148-1151`）——竖直 surface 封顶 999、底部 9999，是为旧 32.5 窄槽保留的上限（08-24 任务 prd 第 21 条）。
- 状态文字 `AssignLoadState`（`PageControl.cpp:1090-1122`）：Loading → 右槽 `Wait/Recovering`；可重试 → 左槽原因词（Conflict/ReadFailed/PresentFailed/Failed）+ 右槽 `Retry`；终态 → 右槽原因词。500ms 门控后才替换（`Bar.Scene.cpp:481-486`）。DirectWrite 设为 `NO_WRAP` 且无 trimming（`UI3.Format.cppm:93`），放不下会溢出槽位、不会缩字。

**加载动画**（`UI3/UI3.LoadingIndicator.cppm`、`UI3/UI3.FrameRing.cppm`）
- 不是独立 spinner，而是**沿页码按钮圆角边框内侧的一段弧**：`ButtonFrameRing::Draw(context,left,top,w,h,corner,frame,...)`（`UI3.FrameRing.cppm:28-77`），内缩 `frame(1)+Gap(1)+Width/2(1.25)=3.25 DIP`、线宽 2.5，弧长按周长比例（`MinArc=.10`、`StaticArc=.25`）。路径按按钮实际宽高重建，**任意矩形都可绘制**（只有内宽/高 ≤ 线宽才跳过）。
- Scene 接线：`Bar.Scene.cpp:274-282`（脏区登记，包络 = 页码按钮 ± `AntialiasOutset 1.5`）、`:917-925`（以 `pageButtons[2]` 的 Bounds 绘制）；门控 `PageLoadingIndicator`（`Bar.Scene.cpp:125`、`:779-781`），截止经 `PageControl.cpp:488, 2937-2940` 唤醒。

**uiScale**
- `PageControl.cpp:944-963` `LayoutMonitor()`：`uiScale = ResolveUiScaleBase(主屏快照, UseTaskbarHeightAsScaleBase 模式) × clamp(UI.Bar.Zoom2, 0.5, 2)`，与主栏共用全局 UI 倍率；存入 `PptState::uiScale`（`PageControl.cppm:655`）。
- `ResolveUiScaleBase`（`Inkeys/Inkeys/Display/Display.cpp:1372-1384`）：`Dpi` 模式 = DPI 比；`TaskbarBasic` = `48/baseHeightDip × DPI`；`TaskbarActual` = `任务栏厚度px / baseHeightDip`；**`baseHeightDip` 默认 80**（= 大尺寸主栏高度），所有调用点（`Bar.Initialization.cpp:101`、`Bar.Zoom.cppm:175-177`、`PageControl.cpp:959, 5289`、`IdtMain.cpp:3099-3106`）都用默认值。
- `ResolveSurfaceLayout`（`PageControl.cppm:1257-1290`）：`scale = ResolvePptUiScale(dpi, uiScale)`（`:1014-1018`），PPT 再乘各对自己的 `bottomPairScale/middlePairScale`（0.5–3，被 `FitScale` 按屏幕/资源上限约束，`:1505-1527`）；白板只用全局倍率。

### B2. 旧 compact 实现保留程度与关闭原因

**开关**：`BarPptCompactPaginationEnabled = false`（`Bar.Metrics.cppm:12`，注释"几何和绘制必须消费同一开关"）。

| 部分 | 状态 | 位置 |
|---|---|---|
| 尺寸常量 | 保留：长边 `165`、短边 `42.5`（`10 + 32.5×2 + 70 + 5×4` / `32.5 + 10`） | `Bar.Metrics.cppm:14-19`；导出 `PageControl.cppm:282-288` |
| 命中合同几何 | 保留（硬编码 DIP 数值）：横向 drag 10、箭头 32.5×32.5、页码 70×32.5；竖向页码 32.5×70；Previous/Next `displaysText=!compact` | `PageControl.cppm:381-421` |
| Scene 布局 | 保留（硬编码 rects，含 More 收拢到 Next 下方的 2×2 rect） | `Bar.Scene.cpp:593-603` |
| 视觉种类 | 保留：箭头 `StandardOneOne`、页码 `PageVertical/PageHorizontal`，箭头标签清空 | `Bar.Scene.cpp:605-609, 526-529, 625-631` |
| 横向页码测宽 | 保留：`UpdateHorizontalPageWidths`，仅在 `!whiteboard && surface<2` 调用 | `Bar.Scene.cpp:442-450, 505-511, 621-624` |
| 白板 compact | **从未有**：`standard = wb || !BarPptCompactPaginationEnabled`，白板始终 2×2 | `Bar.Scene.cpp:526, 564` |
| 加载环 / 状态文字 | 未针对 compact 验证：状态词（10-30）、加载环（11-01/11-05）都是关闭 compact（10-20）之后才加的 | 历史任务 `archive/2026-10/10-30-*`、`2026-11/11-01-*`、`11-05-*` |
| 测试 | 反向锁定：断言开关为 false、四个 surface 都是 70×70、页码更新"永不恢复 compact 横排槽位" | `InkeysHeadlessTests/page_control_tests.cpp:371-379`；`PageControl.cpp:4078-4085, 4137-4144` |

**关闭原因**：commit `e2a46600`（2026-10-20，"feat(ui): standardize paging controls and add taskbar scale modes"）——"Use shared 70 DIP buttons for horizontal and vertical PPT paging, retain the disabled compact layout"，同时引入任务栏倍率与 Zoom2；spec `rendering-and-ui.md:441` 记录"`BarPptCompactPaginationEnabled=false` 禁用原 32.5DIP 路径，原紧凑布局、字体与分支继续保留"。仓库内未找到具体产品理由的文字记录（**待确认**：推测为与白板 2×2 统一观感、标签可读性，需用户确认）。

**结论**：旧 compact 的"PPT 横向 2×1 页码 + 1×1 箭头、竖向 1×2 页码"几何与本需求一致，但它是**硬编码数值的平行分支**，没有走标准列表布局，也没有白板分支；且外框短边是 42.5（上下各 5 DIP 边距），不是 35。

### B3. 文字与加载环容量估算

可用宽度：按钮宽减去两侧加载环占位约 4.5 DIP（内缩 3.25 + 半线宽 1.25；加载时文字与弧线会重叠）。

**横向页码 2×1（70×32.5，主 15 粗体 + 次 13 常规同一行）**

| 内容 | 估算宽 | 结论 |
|---|---|---|
| `123` + `/456` | 27 + 29 ≈ 56 | 放得下 |
| `999` + `/999` | 27 + 29 ≈ 56 | 放得下 |
| `9999` + `/9999`（底部上限） | 36.1 + 36.8 ≈ 73 | **超出 70** |
| `12` + `稍候` / `恢复中` | 18 + 26 / 18 + 39 ≈ 44 / 57 | 放得下 |
| `冲突` + `重试`（可重试，左槽 15 粗体） | 30 + 26 ≈ 56 | 放得下 |
| `12` + `Recovering` | 18 + 70 ≈ 88 | **超出** |
| `Conflict` + `Retry` | 56 + 35 ≈ 91 | **超出** |
| `12` + `Ожидание`（ru Wait） | 18 + 68 ≈ 86 | **超出**（字体回退，近似） |

**竖向页码 1×2（32.5×70，两行，槽宽 32.5，环内净宽约 23.5）**

| 内容 | 估算宽 | 结论 |
|---|---|---|
| 主 `999`（15 粗体，上限 999） | 27 | 槽内可放，但与加载环横向重叠约 1.75 DIP/侧 |
| 次 `/999`（13 常规） | 29 | 同上，贴边 |
| `稍候` / `重试` | 26 | 勉强 |
| `恢复中`、`待機中`（ja） | 39 | **超出 32.5** |
| `Wait` / `Retry` / `Recovering` | 29 / 35 / 70 | 贴边 / 超出 / 严重超出 |
| 左槽原因词 `冲突`（15 粗体） | 30 | 贴边 |
| `Conflict` / `Display`（15 粗体） | 56 / 53 | **严重超出** |

**加载环**：几何按按钮实际尺寸生成，2×1/1×2 都能画；周长从 2×2 的约 247 DIP（内缩后中心线）降到 2×1 的约 172 DIP，弧长随比例缩短，观感会更"碎"但不需要改代码。若 compact 1×1 按钮不是 32.5 而更小（见待确认 1），环内净尺寸进一步缩小。

**若 35 DIP 高度要容纳 32.5 的 1×1**：上下边距只剩 1.25 DIP，而当前背景圆角 8（`BarMainBarCornerRadiusDip`）、按钮圆角 4、背景描边 1 DIP——按钮会几乎贴住背景边框，命中矩形与背景拖动区（PPT 背景可拖动，`CanStartPptDrag`）几乎没有余量。

### B4. 小尺寸改造切入点

1. **运行时尺寸档位取代编译期常量**：`BarPptPagination{Long,Short}SideDip`、`PptControl{Long,Short}SideDip`、`PptArrowButtonSideDip`、`BarWhiteboardPagination*` 都是 `constexpr`，被 `ResolveSurfaceLayout`（`PageControl.cppm:1269-1277`）、`ClampPageControlLayout`（`:1338-1376`）、`FitScale`（`:1505-1527`）、离屏/无头测试使用。需改为按 `uiSizeMode` 求值的函数，并让 PageControl 的布局快照（`GroupLayoutBudgetCache`，`PageControl.cpp:369-378` 及 `NativeFrameIdentityCurrent` 比较）把尺寸档位纳入身份，切换设置时触发重新布局。
2. **Scene 布局优先复用标准路径而非旧硬编码分支**：在 `Configure`（`Bar.Scene.cpp:562-631`）里 compact 时把 `pageButtons` 尺寸映射为：箭头/More `oneOne`、页码 `twoOne`，`ResolveBarButtonListLayout(SingleRow)` 传入 35 行高；竖向沿用现有转置即得 1×2 页码。这样白板与 PPT 同一套代码，DragHandle lane、隐藏按钮锚定、背景宽度都自动正确。旧 `else` 分支可作为参考或删除（删除属于额外清理，需确认）。
3. **视觉种类**：compact 时页码 `PageHorizontal`（横）/`PageVertical`（竖），箭头 `StandardOneOne`（无标签），恢复 `RefreshDisplayedText`/`Configure` 中的横向测宽调用，但条件需从 `BarPptCompactPaginationEnabled && !whiteboard && surface<2` 改为"compact 档位 && 横向"（白板底部也是横向）。
4. **命中合同**：`ResolvePptWidgetContracts` 的 compact 分支要与 Scene 结果一致（或改为从同一列表布局推导），并更新 `displaysText`。
5. **文字溢出**：需要下列之一——(a) compact 下状态词改用更短文案/只显示环不换字；(b) 测宽后按槽宽缩小字号（当前 Word 无自适应，需新增）；(c) 竖向状态词只保留图标/环。底部 `9999/9999` 也溢出约 3 DIP，可改 compact 横向上限为 999 或缩字。
6. **白板 compact**：底部左 HWND 的 More 菜单（关闭/最小化/主题，`StandardFourOne` 140 宽，`Bar.Scene.cpp:640-645` 用 `ButtonOneSideDip+5` 排列）和主题面板是否也缩小；PPT↔白板同实例形变需在 compact 档位内两端都用 compact 几何。spec 提到底部共享 HWND 按白板 2×2 形态预留容量（`archive/2026-10/10-12-canvas-l-deadzone-stroke-start/research/l-deadzone-stroke-start.md:64-70`），compact 时容量可缩小（**待确认**具体预留代码位置）。
7. **uiScale 基准**：`ResolveUiScaleBase` 的 `baseHeightDip=80` 意味着"任务栏实际/基础 48"模式下，大尺寸主栏高度等于任务栏高度。compact 若仍用 80 作基准，35 DIP 的控件在任务栏倍率下会只有任务栏高度的 35/80；若改用 35 作基准，compact 控件会被放大到任务栏高度（按钮与文字都放大 2.3 倍，失去"小"的意义）。需要产品决策，且主栏、翻页、启动预览必须一致（`IdtMain.cpp:3099-3106`、`Bar.Zoom.cppm:175`）。
8. **加载环**：无需改几何；仅需确认 compact 下文字与环的重叠是否可接受（可考虑 compact 下环内缩不变、文字槽宽收窄）。

### B5. 风险

- 标准路径与旧 compact 分支、命中合同三处几何来源，任何一处遗漏都会导致命中与绘制错位；离屏测试 `PageControl.cpp:4072-4086` 现在硬性要求 70×70，需改为按档位断言。
- PPT 拖动、吸附、`ClampPageControlLayout` 预算、组碰撞（`PageControlGroupsOverlap`）、`FitScale` 上限、位置持久化都以外框尺寸计算；档位切换时已保存的 `bottomPairWidth/Height`、`middlePairHeight` 需重新夹取。
- Bottom HWND 在 PPT/白板之间共享实例并做连续形变；中途切换尺寸档位的过渡（布局目标重提交会截断正在进行的转换，见 `Bar.Scene.cpp:521-523` 注释）需定义。
- 闲置淡化、边框光源（`PublishBorderCursorSurfaceBounds`）、Drawpad 命中穿透都依赖 surface bounds。
- `page_control_tests.cpp:371-379` 等测试锁定"开关为 false 且旧常量为 165/42.5"，改造时需同步更新；spec `rendering-and-ui.md:438-453, 1867-1869` 与 `ppt-interop/native-session-ui3.md` 的固定尺寸描述需追加 compact。

### B6. 待确认（产品/设计）

1. compact "高度 35 DIP"是指控件外框（含边距）还是按钮本身？若外框 35，1×1 按钮还是 32.5（边距仅 1.25）还是缩为 25（保持 5 DIP 边距）？旧 compact 外框短边为 42.5。
2. 白板翻页控件是否在范围内（含 More 菜单、主题面板）？
3. 状态文字放不下时的策略：缩字号、改短文案、横向允许页码按钮自适应变宽，还是 compact 下只显示加载环不换文字？
4. compact 下底部页码上限是否也降为 999（`9999/9999` 横向溢出约 3 DIP）？
5. 任务栏倍率（`UseTaskbarHeightAsScaleBase`）在 compact 下以 80 还是 35 为基准？
6. 旧 compact 硬编码分支是改造复用、保留还是删除？

---

## Part C　两主题共同耦合点

- 尺寸档位应是单一来源（配置项 + 运行时快照），主栏、指示器、翻页控件、启动预览、`ResolveBarButtonListLayout` 行高（`Bar.Button.cpp:2033`）同时读取；切换需走统一的"布局失效 + 全量脏区"路径。
- 主按钮宽高拆分会同时影响状态指示器布局输入（`RenderLoop.cpp:10915-10918`）与启动预览缓存宽度。
- uiScale 基准（80）是主栏与翻页共享的决策点，必须一起定。
