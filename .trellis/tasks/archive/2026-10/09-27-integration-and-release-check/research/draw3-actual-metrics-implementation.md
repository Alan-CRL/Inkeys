# Draw3 U2 实际 producer：实施冻结与 P1 红测交接

日期：2026-10-01（P1历史记录2026-09-30保留）。Active task：`.trellis/tasks/09-27-integration-and-release-check`。唯一 writer：`draw3_actual_metrics_impl`。当前状态：**U2-P2 接线源码完成，PATCH_READY；新构建/运行/独立实码审查待 root**。下文 HARNESS_READY、红桩与P1冻结描述保留为历史记录，不代表当前代码。

## 授权与范围

- 最终合同 SHA-256：`4F6839B450AAE10E298F34B28F8B54C4D77CC8AF9988B5AF6B4E0F67E171545F`；增量独立报告给 U2/U3-H 与首批有限 U3-F `GREEN_DESIGN`。root 已明确确认：P1 先留真实 assembler / 精确失效红桩，root 完整 Debug/parked 红测之后才 `GREEN_IMPLEMENT`；后续 helper 和 normalRun/PresentFrame 接线再分批。
- 本 writer 只改 `Draw3.DrawingController.cppm/.cpp`、`Draw3.RuntimeMetrics.cppm/.cpp` 和本报告。保留其他 writer 的源码及现有 E03、M01–M16、U1 schema2 行为。root 独占构建、运行、GUI、Host/Main/工程/spec/共享账本。
- 最小缺口：目前正常 Controller 只给 legacy metricVisible/StageLanding，不能提供对应实际采纳、成功共享栅格、Stored history 与本次输出的 verified proof。本批冻结一份后续 Run 共用的 assembler，CPU probe 直接调用它，不复制另一份判定算法。
- P1 不接 Host，不新增 CLI，不改变模型、预测、采样、pacing、帧请求或产品 gate。现 `--draw3-parked-desktop-exit-test` 增加无 HWND、无磁盘的 CPU proof probe；既有 probe 原样执行。

## 写源码前冻结的 DTO / helper

以下均为 Controller `.cpp` 私有数值载荷；旧 key 只按值比较。持有这些载荷不赋予 ContactRecord/runtime 生存期，绝不通过旧 key 读 Down/Generation/ContactId、TryReadSnapshot、Recycle 或 Reset。

| DTO / 状态 | 冻结字段与责任 |
| --- | --- |
| `ContentMetricKey` | opaque record 值 + generation。实际存活 handle/Down 在 Register 接缝一次复制源；Session 保留原 Down QPC/tool/device/去重键。 |
| `ContentMetricAdoptionKind` | None / Model / RawDown / RawTerminal / Shape；模型 Update 成功且 Append/Extract 实际采纳才为 Model；失败后 lastModelSnapshot 或只消费 sequence 不推进采纳。 |
| `ContentMetricLiveNote` | key、canvas scene、Down admission、consumed/adopted sequence、adopted QPC/admission/kind、唯一 contentToken、geometry/rastered sequence、对应 frameSerial、非预测贡献 bounds、tool、存在位。Live 每帧重新 stage；Laser 不生成正式 landing。 |
| `ContentMetricStoredNote` | key、canvas scene、RenderItemId 两 uint32、strokeIndex、contentGeneration、afterState、terminal sequence/QPC/admission、唯一 contentToken、存在位。只从真实 CPU commit + 当前 Find(id) 复制；在 runtime/handle 回收前完成。 |
| `ContentMetricRasterSignature` | workspace/page/scene/raster anonymous identity、当前 history.Revision/rasterState、viewport x/y/scale、真实 width/height。输出编号不作为 L2 成功戳。 |
| `ContentMetricL2Stamp` / replay latch | valid + 完整 signature。局部写须匹配写前有效 stamp 才能连续推进；失败、清层和 partial replay 撤销；完整 replay 必须末尾 signature 与计划开始一致，不能复制最新 revision 认证旧计划。 |
| `ContentMetricOutputIdentity` | exists、真实 target/rawRevision、非零 metric generation。raw0 合法；同 tuple 保持编号，变化/恢复取下一 checked 编号；极大 raw 不做加法，编号耗尽停诊断。 |
| `ContentMetricCandidate` | 自有 key/proof/冻结 output；不含 runtime、Canvas/history 借用或 GPU/COM。一个真实 Present 的多 proof 使用同一立即 returnQpc；确认前后 Snapshot.confirmed 恰 +1 才释放对应 note。 |
| finite counters | 精确 Cancel/InitRejected/ReconnectSuperseded/ContentSuperseded/SceneSuperseded/Fatal/Stopped、proofOverflow/identityExhausted/consumedNotAdopted/outputMismatch/authoritativeWithheld/noPresent/rasterFailed/excludedLaser。重复失效不放大 unpresented；错一项 proof 不取消其他 pending。 |

私有 `ControllerContentMetrics` 固定容量：Live 64、Stored 64、冻结候选 128（Session pending 仍为已绿 64）、匿名 workspace/page/device 映射 64、当前 output 一项、权威 stamp/replay 一项。不能在热路径扩容；身份映射/计数/token 耗尽关闭相应诊断并保留缺失分母，产品继续原样运行。`RenderItemId` 用 `(uint64_t(generation) << 32) | index` 无损编码，配 scene 和 contentGeneration；不使用 hash 作为身份。

共用方法冻结为 `Register`、`NoteConsumed`、`Adopt`、`ObserveLiveRaster`、`CaptureStored`、`ObserveCanvas`、`ObserveOutput` / `InvalidateOutput`、`Signature`、`CompleteFullReplay`、`BeginLocalWrite` / `CompleteLocalWrite`、`BeginVisibleReplay` / `CompleteVisibleReplay`、`InvalidateRaster`、`Invalidate`、`FreezeCandidates`、`PresentReturned`、`RecordFrame`。GREEN 时这些方法必须被真实 Run / PresentFrame 调用；P1 helper 只是接口/红桩，测试的固定 true/false 是 CPU 合同输入，不叫真实 GPU/Presenter 故障注入。

Session 唯一小扩展：`bool InvalidateContact(ContactRecord* opaqueRecord, uint64_t generation) noexcept`。GREEN 合同为仅 Registered/Pending 首次转 Unpresented 返回 true；Pending 只释放其一个槽，不删除 key，不改 currentPresentSucceeded/frameSerial/其他 pending。P1 明确返回 false 红桩，不改既有 U1 方法。

## 共同 32 MiB 与后续 Host / phase 接口

- 上界为 `32 * 1024 * 1024` **共同 payload**。`Prepare(session, externalAuxiliaryBytes)` 首先核 nullptr（立即 null，无分配/额外钟/采样），再检查 `externalAuxiliaryBytes <= budget - sizeof(ControllerContentMetrics)` 和 `Snapshot.allocatedBytes <= budget - sizeof(ControllerContentMetrics) - externalAuxiliaryBytes`，满足才一次 nothrow 分配旁挂。实际 `sizeof` 由 root 编译后的 CPU probe 打印，源码 static_assert 旁挂不超过 64 KiB；不先越预算再回收。
- 本批 externalAuxiliaryBytes=0；后续 Host 必须先计算自有 phase/checkpoint/replay POD 与功能缓冲，再作为该参数提供。功能 BGRA 固定 `320*240*4=307200` 字节；timed benchmark 不安装此缓冲。Session 自有 unique_ptr 与 owner 生命周期仍在 Host 实施单元，Controller 仅借用。
- Session constructor 容量/schema2 不重写、不在 run 中 Reset。后续 finite `RuntimeMetricsPhaseBoundaries` 保留 ColdEnd/WarmEnd/MeasuredEnd 三界及 exists/ownerQpc/frameSerial/contactSeenOrdinal/Snapshot/input baseline/terminal counts。1..16 warm，17..216 measured；实际 owner 成功终态和 warm pending 全收尾才封界，joined/sealed 后才离线导出。P1 不假装已有该 DTO/WriteJson 三参重载。
- `RuntimeMetricsFrameSample` 现无新增 CPU/stage/Laser 数值字段。本批 RecordFrame 只复用现 DTO；后续有限字段按合同 §9/9.1 同一 writer 扩展并重跑受影响 U1。Move latency 与 Up final stability 仍 null/not collected，本批不拿 Down 时间替代。Laser landing excluded 不关闭真实 frame/Hold/Fade/particle/cost 的后续职责。

## CPU probe 期望（待实际执行）

新增 `RunDraw3ContentProofProductionProbe() noexcept`，由现 parked CLI 的 `RunParkedDesktopExitAutoSaveTest` 调用，打印 `[Draw3ContentProof]`。真实 Coordinator 注册/消费/Up/Recycle，真实 `CommitRuntimeStoredStrokeCpu` / InkCanvas / history append/find/Undo/Redo；QPC 使用 fixture 实际 clock，未来实际 Presenter return 仍须在生产调用旁即时取钟。

| 新回归 | 必须绿的结果 |
| --- | --- |
| U201 已回收 Stored，失败→同内容成功 | Down 源与 key 纯值保留；失败 pending1，后续当前权威帧确认恰1；旧 record 不可读/同槽新 generation 不污染原项。 |
| U202 page / scene / item generation / strokeIndex / contentGeneration | 错身份不能确认；成功 Undo 的隐藏项精确终结，Redo contentGeneration 增加不能复活旧 Down。 |
| U203 rasterState / failed write / partial replay | CPU afterState 推进不产生权威 stamp；另一笔局部成功不修复先前失效；完整同 signature replay 才可确认，跨帧计划变化拒绝。 |
| U204 多 contact 与精确取消 | A Stored+B Live / 多 Stored 一调用只记一次，same returnQpc 多 proof 各恰一次；Cancel A 不清 C Stored，也不撤销本帧成功资格。 |
| U205 reconnect | 旧 A failed Live 精确释放，新 B 独立注册，C Stored 保留；confirmed A、重复、stale、新同址代次 no-op；>64 交接不泄 pending 槽。CPU probe 只证共用失效接口，实际 initializeStroke 接管 seam 留 P2。 |
| U206 raw output | Primary/raw0→Selection/raw1→Primary/raw2 非零不同身份；重复 raw0、恢复同 tuple新编号、极大 raw / checked exhausted；旧 Stored 新输出重新 stage 成功恰一次。 |
| U207 no-call / call false / call true match / call true mismatch | 无调用不记 attempt；每个实际调用结果各记一次，错 output 只拒 landing且保留真实 success/result。after=0/terminal/noPresent/rasterFailed render attempt 进入 frame 分母。 |
| U208 Live adoption | 只 consumed、去抖、模型失败不等于 adopted；真实采纳 admission/非预测几何/成功共享层相符且本帧重新 stage 才确认；raw terminal 为独立 kind。 |
| U209 budget / defaultoff / finite capacity | nullptr Prepare 无旁挂；共同预算先检，超额拒分配；表/token耗尽可见，allocatedBytes 热采样前后不增长，产品 Coordinator 仍可准入/回收。 |

## 文件身份、检查与未验证

实施前四源 SHA-256：

- Controller.cppm：`8F22AD84971BE19EA8F1F3C3B45723821248543B46701BA8656F2FF463C0EE3F`。
- Controller.cpp：`5FBBEB09C5E120E537FAFEB33CA3E9E3955F71FF4D8EA87C1F72CD2403AEA4F5`（M16/E03 后）。
- RuntimeMetrics.cppm：`FA0043331A8E9D23C937D504C0A594500D0EB47CF9E659FE8ADECDB8B4DEEA4F`。
- RuntimeMetrics.cpp：`C65B27BAF4EBA744B516B5E4D17C30A73432D09FD6C91B6FE8B0B26E55700A3B`。

读源确认四文件 UTF-8 BOM + CRLF、bare LF=0；最终 P1 散列/diff/格式将在 HARNESS_READY 补充。已读取完整保存 hook、真实 AGENTS/implement.jsonl/prd/design/implement、相关 spec/index/guide、父最新 handoff/performance、U1 实现/冻结合同与独立增量 review。

Lint/Build/TypeCheck/Tests/EXE/GUI：本 writer 均未执行，root 负责完整 `InkeysRepo.sln Debug|ARM64` 与现 parked CLI 红测并留命令/退出码/输出，不能预填 PASS。后续 GREEN helper/真实 Run/PresentFrame/Host source 必须分别复验及独立实码 review。真 PresentReturn 不代表光学/GPU duration/真实 RTS/Win7/HC-H2；长期/Office/新进程可见恢复门仍保留。未 commit/push/archive/结束任务。

## HARNESS_READY：实际 P1 修改与冻结指纹

| 文件 | P1 SHA-256 | 本批最小变化 |
| --- | --- | --- |
| `Draw3.DrawingController.cppm` | `8BB0B41749CB6121B3C6257CAD0F60F4548E771D491813CA67FF3A708FAD9A7A` | 仅追加 probe 导出及中文职责注释，+2 行。 |
| `Draw3.DrawingController.cpp` | `A677D7A6A514701C0EA8ADA6B10B42F53779D393164C9F85EECE52FC0F69F930` | 新私有固定 DTO/helper 红接缝、新 CPU probe、现 parked 调用/合并退出码，净 +816 行；包含源锁存/预算准备，但没有生产 Run 接线。 |
| `Draw3.RuntimeMetrics.cppm` | `87E370C33F7A080F26C97AD628C2363D6614040323CB2700E33A73D2B37B0F55` | 精确失效 API/中文注释，+2 行。 |
| `Draw3.RuntimeMetrics.cpp` | `64CCBA5895C0D8B466BEF045CDF1DBF4A60219BA9E17CE96EDEEE004FE79BB53` | 精确失效返回 false 红桩，+6 行；已绿 U1 实现/报告方法不改。 |

- 四源最终均 UTF-8 BOM / CRLF / bare LF=0。静态格式/范围检查 exit0，`git diff --check -- <四源+本报告>` exit0；没有编译或运行。
- 做了可复核的字节范围检查：从当前文本仅移除上述新 helper/probe/API 与 parked 调用修改，重建 BOM+CRLF 原文，其 SHA-256 **逐文件精确等于本报告四项实施前指纹**。因此 E03、M01–M16、所有正常 Run/PresentFrame、模型/pacing/输入/画质/旧报告字节均保持该接手状态；Git 对 HEAD 的较大 diff 含之前已绿 U1/E03，不能将它们误认本批覆盖。
- 接缝尚红：`InvalidateContact` 返回 false；`ObserveOutput` 返回不存在；完整/局部/replay 权威戳完成方法返回 false；`FreezeCandidates` 返回空；`PresentReturned` 保留旧 RecordPresent/Unknown。每项新期望都依赖这些生产返回或 Snapshot；没有 unconditional assert(false)。`Register`/采纳数值锁存/CPU Stored 纯值复制/预算 Prepare 只为准备真实源，不形成 GPU 或成功 Present proof。
- 新 probe 不读写任何文件、不创建 HWND/renderer/device/thread、不改配置/注册表/Office，不设置产品测试 gate，不 Reset Coordinator。所有输入是各自新建的真实 Coordinator；CPU canvas/history 是本函数自有对象。fixture 用既有 CPU commit/finalizer/footprint/Find/Undo/Redo，明确固定栅格成功参数只验证 helper 合同。
- `Fixture::Prime` 仅为隔离 **Session 精确失效** 新接口，以真实已捕获 item/key 作为现 U1 Stage 的测试输入；它不实现采纳、权威戳、候选接受或 Present 确认算法。所有 U2 内容归属正/反例调用同一 `ControllerContentMetrics::FreezeCandidates` / 戳 / 输出 / PresentReturned；GREEN 后将由正常 Run/PresentFrame 使用这份实现。
- CPU 回归补充了真实 Clear/页返回的新 scene、Registered 尚无 proof 的 InitRejected、旧 Stored 换输出 restage，以及 >64 Touch 键交接；实际 initializeStroke reconnect 接管 seam、真实失败栅格/Presenter 调用、fatal/slot/loaded install 入口、全 noPresent/rasterFailed/idle 帧原因与 Laser 成本仍留后续生产接线验收，不能由数值 probe 先记 PASS。

root 下一步：冻结四源构建完整 Debug|ARM64，再运行现 `Inkeys.exe --draw3-parked-desktop-exit-test`。应保留旧 Desktop/FatalClosing/FatalActiveInk/LaserIgnoredTouch/InitRejection/Draw3Metrics PASS，新增 `[Draw3ContentProof]` 契约 FAIL，CLI 自然 exit1；**这是期望，尚无本次实际输出/退出码**。`payload: session/auxiliary/commonBudget` 是实际编译 sizeof/Session payload，收到日志后再补精确字节，不预报运行结果。等待 root 的红证据与 `GREEN_IMPLEMENT`；源码停止写入。

## root 真实红证据与本批授权

已完整读取 root 留存的 parked stdout/stderr/status、完整 Solution build status 和 log 收尾；本 agent 没有执行编译、EXE 或 GUI。

| root 文件（忽略目录 `TestResults/release-hardening/`） | 实际记录 |
| --- | --- |
| `draw3-u2-p1-red-debug-arm64-build.log/.status.txt` | 完整 `InkeysRepo.sln Debug|ARM64` exit0，0 Error/14 既有 warning，36.61s。warning 位于 thirdparty/Main；不为其扩展修补。 |
| `draw3-u2-p1-red-debug-arm64-parked.stdout.log/.stderr.log/.status.txt` | **exit1 / pid14764，自然结束**；原六标签 PASS，新 U201–U209 共 29 项具体 FAIL。fixture 本身确实完成真实 CPU commit/recycle，没有前提异常替代红测。 |
| 红 payload | Session `81912`、旁挂 `38352`、共同预算 `33554432` 字节。这里是红版本实际 sizeof，不能冒充新增 latch 后的绿色字节。 |

root 据此发出 `GREEN_IMPLEMENT U2-P1共用helper`，并明确 **本批 normalRun/PresentFrame 继续冻结**；先 helper PATCH_READY → root Build/parked → independent review，再另外许可生产接线。

## P1 helper 绿色实现与严格边界

- `RuntimeMetricsSession::InvalidateContact` 只通过 opaque record+generation 查现有去重表；Registered/Pending 首次转 Unpresented，Pending 只释放其对应唯一槽。Confirmed/Invalid/Legacy/Unpresented、未登记和旧代次均 false/no-op；不删除索引、不撤销 `currentPresentSucceeded`，不改其他 key/serial/预算。原 U1 各方法未改。
- Controller source note 只记录数值。`Adopt` 要已有 consumed sequence、有效 QPC、同 Down admission、合法 phase/kind、实际非预测几何；拒绝去抖/失败/无几何/旧 sequence。真实采纳给几何分配 checked token并撤本 contact 旧 raster 资格；`ObserveLiveRaster` 独立确认本帧/shared-layer success。`consumedNotAdopted` 按被 withheld 的 sequence 去重，**不是逐 Move 事件覆盖/精确 coalesced 数**。
- `CaptureStored` 复制当前 Find(exact id) 的 visible/strokeIndex/contentGeneration/afterState 与已消费 Up 的 sequence/QPC/admission，随后可回收输入；不持旧 runtime。已在 helper 确认 Down 的 Live 在真实 CPU Up 后只释放 note，不新建第二个 Down 样本；其 bool 表示 metrics 终态已处理，不能作为产品 CPU commit/保存成功的返回值。
- `ObserveCanvas` 在成功 CPU scene 切换/replace/load/Clear 的调用边界，精确终结旧 scene 自有 keys，再推进 checked scene。相同 scene/no-op 保留；返回同 GUID 的页也是新 scene。固定 64 映射不会热扩容；raw pipeline generation 倒退、scene/token/output 编号耗尽使 `identityAvailable=false`，精确结束未确认 keys，继续保留真实 frame/attempt 数值，产品正常路径不变。后续 owner/export 必须把这个 unavailable 状态计入 incomplete，不能只看 Session coverage。
- output POD 独立处理 exists 与 target/rawRevision；raw0 与 UINT64_MAX 都合法且不做加法；同 tuple 重用匿名编号，真实 tuple 变更或恢复撤映射后分配下一非零编号。耗尽不绕回，旧 Stored 可以当前 tuple/新编号重新 stage；输出不匹配只拒 landing。失败 API 的 observation 不认证 output mismatch；真实成功错输出时单记 `outputMismatch`。
- L2 完整 replay 只在当前完整 signature/成功值成立时建 stamp。局部写在 Begin 保存匹配写前有效 stamp并立即撤销旧资格，Complete 同 surface/history 单调且全成功才能连续推进；失败后无新匹配 Begin 的成功值不能修复。分帧 visible replay 保存计划开始 signature，incomplete 保留计划；当前 signature 变化直接拒绝，全部成功且末尾同计划才建 stamp。L2-only 写入和显式全 raster/shared-layer 失效分开，避免一笔 Stored 的 L2 操作误清同帧其他 Live；实际清 L0/L1、device/resize/viewport 等仍须 owner 调 `InvalidateRaster`。
- `FreezeCandidates` 使用真实 canvas/history 当前值校验：source scene、exact item generation、visible、strokeIndex、contentGeneration、对应 afterState、当前 viewport/尺寸/history/raster 与成功 L2 stamp。另一笔 append 不取消未变项；Undo→Redo、Clear/scene 换代不复活旧 Down。Stored 的当前可见投影先 double 偏移/裁剪，完全视口外拒绝；要本次实际 composite dirty 完整覆盖该项，缺证保持 pending/withheld。
- **尾 bool 现明确为 `fullComposite`，表示实际整视口合成**，还核 dirty 真覆盖整视口；不把 `PresentFull` 的 API 开关当全内容资格。Live 要本帧 adopted==consumed==geometry/rastered、同 admission、同当前 raster/scene、非预测贡献与本次 dirty 相交。未来 P2 调用必须传实际 composite 区域/结果，不能因 sampling 强制更多 replay/帧。Stored 投影不被当前 dirty 完整覆盖时首版保守 withheld，不能编造旧 backbuffer 的完整身份。
- 一次 `PresentReturned(called=true)` 的首个 Session 操作为真实 `RecordVerifiedPresent(wall, bool)`，不依赖候选/输出/内容是否有效。随后仅 current frame、冻结 output与当前 output/return observation一致、当前权威 Stored stamp仍匹配时逐唯一 proof 使用同一 returnQpc Commit；确认计数恰+1才释放 Stored/标 Live confirmed。false/mismatch/no proof保留真实分母；no-call 不造 API false。QPC 参数仍由后续真实 PresentFrame 返回点提供，本批没有接产品。
- 精确 Cancel/reconnect/InitRejected/scene/fatal/stop/overflow只释放其 key/note/candidate；确认/重复/stale不放大 unpresented，其他 Stored 与当前成功资格保留。候选数组、note、mapping均固定容量；Stage 的已终结/overflow 不继续占旁挂槽。
- Laser Register 计 intentional `excludedLaser`，assembler 从不发正式 Laser landing。终态 precise retire 后 Session unpresented 包含该 excluded 子集，旁挂不将其计为普通 Stopped/Cancelled 等失败原因；后续报告须明确这一子集，不能当普通 failure，也不能把零 landing 当零延迟。frame/result 仍记录；真实 Hold/Fade/bake/particles/分段 wall/CPU/资源 producer 尚未接。
- `RecordFrame` 保留真实终态 after=0/noPresent，冻结 bit15来源进入 rasterFailed；P1增补 fixed CPU cause counter断言。未增加 FrameSample 的线程 CPU/stage/Laser字段、phase DTO 或导出重载，Move/Up latency保持未收集。

## 绿色回归与 PATCH_READY 指纹（尚无绿动态结果）

原红 U201–U209 期望保留；未删除失败用例/放宽数值/关闭旧六套件。新增必要 helper 风险检查：匹配 stamp 的局部正例、失败后无 Begin 的 completion反例、同签名 incomplete→complete replay，以及 Laser正式排除仍保留 frame/result。四结果路径同时核 noPresent/rasterFailed/outputMismatch 原因数值。Fixture::Down 仅增加默认 Pen 的可选 tool 参数供 Laser反例；不改实际 Coordinator 语义。

| 文件 | helper PATCH_READY SHA-256 |
| --- | --- |
| Controller.cppm | `8BB0B41749CB6121B3C6257CAD0F60F4548E771D491813CA67FF3A708FAD9A7A` |
| Controller.cpp | `1687DA015DB78D816E06E6283A5EE14C41E7EEBAADF3469205CC94FC35CA32CC` |
| RuntimeMetrics.cppm | `87E370C33F7A080F26C97AD628C2363D6614040323CB2700E33A73D2B37B0F55` |
| RuntimeMetrics.cpp | `FDBE988ED85F94F51DDE0ACCB5B9358AB7C01AA8C3106F4FFDD670A482E004E0` |

- 四源 UTF-8 BOM+CRLF、bare LF=0；字节范围静态检查 exit0：仅移除整个 U2 新 helper/probe/API/parked调用后，四源仍精确重建本报告实施前 SHA。**normalRun/PresentFrame、M01–M16/E03与旧报告代码仍字节不变**。最终 `git diff --check -- <四源+本报告>` exit0；最后仅在 U2 helper中文注释明确合同bit15，当前SHA为表内值。
- Controller较红净+353行，包含 helper/latch 和新增必要回归；RuntimeMetrics较红净+17行。private latch、confirmed/withheld-sequence、frozen signature/output全计入新版 sizeof，Prepare共同预算仍在分配前复核；root绿色 payload打印后才填具体字节。
- 本 agent Lint/Build/TypeCheck/Tests/EXE/GUI仍未运行，只执行静态格式/范围/身份检查；root 红通过的 Build不能当本绿色编译/测试 PASS。等待 root 完整Debug/parked/受影响门与 independent actual-code review。源码冻结，不 commit/push/archive，不更新 spec/共享账本。

后续必须实接/验收：initializeStroke三拒收与真实 reconnect seam、实际 Append/Extract采纳、共享 L0/L1成功、连续 L2 write/full/tile replay、exact scene/slot/load/clear/Undo/Redo边界、全部真实 render attempts/早continue、Present返回后即时QPC、Host新Session/真join/Seal后导出。功能 metrics-off checkpoint须复用小的 stamp/cutoff值逻辑，**不得为 off 功能创建 dummy Session 或整旁挂表**；目前类仍是 opt-in Session helper，不能冒称该功能已可用。phase16+200、Checkpoint/source回执、Laser成本/线程CPU/资源、逐Move/Up与安静窗口、Release/三架构/Win7/真RTS/光学/HC-H2/长期门均未关闭。

## U2-P2 授权、真实 seam mapping 与实施顺序（写源前冻结）

root 已授权 P2；已读第三完整 native Debug Solution `c3a-u2-b2-green-f065-debug-arm64-build.status.txt` exit0、`draw3-u2-p1-green-debug-arm64-parked` raw/status **exit0 pid35248**，旧六组+ContentProof PASS，实际 payload `81912+39072/33554432`。新独立 `draw3-content-proof-p1-code-review.md` STATIC_GREEN，四源 exact恢复；其 pending动态门由上述 root 证据补充，不将 P1 数值通过提升为真呈现。最终合同仍为 `4F6839B4…1545F`。父 handoff 中 P2未授权的历史条目已由本次直接授权覆盖；C3/PPT/AutoSave/UI/Main 其它 writer 的源码不触碰。

| 真实 seam（以符号为准） | 最小接线 / 资格 |
| --- | --- |
| Controller ctor / startup ClearCanvas/PresentFullCanvas | metrics非null才一次预备固定 State，预算包括新 frame/latch POD；默认null不分配/新钟。外部实际 Present开独立有效serial、空候选cold frame；Run边界不重复Begin。 |
| initializeStroke，tool确定后、acquire/Reset/Down Update之前 | 从仍活的实际handle复制 Down/tool/device/QPC，一次Register；三失败先精确InitRejected，再保留原E03 Discard/状态清理。 |
| reconnect成功 Update+Append/Extract、旧handle Recycle前 | 先精确终结旧A，再接B并用新Down登记/NoteConsumed/实际采纳；失败接管不终结A；不把合并几何的新序号倒填A。 |
| Down Update/Append、consumeLatestSnapshot、completeModelUp、stationary update | 只成功并实际Append/Extract后Adopt；RawDown、raw终态fallback、Shape单列kind；消费/去抖/失败只NoteConsumed，不借lastModelSnapshot认证。 |
| 正常结束 CommitRuntimeStoredStrokeCpu → DrawStoredStroke → L2 Apply | 写前保存完整signature/有效continuity；CPU成功先复制纯Stored note，失败也保留；真正Draw+resolve成功后才能推进L2 stamp，不能把已推进rasterState认证为成功。 |
| restorePageContent / resetGpuForPageSwitch / restoreAfterDocumentSlotSwitch | GPU清层先撤资格，成功CPU scene事务后新scene；完整可见restore才建stamp，Empty须已有整层清理事实。 |
| visible Tile replay | 计划创建时锁开始signature，partial保留；任何计划内容/viewport/尺寸变化拒绝；全部必要可见tile实际成功、且末尾同signature才建stamp，不改变原预算或请求更多帧。 |
| Undo/Redo / Clear / loaded interval / materialize topology / page/slot switch / viewport | 成功CPU visibility或scene提交后精确失效变项；写前旧stamp连续才推进；回滚/部分恢复保守withheld。viewport/shared clear/resize/recovery撤Live与L2；输出恢复同raw tuple先撤mapping。 |
| 实际L1/L0共享提交、结束项后RebuildActiveLayers | 全相关共享层成功后，用真正非预测实点/Shape实端点 bounds锁本帧Live raster；metricVisible/prediction不充当证明。 |
| composite/laser/光标完成 → PresentFrame | 最终实际composite区域冻结多proof；Present实际返回后立即QPC，在observer/ready前恰一次Record结果和same clock逐proof确认。 |
| Run早continue/终态/无Present/idle/fatal/stop | 实际render attempt独立RecordFrame（after0也记）；纯idle/Hold wait/maintenance loop不造零耗时render；故障/退出精确终结自有key，不读旧record、不Reset、不提前Host Seal。 |

顺序：①补P1审查负例与必要 source约束；②一次State/Present边界；③ ingress/采纳/精确终态；④完整/局部/replay authority与scene；⑤共享栅格+composite候选+全部attempt帧；⑥编码/范围/hash/实际diff，交root新Build/parked与独立review。本 agent不Build/run/GUI。

P1 reviewer补强只调用同一生产helper：partial dirty与fullComposite冲突；partial不足/足够覆盖Stored；视口外/viewport/extent变化；wrong admission（包括0）/sourceQPC早于Down/旧geometry sequence；Registered无proof的重复精确失效。采用真实 Coordinator/Canvas/history与fixed CPU成功参数，只证helper合同，不叫GPU故障注入。normal生产GPU bool/signature将逐实际返回接线，真实Host运行和门禁仍由root/U3承担。

P2不添加Host/fixture/Main接口、Move/Up事件数组、线程CPU/stage成本或功能checkpoint。Laser正式landing持续excluded，真实帧与原因保留；其生命周期/成本字段与U3数据仍单列未收集。外部auxiliary预算传递/owner封口与最终incomplete导出在U3接口交接时完成；本批预算至少完整包含Session+Controller State自有payload。

## U2-P2 实际改动 / PATCH_READY（未运行）

- `DrawingController.cppm/.cpp` 新增私有 incomplete State与出线 destructor；只metrics非null时一次nothrow准备，用共用FitsBudget核 `Session.allocatedBytes+sizeof(State)<=32MiB` 后分配。State包含固定表、L2/visible/local/frozen signature与frame/lifetime POD；null立即返回，不QueryClock、不分配。准备失败仅置private metricsUnavailable并断开诊断指针，不改变产品启动/输入；U3需把它整合到unavailable导出，当前没有Host接口。
- 已接实际 `initializeStroke` 工具/倒转/右键解析后的 Down注册；pool/Reset/DownUpdate三失败先精确InitRejected，再沿原E03 Discard；successful reconnect Append后、旧handle Recycle之前精确结束A，B保持自己的Down源。没有改Down/Move发布、采样、模型参数、prediction、pacing、工具/画质或旧路由回收语义。
- 模型实际成功后比较真实point count/尾点或shape modeled endpoint，实际Append/Extract改变后才Adopt；去抖和失败只消费。Down无模型点的真实inputStartPoint独立RawDown，terminal失败的真实fallback独立RawTerminal；Shape最终强制raw端点单列kind。stationary Update也只在实际成功且非预测几何变化、当前实际raw输入位置与所用anchor一致时采用lastInputSnapshot，绝不把failed lastModelSnapshot本身当成功证明。
- 正常CPU commit前复制before signature，真正CPU成功后锁L2连续写，回收前复制Stored id/index/contentGeneration/afterState/Up源；实际DrawStoredStroke和L2 Apply的submitted结果共同完成stamp。CPU afterState照旧推进，false不会授予authority。Undo/Redo成功visibility后调用同一PruneStored，失败rollback不认证；Clear/页/slot/current loaded恢复/拓扑安装推进scene。原保存/加载/observer/窗口/GPU顺序维持，未触其它writer文件。
- shared texture清理、viewport变化、resize/recovery显式撤Live/L2，actual recovery还撤output mapping。Known whole L2 clear与完整可见restore result共同建Full stamp（Empty也需清层事实）；visible replay开始锁实际signature，全部计划成功且无内容/viewport/extent漂移才认证，不增加重放预算/请求帧。跨帧发生CPU变更无法证明覆盖时保守withheld。
- 普通/橡皮实点bounds或确认存在的Down fallback与实际shared成功锁本帧Live surface；Shape预测边不必包含真实边，因此Live Shape只在实际primitive端点与已采纳端点一致时认证。失败帧不锁，非预测几何与当前实际composite相交才stage。最终laser/粒子/光标步骤结束后，在实际composite范围冻结多proof，不借metricVisible/预测/PresentFull。
- `PresentFrame` 每真实API返回后先取returnQPC，然后在observer/ready/帧末之前恰一次PresentReturned（success/failure均记，空候选/输出错不抹分母）；success observation target/rawRevision与当前/冻结匿名编号匹配才确认，所有proof同钟。原正常无metrics不加新Clock；实际原wall计算/observer/fallback请求顺序维持。原Run legacy StageLanding/CommitStagedLandings/RecordPresent已替换，U1专用legacy probe保留。
- Run帧State/RAII在earlycontinue/exception/stop结算实际attempt，after0/terminal/noPresent/rasterFailed均Record；normal end在原pacing之前记录inclusive wall，纯idle/Hold/maintenance不造零render，等待前封帧防止把睡眠计wall。实际Laser active/hold/fade/expiry/particle/bake原因保留、formal landing继续excluded；成本/资源/完整生命周期数值仍未收集。退出/fatal只按自有opaque key精确终结，旧输入对象不读、不Reset，Host join/Seal仍待U3。
- RuntimeMetrics基本原因位统一为 `RuntimeMetricsFrameReason` 固定21位；原基本FrameSample不扩展CPU/stage/事件字段。Session serial耗尽显式停在无效0/invalid，不能绕回；该极限只做静态审查，未运行2^64次伪造测试。

root确认的补强语义：Coordinator admissionRevision初始0合法，现用真正blocked→unblocked到2后wrong0拒绝，不修改准入；sourceQPC必须不早于同key实际Down。新增U210–U212全部使用同生产helper：partial fullComposite冲突、partial不足/完整Stored投影、viewport外与旧extent、actual admission2对0、sourceQPC早于Down、错误geometry sequence、Controller State共预算/null。CPU bool只证判定，不宣称GPU/Presenter故障。新runtimeState sizeof由root下次真实CLI打印，不预填。

| 本次四源 | PATCH_READY SHA-256 |
| --- | --- |
| Controller.cppm | `B119EE5B50F9649DD2C29A69F4045F4EE4C26CA39087893E3EB7AF7BC70670FA` |
| Controller.cpp | `9747058137A1CED95717FA3710DA5E45DE059532DF6E56317FB6D5517F1E909C` |
| RuntimeMetrics.cppm | `1E935AC95020E0B01987D02BC95D762ABD17850832DDDBD63A41FD9004F7D37F` |
| RuntimeMetrics.cpp | `C4A0DB015CF6F66EA1BF7DBBE904A8AA902E1A5844CA62B288184EA761456932` |

最终静态检查：四源UTF-8 BOM+CRLF/bareLF0、括号balance0、scoped `git diff --check` exit0。类型/链接/应用运行并未验证；本 agent未Build、EXE、GUI或benchmark，没commit/push/归档/spec/共享账本修改。root须待全部writer冻结后新完整Debug、parked（旧六+所有新例）、适用Headless/PptCOM与独立实际diff审核；旧P1 GREEN不能替P2。Default null路径/observer与input质量也需独立核实际diff。

剩余明确门：Host仍未传metrics，故目前没有本次真实Host/GPU/软件PresentReturn数据；actual断触/回收失败/CPU加载/resize/device-loss交错需要真实fixture。common external辅助预算传递、unavailable/private counters导出、Host真join后Seal、phase16+200、functional-off共享小stamp/checkpoint/source receipt由U3明确交接。Move→Present、Up稳定+安静窗口、线程CPU/stage、Laser软件成本/资源/长期、Release其它架构、真RTS/Win7/光学/HC-H2均未完成。部分composite没有覆盖整项时保持pending，不能为漂亮count强制更多帧或认旧backbuffer。未开放产品gate照旧关闭。

## P2 checker增量：实际withheld计数接帧bit19

root/独立checker确认normal Freeze后漏接 `AuthoritativeWithheld` 原因位；恢复本 seam 与自报告窄写权，其余三源保持冻结。已在实际 `FreezeCandidates` 前读取 `counters.authoritativeWithheld`，返回后只在本次 counter 实际增加时 `Mark(RuntimeMetricsFrameReason::AuthoritativeWithheld)`。不从 `!l2.valid` 推断，不新增帧/图形/等待/Clock，原frame其它原因位不动；全部仍在既有metrics非null guard内，默认null费用零。

Controller.cpp当前 SHA-256：`ECF133C89F6118B116DE6DADB8ADF0F35D780C03D5513F156255058E362AA5E5`（上表9747是该修补之前的P2快照）。仅+4行含中文注释；内存内移除这4行后，BOM+CRLF原文准确还原 `97470581…E909C`，证明seam之外无变。BOM/CRLF/bareLF0、scoped diffcheck exit0。现U203/U210同生产helper已覆盖无L2/投影不足的实际withheld路径；未增加复制计数判定的镜像测试。本 agent仍未编译/运行，交checker增量实码与root新Build/parked核验，不把静态修补预填动态PASS。PATCH_READY，源码重新冻结。

## P2 checker增量：Undo/Redo 实际尝试与失败帧（PATCH_READY，未运行）

2026-10-01，root 恢复 **Controller.cpp 与本报告** 窄写权修 P2-F02，其余三源冻结。成功尾部才置 renderAttempt 的旧接线可能让 DrawStoredStroke/restore/resolve 失败后带空 dirty 返回 idle，实际 raster/noPresent/bit15 分母丢失。现在 State 共用 `BeginRasterAttempt` 保存此前尝试位并置当前位，`RasterReturned` 只对有栅格工作而实际失败的返回置 RasterFailed；明确无栅格工作时恢复此前位。既有失败位、其他原因位、真实 Present bool 不清零，多次恢复/rollback 仍只由同帧 Finish/RAII 结算一次。

| 实际生产接缝（当前 Controller.cpp 行） | 最小计量增量 |
| --- | --- |
| State 3184–3195 | 两个共用帧元数据 helper；没有新字段、allocator、Clock、帧请求或产品 gate。 |
| Undo hot 10114–10119 | 调用前保存尝试位；据当前 hot restored/dirty 事实保留实际 copy 或退回此前位，不把 miss 当 RasterFailed。 |
| Undo cold 10174–10179 / restoreOriginalTiles 10154–10158 | 真 RestoreComposition 调用前置尝试；据 path/dirty 区分 Empty/前置拒绝与真实 mapped tile 失败。原冷恢复、visibility 失败后的 rollback 均走该实码。 |
| Redo restoreHiddenTiles 10296–10300 | 该既有 lambda 被 base 与 rollback 共同调用，两者统一记录实际恢复；原请求参数/返回结果原样返回。 |
| Redo clear/draw 10324–10333 | 在既有 scratch L1/L0 clear 前置尝试，真正 DrawStoredStroke.succeeded=false 置 bit15，保留原清层、dirty、log 和 return false。 |
| Redo resolve 10363–10366 | 仍只在原非空 redoDirty 分支调用 ApplyOperatorLayers，实际 false 置 bit15；原 rollback、恢复请求、visibility 提交和返回顺序不改。 |

**缓存/空操作分类来自当前 GPU 源，而不是对 false 猜测：**

- 只读 `Draw3.InkHistoryGpu.cpp::RestorePreimage` 1279–1326：false 仅在1285（impl/renderer/currentState前置）或1294（没有匹配 committed hot entry）返回，均早于1299–1318的 CopySubresourceRegion。当前 false 可以证实未提交像素 copy；命中时 dirty 每次 copy 后累加。该 copy 是 void，没有失败回执，**不能证明硬件执行成功或细分 device failure**；新计量只保留已提交尝试，不伪造 hot GPU失败观测。
- `RestoreComposition` 1362–1367 前置拒绝、1378–1383 Empty、1389–1394 excluded item拒绝，均未尝试映射 tile 恢复、dirty 空。1494–1503 的实际 clear/replay失败在返回 Failed 前将 tileDirty 合入结果，成功 tile 在1504–1505也合入 dirty；所以此生产接缝的非空 dirty 是当前 mapped raster工作/尝试的可观测边界。Empty 的 FinishHistoryOperation 仍有资源绑定恢复；此处不造像素栅格帧，**不声称整项CPU/绑定成本为零**。分段 CPU/成本仍未采集。
- `Draw3.StrokeGeometry.cpp::DrawStoredStroke` 620起与 `Draw3.Renderer.cpp::ApplyOperatorLayers` 65起的真实结果直接使用；没有复制 renderer 判定、读取 GPU query 或新接口。本次 Redo 的 draw前已存在真实 scratch clear，draw早退也保留该帧尝试；空 resolve分支没有新增调用。
- 无 canvas、空history、history mismatch、rasterState拒绝均仍在新增GPU尝试接缝前返回；纯 CPU UndoLastVisible/RedoLastUndone 失败不标 RasterFailed。若此前已经有真实栅格或 rollback失败，该帧的已知尝试/失败仍保留。未知异常不回填成功或归零；由原 frame RAII 保留当时已经置位的尝试。

`RunDraw3ContentProofProductionProbe` 新 U213 七项直接调用 **同一 State BeginRasterAttempt/RasterReturned/Finish 与同一 Session/PresentReturned**：Command-only无帧；miss/Empty无帧；栅格false→1 frame/1 noPresent/1 rasterFailed/bit15；成功栅格后的CPU visibility拒绝→1 frame/1 noPresent/无bit15；成功后rollback失败→仍1 frame；失败后的Empty rollback保留该失败帧；先前失败后true Present→实际输入结果的attempt/success各1，noPresent0、无PresentFailed。各项同时核 Command 位不丢、framesInvalid0、Finish两次不重计。探针只输入固定 CPU 合同结果，**没有实际注入 GPU故障、运行 Undo/Redo 或调用真正 Presenter**；normalRun seam仍需独立实码和真实Host验证。原 U1/M01–M16、E03、旧6标签、U201–U212所有断言保持。

当前 Controller.cpp SHA-256：`0CEF8C50C6D8C71BBF2DA1281F26337757B4C2729B78307A825C742521661876`。净+83行；在内存内移除本批 helper/七项探针/metrics接缝并还原两个原 return表达式后，BOM+CRLF原文精确重建 **ECF133C89F6118B116DE6DADB8ADF0F35D780C03D5513F156255058E362AA5E5**。因此该基线的 bit19修补、原GPU/rollback/命令/pacing/输入/返回语义、旧断言与其它源内容未被覆盖；Git对HEAD的大diff仍包含先前已授权P1/P2实现，不能认作本次窄修。

另三源SHA仍为上表 `B119EE5B…70FA`、`1E935AC9…D37F`、`C4A0DB01…6932`。Controller保持 UTF-8 BOM / CRLF / bareLF0，本报告UTF-8无BOM/LF；scoped `git diff --check` exit0。默认metrics-null的新增分支不调用helper、不分配、不取新钟、不改变采样/GPU/帧请求；State大小与共同32MiB预算不变。未Build、EXE、GUI、benchmark、commit/push或修改其它writer文件，**没有本次动态PASS**。root 待所有source冻结后完整Debug/parked与独立增量审核；真实GPU故障/Host on-off、U3所有权/导出及前述Move/Up/Laser成本门仍待后续。源码冻结。
