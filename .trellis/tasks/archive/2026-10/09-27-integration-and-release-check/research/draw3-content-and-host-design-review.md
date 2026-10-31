# E04 Draw3 U2 内容 producer 与 U3 Host / hidden runner 独立设计复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。按 root 分工只读实际 AGENTS、完整保存 hook、check.jsonl 引用、PRD/design/implement、父 handoff/performance、Draw3/input 规范、U1 设计和实际代码审查，再核对当前 Controller/History/Renderer/Presenter/Host/Product/HiddenWindowTest 源码。仅写本报告，没有修改产品、工程、spec、账本或 Git，没有运行构建、EXE、GUI 或 benchmark。

最新增量审查合同：`draw3-content-and-host-contract.md`，SHA-256 `4F6839B450AAE10E298F34B28F8B54C4D77CC8AF9988B5AF6B4E0F67E171545F`。前版 `1D214B07DD065D9D3BCD10C39099896103C284F38EDB4E5F4A1E7DF217685FF5` 的 R1–R5 已由作者修订关闭；下方旧发现保留为验收依据。以下行号以符号为准，Host 的 C2 增量已使 join/释放位置移动，本次只核 metrics 依赖的寿命边界，不复审或修改 C2。

## 分单元结论

| 单元 | 结论 | 本结论的范围 |
| --- | --- | --- |
| 已绿 U1 Session | 保留既有 GREEN | 本次核对 module/实现与原绿色身份相同，Controller 为 M16 后版本；没有重新运行其测试。无需 ordinal 框架或 RegisterActualDown。精确 InvalidateContact 是必要的小扩展，须由 root 交给唯一 Session writer。 |
| U2 实际 Controller producer | **GREEN_DESIGN** | 修订已关闭 R1–R3；实际 Present 计数、reconnect 精确终结、非零 output 编码和完整 replay signature 可进入最小实施。仍未产生真实 producer 或运行 PASS。 |
| U2 Laser 首批范围 | **GREEN_DESIGN：正式 landing / Up 分布 excluded** | 当前没有可直接复用的可靠逐 contact 栅格内容版本。必须继续记录 Laser 实际帧、提交/失败、Hold/Fade/粒子/烘干生命周期与成本，不能把整个工具跳过。 |
| U3-H Host 所有权、默认关闭与封口 | **GREEN_DESIGN** | 每 run 新 Session、Controller 借用、GPU owner 取数、真实 join 后封口/导出可复用现有停止链；不等于接线或运行已通过。 |
| U3-F fixed trajectory / hidden runner | **GREEN_DESIGN（首批有限范围）** | 修订已关闭 R4–R5：core-linear-8 同 Session 16 warm/200 measured；Shape 的 fixed-source 功能 replay 与真 owner QPC 性能 run 分开，GPU 仍活的 owner checkpoint 与 Committed→fresh Load 有明确入口。其它工具 exact pixels、逐 Move/Up、复杂轨迹/多接触和长期门均未通过；新接口/CLI 尚未实现。 |

## Findings (fixed)

无产品修补。本 reviewer 按分工只更新本报告；作者在修订合同中关闭 R1–R5，核对情况如下。U1 既有修补不冒称本次修复，U2/U3 未实施项不计 PASS。

## 2026-09-30 R1–R5 修订增量核对

| 前版发现 | 修订及实际调用点核对 | 结论 |
| --- | --- | --- |
| R1 实际 Present 分母 | §5.2 在实际调用前 stage，§5.4 每调用一次无条件 RecordVerifiedPresent，错 output 只 withheld；§5.5 无调用才 noPresent，四种 assembler 反例固定。§5.6 为 startup/外部空候选开有效 serial，不重开 Run 边界。与现 PresentFrame 5003、RecordVerifiedPresent 523/RecordRenderFrame 533 的实际前提相符。 | 设计缺口关闭；保留真实 call false/true/mismatch 与 no-call 的实现验收。 |
| R2 reconnect / 精确失效 | §3 冻结 bool InvalidateContact：仅 Registered/Pending 首次转 Unpresented 返回 true，保留 key，不动 currentPresentSucceeded 或其它 pending。6289 Recycle 前终结旧 A，原成功接管 B 后独立登记；失败续接不改变 A，已确认 A 幂等。>64 交接及 Stored C 反例已固定。 | 设计缺口关闭；无需 ancestry/ordinal 框架，不改变物理 ContactInput 回收。 |
| R3 output raw0 | §4 冻结 `{exists,target,rawRevision,metricOutputGeneration}` 当前 POD；第一次合法 raw0 编号1，tuple 改变/恢复重新编号，相同 tuple 保留，checked 耗尽停诊断。证明同时核存在位、真实 raw tuple 与匿名编号。Presenter Initialize 793–794 确实合法返回 raw0，U1 ValidProof 仍要求非零。 | 设计缺口关闭；不修改 Presenter 初始状态，也不对 rawRevision 做可能溢出的加法。 |
| R4 冷/预热/稳态 | §8.1 三个 owner 边界携带 QPC/serial/contactSeen ordinal、计数快照与 input baseline；同一 Session 连续16+200，ordinal1只另标首次，1..16/17..216 归群，真实终态/Stored/成功最终帧后才封界。WarmEnd 失败不开始 measured；缺界/跨界/drop 不 Reset、不补漂亮200。离线 WriteJson 有 finite phase POD 重载，allRun 与 phaseSummaries 明确区分。 | 设计缺口关闭；实际 warm/steady200 的 raw、invalid/drop/失败分类需测试。 |
| R5 owner checkpoint / UInk | §8.3 独立功能 gate 与 fixed-source十包 cursor；request/receipt 只持自有数值，host buffer 在 Start 前预分配，真实 terminal Stored/raster/composite/Present 成功后由绘制 owner 一次读 L2，timed benchmark gate 为 false。现 Host 1235/1237/1238 在 join 前已释放 drawing/renderer/graphics，改在这些位置前取 CPU bytes 才成立；现 WARP probe 2526–2543 的 RowPitch copy 模式可复用。随后真实 Clear 捕获/worker Committed，再 fresh Desktop service SubmitLoad/Loaded；匹配 fileGuid/请求、不偷 completion、不继承 EXE 邻目录，不拿孤儿/文件存在作结果。 | 设计缺口关闭；只有首批四 Shape exact BGRA，模型工具按已冻语义限界，未冻允差仍 not covered。 |

Shape 的 exact 条件有实际依据：DrawingController.cpp 6512 建立 raw start，6725/6908 在终态强制 raw Up endpoint；真正 Draw3.StrokeGeometry.cpp::FinalizeStoredShape 515 保存两点/width/style，最终 L2 不含 prediction/光标。单笔、相同输入相对时钟/样式/320×240 viewport/实际 backend 与 device 下可比较全部307200字节，不能用 hash 或该结果替其它模型工具证明逐位等价。固定 source replay 保留真实 frame clock，故修订诚实地没有给 Pen/HardPen 等作 exact geometry 保证。

§4 的共同32MiB已明确先计算 auxiliary bytes，再核 Session allocatedBytes 的余量，足够后才分配固定表/功能像素缓冲；超额/准备失败关闭诊断、产品继续，benchmark不安装像素缓冲。defaultoff与两个功能 gate 均默认空/false，普通产品无 Session/旁挂时钟；explicit metrics-off 功能格子仅启用共用 stamp/cutoff/test-frame，不将它冒称普通 defaultoff 的零开销测量。

§9.1 固定有限 FrameReason 及真实 LaserTrailPhase/CoverageMode 符号、incremental/bake/resolve/particle 批次、Hold idle/Fade/expiry/recovery 和资源事件；§9.2 明确保留真正 Move→对应几何/Present 与 Up→final+安静窗口的尚未实施任务。excluded 只限 Laser 正式 landing/Up；core/Shape 不能替完整性能、光学、长期或恢复矩阵。U2 的 adopted/L2/history/contentGeneration/场景失效、每 proof same returnQpc、Host 真 join 与所有功能 gate 仍须按下方矩阵验收。

## Findings (not fixed)

没有残留的 **R1–R5 设计阻断**。以下是允许实施时必须落实的局部接口与证据边界，尚无源码/运行 PASS；没有为它们要求新框架或产品审批：

- **P2，有限 DTO 与 writer 接口**：PhaseBoundaries、checkpoint observation/receipt、persistence receipt 和 FrameReason 的声明/字段布局、默认值、存在位、失败状态由 Session/U2/U3 唯一 writer 串行交接。Host.h 的数值 receipt 不引入 Renderer/COM getter 或私有 module owner。Session WriteJson 重载必须标 allRun/phase 与缺界 incomplete，无法按 serial 定位的旧 interval 数组不硬切为稳态分布。
- **P2，每包 source 回执**：core 的2160包实际 owner sourceQpc 不能从 bool SendMessage 返回、runner tick 或 latest-only snapshot倒算。U3可用 Host 自有固定数值 receipt 表在消息 owner 记录，停止/producer排空后读取，纳入 auxiliary 预算；Session仍只有绘制线程写。没有这个实接字段就标 not collected，不得给每包来源已齐的 coverage PASS，也不因此改 mailbox/pacing 或建立一般事件框架。
- **P2，功能 request 与保存身份**：checkpoint请求不应借 metrics Session 的存在来认证；off/on共用功能gate与cutoff，request绑定当前run、终态、visible count、完整stamp、真实output。在轨迹/最后Up之前预约，使既有terminal成功帧响应；迟到请求未到cutoff记失败，不为它追加Present。staging需在真实绘制GPU owner完成设备创建后准备，CPU自有缓冲 Start前预分配。保存 receipt 在 ObserveDesktopAutoSave 的 Submit 前锁存实际 snapshot fileGuid，在 PumpDesktopCompletions 观察同一 Save/Committed后仍把原completion交产品；fresh service从相同私有root Load，不能用 runner 自造 identity或抢走产品结果。该薄接线需最终实码审查。
- **P2，失败/尾样本门**：16/200边界和报告在失败、drop、未呈现、迟到source/输出、Run异常、Start/thread创建失败与未join时仍须保持分母与不可封口状态；renderer owner提前停止或readback失败不能补 checkpoint成功。新CLI参数/私有root/默认gate与首次运行实码安全门尚未验证。
- **P2，范围未完成**：新U2/U3、thread CPU/stage wall、Laser软件成本/生命周期、逐Move/Up、其它source/复杂轨迹/多接触/长期资源均未实施或未测。B06/C2的父构建消息不替这些矩阵；本复核不改其源码/记录，也不认定完整性能/恢复门已闭。

## 真实路径核对

Draw3 下短文件名均指 `Inkeys/Inkeys/Drawing/Draw3/`。

| 实际 producer / 消费者 | 核对结果及实施边界 |
| --- | --- |
| `DrawingController.cpp::initializeStroke` 5871、6041、6378、6472、6528 | 实际 handle/Down 仍活时可锁存键、admission、设备、QPC；effective tool 在倒转笔/右键覆盖后确定。应在 acquire/Reset/Down Update 三拒收前一次登记，保留 E03 的精确 Discard。导航/禁第二 Touch 等非墨迹 disposition 另计；不能在每帧重试 dropped 的未保留 key。 |
| `consumeLatestSnapshot` 6738–6890 / `completeModelUp` 6701–6732 | `lastConsumedSequence` 在 6745 先更新，6789 去抖可直接返回；6848 的模型失败后 6880 仍更新 `lastModelSnapshot`。`modelInputThisFrame` 只证明尝试。adoptedSequence 只能在成功 Append/Extract 或明确 raw Down/terminal fallback 实际成为几何后推进，并记录 adoptionKind；成功 Update 但没有对应几何不能捏造本帧贡献。 |
| Live L1/L0，10724–10787、10997、11061–11079 | 普通稳定前缀、橡皮与 Shape 共用层，有末笔结束后的全量 rebuild 和 Shape-only 保留分支。`metricVisible` 在完整 L0 draw 前就可为真。必须分别保存当前几何身份与成功栅格身份；只有相应共享层成功且最终合成覆盖已知非预测贡献后才冻结 Live proof。其它笔失败导致跳过整帧 Present 时没有任何 landing。 |
| `CommitRuntimeStoredStrokeCpu` 1741–1832；正常 Up 10863–10985 | CPU Canvas/history 先提交，得到真正 RenderItemId/strokeIndex/before/after；10967 在 submitted=false 时也推进 rasterState。失败帧的 CPU 真值不得被称作 L2 成功。note 应在 11013 回收/11014 Reset 前复制，随后只持 opaque 键和自有数字。 |
| `InkHistory.cpp::Find` 1042；Undo/Redo 949/974；UpdateItemGeometry 1000 | id 必须同时核 index/generation，当前 item.visible、strokeIndex、contentGeneration 与 note 相符，afterStates 对应同项。Undo 和 Redo 都推进 contentGeneration，旧未呈现项不能 Undo→Redo 后复活。单纯 history revision 变化不能让仍可证明的旧项因另一笔 append 静默消失。 |
| `restorePageContent` 8018；恢复/resize 9679/9805；分 Tile replay 9903–10017 | 完整 restore 返回非 Failed 或全部必要可见 Tile 成功才能建立对应当前 history/viewport/尺寸的 stamp；空 visible tile 集合须结合真实清层事实。每次清 L2、部分 replay、GPU 失败先撤销 stamp。写入另一笔成功只能延续此前有效的 before stamp；不能把当前 rasterState 或 viewportVisibleClear 单独拍成新权威。跨帧 replay 必须核实最后 stamp 真覆盖当前 CPU history，不能只在尾部复制最新 revision；变化无法证明时 withheld，不为指标额外重放或请求帧。 |
| `undoCurrentPage` 8509 / `redoCurrentPage` 8644 | 候选 raster、CPU visibility 和 rollback 有不同成功点。只有成功 visibility 事务才终止对应隐藏项 pending；失败仍保留 CPU 真值，但写前/失败/rollback 的 GPU stamp 必须按实际恢复结果结算。局部热前像或 affected tiles 恢复不自动证明整页已清晰。 |
| Clear 8835 / snapshot ReplacePage 8887 / slot switch 8130 / page switch 9607 | 成功 CPU scene 改变立即失效旧 scene pending；返回同 GUID 旧页也用新 metric scene，空 Clear no-op 保留。slot load/materialize 在 CPU 安装点区分，不靠 pageIndex/PPT 页数。Underlying raster generation 的现有绕回不能在 metrics 中制造身份复用。 |
| `PresentFrame` 5003；真正 Run 调用 11251 | 返回后即时 QPC 在 observer、ready 和帧末之前采集；一真实调用一次 result/attempt，候选各自按 same returnQpc 确认。11205 trusted/模糊 fallback 不是当前 history 的权威 Stored 内容；11231/11235 Laser resolve/draw 失败也会阻止实际 Present。 |
| `TransparentPresentation.cpp::Present` 925–960 | 返回 true 才认可该调用的 observation；951–953 在失败时也填写输出/content 值，不能由字段存在推成功。output target/revision 必须来自绘制 owner 冻结值和当前 observation，不能拼 Host 原子快照。 |

Live→Stored 转换时，即使 Down 已确认，也必须继续记录 Up 终帧的 frame/cost；只是不再新增 Down landing note。首批 Session 只有 Down landing，Move→Present、Up→最终稳定 latency 仍是未采集，不能由 Down 样本或全局成功计数派生。终态 physicalAfter=0 的帧、命令/页/resize/recovery、Laser/粒子及 rasterFailed/noPresent 的真正 render attempt 应进入 RecordRenderFrame；idle/早 continue/维护 loop 另记，不能沿用 11351 的 active-after 子集作为整帧分母。

## 前版 R1–R5 发现与验收依据（修订合同已关闭，实现未做）

### R1 — P1，U2 实际 Present 分母与 proof 拒绝混用

- File：合同 §5 第 4/5 条（113–114）；源码 `DrawingController.cpp::PresentFrame` 5003 和 `RuntimeMetrics.cpp::RecordVerifiedPresent` 523。
- Issue：第 4 条要求一真实调用 Record 一次，第 5 条却要求 output identity 不符时不 Record。若真实调用已返回 true/false，身份不符只说明该候选不能确认，不能抹掉实际 attempt/outcome，否则完整帧/失败与 coverage 分母变小。
- 最小调整：只有未调用 Presenter 的帧不 RecordVerifiedPresent；每次调用均在原返回点记录真实 result/wall。身份不符保留 outputMismatch/invalid-or-withheld 原因，拒绝 landing。raster/composite 失败且没有调用时为 noPresent/rasterFailed，不能合成一次 API false。
- 验收：同一生产 assembler 覆盖 no-call、真实 call-false、call-true 匹配/不匹配；后两种都增加一次 attempt/result，只有匹配项可确认。外部 ClearCanvas/PresentFullCanvas 空候选也应开有效 serial 边界，不能沿用 Run 候选或给 frameSerial=0 的冷帧报有效 RecordRenderFrame。
- Why not fixed：这是尚未实施 producer 的调用合同，修改实施者设计/源码不在本 reviewer 所有权内。

### R2 — P1，U2 断触续接替换 handle 的旧 pending 未冻结终态

- File：合同 §4 的 reconnect 行；`DrawingController.cpp::initializeStroke` 6289–6313；U1 `CommitVerifiedLandings` 477 的 Live 本帧 restage 门。
- Issue：真实续接成功在 6289 Recycle 旧 handle，6290 接管新 handle/Down，且旧笔尚没有独立 Stored item。合同只要求新物理 contact 单独登记，没有指定旧未确认 Live 的终态。旧 proof 下帧不能 restage，会一直占 pending；若之后把合并 Stroke 的新键反填旧键，又会错误归属。连续这类受控失败可占满 64 槽。
- 最小调整：成功接管前用旧 opaque key 精确 InvalidateContact，计 reconnectSuperseded/unpresented；已 confirmed 的旧 contact 幂等不增 unpresented，新 Down 独立一次 Register。最小首批不保留旧 Live 到新 runtime，也不要求增加多 contact ancestry registry。续接失败保留旧 runtime 与原流程，新 contact 按其实际后续初始化结果处理。
- 验收：旧 A 首帧失败、deferred Up→真实 reconnect B 成功，旧 A pending 被释放，B 可确认，另一个 Stored C 保留；A 已确认的反例无重复/额外 unpresented。连续 >64 次类似交接不能仅因遗漏失效制造 pendingOverflow。精确 InvalidateContact 只结束 A 的 Registered/Pending 和槽，不删除保留 key 的去重记录，不撤销 B 同帧有效成功资格；取消 A 不能全局 InvalidatePending。
- Why not fixed：需 root 批准必要的小 Session API，并交给唯一 Session/Controller writer；没有为此重写 U1 或触碰 ContactInput 回收。

### R3 — P2，U2 raw output revision 的合法零值与 U1 非零 proof 不兼容

- File：合同 §4 outputGeneration；`TransparentPresentation.cpp::Initialize` 793–794、`SetOutputTarget` 823–828；`RuntimeMetrics.cpp::ValidProof` 100–109。
- Issue：Presenter 初始 PrimaryDrawpad 的 revision 合法为 0，同目标 SetOutputTarget 不推进；合同要求直接复制 RequestedOutputRevision，而 U1 拒绝 outputGeneration=0。当前 Product 默认先走 Selection，通常避开它，但这不是 getter 的非零保证，更不能让合法初始输出 silently invalid。
- 最小调整：保持 Presenter 行为和 U1 非零合同；Controller 旁挂 output generation 对真实 `(target, rawRevision)` 做有界非零编号，或用检查溢出的无损 `rawRevision+1` 编码，验证时仍匹配原 raw target/revision。不能将零强钳为 1 并与真实 rawRevision=1 混同；穷尽停止诊断。raw 值和 metrics 编码各自说明，pipeline generation 不冒称 device epoch。
- 验收：真实初始 Primary/raw0、Selection/raw1、返回 Primary/raw2，三者身份不同；相同合法 raw0 可形成有效 proof，切输出旧 proof 不确认；旧 Stored 可在当前输出真正成功后 restage 一次。
- Why not fixed：owner 数值编码是 U2 设计选择，未擅自改公开 Presenter/Session 接口或 product 初始选择模式。

### R4 — P2，U3-F 缺少可执行的冷/预热/稳态分界

- File：合同 §8–9；`RuntimeMetrics.cpp::WriteJson` 694–706、724–733、739–745。
- Issue：Host 在 Controller/首帧之前创建同一个 Session，当前 schema2 按 device/tool 汇总全 run，没有 warm-up/scenario/cutover 字段。合同要求 ≥16 完整预热、冷/首次/稳态分开与每群 ≥200，但没有规定用哪个 owner 边界排除预热；直接用现 toolSummaries 会混群。不能 warm-up 后 Reset 来修它，尤其同址 generation 与失败 pending 仍属该 run。
- 最小调整：在固定场景 manifest/旁挂 POD 中冻结 cold/start、最后完整预热成功与稳态 cutover 的 QPC/frameSerial/contactOrdinal 边界，owner 发布，停止后用现 raw 按边界统计。Session 不 Reset；跨界失败/未终态明确 incomplete 或独立归群。每工具/轨迹独立 run 可避免新 scenario registry。给 CLI/runner 固定 core 轨迹的 packet 数、发包间隔、entry/effectiveTool/真实 eraserWidthMode、静止/Up/Cancel 与结束预算，未知 population 拒绝或未验证。
- 验收：16+200 的已知 run 中 steady landingCount 只能为 200（冷/预热另有 16），失败、drop 和 withheld 在其原群体分母，不按最漂亮样本删掉。保留三轮 Release 每群实际 count/median/P95；不足 1000 的 P99=null。每轮失败/截断原始数据保留，不选最好一轮；≥50ms 等长帧口径与旧 legacy 16.67ms 分开命名。
- Why not fixed：尚无 runner/CLI/manifest 实现，需其唯一 writer 冻结小数值合同，不增加 Session reset 或通用标签框架。

### R5 — P1，U3-F on/off 像素与内容等价尚无实际 owner checkpoint

- File：合同 §6 的 off/on 像素要求与 §7 stop/export；`Host.cpp` 1224–1227；`HiddenWindowTest.cpp::CheckPresentationPersistence` 449–674；真实注入 1507–1509。
- Issue：现 Host 未提供 renderer 像素读取入口；Stop 真 join 之前绘制 owner 已释放 Controller/Renderer/device，之后 WriteRuntimeMetrics 不能读取像素。现 CheckPresentationPersistence 复用真实 Host→worker→UInk/冷载，但路径派生到 EXE 旁；其 ReadUInk/内容 hash 不是 backbuffer 比较。两次隐藏轨迹各自读取真实 QPC，modeler 输入时间在 Controller 由 QPCDeltaSeconds 形成，独立 wall 轨迹也不能假设是完全相同 source 序列并强断言 Pen 像素逐位相同。
- 最小调整：冻结一个仅等价夹具启用的有限 owner checkpoint：停止发送、消费必要终态、目标成功提交后且 Renderer 仍活时，在绘制 owner 一次读取自有目标并发布自有数值/像素结果；在 timed benchmark 中不做每帧 readback/GPU wait。或先限定可重复的 raw-final Shape 场景做 exact BGRA，明确其它模型工具还需固定 source 的生产 fixture 证明，不能把该小格扩成全工具 PASS。固定 source 的功能 replay 与 owner 真 QPC 的性能 run 分开命名，不能把计划 tick 冒充性能 sourceQPC。无需给 runner 暴露 COM/context 或通用 Renderer getter。
- 生产 UInk 复用：功能验证另用 privateRoot 下 sandbox 根，沿真正 Host final barrier/worker Committed→生产 codec/SubmitLoad 核 visible stroke/order/样式/宽度/终点/页身份。新 root 必须显式传入，不调用旧 helper 的 EXE 邻目录创建段，不靠任意孤儿文件存在或旧 hash 不同证明恢复。性能场景按合同保持 autoSaveRoot 空；持久化反例单独配同样 on/off 设置，不改存储格式或放宽产品 gate。
- 验收：off/on 必要 Down/Move/Up/Cancel 发布、消费、回收和业务结果均核对；有实际 owner checkpoint 的格子再做像素比较。读取请求失败/未到达、终态未处理、退出仍活或强退不能算等价通过。Report/manifest 明确哪些工具具备 exact 输入与像素证据，哪些仅有语义证据或尚未验证。
- Why not fixed：checkpoint、确定性输入边界和 renderer 寿命属于尚未冻结的 fixture 接口，本 reviewer 不越权添加测试 hook 或扩大公开接口。

## U3-H 可实施边界与 runner 安全门

`Host::Impl` 138–143 已拥有独立 graphics/renderer/presenter/drawing/jthread；当前1172 构造 Controller 的位置可传 borrowed Session。defaultoff 不创建 Session/旁挂表，不新增 QPC/ThreadTimes/输入诊断计数；opt-in 才在构造 Controller 前完成一次预分配。Session 与旁挂状态的实际 sizeof/capacity 共计须核 32MiB payload，prepared 失败标 unavailable 并继续原启动，不能用空成功报告掩盖失败。恢复 tuple 在绘制 owner 保存，当前1238 清 graphics 前复制纯值，禁止引用 UI3 RenderPipeline device 或输出 COM/GUID/用户文稿路径。

当前1272/1322 的失败 Start 与1399 的正常 Stop 真实 join 是封口门；1239 的 running=false、observer inactive 和 stop_requested 都早于/弱于 join。新增 owner 创建失败分支1250–1263没有线程可 join，也须标 startupFailed，不能给正常 sealed run。原命令关闭→RTS Shutdown→最终保存屏障→worker drain→request_exit→join→detach 保留，metrics 不在 drain 中写盘。显式 joined/sealed/startupFailed/exportUnavailable 区分完整与失败 run；再次 Start 必须处理上一份 joined 报告再创建新 Session，禁止 run 中 Reset。`WriteRuntimeMetrics(... ) noexcept` 调用会分配/格式化的 WriteJson 时须 catch 并返回诊断失败，不能触发 terminate。最终 input diagnostics 用本 run baseline 差和占用快照，既有累计计数 Reset 不归零。

最小 hidden fixture 可只提取 `MakeHiddenSpec` 94 与 2505 的四窗构造：MagnifierHost/Freeze/DrawpadPresentation/Drawpad，`visible=false`、屏幕外、自有 Window Service。DComp 创建期样式与 fresh legacy ULW HWND 分开；不用 RunMode 的显示/Capture/Office 正确性场景。StyleContext/Service/observer/checkpoint 上下文保持到 StopProduct 真 join，异常同样先停 Host 再销毁 Service。必须防止从 callback 内反向 Stop/join 同 owner。

现 `Host::ForwardMessage` 1613→`PublishHiddenTestContact` 1490 的返回 0/-1 能作为 accepted/rejected，真实 owner QPC 是 source；有界 SendMessageTimeout 的超时不是未执行证明，不能自动重发 Down。核每个 HWND 属本 Service/当前 PID，不能使用全局输入或 Computer Use。WaitUntil、RuntimeRevision、新的 atomic progress 仅等完成，不是 latency 终点；禁止从活跃 runner 直接读非原子 Session。

`--draw3-presentation-benchmark` 当前没有 IdtMain 分派，仅已有 geometry/history/ulw-copy/hidden CLI。新显式 standalone early 分派须严格解析、验私有绝对 root/父组件/非reparse/本轮唯一创建，在配置、互斥、Office、全局业务与普通启动前 fail closed/返回。若改成 parent/child，则冻结独立 purpose/version、exact parent HANDLE/PID/image/private bin 与限制继承，现 C/UEF/startup 信封不能自动授权它。root 持自己创建的 child HANDLE 和明确整轮预算；Stop 尚未返回时只能记 incomplete，父清理不算产品自然结束。固定文件名 CREATE_NEW，拒覆盖旧报告/sentinel，不递归清未知路径。实际实现首次运行前还需按新 CLI/信封实码安全审查，当前没有运行放行。

## Laser 调查与首批排除依据

`LaserStrokeLayer` 1876–1887 只有 id、RuntimeStroke 借用、completedPoints、incremental cursor/bounds/cancelled，没有 adopted input/raster 版本。1910 的 incremental 成功可以证明一次 coverage 操作，但 1963 Finalize 清 runtime 归属，1983 Bake 成功在 2036/2073 清 layers、将多笔颜色合并；layer id 不再能证明某一 contact 后续成功帧。10611 的 full redraw 分支甚至先推进 incremental cursor/bounds，真正 draw 在 2106–2118。生命周期 lastAllUp/opacity 是整批计时，粒子/光标 Draw 为另一套路径。因此直接把 id/bounds/phase 或普通 history token 接到 U1 Laser kind 会给出不成立的因果成功。

排除正式 landing 合理；新增完整跨 incremental/full-redraw/bake/fade/device-loss 的 per-contact transfer ledger 超过当前最小范围。本轮仍应采 active、Up/bake、Hold idle、Fade、expiry、粒子及 fail/retry/noPresent 的 frame wall/actual attempts/lifecycle counts，保持真实样式/粒子/hold/prediction/pacing。`RuntimeMetricsFrameSample` 当前 reasonFlags 没有固定 enum、阶段 wall/CPU/资源字段尚未接线，必须由唯一 writer 冻结有限值并说明 inclusive 父子段；不得因 excluded 把这些都记 unavailable 后宣布 Laser 调查完成。没有可得 GPU timestamp/显存/cache hook 的项目保留 null，不用软件 wall 代算。

## 最小实施后验收

以下均未运行。U2 probe 必须调用 Run 共用的真实候选/stamp helper、真实 Coordinator/InkCanvas/CanvasRuntimeHistory，固定 CPU success/failure 只证匹配合同；真实隐藏 Host 再证 producer 与实际返回点。

| 场景 | 必要断言 |
| --- | --- |
| Down+Up 同帧、真实回收后首次 Present 失败→同 item 成功 | record 已不可读/可复用，Stored 仍纯值 pending；下一权威 L2 与真实成功确认恰一次，source Down 不变。 |
| A Stored + B Live 共帧、多 Stored 共帧 | 同一个真实 Present 只记一次；每个唯一 proof 用同 successQpc 逐项确认；错 A proof 不终止 B，确认后 note 及时释放。 |
| 另一笔 append 后失败 A 恢复 | 同项 visible/contentGeneration 不变且当前 L2 stamp 完整才可确认；单凭 currentPageHasContent、rasterState 或 B 成功不可确认 A。 |
| Stored raster失败 / 共享 L0/L1失败 / final composite失败 | 实际没有 Present 时 noPresent，CPU item 保留、stamp 无效；部分 replay或模糊快照成功不确认 Stored；同 history 完整 replay 后才确认。 |
| 去抖 Move / 模型失败 / raw terminal fallback | lastConsumed 已进而 adopted 未进的反例 withheld/计数明确；fallback kind 独立，不能报 model success。 |
| Cancel A、初始化三拒收、reconnect、新同址 generation | 精确失效、一次 Down 分母、旧 key 不读/不作用新 contact；其它 pending 不丢；Closing/E03 原语义不改。 |
| 页/slot/load/Replace/Clear/cross-Clear/Undo→Redo | 旧 scene 不复活，空 Clear no-op；成功隐藏项即终止旧 landing；失败 visibility/rollback 不伪造成功。 |
| output0/切 target、viewport/resize、recovery/backend变化 | identity 数值无复用，当前权威栅格与真实输出匹配；既有 page/pipeline值不冒称完整 DPI/device epoch。 |
| Up 终帧/stop/fatal/异常与容量 | after=0 的 frame 仍记录；fatal CPU seal 没有成功 Present；join 后 pending→unpresented，满容量 seen/drop/invalid/overflow 分母保留，构造前后无热扩容。 |
| U3 defaultoff/on、Start失败/owner提前退出、再次 Start | off 无 Session/额外指标时钟；on 故障不改生产准入/画质/pacing；未 join 禁导出，两个 run 不混键，private sentinel 不覆盖。 |

root 串行完成受影响的 `InkeysRepo.sln Debug|ARM64`、strict no-window/parked/相关真实 hidden 套件，再完成适用 Release 三架构与同 Release/device/backend/effects/trajectory 三轮每群采样。真正 render 线程 GetThreadTimes 的 user+kernel delta 单列 threadCpuMs（失败 null），frame/Present/stage wall 不叫 CPU/GPU；进程 private bytes/WS/HANDLE/GDI/USER 在 warm 前后和测量结束查自进程，长期/显存/GPU/cache 缺口分别保留。采样期间停构建和其它基准/扫描。

## 文件身份与 Verification

| 文件 | 本次 SHA-256 |
| --- | --- |
| RuntimeMetrics.cppm | FA0043331A8E9D23C937D504C0A594500D0EB47CF9E659FE8ADECDB8B4DEEA4F |
| RuntimeMetrics.cpp | C65B27BAF4EBA744B516B5E4D17C30A73432D09FD6C91B6FE8B0B26E55700A3B |
| DrawingController.cpp（M16 后） | 5FBBEB09C5E120E537FAFEB33CA3E9E3955F71FF4D8EA87C1F72CD2403AEA4F5 |
| Host.cpp（C2 接线后，本次寿命边界快照） | 60E84B339CB16B8A17AA72A6A62CC83E02A15CAE5513E3292ADFE9C0617FC595 |
| HiddenWindowTest.cpp | 99046FCC96EC0091A412BDF97E92D3608610DE5953A70A3EE09C303E622B4995 |

- Lint：未运行；按只写研究文档的分工，没有源码机械修补或 Git 检查。
- TypeCheck / Build：未运行；C++ module/链接门由 root 完整 Solution 执行，本报告不占构建槽。
- Tests / EXE / GUI：未运行；没有本次动态 PASS。既有 U1/M16 绿色仅引用其独立实码报告，未扩成 U2/U3 producer/像素或性能证据。
- 本次增量：合同冻结 SHA 已复算为4F6839B4；RuntimeMetrics两源、Controller/M16和HiddenWindowTest身份与前版相同。Host已随root C2改变，上表更新为当前60E84B33，实际GPU释放/两失败join/正常Stop及生产Desktop Submit/Completion链已重读。只支持本次设计依赖核对，不替C2实码复审或其运行证据；源码中的metrics/phase/checkpoint/replay/receipt接口仍未实现。
- Spec：实施之后由 root 同步 Draw3/input/errors 与 metrics默认关闭、成功戳/opaque寿命/失效/封口/schema 口径；当前不写成已实现。
- Win7 SP1 仅 KB2670838、Hardware FL11.0 有/无→WARP、DComp 缺失→ULW、FLIP_SEQUENTIAL 保持/两 DWM 禁用仍须目标系统实测。真实笔/Touch、同型多 Touch/压感注入、光学/GPU、长文档/长期资源、Office/跨进程恢复与 HC-H2 同设备可比性均未由本报告关闭。
