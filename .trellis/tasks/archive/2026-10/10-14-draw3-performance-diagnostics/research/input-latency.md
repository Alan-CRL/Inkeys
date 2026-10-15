# Research: Draw3 输入延迟与内容回执

- Query: RTS producer → Contact mailbox → Draw3 消费 / Modeler / prediction / geometry → Renderer → 成功 Present 的 Down、Move、Up 延迟，最小诊断关联与证据边界。
- Scope: mixed（本地生产代码 / Spec，Microsoft 官方 API 文档）
- Date: 2026-10-14

## Findings

### 结论与最小实施建议

1. 不扩 `ContactSnapshot`、`ContactRecord` 或正常逐点几何。复用已有 `qpc`、偶数 `sequence`、`ContactHandle.generation`，在诊断会话中预分配独立 producer 事件数组和 owner 回执数组，退出并停止两个生产方后离线关联。正常模式没有新增计时、统计、分配、I/O 或同步；新增观察尽量置于现有 `metricsState_` / `EnableDiagnostics` 的门禁内。
2. 精确 publication 键必须在 Coordinator 的真实写入接缝捕获，采用 `(opaque record value, generation, assigned even sequence)`；不能在 `PublishMove` 返回后读取 record 再猜序号，届时可能已有更新或回收。record 地址只作诊断会话内值键，不导出指针，不延长寿命，不在后续匹配时解引用。
3. 本次要求“从 RTS 回调得到输入”必须另取 callback-entry QPC。现有 `snapshot.qpc` 是解码前取样，晚于 Down/Up 的 state-writer 等待与 decoder/binding 处理，不包含完整回调前段。保存两个原点并明确名称：`callbackEntryQpc` 与既有 `snapshotQpc`。
4. 对于 Move，只在该 sequence 对应的真实非预测几何被采纳、成功提交并进入本帧实际合成，且 Present 的 canvas/content/output/frame 身份匹配时，记录“真实内容的软件 Present 延迟”。只消费 latest snapshot 或更新 modeler 不代表已绘制。
5. 现有 RuntimeMetrics 是每个 contact 仅一次 Down landing；Down 已 confirmed 后 Live note 不再参与后续 proof，Up 的 Stored note也会被跳过。逐 Move/Up 应由诊断 sidecar 自己保留后续 sequence 回执，不能强行复用现有 Session 的一次性 Down pending/status。
6. Up 分开报告 `upConsumed`、`cpuStored`、`authoritativeFinalPresented`；CPU 文档提交成功不能替代 Present。自然 Laser Hold/Fade/expiry 是工具生命周期，不计作输入延迟。Laser 的现有正式 Down/Up landing 不支持；最小首版应给 consumed 延迟、帧成本与 `proofSupported=false`，或专门增加真正 Laser 提交/合成 proof，不能借普通笔 proof 出零值。

### Files found

以下 `Draw3.*` 文件均位于 `Inkeys/Inkeys/Drawing/Draw3/`。

| 文件 | 用途 |
| --- | --- |
| `Draw3.RealtimeStylus.cpp/.cppm` | 唯一 RTS producer；Down/Up 串行 state writer，Packets/InAir 无等待只读 gate；解码最后一个 RTS 包与发布 Contact。 |
| `Draw3.ContactInput.cpp/.cppm` | 稳定 slot、不可变 Down、最新 Move 覆盖、一致序号、route/generation、可靠终态、合并控制唤醒及 opt-in 计数。 |
| `Draw3.DrawingController.cpp/.cppm` | 唯一绘制 owner，工具准入、modeler、真实/预测点、几何/栅格、history、backbuffer 合成和 Present；内含现有内容 proof 与纯 CPU probe。 |
| `Draw3.RuntimeMetrics.cpp/.cppm` | 固定预算软件 Down landing / frame / Present 样本，proof 校验，退出后 schema2 导出。 |
| `Draw3.RendererPrimitives.cpp` | 普通 Stroke / Highlighter / Shape 的 Map、CPU 写 buffer 与 D3D Draw 提交返回。 |
| `Draw3.TransparentPresentation.cpp` | DComp Present1 与 ULW GPU→CPU Map / dirty copy / UpdateLayeredWindow 边界。 |
| `Draw3.Host.cpp/.h` | 显式 metrics 开关、Session 分配、RTS/绘制线程生命周期与停止后导出。 |
| `Draw3.PerformanceProbe.cpp/.h` | 现有无 HWND 激光增量几何规划基准；并非全链路 latency 测试。 |
| `Draw3.HiddenWindowTest.cpp` | 真实 Host metrics 生命周期 smoke，但会创建隐藏 HWND；本研究不运行。 |
| `InkeysHeadlessTests/draw3_contact_tests.cpp` | Coordinator 生命周期、代次、并发 Close/Discard 与发布/读取 microbenchmark。 |
| `InkeysHeadlessTests/animation_tests.cpp` | `--no-window` 和 `--draw3-contact-benchmark` 入口。 |
| `Inkeys/IdtMain.cpp` | GUI 初始化前退出的生产 CPU/GPU probe 与隐藏测试命令入口。 |

### 1. RTS 回调原点与掉包语义

- `Draw3.RealtimeStylus.cpp:1137` 的 StylusDown 在 `:1141` 记录 arrival，随后 `:1147` 获取 writer guard，查询 decoder、修复 duplicate Down、插 binding；到 `:1195` 才把 `QueryQpc()` 传给 DecodeSnapshot。`DecodeSnapshot :1002` 保存该 QPC，`:1225` 才 PublishDown。现有 QPC 包含解码/准入后段，不包含回调入口到该取样点的等待。
- StylusUp 同理：入口 `:1241`、writer guard `:1250`、解码前取 QPC `:1270`、发布 `:1296`。坏/缺 Up 包改走 Cancelled 并沿用最后有效位置，见 `:1256`、`:1273`；必须报告 `cancelled/fallback`，不能算成功 Up landing。
- Packets `:1346` 接受 bundle，但 `:1358` 仅取 `lastPacket`；前面的 `packetCount-1` 样本是既有主动合并，不是本次诊断丢失。`:1359` 的 RtsPacketStateGuard 忙时直接返回，`:1375` 才取 snapshot QPC，`:1393` PublishMove。应分别计 callback bundle 数量、已收到 packet 数量、last-packet 选择、state-busy、decode-failed/no-binding、publish outcome。
- 鼠标可由 RTS 给 `MouseLeft/MouseRight`，Down `:1213` 按实际按钮解析。WindowControl 的 WM_MOUSE 兼容光标链不能冒称 Contact 绘制 producer。
- 现有 Cursor 输入日志是稀疏人读信息：`RecordInputDiagnostic :1534` 关闭立即返回；Packets `ShouldRecordInputPacket :1544` 每 plugin 每秒最多一次。它既没有完整精确 publication key，也不能给毫秒级全量输入延迟分母。
- 源设备绑定 decoder generation（例如 `:1181`）与 Contact slot generation 是不同身份，不得相互替代。后者负责回收复用保护，前者说明 RTS context 换代。

### 2. Contact mailbox：不是逐 Move FIFO

- 现有数据无需增加字段：`Draw3.ContactInput.cppm:56` snapshot，`:66` QPC，`:68` sequence，`:69` admissionRevision；`:108` ContactRecord，`:149` handle 指针值+generation。
- `Draw3.ContactInput.cpp:211` Initialize 把 Down sequence 固定为2并复制不可变 downSnapshot，`:235` 设置新 route。`:737` PublishDown 获取 slot、`:752` 增 generation、`:756` 锁存 admissionRevision、`:760` 入队。只有 Down record 与控制 token 入队。
- `PublishSnapshot :265` 在 writer 内读当前 seq，`:269` 发布奇数更新态，`:281` 写原始 snapshot QPC，`:283` 发布 `sequence+2` 偶数一致态。这是捕获真实 publication 键的最佳接缝，可用局部值而无需额外读取 record。
- `PublishMove :771` FindProducing / TryLockWriter 不等失败可丢；`:791` 写最新 snapshot；`:795` 另检查 route 并发关闭；`:801` 明确覆盖最新状态、不触发活动帧 wake。`moveContended` 也包含没有 producing route、generation/match 失败，不能在分析里只解释成锁竞争。
- `Close :564` 先 CAS Producing/Quarantined→Closing，`:586` 才等待 writer；`:615` 写可靠终态。ClosingDiscarded 或 quarantine 可能由 producer 回收，不能假设每个终态都被绘制 owner 消费。
- `TryReadSnapshot :820` 前后核 route/generation，`:832` 调 `ReadSnapshot :286`，最多32次比较奇偶序号，终态只在 ConsumerOwned 返回。绘制读失败不自动表示输入丢失；它可能只是竞争或代次不匹配。
- `Count :362` 只有显式 `diagnosticsEnabled` 才 fetch_add；`:1195` 启用，`:1200` 读取当前计数。`occupiedSlots` 是占用 Contact slot，不是 Move backlog，也不是精确 queue depth。
- 相邻已消费 seq 的差值 `/2` 可估算之间发生的已发布更新数，但无法凭差值恢复位置、QPC、实际 callback 失败原因。与 sidecar publication/consume 集合比较才能判定具体 sampled seq `overwrittenBeforeConsume`。Up seq 也递增，计数须按 phase 分组。

### 3. 唯一 owner 的消费、模型、预测与几何

- `Draw3.DrawingController.cpp:8560` initializeStroke 核 handle generation并在 `:8563` 读取不可变 Down；Down 落后于 Up 仍可先初始化起点。工具真实解析在 `:8730`，metrics 登记/Down consumed 在 `:8736`–`:8737`，前面的 suppress/touch pan/选择准入路径必须单独记录拒收/导航 outcome。
- `consumeLatestSnapshot :9496` 在 `:9501` 读取一致 snapshot，重复 sequence 不消费；`:9504`–`:9505` 更新 lastConsumedSequence/lastInputSnapshot；`:9509` NoteConsumed，`:9510` terminal，`:9517` cancel 失效。这里可取 owner consume QPC，计算输入 age 与 callback→consume，而不是用 modelInputTime。
- modelInputTime 是工作算法时间：`:9585` 从 snapshot QPC相对qpcOrigin计算，`:9598`/`:9601` 对 endpoint/单调性修正；不是测量真实 wall-clock 的诊断时钟。
- Down Modeler.Update 在 `:9244`，失败 `:9248` 失效清理；真实输出/RawDown/Shape 采纳 `:9260`。Move/Up Update `:9632` 后真实几何变化才在 `:9660` Adopt；RawTerminal fallback、Shape 最终端点分别见 `:9376`、`:9718`。
- prediction 以单独 predictedResults/points存储；例如 `:13766` Predict、`:13770` RebuildPredictedPoints，真实 prefix `:13771`–`:13779`；Up `:13796` 清旧 prediction。不要把预测提前显示的 tip 当 sampled RTS 点已绘制。
- 没有新 packet 时可为停笔收敛主动 Update，`:13420` stationaryInput → `:13433` 模型 → `:13446` 对原lastInputSnapshot再采纳。诊断应保留新内容token或迭代frame，但同一 sampled sequence只报第一次有效软件呈现，不能造新 RTS 输入。
- 重连在 `:8994` 精确终结旧 handle键再接新 generation；`:9737` awaitingReconnect 暂存 Up。最终延迟须保留 deferred-up 事实，不能把回收旧handle当作所有数据完成。
- 现有 frame成本在 `Draw3.RuntimeMetrics.cppm:57` 说明父段包含子段；`MetricsModelCall :2343` 聚合 Reset/Update/Predict，`:13307` GeometryRasterSubmit跨模型/几何/提交，`:14314` Composite。不能把所有 stageWallMs简单相加得到总成本，也不能把每帧全体 contact成本分摊后冒称单点成本。
- `Draw3.DrawingController.cpp:14495` 与 `:14521` 在工作完成后等帧预算。Move不唤醒活动帧，因此 consume age包括锁帧/调度/上一帧工作；这是解释“不跟手”的必要因素。完全空闲 `:13231` 先结束render span再阻塞，idle wait不混入帧work。

### 4. 内容 proof：可复用的严格门与一次性限制

- `Draw3.RuntimeMetrics.cppm:24` canvas身份由workspace/page/scene/raster/output组成；`:46` proof还包含contentToken、itemToken、consumedSequence、kind、frameSerial。
- `ControllerContentMetrics::Register :2412` 保存真实Down QPC与工具；`:2448` NoteConsumed只更新数值。`Adopt :2452` 要非预测几何、正确phase、非零seq/QPC、与consumedSeq一致、source时钟单调、同admissionRevision，然后分配contentToken与geometrySequence；这能拒绝“只消费未采纳”的伪成功。
- `ObserveLiveRaster :2480` 必须sharedLayersSucceeded、当前frame、同scene、正确adopted/geometry seq；`:14245` owner在真实渲染后由工具实际几何决定realContribution/realBounds，`:14281` 观察同seq栅格。不能拿这个调用名称当GPU完成时间，它是CPU提交侧软件事实。
- `FreezeCandidates :2807` 要当前canvas/raster/viewport/history/output、成功真实合成与dirty范围；Live `:2863`–`:2872` 还核adoption/raster/frame，Stored `:2874`–`:2884` 必须当前权威L2与完整投影覆盖。`PresentFull=true`自身不是完整内容证明。
- `PresentReturned :2888` 先独立记每次真实attempt/result，`:2894`–`:2898` 检查current frame/output/composite/signature，再 `:2925` commit。成功returnedQPC取在 `PresentFrame :7562`，在观察回调与后续诊断前；这里最适合复用timestamp。
- 现有一次性限制：Live `:2855` 和 `:14253` 在 `landingConfirmed` 后跳过；`CaptureStored :2501` 对confirmed Down直接清source，`:2503` 明确不创建第二Down landing。`RuntimeMetricsSession::StageVerifiedLanding` (`Draw3.RuntimeMetrics.cpp:549`) 只接受Registered/Pending；Confirmed不会再接Move/Up。
- `Draw3.RuntimeMetrics.cpp:579` CommitVerifiedLandings以contact.downQpc算总时长，不以proof.consumedSequence的原始QPC算；因此即使首个landing由Move/Up内容证明，仍是Down latency。Stored可跨失败帧等同内容权威回执，Live必须当前帧restage (`:593`)。
- Live realBounds通常是整段realPoints的bounds，`:2869` 只要求与合成区相交；现有proof对“该输入采纳的真实内容已提交与合成”有软件证据，不能证明原始RTS坐标某像素已写入。若要严格声称“本点”，应增加诊断侧实际新增/变化几何的覆盖区与合成包含关系；否则名称保持real-content/software-Present。

### 5. sidecar接线草案与输出口径（建议，不是已实现）

- Host在诊断启动时预分配固定容量，设置observer/recorder指针；两个生产方停止后才导出。`Draw3.Host.cpp:365` 普通metrics关闭直接返回，`:375` Prepare；`:476` 导出要求joined/sealed且拒绝未启动/失败空报告。Controller `:7365` Prepare仅在有Session时创建旁挂，失败 `:7367` 关闭诊断而不影响产品。
- RTS对采样callback建立固定局部上下文：`callbackToken, callbackEntryQpc, tcid,cid,phase,packetCount,outcome`。若要精确区分同一QPC tick的并发callback，应将token通过诊断可选Publish参数/observer上下文带到Coordinator，或仅在诊断分支设定并自动清理thread-local scope。优先显式参数，不以坐标/QPC模糊join。无回调来源的测试注入/CloseAll标为synthetic/lifecycle。
- Coordinator在Down Initialize/Enqueue与PublishSnapshot持writer位置用局部值记录 `callbackToken, opaque record, generation, assignedSequence, snapshotQpc, publicationQpc,phase,result`。不要多一次ReadSnapshot，不获取diagnostic锁等待。不让consumer等待sidecar publication写完；离线join容忍事件在数组中的到达次序不同。
- publicationQpc需明确其精确采样位置（commit前、commit后或函数return前）。若commit后采样，consumer可能已读取，跨线程相邻边界出现1 tick逆序不可凭此宣布产品错序；保留边界口径，负interval标unknown/quantization。callback与最终PresentReturn仍可作为软件总延迟原点终点。
- owner在实际消费复制纯值key，在模型/实际几何处理前后记录采样输入span；Adopt成功后记录 `geometryAdopted,contentToken`；栅格成功后记录 `rasteredSequence,frameSerial,canvas,realContribution`；真实合成后freeze proof与output身份；PresentReturned只对同内容成功帧收尾。后续Move/Up sidecar note不受Down `landingConfirmed`抑制。
- 一个低损耗实现可只采样Move（每N个callback或固定采样预算），全量保留Down/Up结果与aggregate原因计数，并保留采样比例。未采样不等于dropped；高频时避免每个packet/每个geometry顶点取QPC，不在RTS调用GetThreadTimes/console/disk。owner已存在frame成本可复用。
- 若不实施完整per-event模型/几何span，先导出精确callback→publish→consume、内容proof→Present以及frame-level四段成本，并把缺失per-point阶段标null/unsupported，不能把frame阶段成本填成点阶段时间。用户本次完整阶段需求可能需要补采样span，规划时应明确这一工作量。
- 保留原始QPC和频率，最终毫秒离线计算；duration不支持/计时失败/无proof给null和reason，不能给0。

| 诊断量 | 原点与终点 | 解释 |
| --- | --- | --- |
| callbackToSnapshotMs | callbackEntryQpc→snapshotQpc | 回调state gate、decoder/binding等前段；不含设备/驱动在回调前的时间。 |
| callbackToPublicationMs | callbackEntryQpc→明确口径publicationQpc | RTS归一化、publishing/终态writer等待。 |
| publicationToConsumeMs | publication边界→owner实际consume | Down排队、帧调度、上一帧工作；Move mailbox年龄，非逐点FIFO backlog。 |
| snapshotAgeAtConsumeMs | 原snapshotQpc→consumeQpc | 复用既有source时间，适用于没有callback probe的输入，但原点不同。 |
| model/geometry spans | owner对该采样事件的真实调用前后 | per-point wall；等待/线程抢占包括在wall，frameCPU需单独解释。 |
| adoptedToPresentMs | 实际采纳→匹配成功PresentReturn | 后续几何、提交、合成与呈现软件等待；不是GPU时间。 |
| callbackToPresentMs | callbackEntryQpc→匹配成功PresentReturn | 仅对真实内容proof成立的保留采样；分类工具/device/phase。 |
| upToStored/FinalPresentMs | Up原点→CPUcommit/权威Stored软件回执 | 分开收尾和最终呈现，deferredUp/重连单独说明。 |

### 6. 必须区分的 outcome / 分母

- `notSampled`、`traceCapacityDropped`、`traceContentionDropped`、`callbackInvalid`、`stateBusy`、`decoderMissing/DecodeFailed`、`touchpadFiltered`、`publishRejected`、`writtenButRouteClosed`、`admissionRejected`、`navigation/suppressed`。
- `publishedOverwrittenBeforeConsume`（成功发布seq跳过）、`readContended`、`consumedNoGeometry`、`modelFailed`、`predictionOnly`、`geometryOutsideViewport`、`rasterFailed`、`compositeFailed/noPresent`、`PresentFailed`、`outputMismatch`、`scene/contentSuperseded`、`cancelled`、`stoppedPending`、`proofUnsupported`、`confirmedSoftwarePresent`。
- Move覆盖可能降低“可匹配成功样本”的表观延迟，必须同时报告received/published/consumed/confirmed各分母与采样/drop比例，不能仅对成功样本报P95/P99来说明所有点跟手。
- Down/Move/Up及Pen/HardPen/Highlighter/Eraser/Shape/Laser分群；RTS Pen/Touch/Mouse、实际有效工具（如倒转或右键橡皮）、DComp/ULW目标分群。shape用最终raw Up端点，eraser可能没有可见墨迹变化，Laser不进入普通L2，语义不同。
- 序号、generation、内容token/scene/output不能跨会话复用；诊断start/stop/recovery/page切换必须显式失效pending。所有数据丢失保留计数，不覆盖旧样本前缀，不热扩容。

### 7. 软件、GPU、像素与光学的证据边界

- `Draw3.RendererPrimitives.cpp:74`/`:79` Map失败返回-1；`:100` D3D Draw调用后 `:108` 返回0只证明CPU提交路径成功，不是GPU执行已完成。
- `Draw3.TransparentPresentation.cpp:1257` 根据target选择ULW或DComp；ULW `:534` CopySubresourceRegion、`:537` staging Map读回，再dirty copy / UpdateLayeredWindow；DComp `:1278` PresentSwapChain。两者Present wall的内容不同，不能用一个Present耗时推断统一GPU瓶颈。
- 即使ULW Map完成并有source alpha，仍不能证明桌面合成/遮挡后用户看见该点；DComp flip队列可能丢弃已提交帧。现有`Draw3.RuntimeMetrics.cpp:852`明确 `software Down-to-PresentReturn`，gpuMs和releaseVerdict不可用。
- RTT/RTS callback之前的触摸屏/笔采样、USB/驱动传输、Windows输入积压不可从本模块QPC恢复；显示扫描/像素响应/肉眼延迟不可由PresentReturn证明。只把这些列为measurement boundary，不为本次扩展光学或驱动采集。

### 8. 现有无窗口验证接缝（未运行）

- `InkeysHeadlessTests.exe --no-window`：入口 `InkeysHeadlessTests/animation_tests.cpp:1644`，`:1695` 调Contact测试；生命周期 `draw3_contact_tests.cpp:45`，Closing/Discard/reuse测试 `:880`；适合新增diag-off计数不增长、精确发布seq、覆盖、capacity/竞争、source/generation错误的测试。
- `InkeysHeadlessTests.exe --draw3-contact-benchmark`：`:1641`/`:1655`；`draw3_contact_tests.cpp:1289` 3warmup+11run，每run20000个真实PublishMove+TryReadSnapshot，`:1311` 验证每个样本成功，`:1339` 输出ns/run均值与noise。可作普通模式额外热路径回归证据，不等价实际RTS或绘制fps。
- `Inkeys.exe --draw3-parked-desktop-exit-test`：`Inkeys/IdtMain.cpp:1320` 早期入口，生产CPU函数 `Draw3.DrawingController.cpp:6489`，`:6934` 调真实Session probe、`:6935` 调内容proof probe（无HWND/无disk）。适合sidecar proof单测，不必新增构建系统。
- 现有proof fixture `Draw3.DrawingController.cpp:6303` U208覆盖消费未采纳/失败模型/真实采纳/栅格失败/旧frame；`:6380` U210覆盖实际dirty完整Stored投影及viewport变化；`:6401` U211覆盖admission/source QPC/错误几何seq/extent；`:5153` Session probe含失败Present、错误身份、容量/invalid/去重。
- `Inkeys.exe --draw3-geometry-benchmark`（IdtMain `:1344`）、`--draw3-ulw-copy-benchmark`（`:1350`）、renderer pixel test（`:1210`）分别测局部几何、copy、GPU像素，不证明真实输入callback→Present。
- `--draw3-host-metrics-smoke --output-root ...`（IdtMain `:1258`，HiddenWindowTest `:3681`）、`--draw3-hidden-test`（`:1360`）创建隐藏HWND，属于单独需要批准的GUI/窗口测试边界，本研究不执行。
- `inkStrokeModelerTestTests/runtime_benchmark.cpp:123` 会启动demo并用SendInput，不是本次无GUI验证入口，也不是Draw3产品现场证据。

### External references

全部为Microsoft官方Win32/SDK文档，2026-10-14读取；生产接口为RTSCom/rtscom.h、D3D11、DXGI 1.2 Present1，保持当前Win7-compatible产品边界。

- [Acquiring high-resolution time stamps](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)：QPC用于本机区间，频率在系统运行期间不变；平台相关成本，不自行RDTSC或设CPU affinity；跨线程±1 tick顺序不确定。诊断优先保存原始差值，测点数量有界。
- [IDXGISwapChain::Present](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present)：flip presentation queue可能discard已排队帧，Present可能等待message-pump线程。这支持把returned软件延迟与光学可见延迟分开（对具体Draw3行为仍是有限推断）。
- [IStylusPlugin::Packets](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-packets)：回调可包含一个或多个bundled packets。项目的最后包策略由上述生产代码直接确认，不能假设每个回调只有一个硬件点。

### Related specs

- `.trellis/workflow.md`：研究持久化、角色与阶段边界；未读取implement/check JSONL。
- `.trellis/spec/native-desktop/index.md`、`cpp-conventions.md`：minimal diff、保持编码换行、输入/渲染owner、沿现有并发协议。
- `.trellis/spec/native-desktop/input-and-ink.md`：唯一Draw3 RTS、Cursor有界队列、published与准入/像素区别、headless和隐藏窗口边界。
- `.trellis/spec/native-desktop/draw3-integration.md:427`：RuntimeMetrics有限软件证据，32MiB预分配、最多64pending、opaque key+generation、不得以lastConsumedSequence/旧snapshot伪证明、Down/Move/Up/软件/光学/GPU分开、Laser正式landing不支持。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md:31`：高频日志控制与同步I/O边界；source停止→owner停止→资源释放。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：复用现有solution/工具链；真正编译由主任务按用户当前明确的`InkeysRepo.sln / Debug|ARM64 / 原生MSBuild / PATH workaround`执行。

## Caveats / Not Found

- 本报告为静态研究，未编辑代码、编译、执行测试、启动GUI或进行Git操作；不能声称任何新接线已实现/通过。输出仅写当前任务research/。
- 当前公开Publish*只返回bool，没有精确publication observer或callback-entry传播；需要最小诊断专用接缝。QPC+tcid/cid或坐标单独匹配不满足精确并发身份合同。
- 现有diag-off虽然不做统计，已有runtime分支本身仍存在；新runtime开关若在正常模式增加读指针/分支，也不能数学上保证零CPU cycle。可承诺无新增统计/时钟/I/O/分配/原子争用，复用已有门禁并以microbenchmark观察噪声内影响；若严格要求机器指令完全相同，需要单独编译/启动选择专用producer模板等更大变更，不能擅自用“零开销”措辞掩盖。
- Shared stage成本是frame级父子重叠测量，不是每个RTS点的各阶段分解；当前schema2 `inputEventCapacity=0`（RuntimeMetrics `:870`）直接表明无逐事件记录。完整点阶段耗时需补采样span或明确unsupported。
- 现有Live proof使用整段realBounds相交，而非新点像素覆盖；strict raw-pixel/光学终点未找到。Laser正式landing、eraser无墨迹变化、导航/选择/suppressed必须保留不同状态。
- Cursor旧宏`DRAW3_RTS_DIAGNOSTICS`在产品未启用，不能依赖它采集全量输入；现有Cursor日志每秒稀疏Packets也不满足本次需求。
- `.trellis/spec`的部分构建事实与本次用户指令不同；本研究不处理构建，主任务应优先当前用户明确的solution+ARM64规则。
