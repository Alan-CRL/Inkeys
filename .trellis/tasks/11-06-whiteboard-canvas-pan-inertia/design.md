# Technical Design

## 总览与所有权

- 由画布（Draw3 绘制线程）持有：平移输入、手势/惯性状态、视口。背景纹理默认由 Freeze 窗口（D2D/WARP）按画布视口重绘（归属开关 `kWhiteboardPatternOwner`，默认 Freeze）；Draw3 合成路径的纹理 pass（§4）作为后备保留，开关为 Draw3 时 Freeze 只画纯色。
- 新增/改写集中在三块，彼此可独立回退：① 纯 CPU 动力学（`Draw3.CanvasNavigation.*`，无窗口可测）；② 输入路由与控制器接入（`Draw3.DrawingController.cpp`、`Draw3.WindowControl.*`、`Draw3.MouseInput.h`、`Draw3.Bridge.h`）；③ 渲染（`Draw3.Renderer*.cpp` + `Whiteboard.cpp`）。
- 依据：`research/move-mode-input-routing.md`、`research/inertia-root-cause.md`、`research/background-pattern-ownership.md`。所有 file:line 以当前工作树为准，实施时复核。

## 1. 门禁与输入路由

- 在 `Draw3.Bridge.h` 增加与 `SelectionUsesAuxiliaryOutput` 同风格的纯判定 `MoveModePansCanvas(selectionMode, workspace) = selectionMode && workspace == Whiteboard`，删除 `kCanvasNavigationProductIntegrationEnabled`（`Draw3.WindowControl.cppm:30`、控制器 `:12153`）。
- RTS 主路径：`processCommand`（`Draw3.DrawingController.cpp:13458-13495`）在 `ContactAdmitted` 与白板加载/演示抑制检查之后、`DiscardSelectionDown` 之前分流：`Selection && MoveModePansCanvas && 设备∈{Touch, MouseLeft}` 且平移持有者类别匹配 → `registerPanContact(handle)`，不进 `initializeStroke`；Pen、MouseRight、另一类设备仍走 `DiscardUntilTerminal`。这条分支不经 `RejectIndependentTouch`/`multiTouchEnabled`（用户的多指书写开关不影响平移）。
- Win32 鼠标兜底：`WindowController` 无 workspace 信息。控制器每帧把 `MoveModePansCanvas` 的结果发布给窗口层（新增原子 `canvasPanMode_`，仿 `SetSelectionMode`），`MouseInputState::Down/Move` 的 `selectionMode` 参数改为 `selectionMode && !canvasPanMode`。`draw3_mouse_input_tests.cpp:105/154-157` 对应更新。该路径触摸被系统提升为单点鼠标，无多指，属已知降级。
- 平移触点集合 `panContacts`：复用 `gestureContacts`/`addGestureContact`/`ContactSnapshot` 序号消费机制；不再经 `CanvasTouchGestureState`（180 ms 窗口、`PanCandidate`、`cancelTouchDrawingForPan`、`retireEndedTouchBeforeDown`、`InertiaBrakeRequested` 在白板移动模式下全部不可达）。绘制模式的 Touch 仍沿现状（以"导航被阻塞"登记，永不进入平移），最终清理见 implement S6。
- 设备互斥：持有者类别（Touch / Mouse）在首个平移触点出队时锁存，另一类 Down 直接丢弃到终态；`SetTouchPanActive` 仅在 Touch 持有时置位（避免鼠标拖动触发"Touch Pan"光标/笔反馈抑制），鼠标持有时保持系统箭头光标。

## 2. 动力学模块（纯 CPU）

沿用模块 `Inkeys.Drawing.Draw3.canvas_navigation`（已被 `Inkeys.vcxproj` 编译；需加入 `InkeysHeadlessTests.vcxproj`）。旧 `CanvasPanMotionState` 估速/释放/候选逻辑整体由下列类型替换；所有常量集中在 `.cppm` 顶部供标定。

**2.1 估速器 `CanvasPanVelocityTracker`（每触点一个）**
- 样本为该触点绝对位置（DIP）+ 到达 QPC；环形缓冲 20；与末样本 ≤2 ms → 合并（保留较新位置与时间）；与末样本间隔 >`max(40 ms, frameDt+16 ms)` → 清窗（手指曾静止）；丢弃比最新样本早 >100 ms 的样本。
- 输出：样本数 ≥3 且跨度 ≥20 ms 时，对 x、y 各做二次最小二乘，取最新样本时刻的一阶导；否则返回"无效"（不是 0，也不用两点差分）。速度等比限幅到 `kVMax = 12000 DIP/s`。
- 非有限输入、倒退/重复 QPC：丢弃并保持状态。
- 加指/减指不清任何估速器，仅重基线中心点位置（见 2.4）。

**2.2 手速 `v_h`**：所有仍按下触点以及 80 ms 内刚抬起触点的有效速度均值；无有效速度返回"未知"。

**2.3 摩擦积分（共用闭式）**
`dv/dt = −v/τ − c·sign(v)`，每帧精确：`A = |v| + cτ`，`|v'| = A·e^{−dt/τ} − cτ`，`Δs = Aτ(1−e^{−dt/τ}) − cτ·dt`，`|v'| < 20 DIP/s` 视为停止（总距离 `d(v0)=v0τ − cτ²·ln(1+v0/(cτ))`）。`dt` 取绘制帧 QPC 间隔，上限 100 ms（仅防休眠恢复）。两组参数：
- 自由摩擦 `(τ_f, c_f) = (0.30 s, 1000 DIP/s²)`；
- 手摩擦 `(τ_h, c_h) = (0.06 s, 6000 DIP/s²)`，作用于相对速度 `w = v_c − v_h`（先积分 `w`，再令 `v_c = v_h + w'`）。
笔 hover 复用手摩擦且 `v_h = 0`。

**2.4 状态机 `CanvasPanDynamics`**
状态：`Idle`、`Coasting`（无手，自由摩擦）、`Slipping`（有手、尚未抓住）、`Locked`（有手、1:1 跟手）。输入：每帧 `{dt, 触点快照集合, penHover, penContact}`；输出：`contentDelta`（DIP）、`contentVelocity`（供 tile 预测）、`phase`。
- 按下：`|v_c| < 20` → `Locked`；否则 → `Slipping`（触点记基线，位置不再随手直接位移）。
- `Slipping`：位置只积分 `v_c`。手速有效 → 对 `w=v_c−v_h` 做手摩擦积分；手速未知且触点位移 <4 DIP（静止证据）且按下已 ≥30 ms（`kHandStationaryDelay`）→ `v_h = 0`；手速未知但已移动 → 暂不耦合（只受自由摩擦）。`|w| < 20` → 在当前中心点重基线并转 `Locked`。
- `Locked`：`contentDelta = 中心点增量`；触点数变化只重基线中心点（画布不跳）；手速来自 2.1/2.2。
- 抬起：全部手离开时，`Locked` → 释放速度 = `v_h × gate(age)`，`age = 最后 Up 的 QPC − 各触点最后样本 QPC 的最大值`，`gate = 1 (≤30 ms) → 线性 → 0 (≥70 ms)`；`|v| < 250 DIP/s` 不起滑，否则 `Coasting`；`Slipping` → 保持当前 `v_c` 进入 `Coasting`。Cancelled/倒退时间 → 无惯性。轻点（无 Move）→ 不复活旧速度。
- `Coasting`：自由摩擦积分；`penHover` → 手摩擦且 `v_h=0`；`penContact`、离开移动模式、翻页、工作区切换、加载、最小化、Resize → `Stop`（`motion={}`）。
- 视口硬限位沿用现有 double 候选值比较与单轴清零；触限轴 `v_c`、估速器该轴清零。
- 速度同时被 `kVMax` 限幅，故同向连续甩动逐次增长但封顶（AC2）。

## 3. 控制器接入

- `updateCanvasNavigation`（`:13523-13910`）改为：读取笔 mailbox（保留现有 `suppressPenUntilRelease`/`interruptNavigationForPenOrMouse` 的笔语义；Mouse-left 在移动模式不再触发 `interruptNavigationForPenOrMouse("drawing-contact")`，见 `:12136-12141`）→ 消费 `panContacts` 快照喂给估速器 → `CanvasPanDynamics::Step` → 像素换算 → 现有的视口应用块（`ApplyCanvasContentTranslationChecked`、`SetViewport`、`InvalidateRaster`、`markPresentationMutation`、`viewportRefreshPending`、`DiscardHotPreimages`，`:13807-13843`）原样复用。
- 单位：触点位置为窗口客户区物理像素（RTS:983-984；进程 Per-Monitor DPI aware），视口也是像素。动力学在 DIP 内运算：入口用 `SpeedEraser::DisplayScale.dipPerPixelX/Y`（Host.cpp:1635-1636）把位置换成 DIP，出口把 `contentDelta` 乘回像素。无显示缩放信息时回退 1。
- 模式/状态钩子：`SetSelectionMode` 由真→假（离开移动模式）、`SetWorkspace`、翻页命令（已有 `interruptNavigation`，`:16122`）、`whiteboardLoading`、最小化、Resize（spec 声称终止但 `:16704-16756` 未调用，补上）统一走新的 `stopCanvasNavigation(reason)`；`sealPresentationContacts`（`:13364-13419`）保持清零。
- 高精度计时器：`timerPeriod.SetSelectionMode(selectionMode)`（`:16534`）改为"Selection 且无平移/惯性活动"才释放；`navigationInProgress`/活动帧判定（`:16788-16790`、`:17157-17160`）改为读取 `dynamics.phase != Idle`。
- 预测 tile：`ComputeCanvasPredictionOffset` 改读 `contentVelocity`（已封顶 12000，预取带最大 1800 DIP，原 3600）。
- 光标/触觉：移动模式现状不变（鼠标箭头、触摸/笔隐藏、连续触觉禁用）；不新增抓手光标。

## 4. 背景纹理（Draw3 合成路径，方案 A）

> 2026-11-06 修订：用户明确原需求并未要求用 HLSL，纹理默认由 Freeze 窗口（D2D/WARP）按画布视口重绘（不占用画布 GPU）。本节描述的 Draw3 合成路径已实现（S5）并保留为后备，由编译期开关 `kWhiteboardPatternOwner` 选择归属方，默认 Freeze。Freeze 归属的设计与实施见 `research/freeze-pattern-follow-design.md` 与 implement.md S5′。若真机体验不佳，再切换到 Drawpad（Draw3）窗口承担。

- 插入点：`CompositeLayersToBackBuffer`（`:10377-10390`，调用点 `:10464/:10506/:18357`）与平移分支（`:18343-18356`）在 `ApplyOperatorLayers` 与 `DrawDebugPlateUnder` 之后各加一次"纹理 under pass"，建议合并为同一 helper。必须是 under（`SrcBlend=INV_DEST_ALPHA, DestBlend=ONE`，先例 `Draw3.RendererDebugOverlay.cpp:115-125/232-244`），否则橡皮/L0/L1 的 `Retain*Dest` 会把已写入的纹理擦掉、快照 under 会被遮蔽。
- under blend 非幂等：同一像素画两次 alpha 会由 0.18 变 0.33。pass 必须 scissor 到本帧由 L2 重新拷贝的矩形（`frameDirty`），并以离屏测试断言"连续多帧 alpha 不累积"。
- 状态可达性：`activeWorkspace`/`whiteboardThemes` 是 `Run()` 局部变量而 composite 是成员函数；每帧把 `{enabled=(whiteboard && pattern≠Blank), kind, 图案色, stepPx=24×dpiScale, phase}` 作为参数结构传入。`phase = ((W/2 − viewport.x) mod step, (H/2 − viewport.y) mod step)`，用 double 做 `fmod`（viewport 单位是像素，±1048576 超出 float 精度）；(0,0) 时与原 Freeze（以屏幕中心为相位，`Whiteboard.Theme.h:26-28`）一致。图案色沿用 `IsLightWhiteboardBackground`（>128000 → 黑，否则白，α=0.18，预乘）。
- 实现（R1，不改 HLSL）：复用 shape 16——点＝零长度 SolidLine（半线宽 1 DIP 圆点），网格＝贯穿屏幕的长 SolidLine（线宽 1 DIP）；经 `DrawShapePrimitives`（`Draw3.RendererPrimitives.cpp:161-224`，先例 `DrawDebugMarkerShapes`）画到 backbuffer；新增 under blend state 与可选 scissor raster（已有 `laserScissorRasterState`，`Draw3.Renderer.cpp:686-696`）。4K 点阵约 14.6k/6.5k/3.7k 个 primitive（100%/150%/200% DPI），单批容量 100k。若实测帧耗时不达标，退到 R2（新 HLSL shape ≥23，需按 `draw3-cursors.md:74` 同步 Win7 guarded 门限并重生成 `Draw3.Win7GuardedShader.h`）。
- 触发：主题命令后补 `forceFullPresent`（`:16016-16022`）；以 `appliedPatternKey` 记录上次参数，变化即整屏重合成。进入白板/换页/Resize/DPI 已经全屏。
- 门控：必须 `activeWorkspace == Whiteboard && pattern ≠ Blank`，否则破坏桌面/PPT"空画布真透明"与 `SelectionUlw` 零 alpha 校验（`:10453/10458`、`TransparentPresentation.cpp:143-247`、`:18475-18485`）。白板空页同样要画纹理，须排查所有"画布为空 → 清屏/提交透明"的捷径。
- 不泄漏：纹理只写 backbuffer，backbuffer 从不作数据源；L2、历史、可信快照、热前像、composition cache 均不含纹理（`research/background-pattern-ownership.md` §2.3）。
- Freeze：`Whiteboard.cpp:130` 只画纯色；`:689` 的内容失效判断只比较 RGB，避免只改纹理时整屏重绘并取消候选；`DrawThemePattern` 保留给主栏预览（`Bar.Scene.cpp:810`），`DrawTheme` 无调用方后清理。
- 原子性：纹理与墨迹同线程同帧同 viewport；纯色仍由 Freeze 异步提交（≥50 ms）。主题/翻页时纹理与墨迹原子、与纯色短暂错配，性质与现状"墨迹对底色"相同。

## 5. 每页视角

- 在 `Run()` 内与 `whiteboardPages` 并列新增 `std::vector<InkViewport> whiteboardViewports`（随页数扩容，新页默认 (0,0)，不继承）。视口应用块与翻页前把当前页视口写入；`EvictWhiteboardPage`（`:571`）不触碰该表；冷页加载完成后在 `ReplacePage`/加载完成路径把表中视口写回新 canvas（`SetViewport` 校验失败则回退 (0,0)）。
- 不改 UInk、不改 `revision`/dirty；仅会话内有效（新进程是新白板）。UInk 导出仍带保存时刻的视口，加载以侧表优先。

## 6. 兼容与回退

- 回退点：S1 纯 CPU 模块独立；S3 用 `MoveModePansCanvas` 恒为 false 即可整体关闭输入；S5 渲染 pass 与 Freeze 改动同提交，回退时同时恢复。
- 不改 UInk 格式、`InkHistoryRasterKey`、HLSL（R1）。桌面/PPT 行为字节不变。
- 现有 spec 契约冲突点需同步改写：`runtime-and-rendering.md` L869-887（门禁、180 ms、"绝不叠加"、残余候选、6000/12000 减速）、L898-914（验证矩阵与 Good/Bad）、L918（Tests）；`whiteboard.md:33`、`rendering-and-ui.md:1758`（平移缩放仍禁用）；`cpu-gpu-contracts.md`（新 under pass）。

## 7. 测试策略

- 纯 CPU：确定性 LCG 流（对应 `research/sim/streams.py`）覆盖估速器、拓扑、闭式惯性、状态机转移、释放门限、封顶、非法输入；先为现有行为写特征化测试，替换后删除。清单见 `research/inertia-root-cause.md` §4.7，并新增耦合用例（按住/同向加速/反向/抬起于 Slipping/轻点<30 ms/多指）。
- 生产探针：更新 `RunDraw3ControlFenceProductionProbe`（`:5910`、`6021-6093`）使白板 Selection 下 Touch/MouseLeft 进入平移而 Pen/MouseRight 仍被丢弃，桌面/PPT 不变；`OnTouchDown` 调用方（`:5254/5279/5330/5427/5489/5518`）随 S6 更新。
- 渲染：离屏 WARP 验证（仿 `RunCanvasDebugOverlayValidation`）：与 D2D 参考逐像素对比（相位 0、两种背景明暗、点/网格、DPI）、alpha 不累积、相位随视口按公式位移、门控、不进 L2/快照；既有 `whiteboard_ui_tests.cpp`/`draw3_bridge_tests.cpp` 无像素断言，需补 Freeze 纯色断言。
- 视角：`EvictWhiteboardPage` + 冷页加载路径的探针（仿 `:7437`）。
- 未覆盖须如实记录：真机触摸/鼠标手感与常量标定、DComp 与 ULW 回退的视觉、4K 帧耗时、Win7 WARP。

## 8. 风险

- `Draw3.DrawingController.cpp` 体量巨大且状态耦合重，导航与绘制模式 Touch 记账交织；先让新路径旁路旧 FSM，再做 S6 清理。
- 纹理 pass 4K 帧耗时、under 非幂等、"空页捷径"漏网；RTS 鼠标拖出窗口行为与 contactId 规律未验证。
- 摩擦常量、30 ms 轻点阈值、释放门限 30/70 ms 无真机日志，须保留诊断（沿用显式 RTS diagnostics 下的 `LogCanvasPan`，正常模式不输出）并预留标定入口。
- Win32 兜底降级为单点；Selection 释放 1 ms 计时器的修正若遗漏会让平移节拍抖动。
