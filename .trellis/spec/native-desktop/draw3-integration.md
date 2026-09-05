# Draw3 集成规范

## 来源与文件边界

- 来源快照：`D:\Project\Inkeys\inkStrokeModelerTest\Inkeys3-Draw3@8d045298eaaac76f752b4f8b5f3303b3520e50b7`；目标仓库不嵌套源 `.git`。
- `draw3/*.cpp|*.cppm` 重命名到 `Inkeys/Inkeys/Drawing/Draw3/Draw3.*`；HLSL/HLSLI/CSO 到 `Inkeys/Inkeys/Drawing/Draw3/Assets/`；Ink Stroke Modeler 和 Abseil 仅复制头文件到 `Inkeys/additional/`。
- `Inkeys/resource.h`/`Inkeys/Inkeys.rc` 保留 Draw3 资源 ID：`301=IDR_DRAW3_INK_PIXEL_SHADER`、`302=IDR_DRAW3_INK_VERTEX_SHADER`、`303=IDR_DRAW3_LASER_UPDATE_CS`、`304=IDR_DRAW3_LASER_EMIT_CS`。
- 产品编译不得登记源 `main.cpp`、demo/独立测试窗口、`window_performance_hud`、`.git/.vs`、构建输出或第三方 `.cc`；只允许链接源快照自带的三架构 `ink_stroke_modeler_merge.lib`。

## Scenario: 固定版本 Ink Stroke Modeler ABI

### 1. Scope / Trigger

修改 Ink prediction、第三方头文件、平台/CRT/Vcpkg 配置或固定模型库时必须应用本合同。

### 2. Signatures

- `#pragma comment(lib, "ink_stroke_modeler_merge.lib")`
- `InkStrokeModelerLibraryDirectory`: Win32=`..\inkStrokeModelerTest\lib\lib32`，x64=`..\inkStrokeModelerTest\lib\lib64`，ARM64=`..\inkStrokeModelerTest\lib\libArm64`

### 3. Contracts

- `Inkeys/additional/{ink_stroke_modeler,absl}` 只保留公开/传递头文件，不登记或保留 `.cc`。
- 三架构固定库必须来自已合并的 Draw3 commit `8d045298`；不得用本机构建输出静默替换。
- 固定库使用 `/MT`、`NDEBUG` 和 `_ITERATOR_DEBUG_LEVEL=0`。Inkeys Debug 保留禁用优化/PDB，但必须使用 `MultiThreaded`、取消 `_DEBUG`，并在 `Microsoft.Cpp.targets` 导入前设置 `VcpkgConfiguration=Release`。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| 任一架构库缺失 | 链接失败，不回退到源码直编 |
| Debug 使用 `/MTd` 或 `_DEBUG` | 视为 ABI 配置错误，禁止用 `/NODEFAULTLIB` 或忽略 LNK2038 |
| Vcpkg Debug 静态库进入 Inkeys Debug | 改为 Release 子目录，使完整本机依赖链保持 `/MT` |
| 头文件与固定库版本不一致 | 更新必须成套进行并重跑全量 Rebuild |

### 5. Good / Base / Bad Cases

- Good：Debug 自有代码可调试，但所有静态依赖 ABI 为 `/MT`，清空中间目录后成功链接。
- Base：Release 直接使用对应架构固定库。
- Bad：保留 Abseil/Modeler `.cc` 与固定库同时链接，或靠旧 `.obj` 让增量 Build 假通过。

### 6. Tests Required

- ARM64 Debug/Release 对完整 `InkeysRepo.sln` 执行 `/m:1 /t:Rebuild`。
- 静态断言四个第三方目录无 `.cc`、项目无第三方 `.cc` 编译项、三架构库与 pragma/目录映射齐全。
- 运行 `InkeysHeadlessTests.exe --no-window` 和 `Inkeys.exe --draw3-hidden-test`。

### 7. Wrong vs Correct

Wrong：`Debug /MTd + release merge.lib -> 忽略 LNK2038`。

Correct：`Debug 保留调试信息 + /MT + undef _DEBUG + Release Vcpkg libs -> 固定库 ABI 一致`。

## 窗口与所有权

- `WindowService` 是 `WindowRole::Drawpad` 与 `WindowRole::DrawpadPresentation` HWND 的唯一创建、显示、隐藏和销毁者。Draw3 只把主 Drawpad 交给 `AttachExternal(HWND, callbacks)`；辅助窗不得绑定 RTS、WndProc mailbox、document 或第二套 Host。
- Draw3 不创建/销毁顶层窗口，不修改标题、owner 或 Z 序；样式和 `Primary/Presentation/Hidden` 可见性变化必须通过 Window Service 的 owner-thread 命令完成。
- owner 层级保持 `MagnifierHost -> Freeze -> {DrawpadPresentation, Drawpad}`，辅助窗低于主 Drawpad/PPT/Bar 且高于 Freeze；PPT/Bar 继续 owned 到主 Drawpad。Draw3 presenter 不得调用 `SetWindowPos`、`SetParent` 或直接改 owner；`SetBounds(Drawpad)` 必须同步两窗。

## 设备与线程

- Draw3 独立创建 D3D11.1 hardware-first/WARP-fallback 设备、context、DXGI factory、交换链和呈现资源，禁止引用 `Inkeys.UI.RenderPipeline` 的设备。
- UI 线程只向 bridge 发布不可变快照和命令；renderer、document、history、RTS 消费只在 Draw3 绘制线程进行。
- 进程只允许一套 Draw3 Host、绘制线程和 `RealTimeStylusInput` producer；Desktop、Whiteboard 及每个 `PresentationKey` 的 document/history 是该绘制线程独占的独立 slot。辅助 HWND 仅是同一最终 backbuffer 的 presentation target。退出顺序固定为停止命令生产、停止 RTS、唤醒绘制线程、执行最终保存屏障并排空 worker、释放双 presenter/设备，最后由 Window Service 销毁两窗。
- `DrawingControllerRuntimeObserver::drawingActivityChanged` 只按全部 physical contact 的聚合值发布 `0→1/1→0`；每个命令消费边界与帧末复核，避免 Down 后提前 `continue` 漏报。Host 通过独立 `HostRuntimeCallbacks` 注入产品通知、再次去重，并在 Run 正常返回、异常或 stop/join 后补发一次 false；Draw3 核心不得直接依赖 Bar。

## 样式与透明度

- 主 Drawpad 固定不带选择语义的 `WS_EX_TRANSPARENT`；只有 `ShouldPreconfigureNoRedirectionBitmap()` 能力探测通过时才在创建前预置 `WS_EX_NOREDIRECTIONBITMAP`，随后按主 presenter 模式切换 `WS_EX_NOREDIRECTIONBITMAP`/`WS_EX_LAYERED`。
- `DrawpadPresentation` 出生即固定包含 `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`，运行期不得清除或借其承载输入。
- DComp 清除 `WS_EX_LAYERED`；DWM 清除 `WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP`；ULW 设置 `WS_EX_LAYERED` 并清除 `WS_EX_NOREDIRECTIONBITMAP`。
- ULW 必须提交 premultiplied-alpha、top-down 32-bit DIB 和 dirty rect；未绘制像素的 alpha 为零，禁止整窗不透明更新遮挡下层窗口。

Windows 对创建时带 `WS_EX_NOREDIRECTIONBITMAP` 且已经绑定过 DComp target 的主 HWND 可能拒绝后续清除该位（`ERROR_INVALID_PARAMETER`）。Window Service 必须写后读取并核对真实样式。若产品 DComp 启动失败，必须在首帧显示和 Setting 初始化前停止 Draw3 与整条隐藏窗口链，再顺序重建 legacy-compatible 主 Drawpad 及其辅助 sibling；新 Host 禁用 DComp 并从 DWM2 -> DWM -> ULW 继续。两个主 Drawpad generation 不得同时存在，但同一 generation 必须包含长期待命的 presentation-only sibling。

## 功能边界

桥接工具固定为 Pen、Highlighter、FixedEraser、SpeedEraser、Laser、SolidLine、DashedLine、OutlineRectangle、FilledRectangle。清屏、撤销/重做、页面切换、Desktop UInk 自动保存及当前进程内的 PPT UInk 自动保存/恢复已接入；手动保存、Whiteboard 自动保存、跨进程 PPT 恢复/冲突交互、超级恢复、自动直线拉直和输入测试仍保留 `Unsupported/NotReady` 空接口并隐藏产品入口。保留 Draw3 速度橡皮、固定橡皮及 `SpeedEraserOcController`；仅删除旧 Draw2 压感橡皮实现和设置入口。

## Scenario: Desktop UInk 自动保存事务与退出屏障

### 1. Scope / Trigger

修改 Draw3 workspace、Desktop Clear/正常退出、`saveSetting.enable`、Desktop UInk 完整保存、每日索引或 Host/controller/worker 生命周期时必须应用本合同。Whiteboard、PPT 自动保存和跨 Clear 恢复不在本合同范围内；PPT 使用下一节的独立事务。

### 2. Signatures

- `Bridge::Workspace { Desktop, Whiteboard, Presentation }`。
- `DesktopAutoSavePolicy::ShouldCapture(workspace, enabled, hasVisibleContent) -> bool` 与 `CompleteDesktopClear()`。
- `DesktopAutoSaveService::Submit(DesktopAutoSaveTrigger, Draw3UInkExportSnapshot)`、`CloseAndDrain()`。
- `Bridge::CommandType::PrepareExitAutoSave` / `CanvasCommandType::PrepareExitAutoSave`。
- 产品根路径：`GetCurrentExeDirectory() + L"\\Inkeys\\AutoSave"`。

### 3. Contracts

- 自动保存只接受 `Desktop + saveSetting.enable + 当前页非空`。Desktop slot 与 Whiteboard/PPT slot 独立；访问 PPT 不会污染 Desktop 保存资格，也不得将 PPT 内容导出到 Desktop 目录。
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
| 主索引损坏但备份有效 | 使用有效备份继续；主备均无效时隔离当日索引写入 |
| 正常退出存在慢写或失败 | 等待明确终态后继续退出，不弹阻塞窗口、不伪报成功 |

### 5. Good / Base / Bad Cases

- Good：连续两个 Desktop Clear 各捕获一份不可变快照，UI 立即清空，worker 串行生成两个独立 UInk 和有序索引项；退出再排空剩余请求。
- Base：开关关闭、空 Desktop、Whiteboard 或 PPT 只执行原业务行为，`AutoSave` 根目录可以完全不存在。
- Bad：在 Clear 调用栈同步编码/写盘、用日期或数组下标充当唯一身份、让多个 detached writer 覆写同一 JSON，或在索引失败后删除未知 UInk。

### 6. Tests Required

- 纯 CPU 测试覆盖三态隔离、Desktop Clear/Exit 门控、Undo/Redo/空画布、可见快照与快照后继续绘制不变。
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
if (policy.ShouldCapture(workspace, enabled, hasVisibleContent))
    autoSave.Submit(DesktopAutoSaveTrigger::Clear, CaptureVisibleSnapshot());
ClearCurrentInterval();
// Host stop: PrepareExitAutoSave -> CloseAndDrain -> destroy controller/worker.
~~~

## Scenario: PPT 三态 slot、UInk 自动保存与当前进程恢复

### 1. Scope / Trigger

修改 Presentation target、Desktop/Whiteboard/PPT 切换、PPT 页切换、dirty revision、Presentation UInk 元数据/导入、索引、Host 退出屏障或多放映切换时必须应用本合同。本期只自动恢复当前 Inkeys 进程成功保存的 entry；稳定 COM 下的 SlideID 重排、插入和删除属于当前进程范围，跨进程冲突和用户决策仍留待后续。

### 2. Signatures

- `Bridge::PresentationTarget { key, bindingMode, sourceIdentity, bindingToken, pageIndex, totalPages, slideId, slideIds, targetRevision, processLocalIdentity }`。
- `Bridge::ProductState::presentationTarget` 与 scene-stamped `Bridge::Command` 共享不可变 target；`PresentationReadyIdentity` 是不含字符串/拓扑的固定大小 runtime ready 值。
- `PublishProductPresentationTarget(Bridge::PresentationTarget) -> optional<uint64_t>` / `ClearProductPresentationTarget()`。
- `PresentationAutoSaveService::{Start, SubmitSave, SubmitLoad, TryTakeCompletion, CloseAndDrain}`。
- `ShouldQueuePresentationSave(mutationRevision, queuedRevision)` 与 `ShouldEvictPresentationSlot(hasCommittedFile, mutationRevision, queuedRevision, committedRevision, loadPending)`。
- `StablePresentationTopologyChanged(previous, next) -> bool`；只比较同一 StableSlideId 文稿的有序 `slideIds`。
- `ImportApplicationOwnedPresentation(document, expectation) -> Draw3UInkImportResult`。
- 产品布局：`<AutoSave>/presentation/index.json(.bak)` 与 `<AutoSave>/presentation/files/<fileGuid>.uink`。

### 3. Contracts

- 绘制线程持有 Desktop singleton、Whiteboard singleton和 `map<PresentationKey, DocumentSlot>`；每个 slot 自带 document、history、当前页、页 runtime 与持久化 revision。切换必须 park/swap 整个 slot，然后执行完整 GPU reset/replay，不得共用或并行搬运容器。
- managed/native 必须把 key、binding、topology、page 和 `targetRevision` 作为一个共享不可变 target 事务发布。产品命令固定发布时的 workspace/target；Host 按 FIFO 执行 `captured scene -> command`，排空后再应用 latest state。ready 使用固定大小 identity，并同时匹配 workspace、key、binding revision、page/SlideID 和 target revision；仅页码相同不算 ready。
- `StableSlideId` 以规范化绝对路径作 source identity，provider 仅作诊断；无稳定路径时的 process-local identity 必须含 Inkeys PID、Office provider/PID/HWND、binding revision 和名称，防止同一 Office 进程重用未保存名称。
- 稳定模式写 `workspaceType=2 + hostId=PresentationKey + Canvas.slideId`；同一 SlideID 集合允许任意重排，新增页创建空 Canvas，已删除页以 retained marker 保留在同一 UInk/index，重新出现时按 SlideID 恢复。页码退化写 Inkeys 私有 `workspaceType=128 + inkeysBindingMode=page-index`，不伪造 SlideID；只有 strict importer 验证通过的应用自产文件才可解除 fallback/save-as 保护并原地覆盖。
- `SetPresentationTarget` 复用 active 或 parked/warm slot 时，必须先恢复并读取该 slot 的旧 target，再比较新旧有序 `slideIds`、按旧 SlideID 映射 Canvas，成功后才写入新 target；不得在 swap 前用新 target 覆盖 destination。普通翻页中的 `pageIndex`、当前 `slideId`、binding/target revision 变化不是拓扑变化，不能因此重建 document/runtime。
- dirty 是“自上次成功提交后发生修改”，每个 Presentation 文档只比较一组 `mutationRevision/queuedRevision/committedRevision`；Stored stroke、成功 Undo/Redo、有历史的 Clear、viewport 修改推进 mutation，Laser/预测/纯 Present 不推进。每次保存都是全部页快照，所以文档级 revision 足以让任一脏页在离页时触发，同时保证无变化零写。不得用 `currentPageHasContent` 作 PPT 保存门控；因此恢复旧文件后 Clear 再立即退出也必须覆盖为全量空 Canvas 集合。
- 保存触发在同 PPT 换页、A/B/workspace 离开和正常退出屏障。快照包含全部页（包括空页）；同一 key 待处理保存用 latest-wins 替换，in-flight completion 只能提交对应 revision。最终 scene-stamped 屏障位于前序 Clear/Undo/Redo 之后并扫描 active 与全部 parked slot；worker 无超时排空所有已接受请求。显式失败保留旧文件并在 completion 被绘制线程处理后恢复 dirty，但退出不承诺在同一屏障内无限重试失败 I/O。
- 同一 PPT 在当前进程内固定同一 `fileGuid/path`，以 `SaveExistingLogicalFile + expected SourceRevision` 原地覆盖。UInk 先 durable commit，再在命名 mutex 内原子发布严格 index；index 失败保留文件并记录 self-written revision，后续请求先验证并收敛，不得永久卡在 `SourceChanged`。
- clean inactive slot 只在有已提交文件、三 revision 相等且非 load-pending 时可淘汰。重入已淘汰 slot 必须走 `index -> ReadUInk -> strict import -> drawing-thread materialize`；dirty/pending/failed slot 保持 warm，不得被旧磁盘快照覆盖。加载期间清空 surface、不发布 identity-ready，并丢弃 physical contact/破坏性命令，直到 Loaded/NotFound/失败 completion 收敛。
- 稳定路径同 key/source 的 `PageIndexFallback -> StableSlideId` 在当前 session、页数及完整 SlideID 列表可证明 ordinal 对应时，可按 ordinal 一次性升级 slot；新放映 HWND/binding revision 不阻止同进程恢复。process-local 身份仍要求 exact binding token/revision。升级是持久化 mutation，原位覆盖同一文件为标准 Presentation 元数据；不满足证明条件不得局部混用两种模式。
- `presentation/index.json` 是严格 schema：source/key/fileGuid/path 均唯一，entry 恰含 source identity、key、sessionId、file/workspace GUID、relative path、binding mode、processLocal、binding revision、mutation revision、slideIds 和 UInk source revision。稳定模式的 `slideIds` 是已知 active/retained SlideID 并集，保存时只增不删；仅 `sessionId == ProcessSessionId()` 的 entry 可自动恢复。稳定路径 page-index 可在同 session/key/source、相同页数下跨放映 binding 按 ordinal 读写，process-local 必须 exact binding。foreign 或不一致内容返回结构化终态，不弹窗、不删未知文件、不静默覆盖。
- index commit 失败后的 self-written pending entry 只跨同一规范化 autosave root 的 Host generation 保留；切换 root 清除。I/O 使用保留大小写的绝对路径，folded root key 仅用于等价比较和 named mutex。

### 4. Validation & Error Matrix

| 条件 | 必需行为 |
|---|---|
| descriptor busy，或 shared 页码已更新而 descriptor 仍是上一页 | 仅 descriptor bindingRevision 与当前 target 相同才保留且不发 ready；新 binding 首次 busy/stale 必须隔离 |
| descriptor 明确 unavailable/无放映 | 切到隔离 Presentation slot 或 Desktop，不复用旧 key |
| 未修改或首次空 PPT | 不写文件；没有内容不等于没有修改 |
| 恢复后 Clear/Undo/Redo 到空状态 | mutation 推进，下一安全点覆盖同一 `.uink` |
| 稳定路径 fallback 在重开放映后取得完整 SlideID | 同 session/key/source 且页数匹配时按 ordinal 原位升级；process-local binding 不同则拒绝 |
| 稳定 SlideID 重排/插入/删除 | 按 SlideID 映射 active projection；新增页为空，删除页 retained 且不参与当前 ready，重新出现恢复原 Canvas |
| Desktop→同一 PPT 或 A→B 命中 parked/warm slot，且有序 SlideID 已变化 | 使用 destination slot 的旧 target 重映射后再更新 target；不得沿用旧 ordinal，也不得拿离开侧 active target 判断 |
| UInk 成功、index 失败 | 返回 `IoError`、保持 dirty；保留 self-written revision 供后续收敛 |
| index 指向 foreign session | 返回 `CrossProcessConflictDeferred`，本期不自动恢复/覆盖 |
| 文件 revision 与 index 不一致 | 返回 `SourceChanged`，保留原文件和 dirty slot |
| importer 身份/拓扑不匹配或含 Media/未支持语义 | 返回 Invalid/对应 import 错误，不部分 materialize |
| A save 慢时直接 A -> B -> A | 两 key 仍独立；A dirty/pending 保持 warm，旧 completion 不能更改 B |
| Host 重启 | 保留进程级 sessionId 和同根 pending index 修复状态，但清空上一代 queue/completion/Window command mailbox，不消费 stale callback |

### 5. Good / Base / Bad Cases

- Good：A 上绘制 -> 换页保存 -> 切 B -> 再进 A；clean A 走冷读取恢复全文稿，B 的画布/历史不变。A 重排或删除页面后仍按 SlideID 显示，删除页 retained，重新出现时恢复原 Canvas；A 恢复后 Clear 并退出，同一文件被覆盖为空。
- Base：PPT 从未修改，切页/退出零写盘；Desktop 与 Whiteboard 的 slot 不变。
- Bad：用当前页非空作 dirty，按页生成多个 `.uink`，用页码代替 `PresentationKey`，或 EndShow 后永久保留 clean warm slot 而让索引/导入路径不可达。

### 6. Tests Required

- descriptor/bridge 纯逻辑：Unicode 路径、provider-independent key、process-local binding token、重复 SlideID、A/B 同页、stale descriptor/busy 保持和 identity-aware ready。
- UInk/storage：stable/fallback round-trip、稳定 SlideID 重排/插入/删除与 retained Canvas、恢复后清空覆盖、单文件、index 首次/覆盖失败重试、self-written revision、foreign session、Host restart、latest-wins 与 A/B 独立。
- controller 状态：纯策略测试覆盖 load-pending 门控、clean eviction/冷恢复、dirty/pending warm、clear mutation、fallback 迁移与命令场景顺序；另以 `1,2,3,4 -> 1,3,2,4` 断言 parked/warm 需要重映射，并断言仅页码、当前 SlideID 或 revision 变化不重建 runtime。生产 Controller 的 slot park/swap、completion 和全部 parked 最终屏障由完整产品构建与静态调用链核对，真实呈现留给设备验收。
- 运行 `inkStrokeModelerTestTests.exe`、`InkeysHeadlessTests.exe --no-window`、managed PptCOM ownership harness，以及完整 `InkeysRepo.sln Debug|x64` 构建；真实 PowerPoint/WPS 放映、COM busy/损坏与 Office 进程退出仍须设备验收。

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
- `GetEffectivePenOpacity() -> float`
- `WindowController::SetProductVisualStyle(colorRgba, widthDip)` / `ProductVisualStyleSnapshot()`
- `ConfigureDrawingCursor(tool, appearance)` / `ResolvePrimaryDrawingCursorVisual(...)`

### 3. Contracts

- `stateMode` 只保存产品工具意图；`laserActive` 记录最后选择的 Pen 子类型是否为 Laser，不污染已记忆的 `Pen.ModeSelect`。它仅在顶层模式为 Pen 时映射为 Laser；选择、橡皮和图形必须覆盖当前工具但保留该记忆，返回 Pen 时恢复 Laser。
- bridge 只跨线程传递稳定工具、RGB 和粗细快照；活动笔画在 Down 时锁存 `ProductVisualStyle`，Hover 光标在帧边界跟随最新快照。
- 普通笔光标直径是 `max(widthDip, 5 DIP * dpiScale)`；只有最小光标值按 DPI 缩放，实际笔画粗细不重复缩放。
- 荧光笔当前绘制几何固定为 `6.25 × 50 px`，光标必须复用该尺寸；最终 alpha 为 `opacity * fillAlpha = 0.35`，Bar 显示同一有效透明度。
- Eraser Hover 整体 alpha 为 `0.5`，Contact 为 `1.0`；这一规则同时适用固定/速度橡皮、鼠标和倒转笔橡皮。
- Laser 当前没有产品宽度 state；光标与笔迹必须共用 `kLaserSolidDiameterAt96Dpi * dpiScale`。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| 颜色/粗细在 Hover 期间改变 | 下一光标帧使用新样式；已 Down 笔画不重染 |
| 荧光笔经过 Pen 默认 alpha 归一化 | 仍保持 `0.35`，不提升为 `1.0` |
| 橡皮 Hover / Contact | 分别为 `0.5` / `1.0` |
| Laser 尺寸调整 | 同时修改共享直径来源，不单改光标或笔迹 |
| 记忆 Laser 后切换到选择/橡皮/图形 | 当前工具按顶层模式发布，Laser 记忆保留；返回 Pen 后重新发布 Laser |

### 5. Good / Base / Bad Cases

- Good：颜色/粗细快照由绘制线程在帧边界消费，光标立即更新，活动笔画保持 Down 样式。
- Base：没有样式变化时不重复配置 appearance。
- Bad：让 resolver 无条件覆盖工具 alpha，或用普通笔宽度暗中驱动 Laser。

### 6. Tests Required

- 纯逻辑测试覆盖荧光笔 `0.35`、橡皮 Hover/Contact、鼠标、倒转笔和 Touch Contact。
- 静态检查普通笔最小 DIP 直径、荧光笔实际 `6.25 × 50 px` 尺寸以及 Laser 光标/笔迹共享 helper。
- 完整 ARM64 `Debug|ARM64` Solution 构建，再运行 `InkeysHeadlessTests.exe --no-window` 和 `Inkeys.exe --draw3-hidden-test`。

### 7. Wrong vs Correct

- Wrong：`Pen sample -> opacity = 1.0`，把 Highlighter 或 Eraser Hover 当普通笔处理。
- Correct：只对原本不透明的普通 Pen/Mouse appearance 做默认归一化，部分透明工具保留自身 alpha。

## Scenario: 选择模式、当前页内容与 Clear 截断

### 1. Scope / Trigger

修改产品模式桥接、Drawpad 显隐/消息穿透、Bar 选择布局、runtime history、Clear/Undo/Redo、页面切换或高精度计时器时必须应用本合同。

### 2. Signatures

- `Bridge::ProductState::selectionMode : bool`，默认 `true`；不得从 `WS_EX_TRANSPARENT` 反推模式。
- `DrawingControllerRuntimeObserver::currentPageContentChanged(void*, bool, uint64_t)`。
- `HostRuntimeSnapshot` 包含内容、已应用选择模式、请求/就绪输出目标及 revision、`presentedContentRevision`、`auxiliaryFullFrameClean`、`runtimeRevision`；状态线程使用 `WaitForRuntimeRevision()`。
- `InkCanvas::ClearStrokes()`。
- `ResolveDrawpadPresentationSurface(selectionMode, currentPageHasContent, auxiliaryFullFrameClean)`。
- `TimerPeriodController::SetSelectionMode(bool)`。

### 3. Contracts

- 当前页内容真值唯一来自 `CanvasRuntimeHistory::LastVisibleItem().has_value()`。Pen、Highlighter、Shape 和 Eraser history 都算内容；Laser 不算；Eraser 即使把视觉画面擦空仍算；Undo 到无可见项、Clear 或切到空页才为无内容。
- DrawingController 在文档初始化、Stored Stroke 成功进入 runtime history、Undo/Redo 成功、Clear 和页面切换后检查内容布尔值；Host 只在布尔值变化时递增单调内容 revision。每次成功 Present 记录实际目标、输出 revision 和对应内容 revision。
- presentation 状态固定为：非选择只显示主 Drawpad；选择先把最终 backbuffer 全量提交到辅助 ULW，再隐藏主窗并显示辅助窗；选择无内容时只有辅助完整帧 alpha 全零才隐藏两窗。换窗前必须满足请求/就绪 target 与 revision 一致且 `presentedContentRevision == contentRevision`。
- Window Service 用批量窗口位置命令确保两窗互斥可见；失败时先隐藏两窗再收敛到唯一目标。主 Drawpad 不得动态切换 `WS_EX_TRANSPARENT`。
- Bar 仅在“选择+无内容”隐藏 Eraser/Geometry/Recall 等绘制按钮；选择+有内容与非选择均保持完整布局，选择按钮文字恒为“选择”。产品路径不再注册或读取 Pierce/`penetrate.select`。
- Clear 只截断当前页：清 Stroke，并以全新 runtime 替换 history、undo/redo、before/after raster state，再分配新 raster token；丢弃热前像、composition cache/维护、恢复计划和 trusted L2，重置 Laser/粒子/瞬态层并全量透明呈现。保留当前页 viewport 和其他页面；之后 Undo/Redo 必须为空操作。
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
| Clear 后 Undo/Redo | 命令可消费但内容、revision 和画面保持空；旧 Stroke 不恢复 |
| 切换空页/有内容页 | 发布对应布尔值；其他页 history 与当前页 viewport 不受 Clear 影响 |
| `timeBeginPeriod(1)` 失败 | 不调用 end；同次非选择停留不重试 |

### 5. Good / Base / Bad Cases

- Good：有一笔的选择态由辅助 ULW 穿透显示，Clear 后等当前 revision 的完整 clean 帧再隐藏；另一页的 Eraser history 仍存在。
- Base：初始选择空页两窗隐藏；进入 Pen 先预热主 presenter 再显示，未落笔返回选择再次经辅助 clean 后隐藏。
- Bad：用窗口穿透样式推断选择模式、让主 Drawpad 承担穿透、复制固定 L2、视觉像素是否为空推断内容，或把 Clear 实现成可撤回 history 项。

### 6. Tests Required

- Headless 覆盖 `Primary/Presentation/Hidden` 解析，以及 timer begin/end 幂等、失败、模式往返和析构清理。
- 隐藏 HWND 集成覆盖双窗固定样式/owner/bounds、互斥可见、输出 generation 往返、clean 握手、Stored Stroke 内容发布、页面切换、Clear 截断和 presenter recovery。
- 完整 `InkeysRepo.sln Debug|ARM64` 构建，运行 `InkeysHeadlessTests.exe --no-window`、`Inkeys.exe --draw3-hidden-test` 与 `git diff --check`；不得启动可见窗口。

### 7. Wrong vs Correct

Wrong：`selection = current WS_EX_TRANSPARENT`、`主 Drawpad + WS_EX_TRANSPARENT`，或 `Clear -> append erase/blank history -> Undo 可恢复旧画面`。

Correct：`显式 selectionMode + LastVisibleItem 内容真值 + generation/content/clean 握手 -> 双 surface 互斥切换`；`Clear -> 新 runtime/raster token + GPU/瞬态全清 -> Undo/Redo 空操作`。

## Presenter 合同

- Draw3 自己创建 D3D11.1 hardware-first/WARP-fallback device/context、DXGI factory、swap chain 和 presenter，不注册 `Inkeys.UI.RenderPipeline` client，也不共享其 device epoch。
- presenter 模式顺序为 DComp -> DWM2 -> DWM/Win7 -> ULW；每次失败都销毁该模式的 swap chain/presenter 状态后再降级。DComp 在创建前通过能力探测决定是否预置 `WS_EX_NOREDIRECTIONBITMAP`；legacy 重建 Host 明确跳过 DComp。ULW 使用 premultiplied top-down 32-bit DIB、`1/255` CPU alpha 边界和 dirty rect，透明像素 alpha 必须保持零。
- 主 presenter 与辅助 ULW target 共用 renderer/final backbuffer，每帧只向请求目标提交；辅助初始化失败即 Host 启动失败，运行期失败必须重建并全量重试，不回退到主窗穿透。
- 只允许一个绑定主 Drawpad 的 `RealTimeStylusInput` producer；Draw2 RTS 不得初始化或重复发布。退出顺序为停止命令生产 -> 停止 RTS -> 唤醒绘制线程 -> 释放双 presenter/device -> Window Service 销毁两窗。

## 来源任务与验证状态

- 源任务历史统一保存到 `.trellis/tasks/archive/2026-08/draw3-source/`；其中 `active/` 保留源快照的 `in_progress` 状态但不进入目标 active task 列表，原 2026-07/08 归档按月份保留。
- 截至 2026-08-16，文件/资源映射、固定库 ABI、HWND/owner/Z 序、独立设备和唯一 RTS 约束已完成静态审计；ARM64 Debug/Release Solution 全量 Rebuild、`--no-window` 纯逻辑测试和隐藏 HWND 的真实 DComp/DWM2/DWM/ULW 合成测试均通过。测试按 DComp-compatible 与 legacy 两个顺序生命周期运行，并验证样式回调结果与实际样式一致；产品启动使用同样的显示前顺序重建合同。
