# C2 主栏本体实例设计草案

## 1. 决策与范围

推荐 **B**。主栏本体的底板是圆角 `UiShapeClass`，由 `Panel::DrawFrame` 调用 `renderer.Shape`；超椭圆是主按钮。去掉主按钮的做法是给底板自己的根坐标，再复用同一组绘制函数。建立第二个渲染器并重放第一份场景无法隔离实例。证据见 `Bar.Initialization.cpp:390–405`、`UI3.Panel.cpp:67–73`、`Bar.RenderLoop.cpp:8568–8574,9757–9830,9834–9866,10531–10551`。下文源文件行号均以含 C1 的工作树为准，Bar、UI3、Setting 分别位于 `Inkeys/Inkeys/UI/` 的同名目录。

| 比较项 | A · 真实主栏拥有实例 | B · 独立实例与实栏共用绘制片段 |
| --- | --- | --- |
| 真实主栏改动面（估算，非 diff 行数） | `DrawMainBarContent` 约 2,100 行，与主按钮继承、属性、底栏变换、写回交错。即使只移交本体，也要拆 `SubmitTargetsAndLayout` 中的布局批次、More，以及 `AdvanceAnimationsAndDeriveLayout`、字号、光照、登记与交互引用（R:1859、2915、3667、5478、5718、8019、8273、8410、9967、10514）。跨阶段数百至千行级，实栏原对象地址和成功帧身份须保留 | 实栏绘制搬运 R:9757–9830、10479–10507，约 100 行。More 网格到面板物理位置的内联算法在 R:5505–5713，约 200 行需要拆参数或移动；开合门禁、主按钮限位、布局/绘制所属帧次和成功提交仍在原处。其余使用已有按钮、材质、装箱、命中基础 |
| 新增代码 | 实例、真实宿主适配器、预览适配器，还要解决同一按钮被交互/登记/重放引用时的所有权；预览仍需编辑投影、独立帧、几何与测试 | 预览投影与编辑身份、独立帧输入/状态、几何查询，窄的共用绘制与 More 姿态函数。另补主题快照和光照动画参数接缝（§5）。实例实现数百行级；没有第二份按钮或阴影画法 |
| golden | 高。真实动画目标、推进顺序、继承坐标和材质均可能改变 | 较低。逐字节风险集中在常驻透明度、More 层序、提示文字和局部变换 |
| 第二宿主 | 高。重放借当前图元、当前字号帧及不同 mapping，所有权改变会破坏重放 | 保留 R:10511–10552。公共函数必须接收调用方 renderer/context，不能捕获真实 renderer |
| 属性脏区 | 高。身份、几何预备与绘制副作用均会影响已登记样本 | 较低但仍需验证。保留 `state.current`、原指针和 R:9879–9976、10387 的累计/覆盖顺序及原 clip、可见性和抗锯齿边界 |
| 需求与下一轮拖动 | 可全部满足；真实宿主直接取得同一实例的编辑接口，代价是现在迁移所有者 | 可全部满足预览；几何生成、条目位置、C1 缝隙合同及 More 姿态可接真实宿主。下一轮仍需给实栏接自己的输入、模型暂态与成功帧发布 |

R 表示 `Bar.RenderLoop.cpp`。父设计 §4 和子 PRD 字面指定 A。B 将“真实主栏委托实例”改为“真实主栏委托共用绘制函数”，这项**架构变更须主 Agent 审定**。两种路线均以同一组生产门禁验收。若保留原所有权条款，则选 A，不能把 B 记作已经实现了 A。

## 2. 绘制依赖与状态归属

`DrawMainBarContent` 的状态并非只读。按钮绘制会写继承位置、PNG 姿态、缓存和字号修正；属性绘制甚至会发布细调宽度。显式帧参数字段定义在 R:1984–2009，辅助 bounds sampler 还读 R:1042–1090 的属性数字几何。下表按代码路径归属；其后附直接成员核对索引。“传入”指宿主提供本帧值或有效期覆盖本帧的资源，“自带”指实例独占。

| 来源及证据 | 读写内容 | 归属 |
| --- | --- | --- |
| R:8397–8553 | 参数 renderer、D2D context、mapping.layoutScale/targetOrigin，基础/本体/抓手/刚性组矩阵、本地光心和光半径、字号目标倍率 | 自带 renderer、本体原点与布局尺度；传入目标、可用区域与像素标尺。预览不需要底栏拉伸、刚性抓手、拖动和停靠映射 |
| R:8417–8444、8554–8575 | 工具快照、背景警告文字、MainButton 中心/当前宽高、MainBar 继承位置、Draw 按钮的属性锚点 | 自带底板及当前几何，直接从居中布局根继承。预览不需要警告、Logo 和属性锚点 |
| R:8577–9755、9980–10389 | 属性盘色块和轮盘、笔型、粗细/滑条/细调刻度、文字/浮窗、状态读写、取色器、形状/橡皮/撤回面板、施工提示；各自的 Shape/SVG/PNG/Word、反馈层和 DWrite/几何缓存 | 预览不需要；整条子树留实栏，包括 R:9248–9253 对交互字段的写回 |
| R:9757–9786 | mainPanel、MainBar shape、当前 bounds、pptIdleFade、常驻内容透明度、mainButtons 与逐非空项的 mainBarLayoutSnapshot.visible；退场先画，进入后画 | 自带面板/底板、当帧列表/可见性、布局，局部 bounds；传入常驻透明度。预览不需要 PPT 淡化（取 0） |
| R:9788–9830 | MorePanel、MorePanelDivider、MorePanelCloseHit、MorePanelClose SVG、moreClosePressScale、More 面板和两组按钮、More 入口位置、主栏继承、icon.pct；缺失 moreButtons 时读取实栏集合 | 自带全部图元/按钮，显式传两组；C1 模型投影第二组为空。真实调用保留原快照回退。More 在刚性变换下先画，再切 body 画主栏 |
| `Bar.Button.cpp:1863–1869`、`UI3.Button.cpp:441–495` | 当前/目标位置、使能、尺寸、按压倍率与时长、文本/图标内容过渡、颜色、框/角标各层、SVG 或 PNG、clip 可见范围及可选 draw options | 自带每项的全部可变图元/状态、renderer 的刷子、字号及位图缓存；普通按钮使用现有默认 options，预览不派发 clickFunc |
| `UI3.Panel.cpp:67`、`UI3.Renderer.cpp:1914–1945`、`Bar.Initialization.cpp:1236–1254` | 底板的 themeSurface/residentSurface、填充/边框 pct、圆角、描边、点光、lightMaterial 与常驻填充缩放；材质阴影、设备资源 | 自带 shape 与材质进度；传入已应用主题和透明度、光效开关及共享只读设备资源 |
| R:9834–9976 | 超椭圆与 Logo、主按钮加宽方向与量、状态指示器、按压/形变、各 map 全组以及 live More/list 的旧新范围并集 | 预览不需要。预览独立生成几何，不能修改真实 dirty tracker、命中发布或任何图元 |
| R:10386–10478 | currentContentBounds、施工提示、dockTargetIndicator 的 visible/scale/opacity/两种 geometry、dockModeTextFormat/Label、指示器 shape、word、progress、材质、局部字体变换 | 预览不需要 |
| R:8100–8174、10479–10507 | 是否显示、目标文字、进入时刻、局部指针、可用矩形、排版测量、hintRect/Shape、lightMaterial、背景/边框/文字色、formatCache、fontCollection、字号策略、字族及 locale | 自带 hover 来源与期限、矩形、提示底板和格式缓存；传入语言/字体资源与主题。预览不需要 monitor/workArea、主栏全局提示 deadline |
| R:10514–10526 | 字体版本、小数字号策略、当前与最终 zoom、frameTime、dt、动画开关/速率、已算 lighting 和边光开关 | 配置与时间传入；实例自带 clock、最终尺度、字号帧和局部光照。预览不需要实栏的 configZoom、displayUiBaseScale 或实栏灯光快照 |
| R:8971、9879、10529；`UI3.Button.cpp:451` | presentationProbe、SVG 观察、合成边界、宿主裁剪/呈现与第二宿主闭包 | 预览不需要真实 probe、BarAtomic、floating_window、交互提交及重放闭包；外部 context 上原有观察点保留在共用画法内 |

### 直接成员核对索引

下列为 R:8410–10509 中实际用到的成员，组内同一归属。图元 map 内按键区分，不能把整张表传入实例。

- **自带** · `mainPanel, morePanel, mainBarLayoutSnapshot, moreClosePressScale, hoverHintShown, hoverHintText, hoverHintRect, hoverHintShape, barLightMaterial`。`shapeMap` 仅 MainBar、MorePanel、MorePanelDivider、MorePanelCloseHit 转为独立成员，`svgMap` 仅 MorePanelClose。`barButtonSet` 的 main/More 列表转实例；`barMedia.formatCache` 换实例服务。`sizeMode` 使用外部设置的本帧副本。`current` 在实栏仍是原引用，预览只累计自己的范围。
- **从全局快照传入** · `content.fontCollection`、本地化字族/locale 与版本、已解析按钮文本与资源、色板模式、背景/内容透明度、光照/动画/字号设置。`content.mainButtons/moreButtons` 在实栏是帧快照，预览使用自己的帧列表。`barStyle.dpiZoom` 只在实栏的属性计算使用。
- **预览不需要** · `stateModeSnapshot, bottomDockHorizontalMapping, bottomDockMapping, pptIdleFade, mainButtonLightMaterial, compactWidenSign, compactIndicatorExtra, superellipseMap, pngMap, wordMap, bottomDockIndicatorShape, bottomDockIndicatorWord, bottomDockTargetIndicatorProgress`；`barState.drawAttributeBar` 的 thicknessFineDialCandidateActive、thicknessFineDialVisualWidth、thicknessViewMode；`content.penMode, statusBounds, currentContentBounds, bottomDockedDragActive, dockModeTextFormat, dockModeLabel` 及 `dockTargetIndicatorVisible, dockTargetIndicatorScale, dockTargetIndicatorOpacity, dockTargetIndicatorGeometry, dockTargetIndicatorVisualGeometry`。属性盘和主按钮使用的 shape/svg 键同归实栏。
- **预览不需要的 owner 子对象** · `drawAttributePanel, drawAttributeInfoFeedback, drawAttributeSwatchFeedback, geometryAttribute, eraserAttribute, recallAttribute, penPreviewRendering, annotationTooltipPanel, overflowTooltipPanel, colorPicker, statusIndicator, constructionPrompt, bottomDockIndicatorPanel`。这些对象内部的控件、模式、字体及设备资源随整棵子树排除。
- **预览不需要的细调/属性成员** · `drawAttributeAnnotationClosePressScale`、`drawAttributeAnnotationPopupProgress`、`drawAttributeBrushPressScale`、`drawAttributeColorPickerEntryPressScale`、`drawAttributeHighlightPressScale`、`drawAttributeLaserCoreThickness`、`drawAttributeLaserCoreWhiteMix`、`drawAttributeLaserOuterThickness`、`drawAttributeLaserPressScale`、`drawAttributeLaserShellProgress`、`drawAttributeOverflowClosePressScale`、`drawAttributeOverflowPopupProgress`、`drawAttributePenPreviewMorph`、`drawAttributePenThickness`、`drawAttributePenTypeExtensionPressScale`、`drawAttributePenTypeExtensionProgress`、`drawAttributePenTypeFreeLinePressScale`、`drawAttributeSoftPenPressScale`、`drawAttributeThicknessAdjustPressScale`、`drawAttributeThicknessCoarsePressScale`、`drawAttributeThicknessFineDialActivationAnchorFrame`、`drawAttributeThicknessFineDialActivationGeometryTransition`、`drawAttributeThicknessFineDialCandidateFrame`、`drawAttributeThicknessFineDialDwellProgress`、`drawAttributeThicknessFineDialProgress`、`drawAttributeThicknessFineDialRecognitionVisibility`、`drawAttributeThicknessFineDialSelectionProgress`、`drawAttributeThicknessFineDialValueFrame`、`drawAttributeThicknessFinePressScale`、`drawAttributeThicknessHoldGroupScale`、`drawAttributeThicknessHoldProgressFrame`、`drawAttributeThicknessHoldRingLockOpacity`、`drawAttributeThicknessMediumPressScale`、`drawAttributeThicknessPresetCircleDiameter`、`drawAttributeThicknessPresetNumberProgress`、`drawAttributeThicknessPresetNumberValues`、`drawAttributeThicknessPreviewNumberRect`、`drawAttributeThicknessPreviewPopupAnchor`、`drawAttributeThicknessPreviewPopupGeometryValid`、`drawAttributeThicknessPreviewPopupProgress`、`drawAttributeThicknessPreviewPopupScale`、`drawAttributeThicknessSliderAccentOpacity`、`drawAttributeThicknessSliderCenterDiameter`、`drawAttributeThicknessSliderProgress`、`drawAttributeThicknessSliderThumbScale`、`drawAttributeThicknessSliderTrackOpacity`、`thicknessFineDialNewRangeOpacity`、`thicknessFineDialNewRenderRange`、`thicknessFineDialOldRangeOpacity`、`thicknessFineDialOldRenderRange`、`thicknessFineDialRangeTransitionPhase`。以上仅在原属性路径读写。方法局部变量、枚举键与编译期几何常数不作为跨实例状态。

## 3. 从模型生成按钮

使用 `Bar.LayoutModel.cppm:20–52,60–69` 的四段、有序条目和 `Position`，接收已经规范化的模型。持有完整模型与按出现次数区分的运行期对象。建立 A1 → 固定交界 0 → B1 全部可见项 → More 入口 → 固定交界 1 → A2；More 独立收集。隐藏项保留原模型下标。连续 UserDivider 由 `ResolveBarButtonListLayout` 处理（`Bar.Button.cpp:1874–1912,1923–1945`）。两条固定交界分别克隆，MoreBoundary 是标记，入口另有专用原型（`Bar.Button.cpp:757–799,1462–1505`）。空 More 时预览保留入口作为收纳目标；没有可见面板项时关闭面板。

**实例来源**。用独立的初始注册集合，调用 `PresetInitialization` 和 `RegisterBuiltInComponents`（`Bar.Button.cpp:223,802`）得到尚未参与真实动画/上传的原型。按语言版本在宿主边界重建原型快照，提供本地化资源和默认图标，不调用带全局投影/持久化副作用的 `StateUpdate/Load/RefreshLocalizedText`（同文件 934、1084、1199、1409、1513）。从快照取出所有条目，包括单例、用户分割线、两条交界、More 和尺寸所需的伴随项，一律按 C1 的拷贝构造逐份建立并显式 `state = &localState`，清空 `clickFunc`、重置 emph、hoverStage、pressScale、旧字号/图标过渡与缓存。副本使用新对象的 badge/SVG/PNG/Word；不借实栏的 registration.button。C1 对不可重复项会直接使用注册对象，不能照搬该分支。证据见 `Bar.Button.cpp:1421–1458`、`Bar.Button.cppm:46–66`、`UI3.Button.cppm:89–129`、`UI3.Primitive.cppm:360–385,429–443,483`。语言变化原位更新实例文案/排版，合法拖动身份以实例键维持。

**参考外观**。推荐展示常规展开桌面、无历史且不处于空页精简选择态的样例；选中工具采用可复现的硬笔样例，沿用真实硬笔图标 barBrush1 和目标标签（`Bar.Button.cpp:1217–1226`、`Bar.Golden.Test.cpp:273–279`）。这是编辑页参考姿态；冷启动空页选择态会按运行时条件隐藏部分工具。深浅、尺寸、排列、已配置条目及显隐随设置刷新。Setting 随设置页可见快照使用其真实选中色；不可用动作的灰色外观与编辑器的命中资格分别处理。

| 项 | 默认预览与命中 | 依据 |
| --- | --- | --- |
| Recall / Redo | 大尺寸无历史，仅展开的灰色 Recall。小尺寸常规展开显示灰色 Recall、Redo；保留真实投影的插入/交换次序。Redo 无模型位置，命中归到所属 Recall 的编辑单元；抓起 Recall 一并移走伴随项 | `Bar.RecallChrome.cppm:41–68,139–155`、`Bar.Button.cpp:1038–1057,1464–1474,1950–1960` |
| CanvasDebug / Pierce | 默认不显示。调试开关、分离模式及当前工作区会在实栏改变投影；不把这两枚临时按钮注入配置 | `Bar.Button.cpp:972–989,1026–1037,1486–1505`、`Bar.A2.cppm:48–62`、`Other.Config.cppm:432` |
| Whiteboard / Freeze / EndShow | 它们都是 A2 模型项。桌面参考下按 `ResolveBarA2Projection` 的白板功能、定格可用快照呈现相应尺寸/显隐，EndShow 默认无像素。宿主传入演示投影时 EndShow 替换桌面专用项并可按其原下标排序。隐藏项始终保留，其他项跨过它时以模型缝隙定位 | `Other.Config.cppm:298–304`、`Bar.A2.cppm:48–62`、`Bar.Button.cpp:1016–1033` |
| 默认工具、组件、设置、分割线 | 常规样例展示所有可见 A1/B1。大尺寸用有效 span；小尺寸除竖线外折为 oneOne，隐藏标准按钮文字，More 上大图标、选中底色、分割线点光按同一视觉规则。Setting 在 B1/More 恰一份。固定线和 More 不给可移动 Position | `Bar.Button.cpp:1047–1060,1475–1485`、R:3752–3829 |

## 4. 布局、暂态与命中

实例保存基准模型版本和按位置映射的编辑投影。抓起时只从用于装箱的序列移除所选 occurrence（含依附控件），保留克隆及原 Position，不调用 C1 Remove，故必需项和 Setting 不会被规范化补回。以新的装箱宽作为底板 `w` 的目标；按钮中心、尺寸、透明度从当前值重定向。放入时重用手中的实例、插入目标序列；外部新增则新建，取消恢复原位；跨段仍保持独立的运行期键。外部替换模型时使旧命中代次失效。单个格位/一整列消失时宽度才会减少；双行共列只移走半格、或只移走 More 内项时，装箱宽可相同，不人为造空列。测试选独占列观察缩短。实栏批次与宽度目标见 R:3667–3716、3749–3782、4049–4053。

More 复用 `GroupedGrid`（`Bar.Button.cpp:1971–1980`）。共同提取 R:5505–5713 的网格到面板姿态、缩放与按钮定位；输入两组条目、尺寸档、已推进的几何/透明度值与目标、More 中心、上下展开侧、主栏高、可用边界、可选外侧限位。实栏保留限位条件及槽位写回先后；预览走居中、向有空间一侧展开的分支。开合使用 R:5495–5504 的 Back 几何和 Sine 透明度两条曲线、按钮局部坐标与目标字号倍率。以显示中的 panel/body 位置输出命中，主栏在 More 之上，关闭侧栏单独命中，收拢/隐藏项退出命中。抓走 More 最后一项时仍可落到 More 入口，放回后按请求展开。

统一采用**目标局部像素**（含居中偏移和阴影留白），查询与绘制共享同一帧的变换。稳定局部 DIP 布局先按 `ResolveBarMainIconX` 镜像中心，设置 2/3 取固定方向，其余按左到右；面板行仅依展开侧翻转（`Bar.MainLayoutPolicy.cppm:10–33`，R:3735、5664–5677）。无需镜像文字与图标。候选记录完整项矩形、段与下标、锚定线段及可吸附区域。横向列间取相邻外缘中点，双层同列用上下项间的水平缝；More 按实际 row/column、行首行尾和换行生成局部缝，两段交界各有独立候选，空 B1/More 提供索引 0，末尾为原段 size。每个可见项前后映射到模型 gap，隐藏项不造屏上矩形。重复 ID 按 Position 区分。用户线身窄，命中可扩到毗邻空隙中点，优先直接命中的实体。固定交界、伴随项和 More 不贡献额外模型下标。多候选等距先匹配所在段/行，再按逻辑下标；C3 根据 C1 CanPlace 选择可用候选。

抓起期间候选仍用**移除前**的基准模型编号，跨过源位的缝按相邻存留项反查；源前/后重合缝等价，选择源前。C1 `Move` 自行对同段右移减一（`Bar.LayoutModel.cppm:191–202`）。几何带版本，只有宿主确认该帧绘制成功才发布，失败/代次不符禁止用候选帧命中。Revision 同时支持 C3 判别内容变化。

## 5. 独立帧、光照与字号

1. **尺度**。宿主传入目标高度 H（建议 64 页面单位换算为像素）、可用像素宽 W。取 `z = min(H / barHeight, 可用净宽 / 自然包络宽)`，`barHeight` 取 80 或 42.5（`Bar.Metrics.cppm:78–116`），包络计入展开 More、边框/阴影与动画超调。先按候选 z 扣去两侧 AA 像素和材质外扩，再求可容纳的 z；零可用面积返回空几何。在暂态抓起至落定前用编辑前后较大自然宽锁定适配上限，避免窄宿主中的缩短被反向放大抵消。每帧以当前宽居中。单用 renderer 的 `SetFrameZoom(z)`，96 DPI 的目标或像素单位，基础 transform 仅平移；宿主页 DPI 只换算一次。
2. **文字**。实例有自己的 `UIRendering`、`UIRenderingServices`、格式缓存/设备缓存失效回调、每个 Word 的 `fontCorrection` 及 renderer 的 direct correction。以宿主同一代本地化排版、fontCollection、fractional 策略刷新实例，`BeginFontFrame(frameTime, context, targetZoom)` 取本实例稳定适配倍率；本体 `SetFontTargetScale(1)`，More 使用最终姿态比例。仅 Geometry 标签按原条件传 labelRenderer。UI3 还有线程局部的标签适配数值缓存，键含文本、字族、locale、槽宽、原字号与设备代次（`UI3.Button.cpp:588–612`）；同一共享字体集合下可复用该纯测量结果。DWrite factory/collection 可共享，实例的格式缓存及 resetDeviceCaches 不能捕获真实 owner（`UI3.Renderer.cpp:161–168,2468–2483`、`Bar.Main.cpp:79–91`）。测量、绘制、几何登记同帧同序号，禁止第二次推进。R:10514–10526 的同时间戳防重只适用于**同一场景重放**，无法隔离共享图元。证据 `UI3.Renderer.cppm:54–76`、`UI3.Renderer.cpp:178–226,234–251`。按钮布局调用 UI3 显式 renderer/labelSize 重载，避开 `Bar.Button.cpp:1835–1845` 的全局测量器。
3. **推进**。以 `FrameContext.frameTime` 驱动自己的 `FrameAnimationClock::Tick`，idle 恢复首帧/切页分别按其挂起语义处理（`UI3.FramePacing.cppm:89`，R:2266、12452）。一次 Tick、一次给定目标、一次 `UiAdvanceAnimation`/`AdvanceButtonVisual`，图元不得由两条路径推进。使用 `UiAnimationAdvanceContextClass` 的 dtSeconds、speedRate、animationEnabled、forceReplace；关动画时对实例立即到目标，含有 animateWhenDisabled 的反馈层。速率只乘一次，输入源为宿主锁存的 `UiAnimationEnabled/UiAnimationSpeedRate/UiDefaultOperationDur`（`UI3.Animation.cppm:36–38,808–830`、`UI3.Animation.cpp:193–196`）。返回仍活动标志与下次 hover 截止时间；无时间差重复绘制不改变动画。实栏 continue/idle 独立。
4. **主题**。帧输入含 `darkStyle/backgroundOpacity/contentOpacity`。实例持有浅色材质权重及进度。仅给 UI3 retarget 传 dark 仍会漏掉 `GetThemeColor` 和 `value_or` 的立即求值；renderer 绘制常驻 surface 还会取全局背景倍率（`UI3.Button.cpp:157–218,340`、`UI3.Renderer.cpp:1923–1934`）。建议给 `UI3.Theme` 加窄的、可嵌套的线程局部只读快照作用域。实例从原型构造、目标设置、推进到绘制都使用它；所有无 mode 的颜色/透明度 getter 在作用域内走同一快照，颜色直接调用显式 mode 重载，不碰 applied/通知监听器。析构恢复上一指针；无作用域的实栏保留原路径。不可临时调用 `SetThemeStyleSource` 切换进程源（`UI3.Theme.cpp:159–225`）。
5. **灯光和阴影**。实例自带 `LightingStateMachine`，与 renderer 同 z；`primaryAnchor` 用逻辑序首个可见实体按钮当前中心（DIP，跳过线，若无锚则 unavailable），稳定身份用于锚切换。宿主指针以目标局部 px 加递增 serial 传入，减掉居中平移后作为场景像素，`layoutScreenOrigin={0,0}`；离开标为 unavailable。DIP 主锚在状态机内部乘 z，指针不再乘 z（`UI3.Lighting.cpp:184–208`）。从快照输入强调色、边光/动态开关，用私有状态机的帧快照设置 renderer 的 lighting 和 cursor local geometry，禁止发布 shared lighting。动态点光遵守现有开关：动画关闭时指针光立即归零，主光仍保留（`UI3.Lighting.cpp:448–449,490–508`）。给现有光照入口增加显式动画开关/速率重载，原入口继续读取 UI3 的原子设置；预览用本帧值避免二次乘速。主题圆角底板与 More、提示统一走材质绘制；透明预乘 BGRA，外围至少 `ceil(ThemeMaterial::SurfaceShadowOutsetDip * z) + dirtyAntialiasPadding`，包含面板超调、提示及描边，不能只按底板宽建目标（`UI3.Material.cppm:147`，R:8161–8169）。不为无 Logo 的样例画透明主按钮。
6. **提示**。两档均采用小尺寸样式。对稳定的鼠标宿主坐标命中，取实例按钮目标标签、换行转空格，500 ms 延时；已显示时移入另一项直接换字。离开、触摸、抓起、布局/More 动画、来源失效立即清除，门禁后需新移动。复用测量、避让/夹取和绘制（R:8102–8169、10479–10507），工作区改为局部可视矩形，提示最后画。More 的打开由外部明确请求，按钮回调始终不运行。

## 6. 提供给 C3 的接口

以下是拟议签名（在 Bar 域内），新增名字不表示当前仓库已有接口。实例由设置宿主持有；可按默认构造后 SetLayout 和 Advance 的次序初始化。公开类型只暴露模型、数值、资源句柄或只读几何，不暴露按钮对象。

~~~cpp
void SetLayout(const BarLayoutModel& model, uint64_t revision);
bool Lift(Position source, uint64_t revision);
bool Place(const BarLayoutModel& accepted, Position originalGap, uint64_t revision);
void CancelLift();
void SetMoreExpanded(bool expanded);
PreviewFrame Advance(const PreviewInputs& inputs);
UI3::UIRendering& Renderer();
HRESULT Draw(UI3::FrameTarget& target, const PreviewFrame& frame);
void ConfirmFrame(uint64_t frameId, bool succeeded);
const PreviewGeometry& Geometry() const;
PreviewHit HitTest(D2D1_POINT_2F targetPx) const;
std::vector<PreviewGap> InsertionGaps(D2D1_POINT_2F targetPx) const;
~~~

`PreviewInputs` 为宿主当帧时间/区域/目标高度、主题/字号/语言/尺寸/方向/光照/动画与参考外观快照、局部指针（有无、是否鼠标、位置、移动序号）。`PreviewFrame` 含帧号、所需包络、比例、内容版本、动画续帧和可选提示期限；`Advance` 可在拿到目标前测量。`PreviewGeometry` 含帧/模型版本、所有可见位置的矩形、More 入口/面板/关闭键的可选矩形和各段缝隙；`PreviewHit` 区分空、实体 Position、用户线、固定线、伴随项/所属项、More 入口/面板/关闭。`PreviewGap` 含模型 Position、方向与端点、局部吸附区域。`Place` 接 C1 已接受的完整模型及旧模型缝隙，revision 表示编辑基准版本；实例校验源身份并更新暂态，持久化成功的模型由宿主通过 SetLayout 回送新版本，相同布局只换版本，不重启动画；外部新增由新模型和插入位置定位克隆。删除、重置和外部刷新经 SetLayout；过期请求返回失败，宿主重新取得基准模型。

**线程与目标合同**。设置注册点 `Setting.cpp:5286,5596`、主栏 R:13547；同一个 `RenderPipeline.cpp:657,830–856` renderThread 串行调用。实例仅由设置 render owner 操作；本地渲染/查询无需再加锁。交互线程及设置输入提供值快照；共享资源按设备代次取得，任何注册表锁仅用于复制，不跨 D2D 或外部回调。设置持有 epoch 的 D3D11 设备租约（`Setting.Base.cppm:85–93`、`RenderPipeline.cppm:41–65`）。父 §5.1 的 BGRA8 RTV|SRV 纹理 → DXGI surface → 同设备 D2D context 的预乘 target bitmap 属于 C3。

宿主将实例 renderer 绑定到自己的 FrameTarget，设备更换先清该实例的 SVG/PNG/badge/刷子资源；销毁前解绑或先销毁 target。位图宿主 Ensure，外部宿主 AttachExternalContext、BeginDraw 并透明清屏；两者按 `UI3.Host.cppm:45–69,148–170` 在 draw 前后持有帧事务。Draw 仅向已就绪、绑定正确的 context 发命令，保存恢复 transform/绘图状态、配平局部层/裁剪；无有效目标返回失败。外部宿主 EndDraw 后调用 NotifyExternalEndDraw，再 ConfirmFrame；位图走原有 EndFrame/事务的成功结果。失败不发布新几何。C2 不取得 DC、不拥有交换链或纹理，不调用 ImGui。

## 7. 验收

使用下份实施计划的生产 CLI 和独立预览夹具。除了四种尺寸/主题的实际图像，必须能证明一次预览绘制没有推进另一实例的字号、几何、光照或按钮状态。真实主栏的 golden、两种第二宿主形态、属性脏区 retained/full 三项全部保留。此文档不代表已经通过构建或像素比对。

## 8. 主 Agent 审定（2026-11-20）

- **采纳方案 B**：预览为独立实例，真实主栏只把预览必须复用、目前内联在渲染循环中的绘制片段（主栏底板与按钮绘制 R:9757–9830 一带、悬停提示 R:10479–10507、More 网格到面板姿态 R:5505–5713）抽成共用函数并改为调用它们，其余不动。父 design §4 的“真实主栏委托实例”相应改为“真实主栏委托共用绘制函数”，门禁不变：golden 逐字节、第二宿主两种形态、属性脏区 retained/full 全部与 C1 基线一致。
- **删去 §5.4 的主题快照作用域**：需求要求预览的深浅色与真实主栏一致，预览直接使用全局已应用的主栏主题与背景/内容透明度，不新增 `UI3.Theme` 线程局部机制。
- **删去 §5.5 中“给光照入口增加显式动画开关/速率重载”**：预览与真实主栏共用全局动画开关与速率，沿用现有入口。预览仍需独立的 `LightingStateMachine` 与指针输入、不发布 shared lighting。
- **参考外观**按 §3 推荐（常规展开桌面、无历史、硬笔选中）；CanvasDebug / Pierce 不显示。
- 其余（独立 `UIRendering` 与字号帧、独立动画时钟、命中与缝隙接口、外部目标合同）按本文实施。
