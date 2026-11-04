# Research: 五个UI窗口统一 WM_TOUCH 路由的最小落地方案

- Query: 保留物理Mouse，将PPT/白板与Bar统一为WM_TOUCH业务入口并过滤全部Pointer生成兼容鼠标时，怎样按真实Down命中跨窗路由、锁定终态、去重并正确取消。
- Scope: mixed（当前源码接口、既有窗口/渲染/worker所有权、官方WindowFromPoint语义）
- Date: 2026-11-02
- Task: .trellis/tasks/11-02-shape-style-tablet-bar
- Status: 只规划；用户进一步明确五窗可各自独立Touch上下文。主会话决定采用五个本地Context，本文件据此替代此前global-active-router描述，不修改产品。

## Findings

### 最终推荐

采用 **五个窗口各自独立的TouchContext + 固定五槽native注册 + 无业务状态的跨窗分派**。共享解码、primary/fallback、去重和取消规则代码，不共享一份global active/preparing gesture FSM。入口仍为Bar/PageControl实际WndProc中的WM_TOUCH；同一次消息只在该WndProc解码和关闭一次，绝不在HiMsg callback也消费handle。

Down阶段在所有内部锁外，用真实TOUCHINPUT屏幕点调用WindowFromPoint，**只允许返回值恰好为五个已注册UI HWND之一**；然后使用该窗完整的已呈现Probe确认。非五窗、Probe Miss、身份变化或遮挡不确定一律不接纳，不继续在后面窗口搜索，不向外部应用/画布转发，不抢capture。Move/Up按(device,contact)查询五个Context自己持有的接触，只向已拥有该接触的本地Context投值型样本；绝不再WindowFromPoint或按当前悬浮窗口重选。Down锁存目标由目标Context自己保存，不另建global key→target活动表。

它不依赖WM_POINTER API，Win7可实现；不GetCursorPos猜触点，也不把HTOUCHINPUT跨窗口SendMessage/PostMessage。业务依旧进入原共享worker；原物理Mouse/可选Raw拖动入口保持。

### 1. 现有可复用接点（事实）

| 当前接口/位置 | 可复用内容 |
| --- | --- |
| IdtMain.cpp:3438/3440、3565-3578 touchRegisterFuc | 五窗已有统一created回调；RegisterTouchWindow后可按role注册native adapter |
| Window.cpp:1472、1579-1581 | created回调执行前record已存HWND/threadId；此时不受Page WM_CREATE期间Index(hwnd)尚不可查的问题影响 |
| Window.cpp:1057-1133 RunGroup；Service::OwnerThreadId(role) | Bar与四分页同一overlay owner线程，原始UI消息在此串行Dispatch；设置窗口是另一线程 |
| Bar.Interaction.cpp:1027 FreezeMainPresentedButtonHit | 既有屏幕→layout→rigid/body变换、More优先、主按钮冻结命中及前后serial核对 |
| Bar.Button.cpp:2477-2495 HitTestPresentedButton/Shape | 读取成功呈现后的hit maps，内部短锁；不能读取render动画中的几何 |
| Bar.Button.cpp:2498/2531 Prepare/PublishPresentedButtonHits；RenderLoop.cpp:12883-12888 | 准备与成功呈现后交换hit maps；可同一seqlock事务附带小型Touch占用/遮挡快照 |
| Bar.Main.cppm:102/161、EraserAttribute.cpp:464/468、RecallAttribute.cpp:304/308 | 两组件已有PresentationSnapshot/PresentedRegions，提供成功呈现的可见面板与菜单几何 |
| PageControl.cpp:379-395 CommitInputPresentationLocked/CaptureInputSnapshot(index,local) | 同inputSnapshotMutex冻结mode/visible/locked/semantic与scene.SamplePointer，不拼接两代命中 |
| PageControl.cpp:2298-2300 | 成功呈现事务提交scene与InputPresentation；保持该边界，不用配置目标矩形代替 |
| PageControl.cpp:3110-3171 归一化pointer消费者 | 真正Down检查→SetCapture→重查身份→Session→构造SurfaceInputEvent→SubmitSurfaceInput；可提取为值型目标adapter |
| Bar.Interaction.cpp:1111 EnqueueBarPointerSample | 主栏已有值型Pointer样本+capture票据+可选触觉凭据FIFO，不需转发原消息 |
| Bar.InteractionWorker.h:163/189；Interaction.cpp:1777-1860 | 保留现有worker registration/mailbox；不把它扩成native router，也不改变公平调度/Move合并合同 |

### 2. 五个独立Context、共享规则与最低协调（新增规划）

在Input层增加一个独立纯header，例如`Input/UiTouchRouting.h`，提供可被五个实例共用的纯规则/值类型；**不得在header或生产桥里藏一份gRouter.active/gRouter.preparing**。每个窗口实例持有本地接触状态，生产薄接线可落既有UiPointerInput.cpp，避免增加新的编译目标。建议概念签名：

```cpp
struct TouchKey { uintptr_t device; DWORD contact; };
struct TouchContactIdentity {
  HWND target; uint64_t targetRegistration; uint64_t contactGeneration;
};
struct TouchPacket { TOUCHINPUT input; POINT screen; DWORD messageTime; ULONGLONG arrivalTick; };
enum class TouchHit { Miss, Blocked, Accept };
struct TouchProbe { TouchHit disposition; uint64_t presentationRevision, semanticRevision; };
struct NativeTouchAdapter {
  TouchProbe (*probe)(void*, const TouchPacket&);
  optional<TouchContactIdentity> (*findOwned)(void*, TouchKey);
  bool (*begin)(void*, const TouchPacket&);
  bool (*continueOwned)(void*, const TouchPacket&, TouchContactIdentity);
  void (*cancelOwned)(void*, TouchContactIdentity, CancelReason);
};
RegisterNativeTouchTarget(slot, hwnd, registrationGeneration, adapter, staticContext);
DispatchDecodedTouchBatch(originHwnd, span<const TOUCHINPUT>, messageTime);
RequestNativeTouchCancel(TouchContactIdentity, reason);
RetireNativeTouchTarget(hwnd, targetRegistration);
```

#### 每个窗口自己拥有

- Bar一份NativeTouchContext，四个Page各一份，静态owner位置与其现有producer/pointerClients对应。
- 本地状态含 HWND/registration generation、active TouchKey、primary/fallback/按下时间、contactGeneration、Preparing/Active/Retiring阶段、capture ticket、local rejected/retired去重缓存、当前语义身份与取消意图。
- Bar和Page继续有各自worker业务状态（协程/scene/Session），通过带身份值型FIFO联系native半部；不能让两个线程同时写同一个Context对象，也不为分派层再复制第二套业务手势状态。
- sample.generation采用目标Context本地contactGeneration，目标本地BeginTouch/EndTouch使用相同值。跨窗身份是`HWND + targetRegistration + contactGeneration`，不是一个可跨窗误用的裸计数。Page现有SurfaceInputEvent.generation仍为surface lifecycle，不得复用。
- 各Context保留固定容量的retired/rejected指纹，用于重复/旧终态判定；跨窗分派可只读扫描五份缓存，但不另存一份global tombstone或active contact表。

#### 跨窗层只协调，不拥有第二个FSM

- registry仅五槽，保存HWND/注册代次/owner线程及adapter指针，不保存当前TouchKey、pressed、capture owner或active/preparing手势。
- 每个packet先按TouchKey扫描五个Context的findOwned。恰好一个owner时交给它处理（重复Down也由该Context去重），Move/Up不选新目标。
- Down没有owner时才执行WindowFromPoint+目标Probe，由目标Context自行begin；成功后后续自然能从该Context查出owner，不写global key→target映射。
- Move/Up没有owner时按各Context的终态/拒绝缓存识别并丢弃；若异常找到两个owner，不能按registry顺序任选，必须对两份匹配身份取消并拒绝该接触，记录不变量错误。
- 去重仍按同device/contact的同阶段(time+point+flags)，不同点的同毫秒Move保留。新Down后的旧Up在目标本地按按下身份/同源时间和各Context终态缓存拒绝，不能因为key相同完成新一代。

#### 必须保留的全局资源与仲裁

五份Context不代表五个并发UI点击，也不消除Win32鼠标capture及Mouse/Touch来源互斥这一进程资源。保留原单一UI接触策略、primary/fallback和竞争接触规则，不借本任务新增多指跨窗并行操作。

现有MouseChannel/provider的nonMouseActive/touchOwner是来源互斥资源记录，继续按原策略准入/释放；**不能把它当成全局Touch业务Context，也不能用它替代FindOwnedKey去决定Move/Up目标**。目标Context取得/释放自己的lease，旧origin Context不先占lease。静态registry、锁、窗口命令队列及取消邮箱同样是协调资源，不是第二个global gesture FSM。

### 3. 注册、原始入口与值型分派

1. 五窗`spec.created`在touchRegisterFuc之后注册各自native adapter。Bar注册自己静态本地Context的owner接口，Page按role显式给0..3各自Context，不依赖其WndProc中Index(hwnd)在WM_CREATE时已就绪。
2. Bar的HiMsg QueueWindowMessage callback继续只旁观/去重，**不消费WM_TOUCH**。实际Bar/Page WndProc的WM_TOUCH进入同一个薄解码入口，一次GetTouchInputInfo→保序primary优先/原fallback→无状态分派→目标本地Context→RAII CloseTouchInputHandle一次。
3. origin HWND仅为诊断信息，不作为Down业务目标。无owner的Down只用Touch坐标做native资格和准确Probe；raw额外位只能诊断，不参与Pen/Touch拆接触。
4. target adapter直接处理值型TouchPacket/Sample，目标会话标Source::Touch。不要调用目标WndProc递归，也不要把原handle封到消息里投递。
5. Page adapter提取当前归一化指针消费部分：Down前先探测有效区域；命中后按原规则取得capture和票据、重查语义/区域、Session、构造SurfaceInputEvent。Bar adapter提取PublishTouch/EnqueueBarPointerSample及本地producer收尾。两者业务入队仍交原worker。
6. PPT/白板删除按`IsPenGeneratedMouseMessage`接纳的旧兼容分支，统一按`IsPointerGeneratedMouseMessage`过滤系统副本。物理Mouse(无该签名)和其Raw可选补充不受影响。

### 4. Down唯一目标与完整Probe

#### 原生资格/遮挡

`WindowFromPoint(packet.screen)`仅在Down、overlay owner、没有Context/registry/provider/inputSnapshot/scene锁时调用。使用TOUCHINPUT原始坐标，不使用系统鼠标。返回值非五个已注册原生UI HWND或NULL直接丢弃UI路由；不把外部窗口、Setting、画布或透明包络强行转成Bar。

此API可能同线程重入WM_NCHITTEST，甚至触发Bar顶部既有pending capture释放；查询后必须重查registry代次、选中Context本地状态序号与Probe版本。返回的HWND只是原生候选，不代替准确Probe。候选Probe Miss/版本不稳直接拒绝，不在后层5窗继续搜。

- Microsoft明确WFP会跳过native-disabled窗口，且仅对同线程窗发NCHITTEST。它提供Win32原生命中语义，不是任意第三方透明/disabled视觉遮挡的绝对证明。
- 本应用可见而逻辑禁用/locked背景应为Probe Blocked，不能落到后方按钮。Page现有`ShouldAcceptPageControlClientHit`对Whiteboard locked返回HTCLIENT，但PPT locked会HTTRANSPARENT。依“可见禁用遮挡不穿透”合同，在**Touch native查询scope**下让自己的纯NCHITTEST按发布Probe返回HTCLIENT(Occupied/Blocked)/HTTRANSPARENT(Miss)，scope外仍原物理Mouse策略，避免为修Touch扩大Mouse行为变化。该scope只围住锁外WindowFromPoint，退出时恢复TLS；自己的NCHITTEST只读值型快照，不推进Context、不调用capture/业务。
- 主会话最终收敛：Setting/模态窗作为非五窗原生候选直接拒绝，并保留既有组件enabled/modal准入。不要仅凭“可见Setting矩形包含点”无条件阻挡，否则Setting在Bar背后时会拒绝合法主栏点击。本轮按Win32原生命中语义，不增强任意native-disabled/透明窗口的光学遮挡保证，也不建立桌面Z序引擎；自己的可见locked背景通过上述Touch查询scope阻挡。查询失败/身份变化保守拒绝，所有查询锁外。
- 完全alpha=0而故意扩大的触摸hit区域可能被native资格拒绝；当前主要面板有真实背景，未发现必须把非UI native候选重定向到Bar的证据。FineDial、picker和关闭按钮边缘要纳入测试，采用失败关闭而非越层猜测。

#### Bar Probe

不能只用HWND rect或MainBar背景。推荐在已有成功present的hit/maps事务附一个小值型`TouchHitSnapshot`：当前坐标mapping/serial、各显式surface/遮挡区的已呈现几何与可见/blocked状态。覆盖Main/folded按钮、More、Draw、Geometry、Eraser及菜单、Recall、选色板、笔型菜单、粗细预览/FineDial、数值/溢出浮窗、必要透明交互扩展区。

- 用现有BarButtonHitSnapshot/HitTestBarButton的圆角/超椭圆语义；panel组件从PresentationSnapshot取值。PresentedRegions只是RECT包络，不能不经圆角/真正占用判定就把整块阴影算Accept。
- render owner在Prepare阶段构造值，在RenderLoop成功呈现/PublishPresentedButtonHits/CommitPresented同一mapping seqlock奇数事务里发布；native Probe在短锁下复制并校验前后serial，释放锁后才返回/做Window查询或capture。
- 不在native线程读取正在动画的`.val`、不调用会执行业务的Panel::Pointer来探测、不遍历新节点树。路由只决定Bar是否占用/可接纳；既有冻结main-button及各属性业务仍在目标adapter/worker执行。

#### Page Probe

用`ScreenToClient(target,&packet.screenCopy)`后调用已有`CaptureInputSnapshot(index,local)`，冻结InputPresentation与BarSurfaceInputSample。visible/initialized/背景真实占用成立但locked/退场遮挡时返回Blocked；可执行按钮或允许拖动主体返回Accept，真实透明区返回Miss。Whiteboard更多菜单/主题popup的背景遮挡也必须保留，不仅previous/next hit。

SetCapture可重入：adapter在Capture后沿现有语义再取一次同源snapshot并校验target registration、semantic和命中region；失配返回Rejected并走同代取消，不提交半个Down。

### 5. 本地Context锁存与取消：不再有global route状态

- 新Down只在选中的目标Context记录Preparing(identity/key)，再执行其本地begin/capture/入队。跨窗分派器没有Preparing字段。SetCapture可重入，因而回调前不得持registry/provider/scene锁；返回后该Context复核自己的注册代次、contactGeneration及状态序号，失配就取消本代，不提交半个Down。
- Active Move/Up通过FindOwnedKey只读扫描找到拥有者，再调用continueOwned并传本地复合identity；即使系统仍把WM_TOUCH投到旧origin窗，也不改变已锁存目标。实际触点位置继续交原业务，移出取消/拖动/长按语义不变。
- 同一key重复Down不再选择新窗口；仍由原Context判重复/旧事件。另一key的新Down按原单一UI接触及primary/fallback政策准入，不能用五个实例偷偷放开并发或在悬浮时抢掉另一接触。
- Up先排该Context终态，再同代EndTouch(contactGeneration)、安全释放其capture ticket，最后本地Active转Idle并保留终态指纹。enqueue失败/真实cancel/失效只能取消，不伪造成功Up，也不以固定按住超时强制回收。
- 全局lease只由**目标Context**取得，origin窗只负责一次解码/关闭原handle；它不应因为收到旧锚定Touch而先BeginTouch或SetCapture。
- 取消条件包括WM_CANCELMODE、真实捕获丢失、该窗隐藏/销毁/注册退休、DPI/显示/语义/工作区失效、队列溢出及来源接管。它们都以该Context的复合identity为准，不全局清空其他窗的新代状态。
- worker overflow/注销原本只保证worker session和acceptedCaptureTicket收尾（Interaction.cpp:1540、CancelRegisteredInteractionSurfaces）。新增accepted native identity元数据（targetRegistration + contactGeneration），与capture票据同一准入事务固定。ticket=0也必须请求该Context取消，不可跳过producer/lease清理。
- 跨线程只写每个Context对应的有界取消意图槽，保存完整64位targetRegistration/contactGeneration/reason；PostMessage只作owner wake，不把64位identity截成Win32 WPARAM，更不发送原触摸handle。owner在wake和新输入前先drain取消。该邮箱不是global active contact表。
- 旧取消先核targetRegistration，再核该窗contactGeneration；HWND重用、计数重置或未来接触不能被旧票据/旧取消影响。Cancel/End可幂等，但不能释放别的Context持有的lease。
- owner销毁/retire时先取消这一窗的本地Context、同代client reset/EndTouch和capture安全释放，再移除registry槽。Resource Release按现有deferred drain规则请求本地owner取消；静态surface index避免依赖已释放的worker context，也不得对新lifecycle数组盲目reset。

### 6. 锁/所有权顺序

1. 五份native Context都在同一overlay owner线程推进，但状态各自属于对应窗口；registry只做注册/查找，shared helpers只有规则代码。register/retire在spec.created/owner销毁钩子执行。
2. 跨窗FindOwnedKey查询五份本地状态，不持有全局业务锁；复制descriptor/Probe后释放内部短锁，才可WindowFromPoint、GetWindowRect/ScreenToClient、SetCapture/ReleaseCapture或调用目标adapter。
3. Page Probe遵现有inputSnapshotMutex→scene已呈现采样；Bar Probe读presented值型占用snapshot并核mapping serial。不能持这些锁做原生查询/业务。
4. 目标Context的provider.BeginTouch短锁完成后释放，再capture/入队；provider仅互斥来源资源，不回调Context或窗口。worker注册锁不参与native hit/capture路径。
5. mailbox准入固定该Context身份和capture元数据；worker只维护对应窗口的业务半部并异步请求native取消，不等待owner。取消队列不是额外的Touch FSM。
6. RenderPipeline只发布已呈现命中/语义及失效意图，不直接改native Context；owner处理时复核目标注册/本地代次。五窗状态隔离不等于可以忽略共享Win32 capture和来源资源。

### 7. 必需验证

- 五Context共用规则：sticky origin Bar→Page/Left→Right、无owner的Down唯一命中、按住跨窗仍由原Context拥有、Up后可选新窗；同样本重复、同毫秒不同点、TouchId复用/旧Up、fallback/primary和竞争contact政策；每Context有界拒绝/终态缓存；注册+本地代次跨注销/重建。不能出现第二份global active状态或多Context同时拥有同key。
- native资格假接缝：返回外部/Setting/画布/NULL不路由不抢capture；自有窗口ProbeMiss/Blocked不越层；WindowFromPoint/SetCapture重入期间隐藏/取消/重注册会使被选Context的旧选择失效。
- 实际adapter/共享worker：只在Down申请lease/capture；票据0的overflow也回对应owner取消Context接触；旧cancel/旧release不得清新代或另一Context；PPT触摸长按Down/repeat与白板Up单击保留，物理Mouse/Raw原回归保持。
- Bar完整面板/More/picker/FineDial/Eraser/Recall及浮窗遮挡、Page popup/圆角透明区；已呈现Probe版本与真正业务冻结hit一致。
- 完整Debug|ARM64 Solution与无窗口测试；必要Release x64测试产物。真实数位板跨窗/OutOfRange结果仍由用户最后一轮验证，不以纯router代替GUI/驱动实测。

### External references

- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-windowfrompoint ：Win2000已支持，跳过hidden/disabled原生窗口。
- https://devblogs.microsoft.com/oldnewthing/20101230-00/?p=11873/ ：WindowFromPoint同线程使用WM_NCHITTEST，跨线程视作不透明；不能简化为纯矩形查找。
- https://devblogs.microsoft.com/oldnewthing/20110218-00/?p=11453/ ：命中查询会触发WM_NCHITTEST重入，不表示实际鼠标进入，不应在查询里起业务。

### Related specs

`.trellis/spec/native-desktop/ui-pointer-session.md`、`cpp-conventions.md`、`whiteboard.md`；当前source隔离/精确capture ticket/worker公平和失败取消合同继续适用。原diagnostics-plan禁止WindowFromPoint针对旁路日志，本功能Down native资格允许锁外只读查询，范围应由主计划明确替代。

## Caveats / Not Found

- 这是已经选择的最小架构，不是一组未收敛后端选项。没有产品代码、工程/spec或Git修改，没有GUI/build。
- Bar目前没有完整的单次占用Probe；必须补成功present事务下的小值型snapshot，否则只拿MainBar/窗口rect会漏属性浮窗或把阴影算控件。它是实施中必要工作，不是以后靠日志决定。
- WindowService现有generation在private record内，native target registration应自行发独立代次并绑定HWND，或以最小只读identity接口取出；不能只认可重用HWND。本文推荐native registry自己的单调注册代次，不扩大Service公开API。
- 严格WindowFromPoint候选遵Win32原生资格，不承诺任意第三方disabled/透明视觉遮挡的像素级证明；已知Setting/模态阻挡和自有软件locked面板必须有明确拒绝合同。
- 每Context取消意图槽与注册/本地代次复合identity必须在主计划中落成，不能仅写“继续原capture释放”后遗漏零票据路径。

### 分解后的可落地性结论

五套本地TouchContext可以共享同一份解析和状态转换代码，并在同一owner线程串行推进。仍必须有极小的跨窗协调层：固定registry、按key扫描已有owner、无owner Down的原生资格/准确Probe、值型转交，以及跨线程取消意图。它们不保存第二套活动手势。单一UI接触、Win32 capture和Mouse/Touch互斥是原本的资源/政策约束，需要保留；不能以独立Context为由删除这些保护，也不能以保留保护为由重新放一份global business TouchContext。
