# Research: UI3 centered bar dynamic centering

- Query: 追踪 UI3 居中底栏在 PPT/白板进入退出时主栏宽度与竖直位置并行变化的水平偏心直接原因、继承/布局关系、历史演变、复现与修复边界。
- Scope: internal（当前生产代码、测试、git 与 Trellis 历史）；无外部资料需求。
- Date: 2026-10-25
- Baseline: HEAD `fdda877d1d65dae877ee46cd081f36940eea23f6`（`chore: record journal`）；`Inkeys` / `InkeysHeadlessTests` 路径的最近产品提交为 `dffdd2acb7e470c2ce4e54b938ce0531e5cd14c5`。`git diff -- Inkeys InkeysHeadlessTests` 为空；行号对应该工作区。
- Status: 静态调查完成，待主会话做运行验证。历史设计不代表现状。

## Findings

### Files Found

| File Path | Description |
|---|---|
| `Inkeys/IdtMain.cpp` | 无条件启动 UI3 Bar；离屏命令入口 |
| `Inkeys/Inkeys.vcxproj` | 当前参与编译的生产实现；旧 UI2 仅 `None` |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm` | 公共接口、显示/成功呈现快照、离屏协调器接口 |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` | PPT/Whiteboard 场景发布、场景显示器与 dock inset |
| `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp` | 显示快照选择、monitor/workArea/DPI 同事务发布 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 场景交接、布局目标、动画采样、根节点反推、viewport、ULW 的实际主实现 |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | 工作区相关的按钮显隐/尺寸与主栏实际目标宽度 |
| `Inkeys/Inkeys/UI/Bar/Bar.UI.cppm`、`Bar.UI.cpp` | `x/y/w/h` 当前值与 `inhX/inhY` 实际继承坐标的关系 |
| `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h` | dock、workspace、居中根节点及范围纯函数 |
| `Inkeys/Inkeys/UI/Bar/Bar.Animation.cppm`、`Bar.Animation.cpp` | 独立 `BarUiValueClass` 目标、曲线、`SetDirect` 与采样语义 |
| `Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h` | capacity/viewport 与屏幕/layout 坐标转换 |
| `InkeysHeadlessTests/bar_bottom_dock_tests.cpp` | 现有居中/dock helper 回归；包含当前门禁预期 |
| `InkeysHeadlessTests/bar_display_transition_tests.cpp` | 显示过渡与任务栏 Y 所有权测试，不覆盖场景逐帧居中 |
| `Inkeys/Inkeys/UI/Bar/Bar.MultiTarget.Test.cpp` | 可复用的真实协调器离屏 fixture 模式 |

## 1. 当前生产实现与实际调用/数据流

### 1.1 唯一生产路线与接口

- `wWinMain` 直接 `import Inkeys.UI.Bar` 并无条件调用 `Inkeys::UI::Bar::Initialization(startupPlacement)`：`Inkeys/IdtMain.cpp:16,3644-3650`。
- 主工程编译 `Bar.Main.cpp/.cppm`、`Bar.RenderLoop.cpp`、`Bar.State.cpp` 等：`Inkeys/Inkeys.vcxproj:1009-1019`；`Bar.BottomDock.h` 登记于 `:426`。旧 `IdtFloating.cpp/.h`、`IdtWindow.cpp/.h` 仅为 `None`：同文件 `:1186-1190`，不是本次生产路径。
- 场景公开接口位于 `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm:1231-1254`；离屏生产协调器接口 `CreateBarOffscreenRenderCallback` 位于 `:1208-1211`，实现于 `Bar.RenderLoop.cpp:13142-13153`。

### 1.2 PPT / Whiteboard 到场景放置

实际数据流：

1. PPT UI 在 `Inkeys/Inkeys/UI/Ppt/Ppt.cpp:365-370` 调 `Bar::PublishPptSession(session, active, showWindow)`。后者同时发布 `pptPresentationActive`、窗口与 `BarPptSceneState`，再唤醒 Bar：`Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:400-424`。
2. Whiteboard 工作区在 `Inkeys/IdtState.cpp:2574-2588` 调 `SetWhiteboardActive(true)`，退出/恢复路径在 `:2751-2759` 调 false。该接口只发布业务事实、收起辅助面板并唤醒渲染，不再直接强制居中/折叠：`Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:503-518`。
3. 渲染帧在无拖动、取得 direct-window 所有权后消费 PPT/Whiteboard 边沿，并以 `workspaceScene.Update(WhiteboardActive(), PptPresentationActive())` 分类 `Enter / Switch / Leave / Keep`：`Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:12902-12914`；状态分类本身在 `Bar.BottomDock.h:51-77`。
4. 对已有展开 BottomDocked 主栏，`Enter` 和 `Switch` 选择场景显示器并置 `pptDockPlacementPending=true`：`Bar.RenderLoop.cpp:12920-12958`。`ShouldPlaceBarWorkspaceDock` 只检查“展开 + BottomDocked”，不要求 Centered：`Bar.BottomDock.h:54-57`。
5. 同帧主栏表现先根据实时 PPT/Whiteboard 状态投影按钮：`Bar.RenderLoop.cpp:13087-13092,1651-1662`；`ApplyMainPresentationLocked` 使用这两个状态，改变 Freeze/EndShow/Whiteboard/Pierce 的显隐和尺寸：`Inkeys/Inkeys/UI/Bar/Bar.Button.cpp:956-974,1002-1019`。例如 PPT 隐藏 Freeze、显示 EndShow，Whiteboard 又走另一投影：`Inkeys/Inkeys/UI/Bar/Bar.A2.h:38-52`。这些不是假想宽度变化，而是主栏目标成员的真实变化。

### 1.3 主栏实际宽度、主按钮边界与继承

- `ResolveBarButtonListLayout(frame.mainButtons, Main)` 从当前成员尺寸/可见性生成 `layoutTotalWidth`：`Bar.RenderLoop.cpp:1710-1712`；实际列表算法逐项放置并计算总宽：`Inkeys/Inkeys/UI/Bar/Bar.Button.cpp:2349-2356,2418-2469`。
- 展开时 `mainBar->w.SetTar(layoutTotalWidth, ...)`；`mainBar->x` 的目标为 `±(mainButton.GetW()/2 + mainBar->w.tar/2 + 10)`：`Bar.RenderLoop.cpp:2865-2902`。`GetW()` 是目标值 `w.tar`，不是当前 `w.val`：`Inkeys/Inkeys/UI/Bar/Bar.UI.cppm:193-196`。
- 图元的 `x/y/w/h.val` 是本帧动画当前值；`Inherit(Center, parent)` 将子中心放到“父中心 + 子 x/y 偏移”，写回实际绘制左上角 `inhX/inhY`：`Bar.UI.cppm:129-177`、`Bar.UI.cpp:33-57`。所以继承只负责父子坐标传递，不知道屏幕中心，也不会自动让联合外框居中。
- 普通按钮再继承 MainBar：`Bar.RenderLoop.cpp:11106-11110`；主栏继承 MainButton：`:5592-5604`。稳定居中反推发生于下游 Popup/绝对几何派生之前，符合既有所有权。
- 居中对象不是主按钮中心，也不是 MainBar 中心，而是二者及可见描边的联合外框。`ResolveBarBottomDockCenteredRootPlacement` 以主按钮可见宽、主栏中心偏移和主栏可见宽计算 union 左右端，再反推根 X：`Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h:889-917`。

忽略描边、右展时，若主按钮中心为 `M`，主按钮宽为 `B`，主栏宽为 `W`，则主栏相对中心为 `B/2 + W/2 + 10`；联合中心约为 `M + (10 + W)/2`。因此固定 `M` 或只按最终 `W` 设置 `M`，在 `W` 动画期间一般不能保证联合中心不变。左展镜像同理。

### 1.4 帧采样顺序：同一 dt，不同动画所有权

`RenderFrame` 的当前顺序可由以下行号直接确认：

1. `WakeAndSnapshot` 从 `FrameAnimationClock.Tick(frame.frameTime)` 取得本帧唯一 `animationDtSeconds`：`Bar.RenderLoop.cpp:1129-1168`。
2. 场景交接后，`ApplyDisplayTransition` 用该 dt 分别推进 `displayUiBaseScale / displayCenterX / displayCenterY`，再执行 `mainButton->x/y.SetDirect(displayCenter*.val/currentZoom)`：`:1413-1425,1439-1469`。
3. `SubmitTargetsAndLayout` 提交主栏和按钮目标；场景 pending 在完整目标宽度已知后计算最终位置，并对 X/Y 都 `SetTar(..., 0.4)`，设置 `displayTransitionActive=true` 与 `whiteboardDockAnimationActive=true`：`:2763-2767,2792-2806,2825-2838`。局部 `whiteboardDockPlacement` 明确是 `whiteboardDockPlacementPending || pptDockPlacementPending`（`:2763-2764`），所以该 0.4 秒路径同时覆盖 PPT 与 Whiteboard，不能按变量名误认为只覆盖白板。
4. `AdvanceAnimationsAndDeriveLayout` 用同一个 dt 推进 Shape、MainButton、普通按钮及其内容：`:5014-5027,5229-5248,5496-5532`。主栏时间轴在属性推进后才 Advance：`:7371-7387`。
5. 随后计算 dock 映射、继承、dirty/viewport，并提交 ULW。

因此没有证据表明“宽度和 Y 使用不同 dt”是直接原因。它们使用同一帧 dt，但属于不同的 `BarUiValueClass`，各自有 `startV/tar/progress/dur/activeCurve`：`Bar.Animation.cppm:773-838`；推进按各自进度和曲线求值：`Bar.Animation.cpp:119-180`。固定时长相同不等于每一帧满足相同的几何不变量。

### 1.5 稳定居中根节点的实际作用与门禁

在主按钮、主栏及按钮动画已推进后，当前代码调用：

- `mainButton->w.val + mainButton->ft.val`；
- `mainBar->x.val`；
- `mainBar->w.val + mainBar->ft.val`；
- 当前显示器中心（换算到 monitor-local/layout DIP）。

然后反推 `mainButton->x`，同步 `displayCenterX`，重新执行 `MainBar.Inherit(Center, MainButton)`：`Bar.RenderLoop.cpp:5537-5588`。

但 `ShouldDeriveBarBottomDockCenteredRoot` 要求：BottomDocked、Centered、Stable、未拖动、展开、无中心/远端弹簧，且 **`!displayTransitionActive`**：`Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h:875-887`；实际调用在 `Bar.RenderLoop.cpp:5554-5559`。现有测试明确把 display transition active 时的 false 结果作为通过条件：`InkeysHeadlessTests/bar_bottom_dock_tests.cpp:1197-1201`。

### 1.6 屏幕与 workArea 基准

- 显示发布优先 `PptSceneMonitor()` 指定的屏，找不到再 Primary；同一 serial 发布 `bounds/workArea/DPI/uiBaseScale/desktopDockLine`：`Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp:78-119`。PPT 使用放映窗口屏，Whiteboard 使用主屏：`Bar.RenderLoop.cpp:12952-12957`；`Bar.Main.cpp:441-449`。
- 水平中心始终来自完整 `monitorBounds` 的 `(left+right)/2`，不是 workArea、HWND 或 viewport：`Bar.BottomDock.h:478-483`；根反推使用该值并扣除 monitor origin/direct translation/zoom：`Bar.RenderLoop.cpp:5565-5574`。
- Y 的 dock 线是另一基准：场景 inset>0 时取 `monitor.bottom - round(5 * dpiScale)`；桌面优先规范化的 desktop dock line，再回退合法 workArea.bottom / monitor.bottom：`Bar.BottomDock.h:334-370`。`CurrentBarBottomDockLine` 通过 `SceneBottomDockInsetDip()` 选择：`Bar.RenderLoop.cpp:377-385`；场景是否应用 inset 由 `Bar.Main.cpp:451-455` 决定。
- 所以“水平以所在屏幕中心、竖直以场景底边/workArea”是现状的两套正交基准，不能把 workArea 水平中心误当目标。

### 1.7 窗口位置与内容位置提交

- 根节点之后的 `frameAnchor` 由当前 MainButton 当前值和 frameZoom 得出，capacity 围绕它：`Bar.RenderLoop.cpp:7587-7593,7630-7651`；实际 capacity 再由 `ResolveBarWindowCapacity` 处理：`:8833-8842`。
- D2D 使用 `-capacityOrigin` 变换，body/grip 另应用 dock 映射：`:9456-9483`。
- viewport 预测也调用同一个 `ShouldDerive...`，并用 `ResolveBarBottomDockCenteredRootRange` 从主栏 `x/w` 动画范围反推根 X range：`:9039-9084`。**实际反推与 viewport 预测共有 displayTransition 门禁，是后续修复必须一起处理的边界。**
- `candidateSource = candidateViewport - capacityOrigin`：`:9376-9389`；`presentedDestination = monitorOrigin + candidateViewport + directTranslation`：`:12288-12307`。
- 同一个 `UpdateLayeredWindowIndirect` 调用同时收到 `pptDst/psize/pptSrc/prcDirty`：`:12319-12327`。场景交接只在无拖动时运行：`:12903-12906`，故场景就位不是交互线程独立 `SetWindowPos` 推动。
- 只有 GetDC、ULW、ReleaseDC、EndDraw 全成功才提交 dirty/viewport/mapping/命中快照：`:12410-12418,12594-12602`。成功快照的 `visualBodyCenterScreenX` 用主按钮和主栏**实际水平映射后的联合边界**计算，含 monitor origin、zoom 和 direct translation：`:12551-12568`；其 DTO 位于 `Bar.Main.cppm:531-567`。

静态上没有发现“场景窗口位置和内容分别由两次独立 API 提交”这一直接原因。ULW/DWM 的真实光学结果仍需运行验证，但不能用它替代已经确定的几何门禁缺陷。

## 2. 根因：静态确定与推断分开

### 2.1 静态可确定的直接原因（Enter 与 Switch）

对 **Desktop→PPT、Desktop→Whiteboard、PPT↔Whiteboard** 的展开居中 BottomDocked 路径，下列条件同时成立：

1. 场景建立 `pptDockPlacementPending`：`Bar.RenderLoop.cpp:12920-12958`。
2. 场景就位对 X/Y 建立 0.4 秒动画，并开启 `displayTransitionActive`：`:2829-2838`。
3. `displayTransitionActive` 明确禁止逐帧居中根反推：`Bar.BottomDock.h:875-887`、`Bar.RenderLoop.cpp:5554-5559`。
4. 此时 MainButton 根位置只由 `displayCenterX/Y` 写回：`:1413-1425,1467-1469`；MainBar 的 `x/w` 同时按另一组目标/曲线推进：`:2865-2902,5229-5248`。
5. 继承仅跟随该根，不会再按屏幕中心校正：`Bar.UI.cpp:45-57`。

因此场景中间帧的联合中心在代码设计上就没有不变量保障；只有 display transition 结束后，稳定根反推才重新接管。这直接解释“宽度与竖直移动同时发生时偏心、最后又恢复居中”，无需假设掉帧、UI2、D2D 失败或固定宽度误读。

场景初置还用**目标** `GetW()/totalWidth` 计算最终根位置：`Bar.RenderLoop.cpp:2768-2806`。所以中间帧既可能受独立 X 插值与当前宽度不一致影响，也可能表现为“根已趋向最终位置而当前宽度尚未到达”的偏心。Y 的 0.4 秒活动本身足以让根反推在整个过程中关闭，即使 X 恰好不变。

### 2.2 各转场不能混为一谈

| 场景 | 当前代码路径 | 静态结论 |
|---|---|---|
| Desktop→PPT / Whiteboard（展开 BottomDocked） | `Enter` 捕获桌面基线并 `pptDockPlacementPending`：`:12920-12931,12952-12958`；随后 0.4s X/Y | **确定存在 2.1 的根反推空窗** |
| PPT↔Whiteboard | `Switch` 不覆盖桌面基线，仍设置同一 pending：`:12932-12958` | **确定存在同一空窗**；“退出 PPT 去白板”也属于此类，不是 Leave |
| 最后一个工作区→Desktop | `Leave` 恢复 dock/center/fold/side，设置 `desktopRestorePending`：`:12934-12950`；在 `ApplyDisplayTransition` 后直接恢复桌面 X/Y 并清 `displayTransitionActive`：`:13053-13071` | **不同的现有路径**。随后稳定根反推在本帧主栏动画后仍可运行；不能未经复现把它与 Enter/Switch 同因处理 |
| Whiteboard 最小化/恢复 | 修改 center mode 并再次 `pptDockPlacementPending`：`:12966-12989` | 与场景放置共用门禁，列入回归矩阵 |
| 普通显示器/DPI 变化 | `ApplyDisplayTransition` 可用 0.4s displayCenter 插值：`:1220-1287` | 也排除根反推，但不属于本次已确认症状；不应为修场景而盲删全局门禁 |
| Free / Floating / folded | `ShouldPlaceBarWorkspaceDock` 不安排展开 dock，或根门禁不允许 | 不应被“始终居中”修复误改 |

Leave 路径是本次调查的关键反差：当前静态代码并未显示与 Enter 相同的必然根反推空窗。用户报告中的“退出”必须由逐帧运行记录区分究竟是 Switch、最终 Leave、快速反向，还是另有呈现问题。

### 2.3 可以排除为“唯一直接原因”的候选

- **UI3 继承本身**：继承只是坐标传递；真正决定根位置的门禁在 RenderLoop，不是继承引擎自动错误居中。
- **固定宽度/静态子元素位置作为现状唯一算法**：稳定路径已经读取当前 `w.val/x.val/ft.val`；问题是该路径在场景中被禁用。
- **宽度与 Y 的 dt 不同**：两者共享 `frame.animationDtSeconds`；差异是独立动画目标/曲线与根所有权，不是采样时钟分叉。
- **窗口位置和内容必然分两 API 提交**：本路径同次 ULW 提交目的位置、尺寸和 source；没有证据把它定为直接原因。
- **09-22 的动画性能问题**：50ms clamp、光照和共享调度会改变帧时间/观感，但不能解释稳定可推导的“场景期间根反推被关闭”。历史报告本身也要求不把候选写成实机根因。

### 2.4 仍属推断或待验证

- 2.1 是代码级确定性机制，但它在用户机器上的像素幅度、每条操作序列是否都触发、是否还有额外 ULW/DWM 因素，需要真实记录。
- 最终 Leave 是否仍存在用户可见偏心，当前静态证据不足；不能用 Enter 的根因直接宣告所有退出路径同因。
- 快速反向、跨屏、残余 center spring/far-edge flag 是否再产生首帧例外，需实际帧样本。
- 未执行 GUI、Office/WPS、真实 ULW 或构建；本报告不声称已复现用户现场。

## 3. 关键 git 与 Trellis 历史证据

### 3.1 设计演变

| 证据 | 历史设计/修改 | 与现状关系 |
|---|---|---|
| `eaedf9c5`（2026-08-15，`feat(ui3): add elastic bottom dock for bar`）及 `.trellis/tasks/08-14-ui3-bar-bottom-dock-elastic/` | 启动时按主按钮+展开主栏联合宽度居中；渲染线程负责弹簧/布局/ULW，显示快照含 workArea；见该任务 `prd.md:9-20`、`design.md:28-37` | 建立 dock/坐标合同；当时并未覆盖后来 PPT/WB 场景的逐帧根所有权 |
| `6c9a20ec`（2026-08-21，`feat: add bottom dock center snapping`）与 `.trellis/tasks/08-20-ui3-bar-bottom-dock-feedback/` | 引入正交 Free/Centered；任务 R3 要求非拖动居中展开时逐帧联合中心，当时方案是视觉 correction + 稳定后 rebase，见 `prd.md:35-45`、`design.md:30-46` | 是历史方案，不能直接当现状；后续已被根节点方案替换 |
| `595121fb`（2026-08-23，`fix: stabilize centered dock layout transactions`） | 加入两阶段 centered rebase 和门禁 | 属于被后续否定的多阶段方案 |
| `07849596`（2026-08-23，`fix: derive centered bar root from animated geometry`） | 删除 correction/rebase，改为稳定态从当前动画几何直接反推 MainButton 根；同时明确“显示切换/白板放置”由原状态机持有 | 当前 helper 的直接来源。`git blame` 显示 `ShouldDerive` 及 `!displayTransitionActive` 均来自此提交；其历史 `08-23.../research/root-cause.md` 还记录此前修复经真实设备否定，不能再度宣称旧 correction 是答案 |
| `10ea1e0d`（2026-09-25，`fix(ppt): synchronize UI3 layout, slideshow scenes and page transactions`） | 引入 PPT scene pending，进入/真退出时 CenterDock，并复用 displayCenter + 既有 centered-root gate | git 历史中的 `09-25.../research/bar-check.md` 已写“CenterDock placement uses ... standard centered-root gate”，但只把生产集成列为待验证；场景逐帧中心没有形成回归 |
| `dfdc8b8e`（2026-10-15，`feat(whiteboard): implement initial whiteboard workspace`）及 `.trellis/tasks/10-15-whiteboard-scene/` | 以 `BarWorkspaceSceneState` 保存桌面基线，Enter/Switch 贴底 5 DIP 并保留 Centered/Free，最终 Leave 恢复；`design.md:9-13` | 形成当前场景矩阵。它保留“居中模式”这个状态值，但没有扩展根反推的 displayTransition 门禁；状态名保持 Centered 不等于每帧几何居中 |

历史 `07849596` 中的 `root-cause.md` / `break-loop.md` 当前工作区已不存在，本报告通过 `git show 07849596:<path>` 阅读，未把缺失路径当作当前可链接文件。历史 `09-25` 文档同样以该提交内容为准。

### 3.2 三个指定历史任务的结论边界

- **08-14**：仍适用于 workArea/dock、成功呈现和直移交接背景；不能拿“启动居中”证明场景逐帧居中。
- **08-20**：证明“联合外框而非固定子元素”是原始设计目标，也证明 correction/rebase 方案的复杂失败史。当前问题不是恢复该旧方案的理由。
- **09-22**：`research/input-animation-audit.md:93-101` 已指出 displayCenter 的大位移动画通过 `SetDirect` 写到 MainButton，MainButton 自身 ValueRange 看不到完整 display 轨迹；这对 viewport 诊断有用。其 `findings.md:70-104` 关于时钟/capacity/光源的内容均为性能或未复现风险，不是本次几何根因证据。

当前规范也反映这个断层：`.trellis/spec/native-desktop/rendering-and-ui.md:942-946` 一方面要求场景保留 Centered/Free，另一方面只允许 Stable 且无 display transition 时反推根。规范描述现状，不能用来豁免本任务的新逐帧要求。

## 4. 现有测试、复现缺口与最小验证方法

### 4.1 现有覆盖能证明什么

- `bar_bottom_dock_tests.cpp:1165-1235` 覆盖根反推门禁、左右展开、描边、无效几何和根 X range；`:678-689` 覆盖 workspace 分类和场景 dock line。
- `ppt_session_tests.cpp:390-407` 覆盖 Enter/Switch/Leave 状态分类与只恢复一次；`bar_display_transition_tests.cpp:26-79` 覆盖显示起点、任务栏 Y 所有权和反向。
- 这些测试不执行 `Bar.RenderLoop.cpp` 的完整场景链。`InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:92-98` 编译 Animation/FramePacing 等模块，`:200-201` 登记两个测试文件，但没有生产 RenderLoop 编译/链接项。
- **未发现现有测试能在原问题上变红。** 相反，当前 helper 测试把 display transition 时“不得反推”固定为正确行为。纯状态分类、最终根公式或终态通过，都不能证明中间帧联合中心。

仓库存在 `Build/ARM64/Debug/InkeysHeadlessTests.exe`。本轮遵守“只写任务 research”的边界，没有运行可能写 TestResults/config 的基准或测试，也没有把既有二进制时间当作当前源码验证。

### 4.2 首选最小复现：真实 RenderLoop + 离屏 present sink

不需要复制生产公式，也不需要 GUI。复用 `CreateBarOffscreenRenderCallback`（接口 `Bar.Main.cppm:1208-1211`，实现 `Bar.RenderLoop.cpp:13144-13153`）和 `Bar.MultiTarget.Test.cpp:168-219,342-364` 的初始化/调度模式；只替换最终 `UPDATELAYEREDWINDOWINFO` sink，保留真实目标提交、动画、D2D/GetDC/ReleaseDC/EndDraw 和成功快照。

最小步骤：

1. 初始化 `CenteredBottomBar`、展开、稳定的主栏与已知单屏 `Display::Snapshot`。`MonitorInfo` 可直接提供 `bounds/workArea/effectiveDpiX/primary`：`Inkeys/Inkeys/Display/Display.cppm:140-176`；由 `PublishDisplaySnapshot` 走生产发布路径。
2. 先渲染到稳定，确认基线 `visualBodyCenterScreenX == ResolveBarBottomDockMonitorCenterScreenX(monitorBounds)`。
3. 用受控 `FrameContext{frameTime}` 逐帧推进（`RenderPipeline.cppm:61-66`），分别驱动：
   - Desktop→PPT：`PublishPptSession(1,true,showWindow)`；
   - PPT→Whiteboard：`SetWhiteboardActive(true)`，保持 PPT active；
   - Whiteboard→PPT：`SetWhiteboardActive(false)`，保持 PPT active；
   - 独立 Desktop→Whiteboard；
   - 最后 Leave：无 Whiteboard 时 `PublishPptSession(1,false,nullptr)`，或无 PPT 时关闭 Whiteboard；
   - 0.4 秒未完成时快速反向、Whiteboard 最小化/恢复。
4. 在场景中额外切换 Draw↔Selection 或改变实际可见按钮，确保覆盖主栏扩宽和收窄的非线性段；左右展开、100%/150% DPI 各跑一组。
5. sink 记录 `pptDst/psize/pptSrc/prcDirty`；callback 返回后只在 `BottomDockPresentedSnapshot().serial` 前进时记录成功帧。逐帧断言：
   `abs(snapshot.visualBodyCenterScreenX - ResolveBarBottomDockMonitorCenterScreenX(snapshot.monitorBounds)) <= epsilon`。
   同时保存 `mainCenterScreenX/Y`、zoom、centerMode/phase、transitionSerial 和 ULW tuple，便于区分根几何与窗口提交。离屏数值比较可沿用现有测试的 `1e-6`（`bar_bottom_dock_tests.cpp:33-36`）；真实像素验收只接受既有整数取整误差，不把离屏严格浮点阈值冒充 GUI 光学阈值。
6. **修复前判定标准**：不是看最终帧，而是至少一个场景中间成功帧误差超过既有取整容许。若 Enter/Switch 没有红，先检查 fixture 是否确实为展开 Centered BottomDocked、是否发生了宽度变化和场景 Y 动画；不能用稳定基线或终态代替复现。Leave 应单独报告，不把它的结果合并进 Switch。

该接缝直接测量当前生产已发布的“实际视觉联合中心”（`Bar.RenderLoop.cpp:12551-12568`），比自行用固定宽度重算更可靠。离屏结果证明软件几何/事务，不证明真实 ULW/DWM 或 Office/WPS。

### 4.3 辅助纯算法回归（不能替代 4.2）

可在 `bar_bottom_dock_tests.cpp` 用真实 `BarUiValueClass` 和 `BarUiAdvanceAnimation` 驱动 displayCenter、mainBar.x/w 的不同目标，再调用现有 `ShouldDerive...` 和 `ResolveBarBottomDockCenteredRootPlacement`：在场景 display active 时，按当前门禁会跳过根反推，从而得到非零联合中心误差。它能提供很小的红色回归向量，但分支组合仍由测试搭建，不能证明真实场景接线或 Leave 行为；最终必须以 4.2 为准。

### 4.4 真实运行验收

在获得 GUI 授权后，对相同场景逐帧记录成功快照/调试信息并截屏，核对实际主按钮+主栏外框中心与所在屏中点。需区分 Enter、Switch、Leave 和快速反向；同时记录 ULW/HWND 结果，防止把软件中心正确但光学位置异常漏掉。现有 raw diagnostics 可辅助判断采样/失败，但不要仅看 callback 或最终稳定帧：`.trellis/spec/native-desktop/ui3-render-diagnostics.md:30-47,95-103`。

## 5. 候选修复边界与权衡

### 候选 A（优先评估）：把“场景竖直就位”从 generic displayTransition 所有权中拆出，复用当前几何根反推

目标不是删除所有门禁，而是让满足 BottomDocked + Centered + Stable + Expanded、无拖动/弹簧的**场景放置帧**也在主栏动画后执行同一根反推；Free/Floating/folded、捕获/脱离/恢复、普通显示切换仍由原所有者持有。

必须作为一个完整边界处理：

1. 明确 scene placement 与 generic display transition 的判定，不能用第一帧布尔值碰巧区分；`pptDockPlacementPending` 在 Submit 中会清除，`whiteboardDockAnimationActive` 则跨帧存在：`:2763-2765,2829-2838,2854-2858`。
2. 根反推调用 `:5554-5588` 与 viewport 根范围调用 `:9043-9080` 必须消费同一所有权决定，避免只修绘制却破坏包络；不要在两处各自拼接容易漂移的布尔表达式。
3. 保留 MainBar `x/w` 的目标、批次、Back/Ease 曲线和 `displayCenterY` 的 0.4 秒过渡；不得锁宽或只修终点。
4. 根反推中的 `displayCenterX.SetDirect` 会终止独立 X 插值（`SetDirect` 同步 val/tar/start 并清进度：`Bar.Animation.cppm:840-853`）。这意味着 Centered 场景的 X 变成“由屏幕中心和当前宽度约束派生”，而不是独立 0.4 秒曲线。它与用户的渲染时居中设计一致，且 Y/宽度曲线不变，但应作为明确的所有权变化审查；Free 场景仍保留原 X 过渡。
5. 保留 `bottomDockRootLayoutChanged` → dirty/full replacement 的现有事务：`:5586-5587,7293-7317,9409-9419`。不要为了少整窗替换而回到视觉 correction。
6. 同时验证首次场景帧、快速反向、左右展开、跨屏、最小化恢复、Leave 和失败重试；不能把 helper 门禁翻转当完整修复。

权衡：改动集中在已有根所有权和预测路径，最符合原始设计，避免新布局系统；风险是场景/显示切换的状态命名容易混淆，且 X 不再保留独立插值。 Leave 当前已有直接恢复分支，不应为“统一”而强行重写。

### 候选 B：让 displayCenterX、mainBar.x/w 共享一个参数化主进度，以公式保持中心

理论上可保留独立的参数化曲线，但必须同时处理不同起点/目标、布局中途加入、Back 超调、左右换边、zoom 和可见描边，实际上是在复制一套耦合动画求解器。复杂度高且容易重复 08-20 的多状态问题，不建议作为首选。

### 候选 C：恢复逐帧视觉 correction / 延迟 rebase

可以短期挪动最终像素，但不修正 MainButton/MainBar 继承根、命中和 viewport 的同一性，并重新引入 08-20/08-23 已记录的多阶段失败风险。不符合当前“稳定居中无 correction”的所有权方向，不建议。

### 候选 D：全局删除 `!displayTransitionActive`、关闭动画、固定宽度或只对齐终点

- 全局删除会改变普通显示器/DPI 过渡和所有者的既有边界，超出当前症状，风险不可控。
- 关闭宽度动画、锁宽、只修最终帧违反 PRD R4/AC3，不是有效方案。

## 6. 仍需主会话运行验证的问题

1. 4.2 的真实协调器复现是否在 Desktop→PPT、Desktop→Whiteboard、PPT↔Whiteboard 的中间成功帧得到预期非零误差？误差曲线与主栏当前宽度的关系是否符合 2.1？
2. 最终 Leave→Desktop 当前是否已满足不变量？若失败，需保留该路径独立样本，查明是否来自快速反向、残存弹簧、显示 serial 或另一提交问题，而不是套用 Enter 根因。
3. 场景 X/Y 的现有实际曲线、首帧 dt=0、快速反向和动画关闭时，修复候选是否保持 Y/宽度设计且没有首帧跳变？
4. 同步放宽根范围预测后，所有中间帧是否满足 `pptSrc + psize` 位于 target、无裁切、无漏 dirty；根位置变化触发的整窗替换是否按现有合同执行？
5. 普通居中拖动、40 DIP 吸附附近居中、Free 底栏、Floating/folded、无宽度变化的 Y 移动是否无回归？
6. 100%/150% DPI、左/右展开、负 monitor origin、跨屏/原屏消失、Whiteboard 最小化/恢复的矩阵是否通过？
7. 主会话已完成当前 HEAD 的完整 ARM64 Debug Solution 构建（0 警告/0 错误、目标 up-to-date）并运行同一 `InkeysHeadlessTests --no-window` 产物（退出 0）；新增离屏 fixture 仍需实施后重新构建验证。后续 Git Bash 命令使用原生架构 host 和 `-p:` 参数，超时至少 5 分钟：
   - `MSBuild.exe InkeysRepo.sln -p:Configuration=Debug -p:Platform=<NativePlatform>`（`.trellis/spec/native-desktop/build-and-compatibility.md:5-21`）。
   - `./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`；其他原生架构使用对应 `Build/<Platform>/Debug/` 产物。
8. 真实 Office/WPS、Whiteboard、ULW/DWM 与录屏逐帧验收仍需 GUI 授权；headless 和静态证据不替代它们。

## Related Specs

- `.trellis/spec/native-desktop/index.md:3-6,46-60`：唯一 UI3、编译边界和测试边界。
- `.trellis/spec/native-desktop/rendering-and-ui.md:603-738`：动态 HWND、dirty、capacity、ULW 与成功事务。
- `.trellis/spec/native-desktop/rendering-and-ui.md:871-1031`：二维 dock、稳定根节点、workspace 场景和测试要求。
- `.trellis/spec/native-desktop/whiteboard.md:23-35,52-62`：场景基线、5 DIP、离屏与 GUI 验证边界。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md:30-47,95-103`：成功提交诊断与无窗口验证限制。
- `.trellis/tasks/10-25-ui3-bar-centering-dock-geometry/research/shared-frame-contract-and-validation.md`：父任务的共享成功帧不变量和跨子任务验证边界；它是并行研究，不作为本报告的产品证据来源。

## Caveats / Not Found

- 未找到现成的“PPT/Whiteboard 场景 + 动态宽度 + 每成功帧联合中心”回归；现有 helper 测试反而固定 displayTransition 排除行为。
- 未找到静态证据把最终 Leave 与 Enter/Switch 归为同一必然缺陷；需运行样本回答。
- 未执行构建、测试、GUI 或外部搜索；没有修改产品代码、测试、spec、其他任务目录，也没有执行 git 写操作。
- 历史任务中的设计和根因声明只用于解释演变，均未替代当前代码行号证据。
