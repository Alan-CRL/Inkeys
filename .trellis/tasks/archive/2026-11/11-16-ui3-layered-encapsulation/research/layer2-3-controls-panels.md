# 第二、三层调研：基础控件、序列与面板、光照及颜色选择器

## 最重要发现

1. 【事实】按钮已有完整的 Retarget、Configure、Advance、Draw、HitTest 链路，但固定按钮样式仍读取主栏暗色状态，Geometry 标签测量仍使用主栏 renderer。证据：`Bar.Button.cpp:2077`（`RetargetBarFixedButtonVisual`）、`:2420`（`ConfigureBarButtonVisualTargets`）。
2. 【事实】普通控件的重复实现主要集中在主栏绘制属性区域；Geometry、Recall、ConstructionPrompt、翻页及样式栏的普通按钮已大量共用。证据：`Bar.RenderLoop.cpp:10777`（`ThicknessButtonRender`）、`Bar.GeometryAttribute.cpp:924`、`Bar.Scene.cpp:827`（`DrawBarButtonVisual`）。
3. 【事实】两版 picker 的主体几何、marker、footer、长按计时已经一致；默认色系、收起时长、避让、透明度动画和部分按钮绘制路径不同。证据：`Bar.State.cppm:85`（`colorPickerDarkTone`）、`Bar.ShapeEditor.cpp:621`（`darkTone`）、`:1146`（`pickerMotion`）。
4. 【事实】Geometry 长按实际为 **200ms 出环、1000ms 提交**，声明注释仍写 0.5s/2.0s。证据：`Bar.GeometryAttribute.cpp:33`（`HoldRingDelay/ HoldCommitDelay`）、`Bar.Main.cppm:541`（`ConsumeHoldWake`）。
5. 【事实】共享光源由主栏推进；发布订阅、局部坐标映射已有实现，但指针通知仍发往主栏 HWND，发布端直接唤醒 SelectionOverlay。证据：`Bar.RenderLoop.cpp:7405`（`PrepareFrameLighting`）、`Bar.Interaction.cpp:10142`（`NotifyBorderCursorSurfacePointerEntered`）、`Bar.Scene.cpp:939`（`PublishSharedLighting`）。

## 范围与证据约定

本次只读取文件，未修改、创建、删除仓库文件，未运行构建、测试或 GUI。PowerShell 子进程首次被环境拒绝后，改用文件 API 直接读取，未重试子进程。

下文源码路径均相对 `Inkeys/Inkeys/UI/Bar/`；例如 `Bar.Button.cpp:86` 表示该目录中的文件及当前行号。表格中的入口名、字段名或控件名同时作为符号证据。所有外观描述均为静态代码参数；运行后的视觉、时序和输入效果另标【待测】。

【事实】PRD D5 要求以主栏 picker 为外观与手感基准，保留两种提交语义；D7 允许普通控件统一产生已列明的可见变化。证据：`.trellis/tasks/11-16-ui3-layered-encapsulation/prd.md:121`（D5）、`:123`（D7）。

## 1. 公共按钮运行时

### 1.1 定义、签名要点及全局依赖

本表均为【事实】。“无直接读取”仅指该入口自身函数体，不包含调用 renderer、主题函数产生的间接依赖。

| 入口 | 定义与签名要点 | 主栏依赖 |
|---|---|---|
| `BarButtonClass` | 组合 Shape、SVG/PNG、Word、状态、按压倍率、角标与点击函数；`Bar.Button.cppm:86` | 类型包含产品字段；`LabelFontSize` 按 Select/Pierce preset 特判，`:125` |
| `BarButtonVisualParts` | 借用 background 引用及可空 icon、primary、pressScale、hoverStage 指针；`Bar.Main.cppm:300` | 结构本身无全局读取 |
| `RetargetBarButtonInteractionVisual` | button、visible/enabled/selected、duration，另有内容色、图标显隐及填充/边框覆盖；`Bar.Button.cpp:2129` | 转调固定按钮入口，间接读暗色状态 |
| `RetargetBarFixedButtonVisual` | parts 加 visible/enabled/selected/pressed、duration、可选配色；`Bar.Button.cpp:2077` | `barUISet.barStyle.darkStyle`，`:2096`；默认颜色经 `GetThemeColor` |
| `ConfigureBarButtonVisualTargets` | button、布局种类、duration、scale、direct、可选图标高度和字体目标倍率；`Bar.Button.cpp:2420` | Geometry 分支读取 `barUISet.spec` 的测字、字体、locale、设备代次，`:2452` |
| `AdvanceBarButtonVisual` | button、帧上下文、可选副文字，返回 changed/active；`Bar.Button.cpp:2576` | 无直接 `barUISet` 读取；帧时间、速度、动画开关由参数传入 |
| `DrawBarButtonVisual` | 外部 renderer/DC、button、父继承坐标、options；`Bar.Button.cpp:2322` | 无直接 `barUISet` 读取 |
| `DrawBarFixedButtonVisual` | 外部 renderer/DC、parts、继承坐标、字重和对齐；`Bar.Button.cpp:2391` | 无直接 `barUISet` 读取 |
| `BarButtonHitSnapshot` / `HitTestBarButton` | bounds、visible/enabled、圆角和超椭圆指数；检验有限坐标及形状包含；`Bar.Main.cppm:387`、`Bar.Button.cpp:2641` | 无直接全局读取；标准按钮快照仍消费 `IsVisible`、preset、state，`:2633` |
| `ButtonPointerState` | `Update(event, hit, optional screenPoint)` 返回 hover/pressed/activate；`Bar.InteractionWorker.h:139` | 无主栏全局；Move 离开原按钮即清 pressed，Up 后可抑制原地 hover |
| `InteractionMailbox` | 有界模板 FIFO，默认容量512；Push/Pop、溢出、移除与唤醒；`Bar.InteractionWorker.h:79` | 无主栏全局；仅合并允许合并的相邻事件，溢出清队列并置屏障 |
| `BarButtonListClass` | Get、强引用 Snapshot、Replace、Swap，内部 shared_mutex；`Bar.Button.cppm:214` | 无主栏全局 |
| `ResolveBarButtonListLayout` | 输入按钮快照、布局种类及第二组，返回 placements/尺寸/行列/分隔信息；`Bar.Button.cpp:2654` | 无直接全局；读取按钮尺寸、显隐、Divider preset，包含产品排布规则 |
| `DrawBarBackgroundVisual` | 外部 renderer/DC、Shape、父坐标，可选 targetRect/clip；`Bar.Button.cpp:2408` | 直接转发 `renderer.Shape` |
| `BarButtonSetClass` | 注册、配置、preset、主栏投影、More 分流及业务动作；`Bar.Button.cppm:259` | 明确属于产品组织；例如读取 fold/moreExpanded/Recall 状态，`Bar.Button.cpp:1008`（`ApplyMainPresentationLocked` 内投影） |

【事实】默认 `GetThemeColor` 自身读取 `currentBarThemeStyle` 的 darkStyle、背景和内容透明度，变化时请求 SelectionOverlay；因此“不直接读 barUISet”不等于主题服务已独立。证据：`Bar.Theme.cppm:142`（`GetThemeColor`）。

### 1.2 `BarButtonClass` 全字段分类

以下为【事实】字段职责分类；注册信息并非全部直接存放于按钮对象。

| 分类 | 字段 | 证据 |
|---|---|---|
| 通用视觉/状态 | `size`、`lastDrawX/Y`、`button`、`hoverStage`、`pressScale`、`name`、`icon`、`pngIcon`、`iconKind`、`badge`、`localState`、`state` | `Bar.Button.cppm:138`、`:147`、`:152`、`:167`（`BarButtonClass`） |
| 通用动作载荷 | `clickFunc` | `Bar.Button.cppm:163`（`BarButtonClass::clickFunc`） |
| 主栏产品 | `baseSize`、`baseUserVisible`、`preset`、`id`、`userVisible`、`hide`、`only`、`compactMultilineLabel`、`closeMoreAfterAction` | `Bar.Button.cppm:135`、`:165`（`BarButtonClass`） |
| 产品字段的具体含义 | userVisible 与上下文 hide 分离；only 对应不允许多实例；compactMultilineLabel 参与 Select/Pierce 的9号长标签特判 | `Bar.Button.cppm:120`（`IsVisible`）、`:125`（`LabelFontSize`）；`Bar.Button.cpp:140`（`RegisterButton`） |
| 外置注册信息 | registration kind、zone、默认尺寸/可见性、legacyField/legacyEnabled、分类名、设置名、allowMultiple | `Bar.Button.cppm:186`（`BarButtonRegistrationClass`） |

### 1.3 消费方矩阵

均为【事实】。位置为代表性直接使用点；“—”表示所列生产文件中未发现直接调用，并不表示不能经共享入口间接到达。G/E/R/C 分别表示 Geometry/Eraser/Recall/ConstructionPrompt。

| 公共入口 | Main / More | 笔型固定按钮 | G / E / R / C | Scene / ShapeEditor及其他 |
|---|---|---|---|---|
| `BarButtonClass` | `Bar.Button.cppm:264`，Set 成员 | 借用独立图元 | `Bar.Main.cppm:561/195/255/506` | `Bar.Scene.cpp:125`；`Bar.ShapeEditor.cpp:748` |
| `BarButtonVisualParts` | — | `Bar.RenderLoop.cpp:3630`，聚合实参 | — | — |
| `RetargetBarButtonInteractionVisual` | `Bar.RenderLoop.cpp:2755/4693` | — | `Bar.GeometryAttribute.cpp:740`；`Bar.EraserAttribute.cpp:315`；`Bar.RecallAttribute.cpp:249`；`Bar.ConstructionPrompt.cpp:222` | `Bar.Scene.cpp:758`；`Bar.ShapeEditor.cpp:1551` |
| `RetargetBarFixedButtonVisual` | — | `Bar.RenderLoop.cpp:3630` | — | 经完整按钮入口间接使用 |
| `ConfigureBarButtonVisualTargets` | `Bar.RenderLoop.cpp:2750/4689` | 手动布局 | `Bar.GeometryAttribute.cpp:759`；`Bar.ConstructionPrompt.cpp:86` | `Bar.Scene.cpp:342`；`Bar.ShapeEditor.cpp:763` |
| `AdvanceBarButtonVisual` | `Bar.RenderLoop.cpp:5264` | 图元分别推进 | `Bar.GeometryAttribute.cpp:792`；`Bar.EraserAttribute.cpp:328`；`Bar.RecallAttribute.cpp:261`；`Bar.ConstructionPrompt.cpp:236` | `Bar.Scene.cpp:766`；`Bar.ShapeEditor.cpp:1563` |
| `DrawBarButtonVisual` | `Bar.RenderLoop.cpp:10983/11010` | — | `Bar.GeometryAttribute.cpp:924`；`Bar.EraserAttribute.cpp:442`；`Bar.RecallAttribute.cpp:277`；`Bar.ConstructionPrompt.cpp:268` | `Bar.Scene.cpp:827`；`Bar.ShapeEditor.cpp:1839` |
| `DrawBarFixedButtonVisual` | — | `Bar.RenderLoop.cpp:9853` | — | — |
| HitSnapshot / HitTest | `Bar.Button.cpp:2811`，已呈现查询 | `Bar.Button.cpp:2818`，visual查询 | `Bar.GeometryAttribute.cpp:1089`；`Bar.ConstructionPrompt.cpp:289`；E/R用各自 presentation 命中 | `Bar.Scene.cpp:168/190`；`Bar.ShapeEditor.cpp:639/922` |
| `ButtonPointerState` | `Bar.Interaction.cpp:6208/6401` | — | 未直接使用 | `Bar.Scene.cpp:196`；`Bar.ShapeEditor.cpp:657`；`Bar.Main.cppm:124`，状态指示器 |
| `InteractionMailbox` | `Bar.Interaction.cpp:1121`，共享输入邮箱 | 同一输入worker | 同一输入worker | 跨表面输入同样经共享邮箱；定义为 `sharedInputMailbox` |
| `BarButtonListClass` | `Bar.Button.cppm:264`；More用分组快照 | — | `Bar.Main.cppm:562/505`，G/C | `Bar.Scene.cpp:124`；`Bar.ShapeEditor.cpp:586` |
| `ResolveBarButtonListLayout` | `Bar.RenderLoop.cpp:1795/4515` | — | `Bar.GeometryAttribute.cpp:273/408`；`Bar.ConstructionPrompt.cpp:127` | `Bar.Scene.cpp:489`；`Bar.ShapeEditor.cpp:1218` |
| `DrawBarBackgroundVisual` | `Bar.RenderLoop.cpp:10965` | — | `Bar.GeometryAttribute.cpp:881`；`Bar.EraserAttribute.cpp:441`；`Bar.RecallAttribute.cpp:275`；`Bar.ConstructionPrompt.cpp:257` | `Bar.Scene.cpp:789`；`Bar.ShapeEditor.cpp:2097` |
| `BarButtonSetClass` | `Bar.RenderLoop.cpp:527`；`Bar.Interaction.cpp:8539` | 不进入注册表 | 面板 Advance 接收 owner | Scene/ShapeEditor自持列表及按钮 |

## 2. 手工普通控件与专有绘制

### 2.1 标准按钮对照基准

【事实】默认 oneOne 为32.5×32.5、图标20；标准圆角4、边框1、按压倍率0.95、按压底色透明度0.10、hover峰值0.18。hover过渡0.24s，显示完成后以5s淡出；按压默认0.4s，按下 EaseOutCubic，释放 EaseOutBack。按压倍率只作用于绘制，不进入命中快照。证据：`Bar.Metrics.cppm:7/30/37/44/206`（尺寸和视觉常量）、`Bar.Animation.cpp:20`（`BarUiDefaultOperationDur`）、`Bar.Animation.cppm:446/451`（两条曲线）、`Bar.Button.cpp:2043/2109/2633`（hover、按压、快照）。

### 2.2 未走公共按钮绘制的普通控件

表中参数和路径为【事实】；“统一影响”为【推测】，仅描述套用标准默认值可能产生的差异，不构成实现方案。已使用标准按压常量的控件，不能仅因手工绘制就认定手感不同。

| 控件 | 手工路径、参数及命中证据 | 与标准对照及统一影响【推测】 |
|---|---|---|
| More关闭X | 手动矩阵→Shape→SVG；命中30×30、圆角4、图标18；0.95及标准按压曲线；`Bar.RenderLoop.cpp:4608/4622/11022`（`MorePanelCloseHit`）；`Bar.Interaction.cpp:6160` | 缩放/曲线已一致。默认oneOne会扩大命中2.5 DIP、图标扩大2 DIP；手工路径没有固定按钮入口自动补齐的选中光照设置 |
| 笔型扩展入口 | 手动缩放背景，并分别组织分隔线/箭头；宽115−85=30、高30，0.95/默认时长，按压底0.10；`Bar.Main.cpp:81/99`（尺寸）；`Bar.RenderLoop.cpp:3657/9857/9959`（`extensionHit/extensionArrow`）；`Bar.Interaction.cpp:8135` | 默认尺寸变32.5；分隔线0.30、箭头18及随笔型切换显隐属于当前可见差异，不能从标准按钮默认布局推导 |
| 笔型菜单“自由线”行 | Shape＋勾SVG＋Word整体缩放；行高30、勾12；0.95、底0.10；`Bar.Main.cpp:103`（菜单常量）；`Bar.RenderLoop.cpp:4012/11397`（`freeLineRow`）；`Bar.Interaction.cpp:6093` | 标准按压已一致；默认布局会改变行尺寸、左侧勾选区和文字位置 |
| 三档粗细及精调入口 | 自己组织Shape、数字/圆点交接、精调SVG和矩阵；30高、圆角4、数字10，0.95；`Bar.Main.cpp:74`；`Bar.RenderLoop.cpp:3780/3848/10777`（`ThicknessButtonRender`）；`Bar.Interaction.cpp:5699`起各Shape命中 | 默认oneOne改变尺寸、内容字号/槽位；圆点与数字两阶段交换没有标准按钮默认对应物 |
| 11个预设色块 | Shape＋选中勾SVG；30×30、圆角4、勾15；直接Down改色，可拖选跨色块；`Bar.Initialization.cpp:486/490`（`ColorSelect4`代表）；`Bar.RenderLoop.cpp:9675`；`Bar.Interaction.cpp:7915`（颜色选择） | 当前没有普通按钮0.95矩阵路径；加入默认按压/hover会改变反馈。Down与跨色块拖选语义有别于普通Up激活 |
| 自定义色盘入口 | PNG色轮、Shape外框、色芯、勾；手工0.95矩阵；`Bar.RenderLoop.cpp:3415/9765`（`ColorSelect12`）；`Bar.Interaction.cpp:5875` | 缩放参数已一致；默认按钮尺寸、底色与hover会改变圆盘呈现，色轮/色芯仍是专有载荷 |
| picker tone/close | 30×30、圆角4；tone图标20，close为10×10线段X、线宽至少1或1.6×zoom；手工矩阵；`Bar.RenderLoop.cpp:6440/6482/11569/11718`；`Bar.Interaction.cpp:5791/5825` | 0.95及曲线相同，但绘制倍率被夹到≤1，释放Back外扩被截断；标准路径不作该夹紧。默认oneOne会扩大命中 |
| 两个属性tooltip关闭X | Shape＋SVG，20×20命中、14图标、圆角4；0.95及标准曲线；`Bar.Main.cpp:160`（tooltip常量）；`Bar.RenderLoop.cpp:4033/6292/11326`（`ConfigureTooltipCloseButton/DrawThicknessPopup`）；`Bar.Interaction.cpp:5973` | 默认32.5命中和20图标明显变大；当前仅pinned时关闭按钮显现，不能当作一直存在的普通X |
| tooltip的问号/溢出提示入口及正文 | 手工Shape/SVG/Word、hover/pinned和遮挡；badge高24、图标14、命中padding2、正文12/10、内边距8；`Bar.Main.cpp:159`（tooltip常量）；`Bar.RenderLoop.cpp:6182/11371`；`Bar.Interaction.cpp:6018/6061` | 不是完整标准按钮反馈；统一默认参数会改变badge尺寸和hover表现。正文面板本身无按压倍率 |
| 底栏吸附/居中提示 | 临时创建Shape，再DrawTextW；高30、圆角6；开合Back、默认0.4s，无按钮press/hover；`Bar.BottomDock.h:27`；`Bar.RenderLoop.cpp:6895/11934`（`BottomDockIndicator`）；`Bar.Interaction.cpp:10134` | 标准按钮圆角4、按压和hover并非当前表现；提示是阻挡点击的反馈表面，不是可激活动作按钮 |
| Geometry滚动条 | 手工rail/thumb与矩形命中；rail宽4、圆角3；`Bar.GeometryAttribute.cpp:851/930/1066`（`scrollRail_/scrollThumb_`） | 当前是滚动控件，未发现普通按钮式缩放；默认按钮反馈会改变轨道表现，拖动与滚轮语义独立 |
| `BackgroundWarning` | 普通Word，初始位置(700,150)、1200×300、字号30、透明度0；绘制处仍有Aero警告TODO；`Bar.Initialization.cpp:340`、`Bar.RenderLoop.cpp:9641` | 无按钮、hover或命中路径证据；只能归为文字警告预留，不能称已完成的警告控件 |
| Geometry辅助说明 | 有独立Shape/Word绘制，但当前Advance强制 `auxiliaryHintVisible_=false`；`Bar.GeometryAttribute.cpp:836/955` | 当前路径不是活动tooltip；不能据其保留字段宣称存在可见交互 |

【事实】笔型主按钮并非全部手工重复：Brush/Laser/Highlight/SoftPen已调用固定按钮的 Retarget 和 Draw。证据：`Bar.RenderLoop.cpp:3630`（`RetargetBarFixedButtonVisual`）、`:9853`（`DrawBarFixedButtonVisual`）。

### 2.3 确实具有专有内容的路径

| 内容 | 当前扩展点及证据【事实】 |
|---|---|
| Geometry线宽斜向胶囊 | `options.content`，−45°、单次圆头填充、按下附加0.70透明度；`Bar.GeometryAttribute.cpp:889`（`drawButton`） |
| ShapeEditor线宽、色块、色轮和X | `options.content`；线宽−45°、长度24，色芯/勾独立绘制；`Bar.ShapeEditor.cpp:1777`（`DrawButton`） |
| 翻页主题色、网格/点阵预览 | `underlay`画预览底色，`content`调用 `WhiteboardVisual::DrawThemePattern`；`Bar.Scene.cpp:802`（`DrawButton`） |
| 橡皮示例 | `DrawPreview`自持位图缓存、圆形/矩形示例与选中外环，没有经过按钮content回调；`Bar.EraserAttribute.cpp:389/446` |
| 橡皮自动粗细分隔线和箭头 | 完整按钮之外手工复用该按钮pressScale；`Bar.EraserAttribute.cpp:448`（`Draw`） |
| 背景内容 | `BarSurfaceBackgroundSpec::drawContent`，仅接DC和矩形；`Bar.Scene.cppm:26`；调用于 `Bar.Scene.cpp:1101` |
| 状态指示器及主Logo | 指示器自有叠层、超椭圆进度环、停靠/展开与命中；Logo直接SVG；`Bar.StatusIndicator.cpp:359/406`、`Bar.RenderLoop.cpp:11068` |
| 粗细连续预览、滑块/精调拨盘 | 主栏直接绘制曲线、渐变、刻度和thumb；thumb已共用 `DrawBarSliderThumb`；`Bar.RenderLoop.cpp:10225/10725/11893` |
| 多步形状提示进度 | 面板直接画圆环和步骤文字，操作按钮已共用；`Bar.ConstructionPrompt.cpp:259/268`（`Draw`） |

【事实】按钮回调只有绘制参数和bool返回值，没有包络、命中或动画活跃声明；有回调时公共按钮跳过标准包络提前拒绘。背景drawContent也没有相应字段。证据：`Bar.Main.cppm:322`（`BarButtonDrawOptions`）、`Bar.Button.cpp:2333`（`DrawBarButtonVisual`）、`Bar.Scene.cppm:26`（`BarSurfaceBackgroundSpec`）。

【事实】clip目前由调用场景组织：Geometry有面板、类别和参数clip，ConstructionPrompt有面板clip；并非回调接口自动提供独立clip。证据：`Bar.GeometryAttribute.cpp:882/926/950`（`Draw`）、`Bar.ConstructionPrompt.cpp:258`（`Draw`）。

## 3. 进度环与等待反馈

| 路径 | 绘制、持有者与计时【事实】 | 淡入淡出、失败处理【事实】 |
|---|---|---|
| 普通圆环 | `DrawRing`同色转发 `DrawProgressRing`；后者按progress从顶部顺时针画弧，参数携带轨道、颜色和透明度；`Bar.Rendering.cppm:241`、`Bar.Rendering.cpp:1448` | 自身不持有时钟；几何/画刷失败直接跳过，`:1478` |
| `ButtonFrameRing` | 缓存圆角矩形路径与画刷，无轨道；线宽2.5、边框内间隙1、顶边中点起；`Bar.ButtonFrameRing.h:12/23`、`Bar.PageLoadingIndicator.h:17` | 返回false由owner停止尝试；尺寸过小跳过，stroke style失败返回true；`Bar.ButtonFrameRing.h:29/55/65` |
| 页码等待 | Scene持 `indicator/ring/ringFailed`；500ms门控，周期1.4s，非线性头尾伸缩；`Bar.Scene.cpp:146`、`Bar.PageLoadingIndicator.h:14/20/43` | 入180ms、出140ms；失败状态保留dim、停止loading环；新episode/session清ring失败；`Bar.PageLoadingIndicator.h:88`、`Bar.Scene.cpp:682/816` |
| 图形长按锁定 | panel持holdStart/item/accepted，worker消费截止；200ms出环，1000ms提交，移动阈值5 DIP；`Bar.Main.cppm:579`、`Bar.GeometryAttribute.cpp:33/323` | 进度沿按钮框；入180ms/出140ms；失败本次不重试，下次长按或设备代次变化重置；`:657/874/911` |
| picker hold | 主栏手势持稳定点/GetTickCount64，State持反馈；样式栏inputMutex下持稳定点/steady_clock；`Bar.Interaction.cpp:5130`、`Bar.ShapeEditor.cpp:664` | 两版均500ms开始、再1500ms锁定，淡入出180ms；圆环由通用原语跳过资源失败，没有独立ringFailed；`Bar.Interaction.cpp:5153`、`Bar.ShapeEditor.cpp:1613/1901` |
| 多步形状 | `BarConstructionPrompt`持ringProgress和construction status；`Bar.Main.cppm:510/521` | 常态0.4s、完成退场进度0.24s，EaseOutCubic；半径10.75、线宽2.5、圆头；`Bar.ConstructionPrompt.cpp:202/265` |
| 状态指示环 | `StatusIndicatorModel`持outgoing/current及进度；独立超椭圆路径，非ButtonFrameRing；`Bar.StatusIndicator.h:222/756`、`Bar.StatusIndicator.cpp:359` | RingOut .20、RingIn .30、RingGap .12；绘制资源失败跳过；`Bar.StatusIndicator.h:52`、`Bar.StatusIndicator.cpp:368/391` |

## 4. 面板结构、布局和呈现

### 4.1 四个属性/提示面板

以下均为【事实】。四者Draw均接收外部 `BarUIRendering&` 和 `ID2D1DeviceContext*`。

| 面板 | 主要成员 | 布局及成功呈现分离 |
|---|---|---|
| Geometry | 按钮数组、类别/属性列表、layoutTimeline、motion、滚动条、hold/ring、frame/presented；`Bar.Main.cppm:526`（`BarGeometryAttributePanel`） | 类别GeometryGrid最多六列：`Bar.Button.cpp:2673`；参数宽度和固定行、类别视口：`Bar.GeometryAttribute.cpp:408/439`；Draw `:871`，Commit `:969` |
| Eraser | 11按钮、panel/menu motion、三个示例及缓存、分隔线、frame/presented；`Bar.Main.cppm:154`（`BarEraserAttributePanel`） | `ResolveEraserAttributeLayout`按实际示例直径排布；极窄只裁剪，不进入紧凑布局：`Bar.EraserAttributeLayout.cppm:107/126`；Draw `Bar.EraserAttribute.cpp:432`，Commit `:481` |
| Recall | 单Redo按钮、surface、panelMotion、拖动/换边布局、frame/presented；`Bar.Main.cppm:218`（`BarRecallAttributePanel`） | 自然宽80、按钮70、两侧各5；窄区裁剪：`Bar.RecallAttributeLayout.cppm:66`；Draw `Bar.RecallAttribute.cpp:266`，Commit `:287` |
| ConstructionPrompt | 四按钮、divider、文字/步骤环、尺寸动画、身份、frame/presented；`Bar.Main.cppm:474`（`BarConstructionPrompt`） | 文字＋按钮超可用宽度时折行；终结按钮成对换行：`Bar.ConstructionPrompt.cpp:119/128`；Draw `:249`，Commit `:278` |

【事实】主栏成功呈现路径统一发布按钮命中，再提交上述面板及状态指示器的presented状态。证据：`Bar.RenderLoop.cpp:12526`（`PublishPresentedButtonHits`）、`:12527/12536/12538/12539`（各`CommitPresented`）。

### 4.2 Main、More与叠层命中

| 项目 | 当前事实及证据 |
|---|---|
| Main外框/开合 | fold时宽回80、透明度归零；展开填充0.8、边框0.18，换边含中间关键帧；`Bar.RenderLoop.cpp:2963`（MainBar目标更新） |
| More开合 | geometry使用OutBack/InBack，opacity另用OutSine/InSine，默认操作时长；`Bar.RenderLoop.cpp:4505`（`morePanelProgress/morePanelOpacity`） |
| More排布与局部坐标 | GroupedGrid分explicit/forced；面板屏幕边夹紧，内容围绕完整面板中心缩放，按钮保留MainBar局部坐标；`Bar.RenderLoop.cpp:4515/4556/4590/4599` |
| 绘制和clip | 属性先画，More先画，Main后覆盖；Main和More外框传clip=true；Main退场按钮先于可见按钮；`Bar.RenderLoop.cpp:10953/10965/10970/10994`（`DrawMainBar/DrawMoreButton`） |
| 常驻透明度 | Main图标文字使用resident content opacity及PPT idle fade，弹出内容另行组织；`Bar.RenderLoop.cpp:10962/10972` |
| 留白阻挡 | picker留白消费点击；菜单正文及留白阻挡；More视觉在Main下层但输入优先，面板留白消费；`Bar.Interaction.cpp:5868/6132/6147/6250`（对应PointerStage） |

### 4.3 底栏吸附/居中提示

【事实】文字、测量宽度、期望文案及交换阶段属于 `BarRenderLoopState`；显示目标来自dock、drag、fold和手势资格，白板策略还可抑制。证据：`Bar.RenderLoop.cpp:652`（`bottomDockIndicatorWord`）、`:6878`（`indicatorTarget`）。

【事实】文案为“底栏模式”及其“·居中”变体；退场保留最后文案。DWrite测量后，框宽＝文字宽＋两侧留白，单侧留白＝max(0,(30−文字高)/2)。阶段为 Stable、ExpandingFrame、SwappingText、ShrinkingFrame。证据：`Bar.RenderLoop.cpp:6910/7640`（文案与`MeasureDockText`）、`Bar.BottomDock.h:141/293`（阶段及`ResolveBarBottomDockIndicatorGeometry`）。

【事实】提示在全部Bar/面板之后绘制、调试覆盖层之前；`constructionPrompt.Visible()`为真时不绘制原dock提示。证据：`Bar.RenderLoop.cpp:7803`（`dockTargetIndicatorVisible`）、`:11918`（指示器绘制）。

### 4.4 翻页场景固定面板

【事实】`BarPageSurface::Impl`持分页列表及5个pageButtons，More为3个actions，Theme为9个previews和预览背景；menu/theme各有motion、frame、pose。证据：`Bar.Scene.cpp:121`（`Impl`）。

【事实】绘制顺序Theme→More→分页根；组变换同时调整renderer zoom、字体目标和光照局部位置。输入按根、More、Theme的前后优先级选FrontPanel，只检验该面板按钮，因此前层留白不穿透。证据：`Bar.Scene.cpp:178`（`FrontPanel/HitButton`）、`:790`（`SetPose`）、`:1093`（`Render`）。

## 5. 光照链路

| 环节 | 事实及证据 |
|---|---|
| 产生/推进 | 主栏 `PrepareLightingAndDemand`调用一次 `state.spec.PrepareFrameLighting`；renderer持主光、光标光及笔色过渡状态。工具模式决定Draw/Eraser/Geometry/Select/Pierce锚点；`Bar.RenderLoop.cpp:7385/7405`、`Bar.Rendering.cpp:678/788` |
| 发布内容 | 屏幕坐标主光/光标光、两个半径、光标强度、笔色、笔色混合、绘制光透明度、可见性及边缘光开关；`Bar.Scene.cppm:108`（`BarSurfaceSharedLighting`）；主栏填充并发布于 `Bar.RenderLoop.cpp:7421` |
| 翻页订阅 | `SetSharedLightingSubscribed`登记；发布时比较快照，按影响筛选hooks，锁外invalidate/wake；`Bar.Scene.cpp:917/928` |
| 翻页映射/失效 | 屏幕坐标减presentation bounds；组pose再减平移；主光变化FullDamage，光标变化合并新旧影响范围；`Bar.Scene.cpp:103/618/790` |
| 样式栏消费 | 读取同一共享快照，变化置presentPending，再按自身bounds映射；不是另起光源状态机；`Bar.ShapeEditor.cpp:1757`（`ReadShapeEditorSharedLighting`） |
| 具体宿主耦合 | 其他表面进入/离开仍PostMessage到 `floating_window`；发布端查询ProductShapeUiSnapshot并请求SelectionOverlay；`Bar.Interaction.cpp:10142/10149`、`Bar.Scene.cpp:939` |

## 6. 两套连续颜色选择器对照

表内均为【事实】，尺寸为未叠加宿主/面板倍率的DIP。

| 项目 | 主栏版 | 样式栏版 | 结论 |
|---|---|---|---|
| 状态归属 | `colorPickerOpen/DarkTone/TonePress/ClosePress/MarkerVisible/X/Y/PointerPressed/Capture/HoldHintActive/HoldLocked/Progress/PointerY`；`Bar.State.cppm:84` | open/darkTone、dragging/locked、稳定点/时间、hold动画和本地色；`Bar.ShapeEditor.cpp:621/664` | 两份状态 |
| 面板/圆角 | 300×209、8；`Bar.Main.cpp:174`、`Bar.RenderLoop.cpp:6405` | 300×209、8；`Bar.ShapeEditor.cpp:724/1529` | 一致 |
| 色板 | (5,40)、290×132、圆角4；`Bar.RenderLoop.cpp:6425` | [5,40,295,172]、圆角4；`Bar.ShapeEditor.cpp:1847` | 一致 |
| 色相/明暗 | 横向色相；near/far由primaryBar决定，亮暗混合0.4s；`Bar.RenderLoop.cpp:1875/11610` | 同一方向公式，由pickerBelow决定，toneMix .4；`Bar.ShapeEditor.cpp:1622/1848` | 数学一致，方向来源不同 |
| tone按钮 | (5,5)、30×30、圆角4，20太阳/月亮；`Bar.RenderLoop.cpp:6440/6482` | 中心(20,20)、30×30、圆角4，标准oneOne图标20；`Bar.ShapeEditor.cpp:763/1533/1567` | 静态几何一致 |
| tone图标切换 | 两SVG按当前色系直接显隐；`Bar.RenderLoop.cpp:6487` | `TransitionToResource`换图；`Bar.ShapeEditor.cpp:1554` | 过渡路径不同 |
| close按钮 | (265,5)、30×30；X宽10、线宽max(1,1.6×zoom)；`Bar.RenderLoop.cpp:6459/11748` | 中心(280,20)、30×30；content画半宽5的X、同线宽；`Bar.ShapeEditor.cpp:1533/1567/1786` | 静态一致；主栏手工，样式栏公共按钮 |
| press/hover | 标准0.95和曲线，但tone/close绘制倍率夹至≤1；`Bar.RenderLoop.cpp:6453/11569/11721` | 公共Retarget/Advance/Draw；`Bar.ShapeEditor.cpp:1551/1839` | 释放外扩处理不同 |
| marker | 半径5.5，黑3、透明度.72，白1.5；`Bar.RenderLoop.cpp:11643` | 相同；`Bar.ShapeEditor.cpp:1856` | 一致 |
| marker显隐 | exact且非预设色，拖动显现；`Bar.Interaction.cpp:5073/5090` | 同样规则；`Bar.ShapeEditor.cpp:1607` | 一致 |
| RGB/footer | 字号13；色板下沿172至209，高37；整数R/G/B及整数百分比，标签左对齐、值右对齐；间距上限3/6；`Bar.RenderLoop.cpp:6507/6545/6559/6573` | 同为13、高37、y172，间距上限3/6；`Bar.ShapeEditor.cpp:1869` | 两版都有footer，静态规则一致 |
| 显示数值动画 | 拖动直接值，其他变化SetTar；`Bar.RenderLoop.cpp:1903` | 拖动直接值，其他默认.4；`Bar.ShapeEditor.cpp:1624` | 同类策略 |
| 顶部预览 | (40,5)、220×30、圆角4，无文字；`Bar.RenderLoop.cpp:6643` | 同几何；`Bar.ShapeEditor.cpp:726/1864` | 一致 |
| hold启动/锁定 | 静止500ms开始，随后1500ms，合计2s；阈值5×max(1,dpiZoom)屏幕像素；`Bar.Interaction.cpp:155/5133/5153` | .5＋1.5s；阈值5×max(1,frame.scale)；`Bar.ShapeEditor.cpp:1613/2134` | 时长一致；阈值倍率来源不同 |
| hold提示 | 高28、宽min(280,测字宽＋29)，y45或139，底色.82、圆角4、文字12；`Bar.RenderLoop.cpp:6599/6621` | 同静态值；`Bar.ShapeEditor.cpp:1892` | 一致；两版都在出现沿锁存侧边 |
| hold环 | 半径7、宽2，轨道/进度色向Surface混合.70/.35，轨道透明度另乘.45；`Bar.RenderLoop.cpp:11789/11800` | 同值；`Bar.ShapeEditor.cpp:1901` | 一致 |
| hold淡入出/锁后文字 | .18s；锁后环淡出，原HoldColorLabel由灰变TextPrimary；`Bar.RenderLoop.cpp:1884/6632`、`Bar.Initialization.cpp:733` | .18s，仍用HoldColorLabel并变色；`Bar.ShapeEditor.cpp:1619/1892` | 没有独立“已锁定”换文案证据 |
| 默认暗色系 | true；`Bar.State.cppm:85` | false；`Bar.ShapeEditor.cpp:621` | 不同 |
| 开/合时长 | .40/.40，OutBack/InBack；透明度由geometry progress夹紧；`Bar.Main.cpp:186`、`Bar.RenderLoop.cpp:1864/6403` | .40/.24；geometry Back，opacity独立Sine；`Bar.ShapeEditor.cpp:1146`、`Bar.EraserAttributeMotion.cppm:66` | 不同 |
| 紧凑态 | 比例60/370，宽约48.65；`Bar.Main.cpp:69/187`、`Bar.Layout.cppm:35` | compactWidth60，即300宽时比例.2；`Bar.ShapeEditor.cpp:165`、`Bar.EraserAttributeMotion.cppm:24` | 不同 |
| 展开/避让 | 跟随primaryBar上下，色块水平中心对齐；不夹屏幕边；`Bar.RenderLoop.cpp:6377` | 依次远侧、右、左、对象侧；间距8，空闲区域缩放适配，避开toolbar/palette；`Bar.ShapeEditor.cpp:95/124/147` | 明显不同 |
| 捕获/取消 | 主栏ticket捕获、8ms等待更新hold；取消清反馈和捕获；`Bar.Interaction.cpp:3616/5173/5179` | 宿主传captureOwned及presented版本；身份变化、CancelMode、CaptureChanged、失捕获走CancelPointer；`Bar.ShapeEditor.cpp:2367/2425/2609` | 宿主协议不同 |

【事实】两版都共用 `ConvertBarColorPickerRgbToHsv`、`ConvertBarColorPickerHsvToRgb`、`GetBarColorPickerColor`、`ProjectBarColorPickerColor`，以及Hue/Tone渐变画刷。色相停靠为红、黄、绿、青、蓝、品红、红，间隔1/6；黑/白端远离入口。证据：`Bar.Layout.cppm:454/476/506/518`、`Bar.Rendering.cpp:1356/1401`。两版实际分别绘制色板，未直接复用完整 `ColorPalette` 方法：`Bar.RenderLoop.cpp:11610`、`Bar.ShapeEditor.cpp:1848`。

### 颜色提交时机

- 【事实】主栏Down即应用选点，Move实时 `SetInteractionPenColor(..., false)`；完成且仍拥有捕获的Up以最终色 `SetInteractionPenColor(..., true)`记忆。证据：`Bar.Interaction.cpp:5059`（`ApplyColorPickerPoint`）、`:5123/5172/5197/5222`（`RunColorPickerPointerGesture`）。
- 【事实】样式栏拖动只更新 `pickerDragColor/X/Y`，绘制消费本地反馈；Up以Down冻结的身份和基础样式提交一次SetStyle，取消不提交本次颜色。证据：`Bar.ShapeEditor.cpp:2129`（`UpdatePickerPoint`）、`:1612/2423/2476/2625`。
- 【推测】按D5统一后，样式栏确定需要核对的可见差异集中于默认色系、关闭速度、紧凑态、透明度过渡、避让及tone换图；两版已相同的静态几何不应被误列为现有差异。依据为上表及PRD D5。

## 7. 日志概况

以下为【事实】，统计显式 `Inkeys::Log`调用，不把`Enabled`级别判断计作日志输出。

| 文件 | Module / 级别 / 调用数 | 位置与符号 |
|---|---|---|
| `Bar.RenderLoop.cpp` | Bar / Error / 2 | `:9361`，epoch资源重建TryFormat；`:12625`，呈现失败Error |
| `Bar.Interaction.cpp` | UI3 / Error / 3 | `:1609`，表面回调异常；`:2104`，无窗口worker退出；`:8639`，协程续接异常 |
| 同上 | Bar / Error / 1 | `:8927`，第三光源休眠定时器失败 |
| 同上 | Input / Info / 1 | `:9702`，拖动诊断TryFormat；`:9692`为Enabled判断 |
| 其余指定文件 | 未检出显式调用 | 控件主体定位：`Bar.Button.cppm:86`、`Bar.Button.cpp:2077`、`Bar.Main.cppm:300`、`Bar.ButtonFrameRing.h:12`、`Bar.PageLoadingIndicator.h:43`、`Bar.InteractionWorker.h:79`、`Bar.GeometryAttribute.cpp:363`、`Bar.EraserAttribute.cpp:432`、`Bar.RecallAttribute.cpp:266`、`Bar.ConstructionPrompt.cpp:91`、`Bar.Scene.cppm:120`、`Bar.Scene.cpp:121`、`Bar.ShapeEditor.cpp:583`、`Bar.Layout.cppm:506`、`Bar.State.cppm:84`、`Bar.BottomDock.h:293`、`Bar.StatusIndicator.h:277`、`Bar.StatusIndicator.cpp:406` |

【事实】补充读取的公共渐变实现已有两处Bar/Error日志，分别记录色相渐变和明暗覆盖创建失败，并设置不可用/已记录标记。证据：`Bar.Rendering.cpp:1382/1429`（`GetColorPickerHueGradientBrush/GetColorPickerToneGradientBrush`）。

## 8. 迁移/封装前必须先解决的事项

以下清单列出现有冲突和边界事实，不指定实现方式。

| 必须明确的事项 | 当前事实及证据 |
|---|---|
| 通用入口的主栏依赖 | 固定按钮读取 `barUISet.barStyle.darkStyle`；Geometry测量读取 `barUISet.spec`；默认主题读取currentBarThemeStyle并唤醒特定场景。`Bar.Button.cpp:2096/2463`、`Bar.Theme.cppm:142` |
| 产品字段与通用按钮责任 | preset、配置显隐、注册、多实例、More收起策略混于同一模型及Set；`Bar.Button.cppm:135/186/259` |
| 普通控件有意变更边界 | 20/30/32.5等命中尺寸、不同图标大小、色块Down拖选及picker释放夹紧真实存在；`Bar.Main.cpp:160`、`Bar.RenderLoop.cpp:4608/11721`、`Bar.Interaction.cpp:7944` |
| 专有扩展的范围合同 | 回调未声明包络/命中/active，公共绘制因此跳过提前拒绘；`Bar.Main.cppm:322`、`Bar.Button.cpp:2333` |
| picker基准与提交分离 | D5已确定主栏基准，但两种写入时机真实不同；PRD`:121`（D5）、`Bar.Interaction.cpp:5222`、`Bar.ShapeEditor.cpp:2430` |
| 光源与宿主耦合 | 通知绑定floating_window，发布绑定SelectionOverlay；`Bar.Interaction.cpp:10142`、`Bar.Scene.cpp:939` |
| 呈现后命中与留白阻挡 | 面板具有candidate/presented和叠层屏障；`Bar.RenderLoop.cpp:12526`、`Bar.Scene.cpp:193`、`Bar.Interaction.cpp:6250` |
| 注释与实现冲突 | Geometry声明注释0.5s/2.0s，实际200ms/1000ms；`Bar.Main.cppm:541`、`Bar.GeometryAttribute.cpp:33` |
| 日志基线更新 | 当前已有UI3/Error调用，不能继续沿用旧调查“UI3无实际使用者”的判断；`Bar.Interaction.cpp:1609/2104/8639` |

## 9. 未能确认的事项

- 【待测】不同DPI、动画倍率及关闭中反向操作时，两版picker的像素级差异、释放回弹可见程度；静态分支证据：`Bar.RenderLoop.cpp:11569/11721`、`Bar.ShapeEditor.cpp:1551`。
- 【待测】资源创建失败、设备重建、捕获丢失及迟到输入的实际恢复效果；处理入口：`Bar.Scene.cpp:685/824`、`Bar.GeometryAttribute.cpp:659/874`、`Bar.ShapeEditor.cpp:2367`。
- 【待测】极窄工作区内Geometry滚动、Eraser/Recall裁剪及picker缩放后的可用性；规则入口：`Bar.GeometryAttribute.cpp:439/1066`、`Bar.EraserAttributeLayout.cppm:126`、`Bar.RecallAttributeLayout.cppm:76`、`Bar.ShapeEditor.cpp:147`。
- 【推测】`BackgroundWarning`仍属于未完成流程；本次只确认初始透明度0及绘制处TODO，未确认运行时存在有效触发。证据：`Bar.Initialization.cpp:340`、`Bar.RenderLoop.cpp:9641`。
- 【待测】现有日志是否在实际运行中落盘、热路径异常时是否影响响应；调用点见`Bar.RenderLoop.cpp:9361/12625`、`Bar.Interaction.cpp:8639/9702`。本次按约束未执行任何运行验证。