# Draw3 U2-P1 内容 proof helper 实际代码独立复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。本 reviewer 按 root 分工只读实际 AGENTS/check.jsonl、已加载完整 hook、相关 native/Draw3/input 规范、完整父 handoff、最终 U2/U3 合同、实施报告、四源实际 diff/callers 和红原始文件；只写本报告。不递归派发，不改产品/工程/spec/其它报告/账本/Git，不运行构建、CLI/EXE 或 GUI。

## 结论

**U2-P1 当前 helper 候选 STATIC_GREEN；绿色动态门 pending。** 未发现阻断本批精确失效、纯值 proof 装配、固定预算或回收的源码问题。当前只能支持唯一 writer 已实现的数值 helper，不能给 normal Run/PresentFrame 接线、真实 GPU/Presenter、Host opt-in、性能或恢复门 GREEN。root 需在组合源码全部冻结后完成新完整 Debug|ARM64 和 parked 回归，再补相应动态证据。

实际调用搜索显示 `ControllerContentMetrics::Prepare` 仅在新 Fixture/budget probe 中使用；真实 `PresentFrame` 6172 仍于6179调用旧 RecordPresent，正常 Run 6263 仍在12178/12339/12487调用旧 StageLanding/CommitStagedLandings。这个 P1→P2 边界与本批许可一致，不将名称“ProductionProbe”解释为已经产生真实呈现 proof。

## 身份与最小 diff 核对

合同 SHA-256：`4F6839B450AAE10E298F34B28F8B54C4D77CC8AF9988B5AF6B4E0F67E171545F`。实施报告 `draw3-actual-metrics-implementation.md` SHA-256：`9D28144F8F66BBB62C17D070EBAA18C7044007A65BB5A30A41B81D5A0CE02325`。

| 四份冻结源（Draw3 目录） | reviewer 实算 SHA-256 |
| --- | --- |
| Draw3.DrawingController.cppm | 8BB0B41749CB6121B3C6257CAD0F60F4548E771D491813CA67FF3A708FAD9A7A |
| Draw3.DrawingController.cpp | 1687DA015DB78D816E06E6283A5EE14C41E7EEBAADF3469205CC94FC35CA32CC |
| Draw3.RuntimeMetrics.cppm | 87E370C33F7A080F26C97AD628C2363D6614040323CB2700E33A73D2B37B0F55 |
| Draw3.RuntimeMetrics.cpp | FDBE988ED85F94F51DDE0ACCB5B9358AB7C01AA8C3106F4FFDD670A482E004E0 |

没有只信作者“字节不变”的报告：本 reviewer 用内存内的 UTF-8/BOM/CRLF 原文分别移除 Controller 新 helper（1835–2472）、新 probe（4642–5171）、parked 的新增调用/结果合并，以及两个 module 的两行声明和 RuntimeMetrics 的精确失效方法（501–523），不写回文件。恢复后四个 SHA **分别准确等于**实施前 `5FBBEB09…AEA4F5` / `8F22AD84…C0EE3F` / `C65B27BA…700A3B` / `FA004333…EEA4F`。因此 normal Run/PresentFrame、E03、M01–M16、既有 schema/统计/输出均仍是接手时的字节；对 HEAD 的较大 diff 包含旧 U1/E03，不当作本 P1 新改动。未发现额外格式化或工程/资源触点。

## Findings (fixed)

无 reviewer 修补。以下是实际候选中已关闭红接缝的核对结果，不是本 reviewer 执行后的测试 PASS。

### 1. opaque key 与精确失效

- `RuntimeMetrics.cpp::InvalidateContact` 501–522 只以 `{opaqueRecord,generation}` 查现有有界 key 表，不解引用。仅 Registered/Pending 首次转 Unpresented；Pending 最多释放其唯一匹配槽，保留去重索引，恰一次增 unpresented。Confirmed/Invalid/Legacy/Unpresented/未知/stale key 不变。
- 该方法没有写 `currentPresentSucceeded`、frameSerial 或其它 pending；与紧随其后的全局 InvalidatePending 分工明确。U204 在已 RecordVerifiedPresent(true) 后取消 A，再实际 Commit C，能发现误撤同帧成功资格的实现。
- Controller 1836 的 key、1898 的 Stored note、1912 的 candidate 是值载荷；helper 只比较键或传给 Session。`RetireNotes` 2226 按精确键压紧候选并释放自己的 note，`Invalidate` 2235 只在 Session 的首次终结返回 true 时记普通 reason。没有通过旧键访问 Down/Generation/TryReadSnapshot/Recycle，也没有旧 runtime/COM/HWND 借用。
- Fixture 4711–4714 在真实 handle 活时复制 Down；Store 4754 用真实 CPU commit/history，然后4763真实 Recycle。4816 的旧 handle 读取仅用于断言 Coordinator 拒读，helper 并不借此恢复源身份。

### 2. consumed、adopted 与 Live raster

- `NoteConsumed` 1991 只更新 consumed；`Adopt` 1995 要求合法 kind/phase、正 QPC、同 Down admission、恰为已消费 sequence、非倒序且确有非预测几何。成功才分配 checked 内容 token，更新 adopted/geometry，撤该 contact 旧 raster资格。RawDown/RawTerminal 分别限 Down/Up，不用模型失败后的 lastModelSnapshot 冒充采纳。
- `ObserveLiveRaster` 2022 独立确认 sharedLayersSucceeded、当前 Session serial、同 scene 与对应 geometry/adopted sequence，再锁 rastered sequence/serial。L2-only 的 InvalidateL2 不抹其它 Live；`InvalidateRaster` 2221 才清共享层资格。
- `FreezeCandidates` 2353–2371 要本帧 adopted==consumed==geometry==rastered、同 admission/raster/scene、非预测 bounds 有视口内投影且与实际 composite 区域相交。去抖/模型失败的 latest sequence只 withheld，计数按 withheld sequence 去重，不称精确 coalesced/逐 Move 覆盖。Laser不 stage 正式 landing。

### 3. Stored、L2 连续写与 replay

- `CaptureStored` 2036 从 Find(exact RenderItemId) 复制 visible/strokeIndex/contentGeneration/afterStates、实际已消费 Up sequence/QPC/admission，随后能脱离回收后的输入对象。已确认 Live 仅退出 Down note；该 bool 是指标终态处理，不是产品 CPU/GPU/保存成功值。
- Freeze 2332–2343 再核 exact generation、visible、strokeIndex、contentGeneration、Canvas stroke范围和同项 afterState；不合格精确终结。另一笔 append不会仅因整页 revision变化抹掉未变项；Undo/Redo的 contentGeneration变化不会复活旧 Down。
- `ContentMetricRasterSignature` 包括 scene/raster、history revision/rasterState、viewport/scale/尺寸；输出身份不写入 L2 stamp。完整 replay 2166 必须当前有效 signature与成功输入；局部 Begin 2173 先锁匹配的旧 stamp再撤资格，Complete 2182 必须同 before、同 surface、当前 after 与成功链。失败后没有新匹配 Begin 的 true completion仍拒绝，不能因下一笔成功就恢复整页 authority。
- Visible replay 2195–2211 保存开始 signature，partial保留计划，结束时仍必须精确相同；变化立即拒绝，不能拍最新 revision认证旧计划。清层/viewport/resize/device等共享失效须由未来 owner调用 InvalidateRaster，不用 raw rasterState当成功证据。
- StoredProjection 2282 先 finite检查，在 double中偏移/裁剪再转 LONG，完全视口外拒绝。Stored需当前同 signature L2 stamp，而且本次实际 composite dirty完整包含当前可见投影。
- Freeze的 `fullComposite` 在2348–2349指实际整视口合成，并核 compositeBounds确实覆盖整视口；没有以 PresentFull API flag授予 authority。部分实际 dirty只能确认其真实完整覆盖的 Stored投影，不能借旧 backbuffer补没有证明的区域。

### 4. output 与真实 Present 计数

- `ObserveOutput` 2130 将 exists/raw tuple/匿名编号分开；Primary raw0与UINT64_MAX都是合法原值，相同 tuple保持编号，切换或InvalidateOutput后另取非零 checked编号。不对raw做加法，不绕回；scene/content/output耗尽由 ExhaustIdentity 2246撤销未确认键与候选，产品状态不改。
- `PresentReturned` 2387 首先对每个 called=true独立 RecordVerifiedPresent(wall,bool)，无 proof、false或success错output均保留一次真实分母；called=false不合成API false。成功错output另计outputMismatch，不给landing。
- 本帧serial、冻结/当前/observed输出三者一致且compositeReady才逐候选Commit；Stored还复核当前L2 signature。每个候选都用同一个传入returnQpc，confirmed恰+1才释放Stored/标Live confirmed，逆时无效导致pending释放则同步退出note。candidate数组清空使重复调用不重记landing；第二次 called=true仍计另一实际调用，这是 caller必须据真实API调用次数提供的合同。

### 5. 有界数据、默认空与容量

- Live64、Stored64、candidate128、mapping64和单个stamp/replay/output均为固定数组/POD；Session pending仍64。没有在 helper 中 resize/push/sort/字符串、日志、文件、额外QPC、GPU query/wait。token/output/scene checked，映射满或产品raster generation绕回停止诊断，不能复用旧身份。
- `Prepare` 2461 先判 session=nullptr，再按 `sizeof` 与externalAuxiliaryBytes检查共同32MiB，差值按短路顺序防下溢，满足才一次nothrow分配；static_assert ownBytes≤64KiB。外部Host功能buffer/phase/source receipts仍须未来调用者事先计入。
- U209用真实 Session allocatedBytes验证null、精确边界、+1与SIZE_MAX、307200字节功能buffer，并准入/提交/回收65个contact测试64 Stored容量。Session拒stage/overflow终态时StageCandidate 2308退出对应旁挂，Session原分母保留，不能泄同槽或限制产品准入。
- `RecordFrame` 2422不依赖physicalAfter非零，noPresent/rasterFailed与实际FrameSample分别保留。Laser intentional excluded进入独立计数，未呈现子集不伪装普通Stopped；这不证明Hold/Fade/particle等软件成本已有producer。

## probe 与红证据

`RunDraw3ContentProofProductionProbe` 4642 使用真实 Coordinator、Canvas/history、CommitRuntimeStoredStrokeCpu、Find/Undo/Redo，与今后 P2 将调用的同一个 helper。Fixture的 Authorize/成功bool/Present返回参数是明确CPU合同输入，没有 renderer/真实 modeler Update/Presenter故障。Prime仅隔离测试Session失效接口，不重写候选接受算法；不把固定QPC差称落笔延迟。

| 用例 | 实际可核的意义 |
| --- | --- |
| U201 4813–4834 | 真Up/Recycle后旧handle拒读，失败pending跨BeginFrame；当前同项成功恰一次，Stored note退出。 |
| U202 4836–4879、5003–5024 | 真实其它页、Undo/Redo、UpdateItemGeometry、CPU Clear/返回旧页；item generation/strokeIndex/afterState反例，旧scene不复活。 |
| U203 4881–4921 | afterState无权威戳、旧失败后另一笔不能修复、变history/partial replay拒绝；新增匹配local正例、失败后无Begin true completion反例、同计划partial→complete。 |
| U204 4923–4947 | Stored+Live/多个Stored一次Present，多proof同returnQpc；取消A保留C与同帧真实success资格，重复取消分母不增。 |
| U205 4949–5001 | 70次真实Touch准入/Up/recycle后精确失效，C pending保留，无pendingOverflow；confirmed A与同址新代次B、Registered未stage和未知/stale键。此处没有调用实际reconnect匹配/modeler接管。 |
| U206 5026–5060 | raw0/repeat/raw1/raw2、恢复新编号、极大raw/耗尽、失败Stored新输出restage。 |
| U207 5062–5088 | no-call/false/true match/true mismatch的attempt/outcome，终态after=0和noPresent/rasterFailed分母。 |
| U208 5090–5141 | 真实Move消费但未采纳、无几何/栅格失败/旧帧Live、RawTerminal；Laser excluded仍记录frame/result。模型和Raster的bool仅夹具输入。 |
| U209 5143–5161 | 共同payload检查在分配前，固定Stored溢出与真实输入回收；绿色sizeof待root实际运行。 |

已直接读红日志/status：`draw3-u2-p1-red-debug-arm64-build`是完整InkeysRepo.sln Debug|ARM64，exit0、Build succeeded、0 Error/14旧warning、36.61s；`draw3-u2-p1-red-debug-arm64-parked`为自然exit1/pid14764，旧六组PASS、新U201–U209共29具体FAIL，未见fixture准备异常代替期望红。红payload为Session81912/aux38352/共同33554432字节，属于红源，不填作本候选sizeof。上述文件在忽略的TestResults/release-hardening；本 reviewer未重跑。

## Findings (not fixed)

- **P2，绿色动态门尚缺**：当前 four-source helper候选没有对应绿色完整Build/parked结果。红Build0、旧U1/M16/C2/B06绿不能替代它。root须等C3/UI并行写入都冻结后串行完整Debug|ARM64与新parked（新旧全部组）、strict Headless/适用PptCOM；源码若再修，按新身份重验。只读分工禁止本 reviewer执行。
- **P2，必要负例补强**：当前probe的正常Freeze始终用320×240全dirty/fullComposite=true。尚缺同一helper的“fullComposite=true但dirty仅部分→拒绝”、partial实际dirty不足/足够覆盖Stored、完全视口外/viewport或尺寸改变、wrong admission/旧geometry sequence等专门反例。当前实际守卫正确，未据此发现源码错误；在P2真实caller接线前宜补这些确定合同，避免把PresentFull/非预测bbox/旧surface重新接错。mapping/content/scene耗尽也不能用现output耗尽反例冒称全覆盖。
- **P2，P1→P2实际producer门**：还要在initializeStroke三拒收/reconnect接管前、真实Append/Extract与fallback、共享L0/L1成功/清层、local/full/tile replay、slot/load/Replace/Clear/Undo/Redo和viewport/resize/recovery上接正确方法。helper只相信owner提供的成功bool/signature/actual composite区域；当前代码无法证明这些值已来自生产GPU。PresentFrame须在实际调用返回后即时锁QPC并恰一次PresentReturned，早continue/idle/终态/noPresent帧也要按实际原因记录。metricVisible、lastConsumed/lastModelSnapshot、已推进rasterState或PresentFull不能提供该证明。
- **P2，诊断unavailable与sealed报告**：identityAvailable=false、Prepare失败、producerOverflow及excludedLaser是旁挂状态，现U1 WriteJson尚无这些完整整合。之后Host/report必须以本run真实input baseline和这些状态标incomplete/unavailable，不能只读Session coverage给PASS。Laser的unpresented是intentional excluded子集，不能当普通失败或零延迟。Host默认off/新Session/真正join后封口、phase16/200/source receipt/功能checkpoint及metrics-off共用小stamp仍未做，禁止dummy Session填off格子。
- **P2，性能/平台/恢复未验证**：线程CPU/stage wall/Laser生命周期与资源、逐Move/Up/安静窗口、Release/其它架构、真实RTS/硬件笔、光学/GPU、Win7 SP1仅KB2670838、HC-H2同机、长期/Office/新进程可见恢复都不能由这个CPUprobe关闭。P1没有shader/资源/依赖/backend修改，FLIP及两个DWM禁用/默认closed功能没有因此变化。

未擅自补测试/接线或修改接口，因为本轮唯一源码writer是实施者；本 reviewer只对P1给静态结论，后续生产接线仍需root单独许可与独立实码检查。

## Verification

- Lint/静态格式：scoped `git diff --check -- <四源>` **PASS / exit0**；无独立linter执行。四源UTF-8 BOM、CRLF、bareLF=0，模块声明/实现匹配，scope重建指纹四项匹配。
- TypeCheck / Build：**当前绿色候选pending**；本 reviewer未执行。只确认root红源完整Debug|ARM64 Build0，不改写为当前类型/链接PASS。
- Tests：**当前绿色候选pending**；已读真实红parked exit1/29 FAIL/旧六PASS，未执行EXE/GUI。helper STATIC_GREEN不等于动态GREEN。
- Spec：由root在实际生产接线之后同步opaque寿命/失效、成功戳/帧原因/预算/封口与实际coverage；本报告不把拟接线写成已实现。
