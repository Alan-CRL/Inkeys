# UI3 F Source / Bootstrap / 最终读回独立设计复审

日期：2026-10-01。Active task：`.trellis/tasks/09-27-integration-and-release-check`。本 reviewer 只审实际跨组件接口和寿命，唯一写入本报告；未改源码、工程、其它研究、spec 或账本，未执行 Git、build、测试、EXE、GUI、Computer Use 或递归派发。

## 判决

**APPROVE（以下有界接口和时序的设计冻结）；整体 F 仍 NOT RUN-APPROVED。** Source 的 immutable64 / Main433 / Draw435 / warm16+measured200、真实 Touch Seek/action、私有真实 bootstrap 与冻结 Auth 可以继续最小实施。A/B/C 三组接口足够，不需要新 purpose、IPC 字段、Scheduler fence、stop API 或第二套布局/输入/遥测框架。

审查输入为 `ui3-fixture-source-implementation.md` 的191行版本，SHA256 `EE1B6B928666DDC9720A3BA858C8B505588DF42A2D97742EB8A644C3B11B6BDC`。Root 本次派发及后续明确确认的关闭顺序、owning checkpoint job、32MiB 功能上限覆盖该报告旧191行的 Source join→readback→Close顺序。Auth 两源仍采用已独立 scoped static APPROVE 的 `20AB2AF8…87D13` / `552EEC5A…F5EB`；未重复其完整授权审查，也不借 B3 动态结果放行 F。

已读保存的 full hook/context、根 AGENTS、workflow/config、G implement/check manifests、PRD/design/implement、父 handoff、R2与独立设计审查、Source/Auth报告，以及 native-desktop rendering/UI/diagnostics/resources/build/Draw3/input、native quality 和 reuse/cross-layer 指南。下面以本轮读到的真实代码为证据，接口代码块是批准的待实施签名。

## 实际代码决定的最小接口

| 组 | 精确接口/位置 | 调用者与时机 | 边界 |
| --- | --- | --- | --- |
| A | `bool Ui3FiniteObserver::EnableBootstrapBaselineBeforeOwnersStart() noexcept;`；`bool FreezeBootstrapSignature(const Ui3FiniteSignature&) noexcept;` 为 observer 的私有一次冻结步骤 | fixture 在 Bar 注册/Interact 创建前 Enable；仅初始真实 `CompleteAttempt` 成功路径 Freeze | 复用 publication 的 initialStable 和现22-word ready，不给外部提供任意改 baseline 的 setter |
| B | `bool Ui3FiniteObserver::CopyCompletedOutcomeForCurrentOwner(uint64_t run, uint64_t step, uint64_t source, uint64_t revision, Ui3FiniteTargetRecord& out) const noexcept;` | Scheduler `PostControl` 的实际 render owner，复制现512行内精确一行 | 不返回活 span、不读 clock、不改 completed ledger；`CompletedRevision()` 只作轮询提示 |
| C | `bool CaptureAuthorizedFixtureFinalBgra(const Ui3FixtureAuthorization&, const Ui3FiniteTargetRecord& measuredEnd, std::span<uint8_t> destination, Ui3FixturePixelReceipt& out) noexcept;` | 既定本地 EquivalenceAfterRun，正式 Close 后 Source/Interact 已真 join，Bar 尚注册，Scheduler 控制回调一次 | 复用 B 的现成目标记录，替代易错的三个独立 expected 标量；不新增传输 DTO/purpose |

`Ui3FixturePixelReceipt` 沿 Source 报告原提案：generation、committedAttempt、epoch、surface、bufferMutationSerial、targetInvalidationSerial、pixelBytes；width、height、stride、status；sourceX、sourceY。它只在此普通函数调用内复制数字，不进入128B包。最终目标记录与读回的当前成功帧身份分别保存，不能把二者合并成一个假时间戳。

### A：真实 Fit 后的 publication0 一次冻结

直接证据：`Bar.RenderLoop.cpp:2949` 才把真实 layoutTotalWidth 交给 Fit；`Bar.Zoom.cppm:196–198` 可改 configZoom、调用原 UpdateRendering 并写私有 config。`PresentationProbe.cpp:907–911` 当前在 MarkConsumed 比构造时 initialStable，所以固定默认 Zoom 会拒真实 Fit 后的合法状态。初始 ready 中填新签名并没有同步更改 publication 的 initialStable，不能靠 first accepted 后回填补救。

**另一个必须落实的同帧前提：** RenderFrame 在 `:13492` 的 Submit 之前锁 frame.zoom；Fit 可以在 Submit 中改 barStyle.zoom；`:13504` 的 consumed signature 已读取新 configZoom，而该帧仍用旧 frame.zoom 绘制。不能在这个过渡帧冻结“新配置签名+旧缩放像素”。最小处理是仅 finite/bootstrap 观察路径将实际 frame.zoom 与 Submit 后真实 barStyle.zoom 不一致记为 `Ui3FiniteLifecycleInitialPending`，沿 Fit 原来已发的 Request 等下一自然帧；不重跑 Submit、不改本帧 zoom、不补请求。

Enable 必须检查 publicationSerial=0、没有任何 mutation/request/retained row/accepted/revision/completed receipt、observer 尚无 frame/ready、Interaction bit 未发布；仅本次受鉴权 runner 调用。普通 constructor 仍保持现有已冻结初值行为。启用后只允许初始 render owner 暂消费合法0xFF、支持的实际 signature；未知工具/aux/工作区、未知或不匹配的实际 target DPI/display、业务干扰继续拒证，不能把任何合法但外来状态吸收为基线。

Freeze 只在同一初始候选满足以下事实时执行：pub0仍无请求；已消费真实 Submit/Fit signature且实际绘制 zoom 一致；原相关 roles 无 pending/mismatch，initial placement、dock、display 已结束；sameCandidate、goalCurrent、epoch/surface/dims/anchorMapping 均有效；完整 GetDC/ULW/ReleaseDC/EndDraw 成功，anchors 有限且 window alpha 非零。签名来自该帧先前 consumed copy，不能此刻重读业务补字段。

Freeze 私有写 publication.initialStable 一次，随后同一 render publisher 写 ready.initialStableSignature/原 flags，再发布 ready 偶数 release。pub0不调用 NoteCompletedGoal，不伪造 step/revision/accepted 或 Completed 行；初始 layout latch不冒充测量 SVG 完成。重复冻结、accepted后冻结、半写、未settled和失效域均拒绝。

runner acquire 读到 Registered+Transaction+LayoutStable+Anchors 后，才创建 Interact。线程创建与该 ready 发布建立 plain initialStable 的先行关系；实际 `BarInteractionSession::Run` 再发布既有 Interaction bit，runner 收齐后才开 Source。bootstrap 期间不从主线程并发读 plain InitialStableSignature；freeze 后不得再次改它。第一 accepted 仍经过原 FinishAtRenderRequest/FrozenFiniteInputsMatch。

### B：保留第一次真正完成目标的精确记录

直接证据：`PresentationProbe.cpp:1015` 已保护第一次 Completed row 不被后帧覆盖；`:1068–1083` 的 ready attempt/ticks 则在每个完整成功帧更新。`NoteCompletedGoal` 还先于 StoreOutcome。因此 acquire 到 CompletedRevision 和另一次 ready，不能拼成目标完成 tuple。

B 仅在记录过的实际 render owner 上有效；在首次 BeginFrame 记录 owner identity并在 copy核同一线程即可，不能用 capture-off 为 null 的 CurrentFrameDiagnostics 作 owner 证明。线性遍历最多512 retained rows，要求 run/step/source/revision 全部非零且完全相等、scene合法、terminalStatus=CompletedLayoutAndSvg、该行原 Accepted/完整 signature 和成功 proof 有效；复制整行，保全 epoch/surface/trueBarAttemptSerial/整数 ticks/资源失败摘要。错 tuple、未完成、容量缺行、非 owner、已 Seal 都返回 false且不给旧 out 冒成功。AcceptedNoChange 的旧完成引用不能生成本请求的新完整完成 tuple。

setup（仅实际发送时）、warm16、final 的必要 checkpoint 各至多一次；以 CompletedRevision 作预约提示后 PostControl 查询，一次 copy不等待新帧。查询真实 callback 结束后执行，故不能读到 NoteCompletedGoal已发布、StoreOutcome尚半写的行。最终 all owners stopped后仍用 Absorb/全部 publication records 复核216步，不靠最后一行推断全分母。

MeasuredEnd 使用最后 measured 目标的这行真实 finalCommitTicks；on必须 timingValid及原 ticks次序有效，off时间导出 null。owner control排队/轮询/Source停止/join尾时刻都不是目标完成时间。若不能取得精确完成记录，保留未完成结果并正常受监督退场。

### C：一次完整原分辨率 BGRA，统计边界之外

runner 的本地 phase 只能由全部216真实目标已完成、完整 source/events/accepted receipt、停新 publication、收完自有 contact、正式 Close及 Source/Interact 真 join的事实产生。cap仍是同一 exact registry 对象，参数只用72B FrozenInput；不从 mutable Packet/round/capacity猜功能权限。没有这些前提时不调用 C，不设置额外 run-kind。

C 内核 exact cap、B 的 measuredEnd与此 run/最终 accepted goal一致、当前实际 render owner、observer未Seal、coordinator/目标仍存在。本次预定的 Close属于 EquivalenceAfterRun，不把 offSignal=1一概当早停；216前的 Close、任何其它意图/源污染/早销毁/epoch或display变化则拒证。

仅在原 presentCompletion.IsCommitted 的成功分支，旁挂保存该次 viewport/source/尺寸、alpha、actual target identity和最后完整4API身份；必须独立于 raw/diagnostics，因此 off也可用。同时锁存真实 backing mutation/invalidation serial。可以复用现 Ui3SvgProbe 两 serial并提供仅当前 owner 的窄数值读取；不从某个tag的旧 Observation推测当前全背板，也不造平行计数框架。实际 target bitmap lease/identity须核对，补足同epoch/同尺寸 discard/recreate在下一 BeginBackingWrite前的失效空档。

MeasuredEnd是最早完整认证该目标的帧；后续同一最终语义的成功4API帧可能仍推进光影/feedback，所以 C保存的是当前最后成功提交身份，允许其 attempt晚于 MeasuredEnd，但仍须同最终语义、epoch/surface。不得把它的新 tick替换 MeasuredEnd。若最新 BeginDraw/Clear/Draw/failed/deferred已改变 backing，当前 mutation serial与最后成功记录不等，则拒用旧成功像素。不能等额外强制 fullDirty/Request/Flush/present或关闭动画/光影来换成功。

实际 `spec.GetTargetBitmap()`/GetDeviceContext已存在（Rendering.cppm:120–138），不需新 renderer公共 getter。在 owner控制任务中保持 context/bitmap本地 ComPtr lease，创建 CPU_READ|CANNOT_DRAW readable bitmap，从原 ULW真实完整 source rectangle CopyFromBitmap，再 Map逐行复制所有 width×height×4 BGRA，核真实 pitch、short/span/加乘边界，最后 Unmap。原 target为 BGRA premultiplied（Rendering.cpp:143–146）；不缩放、不忽略边缘、不把 capacity padding当 viewport，也不按 dirty rect裁成子样本。复制可以隐式等待设备完成，但不增加主动Flush，其成本完全在功能阶段。

**功能payload固定 max32MiB，且 checked计入共同64MiB。** fixed有限对象/两表/sidecars/expected wire/metadata的实际 sizeof总和仍须≤4MiB；raw按两个真实sample stride核。destination、同时存在的 readable copy、checkpoint/job和离线非流式临时payload均按峰值核，不能只数最终BGRA数组或把32MiB直接加在旧64MiB之外。可预留更小有界功能缓冲，完整 frame或实际mapped pitch超过预留/32MiB/共同预算一律 NOT VERIFIED；不承诺巨大桌面，也不降分辨率。该payload预算不是整个D3D/D2D驱动/RSS上限。

raw中的功能/control后段保留并标 afterEnd；统计只纳入真正 measured区间/身份匹配帧，边界重叠 batch也不混入本段功能费用。完整 BGRA字节、实际viewport/source/alpha、最终工具/宽色/fold/panel、源/动作/accepted结局在同表/hash/profile的 on/off配对中核等价。两个fresh child退出后须仍有可比较的全部原字节：建议private leaf中固定CREATE_NEW输出 `equivalence.bgra`（待实施输出，不宣称当前已存在），逐行/流式写原destination前缀，receipt并入既有meta；Root离线逐字节比较。仅PNG转换或hash不足以保留原premultiplied字节，hash只辅助，PNG离线可选。字节不等即保留失败，不能以容差、某像素或hash相同追认。该图证明仪器等价，不升级为光学/latency/GPU度量或其它 family 已正确。

## Root已选定的完整时序与借用寿命

| 顺序 | 必须实际发生 | 对象寿命/失败处理 |
| --- | --- | --- |
| 1 | cap/私有路径/合法 defaults/真正 fonts+I18n+UI图/预算准备，安装 Source unready binding；Display/原WARP Pipeline/仅Bar Window Service启动 | partial init也记录已创建 owner；Service内部失败 rollback借用 owning FailedCleanupSignal，全部test gates空 |
| 2 | A Enable→真实Register/自然Submit/Fit/四API→一次Freeze→acquire bootstrap ready→创建Interact→实际Run发布InteractionReady | 没有MouseHook/Office/update/StartupPreview；不改正式默认Dynamic。fixture explicit true/true profile如实写meta |
| 3 | 同immutable64 hash，setup/16warm/200measured沿固定 due，own HWND index→真实queue/dequeue/Touch Seek/action→accepted/完整目标 | private消息和拒OS输入必须Handled；WM_TOUCH/WM_INPUT先做原必要资源清理；Unavailable不OS cursor fallback |
| 4 | B 精确最后 Completed row锁 MeasuredEnd；本地停新 publication/收完自有contact，随后正式 SetOffSignal(1) | 此处先建原15秒，不能先 blocking join/readback/I/O；必要Cancel只清对应自有contact |
| 5 | Source真正join、Interact真正join；Bar/observer/cap/backing保持活，再由Scheduler PostControl一次 C | 预定Close可读；unexpected early-stop不可读。不能用stopped/running=false代替join |
| 6 | 确认全部 checkpoint task已结束相关借用；SetContentStateUpdatesReady(false) / StopDisplayTracking真正Subscription Reset；StopRendering同步Unregister/Seal/reset | 先完成控制任务，Unregister本身不是其静止证明 |
| 7 | WindowService.StopAndJoin 的overlay和empty-setting两线程都join；Display::Shutdown drain；RenderPipeline::Shutdown 真Scheduler join | 仅一个Bar spec仍建立两Window线程（Window.cpp:185–194/715–716）；Display shutdown是callback drain，不冒称它有独立新线程 |
| 8 | TakeRawCapture一次(on)/off合法absent；Absorb全rows/纯值state；CREATE_NEW私有离线文件→结果/数字统计→最终Sealed | cap/table/probes/span/jobs/COM借用与目录lease保活到真join/进程死亡；on/off timing真实null/整数ticks |

**控制任务有单独的真实边界。** Scheduler::Unregister（RenderPipeline.cpp:936–943）只等 activeCallbacks，不统计 PostControl tasks；Bar.StopRendering（RenderLoop.cpp:943–947）随即 Seal/reset。每run最多一个在途 owning固定 checkpoint job，其 event/span/outcome/借用在 fixture state中保活。任务内部 catch任何异常并给 Failed回执，所有 observer/coordinator/COM读取、复制及lease结算结束后，才发布完成并以SetEvent作最后相关操作；不能依赖Scheduler通用catch吞异常后没人发回执。

若 B/C等待超时而任务仍可能运行，正式 Close原15秒先激活，保留 cap/probe/span/job/coordinator所在对象，继续等真实任务完成或进程自身监督死亡；**不能先Unregister→Seal/reset，再让迟到控制任务使用它们**。只有已证明任务结束才继续正常清理。未证明静止不返回Auth、不detach、不写成功Sealed；父300秒上限cleanup自己的exact child仅记FAIL。无需新增Scheduler fence/stop API。

## 最少文件所有权

| 所有者 | 必需文件/职责 | 不扩大范围 |
| --- | --- | --- |
| Root | Main最早dispatcher/DPI wrapper、Auth helper集成、主工程/filters、Probe.h/.cpp的A/B与C必要serial接口、RenderLoop中的A zoom pending/C成功tuple+读回 | 先收回B3写权/冻结源；复用现Renderer getter、Window/Scheduler/Shutdown；不改Draw3/旧purpose |
| Source writer | 新Bar.Presentation.Test.cpp与Source.h/.cpp；Interaction的index/dequeue/current-consumed/point/action/Clear门；Button真实Draw第一业务写前门 | 提供Auth两个同名普通链接定义；Main.cpp仅Root许可的必需窄转发，不修改其它Bar/B3源 |
| 本 reviewer | 本research报告 | 不改产品/工程/spec/其它agent文件，不回退并行修改 |

Source普通头/global module fragment不得重新声明 named-module Message/Window类型。GetCompiledUi3FixtureSourceV1与RunAuthorizedPresentationFixture必须由真实同Bar runner/compiled表定义；未满足不得登记后用stub凑链接。新的constexpr固定表构造后immutable，LE/FNV按每个逻辑字段及clock period编码，真实due/BaseSerial0/flags/action一并入hash。

## 必要确定性测试与实际验收入口

| 门 | 最小能揭错的用例 | 入口/证据限制 |
| --- | --- | --- |
| A | 真实小屏/高DPI Fit后新configZoom；Fit当帧旧frame.zoom不能freeze，下一真实一致帧才能；失败API/未settled/缺anchor/错DPI/display/半写拒freeze；accepted后/第二次Enable拒；未启bootstrap保持原行为 | strict `InkeysHeadlessTests.exe --no-window`测同生产observer；真实Fit+frame producer结合主产品现offscreen测试/以后owned Bar验证，不复制CalcLayout |
| B | 第一Completed row后同goal另一个成功/ResourceUnverified帧更新ready，copy仍为原attempt/ticks；wrong run/step/source/revision、NoChange引用、缺行、非owner/Seal拒绝；CompletedRevision先发布也不读半行 | 真实observer+Scheduler控制任务；分别核on真实tick、off无clock且null，不能手拼ready模拟完成 |
| C | 真WARP/D2D全BGRA、非零source offset和完整viewport；commit后failed/deferred backing写拒旧帧；same-size recreate/epoch/resize/earlystop拒；预算/stride溢出拒；同表on/off全字节及state等价 | 现 `Inkeys.exe --bar-eraser-offscreen-test`可承载共用readback数值/API反例；它没有owned Bar/真实ULW，不能代替F |
| source/action | immutable64/LE hash/unknown scene/flags/reserved/baseSerial/order/index；Up沿Down；Window latestUp不覆盖Interact Down；short溢出/Unavailable/重复/Enqueue false/Clear dropped；Draw setup跳过/真实Main与wrong action第一写前拒 | 新纯Source校验直接编入现Headless；真实queue/nestedSeek/action由以后同Bar runner验证，不能只测sender |
| lifetime | owning job真实暂停控制任务后等待超时，正式Close在join/I/O前、迟到task仍有活cap/span/probe/coordinator且未Seal/reset；异常回执；各partial-init owner真实join；empty-setting线程不能漏 | 默认门为空；仅已auth的本run受控场景，失败保留/自身监督死亡，不以父kill PASS |

Root完成三组源码/Source/两普通定义与登记，独立 actualdiff safety CLEAR 后，才可使用 Auth helper已经定义但尚未Main接入/未编译的 `--ui3-fixture-auth-tests` 和 `--ui3-presentation-benchmark --scene main-fold|draw-attribute --round 1..3 --capture on|off --capacity <on合法值或off0>`。当前不得运行。完整 `InkeysRepo.sln Debug|ARM64`（原生ARM64 MSBuild、同PowerShell PATH修正、至少5分钟）是类型/链接门；strict no-window/PptCOM/旧purpose回归分别记录。

真实Bar先一个受控scene的off/on两轮，核16+200目标、全部来源/action/资源结局、完整readback与自然alljoin；再同设备/同explicit true/true效果/同Release两scene各fresh三轮。完整原始分母、失败与成功、median/P95、实际commit gap/完成时间与可得CPU/资源同时报告，样本不足1000的P99=null，raw掉样不得冒全程分布。采样不与build/其它bench并行；不能为B3保守未认证追加fullDirty/Request、放宽断言或筛掉unfinished。

**此处没有预设F会成功。** B3对其它shape/text/lighting的Unknown写仍沿现保守判定；A的bootstrap layout/anchor ready不表示SVG已证。216目标必须各自沿现严格CompletedLayoutAndSvg取得完成，不能追认retained、少画/关闭光影或补帧。若真实Main/Draw顺序导致资源未知而完成不达，先保留本轮实际失败/未验证分母；后续由Root基于真实绘制次序另作最小观察资格修补并独立复审，本报告不扩写其它family设计。

## Findings (fixed)

- File：Source设计报告旧191行及本次Root冻结合同。Issue：Source join/readback在正式Close之前，可能先进入阻塞清理。Fix：Root已明确采用上表4–8顺序，正式Close先于join/读回；本 reviewer没有修改原报告或源码，实施者须同步。
- File：Scheduler/Bar清理接口使用合同。Issue：Unregister不能证明PostControl静止。Fix：Root已明确采用owning单job、最后借用结束后回执、timeout保活到完成/监督死亡；不增加Scheduler公开接口。
- File：bootstrap设计的冻结前提。Issue：真实Fit可在Submit改config，而当前frame.zoom仍旧。Fix：本报告明确同帧zoom一致性及auth观察InitialPending/下一自然帧；由Root在A接口实现落实，未修改生产绘制。

## Findings (not fixed)

- A/B/C及完整Source/runner尚未实施，普通链接/三架构布局、私有config实际写入、动作全部拒绝出口、控制任务真寿命/异常/partial-init/alljoin仍须最终实码复审。设计APPROVE不证明类型、线程静止或F运行安全，不能仅凭Sealed/file存在宣称成功。
- B3/H1由既定唯一writer/root独立交付；本报告只依赖其实际producer接口，不批准B3全部性能/覆盖或借其旧绿色关闭F。首版NoRetained/partial Unknown可能使合法自然scene无法完成，保持NOT VERIFIED，不重画造绿。
- cold/init与render-all资源计数要准确命名；未取得warm/measurement的真实owner边界差分则标not collected，不称steady。Win7、光学/GPU/真笔、mouse light/FineDial/Settings、其它scene/family、HC/H2、最终HF/发布仍未验证。

## Verification

- Lint：未运行产品lint；纯设计研究分工。报告已做UTF-8/CRLF/Markdown路径与静态符号/调用链核对。
- TypeCheck / Build：未运行；依赖源码尚缺、Root唯一构建槽，不将静态声明记编译PASS。
- Tests / EXE / GUI：未运行，NOT VERIFIED / NOT RUN-APPROVED；以上为之后的验收门。
- 只读源身份：Probe.h `E2DE55BE…AE570F7`，Probe.cpp `EB99F2A1…2FF4B9`，RenderLoop `AB1E5B8B…89474F`，RenderPipeline.cpp `7FEF2302…9474B83`。这是读取时点证据，不是并行B3/Root后续改动的最终冻结或HF。
