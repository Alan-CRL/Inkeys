# Research: 拖动滚动性能与输入反馈时序

- Query: ScrollWhenDraggingOnVoid 的实际 CPU 开销与视觉反馈延迟；ImGui 1.92.7 是否已有更合适的机制；最小优化与可靠验收方式。
- Scope: mixed（本地产品/vendor只读；官方 Dear ImGui primary source/issues 只读）
- Date: 2026-10-09

## Findings

### 结论与证据等级

【直接确认】本仓实际 vendored 版本为 1.92.7 / 19270，不是用户推测版本：Inkeys/additional/imgui/imgui.h:32–33。

【直接确认】当前能够消除的结构性延迟是“一次绘制帧”：Setting.cpp:3863 在所有 UI 已经提交后调用 ApplyDragScrolling；Setting.Controls.cpp:227–228 的 SetScrollY 仅设置 window->ScrollTarget。imgui.cpp:11907–11912 是该 setter；imgui.cpp:7924–7931 在目标窗口首次 Begin 中更新 ScrollMax、消费目标并重置 FLT_MAX。因此 frame N 的 MouseDelta 只影响 frame N+1 的正文位置。不是浮点运算慢，也不是同帧 DrawData 会追随新 ScrollTarget。以当前约60Hz调度推断，这个机制约增加一帧反馈等待；不能把该推断当作实测17ms端到端延迟。

【直接确认】官方 1.92.7 头文件的 scrolling 注释明确说明目标在下一次 Begin 消费，并提供 Begin 前 SetNextWindowScroll 的方案。现有内部 pointer setter 若在目标窗口首次 Begin 前执行，亦能同帧消费；官方 UpdateMouseWheel 本身就是在 NewFrame 中对 window 使用 SetScrollY，再由 Begin 正常消费（imgui.cpp:5699–5702、10365–10388）。无需升级 vendor、改 DrawData 顶点或直接事后改 Scroll。

【合理推断】优先优化持续拖动的调用时机与无输入热路径。当前 helper 每帧最多一次，只有固定 ButtonBehavior、hover 检查和至多两个 setter；ApplyDragScrolling 不是每帧扫描所有 g.Windows，只有 active候选及 hovered 的父链。现有代码没有 profiler 证据证明 helper 是主要 CPU 瓶颈。应将“减少一帧提交延迟”和“降低 helper CPU 耗时”分别验证与报告。

用户最新第4点已授权优化旧 helper/部署，这覆盖旧 PRD/spec 的“原正文不可修改”限制。但还需要保留真实控件输入所有权、唯一宿主、边界/释放/弹层/导航屏障行为，不新增消息适配或默认惯性。

### 文件与代码模式

| 文件 | 一行说明与关键位置 |
| --- | --- |
| Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp | 原 helper:214–229，单宿主部署:231–263，当前无输入时仍可能进入 helper；当前 pointer/clip 切换为 frame末模拟旧环境。 |
| Inkeys/Inkeys/UI/Setting/Setting.cpp | backend/NewFrame:2096–2098；全UI后拖动:3863；DX11绘制与 Present(1,0):3873–3888；RenderSettingFrame 加锁 Resume:4100–4104。 |
| Inkeys/Inkeys/UI/Setting/Setting.Design.h | NavigationState、内部 ApplyDragScrolling 接口；可以放 session 局部稳定窗口ID/帧号，避免无所有权裸指针常驻。 |
| Inkeys/additional/imgui/imgui.h | 实际版本:32；scroll timing和公开提前投递建议:494–495；ConfigInputTrickleEventQueue相关IO声明。 |
| Inkeys/additional/imgui/imgui.cpp | NewFrame输入:5519；mouse/hover/move/wheel:5661–5702；NewFramePost:5767；SetScrollY:11907；Begin消费:7930。 |
| Inkeys/additional/imgui/imgui_widgets.cpp | ButtonBehavior:541；ItemHoverable:578；点击 SetKeyOwner/SetActiveID/FocusWindow:623–635；held/release:727–756。 |
| Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp | 共享 FrameInterval=16,666,667ns:52；sleep_until与deadline:736–739；Settings等客户端串行处理与Continue重投递:887。 |
| InkeysHeadlessTests/setting_design_tests.cpp | 真实生产导航/控件/DrawData harness；当前 CheckDragScrolling:2372，旧下一帧消费断言:2452；可以添加实际输入反馈延迟与CPU采样。 |

上述行号为本次只读研究时的工作区版本；并行实施后可能变化。

### 最小优化建议

1. 将“取得滚动所有权”和“已取得所有权后的位移投递”分为两个时机。首次按下应仍在本帧控件提交后确认是否为空白/非交互内容，再保存本session/context的稳定宿主ID；这样 Button、Slider、InputText、Selectable、native scrollbar 已有机会取得 ActiveId/鼠标 key owner。不要在 NewFrame 后先对整个窗口调用 ButtonBehavior 起拖，也不要每帧伪造大 InvisibleButton 抢内容。
2. 持续拖动时，在 ImGui::NewFrame 后、任何产品窗口首次 Begin 前，通过上一帧已确认的唯一宿主提前设置滚动目标。本库内部 pointer SetScrollY 足够与 native wheel同帧生效；如为特定产品宿主包装 Begin，可使用公开 SetNextWindowScroll。不要在 Begin/Render 后直接改 Scroll 或平移顶点，那会让实际命中、子窗口、蓝条和滚动条不同步。
3. 记录已消费的 FrameCount，frame末取得所有权流程不能再消费同一 MouseDelta，避免父子重复滚动或速度翻倍。已有 overlay capture 可优先续接，即使指针离窗；非交互子卡片需由最近真实可滚宿主承担，控件持有输入期间不能通过 fallback 强行滚父页。
4. 提前阶段只能依据上一帧存在且仍合格的捕获宿主。NewFrame会将所有window->Active置false、WasActive=原Active（imgui.cpp:5664–5677），不可直接重用现有eligible的 !Active 判断；用 WasActive/LastFrameActive、稳定ID、所属context/session、打开popup、当前导航屏障、实际range及窗口flags核对。页面切换/隐藏/浮层关闭/显示缩放变化需使捕获失效，不持久保留裸window指针。
5. 首次获取之后，热路径可以只 KeepAlive 自己的ID、验证自身 mouse owner、投递 delta；避免重复 ButtonBehavior/FocusWindow/整棵候选遍历。释放时仅释放自身 overlay所有权，不能 ClearActiveID 清除他人控件。无按下/释放/有效捕获时尽早返回；仅持续捕获也需要处理 stationary held 的 KeepAlive/release，不能因为 MouseDelta=0 丢失所有权。
6. 提前投递 clamp 应由正常 Begin根据实际 ScrollMax 完成；保存浮点累计目标可避免低速/多倍率下每帧 <0.5px被 ImRound64 吞掉造成顿挫，但需要明确与 wheel/keyboard/native scrollbar目标的合并/优先级。若引入累计，须在边界到达、释放、换宿主和外部scroll时重新基准，不能在边界积攒离屏“欠账”导致反向迟滞。与 native ScrollTarget 共存的情况必须测，不要盲目覆盖。
7. 保持 IO.MouseDelta 物理像素一次消费，不能再次乘DPI；不默认增加插值/惯性，指针拖动期间额外tween会增加跟手延迟。宽度/scrollbar显隐动画与内容滚动位置应独立推进。

这是一套可实现的建议，不是未经运行即可宣称通过的方案。初始采集仍放frame末，第一段同时到达的press+movement可能保留采集阶段等待；稳定拖动必须当帧响应。实施应据真实连续帧对照决定是否还要优化首次采集。

### 输入与渲染时序中的其它因素

【直接确认】setting HWND消息线程与渲染线程共用 settingImguiMutex。Setting.cpp:1223–1228 在该锁中调用 ImGui_ImplWin32_WndProcHandlerEx 更新 IO；4100–4104 的同锁覆盖 coroutine Resume，Resume含 NewFrame/UI/Render/Present(1,0)。这意味着消息输入更新可能等待整次绘制/同步Present。共享 RenderPipeline还以16.67ms间隔串行推进多个客户端。上述因素可解释硬件指针比内容更流畅的一部分，但没有本次GUI/perf录制证明锁等待或共享WARP渲染占用多少时间。

不能为了当前需求未经测量改全局FrameInterval、把 Present改0、改设备/交换链、删同步锁、引入独立渲染线程或新消息队列。那会影响共享 immediate-context和其它客户端，超出最小改动边界。若优化调用时机后仍不够流畅，应人工采集现有RenderPipeline诊断中的Settings callback/batch耗时及输入等待；本次headless不能验证DX11/显示驱动/DWM/实机触摸延迟。

【直接确认】默认 ConfigInputTrickleEventQueue=true（imgui.cpp:1637）。UpdateInputEvents:10443–10474 对同一press/release交错、mousewheel混合事件及TouchScreen初次move→press会保留跨帧顺序；连续MousePos在held稳定期会处理到该批最后位置，因此正常高频position本身并非每帧只消费一个。官方 #5545 涉及position/wheel交错积压，不能套用为此次内容拖动一定有积压。不建议全局关闭trickle：它会压平快速press/release丢失点击，违背控件兼容。

【直接确认】ImGui::StartMouseMovingWindow:5234 即使NoMove也设MoveId，UpdateMouseMovingWindowNewFrame:5272在真实MovingWindow存在时每帧FocusWindow；旧helper在子void上可能无法获取overlay而走IsWindowHovered fallback。根窗口是否可move不等于原生HWND是否实际移动（生产每帧SetNextWindowPos({0,0})）。优化时应识别明确的window MoveId而非把任何ActiveId当void，不为了测试方便添加额外root flags遮蔽生产行为。

### 官方参考与版本判断

- [官方 v1.92.7 imgui.h](https://github.com/ocornut/imgui/blob/v1.92.7/imgui.h)：公开 scrolling文档已提供提前Begin投递机制，当前版本就可用。本文对此来源的归纳仅为延迟语义与接口选择，不声称该版本提供完整内容拖动控件。
- [官方 issue #3379](https://github.com/ocornut/imgui/issues/3379)：主题为允许拖动窗口内容，读取时仍Open。vendor imgui_internal.h:1294–1297 的 BgClickFlags仅有Move位，:2711引用此issue；不是成熟scroll手势开关。公开页面未返回评论，不能确认用户旧helper的具体评论作者/日期；旧源码确切历史已记录在既有 settingui2-drag-scroll.md。
- [官方 issue #5545](https://github.com/ocornut/imgui/issues/5545)：讨论mouse位置与wheel事件交错的trickle积压；本文仅用来提示必须依据事件序列诊断，不能以关trickle作为无证据通用优化。
- [官方 v1.92.7 changelog](https://raw.githubusercontent.com/ocornut/imgui/v1.92.7/docs/CHANGELOG.txt)：该版本发布于2026-04-02，包含 InputText、tables、wrapping等调整；本次研究未找到可直接替代整个内容drag算法的新增公开接口。无需以此次性能优化为由默认升级vendor。
- [官方 examples说明](https://github.com/ocornut/imgui/blob/master/docs/EXAMPLES.md)：硬件鼠标cursor与普通图形绘制不共用呈现路径，约60FPS可造成观感差异；仅作人工呈现验收参考，不把headless结果当显示器端反馈。

### 可测验收建议

1. 真实生产主页/正文与nav/flyout/长Combo都覆盖，执行真实AddMousePosEvent/AddMouseButtonEvent/AddMouseSourceEvent→NewFrame→产品渲染→Render；鼠标/TouchScreen各测。相同输入流先测提交 b8beea0b 的真实baseline，或用既有旧helper原实现作为明确标记的baseline路径；不能写数学替身证明新算法。
2. 每个稳定held frame记录IO MouseDelta、宿主Scroll提交前/首次Begin后/Render后、ScrollTarget、DrawData已知实际控件或文本vertex.y、实际ItemRect.y，以及父/子 Scroll。旧baseline应呈现输入frame N的位移到N+1才进入draw；优化后已取得捕获时，frame N在目标首次Begin后即有对应位移，实际顶点和item命中同帧跟随，唯一宿主每帧一次。只有检查ScrollTarget数值不足以证明视觉反馈改善。
3. 连续正向/反向几十帧，在非边界range验证累计位移等于已处理输入（允许原库整数像素量化）；添加每帧0.25/0.4/0.6px小delta序列、多倍率及急反向，验证无慢速丢失、边界滞后、重复消费。stationary held/release/drag离窗与返回、navigation屏障、closed popup、不提交前帧popup分别测。
4. Button/Slider/InputText/native scrollbar held时正文不跟随指针滚动；真实点击业务次数、ActiveId/keyowner保留。outside ImGui mouseDownOwned=false后移入仍不得获取拖动。快速click序列及touch首次无hover事件序列用实际默认trickle处理，验证不丢点击与队列在合理有限帧清空；稳定held高频MousePos批量事件应吸收该批最后位置而非发生渐增积压。
5. CPU采样不要设例如“<50us”这种平台/Debug/runner脆弱阈值。可在现有headless真实harness用steady_clock分开记录函数耗时与整页生成耗时，预热字体/atlas后交替baseline/optimized运行相同已捕获输入，报告median/p95以及真实owner resolve/helper/target写次数。CI主要断言无输入热路径无额外目标写、稳定held一个宿主且一次处理、无额外全window扫描/分配，以及当帧draw响应的结构性指标；绝对时间只作证据记录。不能仅因新代码更短声称CPU提升百分比。
6. 不增加产品每帧日志/堆分配/字符串格式化来测性能。若测试需要counter，内部轻量测试接口或采样scope足够，且不能改变真实算法。已有所有原回归必须继续通过。GUI/DX11/实机手感仍独立人工验收。

### 相关规范

已读取 .trellis/workflow.md、.trellis/spec/native-desktop/index.md、cpp-conventions.md，以及本task research/spec-boundaries.md中的相关UI合同。规范里的原helper不可修改与下一Begin延迟为上一轮合同，最新用户授权允许改善；主会话应在验证后同步最小相关段落。不得在研究角色下改spec/product/test/task metadata。

## Caveats / Not Found

- 研究只读完成；没有进行GUI、Computer Use或实机performance采集，不声称已定位全部端到端延迟，也没有运行代码benchmark。
- gh api只读请求因网络连接失败；官方页面由web读取成功。#3379评论API无法由web打开，页面未返回评论，因此未臆测原helper出处的具体issue评论。
- 未找到1.92.7公开的“一键启用触摸内容拖动”替代API；BgClickFlags的Move位不提供该功能。此结论只限定已核对的版本/API，不保证之后所有release均无新机制。
- 主页遗漏和sidebar间距/显隐动画由另一研究负责，本文件不重复提出页面布局方案。

## Addendum: 导航卡片/行与拖动的最小仲裁（主会话补充范围）

主会话确认最新“所有实际竖直条处按住内容”覆盖主页feature/common整卡和导航Selectable，允许复用现有ImGui拖动阈值进行滚动vs轻点仲裁，Slider/Textbox仍保留原交互。候选登记应局限明确的导航Card/Selectable，不默认把全部普通Button改成可拖动区域。

- 直接证据：Setting.Pages.cpp:146是生产RenderHome，:208的##feature为整卡Button；Controls.cpp:863的##navigate为common整行Button；Shell.cpp:169使用NavItemEx。这些都是已占有ActiveId的点击目标，不能用无条件void fallback抢输入。
- 建议在这些明确提交点登记真实LastItemData.ID及最近有效scroll ancestor ID。按下帧只登记候选，不清item ActiveId，不立即消费delta；轻点释放仍返回原pressed。
- 后续NewFrame若候选ID仍为ActiveId、鼠标按下且MouseDownOwned，使用库现有IsMouseDragging(Left)（imgui.cpp:9970，默认MouseDragThreshold=6px:1660）判断跨阈值。越阈值后才将该候选的ActiveId/keyowner交给滚动overlay，在首次Begin前消费一次该frame位移。该ID转换让原卡按钮release不再产生导航；需要真实业务计数验证，不能仅看ActiveId。
- 保存“候选item ID、宿主ID、候选frame/session、已消费frame”等最小状态足够，不收集整页控件列表；控件仍Active时无需按任何rect重新猜测输入归属。候选失效/换页/关popup/释放应清候选，只有自己的scroll overlay可以释放；不要调用无差别ClearActiveID。
- 第一次跨阈值可只消费该frame的MouseDelta，不补偿整个click-origin距离，避免瞬间跳过deadzone造成突跳；持续慢速位移累计若采用，应处理反向/边界，不能引入人为平滑追随迟延。
- 空白原helper的直接held行为是否同样使用阈值应由实际需求与旧回归决定；导航卡click仲裁复用库阈值不等于必须给所有void再增加等待。
- 需覆盖真实主页feature/common/card padding、nav父/子/leaf、Combo Selectable：轻点只触发一次，越阈值滚动不导航，Slider/Input不被候选登记，拖出宿主仍续接，mouse/touch一致。运行默认trickle输入序列，不关闭队列以规避touch起始帧差异。

此建议已发给 /root/scroll_followup_implement；研究角色没有修改产品、test、spec或task元数据。

## Independent Performance Review: 后续实现草稿（有界只读）

- Review date: 2026-10-09
- Scope: 产品新拖动路径与当前benchmark草稿；不复查动画外观，不运行GUI/benchmark，不修改产品/test/spec。
- Snapshot: 工作区正被实施代理并行修改，以下分别记录初稿与已观察到的修正；不得将此记录替代最终checker和真实run。

### 结构性输入到绘制核对

当前 BeginDragScrolling 在NewFrame后识别已有overlay/允许内容卡候选，保存float position与本帧pending标记；ScrollbarStyleScope在真实窗口Begin前才SetScrollY，并将pending标记置-2。因此不提交/已关闭旧popup不会因为预先写ScrollTarget留下延迟目标，实际提交宿主可由native Begin同帧消费。ApplyDragScrolling在frame末只保持已有capture或获取void的初始capture；原overlayID沿用。结构上能消除持续held输入到正文Begin之间的一帧等待，实际Scroll、DrawData和ItemRect是否同步仍需要最终真实连续帧运行。

第一次读取时 BeginDragScrolling 没有同frame采样guard，多次调用会重复扣MouseDelta。后续已观察新增 ##drag-scroll-sampled-frame，检查==FrameCount后return，第一调用才记录/累加。ScrollbarStyleScope的pending-frame只消费一次，二者职责不同：采样guard防止重复输入累加，scope标记防止重复投递native目标。benchmark绝不能在同一ImGui frame用for循环重复调用新Begin计时，因为后续全部是guard no-op；每个有效sample必须真实独立NewFrame且有明确helddelta/目标写/Scroll变化。

第一次读取时新位置仅以abs(position-Scroll)>1.01重基准。NewFrame原native wheel已能写ScrollTarget，scope随后会覆盖这一目标。已将风险发implement/checker，后续观察实施加入 ScrollTarget.y<FLT_MAX 时优先原目标并标记 ##drag-scroll-rebase，下一frame从实际Scroll归位。还需最终真实held+wheel、显式外部target、小目标变化、nativeScroll以及反向测试，不以代码注释当验证成功。若外部目标是在BeginDrag之后、scope之前设置，也需要scope投递时再检查优先级，不能只检测采样入口。

### 热路径开销与测量范围

- 当前拖动采样沿ActiveIdWindow/最近parent定位，不全扫g.Windows或VtxBuffer；相关scope也没有全顶点扫描。ChildWindow用stack char[512]和ImFormatString/FindWindowByName，不引入std::string堆分配。这里“无新增动态字符串”仅限定新拖动路径，原控件label/文本测量仍有既有字符串/vector，不能推广为整页无分配。
- 候选登记是明确卡片提交点的StateStorage整数写入；新held路径有若干固定literal GetID(hash)及StateStorage查询/写入。window StateStorage第一次新增key可能扩容，steady-state预热后不应持续新增key；benchmark应预热真实字体/atlas/scrollstorage，记录固定有效sample数量。
- 新scope在每个真实滚动宿主处理动画/几何，不能用只测BeginDragScrolling入口的时间代表整条“拖动实现CPU成本”。至少另计ApplyDragScrolling以及actualscope投递阶段，或以同生产渲染总frame作为整路径对照，明确普通UI生成成本一起计入。若报告仅sampler函数，指标名称必须明确，并且不可宣称整体CPU提升。
- 同时维持原函数的latebaseline与优化early路径时，要使用相同生产UI/字体/缩放/flags/动画关闭状态、相同输入数量与位置流、相同hover/ownablevoid/overlay held控制权、相同range和边界分布。baseline既未capture又输入不owned时helper会no-op，或新路径因guard只测no-op，都不是公平held比较。
- 每个sample独立frame，分别idle和sustainedheld；同样warmup，同样真实输入。held期间交替正负位移保留在非边界range，避免某一实现一直clamp导致不同工作量。统计median/p95，有效sample数、实际非zero delta数、宿主目标写数及Scroll变化帧数一起输出。两个实现真实渲染/输入都应运行，不使用数学公式替代baseline。

### 当前benchmark草稿结论

本次读取的 CheckHomeScrollFollowup 仍是 elapsedMicros累加BeginDragScrolling后除全部frames，末行 capturedBeginMeanUs；这混合idle/held/动画显隐帧，且不含真正目标投递与Apply阶段。该值只能作为该harness入口混合成本的原始观察，不能比较优化前后或证明captured path成本。实施代理已明确将其替换/补充legacy与optimized、idle与held独立median/p95；研究时新版尚未写入，未审核或认可不存在的benchmark结果。

其late原生 SetScrollY 对照是在frame末对实际window调用setter，再下一idleframe观察Scroll变化，可以说明setter时序；它不含旧ScrollWhenDraggingOnVoid的真实ButtonBehavior/控制权，不能作为旧完整CPU baseline。CPU比较应保留明确的旧helper真实实现与调用环境，而结构性延迟对照可独立记录。

### Tiny delta、量化和反向

vendor imgui.cpp:10170在UpdateMouseInputs把IO.MousePos用ImFloor变成整数像素，然后MouseDelta由新旧量化坐标相减。真实AddMousePosEvent每次0.25px会积累到0/1pxdelta，不是稳定0.25pxdelta；测试需走真实IO队列，禁止直接改MouseDelta伪造实机低速改善。新floatposition仍可处理边界ScrollMax为小数及未来其它input来源，但不能宣称它改变当前backend采样精度。

Begin消费Scroll时使用ImRound64（imgui_internal.h:529：正数+0.5再截断；imgui.cpp:11784）。当前草稿rebase判定使用ImTrunc(position)与Scroll比较，两者在非整数position不同，可能把库正常四舍五入误当外部变化。建议同库量化后再clamp与真实range比较，或记录上次预期被消费Scroll；最终状态以实施diff为准。这个问题已发实施代理，无实际测试失败结果可报告。

输入反向应立即从当前显示Scroll及保留的亚像素余量继续，不能边界累计离屏欠账。父子只有一个pendingframe/owner，scope不二次消费。测试至少应观察慢速forward/reverse累计位置、抵顶后立即反向、externaltarget后立即反向、无deltaheld续帧以及release。

### 复审限制

本段只读源码与已有harness草稿，没有测得任何CPU时间或端到端百分比改善；不宣称性能测试通过。已将发现分别通知主会话、scroll_followup_implement、scroll_followup_check。最终应依据实际独立frames benchmark来源和准确run日志判断，不引用此草稿作为已完成验收。
