# 设置组件页：主栏编辑器合同

> 来源任务：`11-20-settings-bar-component-editor`（C1 布局模型见 [configuration-i18n-and-assets.md](configuration-i18n-and-assets.md)「UI3 Bar 分区布局配置合同」；本文覆盖 C2 预览实例、C3 拖放与设置宿主、C4 组件库页面、C5 共享编排与交互）。

## 1. Scope / Trigger

修改设置 → 组件页、`Inkeys.UI.Bar:MainContent`（共享内容编排）、`Inkeys.UI.Bar:ContentInstance`（预览实例与共用绘制）、`Inkeys.UI.UI3.DragSession`、`Setting.ComponentEditor.*`、预览的 ImGui DX11 预乘合成与页面动效，或真实主栏的相应编排、推进、绘制调用时适用。

## 2. Signatures

- `Inkeys::UI::Bar::BarContentInstance`：`SetLayout(model, revision)`、`Lift(Position, revision)`、`Place(accepted, originalGap, revision)`、`CancelLift()`、`SetMoreExpanded(bool)`、`Suspend()`、`Advance(PreviewInputs) -> PreviewFrame`、`Draw(FrameTarget&, frame)`、`ConfirmFrame(frameId, succeeded)`、`Geometry()`、`HitTest(point)`、`InsertionGaps(point)`、`DrawGhost(target, source, point, hint, dangerOutline)`、`DrawFollowingHint(...)`。
- 预览输入接口：`Press(hit)`、`SetClosePressed(bool)`、`ReleasePress()`、`Activate(hit)`、`ShouldDismissMoreOnPress(overPreview, point)`。
- 编排与绘制接口见 §3.1，分别定义于 `Bar.MainContent.cppm`、`Bar.ContentInstance.cppm`，按钮推进使用 `Bar::AdvanceButtonVisual`。
- `Inkeys::UI::UI3::DragSession`：`Press / BeginDragging / Move / Release / Cancel`，`Phase / Payload / Point / Origin / PointerId / Kind`；`ReleaseResult { None, Click, Drop }`。
- `Inkeys::UI::Setting::ComponentEditor`：`Draw(ImGuiWindow* scrollWindow, void(*queueWrite)(), float pageOpacity, bool inputBlocked)`、`BeginLibraryDrag(componentId, screenPoint, touch)`、`DragActive()`、`PreviewPressInput<Instance, Hit>`、纯函数 `ResolveDrop(model, source, catalog, trash, outsidePreview, more, gaps) -> DropTarget`；拖动帧入口 `ResolveDragFrameDrop` 同时处理落点与 More 停留，`TrashBounds(preview, bodyBottom, width, height)` 求胶囊几何。
- ImGui DX11：`ImGui_ImplDX11_PremultipliedBegin / End` 特殊回调值（-18 / -19）。`FinishPreviewBlock(block, list, first, end, pageOpacity)` 结束入场块，并调整 `[first, end)` 的预乘图片顶点色。
- 设置控件：`Design::TabStrip`、`Design::ComponentCard(id, spec, size, bool* dragStarted, bool holdForTouch, bool* holding)`、`Design::DragSourceOverlay(id, min, max, skip, bool holdForTouch, bool* holding)`、`Design::DragSourceTouchHoldSeconds`（0.45 s）。

## 3. Contracts

### 3.1 真实主栏与预览的边界（C2、C5）

- `Inkeys.UI.Bar:MainContent` 共享按钮投影、内容样式、批次、目标写入及悬停提示。真实主栏在原帧阶段调用共享函数，仍负责窗口、主按钮、停靠、换边、属性面板、有限观察器、脏区与命中发布。共享层的输入由宿主提供，主题读取边界与实栏回归门禁见 [rendering-and-ui.md](rendering-and-ui.md#主栏按钮统一状态派生合同)。
- 预览每帧的顺序：
  1. `Project` 中在样式变化时调用 `ResolveSelectStyle`、`ResolveDrawStyle`、`ResolveEraserStyle`、`ResolveGeometryStyle`、`ResolveWhiteboardStyle`、`ResolvePierceStyle`，再由 `ResolveMainBarPresentation` 投影尺寸、隐藏与状态。共享 `ResolveBarButtonListLayout` 计算布局，宿主用目标包络更新相机。
  2. `ResolveMainBarBatch` → `ApplyMainBarButtonTargets`（含 `AnchorHiddenBarButton`，预览也用它为抓起项补锚点）→ `ApplyMainBarBodyTargets`（`mainButtonWidth = std::nullopt`，居中）→ 推进底板及本体的 `Bar::AdvanceButtonVisual`。底板与按钮沿用同一批次相位。
  3. `ApplyMainBarMoreTargets` → `PositionContentMore` → 推进 More 的进度和透明度、面板按钮、X 形状及 `AdvanceSvgVisual`；关闭动画时先把 More 进度和透明度同步到目标再定位。随后推进批次时间轴，更新几何与 `UpdateMainBarHoverHint`，提示显示时调用 `PositionMainBarHoverHint`。绘制共用 `DrawContentBody`、`DrawContentMore`、`DrawContentHint`。
- 预览按钮为独立克隆（`state = &localState`、`clickFunc` 为空），自持布局、动画时钟、`UIRendering`、字号与格式缓存、光照及输入。沙盒工具模式默认 `IdtPen`／软笔，无画布内容、白板或放映会话；`whiteboardEnabled`、`freezeAvailable`、`settingsVisible` 是宿主的 `PreviewInputs`，设置宿主当前沿用三个默认 `true`。预览自行持有工具模式、按压和 More 展开状态，既不读取也不修改实栏的运行实例、`BarAtomic`、`floating_window`、光照、脏区或命中。全局已应用主题、透明度、动画开关与速率、选择/穿透模式配置和图形工具样式可以只读共享；`GetStateModeSnapshot` 在这里供图形工具规格与固定状态取样，沙盒的选中工具由本地模式决定。
- 预览主列表与实栏 `buttonList` 同序：A1（Redo 在 Recall 前）→ 交界线 → B1 → More 入口 → 交界线 → A2（Pierce 在 Freeze 前）。More 面板另有同序列表。用户配置中不可见的条目留在模型；已入运行列表后因投影或抓起而隐藏的实例留在列表和批次快照，按锚点收拢淡出；本体退场项先画，命中与缝隙只使用可见项。布局替换时 `Rebuild` 沿实栏 `Load` 的身份规则：单例按 ID 复用，可重复 ID 在同段仅有一份时可换位复用，多份须数量、位置和可见性逐项相同才整组复用。`Place` 移动已抓起的实例，保留在途动画。
- 相机按目标栏高和含阴影的目标宽高包络取等比适配，独立于实栏 `configZoom` 与任务栏基准。`IconOrder` 2/3 按固定方向，其余左到右。预览块上沿紧贴标题，主栏上方常驻一行 More 的回弹峰值预留（含锚点间隔与阴影）；More 达到两行及以上时随目标网格向下扩展，下方约留一行。相机目标随布局或可用尺寸改变，经 `EaseOutCubic` 过渡；新网格立即受目标包络限制。`frame.bounds` 是从宿主局部原点起的稳定矩形，供裁框、布局和放下判断；`frame.extent.height <= inputs.height`。单独开合 More 不改变主栏位姿、缩放或裁框。
- 几何仅在 `ConfirmFrame(frameId, true)` 成功确认后发布；悬停与命中共用层级判定（本体与 More 面板重叠时本体优先）。普通悬停提示仅在小尺寸启用，新的鼠标移动登记来源，沿共享的 500 ms 延时及布局/More 动画门禁。

### 3.2 输入与拖放（C3、C5）

- 设置宿主由 `PreviewPressInput` 的单一 `DragSession` 管理按压、捕获、阈值和取消，组件库拖动共用此会话。状态机独立于主栏与 ImGui，只接受当前接触 ID；鼠标阈值 4 DIP，触摸/笔 10 DIP，按宿主像素比换算。资源未就绪禁止起拖。
- 按下记录已呈现的 kind、position、revision，立即给按压视觉并停止悬停。跨过阈值时，`Lift` 在标记抓起前先调用 `ReleasePress`；抓起项隐藏并从装箱序列移除，仍保留原 `Position`。`InsertionGaps` 返回移除前的模型缝隙编号，原位放下保留夹在中间的隐藏项顺序。普通抬起只有在预览命中范围内、`DragSession` 判为 `Click` 且同 kind、position、revision 时才 `Activate`；拖动抬起只处理放下。固定交界线不能抓起，按下短暂提示。
- X 按 `ButtonPointerState` 的捕获语义处理：移出松开视觉，移回且仍按住恢复；同一 revision 内在 X 上抬起关闭，以该次输入的 `presentedHit` 为准。工具项点击只切换沙盒的选择/绘制/橡皮/图形，不打开属性面板；其他普通按钮只给按压反馈。
- More 入口点击切换，X 点击收起，面板项按 `closeMoreAfterAction` 决定是否收起。组件页提交期间，设置客户区内在 More 入口、面板及其内容之外按下左/右键会收起；几何未确认时跳过外按收起。没有可见 More 项时禁止展开，布局替换或放下使其清空时清除展开状态。任意拖动来源在裁框内命中已呈现的 More 入口满 0.5 秒可自动展开，与落点能否收纳独立；每次进入只消费一次，移出 More、离开裁框或取消时重置计时，空 More 不展开也不标脏。
- 设置宿主每帧读取 `ReadBarLayout()`，布局替换时取消拖动并调用 `SetLayout`；处理输入后 `Advance`，按需绘制并确认预览纹理（静止时复用），再合成及处理落点。Esc、失焦、页面退场或未再提交、活动项被夺、输入来源变化、鼠标物理主键已松开而 ImGui 仍报告按住连续三帧，以及设备换代、目标或预览/浮层绘制失败，统一走 `Cancel()`，清按压、抓起和捕获。目标失效时下一帧开始前丢弃本地 context 与两张纹理并重建，已排进当帧绘制列表的 SRV 保持有效。
- 放下时预览区域取整个已呈现 `frame.bounds`（含上下预留），垃圾桶优先，区外不考虑缝隙或 More。区内命中 More 入口优先检查能否追加到 More 末尾，其余缝隙按距离依次用 `CanPlace` 过滤。More 拒收只有一项例外：空 B1 的插入缝锚在 More 边缘，命中范围向按钮内延伸半个 `ButtonGapDip`（按预览缩放换算），因此用户分割线（库来源或已有实例）仍可落合法的 B1 缝隙。More 面板不收分割线，重叠的 A1/A2 缝也不适用此例外。

| 来源 | 位置 | 反馈 | 松手 |
| --- | --- | --- | --- |
| 预览可删除项（组件、用户分割线） | 合法缝隙 / 可收纳的 More 入口 | 插入 / 收纳 | `Insert` / `Store`，移动 |
| 预览可删除项 | 垃圾桶或预览区外 | 会被删除 | `Delete` |
| 预览可删除项 | 预览区内非法位置 | 不能放在这里 | `Invalid`，回位 |
| 预览固定项（A1/A2 默认项、Setting） | A1/A2 各自区内，或 Setting 的 B1/More 合法落点 | 插入 / 收纳 | `Insert` / `Store`，移动 |
| 预览固定项 | 其他位置（含垃圾桶和预览区外） | 只能在原区域内调整位置 | `NotAllowed`，回位 |
| 组件库项（含分割线） | 合法缝隙 / 可收纳的 More 入口 | 插入 / 收纳 | `Insert` / `Store`，添加 |
| 组件库项 | 其他位置（含垃圾桶和预览区外） | 松手取消添加 | `CancelAdd`，放弃 |

- `Move` / `Insert` / `Remove` 生成模型后，由 `ApplyBarLayout(expected, next)` 发布。仅 `Applied` 入队 `QueueConfigWrite`，插入或移动时还调用 `Place`；松手后 `Cancel(true)` 再读取已发布布局并 `SetLayout`。拒绝或过期时回位且不入队。拖动中不做边缘自动滚动：预览固定在页面上方（§3.3），拖动目标始终可见，下方滚动视口在拖动期间保持不动。
- 垃圾桶胶囊由 `TrashBounds` 放在主栏下方预留区的中央。默认高 36 DIP、左右各 16 DIP 内边距、16 DIP 图标与 8 DIP 图文间距；可用宽高不足时收进预览块。开始拖动时淡入，正常松手后保留最后配色淡出；只在拖动中命中。危险态沿用设置标题栏危险按钮的 `SystemFillCritical` 底色与 `TextOnAccentPrimary` 前景；固定项呈禁用前景，放下仍回位。
- 幽灵使用当前的大/小尺寸按钮，先绘制主栏同材质圆角底板（含边框、阴影，四周各加 `ButtonGapDip`），再绘按钮；已选中的预览按钮保留选中态。可删除项在删除落点，或库项命中启用的垃圾桶时，用 `LayerParameters1` 合成底板和按钮后将整体降至 0.45 不透明度，危险描边与跟随提示保持清晰。跟随提示按落点显示，大、小尺寸拖动均可用。

### 3.3 组件页（C4、C5）

- 结构：固定区（`PageHeader` → 预览块）→ 内层滚动视口 `##component-library`（快捷方式行（`NavigationRow`，跳 `PageId::Window / Section::WindowLayout`）→ 恢复默认布局（两段式：首次点击变“确认重置”，3 秒内再点执行 `Reset()` 并清旧开关，离开页面或超时复位）→ “添加组件”分区 → `TabStrip`（全部 / 内置组件 / 桌面操控 / IslandCaller / SecRandom / NamePicker / ClassIsland））。
- 固定区：标题与预览块直接提交在 `##setting-page-content` 中，其后的内层滚动 child 高度 = 外层 `InnerRect.Max.y` − 外层底部 `WindowPadding.y` − 当前光标 y − 外层 `Scroll.y` − 1，向下取整。按未滚动位置计算，外层内容恰好不超过正文视口，`ScrollMax.y` 为 0，即使测高滞后一帧留下旧滚动值，下一帧也回到 0。拖动滚动与滚轮由 `DragEligible` 自然落到内层；固定区内拖动或滚轮不滚动。预览高度上限 `in.height = max(80 DIP, 0.5 × 外层 InnerClipRect 高度)`，More 多行时固定区随相机过渡增高、内层同帧变矮。内层 child 的块仍由 `PageBlockScope` 参与入场，退场淡出由 `ApplyContentAlpha` 覆盖整棵子窗口；内层滚动条透明度与外层相同（`PageScrollbarAlpha × pageTransition`）。
- “全部”：不分小节，所有条目按 Tab 顺序（内置组件 → 桌面操控 → … → ClassIsland）连续排成一个矩形卡片网格（`ComponentCardMinimumWidth` 260 DIP、`ComponentCardHeight` 96 DIP、`ComponentCardGap` 8 DIP）；标题最多两行，精简说明按剩余高度显示一到两行，越界部分裁剪在卡片内（多语言测试直接断言“标题 ≤ 两行且至少留一行说明”，不依赖墨迹检查）；分割线卡片位于网格首位且没有“+”。目录 `group` 与 Tab 下标一致（1 内置组件 … 6 ClassIsland），“内置组件”Tab 名复用 `BuiltinSection` 文案。专属 Tab（含“内置组件”）：整行条目，完整说明 + 前置条件；ClassIsland Tab 顶部保留 URL 协议提示框。
- 操作键：主栏（含 More）无该组件 → “+”执行 `AutoAdd`（B1 已有 ≥2 个组件进 More）；有 → 垃圾桶执行 `RemoveAllOf`（分割线为 `RemoveUserDividers`，不影响固定交界线）。`Applied` 后 `QueueConfigWrite()`。
- 拖动源：卡片/条目非操作键区域由 `DragSourceOverlay(holdForTouch = true)` 覆盖；操作键悬停时不提交覆盖层；开始拖动即 `BeginLibraryDrag`，之后由宿主接管。
  - 鼠标：越过 4 DIP 即开始。覆盖层同一按钮此前被触摸按住时留下的 `##content-drag-button` 登记，在鼠标激活时清除，页面拖动滚动不会抢先。
  - 触摸/笔：激活即 `AllowContentDrag()` 登记为内容型按钮；`MouseDownDuration ≥ DragSourceTouchHoldSeconds` 且 `MouseDragMaxDistanceSqr < MouseDragThreshold²` 时开始。抓起前位移越过 ImGui 原生拖动阈值，帧首 `BeginDragScrolling` 把活动项夺给内层滚动宿主，即按页面滚动处理；静止容差与该阈值相同，不存在既不抓起也不滚动的区间。内层没有滚动范围时越阈值后既不滚动也不抓起。
  - 等待期间 `holding` 为真，页面逐帧 `RenderPipeline::Request(Settings)` 推进计时（手指静止不产生输入）；抓起反馈为幽灵按钮出现在指针处。
- `PageBlockScope` 包住预览纹理和块内编辑反馈，入场透明度及上浮位置随页面块。预乘纹理由 `PremultipliedBegin/End` 成对包住 `AddImage`，以 `ONE, INV_SRC_ALPHA` 合成并恢复默认混合。`FinishPreviewBlock` 在块结束后用已乘入场透明度的 alpha 补齐纹理顶点 RGB，使入场系数同乘 RGBA。退场通常经 DX11 离屏组先合成块、再对结果四通道同乘页面透明度；逐顶点退路由 `FinishPreviewBlock` 把退场系数补到图片 RGB，正文随后统一乘顶点 alpha。每个系数对对应通道只乘一次。退场首帧以 `inputBlocked` 阻断输入并取消拖动，前景幽灵与跟随提示不参加淡出。

## 4. Validation & Error Matrix

| 情况 | 行为 |
| --- | --- |
| 布局未就绪（`ReadBarLayout()` 为空） | 预览不绘制，操作键禁用 |
| 预览或设备资源未就绪 | 禁止起拖；预览或浮层当帧失败时取消，不提交放下；失败帧不发布新几何 |
| 设备换代 / `D2DERR_RECREATE_TARGET` | 取消手势并清理本地资源；同代目标失效在下一帧重建 |
| 拖动中外部改布局 / `ApplyBarLayout` 返回 `Stale` | 取消抓起，重读已发布布局，过期编辑不写盘 |
| 抬起身份或 revision 不同 / X 外抬起 | 清按压，不激活 |
| More 几何未确认 / 无可见 More 项 | 前者跳过外按收起；后者清展开，点击或停留均不打开 |
| 落点越界或被拒 | 按 §3.2 返回删除、回位或取消添加；More 边缘只准用户分割线落合法 B1 缝 |
| 混用输入来源、ImGui 活动项被夺、鼠标捕获失联或页面开始退场 | 走取消路径，不提交放下 |
| DX11 离屏组不可用 | 走逐顶点退路，预乘图片仍按 RGBA 同比例淡出 |

## 5. Tests Required

- 实栏门禁：golden 35 项逐字节一致；`--ui3-trace-dump <dir>` 的 16 个固定步长场景对 `Build/ui3-trace/base` 逐帧逐值一致（大/小尺寸的收起展开、前半程加入、后半程重开、More 正反向、折叠、按压、绘制属性、换边重排）。两道门禁分别覆盖呈现结果与帧间轨迹。`--ui3-host-offscreen-test`、`--ui3-attribute-dirty-test`、`--bar-eraser-offscreen-test`、`--ui3-geometry-attribute-test` 继续覆盖第二宿主、属性脏区及工具渲染。
- `Bar.Host.Test.cpp` 的 `preview.replica.<large|compact>.*`：`more-{narrow,roomy}-*` 验证回弹峰值、位姿/裁框/缩放稳定、高度上限及确认前后外按；`more-grow-*` / `more-height-*` 验证多行目标包络与高度受限时逐帧入框；`close-glyph-pixels` 验证 X 像素，`batch-edge-sync` 验证底板与末端按钮同相。
- 同一离屏用例族还覆盖 `press-*`、`lift-releases-press`、`host-close-*`（移出、移回、裁剪、过期与已呈现命中）、`activate-*`、`select-collapse-intermediate`、`select-gaps-exclude-hidden`、`pen-restores`（收拢、隐藏项不占缝、再切绘制恢复）、`place-last-more-closes` / `rebuild-empty-more-clears-expanded`、`trash-in-presented-reserve`、`divider-drop-*` 和 `*-hover-more-half-second`。其余预览用例继续验证像素/主题、实例隔离、失败帧不发布几何、原位保序、小尺寸提示的 500 ms 定点与大尺寸禁用、空 More 停留不标脏。
- `drag_session_tests.cpp`：接触 ID、鼠标/触摸/笔阈值、点击/放下与取消；R5 验证垃圾桶优先、区外删除与区内非法回位、固定项区内重排与区外回位、Setting 不可删除、拒绝 A 区落点且可从 More 移入 B1、库项插入/收纳/放弃、空 B1 的 More 边缘例外（库分割线和已有分割线、拒绝 A2 回退）及胶囊居中和窄区限幅。
- `setting_home_warp_tests.cpp::CheckPreviewBlend`：预乘图片与直接绘制像素一致并恢复混合；入场块与旧页退场叠加时，离屏组和逐顶点退路的图片位移一致，分别以红色通道检查 RGB 淡出、以青色底的 G/B 衰减检查 alpha。`setting_design_tests` 继续覆盖卡片/条目多语言排版。
- `setting_design_tests.cpp::CheckDragSourceTouchHold`（popup-only 与全量 1.0/1.5/1.875 倍）：触摸/笔按住满时长才起拖且不滚动、提前松开不起拖、抓起前越阈值转为页面滚动且不再起拖、触摸按过后鼠标越 4 DIP 仍立即起拖、未启用长按的触摸保持 10 DIP 阈值。
- 实机：鼠标/触摸拖动、跨 child 捕获、触摸长按时长与容差手感、卡片上直接滑动滚动、固定区在 More 多行时的挤压、卡片起拖至放下及 Win7 SP1 + KB2670838 的预览绘制与页面退场。

## 6. Wrong vs Correct

| 错误 | 正确 |
| --- | --- |
| 预览另建布局时间轴，底板边缘与末端按钮错相 | 宿主通过 `ResolveMainBarBatch` 生成同一批次，两边的目标写入共用批次曲线 |
| `Rebuild` 整排新建按钮，编辑后未动的按钮也重新飞入 | 根据 `Load` 的身份规则复用实例与在途动画 |
| 用正在动画的底板宽度或 More 进度适配相机，压掉回弹或挪动裁框 | 用目标包络和回弹峰值确定缩放与预留 |
| 预乘纹理只乘 alpha 导致画面发白，或在图片 tint 和离屏组重复乘退场系数 | `FinishPreviewBlock` 同步入场 RGBA，退场由离屏组或 RGB 补偿加正文 alpha 退路处理 |
| More 拒收即返回，空 B1 的分割线拖不进；或任意相邻缝都回退 | 只对用户分割线检查合法 B1 缝隙，其他来源维持 More 的命中优先级 |
| 预览借用真实主栏的按钮，或在设置渲染回调中直接写盘 | 预览使用独立克隆；`ApplyBarLayout` 返回 `Applied` 后由设置业务队列写盘 |

## 7. 已知后续

- 静止组件页因预乘回调不能进入设置页稳定帧去重缓存，收到等价重绘请求时会重新提交一帧（审查 P1-4，记为后续）。
- 真实主栏中的拖拽触发方式（下一轮）；去掉 B1 上限后真实主栏超宽时的处理；A 区多支笔。
- More 外按收起只处理设置客户区，原生非客户区（标题栏）按下不收起。
- 组件卡片 `BeginLibraryDrag` → `Draw` 的完整 ImGui 交互由实机验收覆盖。现有离屏用例从 `ResolveDragFrameDrop` 驱动。
- Win7 SP1 + KB2670838 待实机验收危险幽灵 `LayerParameters1` 图层及页面淡出。产品渲染器其他旧重载 `PushLayer(D2D1::LayerParameters(...), nullptr)` 调用（如 `UI3.Renderer.cpp:346`）的 Win7 空图层兼容性未在本任务评估。
