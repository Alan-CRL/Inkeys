# UI3 B2-P1 finite publication implementation

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。Writer：`ui3_bar_commit_impl`。当前 **B2-P1 GREEN PATCH_READY，动态绿验待root**。root独占工程登记、构建/运行/GUI；本worker未运行任何EXE、未改工程/spec/父账本或其他作者文件。

## GREEN行为（当前优先）

numeric算法已填入同一生产helper：

- Begin单一Interaction owner登记request seen/512prefix/dropped，odd fetch_add(acq_rel)后立即执行前置release fence，再返回Mutation让实际业务写入。payload为17个Accepted atomic word和1个known位；写入relaxed→even release。TryRead一次首serial acquire→全部atomic字段copy→acquire fence→复核；只完整同一even且known/identity/signature有效才返回，不忙等、不读普通writer payload。测试六停点沿同一实际方法，不再是桩marker。
- request ledger独立于最后semantic goal。显式NoBusinessWrite且未记录实际write只终结本request，旧goal step/revision/signature保持，仅publicationSerial更新；StillSame忽略该更新及合法派生side。真正变化递增semantic revision，前pending goal/其引用记Superseded；Unknown/WriteOccurred拒绝使current goal失证、旧pending记ResourceUnverified，新Unknown request记Ambiguous。重入保护不偷走外层odd，外层不得追认。
- no-change不换旧goal identity/revision；ledger新request为AcceptedNoChange，分别引用pending/completed receipt，timingValid=false/finalCommitTicks不伪造。NoteCompletedGoal只在一次稳定revision匹配时发布atomic receipt，不并发写Interaction的普通ledger；owner随后吸收receipt。这是为未来P2留的numeric完成交接，本P1仍没有真实Submit/Advance/SVG/ULW完成producer。尚未由owner吸收的最后receipt不应把P1直接span当成完整最终动画报告。
- 0xFF mask、flags范围、Pen state/submode、RGB、side/view/theme、非0toolRevision/dpi/even displaySerial、finite正width/zoom均校验。两scene冻结tool/width/color/version/view/theme/dpi/display/zoom；side仅snapshot信息，不作为新semantic goal。其他浮层/gesture/不自洽fold+Draw/Draw时仍fold等拒确认，不以0或坏mask补成功。ledger保留原输入坏值及独立invalid分母；schema/source证明仍由后续P2/F提供。
- 出口时钟只沿原已显式安装且clocksEnabled的owner context，defaultnull仍无clock/heap/request。no-change不当0ms完成，坏时间只记录invalid/timing不可用，不clamp。既有Main/Draw实际Mark/Write/规范化后原Wake/Request封口顺序保留。
- 固定512前缀不扩容、不覆盖，满后seen/drop仍累计；serial/revision数值耗尽拒证明而不绕回0。getter仍仅all owners stopped后读；没有live导出/新线程/文件I/O/logger或GPU操作。

本轮GREEN只改Probe.h私有实现声明、Probe.cpp算法及Bar.Main.cpp新增调用的限定名。测试文件逐字等于RED，不改变B201–B209期望或旧B1/B06/R/216；Main.cppm/Interaction/Button与RED相同，RenderLoop/B3/F/工程/其他作者源码未触。

## 真实RED与编译首因（已只读核）

- root已登记normal Probe.cpp主+Headless及header；本worker未改工程。`ui3-b2-p1-red-debug-arm64-headless.status.txt`自然exit1、PID29248；stderr准确B201–B209/FAILED count=9，B210及旧测试无新增失败，stdout末尾`[EraserAttribute] layouts=216 failures=0`。这些是运行共用桩的真实红证据。
- 完整Solution首次被其他writer的C3 AutoSave pointer类型阻断，root独立一行修复。第二完整Build `c3a-u2green-b2red-compilefix-debug-arm64-build.status.txt`仍exit1；log327/567唯一源码error为本新Main:285的WhiteboardActive C2668重载歧义。**完整RED Solution没有PASS**，不是环境/工具链错误，不用Headless可运行替代它。
- 本worker按真实批准来源最小改为 `Inkeys::UI::Bar::WhiteboardActive()`，对应本module的whiteboardActive acquire getter，Ppt函数也明确限定同namespace；未改IdtState全局函数、原Bar内部其他同名调用、C3类型或工具链。GREEN尚未构建，不能提前写该编译首错已被动态验证关闭。

root依据真实9项RED授权GREEN_IMPLEMENT。此后本worker仅做自有静态diffcheck exit0、严格编码/BOM/CRLF与测试字节一致检查，没有build/run/GUI。root须冻结全部并行writer后新完整Debug|ARM64→strictHeadless/U2parked→独立actual-code review；不复用此前部分Build或桩PE的PASS。

## GREEN源码冻结

原RED字节在忽略目录 `TestResults/release-hardening/ui3-b2-p1-red-source/`。新增普通头/cpp无BOM、全部CRLF；Main.cppm/Interaction原BOM保留。当前SHA256：

| 文件 | SHA256 |
| --- | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.h` | `dd09d8f2bc57a33b1ad6aba7aebef05efb1a4c80e3288d986e46b7e9dc2f4b0b` |
| `Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.cpp` | `058edffc72150aa41f0eecfee6377d1f16581294da83189295c18ad25291e5e3` |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm` | `bbf772378f2daeefaf9fb0d50f7db8014cb104522315c3a6d100f32377bc5b25` |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` | `be5609dbedc5345c1a59252c1cf81a1b11073af33ef1871c62bc99aea3d3a6c1` |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | `3af13e04c639cf225d67683293752c079874c960631ffd20e687a614a2eed82d` |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | `ac5654997414bbc9d9c6e21116f0b5bd4f5415aa0eca2fb2205abd40c7ed4f47` |
| `InkeysHeadlessTests/render_scheduler_tests.cpp` | `9bb57d27ed46baaf4e8c262a4fc11e9ea20302970d49b9b623ea6da45f6cbf14` |

## 冻结来源与范围

已读actual AGENTS、当前implement.jsonl/PRD/design/implement、父handoff顶部、439行R2合同 `ui3-finite-target-and-fixture-contract.md`（SHA256 `B32BF41F5EF31F9B8DF6B0488D0F8A6074839800A8CF9B1A7C8E7213054D1097`）及 `ui3-finite-target-and-fixture-design-review.md` 最终R2 GREEN_DESIGN；沿既有native-desktop cpp/rendering/build/quality/reuse指导。本批只有B2-P1；设计GREEN不是执行PASS。

源码唯一写入范围为新Probe.h/.cpp、Bar.Main.cppm/.cpp、Bar.Interaction.cpp、Bar.Button.cpp、render_scheduler_tests.cpp。没有触RenderLoop/RenderPipeline/B1/B06、SVG/UI/Rendering、fixture/新CLI/sourcepointer、wWinMain/Host/Shutdown/工程。P2的Submit/Advance/settled未接；数字completed receipt测试也不代表真实layout/SVG/ULW完成。

## RED符号与DTO（历史冻结）

- normal `Bar.PresentationProbe.h` 定义R2有限scene/status/witness、72B Signature、136B Accepted、Candidate、TargetRecord、数字Counters、固定512 ledger。所有记录无COM/HWND/业务指针/字符串；不二进制dump padding。Mutation和内部owner context只持非owning当前publication指针，不进入raw DTO。
- `Ui3FinitePublication::{BeginMutation,MarkBusinessAccepted,ObserveBusinessWrite,FinishAtRenderRequest,FinishRejected,TryReadStable,StillSameSemanticGoal,NoteCompletedGoal}` 是未来实际producer与无窗口测试共用函数。RED只给数字ticket/实际接缝，TryRead/比较/receipt/validation留明确可编译桩；不在测试复制正确状态算法，不用assert(false)。request ledger和最后semantic goal有分开的固定存储；payload为atomic uint64 word数组，未并发写普通payload。前置release fence及完整算法等待root真实RED后GREEN。
- 新有限hook有AfterOddBeforeFence/AfterFence/AfterBusinessWrite/AfterPayloadHalf/BeforeEven/BeforeRenderRequest；只显式测试安装固定POD函数指针，默认空。**RED站点只是桩marker，尚不声称已有odd/fence/even发布证明**；GREEN才落实R2顺序。所有测试pause的超时路径先Resume再join。
- `Ui3FiniteMutationScope` + TLS `SetUi3FiniteOwnerContext/CurrentUi3FiniteMutation/MarkCurrent.../RejectCurrent.../FinishCurrent...` 为两真实接受点接缝。普通产品没有安装，空observer/context立即no-op，不读钟、不分配、不新增请求。测试clock参数不授权不存在的publication。只在明确owner context的clocksEnabled时可读一次接受时钟；本阶段没有新增CLI/fixture安装。
- `completedRevision_`使用atomic，为未来render receipt与Interaction no-change读取预留安全数值交接；本P1 receipt算法仍stub，未来真实owner/完成proof由P2接入。所有records/counters离线getter要求owner已停止，不导出live mutable数组。
- normal Probe.h分别在Bar.Main.cppm/Main.cpp/Interaction.cpp/Button.cpp的global module fragment include；Probe.cpp是普通C++编译单元，不import任何Bar/RenderPipeline模块，保持单一定义/ODR。root需要将该.cpp在主/Headless工程各登记一次；当前本worker未登记，不能直接运行未链接helper的测试。

## 实际写入接缝及原顺序

1. Interaction Main实际Seek.allowClick后、TryBeginToggle前构造MainFold Mutation；成功后Mark，再原pulse写，随后业务半写checkpoint；原fold/居中/浮层与UpdateRendering逐句保留。Try拒绝明确NoBusinessWrite只终结该request；scope未知退出使用Unknown。
2. Draw在明确Up且temp.preset==Draw时、原ClosePenTypeMenu前构造DrawAttribute Mutation。原ClosePenTypeMenu/真实clickFunc/PPT焦点通知/UpdateRendering/pressed释放仍原顺序。源码987处ClosePenTypeMenu无条件写多个状态及tooltip，所以随后reject不能冒称NoBusinessWrite，记录WriteOccurred，保守拒旧goal确认。
3. Button Draw真正TryBeginToggle成功之后、原ResolveBarDrawButtonToggleDecision前Mark。非Pen路径标UnsupportedState但原ChangeStateModeToPen和原业务写保留；toggle拒绝仅记录拒绝原因，待原UpdateRendering规范化结束封口，没有提前发布一个旧token配新状态。
4. Main.UpdateRendering的StateUpdate/ThicknessDisplayUpdate之后、原Notify/Request前，在有activeMutation时锁存最终signature并Finish；updateState=false也走相同封口。没有activeMutation时不读任何新业务字段/版本/时钟。宽/色/toolRevision来自同一次GetStateModeVersionedSnapshot；side只是snapshot信息，SameSemanticGoal将排除正常render派生换边；flags/tooltip/menu/slider/fine等生成auxClosed。
5. Display仅一次try_lock既有pendingDisplayPublishMutex，读非0偶数serial/dpi；实际初始化两个publisher也持同一个mutex。锁忙/未知时不填validMask，不忙等不补0。theme/configZoom用实际BarStyle，finite条件决定mask；PPT/Whiteboard使mask缺失以拒确认。GREEN校验完整0xFF/finite/冻结tool与environment、Unknown/半写写入和no-change语义。

## strict no-window RED断言（期望未修改）

| ID | 共用生产helper的断言 |
| --- | --- |
| B201 | 完整accepted identity/revision/signature/even serial与ledger row |
| B202 | caller规范化后的最终signature及新semantic revision；不复制PresetHoming算法 |
| B203 | NoBusinessWrite拒绝只结束自身，新publication serial不使旧pending goal Superseded |
| B204 | Unknown/partial出口使旧goal无法确认并保留ambiguous分母 |
| B205 | no-change引用pending/已completed分别记标志，无新commit/0ms时间伪造 |
| B206 | 六实际helper停点：odd/fence/业务/payload/even前只unverified，Finish后原Request前完整新态；全部暂停放行后join |
| B207 | 515请求时512前缀保留、3 dropped、seen=retained+dropped |
| B208 | required mask、tool版本、finite width/zoom、flags/枚举范围、dpi/display；合法派生side不改semantic goal |
| B209 | 缺mask或冻结width/color/toolRevision/display/dpi变化不确认且请求保留 |
| B210 | default无owner/null publication不读clock、不消费request，预计RED也通过 |

预计本批RED准确失败B201–B209九项、B210负向通过；B1/B06/R旧测试主体逐字保留。实际失败数/首因以root完整Solution及strictHeadless自然退出原始证据为准。测试fixture只有已知数字输入及production helper调用，没有自建publication正确算法，也没有真实Main/Draw GUI执行证据。

## 静态检查、预算和源码身份

本worker执行自有tracked文件git diff --check exit0；新旧源码严格UTF-8/CRLF、Main.cppm/Interaction原BOM保留、其余无BOM。原全部B1/B06/R测试主体逐字存在；原216布局测试文件未触。新头static_assert POD/72B/136B、publication<=256KiB，headless同时断言默认32768 R payload+publication<=64MiB；这些类型/预算仍需root实际编译确认。fixed数组仅显式构造publication时存在，普通产品不创建对象；source/256SVG/future总预算不在本P1已实现范围。

原始自有源备份：忽略目录 `TestResults/release-hardening/ui3-b2-p1-source-baseline/`。历史RED SHA256：

| 文件 | SHA256 |
| --- | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.h` | `2f68f2bb097a3576cacc03fd2259767d64adecccc39ee994e7176daac2b36b5b` |
| `Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.cpp` | `9135020d5bc39256a0d7799d8946aada139c55b4b30e2f9fbdf5c6101a3fc559` |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm` | `bbf772378f2daeefaf9fb0d50f7db8014cb104522315c3a6d100f32377bc5b25` |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` | `91fa387627d3fc0bd9c2b0703e705bbc393b3c7f9b2248b7664ee0ac9fba7073` |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | `3af13e04c639cf225d67683293752c079874c960631ffd20e687a614a2eed82d` |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | `ac5654997414bbc9d9c6e21116f0b5bd4f5415aa0eca2fb2205abd40c7ed4f47` |
| `InkeysHeadlessTests/render_scheduler_tests.cpp` | `9bb57d27ed46baaf4e8c262a4fc11e9ea20302970d49b9b623ea6da45f6cbf14` |

## 验证与限制

Build/TypeCheck/Tests/EXE/GUI：本worker未执行，root唯一构建槽。root已经登记normal helper、取得真实9项RED并授权当前GREEN；后续等所有writer冻结，新完整Solution/strictHeadless/U2parked/独立review。绿色尚未运行，不是本轮动态PASS。

真实RenderLoop Submit/Advance/最后settled→失败→成功、角色/epoch/idle/布局批次/资源证明、SVG/B3、独立ready latch、F auth/source/bootstrap/join、真实两scene/像素等价/三轮Release/GPU/线程CPU/HC-H2/Win7仍未实施或未验证。默认产品业务与请求顺序只凭本轮静态diff保持，实际编译/原回归和独立review待root。若未来需要RenderLoop具体接入点，必须按P2另获所有权，不能把P1 helper通过说成目标完成已接。
