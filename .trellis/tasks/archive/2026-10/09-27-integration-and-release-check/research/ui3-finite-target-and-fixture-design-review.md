# UI3 有限目标、SVG family 与私有fixture独立设计复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。仅审 `ui3-finite-target-and-fixture-contract.md`（355行，SHA-256 `C655D5695AA7C61C8B758497866F79FFC9D5FD8C698F873CE18E5B5A7BB74AC4`）、前次U01–U04报告与实际Bar/Button/State/Config/Window/Rendering调用链。本 reviewer不改产品/工程/spec/账本，不运行Git/build/EXE/GUI，只写本报告。另只读C++标准草案核对publication fence，来源见对应段。

## 当前R2分单元结论

**R2冻结439行、SHA-256 B32BF41F5EF31F9B8DF6B0488D0F8A6074839800A8CF9B1A7C8E7213054D1097：B2、B3首SVG计数/保守paint、F均GREEN_DESIGN。** 见文末R2逐项闭合与执行边界。这里准许设计范围内分批实现，不准许尚不存在的新CLI运行，也不关闭真实场景/性能/发布门。

## 初版分单元结论（355行历史）

- **B2 NEEDS_REVISION**：实际业务接受→UpdateRendering规范化→Notify前封口的形状可保留；atomic payload免除普通struct的data race，但写侧顺序屏障需补齐，并明确rejected/no-change与旧目标关系。修订后可先两个scene的有限数值单元，不建全动画registry。
- **B3计数单元GREEN_DESIGN；SVG paint/retained proof NEEDS_REVISION**：只做SVG family、真实parse/raster/upload/draw/API成功边界及cold/warm归属合理。retained/hidden proof最小字段和clip/opacity/失败写入lineage必须冻结；没有完整证明时保守Unverified，不能称ULW成功等于目标图已画出。
- **F NEEDS_REVISION，无运行许可**：128B/64B、early返回、owned index source和两scene限制方向可用；新目录认证、capture-off就绪/完成、动态anchor serial与setup allowlist需具体冻结。实码独立安全CLEAR和一个scene真生产链通过后才能运行三轮。

B06现已最终复验GREEN，只有software commit和默认sink-off门；这里所有新接口/CLI/DTO都仍不存在。C-P2未由本报告审查，不能使用其组合Build0代替真实清理故障/恢复验收。

## Findings (fixed)

本 reviewer无源码修补。新提案已经明确前次四个核心点：Draw接受在Bar.Button.cpp、publication包住StateUpdate/PresetHoming、payload使用atomic标量、资源ready与真正paint分离。下面继续冻结其实现细节，不重复要求重写生产算法。

## B2：实际业务边界与必要调整

**【实码确认】** Main接受在Interaction3294附近，TryBeginToggle后pulse先增再改fold；Draw真正toggle在Button296–319，不在通用Interaction loop内。通用Draw Up分支先ClosePenTypeMenu→clickFunc→NotifyPptBusinessAction→UpdateRendering，之后还有pressed释放与UpdateRendering(false)。Main.cpp209的UpdateRendering持原短mutex，先StateUpdate/ThicknessDisplayUpdate再Notify/Request；Button839的StateUpdate实际含PresetHoming，1315可能把drawAttribute规范化为false。冻结Mutation从第一业务写前开始，在规范化结束、原Request前封口合理，不能把标记放在callback之后或pulse当版本。

### P1-F01：atomic字段还需可证明的写侧顺序屏障

第3节目前是odd(acq_rel)→atomic字段relaxed→末尾release fence/even，reader两次acquire serial，中间读atomic字段及acquire fence。这消除了普通payload并发读写的UB，但尚未建立“reader读到新字段就必须在复核serial时看到odd或更新版本”的跨对象同步关系。odd RMW的release语义不能代替位于后续payload写之前的release fence。

最小冻结：writer在BeginMutation先发布odd，紧跟release fence，先于第一业务修改及payload store；写字段，最后even release。reader首serial acquire、字段relaxed、acquire fence、复核serial；有限一次读取，奇数/不一致直接未验证。或者选一个有等价C++内存模型证明的固定双槽/非阻塞snapshot方案。不要只依赖当前ARM64/x64一次测试通过。release fence→字段原子写→对应字段读→acquire fence的同步条件来自[C++标准草案 atomics.fences](https://eel.is/c++draft/atomics.fences)；这是本reviewer对所提publication次序的推导，不是已有产品代码故障。

验收：生产publication helper在odd后/业务半写/字段发布/even前分别暂停，render一次TryRead只得完整旧/新或unverified；token/签名不能混绑。实际ARM64压力测试是补充，不能替代正确顺序的静态证明。

### P2-F02：rejected/no-change、初始render写入与字段落点

FinishRejected仅应终结新request的ledger。若无业务写入，不应把仍在进行的旧accepted目标冒称Superseded；即使publication serial变化，也需明确旧候选能否凭同semantic revision+signature确认。目标没有改变且无自然后继帧时，不能靠额外Request补计量。写入后异常/规范化改变可明确Ambiguous/Superseded，保留分母。

“仅Interaction是semantic writer”须限定在已完成初始化的这两个scene：RenderLoop2986初始/Whiteboard/PPT布局可直接fold=false，Main415退出白板可fold=true；本fixture应在初始场景稳定后开始，断言无Whiteboard/PPT/display变化，不能忽略实际render写入。pressed-release的非语义反馈可排除，但SVG颜色/opacity proof仍必须按本帧真实使用值。

DTO位置属于root可自行冻结的常规取舍，建议一个Bar内部finite契约头/同module，必要的纯值导出才放RenderPipeline.cppm；不要让RenderPipeline import Bar或引入全局动画map。把valid mask/位宽/tag范围明确，所有业务snapshot用已存在GetStateModeVersionedSnapshot；用本帧实际目标，不在commit重读最新值。512 records/1024 source/256 tags的总预算需同R数组一起核sizeof。

## B3：SVG producer与paint proof

**计数形状可先GREEN_DESIGN。** CacheBitmap真实parse/raster/upload和成功newBitmap/cW/cColor提交在UI.cpp400–489；Rendering.cpp2723的needUpdate和最终DrawBitmap是lookup/draw边界；ResetCache387与RecreateDeviceResources145/DiscardDeviceDependentCaches171是实际失效。CalcWH/内容transition还有独立parse，初始化scope不能塞到第一render帧。per-object单bitmap没有capacity eviction，应N/A；replacement/invalidate/失败保留单列，不当hit或create成功。默认普通observer=null无新clock/分配，fixture capture-off身份proof与计时时钟开关分离。

### P1-F03：retained最小proof需包含有效绘制状态与实际coverage

现DTO列bitmap value/color/size/epoch、dest/transform，但未明确paint proof的opacity/有效clip与被写过的target lineage。实际Rendering::Svg会以tarPct提交DrawBitmap，外层dirty clip已在RenderLoop9886附近Push；API被调用不代表clip外或部分覆盖区域已按新bitmap改写。相同几何/content/color却从半透明变不透明，不能凭旧paint proof确认全区域；HiddenExpected也不能只看enable=false。

最小修订二选一：

1. 固定每tag paint记录至少带surface/epoch、semantic bitmap proof、dest/有效transform、最终opacity、有效clip或可证明coverage、已成功visible/hidden状态与受后续Clear/写入影响的版本；颜色/content/quality更新失败或范围覆盖不全不能Complete。失败/deferred事务可能已改写GPU backing，及时使被触及的旧proof未知，不能把最后成功窗口像素当作未变target。只有完整事务成功才能提交新的paint proof。
2. 第一批只认证本帧实际有完整coverage的DrawnVerified和实际clear覆盖的HiddenExpected；所有无法证明的retained/部分clip保守Unverified。按数据决定是否补小的retained字段，不强制full dirty/新draw/flush，亦不把成功名单缩小来制造PASS。

本提案已承认无法证明可Unverified，方向正确；需要把上述最小字段/更新点写成可执行合同。bitmap ready只证明缓存对象，不证明本次target像素语义。当前CacheBitmap颜色刷新失败return false，质量刷新失败可继续旧图，整体EndDraw/ULW仍可成功；必须继续保留Missing/QualityFallback与未完成分母。

BindObservationTag还应明确首次owned init值如何建立初始valueRevision（绑定发生在InitializeUI之后），tag命名空间避免svgMap/按钮重号，首render cache如何从Known producer得semanticKnown。没有观察到的旧cache不得追认；32/64bit浮点bits和计数乘法保留finite/溢出检查。普通产品不启marker/新ledger，不因为scope类型存在就复制缓存。

验收：真Svg/CacheBitmap首建、复用、颜色/content/size/epoch变化，parse/raster/upload失败；成功ULW但missing图不complete；opacity变更、部分clip、failed/deferred写后retained、完整隐藏清理和256cap。实际BGRA仅独立等价回读，软件proof不冒称光学可见。

## F：auth/source/bootstrap与剩余冻结

### P1-F04：提案目录不能直接调用旧IsTestDirectory

128B align8/offset64和64B input offset32/40/48/56的布局计算一致；argc8、独立magic/purpose/nonce、三继承HANDLE、exact PID/file identity、只走early child返回的形状可用。然而旧ShutdownSupervisor::IsTestDirectory只接受既有Shutdown/UEF命名的单层目录，提案 `ui3-finite-*/rN/s<scene>`不满足它。直接复用会拒绝所有合法fixture。

最小修订：为新purpose定义独立owned-root验证，核仓根标记、固定新prefix/round/scene层级、每层非reparse、leaf/bin/EXE和文件身份；保留旧C/E验证原义。公开CLI无任意路径，parent只在新唯一目录create-new，不放宽成任意同前缀路径。实际code后再做运行前CLEAR与坏header/parent/root/index负例。

### P1-F05：capture-off必须有独立就绪/完成证明

当前B06在cap0不会产hasBarCommitStamp或barCommitTicks；第7节“等待首次B1实际commit”及CompleteAttempt真tick参数不能照此阻塞off轮，也不能临时打开raw或用clock参数绕gate。原InteractionReady的Startup::Report也需核是否在无StartupPreview/正常startup plan时真的可观察。

最小冻结：在真实Interaction Run入口与Bar实际CompleteAttempt旁挂私有有限ready/committed identity latch，off时同样确认布局/SVG/事务结果，但timingValid=false、时间导出null/unavailable，不填0ms完成。on轮用B1真实tick；phase/source schedule自己的必要时钟与B1细阶段分开。这个probe只auth fixture存在，退出后真join才撤，普通产品不加读钟。

### P2-F06：immutable source serial/setup action与确认顺序

expectedBaseCommitSerial不能编译期预测实际Bar successSerial。冻结0表示“owner接收时绑定当前真实committed anchor”并将实际serial写独立sidecar，或改为明确逻辑previous-step标识；不把可变anchor写回immutable table。Up继续用该contact的Down屏幕点，sourceSequence/consume ack一次；Clear删除、Unavailable、过期anchor都保留失败，不重发掩盖。

DrawAttribute需要setup Main真实tap展开，但当前文字“Draw scene只允许Draw preset”需为编译内setup token明确允许MainGrip动作，和measured的Draw action分开；不能让setup阶段绕掉action gate。错误命中必须在外部callback之前拒绝，包括真正通用clickFunc和Main支路；不直接设置fold/tool目标取快结果。

现proposal对原Wait/TryGet/nestedSeek的screen marker路径、current consumed与Window snapshot区分、Main final veto/hover/取消GetCursorPos seam、NotInstalled走OS/Unavailable不fallback，都对应真实代码。其它FineDial/color/RawInput/焦点/Settings仍Unsupported/未覆盖；Touch本来不激活mouse light，不能伪造registered=true或关闭primary/dynamic效果来报完整场景通过。

### 合法Pen baseline与生命周期

实际IdtState.cpp977的ChangeStateModeToPen先按原mode mutex改状态，SyncDraw3State685还会ApplySettingOwnerDesiredState、PublishDraw3State、ReconcileDraw3Presentation；后者在ProductRunning=false时立即Waiting。当前这条入口没有看到自启/Office/配置写盘/新业务线程，但Setting owner命令和bridge发布真实存在，需固定bootstrap调用时点/seed并验证只有自有role，保持NotReady guard，不能为fixture启动Host或绕产品gate。

Config::GetFilePath实际globalPath+Inkeys/Config/main.json，Load可Write；globalPath尾分隔/private metadata/opt路径必须先建立并核，再构图。同Bar module调用生产init子函数可行，完整Initialization171启动MouseHook，所以必须持续跳过它。固定字体/I18n/default图和图标不得省略；源table/授权在Window callbacks可能使用前建立，真实Bar.Render/Interact通过后才发动作。

stop必须source停→对应Cancel被真consume→正式Close原15s→Interact join→Display stop→同步Unregister→Window/RenderPipeline真join→Take/数字封口→撤probe/table，不以offSignal/running=false代替join。failed startup也有有界own-child清理，parent强殺不算PASS。PNG/BGRA回读与离线报告在结束后，不能每帧侵入计时。

## 最小实施顺序与验收

1. 修F01并冻结B2纯值内部类型、rejected语义、两个真实writer边界；共用production helper红绿和半写/反向/normalization测试。只两个scene，未开放gesture不计完整。
2. SVG首family先计数/实际ready，再按F03选择完整有限paint字段或保守Unverified；用真实DrawBitmap/clip/clear/Complete路径故障与成功反例，默认off实际路径需回归。
3. 冻结F04–F06、private seed/合法Pen/初始tag，先授权负例和一个真实hiddenBar scene实码独立safety CLEAR。不是本design直接许可运行。
4. 新完整InkeysRepo.sln Debug|ARM64、strict no-window和真实HWND分别记录；之后同Release/设备/配置两个scenefresh三轮，16warm+200测量，count/drop/未完成与分位数同时输出，不足1000的P99 null。cap合并预算<=64MiB，不等价或污染不报PASS。
5. B1成功软件戳、有限LayoutAndSvgProof与其余performance/真实输入/光学/鼠标光/FineDial/Settings/Win7/HC-H2各自保持边界，不挑最好轮或自动关整个E04/发布门。

## Verification

Lint/TypeCheck/Build/Tests/GUI：本reviewer未执行，全部新finite接口仅设计；没有动态GREEN。已核冻结SHA、实际业务/资源/source/配置路径及所列最小技术取舍。C++内存序只读标准工作草案后作明确推导；没有新增依赖、修改业务算法或无限扩框架。

## 2026-09-30 R2 frozen独立增量复审

本 reviewer完整核对R2的R1–R8关键新增条款和对应已读实际调用链，文件SHA与root指定一致。只更新本报告，没有改源/spec/项目/账本、没有构建/运行/Git或递归派发。

### Findings (fixed)：设计缺口已闭合

| 修订 / 前次发现 | R2真实合同闭合点与实现必须保持的边界 |
| --- | --- |
| R1 / F01 | BeginMutation在odd fetch_add后紧跟release fence，先于第一业务写与atomic payload写；字段relaxed、even release，reader字段后acquire fence再复核，仅一次不忙等。不能把末尾fence或压力测试替代前置顺序。标准依据沿前次引用，未引入新同步框架。 |
| R2 / F02 | request ledger与最后accepted semantic goal分开。NoBusinessWrite拒绝只结束自己；guard默认Unknown，半写/未知出口不能追认，旧pending goal不被纯拒绝冒称Superseded。AcceptedNoChange引用旧pending/committed分别记录，无0ms假完成。测量phase在真实initialLayoutStable后开始，初始render fold/PPT/Whiteboard/display写未跟踪则拒确认。side改为render派生候选，不能因为正常自动居中误判用户新request。 |
| R3 / F03 | 首版明确不产RetainedVerified。新增final opacity/window alpha、真正effective clip、expected visible bounds、coverage、bufferMutation/targetInvalidation serial；任何BeginDraw/Clear/Draw可能写backing前使旧proof未知，failed/deferred也不恢复旧成功像素假设。只有本candidate完整coverage实际Draw或真实Clear覆盖旧bounds、之后无未知覆盖、全部事务成功才认证；partial/未重画/未知clip/旧hidden一律Unverified。无强制dirty/draw/flush，保留全部required/failure分母。 |
| R4 / F04 | 新purpose专用validator逐组件核repo/TestResults/release-hardening/master32hex/rN/sScene/bin与seed/output，所有regular/nonreparse、separator-aware边界。源EXE必须在同repo且与继承parent真实image文件身份相同；parent源/leaf双方root证明保持必要目录/file HANDLE。旧C/E IsTestDirectory不调用、不放宽。128B/3HANDLE不再为共享root扩大协议。 |
| R5 / F05 | 私有ready/latch来自真实Register、Interaction Run、完整CompleteAttempt和Submit/Advance initialStable；不依赖无startuptracker的Report。captureoff无raw/stamp/计量clock，但仍有纯committed identity/anchor/layout/SVG证明，time null/timingValid=false。on沿B1真实戳，不另造API时间，不用clock参数绕gate。 |
| R6 / F06 | immutable baseSerial首版强制0，Window owner一次读稳定committed latch并把实际anchor mapping/epoch/serial绑定sidecar；Up沿Down，不修改table或用最新几何倒填。Setup/Warmup/Measurement/Teardown及expected action位已冻结。Draw Setup只有预留Main tap；已展开两行SkippedAlreadyOpen，计数明确；Warm/Measure仅Draw，未消费/保留Cancel不伪造成功输入。 |
| R7 | 内部Bar.PresentationProbe.h/同module小helper落点已冻结，不让RenderPipeline import Bar/业务setter。validMask0xFF、flags/数值范围与field owner明确；tag namespace分svgMap/button，绑定只在known-owned初始化无bitmap时建立revision1，不追认未知cache，256上限映射不是直接拿大tag索引。总sizeof/checked乘法/溢出与default-null/no-clock有执行门。 |
| R8 | 源停止、合法Cancel实际consume、原Close监督、Interact/Display/同步Unregister/Window/Scheduler真join后Take/封口/撤表。off的raw absent合法；不以offSignal/running=false撤背板。私有parent超时只清own HANDLE并FAIL，sentinel/软件事务不冒UInk、光学、Win7或整体UI验证。 |

### 最终分单元判决

**B2 GREEN_DESIGN。** root可先派有限数值publication/helper与真正Main/Draw接受入口的RED；必须包括Bar.Button.cpp实际TryBeginToggle、Interaction第一业务写、Main.UpdateRendering规范化后Request前封口。初版陈述的主要问题已由R2实质修正；没有必要再问用户DTO位置或再设计全animation registry。

**B3首SVG计数与首版保守paint GREEN_DESIGN。** 这批准的是固定family、实际CacheBitmap/DrawBitmap/clip/clear/Complete的观测，以及NoRetainedVerified策略。它可能使正常partial/保留帧得到ResourceUnverified，或没有自然后继帧而timeout；这是准确限制，不允许额外请求/强制full dirty来换PASS。若未来要接受retained，须另冻并审完整lineage；本GREEN不会隐式批准。

**F GREEN_DESIGN，无运行许可。** 128B独立purpose、64B immutable table、owned source、new-tree validator、privateconfig/bootstrap、独立on/off latch和stop已经有足够有限接口。合法Pen调用仍走原模式mutex→Setting owner/bridge→ProductRunning=false的Reconcile早退；实码必须核仅自有/不存在role、配置路径和NotReady guard，无Host/Office/MouseHook/系统业务启动。MainFold/DrawAttribute以外仍未覆盖；新CLI/auth-suite当前不存在，不能运行或记录PASS。

### Findings (not fixed)：后续实码/数据门，不再是R2设计阻断

- **P2，实现与所有权**：源/头/module include、原业务和GetStateModeVersionedSnapshot调用次序、atomic字段/fence、unknown witness、fixed容量与default-null成本都要看实际diff；当前只是合同，没有实现/构建结果。root只先做B2 RED，B3/runner不能借此并发扩写。
- **P2，paint覆盖与数据可得性**：full coverage/opacity/actual effective clip必须来自原绘制调用，不能用dirty矩形替代未知clip，也不能把cacheReady或Draw API次数直接认证。NoRetained/未知情况可能使两scene不能取得完整性能分布，仍报未验证/保留分母；不因设计GREEN承诺三轮会成功。
- **P2，来源与lifetime**：source Window snapshot与Interaction当前consumed TLS不能混用；Unavailable不得fallback OS。普通OS/RawInput/其它action污染必须在执行外部业务前拒绝并计账，index/phase/bitmap/tag/packet/root负例和实码运行前独立安全CLEAR仍必需。parent必要HANDLE在child死亡/核结果前保活，精确owned文件清理先结束相应lease，不清未知文件。
- **P2，范围**：鼠标光、FineDial/颜色drag、Settings竞争、其它resource family、实际笔/光学/GPU/HC-H2/Win7都仍职责未完成；B1/C-P2其它组合门与Draw3U2 RED不由本设计改写。整个task/E04不标PASS或complete。

### 最小可执行交付门

1. B2先只生产数值helper/接受入口RED，利用现strict no-window入口直接测试生产publication：odd/fence/业务/payload/even暂停、rejected保旧goal、no-change pending/committed、missing mask/来源变化/unknown写、真实Draw callback与PresetHoming封口。先查RED实际specific failures再GREEN，不复制状态算法。
2. B2候选/finite role接线另看实际Submit/Advance与最后失败→成功/反向/restart/idle/epoch，不能仅单helper通过就确认真实动画完成；不加采样唤醒。
3. B3之后以真实Svg/CacheBitmap/Clear/PushClip/DrawBitmap/Complete共用入口测first/reuse/color/size/epoch/失败/full-partial-empty clip/opacity/hiddenclear/backing失效；BGRA只独立等价，不污染计时。
4. F最后做新树正例/逐层负例、packet/purpose/parent/index/action/phase/anchor/read来源/无tracker cap0就绪与真join；实际diff另发运行前CLEAR。先一个owned hiddenBar scene，再同Release/设备/效果两scene各三轮，保留全部原始/成功与失败分母/P99不足null，不挑最好轮。

本次Lint/TypeCheck/Build/Tests/GUI全部未运行，仅R2设计GREEN与边界核对。没有新增用户权限门、架构框架或产品改动；运行/性能结论仍以之后实际冻结源和证据为准。
