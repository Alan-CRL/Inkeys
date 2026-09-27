# Research: 独立 PPT 结束页的身份、Draw3 页槽与 UInk 往返

- Query: 真正的 `ppSlideShowDone` 结束黑页如何成为同文稿内独立、可写、可保存和冷恢复的页面；比较同文件 synthetic canvas 与独立 UInk 文件。
- Scope: internal
- Date: 2026-09-25
- Status: 源码调查与实施建议；本 research agent 未修改产品代码、未运行测试。

## Files found

| 文件 | 职责 |
| --- | --- |
| `PptCOM/PresentationDescriptor.cs` | 读取 View.State、放映 HWND、文稿身份、总页数及正常页 SlideID 拓扑。 |
| `Inkeys/IdtPlug-in.cpp` | 可信会话/EndScreen 分类、目标发布、PageControl 提交回执。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h/.cpp` | `PresentationTarget`/ready 身份与 bridge 准入。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Presentation.h/.cpp` | descriptor 解析、正常目标构造、slot 复用和拓扑重映射判定。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 单文档 slot、页历史、切页、保存快照、冷物化、重排。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cppm/.cpp` | 保存/读取请求、索引、UInk 导出/导入、revision/失败完成。 |
| `inkStrokeModelerTest/draw3/uink_model.cppm` | UInk 无符号页序、`slideId` 与 `extra` 数据模型。 |
| `inkStrokeModelerTest/draw3/uink_draw3_export.cppm/.cpp` | Draw3 快照正规化、active/retained 重编号、编码。 |
| `inkStrokeModelerTest/draw3/uink_codec.cpp`, `uink_codec_encode.cpp` | 读取、写入并验证 Presentation UInk canvas。 |
| `inkStrokeModelerTest/draw3/uink_draw3_import.cppm/.cpp` | 应用自有文稿的严格拓扑导入及普通页投影。 |
| `inkStrokeModelerTest/draw3/uink_file.cpp` | 文件编辑会话与原子完整保存、读取后自检；另有 append 规则。 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cppm` | 可见页码提交门禁。 |
| `inkStrokeModelerTestTests/presentation_autosave_tests.cpp`, `uink_tests.cpp`, `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp` | worker/UInk 及真实隐藏 Host 的现有测试接缝。 |

## Findings — confirmed source behavior

1. `PresentationDescriptorReader.ReadShow` 对 State=5 认定 EndScreen；State 3/4 不走此分支（`PptCOM/PresentationDescriptor.cs:305-316`）。EndScreen 只取 Slides.Count 后提前返回，不枚举 SlideIDs，descriptor.status 仍通常是 `Unavailable`，currentPage=0（同文件 `347-357`）。路径、名称、HWND/PID 是提前取得的（`280-290`）。这解释了在结束页首次启动时目前缺少稳定拓扑，并非必须先观察普通页的产品要求。
2. native 只在 `pageStatus==Valid`、raw 页码/descriptor 页码一致时构造正常 target（`Inkeys/IdtPlug-in.cpp:649-657,724-731`）；EndScreen 只把 UI 发布为 `-1/N`，target revision 为 0，且旧目标输入被暂停（`746-784,795-810`）。`ResolvePresentationTarget` 仅接受正常 `StableSlideIds`/`PageIndexFallback`（`Draw3.Presentation.cpp:281-319`）。
3. Bridge target 的 `pageIndex`、`totalPages` 是无符号数，ready 身份含 key、binding mode、SlideID、pageIndex、binding/target/session revision；尚无页种类（`Draw3.Bridge.h:65-105`）。发布校验要求 `pageIndex<totalPages`，StableSlideId 还要求 `slideIds[pageIndex]==slideId`（`Draw3.Bridge.cpp:65-80`）。Host 的 UI-ready 判定使用同一 ready 身份（`Draw3.Host.cpp:1431-1463`）。
4. DrawingController 的 Presentation slot 保存整个 document、页 runtime、retainedSlides、当前索引、target、文件 GUID、mutation/queued/committed revision、加载态；进入/离开通过单 Host 内的 slot 交换（`Draw3.DrawingController.cpp:369-383,4415-4469`）。目标命令目前只接受 `pageIndex<totalPages`，并要求 document/runtime 正好有 `totalPages` 页（`5624-5637,5713-5723`）。
5. 每次切页会先捕获离开侧保存，再切换 document 并在需要时冷加载；加载未决会等待，不把旧画布当 ready（`Draw3.DrawingController.cpp:5624-5640,5720-5743`）。加载完成仅在当前/parked key 与 slot 可复用时安装；save completion 只更新匹配 slot 的 committed revision，失败可放回重试状态（`5405-5467,5558-5621`）。EndScreen 可以复用这一事务，但需让目标身份和页数合同同时接受专属页。
6. 普通保存快照要求 document 页数等于 PPT `totalPages`；稳定模式每个 active 页必须有对应真实 SlideID，retained 是 `map<SlideID, canvas>`（`Draw3.DrawingController.cpp:4536-4624`）。冷物化按真实 SlideID 构建 normal active 页，旧 retained 单独回填；重排也按 SlideID 重建，因而没有专门处理时会丢掉 slideId-less 结束页（`4735-4855,4865-4954`）。
7. UInk 的 `UInkCanvas.pageIndex/pageNumber` 与 `UInkWorkspace.currentPageIndex` 均是 `uint32_t`；canvas 有独立 pageGuid、可选 SlideID 和 `extra`（`uink_model.cppm:117-125,318-335`）。UI 的 `-1` 因此只能是显示/业务哨兵，不能转成 UInk 负页序或 `UINT_MAX`。
8. Exporter 的 `CanonicalCanvases` 将 active 先后、retained 随后从 0 连续重编号，覆盖调用方写入的 pageIndex/pageNumber；`WithPageStateMarker` 只替换 `inkeysPageState`，保留其他 extra（`uink_draw3_export.cpp:34-70`），并写入 SlideID/extra（`397-407`）。故需要把 end canvas 明确放在 normal active 的后面、retained 的前面；单独塞一个手写 index 不可靠。
9. 稳定 UInk workspaceType=2 的 **编码和解码两侧**都不能直接接纳无 SlideID canvas：encoder 拒绝（`uink_codec_encode.cpp:1169-1198`），decoder 标 `presentationUnbound` 和 TemporaryIdentity（`uink_codec.cpp:1286-1301`）；已有文件的临时身份使 `SaveUInkFile` 拒绝保存（`uink_file.cpp:832-848,851-875`）。解码器目前在读取 canvas.extra **之前**就做无 SlideID 分类（`uink_codec.cpp:1293-1301,1326-1331`），添加 marker 还必须调整判定顺序。
10. 严格应用导入器要求单 workspace、正确 hostId/binding、`currentPageIndex<expectation.pageCount`，逐 canvas 要求非 unbound、`pageNumber==pageIndex+1`；稳定模式每 canvas 必须有 SlideID，按 `knownSlideIds` 和当前真实 SlideIDs 归类，缺页从而由 Controller 创建，retained 只认已标记的旧正常页。fallback 分支只取 `0..pageCount-1`，因此裸追加 end canvas 会被丢掉（`uink_draw3_import.cpp:278-419`）。
11. AutoSave 的请求验证同样要求 snapshot/currentPageIndex 与正常 target 一致，active 数量等于 `target.totalPages`，每个稳定 active canvas 有对应真实 SlideID（`Draw3.PresentationAutoSave.cpp:465-532`）。索引的 `slideIds` 仅收集真实正 SlideID，并保持同源文稿唯一；同一 presentation source 写一个文件，保存时先严格导入旧文件、合并 pageGuid/Clear interval，再完整保存，index 提交失败保留 self-written pending entry（`265-343,573-620,624-797`）。读取同样先严格导入并按 interval 投影（`840-947`）。
12. 冷恢复的当前 hidden Host harness 已验证普通页 held-contact→保存排空→重启→SlideID 重排，但目前只检查 `.uink` 文件可读、正常页恢复；新增结束页必须断言读回的 pageGuid、marker 和笔迹内容（`Draw3.HiddenWindowTest.cpp:413-543`）。UInk 单元接缝在 `inkStrokeModelerTestTests/uink_tests.cpp`，worker 接缝在 `presentation_autosave_tests.cpp:148-190,610-710,729-787`。

## Recommended design — same presentation file, one marked end canvas

这是一项**建议**，不是现状或已验证实现。相比为同一文稿建立第二个索引/文件，它更直接沿用现有单 worker、保存失败重试、Clear interval、进程内恢复和 index/source revision 边界。

### Runtime identity

- 给 `Bridge::PresentationTarget` 和 `PresentationReadyIdentity` 加 `PageKind { Slide, EndScreen }`。对正常页，`pageIndex∈[0,N)`、`slideId` 与真实 slideIds 匹配；对 EndScreen，`pageIndex=N` 专指**内部 document slot index**，`slideId=null`、`totalPages=N` 仍是真实 PPT 总页数，`slideIds` 仅含 N 个真实 ID（fallback 则为空）。EndScreen 身份需带同一稳定 `PresentationKey`、binding/session/target revision。`ReadyIdentityFor` 与 bridge 同目标判等都包含 kind。仅 `PageKind::EndScreen` 能通过 `pageIndex==N`；Unknown/Unavailable 不因此进入 target。
- 在同一 `DrawingDocumentSlot` 中始终或最迟首次进入时维护 N 个正常页加 1 个专属 end 页，end pageGuid 独立；历史/Undo/Clear 指向当前内部 index N。正常页仍按 SlideID 重排，end pageGuid 与 runtime 随文稿保留并在新增/删除后的新 N 处安置，绝不进入 `retainedSlides` 或真实 slideIds。若采用按需创建，复用判断须容许 legacy N 页及新 N+1 页，冷加载完成必须补出空 end 页后才 ready；始终创建 N+1 可简化不变量。
- `PresentationKey`/文件索引不包含 show-session revision，避免每次重入换存储页；session/binding/target revision 只作为 runtime 的过期回执屏障。普通 slide 和 EndScreen 交接仍走 capture old→隔离 contact→切 slot index→成功 Present→同 target UI commit→开放输入。

### UInk representation and strict round trip

- 在同一 `.uink` 内把 end canvas 作为 normal active N 页之后的第 N+1 个 active canvas。`canvas.pageIndex=N`、`pageNumber=N+1`、无 SlideID、独立 pageGuid；`canvas.extra` 有且仅有一个 Inkeys 专属、版本明确的 `inkeysPageKind=end-screen` marker，并保留既有 binding/pageState extra。`workspace.currentPageIndex=N` 仅当结束页当前活动；`header.pageNum` 是内部 canvas 数（再加 retained），`index.json.slideIds` 和 UI/COM 总页数始终为 N。
- 为此 marker 在 `uink_codec_encode.cpp` 和 `uink_codec.cpp` 做**仅针对 workspaceType=2 且合法 marker 的狭窄无 SlideID 例外**；decoder 在检测 presentationUnbound 前解析 extra。没有 marker 的稳定 PPT canvas 仍是 TemporaryIdentity；有 SlideID 同时标 end、重复/错误 marker、多个 end page 由应用导入器拒绝。当前 PresentationAutoSave 走完整 SaveUInkFile，并不走 `uink_file.cpp` 的 append batch；若未来需要 end canvas 的 append，则其 `1224-1238` 也要同步扩展，不能以完整保存通过来推断 append 通过。
- `ImportApplicationOwnedPresentation` 保留原有普通页 SlideID/ordinal 及 retained 规则，另验证**零个或一个** end canvas；若存在，必须位于真实页之后、retained 之前，具有唯一 pageGuid、index N、无 SlideID、正确 marker/binding。旧文件缺 end 时返回“无 end”并由 Controller 生成空页。已含 end 的文件验证 marker、数量和 index，工作区 currentPageIndex=N 只在 end 存在时有效。stable 导入时 end 独立于 `bySlideId`；fallback 导入时必须显式携带，不可只遍历 0..N-1。重复 end 或多余无 SlideID canvas 整体拒绝，不把旧末页墨迹迁移为 end。
- 保存请求按内部 `N+1` 页验证，但 `target.totalPages`、稳定 `target.slideIds` 和 index.json 中的 ID 集合仍只有真实 N 页。`MergePresentationSnapshot` 继续按 pageGuid 合并 end 的 stroke/Clear interval，`PreviousInterval` 的 end pageGuid 继续走既有路径。完成回执沿用 key/slot/mutation revision；对同一 key 的不同场次还需确认当前/parked target 的 page-kind/代次不会让迟到 completion 激活新场次。已有完成路径只改提交计数，不直接改变当前页（`Draw3.DrawingController.cpp:5405-5467`），但加载安装需守住当前 slot revision。
- 旧 N-canvas 文件应可冷读；新 N+1 文件应能 `ExportDraw3SnapshotToUInk → SaveUInkFile → ReadUInkFile → ImportApplicationOwnedPresentation → materializePresentationSlot`，并在同一文件内证明最后一张 slide 与 end 的 GUID、笔迹、Clear/Undo history 均独立。`SourceChanged`、索引提交失败和旧 completion 继续按现有 worker 语义重试，不能用新 pageGuid 覆盖已有 end page。

### EndScreen entered before any Valid page

- `PptCOM/PresentationDescriptor.cs:351-357` 的提前返回应移到同一次 `Slides` acquisition 完成受限、完整的 SlideID 枚举之后；保持旧 12 字段 descriptor schema、`status=Unavailable` 与 `pageStatus=EndScreen`，只为新会话接口 EndScreen 携带经过验证的 `slideIds`。native 使用**专用 EndScreen target 工厂**验证 `pageStatus`、可信 session/HWND/PID、binding revision、真实 `totalPage`、同次完整 topology，不通过正常 `ResolvePresentationTarget` 的正页检查，也不从另一场会话复制末页 target。
- 对无法枚举 SlideID 的 WPS/忙碌提供方，可以尝试明确的 process-local page-index fallback：必须在加载结果为匹配 fallback 或 NotFound 后才准许产生可保存的 ready；若磁盘已有 stable 文件而本次 topology 不足，保持暂不可确认/重试，不要进入一个之后只能 `SourceChanged` 的可写空页。`TransientBusy`/Unknown 绝不制造 EndScreen target。这个分支需设备验证，不能仅凭源码保证所有 Office/WPS 组合。
- UI `-1` 只在持有 `PageKind::EndScreen` 的非零 target revision 时可确认；扩展 `PageControl.cppm:635-644` 与 `IdtPlug-in.cpp:134-144` 的提交判定，明确匹配 target kind、session、revision、`total=N`。所有控件隐藏时沿用现有 `requiredMask` 退化，正在淡出的旧数字仍等待已呈现表面交接（`PageControl.cppm:616-644`）。

## Alternative B — separate legal UInk page file

可给 end 页单独使用合法 `workspaceType=128`/一页 UInk，避开稳定 workspace 的 SlideID 缺失规则；但需要建立第二索引项或衍生 storage key、同文稿双文件关联、两个 revision/失败重试序列，以及 Host 内普通/end slot 的切换和跨文件 CloseAndDrain。现有索引严格恰好 12 字段并按 sourceIdentity 只找一个 entry（`Draw3.PresentationAutoSave.cpp:288-343,639-645`），所以这不只是“另存一个文件”。若不同时改事务/关联，普通页的 Clear 或退出保存可能覆盖结束页或使它不可达。故在当前单 worker/单文稿 slot 设计中不推荐。

## Concrete test seams / acceptance evidence

- `uink_tests.cpp`：marker 合法组合的 Encode→Decode 不出现 `presentationUnbound`/TemporaryIdentity；无 marker 的无 SlideID、重复 marker、marker+SlideID、错误位置/重复 end 保持拒绝；旧普通 Presentation 文件正常导入。
- `presentation_autosave_tests.cpp`：一张普通页与 end 各有不同 pageGuid/墨迹；同文件 save/load 与 Clear interval；真实 PPT 页数不变；SlideID 增删/重排后 end 不被 retained 化；index commit 失败重试、SourceChanged、旧文件无 end 的冷加载。
- `Draw3.HiddenWindowTest.cpp`：生产 Host 在末页 A→End 空→B→末页 A→End B，独立 Undo/Clear/Redo；停止/排空、直接读 `.uink` 内容、重启同根冷读取并恢复 B；在 End 先启动、单页 PPT、held contact 与 UI ack 迟到情形复测。现有 `CheckPresentationPersistence` 是可扩展入口（`413-543`）。
- `InkeysHeadlessTests/presentation_descriptor_tests.cpp`、`ppt_session_tests.cpp`、`draw3_bridge_tests.cpp`、`page_control_tests.cpp`：EndScreen 工厂、严格 identity、`-1/N` 回执与普通页不回归。Managed `PptCOM.Tests`：State=5 时同次拓扑、busy/缺失、不泄漏 COM temporary。
- 本调查没有运行构建、UInk、Host 或真实 PowerPoint 测试；上述是应执行的验收项目，不是已通过记录。

## Related specs and external references

- `.trellis/spec/ppt-interop/com-contract.md`、`native-session-ui3.md`：现有 descriptor/会话合同。`native-session-ui3.md` 当前“Unknown/EndScreen 暂停写入”是**待本功能修改后需更新**的旧规则；Unknown 仍必须安全禁写。
- `.trellis/spec/native-desktop/draw3-integration.md`、`input-and-ink.md`：单 Host、slot、UInk、contact 边界及保存合同。
- 外部参考：无。本调查只依据当前源码和用户提供的 State=5 语义，未在 Office 上验证事件时间或可用属性。

## Caveats / Not Found

- 只读代码不能证明真实 PPT 的 `Slides` 在 State=5 时总能成功枚举；稳定拓扑若暂不可用，必须明确重试并把该设备组合标为未验证。
- 当前 UInk `uink_file.cpp:1217-1238` append batch 路径仍强制 Presentation canvas 有 SlideID；所荐同文件方案依赖当前完整保存路径。若产品改用 append，必须同步处理该校验。
- 对 fallback 文件在幻灯片增删后的 ordinal 对应关系，现有 `CanReusePresentationDocumentSlot` 以页数一致为界（`Draw3.Presentation.cpp:372-383`）；无法以 stable SlideID 证明时应隔离而非臆测映射。
- 本文件只给出存储/目标实现边界；实际桌面退出穿透故障由另一调查负责，不能以结束页保存设计推断其运行时根因。
