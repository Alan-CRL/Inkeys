# Draw3 集成规范

## 来源与文件边界

- 来源快照：D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3@8d045298eaaac76f752b4f8b5f3303b3520e50b7；不嵌套源.git。2026-10-12阶段1获批，仅调整Modeler/Abseil构建归属，算法及产品几何不变。
- Draw3产品源在Inkeys/Inkeys/Drawing/Draw3；Modeler运行源唯一存放于Inkeys/additional/ink_stroke_modeler；Abseil通过vcpkg同包头/库消费，Inkeys/additional/absl保留旧.0头用于回退。
- Inkeys/resource.h/Inkeys.rc保留资源301-304及现有shader嵌入链。
- 产品不编译demo入口、独立测试窗口、window_performance_hud或Abseil.cc。共享清单保留13个未修改上游Modeler运行.cc；2026-10-12阶段2授权新增独立terminal_tail_reconstructor本地扩展TU，不隐式接入产品；排除GMock type_matchers.cc和测试/fuzz源码。

## Scenario: 固定版本 Ink Stroke Modeler 与 vcpkg Abseil ABI

### 1. Scope / Trigger
修改Ink prediction、vendor源/头、共享MSBuild清单、vcpkg依赖或CRT时应用此合同。阶段1证据归10-12-modeler-source-build-baseline，配置存在不代表验收。

### 2. Signatures
- vcpkg.json精确override Abseil20250512.1#0，保留原baseline与其他版本。
- 三个static-v143 triplet仅给Abseil设置C++20/REQUIRED，MSBuild三消费者统一VcpkgConfiguration=Release。
- Modeler13个传统TU共享清单/canonical Modeler头；Abseil头与静态库来自同一vcpkg installed包，旧merge/.0头只作独立对照/整套回退。

### 3. Contracts
- active vcpkg Abseil include优先于保留的vendor .0头；实际patch版本、std string_view/ordering、lts_20250512、MT/NDEBUG/iterator0与库一致，不混用版本。
- 不手工合并/复制Abseil库或拆旧merge；记录包source/port/toolchain/flags/hash/machine/CRT/namespace，无Modeler定义。
- 业务Debug保持原调试/优化合同；仅共享清单标记的Modeler TU（13个上游源及阶段2独立扩展）保持旧MinSizeRel /O1 /Ob1、/MT、NDEBUG、C++20、/fp:precise、EH/RTTI，PCH/模块扫描关闭。运行期obj更新用逐项marker限制，不能改变业务cpp/cppm obj。
- Abseil使用vcpkg标准Release优化，须对照验收；其他依赖/SDK/toolset不升级。
- triplet文件hash参与所有包缓存，首次可重建同版本依赖；改变依赖设置前检查空间，不以cache失效为由改版本。
- 源码Modeler与旧merge不能混链；Update追加、Predict非修改、Save/Restore、参数和几何接纳保持。

### 4. Validation & Error Matrix
| 条件 | 必需结果 |
|---|---|
| package或13cc缺失 | 构建失败，不静默回旧merge |
| MTd/_DEBUG/iterator不匹配 | ABI错误，不忽略LNK2038 |
| 头与库版本/ABI不同 | 同包配套并完整Rebuild |
| 回放字段差异 | 仅声明的.0→.1依赖版本metadata可不同；语义全部bit exact |
| 可重复且超噪声退化 | 阶段1不通过，不调算法掩盖 |

### 5. Good / Base / Bad Cases
- Good：13TU独立优化、vcpkg同包Release头/库、业务flags保持，完整Rebuild/回放/性能通过。
- Base：三架构共享源码，各自对应installed包。
- Bad：vendor.0头+.1库、源码+merge混链、整工程MTd、旧obj复用。

### 6. Tests Required
- InkeysRepo.sln Debug ARM64完整Rebuild，补三架构Release；demo/test既有solution。
- 切换前独立旧EXE；--modeler-replay精确状态/计数/时间/全部Result，--modeler-benchmark 30轮交替。
- 计时在无构建干扰时执行；reset为完成Up后64次无参Reset批次均摊，不能据此推断Reset(params)/新Down初始化成本。
- 来源/实际include/flags、三架构ABI与x86/x64静态imports，旧头/库hash保持。
- Headless --no-window保持MTd且不消费Abseil。GUI/D3D/Win7真机本阶段不执行。

### 7. Wrong vs Correct
Wrong：Debug MTd + MT包，vendor旧头混新库，源码和merge同时链接。
Correct：同包Abseil头/库、MT/NDEBUG/iterator0/std别名、13TU独立优化、严格旧/新对照。

共享props中运行期metadata更新必须逐项限制 `InkStrokeModelerRuntime=true`；不要在Target内用 `ClCompile Update="@(InkStrokeModelerSource)"` 假定已限制范围，否则业务cpp/cppm可能共用obj。实际vcpkg properties/imports先于ItemDefinition求值，installed路径需用getItem及CL.read双重确认。

## 窗口与所有权

- `WindowService` 是 `WindowRole::Drawpad` 与 `WindowRole::DrawpadPresentation` HWND 的唯一创建、显示、隐藏和销毁者。Draw3 只把主 Drawpad 交给 `AttachExternal(HWND, callbacks)`；辅助窗不得绑定 RTS、WndProc mailbox、document 或第二套 Host。
- Draw3 不创建/销毁顶层窗口，不修改标题、owner 或 Z 序；样式和 `Primary/Presentation/Hidden` 可见性变化必须通过 Window Service 的 owner-thread 命令完成。
- owner 层级保持 `MagnifierHost -> Freeze -> DrawpadPresentation -> Drawpad -> PPT/Bar`，使 PPT/Bar 通过传递 Owner 关系始终高于两套画布表面。Drawpad 仍是顶层 owned popup，不是 `WS_CHILD`。Draw3 presenter 不得调用 `SetWindowPos`、`SetParent` 或直接改 owner；`SetBounds(Drawpad)` 必须同步两窗。

## Scenario: 产品 RTS 注册前置顶准备

### 1. Scope / Trigger

产品首次启动或 DComp 失败后重建 ULW HWND 链时适用。历史 Inkeys2 在 0c0aaf0e99a4 / 649de99daecc 保留根置顶先于 RTS 创建的经验合同；不将该经验写成微软 API 强制要求。

### 2. Signatures

`IdtMain.cpp` 私有 `bool PrepareDraw3WindowTopmost(Window::Service&, HWND drawpad, const char* phase)`；复用公开 `RequestTopmostRefresh()`、`Handle()`、`OverlayRoot()`，不改变 Host/Window 公共接口。

### 3. Contracts

- 每次 `StartDraw3ProductWithFallback` 的 `StartProduct` 前准备，phase 为 initial 或 rebuilt-ulw；后者只在旧 Host/窗口清理并完成新链创建后执行。
- 桌面策略必须允许 topmost 且非 Whiteboard；只通过 Window Service 同步刷新根，Mag 缺失时 Freeze 为根，不显示/激活或单独置顶画布/主栏。
- 检查 root、Freeze、DrawpadPresentation、Drawpad 的有效句柄、直接 GW_OWNER、实际 WS_EX_TOPMOST 和刷新前后身份；策略 true 和 API 成功都不能代替 HWND 事实。
- 初次准备失败立即返回，不能误进 DComp 回退；新链准备失败也不启动 Host/RTS。启动根刷新最多等待 5 秒；超时队列命令不再执行，准备失败在日志和 join 前建立既有退出监督。准备成功后才创建该次 Host cleanup scope，保留原失败监督。
- 首帧后的 Preview observer/InitialTopmostRefresh 及周期刷新、白板 NOTOPMOST 保留。日志每次准备一次，含阶段、角色、原因和 HWND/owner/exstyle/read error；服务调用错误由 owner thread 捕获，不从调用线程读取陈旧 GetLastError。

### 4. Validation & Error Matrix

root/策略/句柄/owner/实际 topmost 不符、刷新失败、读取样式失败或身份变化 → 返回 false，不调用该次 StartProduct。Mag 未创建但 Freeze 为有效无 owner 根 → 允许。成功 → 返回 true，随后才初始化 RTS。

### 5. Good / Base / Bad Cases

Good：初代及重建代都完成准备再初始化 RTS；Base：Mag 缺失仍以 Freeze 抬升整链；Bad：只在首帧后刷新，或 prepare && StartProduct 失败统一进入 DComp 回退。

### 6. Tests Required

静态核对两处 gate、回退/清理边界和无激活合同，完整 Solution Debug|ARM64 与 `InkeysHeadlessTests --no-window`。后者不验证真实 HWND/RTS/Z 序；真实窗口/笔输入和白板行为须另按授权验证，不能将编译或属性检查认作视觉证明。

### 7. Wrong vs Correct

Wrong：只等 RtsReady/首帧后刷新；Correct：每代窗口准备失败直接返回，成功后调用 StartProduct，保留后置恢复。实际 Windows/RTS 系统内部因果仍需故障现场证据。

## 设备与线程

- Draw3 独立创建 D3D11.1 设备、context、DXGI factory、交换链和呈现资源；默认 hardware-first/WARP-fallback，启动快照 EnableHardwareAcceleration=false 时直接 WARP，回退/恢复复用同一快照。禁止引用 `Inkeys.UI.RenderPipeline` 的设备。
- Hardware 与 WARP 都可落在 FL11.0；这不保证动态 SRV 的 `WRITE_NO_OVERWRITE`。除实际 Hardware adapter VendorId=0x1D17 且启动 EnableZhaoxinCompatibility=true 时大结构化资源使用 DEFAULT 局部更新外，动态路径的生产 renderer 必须在当前设备上查 `D3D11_OPTIONS::MapNoOverwriteOnDynamicBufferSRV`，不支持或查询失败时普通笔与 Shape 逐批 DISCARD 且 buffer offset 为零；支持设备保留环形路径。此上传门禁与 ULW 的 `FLIP_SEQUENTIAL` 独立，不能为修一项而替换交换链模式。
- UI 线程只向 bridge 发布不可变快照和命令；renderer、document、history、RTS 消费只在 Draw3 绘制线程进行。
- 进程只允许一套 Draw3 Host、绘制线程及选定的 RTS 或 Win32Mouse 输入 producer；Desktop、Whiteboard 及每个 `PresentationKey` 的 document/history 是该绘制线程独占的独立 slot。辅助 HWND 仅是同一最终 backbuffer 的 presentation target。退出先关闭绘制准入并预约最终保存命令，停止并排空选定输入 producer，再唤醒绘制线程、执行最终保存屏障并排空 worker、释放双 presenter/设备，最后由 Window Service 销毁两窗。
- `DrawingControllerRuntimeObserver::drawingActivityChanged` 只按全部 physical contact 的聚合值发布 `0→1/1→0`；每个命令消费边界与帧末复核，避免 Down 后提前 `continue` 漏报。Host 通过独立 `HostRuntimeCallbacks` 注入产品通知、再次去重，并在 Run 正常返回、异常或 stop/join 后补发一次 false；Draw3 核心不得直接依赖 Bar。

## 样式与透明度

- 主 Drawpad 固定不带选择语义的 `WS_EX_TRANSPARENT`；只有 `ShouldPreconfigureNoRedirectionBitmap()` 能力探测通过时才在创建前预置 `WS_EX_NOREDIRECTIONBITMAP`，随后按主 presenter 模式切换 `WS_EX_NOREDIRECTIONBITMAP`/`WS_EX_LAYERED`。
- `DrawpadPresentation` 出生即固定包含 `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`，运行期不得清除或借其承载输入。
- DComp 清除 `WS_EX_LAYERED`；ULW 设置 `WS_EX_LAYERED` 并清除 `WS_EX_NOREDIRECTIONBITMAP`。两种 DWM 实现属于禁用的历史路径。
- ULW 必须提交 premultiplied-alpha、top-down 32-bit DIB 和 dirty rect；未绘制像素的 alpha 为零，禁止整窗不透明更新遮挡下层窗口。

Windows 对创建时带 `WS_EX_NOREDIRECTIONBITMAP` 且已经绑定过 DComp target 的主 HWND 可能拒绝后续清除该位（`ERROR_INVALID_PARAMETER`）。Window Service 必须写后读取并核对真实样式。若产品 DComp 启动失败，必须在首帧显示和 Setting 初始化前停止 Draw3 与整条隐藏窗口链，再顺序重建 legacy-compatible 主 Drawpad 及其 presentation-only 前置表面；新 Host 禁用 DComp 并只尝试 ULW。两个主 Drawpad generation 不得同时存在，但同一 generation 必须包含长期待命的 presentation-only 前置表面。

## 功能边界

桥接工具固定为 Pen、HardPen、Highlighter、FixedEraser、SpeedEraser、Laser、SolidLine、DashedLine、OutlineRectangle、FilledRectangle。清屏、撤销/重做、页面切换、Desktop UInk 自动保存及当前进程内的 PPT UInk 自动保存/恢复已接入；手动保存、跨进程 PPT 恢复/冲突交互、超级恢复、自动直线拉直和输入测试仍保留 `Unsupported/NotReady` 空接口并隐藏产品入口。保留 Draw3 速度橡皮、固定橡皮及 `SpeedEraserOcController`；仅删除旧 Draw2 压感橡皮实现和设置入口。

Whiteboard 初版的独立自动保存、按页冷热状态、完整历史恢复和流式 UInk 读写见 [whiteboard.md](whiteboard.md)。其保存不读取 Desktop 开关，也不复用 PPT 整文稿常驻策略。

## Scenario: Desktop UInk 自动保存事务与退出屏障

### 1. Scope / Trigger

修改 Draw3 workspace、Desktop Clear/正常退出、`saveSetting.enable`、Desktop UInk 完整保存、Clear 后恢复、每日索引或 Host/controller/worker 生命周期时必须应用本合同。Whiteboard 与 PPT 不读取 Desktop 开关；PPT 使用下一节的独立事务。

### 2. Signatures

- `Bridge::Workspace { Desktop, Whiteboard, Presentation }`。
- `DesktopAutoSavePolicy::ShouldCapture(workspace, enabled, hasVisibleContent) -> bool` 与 `CompleteDesktopClear()`。
- `DesktopAutoSaveService::{Submit, SubmitLoad, TryTakeCompletion, CloseAndDrain}`。
- `DesktopPersistenceCompletion { operation, status, trigger, fileGuid, loadedSnapshot }`。
- `Bridge::CommandType::PrepareExitAutoSave` / `CanvasCommandType::PrepareExitAutoSave`。
- 产品根路径：`GetCurrentExeDirectory() + L"\\Inkeys\\AutoSave"`。

### 3. Contracts

- 自动保存只接受 `Desktop + saveSetting.enable + 当前页非空`。Desktop slot 与 Whiteboard/PPT slot 独立；访问 PPT 不会污染 Desktop 保存资格，也不得将 PPT 内容导出到 Desktop 目录。
- 每次有效 Desktop Clear 都替换全局最近恢复点。开关关闭、提交拒绝或保存失败时保留一份 `Draw3UInkCanvasSnapshot`；保存和索引 durable 后 completion 以 `fileGuid` 匹配恢复点并立即释放该快照，只保留 committed path locator。
- Undo 总是先撤回当前区间 Stroke；到 `undoFloor` 后，内存恢复点直接物化，磁盘恢复点经 `SubmitLoad(fileGuid)` 严格读取并在绘制线程全量 replay。成功后消费恢复点，以 `undoFloor=0` 物化恢复画布并关闭再次跨 Clear 的资格；恢复出的 Stroke 可继续逐笔 Undo 到空，之后再次 Undo no-op。
- 触发点只有 Desktop Clear 的破坏操作之前和正常退出安全点。Undo/Redo（包括未来跨 Clear 撤销）不得调用自动保存；空画布或开关关闭时不得捕获快照、入队或创建目录。
- DrawingController 线程只按 runtime history 捕获当前可见、完全自有的 CPU 快照；UInk 编码、文件和索引 I/O 由 Host 拥有的单一可 join 串行 worker 执行。worker 不得持有 controller/GPU 引用，也不得 detach。
- 布局固定为 `desktop/YYYY-MM-DD/HHmmssfff_<saveRequestId-short>[_suffix].uink` 和同目录 version 1 `index.json`。请求创建时固定 `saveRequestId`、`fileGuid`、`sessionId`、`sequenceInSession`、本地日期和带时区 `createdAt`；同一请求幂等，不同请求使用 create-new 且永不覆盖。
- UInk 必须先 durable commit 并校验身份；每日索引随后在由规范化根路径与日期派生的 named mutex 内重读、自校验并原子替换。索引失败保留孤儿 UInk 和最后有效索引，不删除未知文件。
- `saveDays` 不连接新 UInk；本合同不允许自动过期、容量裁剪、启动清理或历史删除。产品代码只可清理当前事务尚未发布的已知临时文件。
- 正常退出顺序固定为停止新命令/contact producer -> controller 存活时执行最终捕获屏障 -> 关闭提交端 -> `CloseAndDrain()` 等全部请求进入 Committed/Failed -> 销毁 controller/worker。不得用会主动放弃已接受写入的硬超时。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| 当前 workspace 为 Whiteboard 或 Presentation | 不捕获 Desktop slot、不入 Desktop 队列、不创建 Desktop 索引项 |
| Desktop Clear 且符合条件 | 先捕获不可变快照并接受请求，再立即 Clear，不等待磁盘 |
| Undo/Redo、空画布、开关关闭 | 自动保存零调用；已有请求继续收敛 |
| 同毫秒不同请求或目标名已存在 | 使用唯一身份/安全后缀 create-new，绝不覆盖 |
| 相同 `saveRequestId` 重试 | 校验绑定 `fileGuid` 后复用，索引不重复 |
| UInk 写入失败 | 不修改索引，请求 Failed |
| UInk 成功、索引失败 | 保留孤儿与旧索引，记录诊断，请求 Failed |
| Undo 发生在 Clear 保存未完成时 | 直接消费内存快照；迟到 completion 因 `fileGuid` 不匹配而忽略 |
| 第二次有效 Clear 早于第一次 completion | 新 `fileGuid`/快照替换旧恢复点；旧 completion 不得释放新快照 |
| committed 文件回读损坏 | 当前空画布保持权威，locator 保留且 loadPending 清除，允许再次请求 |
| 主索引损坏但备份有效 | 使用有效备份继续；主备均无效时隔离当日索引写入 |
| 正常退出存在慢写或失败 | 等待明确终态后继续退出，不弹阻塞窗口、不伪报成功 |

### 5. Good / Base / Bad Cases

- Good：Clear 后立即空屏；保存完成前 Undo 从单份内存恢复，完成后内存释放且 Undo 从对应 UInk 恢复；第二个 Clear 只保留最新恢复点。
- Base：开关关闭、空 Desktop、Whiteboard 或 PPT 只执行原业务行为，`AutoSave` 根目录可以完全不存在。
- Bad：在 Clear 调用栈同步编码/写盘、用日期或数组下标充当唯一身份、让多个 detached writer 覆写同一 JSON，或在索引失败后删除未知 UInk。

### 6. Tests Required

- 纯 CPU 测试覆盖三态隔离、Desktop Clear/Exit 门控、Undo/Redo/空画布、`undoFloor`、可见快照与快照后继续绘制不变。
- worker 测试必须断言 Save completion 的 `fileGuid/status`、durable 后 `SubmitLoad` 返回同一页快照、失败/重复/旧 completion 不消费新恢复点。
- 存储测试覆盖同毫秒/候选名碰撞、幂等重试、两个 writer、跨午夜、UInk/索引故障、孤儿保留、主备恢复、损坏 schema/时间/GUID/序号/重复路径以及未知文件保留。
- 生命周期测试覆盖慢写 drain、提交端关闭、FIFO 最终屏障、controller 存活期捕获和失败退出。
- 使用 ARM64 原生 MSBuild 全量 Rebuild `InkeysRepo.sln Debug|ARM64`，运行 `inkStrokeModelerTestTests.exe`、`InkeysHeadlessTests.exe --no-window` 与 `git diff --check`。人工产品验证未完成前任务保持 active。

### 7. Wrong vs Correct

~~~cpp
// Wrong：绘制线程同步写文件，且后台线程脱离 Host 生命周期。
if (clear) {
    SaveUInk(currentDocument, dateName);
    std::thread(UpdateGlobalIndex).detach();
}

// Correct：安全点只产生值快照；owned worker 串行提交，退出显式排空。
auto snapshot = CaptureVisibleCanvas();
recovery = { snapshot.canvas, snapshot.fileGuid };
if (policy.ShouldCapture(workspace, enabled, hasVisibleContent))
    autoSave.Submit(DesktopAutoSaveTrigger::Clear, std::move(snapshot));
ClearCurrentInterval(); // completion durable 后 recovery.canvas.reset()
// Host stop: PrepareExitAutoSave -> CloseAndDrain -> destroy controller/worker.
~~~

当前会话/目标代次、成功 Present 后 UI 回执门禁及活动 contact 页边界详见 [PPT 会话合同](../ppt-interop/native-session-ui3.md)。保留本节 slot/存储合同；不得再以无限等待 active.empty 作为唯一换页条件。

## Scenario: PPT 三态 slot、UInk 自动保存与当前进程恢复

### 1. Scope / Trigger

修改 Presentation target、Desktop/Whiteboard/PPT 切换、PPT 页切换、dirty revision、Presentation UInk 元数据/导入、索引、Host 退出屏障或多放映切换时必须应用本合同。本期只自动恢复当前 Inkeys 进程成功保存的 entry；稳定 COM 下的 SlideID 重排、插入和删除属于当前进程范围，跨进程冲突和用户决策仍留待后续。

### 2. Signatures

- `Bridge::PresentationTarget { key, bindingMode, sourceIdentity, bindingToken, pageIndex, totalPages, slideId, slideIds, targetRevision, processLocalIdentity }`。
- `Bridge::ProductState::presentationTarget` 与 scene-stamped `Bridge::Command` 共享不可变 target；`PresentationReadyIdentity` 是不含字符串/拓扑的固定大小 runtime ready 值。
- `PublishProductPresentationTarget(Bridge::PresentationTarget) -> optional<uint64_t>` / `ClearProductPresentationTarget()`。
- `PresentationAutoSaveService::{Start, SubmitSave, SubmitLoad, TryTakeCompletion, CloseAndDrain}`。
- `PresentationSaveRequest { target, mutationRevision, snapshot, clearPageGuid?, clearIntervalOrdinal? }`。
- `PresentationLoadRequest { target, kind=Current|PreviousInterval, pageGuid?, intervalOrdinal }`。
- `ShouldQueuePresentationSave(mutationRevision, queuedRevision)` 与 `ShouldEvictPresentationSlot(hasCommittedFile, mutationRevision, queuedRevision, committedRevision, loadPending)`。
- `StablePresentationTopologyChanged(previous, next) -> bool`；只比较同一 StableSlideId 文稿的有序 `slideIds`。
- `ImportApplicationOwnedPresentation(document, expectation) -> Draw3UInkImportResult`。
- 产品布局：历史 Base 轨仍用 `<AutoSave>/presentation/index.json(.bak)`；同一 source/key 的另一 binding 按 `slide-id/` 或 `page-index/` 独立 sidecar 轨保存。每轨的 schema v2 index 指向 `files/<fileGuid>_<transactionGuid>.uink` 版本文件；旧 schema v1 `files/<fileGuid>.uink` 仍按原路径只读/续存，不删除未知文件。

### 3. Contracts

- 绘制线程持有 Desktop singleton、Whiteboard singleton 和按 `(PresentationKey, SlideBindingMode)` 分 lane 的 Presentation slot；每个 slot 自带 document、history、当前页、页 runtime、非零 slot generation 与持久化 revision。切换必须先准备目标 lane，再 park/swap 整个 slot 并执行完整 GPU reset/replay；旧 page-index 与新 StableSlideId 不共用 document、页面或 fileGuid。
- managed/native 必须把 key、binding、topology、page 和 `targetRevision` 作为一个共享不可变 target 事务发布。产品命令固定发布时的 workspace/target；Host 按 FIFO 执行 `captured scene -> command`，排空后再应用 latest state。ready 使用固定大小 identity，并同时匹配 workspace、key、binding revision、page/SlideID 和 target revision；仅页码相同不算 ready。
- `StableSlideId` 以规范化绝对路径作 source identity，provider 仅作诊断；无稳定路径时的 process-local identity 必须含 Inkeys PID、Office provider/PID/HWND、binding revision 和名称，防止同一 Office 进程重用未保存名称。
- 稳定模式写 `workspaceType=2 + hostId=PresentationKey + Canvas.slideId`；同一 SlideID 集合允许任意重排，新增页创建空 Canvas，已删除页以 retained marker 保留在同一 UInk/index，重新出现时按 SlideID 恢复。页码退化写 Inkeys 私有 `workspaceType=128 + inkeysBindingMode=page-index`，不伪造 SlideID；strict importer 必须验证旧索引与 UInk 的身份和 revision；旧 page-index 数据保持其 lane 与物理版本，新 StableSlideId 会话建立独立 lane/sidecar，不按 ordinal 搬运旧墨迹。
- `SetPresentationTarget` 复用 active 或 parked/warm slot 时，必须先恢复并读取该 slot 的旧 target，再比较新旧有序 `slideIds`、按旧 SlideID 映射 Canvas，成功后才写入新 target；不得在 swap 前用新 target 覆盖 destination。普通翻页中的 `pageIndex`、当前 `slideId`、binding/target revision 变化不是拓扑变化，不能因此重建 document/runtime。
- dirty 是“自上次成功提交后发生修改”，每个 Presentation 文档只比较一组 `mutationRevision/queuedRevision/committedRevision`；Stored stroke、成功 Undo/Redo、有历史的 Clear、viewport 修改推进 mutation，Laser/预测/纯 Present 不推进。每次保存都是全部页快照，所以文档级 revision 足以让任一脏页在离页时触发，同时保证无变化零写。不得用 `currentPageHasContent` 作 PPT 保存门控；因此恢复旧文件后 Clear 再立即退出也必须覆盖为全量空 Canvas 集合。
- PPT 持久化当前始终启用，不读取 Desktop 的 `saveSetting.enable`。未来 PPT 保存开关必须使用独立设置和迁移策略；Host 仍以同一 AutoSave root 启动 PPT worker。
- 同一 Canvas 的 canonical UInk `content[]` 是 Stroke/Clear 有序日志。普通 save 保留当前 `intervalOrdinal` 之前的 sealed prefix，只替换 active tail；Clear save 先写入当前 tail，再追加独占 undo group 的 Type 6，并把 ordinal 加一。`A,Clear,B,Clear,C` 当前投影只物化 C。
- `clearPageGuid + clearIntervalOrdinal` 必须同时出现并匹配请求中的页；Boundary 请求严格 FIFO、不可 latest-wins。普通 Tail 只可替换同 key 最近 Boundary 之后的普通 Tail。completion 也携带两字段，controller 仅在当前 ordinal 等于 `sealed+1` 时释放对应 fallback，防止第一次 Clear 的迟到 completion 释放第二次 Clear 内容。
- 保存触发在 Clear boundary、同 PPT 换页、A/B/workspace 离开和正常退出屏障。快照包含全部页（包括空页）；同一 key 的普通 Tail 使用 latest-wins，Boundary 始终逐项 durable。最终 scene-stamped 屏障位于前序 Clear/Undo/Redo 之后并扫描 active 与全部 parked slot；worker 无超时排空所有已接受请求。显式失败保留旧文件和页级 fallback，并在 completion 被绘制线程处理后恢复 dirty。
- 同一 PPT 的每条 storage track 保持逻辑 `fileGuid/workspaceGuid`；每次保存先 durable commit 到新的 versioned UInk 路径，再在同轨命名 mutex 内原子发布 schema v2 index，旧主索引转 backup，旧 UInk 不就地覆盖。index 提交失败保留最后已提交索引/文件及同进程同 root、track、generation 的 self-written pending；后续 Save/Load 只在严格身份与 revision 匹配后收敛，不得永久卡在 `SourceChanged`。不自动回收仍可能被备份索引引用的历史版本。
- clean inactive slot 只在有已提交文件、三 revision 相等、非 load-pending 且没有Clear continuation/Stroke redo时可淘汰。重入已淘汰 slot 必须走 `index -> ReadUInk -> strict import -> drawing-thread materialize`；dirty/pending/failed slot 保持 warm，不得被旧磁盘快照覆盖。加载期间清空 surface、不发布 identity-ready，并丢弃 physical contact/破坏性命令，直到 Loaded/NotFound/失败 completion 收敛。
- 当前区间 Undo 到 floor 且 `previousClearUndoAvailable` 时，若 boundary fallback 尚在则直接恢复；否则提交一次 `PreviousInterval(pageGuid, intervalOrdinal-1)`。worker 只返回目标区间的可见 tail，controller 只替换匹配 Page/Slide 的 runtime，以 `undoFloor=0、intervalOrdinal=0` 物化并消费跨 Clear 资格；恢复出的 Stroke 可逐笔 Undo/Redo，到空后不得继续进入更早区间。其他 active/retained Canvas 不动。成功后的普通 Tail 保存把恢复画布写成 canonical 新根并截断前后 Clear 分支；跨最近Clear的会话Redo以同页有限CPU continuation保留后继隐藏Stroke及redo栈，旧区间Stroke→Clear→后继Stroke逐步回到最新；新Stroke取消分支，不改变UInk格式。
- `PageIndexFallback -> StableSlideId` 不能凭路径、页数或完整 SlideID 列表证明旧 ordinal 墨迹的一一对应关系；`CanUpgradePresentationBindingByOrdinal` 必须拒绝。当前会话旧 fallback lane 保留其内容与旧文件，新 Stable lane 建立独立空文稿并保存到独立 track。各 lane 内的稳定 source 可按现有 binding/SlideID 身份恢复；process-local 身份仍要求 exact binding token/revision。
- 每条 track 的 `index.json(.bak)` 是严格 schema（写 v2、读 v1/v2）：source/key/fileGuid/path 均唯一，entry 恰含 source identity、key、sessionId、file/workspace GUID、relative path、binding mode、processLocal、binding revision、mutation revision、slideIds 和 UInk source revision。稳定模式的 `slideIds` 是已知 active/retained SlideID 并集，保存时只增不删；仅 `sessionId == ProcessSessionId()` 的 entry 可自动恢复。稳定路径 page-index 可在同 session/key/source、相同页数下跨放映 binding 按 ordinal 读写，process-local 必须 exact binding。foreign 或不一致内容返回结构化终态，不弹窗、不删未知文件、不静默覆盖。
- index commit 失败后的 self-written pending entry 只跨同一规范化 autosave root 的 Host generation 保留；切换 root 清除。I/O 使用保留大小写的绝对路径，folded root key 仅用于等价比较和 named mutex。
- 已选中 UInk 的 ReadUInkFile IoError 必须保留到 completion，沿用四次退避预算，不先因无 snapshot 变成 SourceChanged/manualOnly；其余严格检查与 foreign track 隔离保持，见 [F2](canvas-freeze-boundaries.md#f2ppt-已提交-uink-的暂时不可读必须保留-ioerror)。

### 4. Validation & Error Matrix

| 条件 | 必需行为 |
|---|---|
| descriptor busy，或 shared 页码已更新而 descriptor 仍是上一页 | 仅 descriptor bindingRevision 与当前 target 相同才保留且不发 ready；新 binding 首次 busy/stale 必须隔离 |
| descriptor 明确 unavailable/无放映 | 切到隔离 Presentation slot 或 Desktop，不复用旧 key |
| 未修改或首次空 PPT | 不写文件；没有内容不等于没有修改 |
| 恢复后 Clear/Undo/Redo 到空状态 | mutation 推进，下一安全点提交新版本 UInk 并原子更新该 lane 索引 |
| 连续多次 Clear | 每个 Boundary 按接受顺序写入同一 Canvas；普通 Tail 不得越过或替换 Boundary |
| 当前区间已经 Undo 到 floor | 先恢复 `intervalOrdinal-1`；到 0 后停止跨区间 Undo |
| interval load 时用户继续绘制/破坏命令 | 使用 loadPending 输入闸门抑制，completion 匹配后才重新开放 |
| SlideID 重排且旧区间加载完成 | 以 pageGuid/SlideID 定位目标页，只替换该页；其他页保留较新 mutation |
| 旧 fallback 在新会话取得 StableSlideId | 旧 lane/文件原样保留；新 Stable lane 不导入旧 ordinal 墨迹，独立建页与保存 |
| 稳定 SlideID 重排/插入/删除 | 按 SlideID 映射 active projection；新增页为空，删除页 retained 且不参与当前 ready，重新出现恢复原 Canvas |
| Desktop→同一 PPT 或 A→B 命中 parked/warm slot，且有序 SlideID 已变化 | 使用 destination slot 的旧 target 重映射后再更新 target；不得沿用旧 ordinal，也不得拿离开侧 active target 判断 |
| 新版本 UInk 成功、index 失败 | 返回 `IoError`、保持 dirty；磁盘旧索引/旧 UInk 仍是最后已提交恢复点，同进程同 root/track/generation 保留 pending 供严格自恢复 |
| index 指向 foreign session | 返回 `CrossProcessConflictDeferred`，本期不自动恢复/覆盖 |
| 文件 revision 与 index 不一致 | 返回 `SourceChanged`，保留原文件和 dirty slot |
| importer 身份/拓扑不匹配或含 Media/未支持语义 | 返回 Invalid/对应 import 错误，不部分 materialize |
| A save 慢时直接 A -> B -> A | 两 key 仍独立；A dirty/pending 保持 warm，旧 completion 不能更改 B |
| Host 重启 | 保留进程级 sessionId 和同根 pending index 修复状态，但清空上一代 queue/completion/Window command mailbox，不消费 stale callback |

### 5. Good / Base / Bad Cases

- Good：A 上绘制 -> 换页保存 -> 切 B -> 再进 A；clean A 走冷读取恢复全文稿，B 的画布/历史不变。A 重排或删除页面后仍按 SlideID 显示，删除页 retained，重新出现时恢复原 Canvas；A 恢复后 Clear 并退出，新版本保存空 Canvas，旧已提交版本仍可由 backup 恢复。
- Base：PPT 从未修改，切页/退出零写盘；Desktop 与 Whiteboard 的 slot 不变。
- Bad：用当前页非空作 dirty，按页生成多个 `.uink`，用页码代替 `PresentationKey`，或 EndShow 后永久保留 clean warm slot 而让索引/导入路径不可达。

### 6. Tests Required

- descriptor/bridge 纯逻辑：Unicode 路径、provider-independent key、process-local binding token、重复 SlideID、A/B 同页、stale descriptor/busy 保持和 identity-aware ready。
- UInk/storage：stable/fallback 各轨 round-trip、旧 page-index 字节保留且新 Stable 独立空页、稳定 SlideID 重排/插入/删除与 retained Canvas、index v1/v2 与版本文件、index 首次/覆盖失败重试和旧版本可恢复、self-written pending、foreign session、Host restart、Tail latest-wins、Boundary FIFO 与 A/B 独立。
- Clear 测试至少覆盖 `A/Clear/B/Clear/C` 当前投影 C、按 ordinal 加载 B/A、boundary/tail 同队列、连续 completion 身份、退出重入、旧区间建新分支和到 ordinal 0 停止。
- controller 状态：纯策略测试覆盖 load-pending 门控、clean eviction/冷恢复、dirty/pending warm、clear mutation、fallback/Stable 双 lane 隔离与命令场景顺序；另以 `1,2,3,4 -> 1,3,2,4` 断言 parked/warm 需要重映射，并断言仅页码、当前 SlideID 或 revision 变化不重建 runtime。生产 Controller 的 slot park/swap、completion 和全部 parked 最终屏障由完整产品构建与静态调用链核对，真实呈现留给设备验收。
- 运行 `inkStrokeModelerTestTests.exe`、`InkeysHeadlessTests.exe --no-window`、managed PptCOM ownership harness，以及完整 `InkeysRepo.sln Debug|ARM64` 构建；真实 PowerPoint/WPS 放映、COM busy/损坏与 Office 进程退出仍须设备验收。

### 7. Wrong vs Correct

~~~cpp
// Wrong：空画布就认定无需保存，会把已清空的恢复文件复活。
if (currentPageHasContent) SubmitPresentationSave(snapshot);

// Correct：存储门控只看持久化 revision，快照可以是全空 Canvas 集合。
if (ShouldQueuePresentationSave(mutationRevision, queuedRevision))
    SubmitPresentationSave(CaptureWholePresentation());

// Wrong：页号匹配即发布 ready，A/B 同页会串画布。
ready = runtime.pageIndex == observedPage;

// Correct：同一不可变 target 的身份、拓扑和 revision 全部匹配后才 ready。
ready = runtime.presentationTarget == expectedTarget;

// Wrong：先应用 latest B/Desktop，再让没有场景身份的旧 Clear 落到新画布。
PumpBridgeState();
PumpBridgeCommands();

// Correct：命令携带发布时 target；逐条 captured scene -> command，最后收敛 latest。
PumpSceneStampedCommands();
PumpBridgeState();
~~~

## Scenario: 工具光标样式与有效透明度

### 1. Scope / Trigger

修改 Pen、Highlighter、Eraser 或 Laser 的颜色、粗细、光标可见性、透明度或 Bar 显示值时必须应用本合同。

### 2. Signatures

- `Bridge::ProductState { tool, colorRgba, widthDip, revision }`
- `StateModeClass::Pen.Laser.color` / `ResolvePenColorStateSlot(laserActive, highlighterSelected)`
- `GetEffectivePenOpacity() -> float`
- `WindowController::SetProductVisualStyle(colorRgba, widthDip)` / `ProductVisualStyleSnapshot()`
- `ConfigureDrawingCursor(tool, appearance)` / `ResolvePrimaryDrawingCursorVisual(...)`

### 3. Contracts

- `stateMode` 只保存产品工具意图；`Pen.Laser.color` 是独立于 Brush/Highlighter 的颜色槽，默认 `RGB(255,16,0)`。`laserActive` 记录最后选择的 Pen 子类型是否为 Laser，不污染已记忆的 `Pen.ModeSelect`；Laser 活动时颜色调节按钮、预设色和自定义颜色窗口只读写该槽。Laser 仅在顶层模式为 Pen 时映射为当前工具；选择、橡皮和图形必须覆盖当前工具但保留该记忆，返回 Pen 时恢复 Laser。
- bridge 只跨线程传递当前工具的稳定 RGB 和粗细快照；活动笔画在 Down 时锁存 `ProductVisualStyle`，Hover 光标在帧边界跟随最新快照。Laser 的实际轨迹外壳、Hover/Touch 笔尖外壳与 Bar 粗细预览外壳必须使用 Laser RGB，不得回退到固定红色或其他笔的颜色槽。
- 普通笔光标直径是 `max(widthDip, 5 DIP * dpiScale)`；只有最小光标值按 DPI 缩放，实际笔画粗细不重复缩放。
- 产品荧光笔矩形尺寸复用当前 `CanvasDiameterForTool` 和笔尖长宽比，不再引用旧 demo 的固定 `6.25 × 50 px`；最终填充 alpha 为 `opacity * fillAlpha = 0.35`，Bar 显示同一有效透明度。鼠标/笔类型、荧光笔独立预览和固定设备边缘合同见 [draw3-cursors.md](draw3-cursors.md)。
- Eraser Hover 整体 alpha 为 `0.5`，Contact 为 `1.0`；这一规则同时适用固定/速度橡皮、鼠标和倒转笔橡皮。
- Touch 速度橡皮的场景、标尺及有效阈值由 Down 时的 `SpeedEraser::Config` 锁存；Host/Controller 的帧级诊断应报告同一 contact 的目标、实际几何和最终光标，不得用光标列表首项代表另一指或鼠标。
- Laser 使用独立的 `Pen.Laser.width/color`；光标、笔迹与粗细预览必须共用当前 Laser 宽度和颜色语义，固定漫反射宽度仍只由 Draw3 的 DPI 尺寸契约决定。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| 颜色/粗细在 Hover 期间改变 | 下一光标帧使用新样式；已 Down 笔画不重染 |
| Laser 模式点击预设色或提交自定义颜色 | 只更新 `Pen.Laser.color`，实际外壳、Hover 光标和粗细预览同步使用新 RGB；Brush/Highlighter 记忆不变 |
| 荧光笔经过 Pen 默认 alpha 归一化 | 仍保持 `0.35`，不提升为 `1.0` |
| 橡皮 Hover / Contact | 分别为 `0.5` / `1.0` |
| Laser 尺寸调整 | 同时修改共享直径来源，不单改光标或笔迹 |
| 记忆 Laser 后切换到选择/橡皮/图形 | 当前工具按顶层模式发布，Laser 记忆保留；返回 Pen 后重新发布 Laser |

### 5. Good / Base / Bad Cases

- Good：颜色/粗细快照由绘制线程在帧边界消费，Laser 光标立即更新为所选颜色，活动笔画保持 Down 样式。
- Base：没有样式变化时不重复配置 appearance。
- Bad：让 resolver 无条件覆盖工具 alpha，或用普通笔的颜色/宽度暗中驱动 Laser，或在渲染/预览层重新硬编码红色。

### 6. Tests Required

- 纯逻辑测试覆盖荧光笔 `0.35`、橡皮 Hover/Contact、鼠标、倒转笔和 Touch Contact，并断言 Laser 默认红色及其颜色槽与 Brush/Highlighter 隔离。
- 静态检查普通笔最小 DIP 直径、荧光笔矩形复用当前产品宽度，以及 Laser 光标/笔迹/粗细预览共享当前 Laser RGB、活动 layer 锁定 Down 样式。
- 完整 ARM64 `Debug|ARM64` Solution 构建，再运行 `InkeysHeadlessTests.exe --no-window` 和 `Inkeys.exe --draw3-hidden-test`。

### 7. Wrong vs Correct

- Wrong：`Pen sample -> opacity = 1.0`，把 Highlighter 或 Eraser Hover 当普通笔处理。
- Correct：只对原本不透明的普通 Pen/Mouse appearance 做默认归一化，部分透明工具保留自身 alpha。

## Scenario: 选择模式、当前页内容与可撤回 Clear

### 1. Scope / Trigger

修改产品模式桥接、Drawpad 显隐/消息穿透、Bar 选择布局、runtime history、Clear/Undo/Redo、页面切换或高精度计时器时必须应用本合同。

### 2. Signatures

- `Bridge::ProductState::selectionMode : bool`，默认 `true`；由 `IsComputerInteractionMode(StateModeSelect)` 将 `IdtSelection`/`IdtClickThrough` 统一投影，保留独立 UI 工具身份；不得从 `WS_EX_TRANSPARENT` 反推模式。
- `DrawingControllerRuntimeObserver::currentPageContentChanged(void*, bool, uint64_t)`。
- `HostRuntimeSnapshot` 包含内容、已应用选择模式、请求/就绪输出目标及 revision、`presentedContentRevision`、`auxiliaryFullFrameClean`、`runtimeRevision`；状态线程使用 `WaitForRuntimeRevision()`。
- `CanvasPageRuntimeState::{undoFloor, intervalOrdinal, intervalLoadPending, previousClearUndoAvailable, boundaryFallback}` 与 UInk Type 6 Clear。
- `ResolveBarClearClickAction(selectionMode, currentPageHasContent, doubleClickContinuation, clearAttemptedForDoubleClick, acceptedClearForDoubleClick)`。
- `ResolveDrawpadPresentationSurface(selectionMode, currentPageHasContent, auxiliaryFullFrameClean)`。
- `TimerPeriodController::SetSelectionMode(bool)`。

### 3. Contracts

- 当前页内容真值唯一来自当前 interval 的 `CanvasRuntimeHistory::LastVisibleItem()`：存在可见 Stroke 时有内容，否则无内容。Pen、Highlighter、Shape 和 Eraser 都算 Stroke 内容；Laser 与 UInk Clear marker 不算；Eraser 即使把视觉画面擦空仍算。
- DrawingController 在文档初始化、Stored Stroke 成功进入 runtime history、Undo/Redo 成功、Clear 和页面切换后检查内容；跨文档/页槽切换即使 `hasContent` 不变也发布新的 `contentRevision`。Host 必须把 `(hasContent, contentRevision)` 当成完整载荷：任一字段变化都存储、通知 `WaitForContentRevision` 并推进 runtime wake；两者都相同才幂等跳过。每次成功 Present 记录实际目标、输出 revision 和对应内容 revision，Selection 显隐仍要求 `presentedContentRevision == contentRevision`，不得用新呈现版本反向伪造目标。
- presentation 使用桥接 selectionMode：绘制只显示主 Drawpad；选择/穿透先把最终 backbuffer 全量提交到辅助 ULW，再隐藏主窗并显示辅助窗；选择无内容时只有辅助完整帧 alpha 全零才隐藏两窗。换窗前必须满足请求/就绪 target 与 revision 一致且 `presentedContentRevision == contentRevision`。
- Window Service 用批量窗口位置命令确保两窗互斥可见；失败时先隐藏两窗再收敛到唯一目标。主 Drawpad 不得动态切换 `WS_EX_TRANSPARENT`。
- Bar 仅在“选择+无内容”隐藏 Eraser/Geometry/Recall 等绘制按钮；选择+有内容与其他工具保持完整布局。合并式选择沿现有语义，分离式非选择状态显示“选择(清空)”并在 Clear Accepted 后进入选择；独立 Pierce 仅按[选择与穿透合同](configuration-i18n-and-assets.md#选择与穿透操作模式合同)注入运行时主栏，旧持久化 Pierce ID 与 `penetrate.select` 不恢复。
- Clear 只在 UInk canonical `Canvas.content` 中是 append-only Type 6 操作；活动 runtime 必须清空 `InkCanvas` Stroke、替换为 fresh history/raster token 并全量清理 GPU/瞬态资源。保存成功后释放 fallback，Undo 到 floor 时再按 Desktop locator 或 PPT page/ordinal 物化最近目标区间；该画布可逐笔撤空，但不能再跨越更早 Clear。
- Clear 成功后仍分配新的 raster token，丢弃不兼容的热前像、composition GPU cache/维护、恢复计划和 trusted L2，并重置 Laser/粒子/光标/瞬态层后全量透明呈现；CPU history、当前 viewport 和其他页面保留。Undo 隐藏 Clear 并恢复旧 Stroke，Redo 重新显示 Clear 并清空。
- 清空按钮采用两步语义：有内容时单击只发布 Clear 并保持模式；无内容且非选择时单击进入选择；空页已选择时使用统一 Disable 灰色且不响应。绘制或穿透有内容时双击由第一击已接受的 Clear 资格使第二击直接进入选择，不等待异步内容回报；发布失败时第二击重试 Clear。Clear 不进入 300ms toggle 点击合并。
- `timeBeginPeriod(1)` 只在进入非选择模式时幂等尝试；回到选择或绘制线程退出时，仅对成功 begin 配对 `timeEndPeriod(1)`。begin 失败后同一次绘制停留不重试，必须离开并重新进入绘制模式。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| 初始选择空页 / 清空后选择 | 辅助 ULW 完整帧 clean 后两窗隐藏；Bar 精简 |
| 空选择进入绘制但未落笔，再选择 | 重新进入隐藏空页状态 |
| 选择且有 history | 主 Drawpad 隐藏，辅助穿透窗显示；Bar 保持完整 |
| 选择可见态 Clear 或 Undo 到无内容 | 当前内容帧与 full-frame clean revision 就绪后隐藏辅助窗 |
| 选择空页仍有 Laser/粒子 | 辅助窗持续呈现淡出；最后全帧 alpha 为零后隐藏 |
| Eraser 在空页形成 history 或擦到视觉空白 | `currentPageHasContent=true` |
| Clear 后 Undo / Redo | Undo 恢复旧 Stroke 并发布有内容；Redo 再次透明清空并发布无内容 |
| 空内容执行 Clear | no-op，不追加连续 Clear，不推进 content revision |
| 绘制有内容时双击清空 | 第一击 Clear，第二击进入选择；observer 是否已回报空内容不影响结果 |
| 第一击 Clear 未 Accepted | 不锁存进入选择资格；双击第二击重试 Clear |
| 切换空页/有内容页 | 发布对应布尔值；其他页 history 与当前页 viewport 不受 Clear 影响 |
| `timeBeginPeriod(1)` 失败 | 不调用 end；同次非选择停留不重试 |

### 5. Good / Base / Bad Cases

- Good：有一笔的选择态由辅助 ULW 穿透显示，Clear 后等当前 revision 的完整 clean 帧再隐藏；Undo 恢复该笔并重新显示辅助窗，Redo 再次隐藏，另一页的 Eraser history 仍存在。
- Base：初始选择空页两窗隐藏；进入 Pen 先预热主 presenter 再显示，未落笔返回选择再次经辅助 clean 后隐藏。
- Bad：用窗口穿透样式推断选择模式、让主 Drawpad 承担穿透、复制固定 L2、视觉像素是否为空推断内容，或 Clear 时删除 Stroke/替换 runtime。
- PPT 真退出及 Desktop/Presentation、Selection/绘制交接必须保持旧可见画面直至最新工作区、模式、输出目标/revision、content Present 与场景身份均匹配；等待或过期帧不主动双隐藏。就绪后通过 owner-thread 成对显隐事务切换目标，有内容选择页使用辅助 ULW，空 clean 页可隐藏画布但主栏保留。旧非 layered 主表面保留期间不承诺系统穿透；AdmissionBlocked 不能代替该穿透。可信退出的条件 Selection 成功后必须再刷新 Bar 样式和渲染，不能沿用提交前的高亮；Desktop交接就绪时刷新根并在失败时重试。
- PPT EndScreen 仅由可信 State=5 与同次完整文稿拓扑产生明确 `PageKind::EndScreen` 目标：内部索引为真实 `N`，COM/UI 总页数仍 `N`，无 SlideID。正常页与 EndScreen 共用单 Host 页边界、contact 隔离、成功 Present、UI ready 和保存 worker。UInk 文件在 N 个真实 active 页后保存一个带单个 `inkeysPageKind=end-screen` 的独立 pageGuid；codec 两侧和应用严格导入须配套验证，旧 N-canvas 文件冷读补空页。重排/增删幻灯片不能把该页放入 retained SlideID 集合。

### 6. Tests Required

- Headless 覆盖 `Primary/Presentation/Hidden` 解析、Clear 点击/双击决策，以及 timer begin/end 幂等、失败、模式往返和析构清理。
- CPU history 覆盖普通 `A/B/C` 的 Undo/Redo、`undoFloor` 截止、Clear 恢复画布逐笔撤空且不跨第二个边界、新 Stroke 分支丢弃 redo 和每页隔离；UInk/storage 覆盖 `A/Clear/B/Clear/C`、空 Clear no-op 与最近区间恢复。
- 隐藏 HWND 集成覆盖双窗固定样式/owner/bounds、互斥可见、输出 generation 往返、clean 握手、Stored Stroke 内容发布、页面切换、Clear 后 Undo 恢复、普通 Stroke Redo 和 presenter recovery；跨最近Clear的会话Redo以同页有限CPU continuation保留后继隐藏Stroke及redo栈，旧区间Stroke→Clear→后继Stroke逐步回到最新；新Stroke取消分支，不改变UInk格式。
- Selection PPT 页级回归须通过真实 Controller observer 和 Host 覆盖两个不同有墨迹页的 `true/r→true/r+1`、两个空页的 `false/r→false/r+1`、相同完整载荷幂等、EndScreen 与普通页往返；每次比较 Host 目标/成功 Present 内容版本及辅助 ULW 的完成输出，不能只等待 `presentationReady` 身份或内容 bool。Pen 模式与真退出保留旧帧、就绪交接和主栏模式同步合同同时复测。
- 完整 `InkeysRepo.sln Debug|ARM64` 构建，运行 `InkeysHeadlessTests.exe --no-window`、`Inkeys.exe --draw3-hidden-test` 与 `git diff --check`；不得启动可见窗口。

### 7. Wrong vs Correct

Wrong：`selection = current WS_EX_TRANSPARENT`、`主 Drawpad + WS_EX_TRANSPARENT`，或 `Clear -> ClearStrokes + replace runtime -> Undo no-op`。

Correct：`显式 selectionMode + 当前 interval content + generation/content/clean 握手 -> 双 surface 互斥切换`；`Clear 封存 UInk boundary -> fresh runtime + GPU/瞬态全清 -> Undo 到 floor 后按 locator/ordinal 恢复`。

## Presenter 合同

以下首发选路合同来自 2026-09-27 用户决定；H0 源码仍含 DWM 自动/强制路径。本次工作区已在源码中禁止该选择，但隐藏 HWND、Win7 与运行期设备故障尚未实测，不能把静态选路当作透明呈现 PASS。

- Draw3 自己创建 D3D11.1 device/context（默认 hardware-first/WARP-fallback，硬件加速启动偏好关闭时直接 WARP）、DXGI factory、swap chain 和 presenter，不注册 `Inkeys.UI.RenderPipeline` client，也不共享其 device epoch。
- 首发正式 presenter 模式顺序为 DComp -> ULW；DwmBlurBehind2 与 DwmBlurBehind 在自动回退和强制模式中均不可选。每次失败都销毁该模式的 swap chain/presenter 状态后再降级。DComp 在创建前通过能力探测决定是否预置 `WS_EX_NOREDIRECTIONBITMAP`；legacy 重建 Host 明确跳过 DComp。ULW 保持现有 FLIP_SEQUENTIAL swap chain、premultiplied top-down 32-bit DIB、`1/255` CPU alpha 边界和 dirty rect，透明像素 alpha 必须保持零。
- 用户已在 Win7 SP1 且仅安装 KB2670838 的环境实测 `FLIP_SEQUENTIAL` 可用；微软通用 DXGI swap effect 文档写 Win8 起支持，与该实测相冲突。首发实现以用户实测为兼容约束，保留 FLIP，不添加 bitblt 或 swap effect 降级。设备/驱动、Hardware FL11.0 与无 FL11.0 时的 WARP、ULW 成功 Present 仍须逐格复验；文档冲突不能替代本轮真机证据。
- 主 presenter 与辅助 ULW target 共用 renderer/final backbuffer，每帧只向请求目标提交；辅助初始化失败即 Host 启动失败，运行期失败必须重建并全量重试，不回退到主窗穿透。
- 只允许一个绑定主 Drawpad 的选定输入 producer（RTS 或 Win32Mouse）；Draw2 RTS 不得初始化或重复发布。退出顺序为关闭绘制准入并预约最终保存 -> 停止并排空选定 producer -> 唤醒绘制线程并执行最终保存屏障 -> 释放双 presenter/device -> Window Service 销毁两窗。

## Scenario: DComp 交接上屏屏障与 Laser 最终过期

### 1. Scope / Trigger

修改 Primary DComp 与 Selection ULW 输出切换、Present/ready 发布、窗口显隐交接，或 Laser 生命周期多次求值与瞬态资源清理时适用。`Present1` 返回不代表 DWM 已显示新帧；隐藏主 HWND 也不清除其最后已显示内容，过早 ready 会在重新显示时露出旧 Laser 或旧笔迹。

### 2. Signatures

- `TransparentPresentationController::SetOutputTarget(target)` 生成单调 output revision；`Present(dirty, presentFull, contentRevision)` 是交接屏障所有者。
- `DCompHandoffBarrierState::{Arm,Cancel,Consume}` 记录待上屏 revision；`DrawingController::PresentFrame` 只在 `Present` 整体成功后调用 `observer_.presented`。
- `LaserTrailExpiredThisFrame(previous, final)` 以帧首和本帧最终 `LaserTrailPhase` 判定唯一过期边沿。

### 3. Contracts

- 切回 Primary 时 arm 最新 revision；仅该 revision 的 DComp 全帧 `Present1` 成功后执行一次 `DwmFlush`，其成功返回后才允许 Host 发布 handoff ready。普通 DComp 帧和 ULW 不执行此等待。
- `Present1` 失败不消费屏障；`DwmFlush` 失败必须恢复同 revision pending、不得成功 ready，并记录 target/output/content revision 与 HRESULT，沿用现有恢复。切到 ULW 或快速产生新 revision 时淘汰旧等待，不能为旧目标补 ready。
- Laser 一帧可先在动画阶段、再在输入阶段求值。最终相位仍为 Active/Hold/Fade 时不清资源；最终进入 Inactive 时当帧清理 coverage、stable/live bounds、stroke layers 和 coverage mode，下一帧不重复。首次过期即使 opacity 增量低于 dirty 阈值，也必须 union 旧 bounds，保证不被 idle/maintenance 提前跳过。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| DComp 普通或 partial 帧 | 不调用交接屏障 |
| 交接 Present 失败 | 不等待、不 ready，同 revision 全帧重试仍可取得屏障 |
| `DwmFlush` 失败 | 不 ready，保留 pending 并进入恢复 |
| DComp→ULW→DComp 快速往返 | 旧 revision 被拒绝，仅最新目标等待一次 |
| Laser 第二次求值才 Inactive | 当帧最终清理一次 |
| 同帧新 Down 回到 Active | 不误清旧批次，沿用既有生命周期 |
| 末段 opacity 变化低于阈值 | 旧 bounds 仍 dirty，清理不被 idle 跳过 |

### 5. Good / Base / Bad Cases

- Good：全帧 Present → DWM 上屏屏障 → ready → owner-thread 成对显隐。
- Base：连续书写和普通 ULW 呈现保持原有无额外 `DwmFlush` 路径。
- Bad：用固定 Sleep/重复 Present 代替同步，或只在第一次 Laser 求值后计算过期并可能永久丢失清理。

### 6. Tests Required

- 纯状态测试覆盖屏障成功、失败、Present 重试、普通/ULW/partial 不等待、成功后不重复和快速 revision 往返；这些只证明顺序，不冒充真实 DWM 视觉证据。
- Laser 探针覆盖第二次求值才过期、下一帧不重复及新 Down；检查生产代码先强制旧 bounds dirty、再在最终相位后清理。
- 完整 Solution 原生 Debug 构建，运行 `InkeysHeadlessTests.exe --no-window`、`Inkeys.exe --draw3-control-fence-test`、`Inkeys.exe --draw3-hidden-test` 与 `git diff --check`。
- 真实桌面必须反复验证 Laser→Selection→消失→绘制及普通笔迹快速往返；未执行可见视觉验收时保持设备待验收。

### 7. Wrong vs Correct

Wrong：`Present1(...) == S_OK -> observer.presented(success) -> ShowWindow(primary)`；或 `laserExpired` 只在第一次相位求值后计算。

Correct：`Present1 -> DwmFlush(仅交接全帧) -> observer.presented(success)`；Laser 用 `LaserTrailExpiredThisFrame(frameInitial, finalAfterInput)` 驱动最终清理，首次过期预先保留 dirty。

## 来源任务与验证状态

- 源任务历史统一保存到 `.trellis/tasks/archive/2026-08/draw3-source/`；其中 `active/` 保留源快照的 `in_progress` 状态但不进入目标 active task 列表，原 2026-07/08 归档按月份保留。
- 截至 2026-08-16，文件/资源映射、固定库 ABI、HWND/owner/Z 序、独立设备和唯一 RTS 约束已完成静态审计；ARM64 Debug/Release Solution 全量 Rebuild、`--no-window` 纯逻辑测试和隐藏 HWND 的真实 DComp/DWM2/DWM/ULW 合成测试均通过。测试按 DComp-compatible 与 legacy 两个顺序生命周期运行，并验证样式回调结果与实际样式一致；产品启动使用同样的显示前顺序重建合同。

## RuntimeMetrics 有限软件证据（2026-09-30）

- Session 的唯一 render owner 预分配固定 payload，总预算32MiB（不含 allocator metadata），open-address 去重表负载不超过0.5，最多64 pending；这些是诊断容量，不限制产品准入或减少输入。超容量保持 seen/dropped/invalid/missing 分母，不覆盖前缀，不热扩容/写盘/log。
- 每个真实 Down 只登记一次实际源 QPC/tool/device 和 opaque record值+generation，保存数值不延长或读取ContactRecord寿命。布局/当前frame/output/content身份为完整proof；失败不能通过lastConsumedSequence、hasContent revision或old snapshot推定采纳/权威像素。
- 同步记实际Present attempt/result。Stored proof可在Up/recycle后按纯值保留，失败后只能在同一当前权威内容的新成功帧确认；Live/Laser必须当前帧重新Stage，BeginFrame不能借用旧活动层proof。旧API/无proof一律legacy/unverified，不宣称正式Down landing。
- schema2区分attempt/success/failure/unknown、finite invalid/drop/missing和不支持项。tool按真实enum映射。统计仅有限有效分母；小于1000有效样本P99=null，空值null，legacyThresholdMet不是releaseVerdict。WriteJson只在owner结束后离线create-new，不覆盖已有用户文件。
- U1/M01–M16直接测试真实Session/Coordinator与错误身份/失败/精确帧restage，Debug CPU probe绿和独立review只证明数值合同。实际Controller normalRun/PresentFrame采用源、Host是否创建Session、phase和三轮完整DComp/ULW轨迹必须另由U2/U3证据确认；未实现的接线不能记PASS。
- Laser的正式Down/Up landing在首次有限proof中不支持，但真实帧成本、Hold/Fade/particle、输入语义和长期资源仍必须单独调查。Down/Move/Up逻辑提交、软件Present return、光学像素可见及GPU duration分别报告，不用一种替代其它。

## 共享 UInk 内部 sibling 路径合同

主产品直接编译共享 `inkStrokeModelerTest/draw3/uink_file.cpp`。SaveUInkFile 的 temp/backup/new-recovery 必须使用规范化最终路径的原同父前缀加独立 GUID36 短叶及固定后缀，不再次拼入最终末名；同目录/同卷原子替换、CREATE_NEW、锁、strong source revision/SHA 和最终格式不变。恢复调用者消费实际返回的 opaque recoveryPath，不能按最终 basename 猜恢复文件名。

此短内部名称不承诺任意最终长路径支持：普通父前缀加最长49字符内部叶仍受 Win32 路径边界约束；不以新 manifest/registry/补丁提升最低平台，不授权按后缀清理未知文件。2026-10-01 shared helper 的合法230首存/更新/predecessor严格回归已真实红绿；主产品原PPT长路径C10、Release三架构与Win7真机仍分别待验证。清理创建/身份归属竞态独立F069，命名修补不能证明其已解决。

## Whiteboard 页级 UInk 与编辑历史合同

`【直接确认：源码；2026-10-15】` 白板使用 `Inkeys.Drawing.Draw3.whiteboard_store` 和共享 `draw3.uink_page_file`，不复用 PPT 的整文稿读写。产品范围见 [whiteboard.md](whiteboard.md)；本节说明实际接口与所有权，测试通过情况以任务验证记录为准。

- `ScanUInkPageFile(path)` 返回 `UInkPageFileDirectory`：路径、完整 `UInkSourceRevision`、不含各页笔迹/extra 的 metadata，以及 `{pageGuid, offset, length}` 目录。扫描、SHA 和自校验流式进行，自校验逐页物化。`peakBufferBytes` 仅是序列化工作缓冲高水位估计，不包含解码模型、调用方快照或分配器余量，不能据此宣称总 RAM 上限。
- `ReadUInkPageFile(directory, pageGuid)` 打开文件并核对目录绑定的完整 revision 后读取指定页，返回 `pageIndex/currentPageIndex=0` 的单页文档，保留 pageGuid。旧偏移不得套到新版本；文件身份/内容改变、GUID 缺失或页损坏均失败，不用空页替换。
- `RewriteUInkPageFile(newPath, previous, onePage, orderedPages, currentPageIndex)` 只接受不存在的新路径；旧版本只读，已存在页顺序必须保留且只可追加。目标页重新编码，未修改页按目录分块复制；Flush 和逐页自校验后返回新目录。逻辑 file/workspace/page GUID 保持，UInk 10 标准格式与原桌面/PPT接口不变。
- `WhiteboardStore::{Save,Load,Take,Drain}` 由单 worker 持有目录及文件 I/O，绘制线程提交不可变单页快照并消费 completion。每进程会话写 `AutoSave/whiteboard/<GUID>/`，`<sequence>.uink` 是同一逻辑白板的物理版本；`current` 索引经临时文件 flush 后原子替换为版本文件名，只有索引发布成功才替换运行期目录。失败保留原目录和 warm 页；同页排队快照合并，保存队列上限为两项，队满页保留 warm 待后续安全点重试。
- Store 保留当前与一份前版本；清理更旧的本进程版本只能通过 `TryRemoveOwnedUInkPageFile` 校验强 revision 并按同一文件句柄删除，失败保留，不得退化为按路径删除，也不得清理扫描得到的外部文件。新进程不读取旧白板，旧会话保留供未来历史功能。
- Canvas `extra["inkeysWhiteboardHistory"]` 是版本 1 JSON：`version/rgb/pattern` 加完整 CPU strokes、历史 items（strokeIndex/visible/compositionBarrier）、redo 索引、undo floor、interval、最近 Clear 资格、boundary fallback 及有限 Clear redo continuation。恢复严格验证类型、索引、重复 key、主题范围及 continuation 深度；重建 footprint/raster token，GPU 缓存与句柄不入文件。标准 `content` 只导出当前可见笔迹，通用读取器不应看到隐藏 redo。
- Load completion 携带页面及运行 generation，安装前再次核对 active workspace、待加载页、generation 和 pageGuid；成功才成对恢复内容/主题并切页。Save completion 以页面 revision 仲裁，只有成功且 saved==当前 mutation、非当前页且非正在加载时才卸载页内容/历史/CPU/GPU。保存失败可翻页，失败页保持 warm；关闭白板不销毁文档，退出屏障还处理 parked dirty 页。
- 必须验证多页与主题空页 roundtrip、隐藏 redo、Clear continuation、过期 completion、源版本替换/内容损坏、写入和索引失败、快速返回，以及至少 100 个非空页的正常卸载。流式缓冲统计与实际页 runtime 释放分别断言；GUI/PPT/任务栏验收不由这些无窗口测试替代。

## 启动输入回退

完整 RTS → 基础 X/Y RTS → Win32Mouse，每档安全停止/排空后才进入下一档。Host按InputReady放行；RTS与窗口鼠标绘制互斥。鼠标owner线程启停、捕获和最终保存的合同见input-and-ink.md。迁移不新增性能采集或资源探针。

## 双画布底行与非全屏策略（2026-10-19）

- `Drawpad` 与 `DrawpadPresentation` 在 Desktop/PPT 的实际可见/输入区域始终排除目标屏幕最底 1 **物理像素**；不依赖任务栏方向、autoHide、UI 倍率或 DPI。由 Window Service owner 在线程内设置 window region，保留完整 HWND/client、Host/backbuffer、RTS 标尺和文档坐标；区域外笔迹不删除、不改写持久化。
- 两窗首次显示前设置公开的 `NonRudeHWND` 属性；调用者传入 WS_VISIBLE 也必须先撤掉，策略成功后才显示。`MarkFullscreenWindow(FALSE)` 只撤销显式标记，不能替代禁止 Shell 自动识别全屏的属性。
- Whiteboard 恢复两画布创建时的原 region/属性，继续由 Freeze/root 作为既有全屏锚点。退出、进入失败和窗口重建均恢复相应场景；selection 固定穿透、单 Host 与 owner 链不变。
- scene、实际 bounds/monitor 或 HWND 变化才重新应用；两个角色共同隐藏、应用、核对并恢复可见性后才确认白板 mode。失败逆序回滚；原状态可信、几何未变且完整回滚时保留旧 applied key 并停止内部重试，避免对原场景重复隐藏/显示；首次空 key 保持空值，后续显式请求仍可应用。旧 region 不适用于新 bounds、回滚未收敛或已有 dirty/恢复状态时保留双隐藏及原显隐意图，交既有 owner 循环 250ms 失败重试，正常无额外轮询。后来的 Hide/Hidden/HideAll 优先于保存的恢复显隐；连续失败即使已清旧 applied key，也必须保持双隐藏直到真实成功。
- `Window.CanvasPolicy.h` 的生产区域和双窗事务 helper 由 Headless 无 HWND 测试直接覆盖；测试不证明真实 USER32 重入、DComp/ULW 可见裁剪或 Shell 分类。既有全屏 COM 链仍为 best effort；GUI/Shell/旧系统结果单独记证据。已有 capture 按原输入所有权收尾，不为底行策略擅自取消笔迹。

## 三态过渡与实际选择态准入（2026-10-27）

- Host接纳工作区Pending时先关闭基础新Down门，以当前协调epoch绑定；只同epoch完成可撤其自身原因，旧false不能开放新pending。同epoch已退休的旧true也不能复活。停止/新run按实际生命周期复位，不用业务版本替代Host代际。
- Pending原因OR到现有stop/fatal/recovery/load/scene/UI-ready，不能整体清AdmissionBlocked。墨迹shortcut baseline也不得绕过这些适用原因；Release、ControlWake、目标准备、成功帧/UI回执与真实终态drain不受此新门阻挡。BasicControl和系统/Bar安全动作保持各自资格。
- 物理鼠标可在RTS成功路径以TDK_Mouse进入MouseLeft/Right；Win32 fallback的选择态拒绝不足以覆盖它。DrawingController对所有workspace的实际Selection新Down统一DiscardUntilTerminal，不能仅限白板，不能因稍后切Pen补画旧尾事件；正常非Selection工具/临时擦除语义不变。
- 产品已发布的工具不等于绘制owner已应用。协调使用真实已应用产品/工具/选择凭据与Host代际，UI稳定工具按Committed对应记忆，不能仅看ActiveTool=Pen推断选择尚未应用。
- 生产无窗口probe只证明实际共用拒收操作、ContactInputCoordinator/DrainIngressBatch或Host实际准入接缝；私有Run/Service未动态执行部分另做源码审查，不冒充真实Win32/RTS设备/任务栏实测。

- 过渡准入setter绑定真实HostGeneration及请求epoch：旧run的相同数字epoch不能设置或撤销新Host的门。完成只撤自身Pending原因，不能清加载、恢复、停止或fatal原因；最小化Whiteboard继续闭门，恢复使用新视图版本。工具完成凭据读取owner实际appliedProductRevision/appliedTool，不用Bridge已发布revision冒充应用成功。
