# Research: 当前 Scroll Follow 产品接入点

- Query: 基于当前代码定位 Draw3 唯一 owner、场景/viewport、输入收尾、双窗握手及 Begin/ApplyAbsolute/End 屏障；定位 UI3 More、配置/i18n/SVG、业务退出入口；区分可运行的全隐藏测试与会显示 HWND 的既有 harness，并设计 A → follow → B → history → save 往返验证。
- Scope: internal（当前工作区源码与 Trellis specs；不修改生产代码，不执行 Git 操作）
- Date: 2026-10-21
- Active task: `.trellis/tasks/10-21-scroll-follow-mag-capture`。派发正文曾写父任务 `10-21-scroll-follow-v1`，主会话已确认以 hook 的 capture 目录作为真实输出路径。

## Findings

### 1. 先采用的结论

1. **Draw3 的输入位置、stored 点和 viewport 实际都在 client/canvas 像素空间。** `CanvasNavigation` 的若干常量虽以 Dip 命名，不能据此给滚动目标乘 `96/DPI`。tracker 像素只乘一次 `sourcePixelsPerPixelX/Y`（若 tracker 另做降采样，则把降采样比纳入该单次映射），得到 source 物理位移，再按同代 source/client 映射进入 Draw3；当前主屏同尺度路径是 client px。
2. 产品尚无 scroll 会话桥。`TranslateViewport` 已存在于内部 `CanvasCommandType`，但不是公开 `Bridge::CommandType`；它是增量命令，没有 session/sequence/场景身份/完成回执，不能直接当成可丢帧的绝对目标接口。
3. **输入关闭必须并入 Host 的聚合 gate。** 单独调用 `input.SetAdmissionBlocked(true)` 会被 `RefreshPresentationInputGateLocked()` 下一次状态泵覆盖。恢复也不能无条件置 false。
4. **只读 `HostRuntimeSnapshot` 不足以取得 Begin 基准。** 它没有 viewport、WorkspaceGuid、PageGuid。基准必须在绘制 owner、安全边界从真实 document/canvas 按值采集。
5. **More 当前忽略持久化 B 区。** 只改 `MakeDefaultExtensionButtons()` 不会显示新按钮；`Load()` 当前显式 More 只插入 Setting。新增按钮需要注册、显式 More 插入及最终状态投影同步补齐。
6. **现有 `--draw3-hidden-test` 和 `--draw3-eraser-hidden-test` 不是全程 invisible-HWND 测试。** 它们调用真实显隐事务并验证 `IsWindowVisible=true`。本轮不能直接运行；`--draw3-host-metrics-smoke` 才有全程不显示 HWND 的既有模板。

### 2. Files found

| 文件 | 当前用途及接入重点 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.h/.cpp` | 产品入口与生命周期门；`PublishProductState/Workspace/Command` 和 `StopProduct`。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h/.cpp` | 唯一绘制线程、bridge 消费、输入 gate、runtime 和输出 ready；适合接产品层窄会话接口。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h/.cpp` | 不可变产品状态、可靠 scene-stamped FIFO 命令；目前无 scroll command。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp/.cppm` | document/history/viewport 的唯一 owner；现成 contact seal、平移、tile 恢复、保存安全点。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp/.cppm` | content delta → viewport 的符号、范围保护、屏幕/画布变换；不做 DPI 位置换算。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp/.cppm` | admission revision、可靠 command wake、route quarantine/recycle。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp/.cppm` | HWND 消息、鼠标 client 坐标、内部 canvas command 队列和 resize/DPI。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | packet → canvas px 解码；shared position scale 与设备上下文。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp` | 完成笔画时将锁存 viewport 加到输入点，生成 canvas-local stored 点。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationState.h` | 双窗 ready 判定与 Primary/Presentation/Hidden 决策。 |
| `Inkeys/IdtState.cpp/.h` | 工具与工作区产品状态、临时 selection 投影必须覆盖的显隐 guard、白板事务和 Freeze 入口。 |
| `Inkeys/Inkeys/Window/Window.cpp/.cppm` | 双窗 owner-thread 成对显隐的唯一执行者。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp/.cppm` | 主栏按钮 enum、注册、More 列表、状态、Undo/Redo 和 Clear 点击。 |
| `Inkeys/Inkeys/UI/Bar/Bar.KeyboardShortcuts.h` | 独立键盘 Clear/Undo/Redo/Freeze 路径；只接按钮会漏掉它。 |
| `Inkeys/Inkeys/Other/Other.Config.cpp/.cppm` | 持久化 Bar 三区 schema、legacy layout 迁移、默认 B 顺序。 |
| `Inkeys/Inkeys/UI/Freeze/Freeze.cppm` | 原子 Freeze 状态、单个 observer、Toggle 和工作区互斥。 |
| `Inkeys/IdtPlug-in.cpp` | PPT 生命周期、文稿目标替换、Desktop/Presentation 发布。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp/.h` | 当前隐藏测试工具；普通入口会 show，metrics smoke 可作全隐藏模板。 |
| `inkStrokeModelerTest/draw3/uink_draw3_{capture,export,import}.cpp` | 共享 UInk 快照/编解码边界，保存点与 viewport 的真实格式。 |
| `inkStrokeModelerTestTests/contact_input_tests.cpp`, `uink_tests.cpp` | UInk 早退 CLI 和实际 Save/Read/Import 往返测试。 |

### 3. 坐标合同：位置不经过 DIP

| 环节 | 直接源码证据 | 对 follow 的含义 |
| --- | --- | --- |
| 产品 DPI awareness | `Inkeys/IdtMain.cpp:156` 的 `EnsureProcessDpiAwareness()` 请求 `PROCESS_PER_MONITOR_DPI_AWARE`，早退自检需复用其前置配置。 | HWND client 与像素映射不能由测试进程 DPI 虚拟化悄悄改变。 |
| 鼠标输入 | `Draw3.WindowControl.cpp:494` 的 `HandleCompatibilityMouse()`，503–514 将 `GET_X/Y_LPARAM` 直接传入 `mouseInput_.Down`。 | client px，没有 `96/DPI` 除法。 |
| RTS 输入 | `Draw3.RealtimeStylus.cpp:1680` 从 `GetPacketDescriptionData` 取 shared positionScale；958–959 直接 `packetXY * positionScale`。2883 的现有诊断明确 `positionMeaning=canvasPx=packetXY*sharedPositionScale`。 | 沿用 RTS 自身到目标画布的 position scale，scroll 不能再套一次该 scale 或 DPI。 |
| 注入测试 | `Draw3.Host.cpp:2479` 将 mailbox 的 short X/Y 直接写入 `snapshot.position`。 | 合成测试的输入也应明确是 client px；不能传 DIP 然后把通过当成产品坐标正确。 |
| Down 锁存 | `Draw3.DrawingController.cpp:10595` 至 10605 从当前 canvas 锁存 `runtime->viewport` 与 WorkspaceGuid/PageGuid。 | B 的归属和 offset 在新 Down 固定，旧 A 不重写。 |
| Stored 点 | `Draw3.DrawingController.cpp:2374` 将锁存 viewport 传给完成函数；`Draw3.StrokeGeometry.cpp:898`–901 存 `point.x + canvasOffsetX` / `point.y + canvasOffsetY`；shape 在968同样做。 | viewport 与输入点必须同单位，实际是 canvas px。 |
| 可见区域/重放 | `Draw3.DrawingController.cpp:1202` 的 `VisibleInkBounds()` 把 viewport 与 client `width/height` 直接相加；12387 的 RestoreComposition 请求传原 viewport 和 client 尺寸。 | 不是 DIP renderer；位移应进入 client/canvas px。 |
| 平移符号 | `Draw3.CanvasNavigation.cpp:105` 做 `requested = oldViewport - contentDelta`；139–150 的 `ScreenToCanvas=screen+viewport`、`CanvasToScreen=canvas-viewport`。 | tracker 内容向上 `-120px` → viewport 向下 `+120px`。 |
| UInk | `Draw3.DrawingController.cpp:1401`、9234 把 viewport 原样捕获；`uink_draw3_export.cpp:168` 点原样写入，406 的 viewport 亦原样写入；`uink_draw3_import.cpp:253` 原样读回 viewport。 | `snapshot.dpiScale` 不会替位置补做转换。不能“先除 DPI，期望保存时修复”。 |

建议 target 携带：`sessionId, sequence, sceneIdentity, source/client/displayIdentity, sourcePixelsPerPixelX/Y, totalContentClientPxX/Y`（或明确命名为 tracker px 并在唯一 owner 转换处完成一次）。Begin 固定 source rect、client 几何、映射比例；来源尺寸/DPI/旋转/client origin 或设备代次改变时结束本会话。**比例应来自实际 Mag transform 与帧生成过程，不能用 sourceRect 宽/child client宽盲算。** 主会话已更正：原路径 identity transform=1，上轮图案整图tile边界匹配支持1:1；既有1px source/client几何差异是裁切边界，不证明自动fit缩放。正式帧应核对 `MagGetWindowTransform` 并显式发布identity下ratio=1；tracker的降采样比才按真实处理并入该一次转换。此项使用主会话提供的实验事实，本研究未重复执行图案实验。

绝对应用建议：先从 **Begin 的 baseViewport** 计算临时目标 `base - absoluteContentClientPx`，再通过 `ApplyCanvasContentTranslationChecked()` 做现有范围保护；若复用增量应用 helper，传 `currentViewport - desiredViewport`，不能把累计总量每次再叠加到 current。例：baseY=0、累计 contentY=-120，最终 viewportY=120；内容 A 保留原 storedY=150，显示在30；退出后同一 clientY=150 的 B 保存为 storedY=270。整个链不乘 `96/DPI`，不移动 A 的点。

### 4. Draw3 owner 与最小屏障落点

**已知执行链**：`Product::PublishProductCommand`（`Draw3.Product.cpp:229`）→ `Host::PublishCommand`（`Draw3.Host.cpp:2758`）→ 可靠 command wake → `ConsumeBridge`（1724）/`PumpOneBridgeCommand`（1669）→ `WindowController` 的 `CanvasCommand` → `DrawingController::Run()` 内部 `processCanvasCommands`（`Draw3.DrawingController.cpp:13691`）。后者13702仅在 `active.empty()` 时取命令。文档的构造及 Desktop/Whiteboard/PPT 独立 slot 在8975、8997、9022；不能另开一套 document/Host。

- 发布端现成的顺序保护在 `Host.cpp:2760`：`commandPublishMutex` + `presentationTargetMutex`，先 `TryReserveCommandWake()`，bridge 接受后才 `PublishReservedCommandWake()`。Begin/End 应使用这种可靠语义；高频 Apply 用单槽 latest-only 和 wake，不能每帧向无界 FIFO 追加。
- 基准从 owner 读取 `document_->WorkspaceGuid()`、当前 `InkPage::PageGuid()`、`kDefaultDeviceKey`、canvas viewport、当前场景/Host 代次。`Bridge::AcceptedStrokeBegin`（`Draw3.Bridge.h:152`）已有 workspace/page/sceneGeneration/hostGeneration；controller 的 `acceptedStrokeSceneGeneration` 和 callback 在9081。`Shortcuts::Scope`（`Draw3.Shortcut.h:10`）已有 hostGeneration/pageEpoch。可沿用相同代次原则，但不以 pageIndex 或 `ProductState::revision` 代替完整身份。
- `HostRuntimeSnapshot`（`Draw3.Host.h:314`）没有 viewport/页 GUID；需要新增窄 Begin reply，不能在 UI 线程从 runtime 拼装假的基准。Begin 返回不可变值，拒绝缺文档、非 Desktop、busy scene、旧 Host 或 Freeze active。

**Begin 建议落点**：在上述可靠发布门内关 follow admission 原因并发布 Begin；owner 通过已存在的 `sealPresentationContacts` 收尾，然后在 `processCanvasCommands` 的 `active.empty()` 安全边界采集真实基准并 ACK。`sealPresentationContacts`（`Draw3.DrawingController.cpp:11279`）按 admission revision 只结束已接受的最后位置，持久笔走原 Up/Stored 事务；gesture contact 走 `DiscardUntilTerminal`，清 transient/haptics。`processCommand`（11332）每个 control marker/Down 边界会先调用 seal；被拒的 Down 在11347只隔离到真实终态。不能用 `ResetForNextRun()` 或 consumer 伪造物理 Cancel 来实现 Begin。

**ApplyAbsolute 建议落点**：owner 验 session、递增 sequence、Workspace/Page GUID 和各代次后，在14913的 seal 之后、14918的 viewport 恢复处理之前消费最新目标。若选择在现有内部 TranslateViewport 分支旁处理，也要保留 pending/wake，使下一轮能进入恢复，不能在命令已经 report 后丢失最后目标。不得受 `kCanvasNavigationProductIntegrationEnabled=false`（`Draw3.WindowControl.cppm:29`）阻止这个窄接入；也不得为了 follow 把原手势/键盘导航总开关改为 true。

**复用的应用尾部**：`TranslateViewport`（`Draw3.DrawingController.cpp:14395`）成功 SetViewport 后按现有路径 `InvalidateRaster`、`DiscardHotPreimages`、`viewportRefreshPending=true`、`viewportRefreshClearsTransient=true`、`forceFullPresent=true`。14918起清理屏幕 L2/transient，按可见 tile plan 和 budget 调用原 `historyGpuCache.RestoreComposition`；canvas-local composition cache 保留。不要把平移变成 Stroke 变换、历史追加、清 redo 或新常驻 GPU 镜像。现有 trusted snapshot 的签名包括 viewport（3112），不可用旧签名证明新位置已呈现。

**End 建议落点**：首先关闭 target 发布，可靠 End 自带最后可信绝对目标与身份，不依赖可被合并覆盖的单槽尾项。owner 应用后等待 `presentSucceeded && !viewportRecoveryPending && !viewportRefreshPending`（可复用 `Draw3.DrawingController.cpp:16490`–16495 的真实帧安全点）再发 End 内容完成回执。当前 `commandProcessed/reportCommand` 在13695即报告，**不代表新 viewport 完整呈现**；必须另有 applied/presented session+seq 或 viewport revision，不能只看 `presentedContentRevision`，因为平移不会改变当前非空 bool。

后续tool/Undo/Redo/Clear必须排在可靠End之后，并在所需End回执前保持输入gate；仅将UI `active=false` 然后立即发布动作/开放输入不满足顺序。普通命令的现成中央门是 `Host::PublishCommand:2758`（还有独立shortcut通道）；工具函数可在各自意图入口汇聚窄的deferred action，但 `SyncDraw3State:773` 在状态已修改、且某些同工具分支不调用它时才执行，不能独自承担End-before-action。

**聚合输入 gate**：`Host::RefreshPresentationInputGateLocked`（`Draw3.Host.cpp:767`）将 inputStopping、commandScenePending、白板 load 和 PPT ready/UI ready 等合成后写 BaseAdmissionBlocked。follow 的 initializing/tracking/stopping 原因应并入这里；释放 follow 原因后重新算整式。`ContactInputCoordinator::SetAdmissionBlocked`（`Draw3.ContactInput.cpp:896`）只在总开闭变化时递增 admission revision；`ContactAdmitted`（936）还检查 Down 的旧 revision。因此不要在 active follow 中反复开关 true/false，也不能在一次失败回滚时释放其它 blocker。

### 5. 双窗口 selection 握手与临时穿透

- `SelectionUsesAuxiliaryOutput`（`Draw3.Bridge.h:187`）是 `selectionMode && workspace != Whiteboard`。主窗始终承载输入，辅助窗是同一 backbuffer 的 selection-only ULW，无第二输入或文档。
- 绘制循环14629检测 effective selection 变化，调用 `presentation_.SetOutputTarget()`；`TransparentPresentationController::SetOutputTarget`（`Draw3.TransparentPresentation.cpp:1147`）推进 output revision。Host 的 present observer仅在 **成功 full present** 时更新 ready target/revision（`Draw3.Host.cpp:915`–940），失败不能放行。
- `DrawpadPresentationReady`（`Draw3.PresentationState.h:17`）要求 firstFrame、同 workspace/selection、requested=ready target/revision、presentedContentRevision=contentRevision、PPT 身份一致。`ResolveDrawpadPresentationSurface`（34）只在 selection+无内容+aux 完整 alpha clean 时允许双隐藏。
- 产品显隐实际入口是 `IdtState.cpp:508` 的 `ReconcileDraw3PresentationState()`，622调用 `Window::Service::SetDrawpadSurfaceVisibility`。target未ready时573–593保留旧表面；不是直接 hide/show。
- **临时 follow selection 不应改写用户工具/笔色宽。** 当前 `PublishDraw3State`（`IdtState.cpp:261`，276）从真实 StateMode 推 selection；Reconcile在519–539再次比较 StateMode 与 bridge，bridgeGuard还会重验。只在 Bridge 里强制 selection=true 会被这个 guard 拒绝，或被下一次 `SyncDraw3State` 覆盖。需要一个同代、可撤销的 follow override，参与 Publish、Reconcile 的 effective selection 和 guard；真实工具快照保持。不要借 `ChangeStateModeToSelectionFromButton()`，该入口可能发 Clear。
- 开始 Tracking 前等辅助 full output ready **及 owner-thread 成对切换成功**。End 保留 gate，结束 override，等原工具 primary 输出完整 ready 与成对切换成功后才放开输入；失败维持 Stopping/blocked 的真实状态，不能用超时伪造 ready。窗口操作与等待必须在 state/bridge/command 锁外，不在 Mag frame callback 中等待 Draw3/Window owner。

### 6. UI3 More / 布局迁移 / i18n / SVG

| 位置 | 必要接入 |
| --- | --- |
| `Bar.Button.cppm:23` 的 `BarButtonPresetEnum` | 为“视图调整”增加独立 preset；追加到尾部可保持现有 enum 值。preset数组在250为40项，状态是247的 map。 |
| `Other.Config.cppm:36` 的 `BarButtonId` | 增稳定官方 ID，例如 `Inkeys.Bar.ViewAdjust`；220的默认 B 在 Setting 前加入它。不是新建另一套配置系统。 |
| `Bar.Button.cpp:103` 的 `RegisterButton` | 当前扩展区官方 ID 仅特许 Setting（122–124）；新官方 More 按钮需同样精确放行，不能放开任意 `Inkeys.*`。 |
| `Bar.Button.cpp:686` 附近构造/729注册 | 参考 Setting 创建 label/icon/callback/state 并注册 Extension。按钮应发会话 toggle 意图，不能在渲染回调执行截图或同步等 End。 |
| `Bar.Button.cpp:1407` 的 `Load` | 1410明确忽略持久化 ExtensionButtons；1461的 explicitMore 当前只塞 Setting。新按钮在此稳定插一次、位于 Setting 前；不要加入16旧组件的“两实体上主栏/其余overflow”分流。 |
| `Bar.Button.cpp:1263` 的 `NormalizeExtensionZone` | 持久化 B 的单例/未知插件保留规则现成；补新按钮必须保持已有 entry 顺序、Visible及其它字段，缺失时仅插在现有 Setting 前，Setting缺失则附加；不得以缺新按钮触发整个B或A区reset。 |
| `Other.Config.cpp:921` 的 `TryMigrateLegacyBarButtonLayout` | 仅三个新字段都缺失才拆旧 ButtonLayout；913为解析后迁移调用处，`ReadAll`在783、`ReadMini`在800。新按钮补缺不能只改默认值，必须覆盖“已有B但缺新按钮”的旧用户配置；codec在367负责元素类型/Visible，无需改未知插件持久化格式。 |
| `Bar.Button.cpp:958` 的 `ApplyMainPresentationLocked` | 唯一最终 size/hide/Selected/Disable 投影；968已取 Freeze active。这里把新按钮限制 Desktop、Freeze active禁用、有效会话选中。不要只改构造时状态。 |
| `Bar.Button.cpp:926` 的 `StateUpdate` / `barUISet.UpdateRendering()` | 会话状态改变后通过既有刷新入口重算按钮和 More 命中，避免仅更新原子flag而画面不更新。 |

现行 spec 有“UI3运行投影不写B”的旧兼容合同。任务已明确要为新按钮补缺时，应只对 **该新官方 entry** 做窄迁移并记录此差异，保留原16旧组件开关投影及用户其它B顺序；不要恢复整套B自定义功能作为附带重构。

文案源为 `Inkeys/src/i18n/zh-CN.jsonc`、`zh-TW.jsonc`、`en-US.jsonc`，主栏 label 路径参考 `UI.Bar.MainButtons.SettingsLabel`（`Bar.Button.cpp:693`）。由实现者添加三语“视图调整”并运行 `pwsh ./Scripts/i18n.ps1 sync`、`check`，生成 `Inkeys/IdtI18nKeys.g.h` 和 `Scripts/i18n.zh-CN.snapshot.jsonc`；不得手改生成 header。研究角色不执行会写这些文件的 sync。

SVG走现有代码原生资源链：`Inkeys/src/UI/*.svg` → `Inkeys/Inkeys.rc` 的 `UI` string资源（Setting在302、More在342）→ `InitializationFromResource(L"UI", L"bar...")`（`Bar.Button.cpp:702`）。新 SVG 还需 `Inkeys.vcxproj`（现资源登记1267起）和 `.filters`（Setting在2486）登记，工程改动交指定实现者。此次调查未发现“SVG生成脚本”；SVG本身是源资产，不能把 i18n sync 误称为会生成SVG。图标缓存随原 `BarUIRendering::DiscardDeviceResources` device epoch路径处理，无需新缓存寿命。

### 7. 必须 End-before-action 或使旧 target 失效的入口

| 触发 | 当前具体入口 | 最小合同 |
| --- | --- | --- |
| Select/ClickThrough/画笔/荧光/激光/Shape/Eraser | `IdtState.cpp:1272`、1311、1316、1348、1410、1432、1450、1471、1492、1528、1533、1539 | 在工具意图应用前结束 follow；同工具重复点击也要处理，1457早退不会调用 Sync。不要只在 Sync之后补stop。 |
| 颜色/宽度及设备画具设置 | `IdtState.cpp:1023`、1028、1033、1065、1099、718、738 | 识别用户编辑与纯显示上下文更新；若终止则在状态锁外排 End，禁止回调重入锁。 |
| 主栏Undo/Redo、弹窗Undo/Redo | `Bar.Button.cpp:1604` 的 `EngageRecallCommand`，1620发产品命令；兼容`IdtDrawpadFacade.cpp:119/125`也直接发命令 | 产品普通命令提交边界统一保证前序End，不能只改一个按钮。 |
| Clear/选择按钮带Clear | `Bar.Button.cpp:526`；`IdtState.cpp:1238` 的 `ChangeStateModeToSelectionFromButtonImpl`、1272产品入口 | 保留原Clear拒绝时不换工具的事务；follow End失败不得继续Clear。 |
| 键盘Undo/Redo/Clear/Freeze | `Bar.KeyboardShortcuts.h:168` BeginClear、216起分发，231 Freeze，237 Undo/Redo；`IdtState.cpp:1363/1393`条件工具/Freeze | 独立shortcut租约/场景scope继续有效，End不能绕开lease，也不能使旧动作在新页执行。 |
| Freeze按钮/其它Toggle | `IdtState.cpp:1321`、1393；`UI/Freeze/Freeze.cppm:128` Toggle、145/150工作区状态 | follow开始检查实际`Freeze::Snapshot`；先结束再启Freeze。Freeze只有一个StateObserver，不能用新注册覆盖原协调器observer。 |
| 白板进入/退出/翻页 | `IdtState.cpp:833` RequestWhiteboardActive；900/906翻页；`StateMonitoring:2072`实际工作区事务，2128/2148等发布Workspace | 用户请求即撤销follow发布；在Draw3实际workspace切换/页替换前owner再次退役scene，不能只靠UI状态。 |
| PPT开始/结束/文稿或绑定目标变更 | `IdtPlug-in.cpp:1153` ClearProductPresentationTarget，1162 Desktop，1176 PublishProductPresentationTarget；`Draw3.Product.cpp:151/160`公开Workspace/Target入口、201的兼容PublishProductPage | 由产品workspace/target边界覆盖所有生产者，旧Desktop目标不得抵达Presentation。 |
| 页/文档真实替换/恢复 | Controller `SetPresentationTarget:14029`、`SetWorkspace:14133`、翻页14424起；白板cold安装9335、Desktop恢复13738、PPT安装13978、Clear14250附近 | 每次安装/换slot/ReplacePage退役当前follow身份；相同页index甚至相同PageGuid恢复也需要运行代次防迟到。 |
| 显示/大小/DPI/位置 | `Host::ForwardMessage:2594`对WINDOWPOS/DPICHANGED/DISPLAYCHANGE置displayDirty；`PumpDisplayScale:1424`取display generation/client/monitor；`WindowControl:1981/1998` SIZE/DPI；Controller `ProcessPendingResize:8938/14819` | capture geometry与Drawpad client/display变化必须先撤目标；先在owner gate失效，再异步完成产品stop；不要在WndProc同步join/截图。 |
| GPU设备/Presenter恢复或故障 | Controller `graphicsRecoveryPending_:14684`，失败16475；`TransparentPresentation::RecoverFromRuntimeFailure:1175` | 停follow并退休设备/输出代次；恢复沿原renderer/history路径，不沿旧图像或旧ready继续跟随。 |
| 退出/重启/Host关闭 | `IdtMain.cpp:391` StopMagnifierCoordinator，3844 join Mag、3848 StopProduct、3851 WindowService stop；`Host::Stop:2217`，2233关准入，2244排最终保存，2251停producer | 先关闭follow发布并把最后可信目标并入可靠owner屏障，再让最终保存读取最后viewport。原Stop无超时drain合同保持；UI不得等待不可取消PrintWindow。任何early-return/启动回退也不得留旧session可重启。 |

这些是当前需要接线的位置，不表示它们已经实现 follow。避免在所有低层函数里各加一个互相等待的stop；产品动作边界负责顺序，owner的scene/device失效负责最后防线，二者各自完成最小职责。

### 8. 隐藏验证与 UInk CLI

| 入口/夹具 | 当前事实 | 本轮使用判断 |
| --- | --- | --- |
| `InkeysHeadlessTests.exe --no-window` | `animation_tests.cpp:1660`关闭会创建HWND的测试；bridge/contact/窗口决策/布局纯算法可复用。 | 可运行，不能证明真实present/可见穿透。 |
| `Inkeys.exe --draw3-host-metrics-smoke --output-root <已存在私有绝对目录>` | CLI在`IdtMain.cpp:1396/1511`严格早退；`RunHiddenWindowRuntimeMetricsSmoke:3713`，3764创建visible=false HWND，3775逐窗断言不可见；后续只发布状态/注入/Stop，没有surface show。 | 可复用的全隐藏真实Host模板；新增follow专测应按此结构，不读写用户配置/文件。 |
| `Inkeys.exe --draw3-hidden-test` | `IdtMain.cpp:1625` → `RunHiddenWindowIntegrationTest:3928`；3993/3997主动显辅助/主窗，RunMode975/998再show并取capture。 | 本轮不可直接运行。`MakeHiddenSpec:102`屏外+visible=false只是出生状态，不构成全程隐藏证明。 |
| `Inkeys.exe --draw3-eraser-hidden-test` | 3979也进入同一RunMode；975/998的show发生在eraser分支前。 | 本轮不可直接运行；不能因eraserOnly就忽略show。 |
| `Inkeys.exe --freeze-mag-printwindow-test` | 旧受控图案/覆盖窗实验，当前任务明确禁止本轮运行。 | 不运行；旧原型失败不作正式路径阻塞。 |
| `inkStrokeModelerTestTests.exe --uink-file-only` | `contact_input_tests.cpp:3470`早退执行RunUInkTests+RunUInkPageFileTests，避开默认GPU套件。 | 可复用真实codec/文件事务回归。 |
| `inkStrokeModelerTestTests.exe --uink-page-file-only` | 3468只运行页文件事务。 | 可运行。 |
| `inkStrokeModelerTestTests.exe --uink-presentation-only` | 3465调用RunPresentationUInkRoundTripTests；`--desktop-autosave-only`在3463，`--presentation-autosave-only`在3455。 | 可做共享存储回归，不等于产品follow已接通。 |
| `inkStrokeModelerTest.exe` 和 tests `--benchmark` | demo `main.cpp:122`调用`window.Show()`；runtime_benchmark.cpp:208使用SWP_SHOWWINDOW。 | 本轮不运行。不要用默认demo代替隐藏Host。 |

隐藏 smoke 已有真实mailbox注入模式：`HostStartOptions::enableHiddenTestContactInjection=true`、`DrawpadMsgCallback`，`HiddenWindowTest.cpp:3856`以 `SendMessageTimeoutW(kDraw3HiddenTestContactMessage)` 传纯值，不借超时后悬空的栈payload；每个Down/Move/Up等真正runtime进度，3882真Stop，3917之后再销毁service。`autoSaveRoot`为空关闭worker（`Draw3.Host.h:256`），新roundtrip需显式设置任务私有root，`autoSaveEnabled=true`。

### 9. A → follow → B → history → save 的可执行最小新增专测

现有测试没有这条产品链，需新增专用严格早退CLI，保持所有HWND `IsWindowVisible=false`，不调用`ReconcileDraw3Presentation()`的实际show分支或`SetDrawpadSurfaceVisibility(Primary/Presentation)`。可以验证输出ready事实与纯窗口决策；真正系统穿透另列可见验收。

1. 按metrics smoke启动真实Host、私有保存root，发布普通Desktop/SolidLine或Pen并等primary output ready；注入A的完整Down/Move/Up，等Stored/recycle与canUndo。记录A指纹、页/工作区GUID（需窄owner测试快照或最终文件证明）。
2. 从生产Begin/ApplyAbsolute/End接口进入follow，固定base viewport，拒一条持笔边界后的Down并验证真实Up后slot回收；发乱序/重复seq、旧session、错Page/Host及最后目标。合并压力下只接受最后可信绝对量，不能产生增量漂移；初始化失败必须回滚override和自己的gate原因。
3. 测1x/1.25x/2x的**模拟DPI但相同物理内容位移**，以及sourcePixelsPerPixel=2的降采样case，断言结果分别是相同client位移和正确倍数；这是防重复DPI的必要数值测试。基准非零、负向、硬边界、非finite也要验证，不把clamp当成功跟踪。
4. End带最后目标，等owner恢复tile和成功full present对应的会话/seq回执；再恢复原工具effective selection并放输入。断言follow没有新增历史项、清redo、改PageGuid、改变A坐标。无移动结束也保持历史。
5. 在相同client位置注入B，保存坐标应为`Bclient+最终viewport`；Undo一次只去B，再Undo去A，Redo依次恢复A/B。follow前已有redo分支另测，跟随本身不得取消它；新B按原语义取消redo。
6. 设置autoSaveEnabled并在全部前序End/history动作后调用真`StopProduct()`，利用原`PrepareExitAutoSave`最终屏障与worker drain保存。`CaptureDesktopAutoSaveForScene`（Controller1362/1401）已捕获真实viewport、visible history和点；自动保存只在Clear/Exit，不应为了follow增加每帧存盘或改变手动Save的Unsupported状态。
7. 真Stop后读私有root的索引及对应`.uink`，要求`ReadUInkFile=Complete`，再`ImportDraw3UInkDocument=Success`，检查格式v10、page/workspace GUID、最终viewport、A/B点/style及visible顺序。`HostHiddenPersistenceSnapshot`（`Draw3.Host.h:277`）可辅证accepted/committed计数，不能代替文件检查。
8. 沿现有真实codec模式（`uink_tests.cpp:3732`的RunFinalizedTailPersistenceTests，3759 Save→3760 Read→3773 Import）做数据相等检查；不要复用`HiddenWindowTest.cpp:166`的CheckSizeBoundaryFile来宣称A/B roundtrip，该helper从诊断三点另造snapshot，只证明那组三点可存。

补充失败/生命周期数值测试：latest目标与End竞态；End后迟到frame不发布；已接受End在满普通队列下仍可可靠抵达；显示/DPI/device/page/文档替换退出；Stop再次调用；Start失败/回退、Host重启旧seq失效；Freeze互斥与工具/shortcut先End再执行。生产真实viewport重放在14918后按预算跨帧，End必须覆盖它，不能因`commandProcessed`或任意一次successfulPresent就判定完整。

### 10. Related specs 与 external references

- 已读 `.trellis/workflow.md`；研究仅写本任务research，不读implement/check JSONL，不执行任务/工程变更。
- `.trellis/spec/native-desktop/index.md`：唯一Host、WindowService ownership与产品层边界。
- `.trellis/spec/native-desktop/draw3-integration.md`：输入/绘制owner、双窗selection、Desktop保存、UInk与cache/恢复合同；其中Dip命名不替代本报告的实际坐标代码证据。
- `.trellis/spec/native-desktop/input-and-ink.md:458`：Closing/Quarantined、DiscardUntilTerminal和Reset代次约束。
- `.trellis/spec/native-desktop/canvas-freeze-boundaries.md`：诊断不能阻塞绘制、PPT pending/ready、持久化真实drain合同。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:150`起：Bar注册/B兼容投影、未知插件保留、最终状态投影；268起国际化生成链。
- `.trellis/spec/native-desktop/scroll-follow-capture.md`：只取其现存线程/原Mag所有权背景。其旧“诊断后结束”阶段描述已被任务本轮授权更新覆盖；不以旧独立capture结果判定当前正式路径失败。
- 外部参考：本次是内部接线调查，没有新增网络资料或API版本假设。使用当前共享UInk v10实现、现有C++20 module/Win32/D3D11路径；未升级依赖或构建链。原Mag无callback基线按主会话给定已通过，不重复历史实验。

## Caveats / Not Found

- 本次仅静态研究和研究文件写入，未执行编译、测试或任何GUI/Computer Use。生产代码/工程由其它实现者修改，本文行号是研究时快照，后续编辑可能移动。
- 当前没有生产Begin/ApplyAbsolute/End、新按钮或上述A/B完整专测；本文的“建议/需新增”不算已实现或已通过。
- 真正全隐藏的Host能够证明CPU/历史/保存/软件Present结果及纯双窗决策，不能证明屏幕上穿透、底层应用确实收到滚轮/触摸、overlay排除后的真实背景取帧或光学一致性。
- **最终必须可见验收，且本轮不运行**：真实主栏More布局/选中/禁用和三语文字；浏览器/Word/PDF真实滚动和静止画面；主窗→辅助窗交接、墨迹不闪、输入确实到下层；实体笔/触控、双窗Z序/捕获；DPI/主屏切换与设备恢复的用户可见状态；长时间性能与资源；旧Windows真机。遇这些才能完成的门槛由主会话明确停止并报告，不以隐藏测试通过代替。
- `python`裸命令在本会话命中了不可访问的WindowsApps别名，因此研究读取改用PowerShell；没有因环境问题修改项目。所有有效结论来自读到的源代码，不把该一次工具启动失败当构建失败。
