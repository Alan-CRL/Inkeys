# PPT 三态画布与 UInk 自动保存恢复实施计划

> 用户已于 2026-09-04 明确批准实施；当前任务为 `in_progress`，按以下顺序执行并在结束前复核全部门禁。

## 2026-09-05 实施记录

- Phase 0-9 的业务源码、工程登记和纯逻辑/存储自动化已完成；保存门控只看 mutation revision，已覆盖“恢复 -> 清空 -> 立即退出 -> 再次恢复为空”。真实 Controller 三态序列已加入屏幕外 harness 并编译，但执行因 `WS_VISIBLE` 与本轮无窗口约束冲突而被安全策略拒绝。
- Phase 10 的 x64 完整解决方案构建、两个 native headless 及 managed COM ownership harness、DLL/TLB 方法顺序、CRLF/BOM、`git diff --check` 和独立 `trellis-check` 已完成。
- Phase 11 仍按用户约束延期：本轮未启动 PowerPoint/WPS/可见窗口，不声称真实 RCW 进程退出或页面插删/重排冲突已验收。

## Phase 0 - Approval And Baseline

- [x] 用户审核最终 Goal、范围、共享 presentation claim 限制、PageIndexFallback 和 COM 释放合同，并在规划摘要后的后续消息中明确批准实现。
- [x] 加载 `trellis-before-dev`，完整阅读 native-desktop、ppt-interop、UInk、并发/资源和测试规范。
- [x] 执行 `task.py start 09-04-ppt-canvas-uink-lifecycle`；开始前再次检查工作树并保留用户已有改动。
- [ ] 记录 `InkeysRepo.sln Debug|x64`、`inkStrokeModelerTest.sln Debug|x64` 与相关无窗口测试基线。
- [x] 固定 PptCOM 原始 13 方法顺序/GUID 与二进制 blob 证据；生成 `.tlh` 确认 getter 只追加为第 14 项，部署 DLL/TLB 与构建产物 SHA-256 一致，`IdtDrawpad.cpp=None`。

完成条件：用户明确批准最新规划，任务进入 `in_progress`，基线可复核。

回滚点：规划尚未批准或基线已有相关失败时不开始源码编辑。

## Phase 1 - Remove EndShow Confirmation

- [x] 删除 `CheckEndShowClass` 声明、定义和全局实例。
- [x] 清理 `IdtPlug-in.cpp` 中仅供该弹框使用的 MessageBox import/undef；通用 I18n include 因文件其他 UI 文案仍使用而保留。
- [x] EndShow business command 保留队列与 dispatcher 去重；非选择模式无确认地切 Selection，再调用 `EndPptShow()`。
- [x] 确认 `PptCOM.NextSlideShow(bool check)` 与 PageControl/A2 EndShow 路由未改。
- [x] 不修改未编译 `IdtDrawpad.cpp` 与 facade 的空键盘 hook。

完成条件：生产 EndShow 没有模态确认，退出状态收敛与 COM 末页保护保持原合同。

回滚点：该阶段为独立小改；后续 PPT 生命周期出现阻塞时可以单独保留或撤回，不与文档模型绑死。

## Phase 2 - PptCOM Primitive Descriptor And Ownership Tests

- [x] 新增纯托管 DTO、binding revision 和短锁缓存；缓存只能包含 string/int/long/int[] 等纯值。
- [x] 新增单一 late-bound Slide identity reader：借用 bound Presentation/SlideShowWindow，当前 SlideIndex/SlideID 来自同一次 View.Slide，完整 topology 显式获取 Slides 与逐页 Slide。
- [x] 使用 `for + Item(i)`，每个 temporary 在 finally 中按 `Slide -> Slides`、`current Slide -> View` 精确 `SafeRelease`；不使用 foreach、链式 COM 属性或 temporary `FinalRelease`。
- [x] 将 busy、属性不存在、重复/非法 SlideID、数量不符区分为 StableSlideIds、TransientBusy、PageIndexFallback、Unavailable；任何失败不返回部分数组，并在 managed 分配前限制 10,000 页/32 KiB identity。
- [x] 只由 PptComService binding/monitor owner 在绑定建立、总页数变化和低频拓扑复核时更新缓存；event 只置 refresh-pending；COM 读取与 cache lock 不重叠。
- [x] `FullCleanup` 先使旧 cache/binding revision 失效，再按既有顺序解绑和释放长期字段；builder 与 cleanup 保持同一 service owner 串行。
- [x] 在 `IPptCOMServer` 末尾追加 `GetPresentationDescriptor()`；用 .NET 4 结构化 JSON serializer 输出 BSTR，clone/serialize 异常均返回 Unavailable JSON。
- [x] 新增 managed headless ownership harness/fakes，覆盖 acquisition/property 故障、PowerPoint/WPS 值差异、borrowed 不释放、temporary 恰好释放一次和 COM-valued scalar。
- [x] 由 PptCOM 工程重新生成并同步 `PptCOM.dll`、`PptCOM.tlb`，不手工改二进制。

完成条件：descriptor 不跨线程访问 RCW，pure dynamic 路径有故障注入释放证据，PptCOM 独立构建通过。

回滚点：新 descriptor 失败时保留既有页码 ABI并进入 fallback；不得为了“先跑起来”缓存 dynamic 对象或跳过释放。

## Phase 3 - Native Descriptor Validation And Deterministic Identity

- [x] native 通过既有 PptCOM snapshot 获取新 BSTR，并把 COM 调用/解析失败转为明确状态。
- [x] 用 jsoncpp 严格解析 12 字段 schema，限制 UTF-8 payload/string/active totalPage/slide 数量，校验 total/current、唯一 int32 SlideID 与 currentSlideId 对应关系。
- [x] `PresentationAutoSaveService` 使用进程静态 `ProcessSessionId()`；Host 重启不改变。PresentationKey 直接由规范 source identity 确定性派生，无额外 Registry。
- [x] 规范化 FullName 的跨进程、跨 PowerPoint/WPS source identity；provider 只作诊断；为路径缺失文档生成 process-local binding token。
- [x] index 严格校验确定性 source identity/PresentationKey；不修改 PPT Tags，也不另行分配 key。
- [x] 实现 stable、同 binding busy 保持、新 binding busy 隔离、page-index fallback、unavailable 和 fallback→stable 安全升级的纯逻辑测试。

完成条件：相同文档稳定复用 key，不同文档同页不碰撞；无/坏 ID 有显式 fallback 身份。

回滚点：解析/身份不可信时返回隔离 fallback/unavailable，绝不复用上一 PresentationKey。

## Phase 4 - Atomic SceneTarget And Identity-Aware Ready

- [x] 在 Bridge 定义固定身份类型、SlideBindingMode 与共享不可变 PresentationTarget；工具状态发布不能覆盖它。
- [x] 新增 `PublishPresentationTarget`，一次发布 key、binding、page、slideId、topology 和 targetRevision；重复目标不复制 10k 拓扑。
- [x] Bridge 命令固定发布时的 workspace/target；Host 按 `captured scene -> FIFO command -> latest state` 顺序送到绘制线程，覆盖 A Clear/Undo/Redo 后立即 Desktop/B 与退出屏障。
- [x] 扩展 HostRuntimeSnapshot/observer，ready 只发布固定大小 key、binding、page/SlideID 和 readyTargetRevision，不复制 topology。
- [x] PptInfo 只有在 identity-aware ready 条件完整匹配时更新 PptInfoStateBuffer；结束页投影仍沿用现有 UI 特例。
- [x] 纯逻辑测试覆盖 A/B target、scene-stamped command、target coalescing/reset/final FIFO、同/新 binding disposition；Window mailbox 在 Host attach/detach 清空上一代命令。

完成条件：任何有效 PPT UI 页码都对应正确文档与正确页已经在 Draw3 ready。

回滚点：不得退回两步 workspace/page publication；descriptor 不完整时宁可保持未 ready。

## Phase 5 - Three-State DocumentSlot Refactor

- [x] 将主/secondary swap 重构为 Desktop singleton、Whiteboard singleton 和 map<PresentationKey, slot>；保留现有 CPU 文档真值。
- [x] 把 pageRuntimeStates、current page、raster tokens 与 document 放进同一 DocumentSlot，禁止平行容器跨 slot 错配。
- [x] 为 Presentation slot 增加 binding topology、稳定 file/page identities、文档级 mutation/queued/committed revision 与 load/persistence 状态。
- [x] 抽取并复用完整的 document/page switch GPU reset/replay 序列；Desktop↔PPT 也执行与原 Whiteboard 边界同等级清理。
- [x] active contact 收尾前不切 slot；load pending 抑制输入/破坏性命令，成功 present 后才发布 ready。
- [x] 删除 `PptTouched`、ObservePresentationVisit 与 presentationVisitEpoch 临时门控，让 Desktop autosave 绑定 Desktop slot。
- [ ] 生产 Controller 的 Desktop→A→Desktop、A-Clear→Desktop 和 A→B→A hidden harness 已实现并编译；因 harness 会对屏幕外 HWND 设置 `WS_VISIBLE`，本轮安全策略拒绝执行。Whiteboard/真实 Office 切换保留 Phase 11。

完成条件：三态与每个 PPT 文档的 CPU/runtime 所有权明确，任何切换不串 Stroke、历史或 GPU cache。

回滚点：若 slot 重构未闭合，不删除 PptTouched；不得保留“枚举三态但实际共用文档”的半状态。

## Phase 6 - Presentation UInk Metadata And Strict Importer

- [x] 以默认值扩展 Draw3UInkExportSnapshot 的 workspaceType/hostId/currentPage/extra 与 Canvas slideId/extra，保持 Desktop 导出回归。
- [x] Presentation capture 遍历全部页及可见 runtime history，保留稳定 fileGuid/workspaceGuid/pageGuid，包含空页和 ordered binding。
- [x] Stable 模式写 workspaceType=2 与真实 SlideID；Fallback 写 private workspaceType、pageIndex 和明确 extra marker，不伪造 slideId。
- [x] 新增共享纯值 `uink_draw3_import`，只投影本程序支持的 canonical Presentation 子集，不改变完整 reader。
- [x] importer 校验 Header/fileGuid/hostId/binding/完整有序 topology/layer/viewport/content；拒绝 Media、高级内容、未知语义、unbound canvas 和 marker 混用。
- [x] 绘制线程 materialize InkCanvasCollection 与 CanvasRuntimeHistory，重算 footprints/raster tokens，redo 清空后完整重放。
- [x] 添加 exporter/import baseline、stable 精确 topology、fallback ordinal/升级、topology/extension/unbound/Media rejection 与存储 round-trip 测试；插删/重排自动解释延期。
- [x] 把新共享 module 登记到 Inkeys 与 inkStrokeModelerTest 测试工程，保持唯一实现源。

完成条件：同进程自产 PPT UInk 可完整恢复为可显示、可 Undo 的 Draw3 文档，外部高级文件不会被有损误导入。

回滚点：import 失败不改 active slot；切到隔离空文档并保留源文件。

## Phase 7 - Shared Presentation Storage And Index

- [x] 新增独立 PresentationAutoSaveService；复用 uink_file，不改变 Desktop service/schema。
- [x] 建立 `presentation/index.json(.bak)` 与 `presentation/files/<fileGuid>.uink`，目录不含日期或 process session。
- [x] 实现恰含 12 个 entry 字段的严格 index schema、Unicode source identity、根内路径、唯一 source/key/fileGuid/path、binding marker 和 UInk Header 自校验。
- [x] 用 folded presentation root key 派生 named mutex；实际 I/O 保留绝对路径大小写，持锁重读 index 后 claim/create/overwrite。
- [x] 以 `sessionId == ProcessSessionId()` 作为当前进程 claim，不维护 currentProcessEntries；foreign 一律拒绝，稳定路径 fallback 可同 session 跨放映 binding，process-local 必须 exact binding。
- [x] 首次文件先 durable commit 再发布 index；覆盖使用 stable fileGuid/path、expected SourceRevision 和 SaveExistingLogicalFile。
- [x] 实现 SourceChanged、foreign entry、主备损坏、孤儿、index 失败/self-written pending 修复的结构化终态，不弹窗口、不删除未知文件；pending 仅跨同 root Host generation 保留。测试覆盖主索引损坏后从匹配备份恢复，以及主备同时损坏时拒绝读写并保留 UInk。
- [x] 添加两个 service/writer、同 identity 并发、source revision 冲突、Host 重启 stale completion/pending 修复、root 隔离及 worker 异常注入测试。

完成条件：当前进程内同一 PPT 始终覆盖同一 canonical UInk；冲突时旧文件不被静默覆盖。

回滚点：Presentation 设置门控可关闭新提交；任何失败保持 Desktop 历史与已有 presentation 文件不变。

## Phase 8 - Dirty Tracking And Latest-Wins Save Queue

- [x] 每个 Presentation slot 建立文档级 mutation/queued/committed 单调状态；全量 snapshot 使任一页 mutation 脏化整份文稿，无需逐页/structure revision。
- [x] 在 Stored 内容提交、成功 Undo/Redo、有历史的 Clear、viewport 和身份安全升级成功路径推进；Laser/预测/纯 Present 零推进。
- [x] 抽取全 Presentation visible snapshot builder；遍历全部页并以 runtime history 构造内容，空页也保留。
- [x] 接入同 PPT 切页、A→Desktop/Whiteboard/B、EndShow workspace 变化和 Inkeys final barrier；Bridge 命令固定发布场景，Clear 后立即离场仍先 mutation。
- [x] 实现按 PresentationKey 的 pending replacement、in-flight latest follow-up、completion revision 对账和 CloseAndDrain。
- [x] 保存门控只比较持久化 revision，不读取 `currentPageHasContent`；首次从未修改的空文档不创建文件，恢复/保存后清空并立即退出提交空 Canvas 全量状态。
- [x] 完成回调通过 owned queue/control wake 回到绘制线程；worker 不持有 controller、runtime lock 或 GPU 引用，所有工作项异常不得逃出 jthread。
- [x] 纯逻辑/存储测试覆盖无变化门控、latest-wins、旧 completion mailbox 清理、失败终态/dirty 回退代码路径及最终已接受请求 drain；不声称失败 I/O 在同一次退出内无限重试。

完成条件：每个 dirty 边界最终提交最新完整版本，无变化零写入，后台 I/O 不阻塞绘制线程。

回滚点：若 completion/dirty 对账不可靠，先撤下保存触发，保留独立 slot；不得把入队等同于已保存。

## Phase 9 - Current-Process Restore And Extreme Switching

- [x] warm slot 优先；pending/failed slot 永不被旧磁盘内容覆盖。
- [x] clean inactive slot 成功提交后允许淘汰，建立 index→read→strict import 冷路径。
- [x] load 请求进入 owned worker，completion 带完整 target；加载期间不发布 identity-ready并抑制输入/破坏性命令。
- [x] 只读取当前 session 或同根 self-written pending entry；stable 要求有序 SlideID topology，稳定路径 fallback 按同 key/source、页数和 pageIndex，process-local 按 exact binding。
- [x] compatible stale load 可 materialize 到对应 active/parked slot并使用 latest target；不兼容 completion 丢弃，失败保留文件并使用隔离空 slot。
- [ ] Bridge/storage/policy 自动化已覆盖 A→B→A、latest-wins、同页不同 key、直接 scene 切换、fallback 重入和 Host restart；GPU Controller harness 已编译但未获准执行，慢 load/save 与真实呈现保留 Phase 11。

完成条件：用户要求的当前进程重入恢复与多个正在放映 PPT 直接来回切换均由自动化状态机证据覆盖。

回滚点：冷加载失败只退为空 slot，不能退回共享 Desktop/PPT 或 page-only stable ready。

## Phase 10 - Quality Gate

- [x] `rg` 审计全部 Workspace/Presentation target、PptTouched 删除条件、dirty 入口、save/load ownership 与 COM Release/FinalRelease 调用。
- [x] 运行 managed ownership harness 与 native descriptor/parser/state/index/import tests，包括 Presentation index 主备损坏 fixture。
- [x] 使用 x64 `MSBuild.exe` 对完整 `InkeysRepo.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo` 构建，0 errors。
- [x] 使用 x64 `MSBuild.exe` 对完整 `inkStrokeModelerTest.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo` 构建，0 errors。
- [x] 运行 `Build\x64\Debug\InkeysHeadlessTests.exe --no-window`、`x64\Debug\inkStrokeModelerTestTests.exe` 和新增 managed headless tests，均 exit 0。
- [x] 执行 `git diff --check`，逐文件核对原编码、BOM、CRLF、中文关键注释和最小修改范围。
- [x] 核对 PptCOM DLL/TLB 为本次生成且接口方法只在末尾追加；完整解决方案生成/编译了新 `.tlh`。
- [x] 使用 `trellis-check` 做规范、跨层数据流、复用、并发、失败矩阵和测试覆盖审查，修复后重复相关门禁。
- [x] 未启动常规产品、PowerPoint 或 WPS，未操控电脑、未创建 commit；屏幕外 hidden harness 的启动请求因 `WS_VISIBLE` 约束被策略拒绝。

完成条件：所有静态、managed/native headless、完整 x64 构建与 Trellis 检查通过；未执行真实 Office 验收明确记录。

## Phase 11 - Deferred Manual Acceptance

- [ ] 在用户另行明确授权可见测试后，分别用 PowerPoint 与 WPS 重复进入/退出、A/B 多实例切换、COM busy/损坏退化和快速翻页。
- [ ] 每轮结束核对 Office/WPS 进程是否按用户操作退出，不因 Inkeys 新增 RCW 引用残留后台。
- [ ] 验证真实 SlideID 插入/重排行为和 fallback 提示需求，为跨进程冲突任务补充证据。

本期代码交付不得把 Phase 11 未执行描述为已验证；是否因此保持任务 active 由最终验收时决定。

## Planned File Ownership

- `PptCOM/PptCOM.cs`、必要的 descriptor DTO/helper 文件、managed headless test 工程、生成的 `Inkeys/PptCOM.dll` 与 `Inkeys/PptCOM.tlb`。
- `Inkeys/IdtPlug-in.cpp/.h`、`Inkeys/IdtState.cpp/.h`。
- `Draw3.Bridge.*`、`Draw3.Product.*`、`Draw3.Host.*`、`Draw3.WindowControl.*`。
- `Draw3.DrawingController.*`、必要时对 `Draw3.InkDocument.*`/`Draw3.InkHistory.*` 做构造与 slot 所需最小扩展。
- `Draw3.AutoSave.*` 仅抽取确有复用价值的无业务 helper；Desktop 行为/schema 不改。
- 新增 Presentation storage/index module。
- `uink_draw3_export.*`、新增 `uink_draw3_import.*`、相关 `.vcxproj/.filters`。
- `InkeysHeadlessTests/`、`inkStrokeModelerTestTests/` 及 managed ownership tests。

不计划修改 `Inkeys/IdtDrawpad.cpp`、Whiteboard UI、UInk codec wire version或 Desktop 历史布局。
