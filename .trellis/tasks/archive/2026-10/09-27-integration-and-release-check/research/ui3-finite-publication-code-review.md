# UI3 B2-P1 有限publication实际代码独立复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。本reviewer只读actual check.jsonl与适用native-desktop cpp/render/diagnostics约束、R2 frozen合同/独立设计报告、冻结七源/原baseline和RED source、实际接受入口与测试。没有改产品/项目/spec/账本、没有build/EXE/GUI/Git/递归。只写本报告。

## 结论

**B2-P1实际代码GREEN；当前冻结七源完整Debug ARM64及strict Headless数值回归已由root PASS，实际status见文末增量。** 当前没有确认的P1阻断。这个结论只覆盖数值publication与Main/Draw接受接缝；Submit/Advance/settled、SVG、fixture/source/ready/stop及实际完成/帧性能均未接，不升级。

| 冻结source | 本reviewer核SHA-256 |
| --- | --- |
| `Bar.PresentationProbe.h` | `DD09D8F2BC57A33B1AD6ABA7AEBEF05EFB1A4C80E3288D986E46B7E9DC2F4B0B` |
| `Bar.PresentationProbe.cpp` | `058EDFFC72150AA41F0EECFEE6377D1F16581294DA83189295C18AD25291E5E3` |
| `Bar.Main.cppm` | `BBF772378F2DAEEFAF9FB0D50F7DB8014CB104522315C3A6D100F32377BC5B25` |
| `Bar.Main.cpp` | `BE5609DBEDC5345C1A59252C1CF81A1B11073AF33EF1871C62BC99AEA3D3A6C1` |
| `Bar.Interaction.cpp` | `3AF13E04C639CF225D67683293752C079874C960631FFD20E687A614A2EED82D` |
| `Bar.Button.cpp` | `AC5654997414BBC9D9C6E21116F0B5BD4F5415AA0ECA2FB2205ABD40C7ED4F47` |
| `render_scheduler_tests.cpp` | `9BB57D27ED46BAAF4E8C262A4FC11E9EA20302970D49B9B623EA6DA45F6CBF14` |

七hash匹配implementation；R2合同439行SHA `B32BF41F5EF31F9B8DF6B0488D0F8A6074839800A8CF9B1A7C8E7213054D1097`已核。没有以实现者说明代替实际源码/入口检查。

## Findings (fixed)

- **本reviewer无产品修补。** 新Main的WhiteboardActive歧义由writer最小限定 `Inkeys::UI::Bar::WhiteboardActive()`，Ppt同样限定实际Bar getter；当前完整Solution0支持这一编译首错关闭。首轮C3 pointer错误及其修补在其它owner范围，没有修改它们或工具链。
- 原B1/B06/R `RunRenderSchedulerTests`主体通过baseline起点至原return前的完整文本prefix逐字核一致；只新增finite helpers/尾B201–B210。Main全局barUISet之后的suffix也逐字相同。实际Interaction/Button插点没有移原Seek、clickFunc、状态修改或UpdateRendering；没有为了指标改widget算法。

## 实际数值helper核对

### normal头、布局、atomic snapshot与ODR

Probe.h在四Bar module单元global fragment包含；Probe.cpp是普通C++单元，只include该header，不import Bar/RenderPipeline或IdtMain。没有header定义全局mutable状态/函数体复制；TLS和函数定义只在普通.cpp一次。工程登记由root，主/Headless各自链接同一实现，完整Solution是实际类型/链接门。

Accepted是136B、Signature72B；当前组成5个uint64、两个32bit enum、两个int64、72B signature，字段排列没有atomic word中的padding缝，且全部成员default初始化。PublishAndClose以bit_cast为17个uint64 atomic word，再加known word；没有从mutable普通payload被另一owner读取，也没有导出C++ padding/地址。Candidate/TargetRecord的bool/padding不参与这个word payload或二进制IPC。

Begin先登记bounded ledger，然后odd fetch_add(acq_rel)→实际前置release fence，才返回caller允许业务写；Publish所有word relaxed后even release。TryRead仅一次首serial acquire→全部atomic word/known→acquire fence→复核even，同serial+known+完整identity/signature才返回。六个停点沿这些真实函数，default hook为空。未有自旋等待writer、I/O、GPU或业务锁；跨状态耗尽不绕回zero作有效目标。

### ledger、goal与完成receipt

Begin每request一次seen，固定512 prefix、retained/dropped分开；满后仍发布语义目标但不扩容/覆盖prefix。记录与goal独立，caller source identity不被每callback冒充。无business write的显式NoBusinessWrite拒绝只终结当前row、旧goal保留，serial更新也可StillSameSemanticGoal确认旧revision；未知/WriteOccurred拒绝使goal失证，旧pending记ResourceUnverified并保留ambiguous分母。

新目标真正改变时递增semantic revision并将旧pending/same-revision引用标Superseded。no-change不替换旧goal的step/revision/source，另row AcceptedNoChange/reusedRevision分别引用pending/已完成；没有新truecommit或0ms timing。derived side在SameSemantic比较中排除，未来candidate另按真实render目标验证。

NoteCompletedGoal只一次TryReadStable与atomic completedRevision CAS，不并发改plain goal/records/counters。ReconcilePreviousGoal由Interaction writer吸收receipt后更新ledger。当前completion函数只有数值测试调用，没有真实完成producer；single Interaction writer与停后getter是明确接口合同，不能多线程Begin/Finish或live读span。

### validator与真实来源

IsSignatureValid核0xFF mask、旗标范围、实际Pen状态数值1、soft/hard/highlight子枚举0..2、RGB24、side/theme0..1、thickness enum0..2、finite正width/zoom、非0dpi、有效工具/环境版本。FiniteSceneStateSupported再限定本batch Preview、aux闭合、其它panel/gesture关、fold与Draw自洽；变化的tool/width/color/theme/dpi/display/zoom与initialStable匹配，坏值保留row/invalid，不clamp。

Main真实signature来自同一次GetStateModeVersionedSnapshot的state/width/color/revision。side由Bar现值作快照，工具有效位仅真实Pen非Laser且width有效；display通过Bar.pendingDisplayPublishMutex单次try-lock，只读其pendingDisplaySerial的完整even以及dpi，忙/未知不补mask。**这不是Display模块全局snapshot serial**；Initialization60的Bar publisher实际fetch_add两次，正常serial2/4...，因此even约束有真实来源。

configZoom来自BarStyle字段；实际Zoom::Initialize/Normalize/Round保持0.5..2.0有限正值，default1.0，与validator相容。工具原始revision初始化0（IdtState37），0不是被getter伪造的缺来源；当前R2 fixture要求先合法ChangeStateModeToPen，ApplyPenModeTransitionLocked197递增，因而本窄baseline为>=1。不能在后续P2/F把真实raw0硬改成1装provenance，详见P2门。

## 生产接受接缝核对

- Main在Seek.allowClick之后、TryBeginToggle和第一pulse之前构造Mutation；真实toggle成功才Mark，然后原pulse/ObserveBusinessWrite与fold/中心/浮层写。原UpdateRendering仍调用；toggle拒绝在第一写前明确NoBusinessWrite。scope未知异常出口默认Unknown，不能数callback当accepted。
- Draw在明确Up且preset==Draw时、ClosePenTypeMenu前构造Mutation；原ClosePenTypeMenu实际无条件写多个menu/tooltip状态，因此观察WriteOccurred。真正Button Draw callback的TryBeginToggle成功才Mark；非Pen→Pen不属本scene，记Unsupported，原业务仍正常执行。toggle拒绝不能在菜单写之后错误称NoBusinessWrite。其它preset不启该Mutation。
- Main.UpdateRendering保持原mtx/StateUpdate/ThicknessDisplayUpdate。只有Current active Mutation时收最终signature，包含PresetHoming规范化和aux状态；qualifiedBar facts拒PPT/Whiteboard。Finish在原Notify/Request之前，含updateState=false。没有额外Request、日志/线程/磁盘/GPU操作；normal未安装TLS时构造/Mark/Observe/Finish全no-op，无新增clock或heap。

## 测试与证据

B201–B209直接production Ui3FinitePublication，B210直接TLS/scope。B201原子accepted+row，B202caller最后签名/new revision，B203拒绝旧goal不Supersede，B204Unknown失证，B205pending/complete nochange，B206六真实停点及生产reader，B207515请求/512prefix+3drop，B208mask/finite/枚举/来源与derived side，B209冻结来源变化拒绝。测试比较已知状态，不复制正确publication/ledger算法。

B206 writer停住时reader一次尝试；Wait超时同样先Resume再真实join、然后撤hook，publication/pause对象没有提前析构。B210空owner/null publication不消费request、不读测试clock。各new helper没有新CLI/GUI或全局MouseHook；原B/R文本保持。B202等只是数值helper合同，不含真实PresetHoming/Submit/ULW。

已读 `ui3-b2-p1-red-debug-arm64-headless.status.txt` exit1/pid29248；stderr恰9个B201–B209+FAILED count9，B210无失败。首次完整Build因C3 pointer失败、第二因新Main歧义失败，均不伪记PASS。当前 `c3a-u2-b2-green-f065-debug-arm64-build.status.txt`真实exit0，log0Error/4Warning、28.42s。新绿Headless/parked/PptCOM由root session6778顺序复验，本报告此时不预写退出码。

## Findings (not fixed)

- **P2，raw0/phase来源门**：当前validator拒toolRevision0只与R2合法Pen baseline相容，不能泛称0为无效source。未来fixture必须从真实ChangeStateModeToPen+versioned getter建立initialStable，并断言phase已ready；若需要支持合法原始revision0的其它入口，应以validMask/source provenance区分未知并窄修validator/tests，禁止造非0。本P1无安装者，不是已证普通产品回归。
- **P2，多owner与停后封口**：NoteCompletedGoal已避免plain ledger race，但Interaction最后一次没有后继mutation时，atomic receipt不会自动吸收到最后plain row。P2必须给单owner受控封口/receipt吸收，再all owners joined后导出，不让render直接写records。SetTestHooks/TLS/context的寿命亦只能在owner启动前/真实join后改变；当前停后span不是完整最终动画报告。
- **P2，真实candidate/完成尚未接**：numeric receipt只验证revision一致；P2须给真实canvas/layout/SVG/完整事务proof以及晚到receipt/新目标交错策略，不能只NoteCompletedGoal当已完成。Submit/Advance/settled/B3/F和initialStable/anchor/source/privateconfig仍没有本批实现，未审未授权范围不扩写。
- **P2，平台与性能**：当前只有本机Debug ARM64类型/链接门；Release/其它架构、真实Bar/像素/三轮/CPU/GPU/Win7/HC-H2不因本helper变绿通过。static布局/lock-free实现与module链接的跨架构门由root后续安排。

## Verification

- Lint：本reviewer未运行linter/Git；只读7source字节/模块边界/基线片段、实际callchain与测试。
- TypeCheck/Build：root最新完整Debug|ARM64 **PASS**，实际status已核；reviewer未发起构建。
- Tests：RED真实9失败已核；本批新动态GREEN尚待root实际raw/status，不预写PASS。旧绿不替代当前gate。
- 只此report写入，source/spec/task状态不改；STATIC_GREEN可与之后动态结果按最终source身份追加。

## 2026-09-30 最终动态门增量（同冻结七source）

本reviewer随后直接读取本轮文件，未执行EXE：

- `c3a-u2-b2-green-debug-arm64-headless.status.txt`实际exit0、pid34212；stderr空，stdout216layouts failures0/PASS animation。先前9项B201–B209红与B210/旧B/R在同入口转为无失败，**B2-P1已定义数值合同的动态门GREEN**。
- `c3a-u2-b2-green-pptcom.status.txt`实际exit0、pid7044；stdout PASS PptCOM descriptor ownership and session/owner contracts，stderr空。
- 前文完整Solution `c3a-u2-b2-green-f065-debug-arm64-build.status.txt`exit0、0Error/4Warning已核。其它writer的修补只通过同构建体现，不作为本reviewer对C3/F065/U2实码范围的结论；本轮parked/U2具体文件未读，不代写其结果。

因此当前判决从STATIC_GREEN追加为**B2-P1 GREEN（数值helper/真实接受接缝、当前本机Debug编译与定义回归）**。原动态待验文案保留为此前检查点；P2的raw0 baseline、atomic receipt停后吸收、真正Submit/Advance/SVG/成功事务来源与owner/lifetime门继续保留。没有新fixture/真实有限完成/软件帧性能数据，未扩大任务或发布PASS。
