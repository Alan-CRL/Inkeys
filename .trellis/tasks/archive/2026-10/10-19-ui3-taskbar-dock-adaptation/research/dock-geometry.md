# Research: dock geometry and scene animation

- Query: 普通底栏/居中底栏在顶/侧任务栏、底部自动隐藏任务栏升降中的停靠线、描边、显示过渡和PPT既有语义。
- Scope: internal
- Date: 2026-10-19

## Findings

### 需求与现有边界

本报告依据主会话最新分派：顶/侧任务栏时底栏贴屏幕底；底部自动隐藏收起时主体边框距屏幕底5真实屏幕DIP，升起时贴稳定任务栏top；复用类似PPT动画；PPT进入退出语义保持。新task PRD目前为待完善草案。本轮仅读代码，不实施、不编译、不GUI、不commit。

这里的普通底栏是竖向 BottomDocked + 水平 Free；居中底栏是 BottomDocked + Centered。浮动栏不应因任务栏升降被自动吸附。StartupPlacement.BottomLeftFloating/BottomRightFloating是另外的角落浮动启动行为，不应误当普通底栏修改。

### Files found / precise geometry

- `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h:316-338`：唯一基本停靠线 ResolveBarBottomDockLine。若insetDip>0，优先返回monitorBounds.bottom-round(insetDip*dpiScale)，不读取workArea；否则验证workArea完全位于monitor内，仅当workArea.bottom<monitor.bottom返回workArea.bottom，其他返回monitor.bottom。
- `Bar.BottomDock.h:307-314`：ResolveBarBottomDockInsetPixels 用round(insetDip*dpiScale)。5真实DIP在150%DPI为8物理像素，与用户UI倍率无关。
- `Bar.BottomDock.h:340-362`：ResolveBarVisibleBorderBottomScreen = centerY+(bodyHeightDip+strokeWidthDip)*zoom/2；反函数ResolveBarBottomDockCenterScreenY从目标线减去该半高。名义主体80DIP，stroke在轮廓中心线上，两侧各半描边，故必须包含stroke而非简单80*zoom。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:373-381`：CurrentBarBottomDockLine取真实DPI/96 + SceneBottomDockInsetDip调用上述helper。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:443-447`：SceneBottomDockInsetDip在白板或pptDockSceneActive时为5，否则0。不能直接把自动隐藏状态塞进PPT scene active；应保留场景优先级。
- `Inkeys/Inkeys/UI/StartupPlacement.h:34-68`：CenteredBottomBar启动仍调用旧停靠线；可见width/height已包含描边，不包含ULW透明留白。两角浮动以workArea左右底边加24 UI DIP margin并钳制，属既有独立行为。
- `Bar.RenderLoop.cpp:3095-3188`：首次布局和PPT/白板pending布局求完整主体联合宽度，X整体居中；Y用停靠线和主按钮/主栏最大stroke，随后非场景初始布局又用ResolveStartupPlacementBounds覆盖screenCenterY。因此只改CurrentBarBottomDockLine还不够，CenteredBottomBar启动/Preview必须一致更新。
- `Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h:217-227`：InflateAndClipBarWindowRect用于脏区等包络，不是停靠线。
- `Bar.RenderLoop.cpp:9654,10049-10053`：viewport另有2物理像素padding及动画峰值预测；`Bar.BottomDock.h:1273-1304`包络含24DIP弹性外扩。这些是透明HWND/阴影容量，不能算作主体与任务栏的间距，不能移动整个viewport来实现5DIP视觉要求。

### 当前顶/侧、自动隐藏表现

- 顶/侧任务栏通常仅改变workArea.top/left/right，现有helper已返回monitor.bottom；tests已明确覆盖。本轮对顶/侧场景仅补回归覆盖，不为制造改动而重写已正确分支；若其他appbar干扰，作为边界风险记录。
- 底部普通任务栏：当前只知道workArea.bottom，通常正确停在任务栏顶。
- 底部自动隐藏收起：workArea可能等于monitor或只留少量触发条，目前最终贴屏幕底或触发条顶部，没有5真实DIP间距。
- 底部自动隐藏升起：overlay一般不改变workArea，当前不会移至升起任务栏顶。必须有实际edge/rect/显隐状态，不能仅靠厚度、IsWindowVisible或workArea差值。
- 最新Display已有taskbarThicknessPx与2000ms低频采样，但仅厚度不表达升降；任务栏移动而厚度不变会被语义相等抑制。新状态需进入Display semantic equality以及Bar pending/presented环境一致快照。
- `Inkeys/Inkeys/Display/Display.cpp:379-448`采样主/副任务栏，完整矩形用于厚度而非交集；`Display.cpp:668-681`TaskbarCreated和2000ms WM_TIMER；`:992-995`Taskbar刷新复用当前快照。任务栏显隐采样/通知方案由同任务其他研究提供，不在渲染/Move热路径查询Shell。

### UI基准与真实DPI分工（上一任务已落地）

- `Bar.RenderLoop.cpp:1172-1181,1345-1352`：targetDpiScale是真实DPI；targetUiBaseScale来自任务栏尺度；barStyle.dpiZoom保留真实DPI，uiBaseZoom是独立UI基准，最终zoom=uiBaseZoom*configZoom。
- `Bar.BottomDock.h:293-305`：ResolveBarBottomDockInteractionZoom已支持uiBaseScale。主栏拖动阈值/弹性原本按最终UI zoom缩放，不属于本任务要求重定义的对象。
- 新5DIP间距继续用dpiScale（真实DPI），不要用uiBaseZoom/configZoom。主体半高、stroke和横向联合宽度继续乘最终UI zoom。任务栏实际top是物理屏幕坐标，不再乘DPI。

### 显示过渡 / PPT动画 / 触发

- `Bar.RenderLoop.cpp:1162-1370` ApplyDisplayTransition消费PendingDisplaySnapshot；displayCenterX/Y本身保存局部物理像素，再除currentZoom给主按钮逻辑坐标。
- `:1199-1250`：非拖动遇display serial变化且dockLayoutLocked时直接SetDirect目标X/Y和UI基准；普通非dock显示过渡用SetTar(...,0.4)。因此新增任务栏显隐若只走旧display serial，会跳变。
- `:1320-1344`：非拖动使用BarUiAdvanceAnimation推进displayUiBaseScale和displayCenterX/Y，displayTransitionActive联合续帧；拖动期间冻结旧过渡，避免指针下漂移。
- `:1353-1368`：whiteboardDockAnimationActive可抑制稳定贴底；否则已初始放置、无pending、非拖动、BottomDocked时每帧SetDirect Y至停靠线。单独新增SetTar Y会被这段覆盖，必须有独立任务栏过渡保护或清晰统一的停靠过渡状态。
- `:3157-3166`：PPT与白板pending布局共同走displayCenterX/Y.SetTar(...,0.4)，设置displayTransitionActive及whiteboardDockAnimationActive。虽然变量名写whiteboard，PPT也复用。该链已有动画开关/速度语义，适合借用Y属性与推进方式，不需要创建第二套tick/动画线程。
- 新任务栏升降只应改变Y目标，保留普通Free栏的X及Centered水平锁。不要设置pptDockPlacementPending/whiteboardDockPlacementPending去复用：这些会重新整体居中、改方向相关状态，且破坏普通底栏语义。
- 只在解析后的有效dockLine或相应state改变时重新定向；重复采样不能重启0.4秒动画。快速反转从当前已显示Y重新定向，不能重用旧起点。无动画配置需即时到位。

### PPT进退不可改变

- `Bar.BottomDock.h:51-60` ResolveBarPptSceneAction：白板优先Keep；进入时展开且已底栏 -> CenterDock；展开浮动 -> Keep；收起底栏 -> Detach；退出时展开 -> CenterDock，收起浮动 -> Keep。
- `Bar.Main.cpp:415-423` ConsumePptSceneTransition在消费边沿时才切换pptDockSceneActive；同session重复发布不重播场景动作（BarPptSceneState位于BottomDock.h:62-85）。
- `Bar.RenderLoop.cpp:13589-13643`：消费前通过directWindowDragPhase Idle->Absorbing CAS和directWindowDragMutex；拖动中不消费边沿。动作前从最后成功BottomDockPresentedSnapshot重基准；Detach保持已显示位置、CenterDock发布模式/清理旧弹簧并置pptDockPlacementPending。
- PPT/白板期间5真实DIP场景线优先，普通任务栏升降不应覆盖。退出PPT时继续原展开居中逻辑，只把“恢复桌面停靠线”的解析扩展为最新任务栏实际状态。

### 拖动、回弹和Centered边界

- `Bar.Main.cppm:613-618`以及`Bar.RenderLoop.cpp:13708-13710`：dockLayoutLocked = BottomDocked || recoveryActive；它不等于稳定底栏，不可据此无条件抢所有Y所有权。
- `Bar.Interaction.cpp:6804-6820,6859-6874`：拖动environment携monitor/workArea/最终zoom/scene inset/真实dpiScale；tracker以当前完整环境求dockLine。新环境字段或resolvedDockLine必须传播到这里。
- `Bar.Interaction.cpp:6997-7045`：RebasePointerForPresentedEnvironment只消费成功呈现的显示环境来重基准tracker；`:7069-7091`用pending display serial/zoom建立呈现barrier。新任务栏显隐需纳入此屏障，否则渲染已向新线而输入仍用旧线。
- `Bar.BottomDock.h:657-746`：纵向tracker自由捕获、跨越捕获区、脱离恢复共用ResolveBarBottomDockLine；只更新渲染Y会导致拖动仍吸附旧线。
- `Bar.Interaction.cpp:7136-7159`：悬浮Y按monitor bounds钳制，并使用dockCenter判断向下脱离被屏幕阻挡时松手仍保持底栏。须消费同一新停靠线。
- `Bar.RenderLoop.cpp:7640-7705`：抓取态弹簧从真实屏幕抓手与base dock center差值获得；松手/悬浮恢复从最后输入形变量播入；capture-bottom从最后成功Floating像素播入。新任务栏动画应移动base center，不重置这些原果冻弹簧。
- `Bar.RenderLoop.cpp:1647-1663`：Centered且展开时锁展开方向；正常横向布局变化由子几何反推根X。任务栏Y变化不得触发CenterDock动作、清弹簧或重新设置center mode。
- 新过渡遇起拖时应按既有“当前已呈现快照->重基准/屏障”交接；拖动/recovery期间到底延后显隐平移还是按新base连续接管需在design固定一个策略，不能同时有动画与输入两套Y写者。
- `.trellis/spec/native-desktop/rendering-and-ui.md:950-970`严格规定偶数serial完整tuple、CAS写者、deferred barrier、松手待吸收Retry、成功快照唯一依据，任何新环境字段须遵守，而不能实时跨线程读取任务栏状态拼帧。

### 推荐最小变更点

1. 扩展已有Display任务栏快照提供可靠的edge/autoHide/actual visibility/实际top（或稳定解析后的desktopDockLine），纳入semantic equality；跟随既有pending/presented显示环境发布，不在UI另起轮询。
2. 在BottomDock.h扩展纯dock-line策略：场景inset>0优先；top/side或无bar->monitor.bottom；bottom非隐藏/已升起->稳定任务栏top；bottom自动隐藏收起->monitor.bottom-round(5*dpiScale)。无可靠实时信息按有依据的旧workArea回退，避免拿overlay瞬态错误top当目标。
任务栏top采用稳定锚点，不逐帧追逐Shell展开动画中的矩形位置。
3. 同一策略供CurrentBarBottomDockLine、输入tracker/blocked-release、CenteredBottomBar启动/Preview调用；避免把假workArea.bottom塞入完整工作区导致其他弹窗/浮动布局受影响。
4. ApplyDisplayTransition区分“同屏任务栏有效线变化”与跨屏/DPI变化。前者在稳定BottomDocked触发Y过渡，保留X/UI基准/center mode；添加防每帧贴底覆盖的保护并让existing displayTransitionActive继续驱动调度，结束后回归统一稳定线。
5. 场景进入/退出仍原ConsumePptSceneTransition链；场景优先抑制桌面任务栏目标。任务栏事件与PPT同时发生只解析最终优先目标，不先播桌面再播PPT。
6. 不调整透明HWND包络来实现视觉间距。1物理像素底边穿透若涉及hit-test/最终alpha，应另在对应研究中落点；此报告只确认其不得混入主体停靠线或5DIP换算。

### 可复用测试 / 建议增量

- `InkeysHeadlessTests/bar_bottom_dock_tests.cpp:537-573` 已覆盖底/顶/侧/无效workArea、5DIP真实DPI换算、可见stroke底边在1/1.5/1.875缩放精确贴线。扩展为明确任务栏edge/autoHide hidden/visible矩阵、负原点、UI倍率与真实DPI分离。
- 同文件`:36,122,193,234,324,367` 已有水平捕获成功序列、旧帧不可确认capture、成功恢复像素播入、发布CAS所有权、失败保留visual demand、后续输入下首次捕获呈现；应复用新环境切换构造，不能仅加源文本断言。
- `InkeysHeadlessTests/ppt_session_tests.cpp:297-311` 已覆盖所有PPT action矩阵，包括展开浮动进入保持、收起底栏解除、退出展开居中、白板优先。保持这些断言并补PPT期间自动隐藏变化不覆盖场景线。
- `InkeysHeadlessTests/startup_preview_state_tests.cpp:190-225` 现有负原点Preview/formal可见描边一致；新增CenteredBottomBar自动隐藏收起/升起和侧栏场景。
- `InkeysHeadlessTests/bar_display_transition_tests.cpp:25-46` 现有负原点跨屏起始连续，可作为“同屏仅Y线变化不改X、不重启相同目标、反向从current值”的纯策略测试基础；当前它不是生产RenderLoop动画集成测试，不能据此宣称实机动画验证。

### Related specs / external references

已读`.trellis/workflow.md`（上轮会话）、`.trellis/spec/native-desktop/rendering-and-ui.md`（底栏二维所有权与最新UI基准合同）、`.trellis/spec/ppt-interop/native-session-ui3.md`（主栏进退）、`.trellis/spec/native-desktop/startup-preview.md`（可见轮廓/Preview交接）。本报告不提出外部API新合同，无外部检索。

## Caveats / Not Found

- 已有顶部/侧边默认行为正确不代表第三方appbar或特殊Shell正确；新任务应按真实任务栏edge明确优先级。
- 没有找到“自动隐藏升起/收起”的现有字段；当前厚度缓存解决缩放，不解决可见位置。2000ms采样不足以承诺紧跟升降动画，需由Display研究决定事件源与必要采样节奏。
- 无窗口helper回归不能证明Explorer动画中rect、可见边框与overlay时序；本轮按要求不启动GUI，最终必须如实保留实机视觉未验证限制。
