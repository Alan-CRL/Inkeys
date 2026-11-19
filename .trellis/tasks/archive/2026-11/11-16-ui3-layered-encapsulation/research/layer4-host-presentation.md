# 第四层调研：宿主、目标资源、呈现与测试入口

## 最重要发现

- 【事实】已经存在“无主栏 HWND、执行真实主栏绘制”的入口：`CreateBarOffscreenRenderCallback` 只替换最终提交 sink，资源、动画、GetDC 和提交确认仍运行生产协调器。（`Bar/Bar.RenderLoop.cpp:13295`，`CreateBarOffscreenRenderCallback`）
- 【事实】多目标基准已经使用该入口，但默认 sink 只检查 HDC、尺寸及 alpha 参数；它没有检查主栏 BGRA 内容。（`Bar/Bar.MultiTarget.Test.cpp:108`，`ReceivePresentation`；`:249`，`MeasureRun`）
- 【事实】主栏、分页、形状编辑各有独立 renderer/context/bitmap；共享的是 RenderPipeline 的设备 epoch、D2D/DWrite 工厂。（`Bar/Bar.Main.cppm:895`，`BarUISetClass::spec`；`Bar/Bar.Scene.cpp:139`，`BarPageSurface::Impl::renderer`；`Bar/Bar.ShapeEditor.cpp:585`，`ShapeEditorSurface::Impl::renderer`）
- 【事实】分页除四 API 成功外，还等待精确原生回执；SelectionOverlay 则在 Present、SetBounds、Show 成功后直接提交快照，两者确认机制不同。（`PageControl/PageControl.cpp:2374`，`ConsumeNativeSurfaceFrame` 所在回执段；`SelectionOverlay/SelectionOverlay.cpp:108`，`Render`）
- 【事实】`Bar.Presentation.Test.cpp` 是受鉴权的真实窗口夹具，不属于无窗口测试；当前产品也已经使用 `Module::UI3` 记录管线诊断，旧背景文档的“尚无使用者”不能继续引用。（`Bar/Bar.Presentation.Test.cpp:771`，`Bootstrap`；`Inkeys/IdtMain.cpp:3143`，`wWinMain`）

## 阅读范围与证据约定

本次仅静态读码，没有修改文件、运行构建、测试或 GUI。命令进程被权限拒绝后改用文件读取，没有重试启动子进程。大文件均通过符号检索后读取局部。

以下 `Bar/`、`PageControl/`、`SelectionOverlay/`、`RenderPipeline/`、`Whiteboard/` 路径相对 `Inkeys/Inkeys/UI/`；其他路径相对仓库根。每项代码结论附当前文件行号及符号；【待测】不表示已有运行结果。

【事实】D2 明确要求三个现有宿主统一接入，并以无窗口主栏目标验证；同时要求拆开 renderer 对目标、GDI interop 和 Begin/EndDraw 的所有权。既有离屏回调尚不能单独证明这些所有权已经拆开。（`.trellis/tasks/11-16-ui3-layered-encapsulation/prd.md:114`，决策 D2；`Bar/Bar.Rendering.cpp:324`，`RecreateDeviceResources`）

## 1. 现有呈现时序

### 1.1 主栏

下表均为【事实】；“分离程度”描述当前责任所在，不代表封装方案。

| 步骤 | 拥有者、调用及成功边界 | 分离程度与证据 |
|---|---|---|
| 1. 注册单帧入口 | `BarUISetClass::Rendering` 创建协调器；协调器以 `Client::Bar` 注册 `RenderFrame(FrameContext)` | 共享管线拥有调度；主栏拥有完整回调。`Bar/Bar.RenderLoop.cpp:1053`，`Rendering`；`:12689`，`Register` |
| 2. 捕获状态、显示转换、布局 | `WakeAndSnapshot`、`ApplyDisplayTransition`、`SubmitTargetsAndLayout`、`AdvanceAnimationsAndDeriveLayout` 顺序执行 | 都在主栏协调器；显示环境和业务内容尚在同一帧编排。`Bar/Bar.RenderLoop.cpp:985`，协调器阶段声明；`:13262`，`RenderFrame` |
| 3. 光照与需求 | `PrepareLightingAndDemand` 计算主栏光源，并发布屏幕坐标光照快照供其他表面消费 | 主栏仍是共享光源生产者。`Bar/Bar.RenderLoop.cpp:7385`，`PrepareLightingAndDemand` |
| 4. 几何、容量、资源 | 确定 viewport、capacity 后调用 `spec.EnsureDeviceResources(epoch,width,height)`；重建失败保留全脏重试 | 资源仍由内容 renderer 持有。`Bar/Bar.RenderLoop.cpp:9327`，`CalculateDirtyAndDrawPresent`；`:9350`，失败段 |
| 5. 开始绘制 | 设置目标变换后调用 `BeginFrameDraw`；该函数执行 BeginDraw、dirty clip、透明 Clear | renderer 同时掌握帧生命周期。`Bar/Bar.RenderLoop.cpp:9636`；`Bar/Bar.Rendering.cpp:470`，`BeginFrameDraw` |
| 6. 内容绘制 | 主栏、属性、More、底栏提示等继续在同一函数中绘制 | 尚无单独的完整主栏内容绘制入口；协调器阶段 `CalculateDirtyAndDrawPresent` 同时包含绘制和提交。`Bar/Bar.RenderLoop.cpp:996`，该阶段声明；`:9648`，主栏绘制段 |
| 7. 提交准备 | 绘制命令结束后申请 Bar 呈现租约，核对原生表面、直拖和源裁剪范围 | 属窗口/提交责任。`Bar/Bar.RenderLoop.cpp:12112`，`CalculateDirtyAndDrawPresent` |
| 8. 四 API | GetDC(COPY) → ULW 或替代 sink → ReleaseDC(空 RECT) → EndFrameDraw | 仍在同一协调器；sink 只替换最终 ULW。`Bar/Bar.RenderLoop.cpp:12218`、`:12231`、`:12243`、`:12251`，`CalculateDirtyAndDrawPresent` |
| 9. 成功确认 | `BarPresentDecision::CompleteAttempt` 要求四 API 全成功；随后才推进 dirty、viewport、mapping、窗口边界、按钮与面板快照 | 有明确事务边界。`Bar/Bar.PresentDecision.h:145`，`IsComplete`；`:352`，`CompleteAttempt`；`Bar/Bar.RenderLoop.cpp:12323`，成功发布段 |
| 10. 失败 | 保留旧成功快照、RetainForRetry；按失败类别退避，必要时丢弃本地目标或上报共享设备丢失 | 不提前发布候选。`Bar/Bar.RenderLoop.cpp:12619`，失败段；`Bar/Bar.PresentDecision.h:364`，`CompleteAttempt` |

【事实】GetDC 前退出 dirty clip 的公共入口是 `FinishFrameDrawCommands`；`EndFrameDraw` 调用真正 EndDraw 并处理遮罩缓存失败。（`Bar/Bar.Rendering.cpp:481`，`FinishFrameDrawCommands`；`:486`，`EndFrameDraw`）

### 1.2 四个分页宿主

| 时序 | 【事实】实际责任与证据 |
|---|---|
| 注册 | PageControl 为四个 HWND 分别登记输入，并为四个 Client 注册 `RenderSurface(index,frame)`。`PageControl/PageControl.cpp:6212`，`Acquire` |
| 更新内容与布局 | 离屏接线清楚展示相同内容链：ConfigurePagination、SetBounds、Advance，再取 PresentationBounds；生产提交入口为 `PresentScene`。`PageControl/PageControl.cpp:3727`，`CreatePageControlOffscreenRenderCallback`；`:1496`，`PresentScene` |
| 绘制 | `PresentScene` 确保资源，BeginFrameDraw，再调用 scene.Render；`BarPageSurface::Render` 接受 DC，绘制背景、菜单、主题及分页按钮。`PageControl/PageControl.cpp:1581`，`PresentScene`；`Bar/Bar.Scene.cpp:1080`，`Render` |
| 像素提交 | FinishFrameDrawCommands → 呈现租约 → GetDC → ULW/sink → ReleaseDC → EndFrameDraw。`PageControl/PageControl.cpp:1624`、`:1643`、`:1669`、`:1678`、`:1683`，`PresentScene` |
| 原生候选 | ULW 已携带几何；随后发布 `applyBounds=false`、`verifyBounds=true` 的候选，捕获 sceneSerial。`PageControl/PageControl.cpp:2801`，`RenderSurface` |
| 回执与快照 | 核对 revision、frameSerial、workspaceRevision、HWND、generation、visible、bounds 和 presentationRevision，才调用 `CommitPresentationCandidate`、发布 InputPresentation。`PageControl/PageControl.cpp:2374`、`:2400`、`:2417`，原生回执消费段 |
| 失败 | 像素失败返回 Retry/DeviceLost；回执不符保留强制完整提交，不确认新命中。`PageControl/PageControl.cpp:1696`，`PresentScene`；`:2384`，回执拒绝段 |

【事实】分页内容绘制已与 ULW 调用分开，但 `BarPageSurface` 同时仍提供资源、renderer、GDI interop 和呈现候选接口。（`Bar/Bar.Scene.cppm:149`，`BarPageSurface`；`:155`，候选接口）

### 1.3 SelectionOverlay 与形状编辑

| 时序 | 【事实】实际责任与证据 |
|---|---|
| 宿主准备 | `SelectionOverlay::Render` 读取 Draw3 快照、显示器工作区和主栏窗口矩形，调用 Configure、Advance。`SelectionOverlay/SelectionOverlay.cpp:85`，`Render` |
| 隐藏分支 | 不可见或最小化时请求 Hide，然后 `CommitPresented(false)`。`SelectionOverlay/SelectionOverlay.cpp:99`，`Render` |
| 绘制与像素提交 | 确保资源后调用 `ShapeEditorSurface::Present`；Present 内部 BeginFrameDraw、`Impl::Draw`、Finish、GetDC、sink、ReleaseDC、EndFrameDraw。`Bar/Bar.ShapeEditor.cpp:2266`，`Present` |
| 原生完成 | Present 成功后执行 SetBounds、Show，二者成功才 `CommitPresented()`。`SelectionOverlay/SelectionOverlay.cpp:108`，`Render` |
| 失败 | Present 失败返回 Retry；D2DERR_RECREATE_TARGET 释放资源并返回 DeviceLost。`SelectionOverlay/SelectionOverlay.cpp:106`，`Render` |

【事实】`ShapeEditorSurface::Draw` 已独立存在，但无 DC 参数，委托自有 Impl；Present 仍同时承担绘制生命周期和 GDI 提交。CommitPresented 是另一独立调用。（`Bar/Bar.ShapeEditor.cpp:2261`，`Draw`；`:2266`，`Present`；`:2304`，`CommitPresented`）

【推测】隐藏分支没有检查 Hide 的返回结果便禁用命中，和正常分支“原生操作成功后再提交”的严格程度不同；是否会造成可见内容与输入短暂不一致需运行确认。（`SelectionOverlay/SelectionOverlay.cpp:101`，`Render`）

### 1.4 白板背景

【事实】Whiteboard 持有 `backgroundScene`，配置为背景场景；RenderBackgroundOnce 确保资源，调用 scene.Render，再经 GetDC、`SubmitFreezeSurface`、ReleaseDC、EndDraw 提交 Freeze。失败会失效并释放目标；成功像素先形成 BackgroundSubmission，等待原生候选回执后才提交 scene、消费 damage。（`Whiteboard/Whiteboard.cpp:77`，`backgroundScene`；`:223`，背景配置；`:438`、`:541`、`:575`、`:583`，`RenderBackgroundOnce`；`:472`，回执成功段；`:631`，`BackgroundSubmission`）

## 2. 目标资源与设备所有权

| 对象 | 【事实】所有权、创建及重建条件 |
|---|---|
| 主栏 renderer | `BarUISetClass::spec` 是成员实例。`Bar/Bar.Main.cppm:895`，`spec` |
| 分页 renderer | 每个 `BarPageSurface::Impl` 自含 renderer；PageControl 持有四个 SurfaceState。`Bar/Bar.Scene.cpp:139`，`renderer`；`PageControl/PageControl.cpp:386`，`surfaces` |
| 编辑器 renderer | `ShapeEditorSurface::Impl` 自含 renderer。`Bar/Bar.ShapeEditor.cpp:583`，`Impl` |
| context、bitmap、interop | 都是 `BarUIRendering` 的 ComPtr 成员；由 epoch.d2dDevice 创建新 context，再创建 TARGET\|GDI_COMPATIBLE、BGRA8 premultiplied bitmap，最后查询 GDI interop。三项成功才替换旧资源。`Bar/Bar.Rendering.cppm:425`，资源成员；`Bar/Bar.Rendering.cpp:324`，`RecreateDeviceResources` |
| 重建条件 | Ensure 比较 epoch.generation、三项资源是否存在及目标像素宽高；该条件没有独立 DPI 字段。DPI 通过布局倍率、目标尺寸和字体策略影响上层。`Bar/Bar.Rendering.cpp:312`，`EnsureDeviceResources`；`Bar/Bar.RenderLoop.cpp:1251`，`ApplyDisplayTransition` |
| 丢弃 | SetTarget(nullptr)，释放 interop/bitmap/context，generation 和尺寸归零，并清设备缓存。`Bar/Bar.Rendering.cpp:361`，`DiscardDeviceResources` |
| 共享底层 | DeviceEpoch 持有 D3D、immediate context、DXGI、D2D device；SharedAssets 持有 D2D factory、DWrite factory、fontCollection。`RenderPipeline/RenderPipeline.cppm:41`，`DeviceEpoch`；`:54`，`SharedAssets` |
| 工厂来源 | renderer 的 SharedD2DFactory、SharedDWriteFactory、SharedFontCollection 直接转发 RenderPipeline。`Bar/Bar.Rendering.cpp:124`，三个共享访问器 |

【事实】不绑定 HWND 即可取得共享设备并创建目标：RenderPipeline::Initialize 不接收 HWND，GetDeviceEpoch 返回设备；renderer.EnsureDeviceResources 只需要 epoch 和尺寸。既有测试已直接这样调用。（`RenderPipeline/RenderPipeline.cpp:1132`，`Initialize`；`RenderPipeline/RenderPipeline.cppm:341`，`GetDeviceEpoch`；`Bar/Bar.EraserAttribute.Test.cpp:211`，`RunEraserAttributeOffscreenTest`）

【事实】当前 renderer 的上述目标创建路径固定包含 GDI_COMPATIBLE；测试读回另外创建 CPU_READ|CANNOT_DRAW 位图，再 CopyFromBitmap、Map。读回位图不是内容绘制目标。（`Bar/Bar.Rendering.cpp:336`，`RecreateDeviceResources`；`Bar/Bar.EraserAttribute.Test.cpp:3008`，`drawPageSurface`）

## 3. RenderPipeline 的固定接入点

| 项目 | 【事实】当前定义 |
|---|---|
| Client | Bar、StartupPreview、PptBottomLeft/Right、PptMiddleLeft/Right、Settings、WhiteboardFreeze、SelectionOverlay、Count。`RenderPipeline/RenderPipeline.cppm:68`，`Client` |
| WindowRole | MagnifierHost/Child、Freeze、DrawpadPresentation、Drawpad、四个 Ppt 角色、Bar、Setting、SelectionOverlay、DisplayObserver、Count。`Inkeys/Inkeys/Window/Window.cppm:24`，`WindowRole` |
| 回调与注册 | `RenderCallback` 为接收 `const FrameContext&`、返回 FrameResult 的函数；Register/Unregister 管理槽位，Request(client/mask) 唤醒，PostControl 投递渲染线程任务。`RenderPipeline/RenderPipeline.cppm:179`，`RenderCallback`；`:350`，全局入口 |
| 时钟与节拍 | FrameContext 携带 epoch、assets、frameTime；SetFrameRate 仅把 30 保留为30，其余归60；动画时钟 idle 后首次 dt=0，活动 dt 最大50ms。`RenderPipeline/RenderPipeline.cppm:61`，`FrameContext`；`RenderPipeline/RenderPipeline.cpp:620`，`SetFrameRate`；`Bar/Bar.FramePacing.cppm:100`，`Tick` |
| 设备恢复 | RecoverDevice 按当前 backend 创建新 generation，并整体发布 epoch；Initialize 将恢复函数交给 Scheduler。`RenderPipeline/RenderPipeline.cpp:254`，`RecoverDevice`；`:1163`，`Initialize` |
| 后端选择 | Initialize 默认 Warp；CreateRenderDevice 在 Hardware/Warp 间选 D3D driver，11.1/11.0 被 E_INVALIDARG 拒绝时仅重试11.0；另有 PrepareBackend/CommitPreparedBackend。`RenderPipeline/RenderPipeline.cpp:194`，`CreateRenderDevice`；`:1152`，`Initialize`；`RenderPipeline/RenderPipeline.cppm:347`，后端入口 |

【事实】若增加独立 Client 槽，已确认的固定接点包括 Client 枚举、DispatchOrder、诊断 clientNames；callback、generation、活动计数及诊断数组按 Count 定长，AllClientMask 也按 Count 生成。PptPageMask/WhiteboardMask 是显式产品分组，不能自动解释为所有 UI 目标。（`RenderPipeline/RenderPipeline.cppm:68`，`Client`；`RenderPipeline/RenderPipeline.cpp:54`，`DispatchOrder`；`:108`，`FormatDiagnostics`；`:591`，`Scheduler::Impl`；`:71`，`AllClientMask`；`RenderPipeline/RenderPipeline.cppm:253`，分组 mask）

【事实】注册和 FrameContext 没有 HWND 参数；现有无窗口基准复用 Client::Bar 及四个分页槽。WindowRole 则对应原生 Handle、SurfaceSnapshot、呈现租约和候选回执，当前没有 Offscreen 角色。（`RenderPipeline/RenderPipeline.cppm:312`，`Register`；`Bar/Bar.MultiTarget.Test.cpp:301`、`:384`，`MeasureRun`；`Inkeys/Inkeys/Window/Window.cppm:323`、`:346`，`Service`）

【推测】无窗口绘制本身不必增加 WindowRole；是否需要新增 Client 取决于后续是否要求其与现有主栏同时独立调度。现有函数形态只能证明两者未强制绑定，不能替代该范围决定。（同上 `Register`、`Service`）

## 4. 主栏内容阶段的窗口依赖

| 输入/依赖 | 【事实】读取位置及用途 | 分类 |
|---|---|---|
| 显示器 bounds/workArea/DPI/uiBaseScale | PendingDisplaySnapshot 驱动显示转换、monitorOrigin 和倍率。`Bar/Bar.RenderLoop.cpp:1251`，`ApplyDisplayTransition` | 内容布局所需环境输入 |
| barWindow.w/h | FineDial popup 钳边；调试文字放置；layoutBounds/capacity 计算。`Bar/Bar.RenderLoop.cpp:5674`，`SubmitTargetsAndLayout`；`:8735`、`:8769`，`CalculateDirtyAndDrawPresent` | 布局可用范围，并非仅提交尺寸 |
| 屏幕原点、直拖位移 | 构成光照输入和跨表面共享光源屏幕坐标。`Bar/Bar.RenderLoop.cpp:7402`、`:7422`，`PrepareLightingAndDemand` | 光照映射 |
| 工作区与屏幕坐标 | 属性面板 Advance 接收工作区、屏幕原点；底栏吸附与居中使用显示器端点。`Bar/Bar.RenderLoop.cpp:7294`，`AdvanceAnimationsAndDeriveLayout`；`:12952`，`RenderFrame` | 布局、避让、手势连续性 |
| MonitorFromPoint | 工作区进入时保存桌面基线显示器。`Bar/Bar.RenderLoop.cpp:13023`，`RenderFrame` | 场景恢复策略 |
| 光效 outset、任务栏保护 | 参与包络、viewport 和 source 计算；提交前再次检查窗口未越保护边缘。`Bar/Bar.RenderLoop.cpp:8922`、`:9277`、`:12203`，`CalculateDirtyAndDrawPresent` | 绘制包络与提交共同需要 |
| capacityOrigin | 参与 D2D 基础变换及 source 映射。`Bar/Bar.RenderLoop.cpp:9531`，绘制变换段；`.trellis/spec/native-desktop/rendering-and-ui.md:739`，“基于变化的脏区事务合同” | 绘制目标映射 |
| floating_window | 本文件中实际使用位于租约身份核对及 ULW 调用。`Bar/Bar.RenderLoop.cpp:12116`、`:12231`，`CalculateDirtyAndDrawPresent` | 原生提交 |
| 成功窗口/命中坐标 | 成功后发布窗口边界、monitorOrigin、按钮命中和底栏映射。`Bar/Bar.RenderLoop.cpp:12337`、`:12426`、`:12525`，成功发布段 | 呈现确认与输入 |
| 主栏矩形 | 成功后取 owner.barWindow 矩形检测编辑器避让变化。`Bar/Bar.RenderLoop.cpp:12529`，成功发布段 | 跨场景避让通知 |

【事实】`Bar.RenderLoop.cpp` 的相关内容阶段主要消费缓存显示快照和布局几何；SelectionOverlay 另有直接 GetWindowRect(Bar) 的读取，不能把后者算成 RenderLoop 内调用。（`Bar/Bar.RenderLoop.cpp:1251`，`ApplyDisplayTransition`；`SelectionOverlay/SelectionOverlay.cpp:92`，`Render`）

`barUISet` 中已确认的窗口/呈现绑定成员如下，均为【事实】：

| 成员组 | 证据 |
|---|---|
| barWindow、spec | `Bar/Bar.Main.cppm:892`，`BarUISetClass` |
| candidate/presented 按钮与视觉命中表 | `Bar/Bar.Main.cppm:1293`，`presentedButtonHits_` 等 |
| 原生鼠标光注册、屏幕点、可见区域与计时状态 | `Bar/Bar.Main.cppm:1399`，`borderCursor*` |
| 直拖锁、阶段、位移，committedWindowScreenBounds/Handle/Generation/PresentationRevision | `Bar/Bar.Main.cppm:1418`，`directWindow*`、`committedWindow*` |
| NativeDirectMove 及回执 revision | `Bar/Bar.Main.cppm:1430`，`NativeDirectMove` |
| Display 订阅、待发布 bounds/workArea/DPI、已呈现 monitorOrigin | `Bar/Bar.Main.cppm:1445`，`displaySubscription`、`pendingDisplay*` |
| 底栏已呈现映射 serial、模式、弹性与几何快照 | `Bar/Bar.Main.cppm:1475`，`bottomDockPresented*` |

## 5. 输入接入与“一窗一个表面”

| 项目 | 【事实】当前机制 |
|---|---|
| 表面事件 | SurfaceInputEvent 同时携带 HWND、generation、presentedRevision、semanticRevision、captureTicket、触点来源及坐标。`Bar/Bar.InteractionWorker.h:183`，`SurfaceInputEvent` |
| 注册 | RegisterInteractionSurface 接收 HWND+generation、handler/tick/cancel/context，支持 nativeWindow=false；同 HWND 新注册会把旧注册设为 inactive。`Bar/Bar.InteractionWorker.h:214`，注册入口；`Bar/Bar.Interaction.cpp:1949`，实现 |
| 邮箱 | 默认容量512；仅合并相邻且准入条件匹配的 Move；溢出清队列并建立取消屏障。`Bar/Bar.InteractionWorker.h:78`，`InteractionMailbox` |
| 投递/消费 | SubmitSurfaceInput 核对活动注册及代次；PumpSharedInteraction 每批最多取32条，分别派发表面事件与主栏协程消息；溢出取消已准入捕获。`Bar/Bar.Interaction.cpp:2020`，`SubmitSurfaceInput`；`:1688`，`PumpSharedInteraction` |
| 卸载 | 注销不在 HWND owner 等待 worker；另以 DrainInteractionSurfaceCallbacks 等待在途回调退出。`Bar/Bar.Interaction.cpp:1970`，`UnregisterInteractionSurface`；`:1985`，`DrainInteractionSurfaceCallbacks` |

三类宿主的输入接线均为【事实】：

- **主栏**：IdtMain 的 messageCallback 调 `QueueWindowMessageInLayoutSpace`；窗口线程固化屏幕点、转换 layout 坐标、建立捕获票据，进入共享邮箱的 `forMain` 路径。生产主栏不是照搬分页的 RegisterInteractionSurface handler。（`Inkeys/IdtMain.cpp:3784`，`AddOverlayWindow`；`Bar/Bar.Interaction.cpp:10174`，`QueueWindowMessageInLayoutSpace`；`:1094`，`SharedInputEnvelope`）
- **分页**：四窗由 Ppt::WindowProc 转到 PageControl；原生适配器捕获成功 scene 命中、semanticRevision 与票据，SubmitSurfaceInput；worker 执行 PageControlInputEvent。（`Ppt/Ppt.cpp:307`，`WindowProc`；`PageControl/PageControl.cpp:3391`，原生入队段；`:2868`，`PageControlInputEvent`）
- **SelectionOverlay**：自有 WindowProc 和原生接触状态；入队携带 PresentedGeneration；worker Input 再调用 ShapeEditorSurface::Pointer/Contact。（`SelectionOverlay/SelectionOverlay.cpp:187`，原生接触入队段；`:115`，`Input`；`:294`，`Acquire`）

【事实】“一窗一个表面”体现在：注册按 HWND 替换、注销按 HWND 清理；PageControl 用四个固定 SurfaceState/InputPresentation 槽；Window Service 用 WindowRole 定长存放 WindowRecord 和 SurfaceSnapshot。事件有按钮身份，但没有独立于 HWND 的通用内容实例身份。（`Bar/Bar.Interaction.cpp:1959`、`:1975`，注册/注销；`PageControl/PageControl.cpp:386`、`:426`，固定数组；`Inkeys/Inkeys/Window/Window.cpp:4741`、`:4749`，`Service::Impl`；`Bar/Bar.InteractionWorker.h:183`，`SurfaceInputEvent`）

【事实】命中读取最近成功版本：主栏把按钮命中和底栏映射放在同一 serial 发布；分页 SamplePointer 读取 committed；编辑器 Hit 读取 presented。（`Bar/Bar.RenderLoop.cpp:12525`，成功发布段；`Bar/Bar.Scene.cpp:1037`，`SamplePointer`；`Bar/Bar.ShapeEditor.cpp:2314`，`Hit`）

## 6. 候选与已呈现状态

| 对象 | 【事实】提交机制及失败处理 |
|---|---|
| 主栏 | BarPresentDecision、dirty tracker、viewport/mapping、按钮命中统一在四 API 成功分支推进；失败 RetainForRetry。`Bar/Bar.RenderLoop.cpp:12321`、`:12619` |
| BarPageSurface | 普通 CommitPresented 复制 candidate；异步路径 CapturePresentationCandidate 固化候选和 damageRevision，CommitPresentationCandidate 只确认对应 serial，捕获后的新 damage 保留。`Bar/Bar.Scene.cpp:986`、`:993`、`:1004` |
| ShapeEditorSurface | CommitPresented 在 inputMutex 内复制 candidate、递增 presentationGeneration；调用方仅在成功路径调用。`Bar/Bar.ShapeEditor.cpp:2304`；`SelectionOverlay/SelectionOverlay.cpp:110`，`Render` |
| ConstructionPrompt | CommitPresented 将 frame_ 复制到 presented_；输入复制 presented_ 后判断。`Bar/Bar.ConstructionPrompt.cpp:278`，`CommitPresented`；`:284`，输入段 |
| 图形/橡皮/撤回属性 | 各有 frame_/presented_ 和锁保护的 CommitPresented；主栏成功分支统一调用。`Bar/Bar.GeometryAttribute.cpp:969`；`Bar/Bar.EraserAttribute.cpp:481`；`Bar/Bar.RecallAttribute.cpp:287`；`Bar/Bar.RenderLoop.cpp:12527` |
| 呈现透明度 | PresentationAlphaState 分 requested/attempted/committed；失败保留旧 alpha 并要求重试。`Bar/Bar.PresentationAlpha.h:34`，`BeginAttempt`；`:46`，`CompleteAttempt` |
| 有限场景探针 | Ui3FiniteObserver::CompleteAttempt 对失败仅保留 Pending；成功仍核候选、epoch、surface、attempt、尺寸和语义身份。`Bar/Bar.PresentationProbe.cpp:1333`，`CompleteAttempt` |

【事实】ULW 成功而后续失败时，`BarWindowPresentationTransaction::WindowUpdated` 已使 boundsReady=false，只有最终 Commit 才恢复；旧缓存不能继续作为安全直移依据。（`Bar/Bar.WindowGeometry.h:13`，`BarWindowPresentationTransaction`；`Bar/Bar.RenderLoop.cpp:12236`、`:12544`，事务调用）

## 7. 测试入口机制

### 7.1 主工程内入口与输出

【事实】四份指定测试源均为主工程无条件 ClCompile 项；harness 默认 Debug 开、其他配置关，可由 EnableUi3PresentationHarness 覆盖。该宏主要控制受鉴权夹具及部分专项入口，不包住 Geometry/Eraser/MultiTarget 的基本 runner。（`Inkeys/Inkeys.vcxproj:84`，harness 属性；`:1053`、`:1058`、`:1061`、`:1070`，ClCompile 项）

| 文件/入口 | 【事实】触发与输出协议 |
|---|---|
| MultiTarget：RunUi3MultiTargetOffscreenBenchmark | `Inkeys.exe --ui3-multitarget-benchmark`，IdtMain:2037 分发；stderr 每组输出 failures，CSV/JSON 写 `TestResults/ui3-shared-runtime`；成功0、失败1、COM失败2、管线失败3。`Bar/Bar.MultiTarget.Test.cpp:495`、`:503`、`:515`、`:545` |
| MultiTarget 其他入口 | `--ui3-workspace-centering-test`、`--ui3-bottom-dock-transition-test`；对应 RunUi3WorkspaceCenteringTest/RunUi3BottomDockTransitionTest，完整专项受 harness 控制，关闭时返回2。`Inkeys/IdtMain.cpp:2040`；`Bar/Bar.MultiTarget.Test.cpp:2222`、`:2267`、`:2269` |
| Geometry：RunGeometryAttributeOffscreenTest | `--ui3-geometry-attribute-test`，要求仅该参数；结果写 `Build/shape-p2-ui/results.log`，末尾 stderr 输出 checks/frames/failures；返回0/1/2/3。`Inkeys/IdtMain.cpp:1892`；`Bar/Bar.GeometryAttribute.Test.cpp:193`、`:208`、`:1815` |
| Eraser：RunEraserAttributeOffscreenTest | `--bar-eraser-offscreen-test`；结果写 `Build/eraser-b/offscreen-results.log`，失败记录带 `[EraserVisual] FAIL`，末尾 failures；返回0/1/2/3；还调用 PageControl::RunOffscreenTests。`Inkeys/IdtMain.cpp:2033`；`Bar/Bar.EraserAttribute.Test.cpp:191`、`:204`、`:3151`、`:3238` |
| Presentation：RunAuthorizedPresentationFixture | 由最早的 TryRunUi3PresentationFixtureEarly 接入，不直接按普通单参数调用。公开父入口为 `--ui3-presentation-benchmark --scene main-fold\|draw-attribute --round 1..3 --capture on\|off --capacity N`；另有 `--ui3-fixture-auth-tests`。`Inkeys/IdtMain.cpp:1696`；`Inkeys/Inkeys/Helper/Ui3PresentationFixtureAuth.cpp:37`、`:461`、`:836` |
| Presentation 协议 | capability/共享 packet 发布 Passed/Failed、完成步数及 Sealed；runner 返回0/90，未授权91；参数或禁用 harness 拒绝81。生成 source-events、原始采样及功能证据文件。`Bar/Bar.Presentation.Test.cpp:1412`、`:1444`；`Inkeys/Inkeys/Helper/Ui3PresentationFixtureAuth.cpp:40`、`:854`；`Bar/Bar.Presentation.Test.cpp:1189` |

【事实】这些入口在普通产品窗口初始化前返回；没有统一的“stdout PASS/FAIL”协议，必须结合各 runner 的退出码和结果文件判断。（`Inkeys/IdtMain.cpp:1694`，`wWinMain`；`:1886`、`:2033`，专项分发；上述各 runner 输出段）

### 7.2 窗口、设备与交互会话要求

| 测试 | 分类与证据 |
|---|---|
| MultiTarget、Geometry、Eraser | 【事实】直接初始化共享 WARP/D2D，未调用主栏窗口初始化；使用真实目标及替代提交 sink。可由 CLI 自动执行，仍会写结果文件。`Bar/Bar.MultiTarget.Test.cpp:212`、`:249`、`:503`；`Bar/Bar.GeometryAttribute.Test.cpp:195`、`:947`；`Bar/Bar.EraserAttribute.Test.cpp:193` |
| Presentation | 【事实】Bootstrap 创建真实 layered Bar HWND，赋给 floating_window，再调用 InitializeWindow、Rendering、Interact；需要真实 USER32 窗口环境。`Bar/Bar.Presentation.Test.cpp:771`、`:785`、`:797`、`:823` |
| 硬件 GPU | 【事实】上述共享管线默认 WARP，不要求选择 Hardware backend；但仍实际创建 D3D/D2D 设备，不是纯 CPU 数学桩。`RenderPipeline/RenderPipeline.cpp:1152`，`Initialize` |
| 非交互会话 | 【待测】无窗口 runner 在服务/session 0、远程会话或受限桌面中的可运行性，本次没有验证；“没有 HWND”不能替代设备初始化成功证据。`Bar/Bar.MultiTarget.Test.cpp:508`，初始化失败退出；`Bar/Bar.GeometryAttribute.Test.cpp:198`，同类门禁 |

### 7.3 “多目标”已经覆盖什么、还缺什么证据

【事实】基准是同一 Scheduler 调度主栏加四个分页回调；Main 负载仅主栏，PPT 负载四个分页可见，Whiteboard 负载两个底部分页可见。每组120预热+600活动帧，100%/150%/200%，三轮；检查成功提交数、倍率、同批 frameTime、掉样、主栏可见状态、剔除计数和 idle 多余回调。（`Bar/Bar.MultiTarget.Test.cpp:46`，常量；`:301`、`:360`，`MeasureRun`；`:436`、`:474`，校验；`:537`，runner 矩阵）

【事实】输入经过真实 mailbox/dispatcher，但主栏用 MainInputFeedback 适配器，分页读取真实已呈现 scene 命中；不是完整主栏原生 WndProc/复杂手势的端到端测试。（`Bar/Bar.MultiTarget.Test.cpp:57`，`MainInputFeedback`；`:278`，`MeasureRun`；`PageControl/PageControl.cpp:3716`，离屏输入适配）

【事实】“画入无窗口位图”已经发生；默认 benchmark 缺的是像素内容校验，而且仍使用全局 barUISet、自有 GDI-compatible renderer 和 ULW 形态 sink。它尚未证明 D2 要求的资源/帧所有权拆分，也没有证明两个主栏实例同时独立存在。（`Bar/Bar.MultiTarget.Test.cpp:108`，`ReceivePresentation`；`:208`，`MeasureRun`；`Bar/Bar.RenderLoop.cpp:13298`，离屏回调说明；`Bar/Bar.Rendering.cpp:324`，资源创建）

【事实】同文件其他几何专项已有 `SaveTransitionPixels`，通过源 HDC 裁剪复制并导出 BGRA；受 `INKEYS_UI3_TRANSITION_PIXELS` 门控，未进入默认性能采样。（`Bar/Bar.MultiTarget.Test.cpp:116`，`SaveTransitionPixels`；`:1679`，转场夹具 sink）

### 7.4 没有主栏窗口时的场景夹具

【事实】PageControl 离屏回调私有持有 SurfaceState，以 nullptr HWND 调生产 PresentScene；四 API 成功后直接 CommitPresented、ConsumeDamage，不走 Window Service 回执。（`PageControl/PageControl.cpp:3685`，`Fixture`；`:3748`，`CreatePageControlOffscreenRenderCallback`）

【事实】`--ui3-shape-editor-test` 调 `RunShapeEditorOffscreenTest`，入口位于生产 ShapeEditor 文件。夹具构造真实 ShapeUiSnapshot、对象和 handles，传入模拟工作区及空主栏矩形；sink 接收真实 DC/尺寸，EndDraw 后读回像素，再由测试调用 CommitPresented。`expect(...)` 属此 runner，不是普通启动时自动执行。（`Inkeys/IdtMain.cpp:1886`；`Bar/Bar.ShapeEditor.cpp:2647`，runner；`:2925`、`:2945`，场景构造；`:3008`、`:3015`，sink/Present/readback）

【事实】该 runner 输出 `Build/shape-p34-ui/results.log`，stderr 汇总 scenes/commits/checks/failures，成功0、断言失败1、COM失败2、管线失败3。（`Bar/Bar.ShapeEditor.cpp:2651`、`:2660`、`:4465`，`RunShapeEditorOffscreenTest`）

### 7.5 独立测试工程与耗时

| 工程 | 【事实】归属、直接编译内容与运行入口 |
|---|---|
| InkeysHeadlessTests/InkeysHeadlessTests.vcxproj | 在 InkeysRepo.sln；输出 `Build/<Platform>/<Configuration>/InkeysHeadlessTests`。`InkeysRepo.sln:12`，Project；工程`:65`，OutDir/TargetName |
| 同上 UI/Log 清单 | Bar.Animation、Metrics、SurfaceLayout、Scene接口、FramePacing、ToggleClickCoalescer、WakeSignal；RenderPipeline实现；StartupPreview接口/实现；Ppt/Freeze/PageControl/Whiteboard接口；MessageBox接口、实现、Layout、Window；PresentationProbe、Presentation.Source；Setting.EraserDemoGpu、Controls、Shell、Pages；Eraser/Recall布局与动效、RecallChrome；Log.cpp。`InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:97`、`:118`、`:139`、`:157`、`:163`、`:236`，ClCompile 项 |
| 同上运行 | `InkeysHeadlessTests.exe --no-window` 跳过窗口测试；含真实 Scheduler 和 harness-disabled 契约；失败stderr、成功stdout，退出1/0。`InkeysHeadlessTests/animation_tests.cpp:1768`、`:1860`、`:1867`、`:1905`，main |
| inkStrokeModelerTest 与 inkStrokeModelerTestTests | 在独立 `inkStrokeModelerTest.sln`，不在主 solution 的 Project 列表；前者为应用 demo，后者为测试工程。`inkStrokeModelerTest.sln:6`、`:8`；`InkeysRepo.sln:6`，Project 列表 |
| demo 测试编译范围 | 测试工程直接编译 demo draw3 与部分生产 Drawing/Draw3 源；其 ClCompile 清单没有生产 UI/、Log/源。默认 runner 包含 ThinStrokeGpuTests，不能当严格无窗口 UI3 测试。`inkStrokeModelerTestTests/inkStrokeModelerTestTests.vcxproj:106`、`:123`、`:135`，ClCompile；`inkStrokeModelerTestTests/contact_input_tests.cpp:3651`，wmain |
| demo 运行 | `inkStrokeModelerTestTests.exe` 默认执行测试；成功stdout、失败stderr，返回0/1；runtime benchmark 会另起 demo 并传 `--metrics-output`、`--strict-metrics`。`inkStrokeModelerTestTests/contact_input_tests.cpp:3613`、`:3652`；`inkStrokeModelerTestTests/runtime_benchmark.cpp:118`，`RunRuntimeBenchmark` |

【推测】完整 MultiTarget 矩阵仅按720帧×27组、60FPS计算已约5.4分钟，另有 idle 观察及初始化开销；这不是实测典型耗时。（`Bar/Bar.MultiTarget.Test.cpp:46`、`:537`、`:421`，基准常量与循环）

【待测】独立 headless、demo 测试及其他离屏 runner 的本机典型耗时，本次未运行，未确认。（对应上述 main/runner 入口）

## 8. 日志、失败诊断与规范约束

### 8.1 当前日志与聚合实现

| 路径 | 【事实】已有记录 |
|---|---|
| 主栏资源失败 | 每 epoch 首次失败以 TryFormat/Error、Module::Bar 记录 HRESULT。`Bar/Bar.RenderLoop.cpp:9358`，资源失败段 |
| 主栏四 API 失败 | Log::Error/Module::Bar 一次记录 GetDC HRESULT、ULW布尔值、GLE、ReleaseDC HRESULT、EndDraw HRESULT；成功后复位失败 latch。`Bar/Bar.RenderLoop.cpp:12624`；`:12324`，提交段 |
| renderer 降级 | Module::Bar 的 Error 记录渐变、路径、遮罩、Gaussian、高光失败；精确遮罩失败用 Warn 回退。`Bar/Bar.Rendering.cpp:1253`、`:1535`、`:1758`、`:1873`、`:2159`、`:2798`，对应创建函数 |
| 分页 | INKEYS_PAGECONTROL_PRESENT_TRACE 门控，TryWrite/Debug/Module::PageControl；记录阶段、hr、error、session/publication/device、DPI、outset、target/backing及恢复，重复失败限频。`PageControl/PageControl.cpp:2198`，`TraceSurfaceStage` |
| SelectionOverlay | Render/Present 以 HRESULT 和 FrameResult 传递失败；该提交段没有独立 Log 调用。`SelectionOverlay/SelectionOverlay.cpp:106`，`Render`；`Bar/Bar.ShapeEditor.cpp:2297`，`Present` |
| 白板 | Freeze 提交失败失效/释放并返回 Retry/DeviceLost；另有专项纹理性能 Debug/Module::Whiteboard 诊断。`Whiteboard/Whiteboard.cpp:584`，`RenderBackgroundOnce`；`:124`，`FollowProfiler::Flush` |
| 管线聚合 | FrameDiagnostics 经 callback TLS 采集；DiagnosticsAccumulator 聚合长帧、API失败、恢复与光影计数；FormatDiagnostics 输出 `[UI3Diag]`。`RenderPipeline/RenderPipeline.cpp:42`，`CallbackDiagnosticsScope`；`:855`，回调采样；`RenderPipeline/RenderPipeline.Diagnostics.h:101`，聚合器；`RenderPipeline/RenderPipeline.cpp:106`，格式化 |
| 产品日志桥 | IdtMain 安装 sink，以 TryWrite/Warn/Module::UI3 输出；该模块当前已有实际使用者。`Inkeys/IdtMain.cpp:3143`，`wWinMain` |

【事实】聚合器默认健康静默，50ms 为长帧阈值、1秒输出窗口；失败依据实际 API/显式失败/异常/DeviceLost，Retry 本身不等于失败。（`RenderPipeline/RenderPipeline.Diagnostics.h:10`，常量；`:14`，`PresentFailed`；`:196`，`AddClient`）

【事实】主栏仍有普通 Error/Warn 热路径调用；Error 模板转 Write，而非 TryWrite。因此现状不能概括成“呈现路径日志已全部非阻塞”。（`Bar/Bar.RenderLoop.cpp:12625`；`Bar/Bar.Rendering.cpp:2159`；`Inkeys/Inkeys/Log/Log.h:103`，`Error`；`:120`，`TryFormat`）

### 8.2 后续必须遵守的 spec 条款

以下均为【事实：规范明文要求】，括号内列对应符号。

| spec 文件与小节 | 必须保持的约束及代码锚点 |
|---|---|
| `rendering-and-ui.md`“公共按钮、固定面板与多目标事务” | clip先于Clear，GetDC前弹栈；资源重建完整重画；成功后才发布命中。spec`:49`；`Bar/Bar.Rendering.cpp:470`，`BeginFrameDraw` |
| 同文件“UI3共享串行调度器合同”§3 | 单一调度/pacing、同步注销、局部target与共享设备丢失分开、恢复期间不向旧epoch回调。spec`:228`；`RenderPipeline/RenderPipeline.cpp:254`，`RecoverDevice` |
| 同文件“基于变化的脏区事务合同”§3 | 旧新damage并集；映射/容量变化整窗替换；消息入队固化坐标；四API成功才推进。spec`:727`、`:739`、`:742`；`Bar/Bar.PresentDecision.h:352`，`CompleteAttempt` |
| 同文件“共享设备、串行帧与光影缓存契约”§3 | 每目标独立context/target、共享epoch；单Begin/End；Win7 GetDC前退出clip/layer；ReleaseDC空修改RECT。spec`:1152`；`Bar/Bar.Rendering.cpp:324`，`RecreateDeviceResources` |
| 同文件“原生窗口候选与真实回执”§3 | 精确native ACK才提交捕获候选；不能确认后来命中或吞新damage。spec`:2662`；`Bar/Bar.Scene.cpp:1004`，`CommitPresentationCandidate` |
| `ui3-render-diagnostics.md`§3及“公共UI3多目标无窗口基准” | TLS不跨帧线程；raw固定容量、join后seal；真实提交戳与callback-end分开；无窗口结果不能证明DWM/实机手感。spec`:32`、`:95`；`RenderPipeline/RenderPipeline.Diagnostics.h:37`，`ClassifyBarCommitStamp` |
| `ui-pointer-session.md`§3及“五套独立TouchContext”§3 | 原生Down/Up、票据、来源/注册代次、取消与Raw补充分工保留；不得让旧终态释放新捕获。spec`:24`、`:57`、`:176`；`Bar/Bar.InteractionWorker.h:183`，`SurfaceInputEvent` |
| `native/quality-and-validation.md`“Required Static Review”“Windows7”“Automated Tests” | 审核早退、裁剪、提交游标；Win11 ARM64不等于Win7验证；独立demo不能替代生产呈现。spec`:3`、`:17`、`:58`、`:91`；`Bar/Bar.PresentDecision.h:145`，`IsComplete` |

## 9. 收尾事实清单

### 宿主边界必须覆盖的职责

以下从现有宿主实际行为归纳，均为【事实】，不限定接口形态：

| 职责 | 现有证据 |
|---|---|
| 目标身份、窗口代次、显示/隐藏、几何与呈现租约 | `Inkeys/Inkeys/Window/Window.cppm:201`，`WindowSurfaceState`；`:346`，`TryAcquirePresentationLease` |
| context/bitmap/interop 生命周期及epoch、尺寸重建 | `Bar/Bar.Rendering.cpp:312`，`EnsureDeviceResources` |
| 帧Begin/Clear/clip/Finish/End和最终提交结果 | `Bar/Bar.Rendering.cpp:470`，`BeginFrameDraw`；`Bar/Bar.ShapeEditor.cpp:2266`，`Present` |
| 屏幕、布局、目标像素、source/viewport/outset转换 | `Bar/Bar.SurfaceLayout.cppm:27`，`ResolveBarSurfaceScreenPoint`；`Bar/Bar.RenderLoop.cpp:12196`，提交坐标段 |
| 原生输入、捕获票据、取消、注册与排空 | `Bar/Bar.InteractionWorker.h:183`，事件/注册；`Bar/Bar.Interaction.cpp:1985`，Drain |
| 调度、失效、idle、设备恢复及同步注销 | `RenderPipeline/RenderPipeline.cppm:305`，`Scheduler` |
| 像素成功和原生回执后发布命中/业务版本、消费damage | `PageControl/PageControl.cpp:2374`、`:2400`，回执消费段 |
| 失败重试、旧成功状态保留及诊断 | `Bar/Bar.PresentDecision.h:352`，`CompleteAttempt`；`RenderPipeline/RenderPipeline.Diagnostics.h:145`，`AddClient` |

### 离屏验证可直接复用的现成部件

| 【事实】现成部件 | 证据 |
|---|---|
| 真实主栏协调器的替代提交入口 | `Bar/Bar.RenderLoop.cpp:13295`，`CreateBarOffscreenRenderCallback` |
| 无HWND共享WARP设备、字体与renderer目标 | `Bar/Bar.MultiTarget.Test.cpp:503`，runner；`Bar/Bar.Rendering.cpp:324`，资源创建 |
| 分页生产PresentScene离屏接线 | `PageControl/PageControl.cpp:3681`，`CreatePageControlOffscreenRenderCallback` |
| 独立Scheduler、raw capture和成功提交计数 | `Bar/Bar.MultiTarget.Test.cpp:285`、`:436`，`MeasureRun` |
| 无窗口输入worker与opaque HWND身份 | `Bar/Bar.InteractionWorker.h:234`，`CreateOffscreenInteractionWorker`；`Bar/Bar.MultiTarget.Test.cpp:278` |
| 主栏裁剪像素读回与失败sink夹具 | `Bar/Bar.GeometryAttribute.Test.cpp:947`，scene sink；`:958`，`capturePresented` |
| 编辑器真实Present、读回、命中和动作夹具 | `Bar/Bar.ShapeEditor.cpp:2945`、`:3015`，`RunShapeEditorOffscreenTest` |
| 纯提交、alpha、dirty、几何及探针状态机制 | `Bar/Bar.PresentDecision.h:352`；`Bar/Bar.PresentationAlpha.h:46`；`Bar/Bar.DirtyRegion.h:135`，各提交函数 |

### 迁移/封装前必须先解决的事项

| 【事实】当前未闭合事项 | 证据 |
|---|---|
| renderer仍同时拥有通用绘制、目标资源及帧生命周期，与D2要求的拆分范围重叠 | `Bar/Bar.Rendering.cppm:208`、`:425`，`BarUIRendering`；PRD`:114`，D2 |
| 三类宿主的成功边界不同：主栏四API+映射，分页再等精确ACK，编辑器再做SetBounds/Show | `Bar/Bar.RenderLoop.cpp:12321`；`PageControl/PageControl.cpp:2374`；`SelectionOverlay/SelectionOverlay.cpp:112` |
| 无窗口基准已经存在，但默认没有主栏BGRA校验，也未形成独立资源所有权证据 | `Bar/Bar.MultiTarget.Test.cpp:108`，`ReceivePresentation`；`Bar/Bar.RenderLoop.cpp:13298`，离屏入口 |
| 输入注册仍按HWND替换，固定分页/WindowRole槽不能直接视为任意同窗多内容路由 | `Bar/Bar.Interaction.cpp:1959`；`PageControl/PageControl.cpp:386`；`Inkeys/Inkeys/Window/Window.cpp:4749` |
| 呈现代码仍有普通Error/Warn调用；不能按“全部热路径非阻塞”迁移 | `Bar/Bar.RenderLoop.cpp:12625`；`Bar/Bar.Rendering.cpp:2159`；`Inkeys/Inkeys/Log/Log.h:103` |
| 工程验证范围不同：主工程runner与headless直接编译清单必须分别核对 | `Inkeys/Inkeys.vcxproj:1053`；`InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:97`，ClCompile项 |

### 未能确认的事项

- 【待测】本分支上述测试是否通过、典型运行耗时、实际资源与像素输出；本次没有执行任何 runner。（各入口见第7节）
- 【待测】无窗口测试在服务会话、受限桌面、远程会话中的设备初始化与GDI读回能力。（`Bar/Bar.MultiTarget.Test.cpp:508`，初始化门禁）
- 【待测】SelectionOverlay 的隐藏失败、原生几何操作失败及设备移除场景是否始终保持可见内容与命中一致。（`SelectionOverlay/SelectionOverlay.cpp:99`、`:106`，`Render`）
- 【待测】Windows 7 SP1+KB2670838、真实Hardware后端、ULW/DWM和主观流畅度；WARP离屏事务不能替代这些验证。（`RenderPipeline/RenderPipeline.cpp:194`，`CreateRenderDevice`；`.trellis/spec/native-desktop/ui3-render-diagnostics.md:103`，无窗口基准边界）
- 【推测】新增独立离屏Client后，除已列固定表以外还可能有测试期望需要同步；本次未将全仓所有消费者逐一穷尽，不能宣称枚举引用审计已经完成。（`RenderPipeline/RenderPipeline.cpp:54`，`DispatchOrder`；`:108`，`clientNames`；`InkeysHeadlessTests/render_scheduler_tests.cpp:1843`，固定Client测试）