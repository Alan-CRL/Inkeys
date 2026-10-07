# C3-A / Main C07 实际源码与运行前安全独立复审

日期：2026-09-30。Active task：.trellis/tasks/09-27-integration-and-release-check。本 reviewer只读冻结材料、实际diff、全部新Fixture 495行、root authorizer/runner/publisher、Main共用span与C07、helper/Host/AutoSave实际调用链；仅写本报告。不自修产品、不改其它报告/Spec/账本/工程/Git，不构建或运行EXE/GUI，不递归派发。

## 判决

**C3-A八个场景＋Main C07：STATIC GREEN，逐case CLEAR_SAFETY。** 当前C03/C05/C08共8个hold/release入口及C07有实际鉴权、owned HWND、受管context/线程/事件、真实故障/成功前提和失败不误计PASS的退出路径。Main C07的异常/前提失败清理也已按代码核对，没有发现提前释放仍活context的问题。

**CLEAR仅针对本报告所列源码身份；执行前仍须最后修补后的完整Debug|ARM64 Build真实0。** 当前已读两个完整构建都为1；第二个不能作为链接成功或新CLI运行依据。后续C3源/鉴权/Main/事件/项目变化需增量源码复审；取得新Build0并核候选EXE身份后，root才可按单case安排真实测试。安全放行不是动态PASS、Win7或RTS静止证明。

**C09/C10/C11和三个reader：NOT IMPLEMENTED / NOT CLEAR。** 当前真实dispatch明确Failed/error90，不能给这些case运行许可或生产恢复PASS。新Desktop fixture reader和expected validator还有下面两项已确认待修，root已接受留C3-B前处理；当前夹具调用不可达，不把它们写成现有普通产品高危恢复入口。

## Findings (fixed)

- **AutoSave.cpp:805源码类型错误由唯一worker修复。** 首次完整构建在 initializer_list 混合 wstring* / const wstring*，出现C3535/C2440；属于源码错误。当前一行已改为显式 std::array<const std::wstring*,2>，路径/校验顺序未变，SHA从3C009E65…变为 **E897076C015CDA579E256B292F5505E64C2E286A14810A0F3DA62C9FE0DB3F4F**。reviewer未改源码；后续组合构建已越过该错误，但因另一UI源错误整体仍1。
- 设计的五项修订保留：plain bool/gates{}与trivial布局；真实after-wake后hold；local/outer publisher允许幂等多调用、首见证一次；C05字段映射既有Trace；receipt单槽/Extra完整编码属于后续C3-B，当前不假称已实现。
- root明确有限普通Close wrapper接受case7–14：C07最终真实Close可同样见证，原8–14不变；不允许reader、普通未知case借此发布关闭。

## 逐case运行前安全表

| selector | 静态 / safety | 已核实际条件 |
| --- | --- | --- |
| C03-presenter-hold / C03-presenter-release | GREEN / CLEAR | 真ULW required ConfigureWindow style拒绝、GraphicsReady、精确set LAYERED/clear NORD、未FirstFrame；actual outer Begin after-wake门 |
| C05-before-hold / C05-before-release | GREEN / CLEAR | required Drawpad.beforeCreate真false，实际Rollback/promise前暂停；Drawpad=0、前三窗owned/hidden |
| C05-created-hold / C05-created-release | GREEN / CLEAR | 真created(HWND)记录owned hidden窗再throw，实际Rollback在DestroyWindow前Begin；四角色仍活 |
| C08-render-hold / C08-render-release | GREEN / CLEAR | 实际Stored/history与非首成功内容Present尾事件、正式Close/原deadline/真实StopProduct等待 |
| C07-main-ulw | GREEN / CLEAR | 同一Main模板首Host全模式拒绝→旧chain/owner终止→独立新ULW→超原grace后16Moves/Up/成功内容帧→正式Close |
| C09/C10/C11 / read-committed / foreign-reader | 未实现 / NOT CLEAR | entry switch default实际FailFixture90；没有trajectory、完整receipt或fresh reader调用的动态证据 |

表中所有CLEAR均受“新完整Build0＋同一候选来源/增量复审”门约束，不能拿旧PE、Header声明或已审primitive代替。root先逐个release/no-fault验证前提，再对应hold/C07三轮，父强杀/前提失败/错误90一律FAIL。

## Actual auth / POD / early dispatch

- ShutdownSupervisor.cpp:2168–2231先核argc9/finitecase、数值非零和指针宽度、三个HANDLE_FLAG_INHERIT、父HANDLE实际PID/非self、父/current镜像和copied EXE文件identity。HasExactFailedArmChildIdentity沿实际volume/file-index比较，没有用路径子串当文件identity。
- CanonicalCleanupPath只接受本轮drive绝对形状；source规范路径必须位于由真实run TestResults/release-hardening边界导出的同repo前缀。LeaseCleanupPath:2146–2165逐drive/root到每层目录及copied/source文件用FILE_READ_ATTRIBUTES、OPEN_REPARSE_POINT、BACKUP_SEMANTICS核属性；无DELETE共享，lease vector保持到child全部owner返回或死亡。这个窄驱动器路径限制属于新fixture，不改产品UNC兼容。
- 1024B magic1430FA04/version1/bytes/case/issuer/intent、nonzero nonce、reserved与初始status、零Trace/observed均先核。producer必须origin=0/expected全零；reader必须origin非零且非self并通过当前ValidCleanupReceipt。完整expected语义的未完成项另列，reader当前没有活实现。
- g_cleanupRealObservation/case/directory只在这些核验后绑定，随后authorized/Authorized和ack发布；newpurpose无论返回错误或catch72，TryRun…Early都true-return，不落配置、单实例、普通Main/Office。public selector少参数/未知case返回71；四negative的wrong-image/no-inherit/old-magic+declared96/unapproved-case在任何Window/gate/fault/artifact前拒绝。旧48/128/96B路由和普通套件没有自动扩跑。
- Parent:2244–2293只在PROC_THREAD_ATTRIBUTE_HANDLE_LIST列父process查询+同步、ack EVENT_MODIFY_STATE、mapping FILE_MAP_WRITE三个句柄；每轮CREATE_NEW owned run/bin/copy/nonce，没有给child任意external path/window/process权限。negative3实际是新1024B mapping内旧magic/声明96，不能将它的结果称作已测试小mapping对象所有边界。
- IsAuthorizedCleanupRealLaunch:2490–2495核当前唯一packet/case/canonical directory和authorized；Fixture entry/Main C07第一动作都调用它。当前没有其它未经authorizer的入口调用两fixture，亦无普通config/env故障开关。

## Heap State / gate / HANDLE / publisher lifetime

- FixtureChildState由runner、四WindowSpec lambda及Win32 thread envelope各持shared_ptr，Host callbacks/startupContext借同一heap对象的成员；State不持Host。控制/Close thread进入后取强引用再delete envelope。事件由State拥有、Signal强引用独立State；pending envelope创建失败走真正固定90 noreturn，不释放可能仍活对象。
- RunStartupFailure把management scope留在runner栈，owner只持Signal。after-wake reached事件已到后才幂等Begin读同一已Armed tick，未制造第二timer。hold无Complete/析构/Close，永远留栈直到真实monitor终止进程；release先放门、starter HANDLE真signaled/exit0/CloseHandle，再Stop/Window.StopAndJoin、销毁计数、Complete真实monitor join和旧Signal0，最后等过原grace+1000。
- FailFixture:36–47确实写error90/resultFailed然后循环TerminateProcess(90)/Sleep1，不是靠noreturn注释假定不返回；不会用90制造产品001A/0015/0016。catch也到此路径，mapping/path lease仍在root RunCleanupRealChild调用栈。正常return只有全部业务owner/辅助thread/monitor结束之后，才清g observation、unmap和释放lease。
- Main C07的state及oldOwners在try外；catch和每个前提失败都进实际fail closure:763–773，先正式SetOffSignal，随后StopProduct、Window.StopAndJoin，才关闭old owner管理句柄并返回90/91。即便join停住，普通15秒监督/Failed-only noreturn仍保留map/heap/callback上下文到死亡；若完整返回，业务owner已经joined，当前State才可离开。没有detach、TerminateThread或以关活句柄冒充join。
- helper当前true分支明确CAS→SetEvent(wake)→WaitTestGate；false保留旧pre-wake C00。plain第三bool无成员initializer，gates{}零默认；sizeof/offset/trivial/lock-free断言实际写出，不因编译环境绕过。
- root publisher:2510–2523先真正NoWait原子前奏，g_cleanupRealFatalRecorded.exchange只使首见证写tick/intent。多个local/outer可幂等调用，原截止不重置。ordinary wrapper:2525–2537真正SetOffSignal1之后读取PublishedShutdownDeadlineTick和g_armState2/4，没有把grace当普通15秒。
- PublishCleanupRealStage用Interlocked单调max CAS，每个更高stage只发布一次；实际有coordinator、style/fatal/close线程调用，不能复述为只有一个物理writer。相关字段在各自发布前填、成功最终包在child死后读取；这个实现的单调发布与当前所有消费点已核，未因多个幂等调用重复推进阶段。

## 真场景和Main正常路径

C03实际代码核styleRejected=1、mask、GraphicsReady、ProductRunning且无FirstFrame，并以Signal实际Armed值作grace。这里RTS尚未进入，不伪称驱动永久停滞。C05的before=false不要求未创建Drawpad destroyed；created=true/throw保有真实HWND/lifecycle；required Rollback/DestroyGroup/Start failed StopUnlocked均走既有Window生产代码。

C08在Host.cpp:397–408原快照/ULW统计/RuntimeRevision发布之后，仅hidden injection+成对事件+真succeeded+firstFrameReady+history内容+实际内容revision匹配才停。普通帧无新增clock/lock。WriteStoredStroke只向本child当前Drawpad发private消息，Down新stroke、6逐consumed Move、Up新terminal、history/completed kind/content/成功Present均核本轮差分；时序不足直接90，不凭event名PASS。Close辅助线程调用root正式wrapper，join并核state2/4和原tick后管理线程真正StopProduct；hold不增startup timer。release先放门，再同样Close/Stop/Window join，自然返回才Passed。

C07正常Main和early counterexample确实共用StartDraw3ProductWithFallback模板，普通调用observation默认null：原warn logger、StopProduct→旧Window join→first Complete→清NORD/LAYERED→意图空才新Window/ULW scope的顺序和modifiers保留。测试有observation才旁挂数字/跳过普通logger；没有手抄另一份span。首generation的所有实际style请求返回false，首Host在内层不能ULW成功；graphicsReady至少两次和firstStartFalse见证才通过。

C07在旧join处先核旧HWND失效/Host不running/SignalCancelled；新窗口创建后再核四个旧thread HANDLE已signaled和created2/destroyed1。generation用生命周期计数，不要求old/new数值HWND不同。ULW ready核真实成功content、requested/ready完整output revision和样式无NORD/transparent；等超过旧原grace后向当前owned hidden窗送Down、16逐consumed Move、Up/recycled/history/新成功Present；随后同有限正式Close wrapper，最终两代销毁计数齐全才自然0。缺DComp前提91、任何异常90不会计绿。

Parent真实验证: startup hold必须001A/故障门/未Start或Stop返回/原grace+15000±调度容差；ordinary hold必须0015/0016/实际Close原tick/state2或4/Stop未返回。release需Cancelled/旧窗销毁/过旧grace仍活，无fatal/intent；C07需旧/new代次和过旧grace成功一笔。所有case先exact child HANDLE signaled/GetExitCodeProcess，deadline是绝对原tick；timeout返回false后TestChildGuard仅处理自己HANDLE。最后packet CREATE_NEW/Flush发生在child死后；错误90、父强杀或reader失败不被成功记录覆盖。

## Findings (not fixed)

1. **Desktop strict fixture reader预读重解析缺口，C3-B前待修。** AutoSave.cpp:781/785先调用ReadIndex，805–809才查index/selected UInk reparse。原ReadTextFile:292–294用普通CreateFile会跟随链接，故可能先读外部索引再拒绝；root/date目前也仅attrs检查，没有完整artifact层只读lease。当前Fixture不调用该新API，普通SubmitLoad行为未扩大，不当作现活产品高危入口。root已接受F-065后续最小专用修补：先OPEN_REPARSE_POINT只读lease核目录/index/backup/选中UInk并保活到读取结束，复用原ReadIndex/strictRead；不改普通ReadIndex/SubmitLoad。
2. **future reader合法expected校验未完。** ValidCleanupReceipt:2090–2128只有宽范围；bindingMode/pageKind可到3/4，workspaceType、slideIds与scenario族没核，日期只数字形状、不核真实月份/日。应在C3-B前按Desktop/PPT/foreign case族及实际enum/页面/SlideID/unused-zero规则校验，再独立review。当前15–17仍entry90，不能放行reader；parent没有可信baseline的public reader-only也返回71。
3. **存储/receipt/恢复全链尚未实施。** 本批只写delay event/getter/private loader及strict reader雏形；未有C09/C10/C11真实Host保存/worker gate/正式Close、全部variant/depth32 digest或fresh reader。不能把已有Read函数、pending计数、UInk文件存在或原primitive sentinel写成恢复PASS。
4. **编译与动态证据尚缺。** 本reviewer未运行，当前两个完整build1如下；没有可用于本批新CLI的最终成功链接证据。C08诊断时序/C07实际RTS/ULW与三轮结果、成功RTS callback quiescence、Win7/HW/WARP/Release/低层未返回API及整体任务均保留未验。

其中1/2为本次确认并已交root的后续模块问题，未自行改源；3/4为准确实施/证据状态。没有发现阻止C3-A八case/C07本身安全进入受控测试的源内寿命问题；执行仍受Build0与最终来源门。

## 最终身份与格式

| 文件 | SHA-256 |
| --- | --- |
| Draw3.FailedCleanupFixture.cpp（495行） | BAB8B65749FDEE676DDAFD31D497D00154048D746BBBFD523A03DAB877D5154F |
| Draw3.Host.h | 1B12F3901E5474A595D0B1DBA935DCA6B89BCF899CFC4BE799D227462F9E4814 |
| Draw3.Host.cpp | 823EA958822E05C17F6546477183968B9BE64E9EE117D621294CBA217FE7FE10 |
| Draw3.AutoSave.cppm | 0C7186F5D3FB6C93898CFEA81D00BAC802B3F6CC24D439C7A0B509F416F3A8C6 |
| Draw3.AutoSave.cpp（单行compile fix后） | E897076C015CDA579E256B292F5505E64C2E286A14810A0F3DA62C9FE0DB3F4F |
| IdtMain.cpp | 892DB0445DDC0A4E9566FD4C1C8B224B81BC9AFA5DE26AAE6DC1CA66B218DC89 |
| ShutdownSupervisor.cpp | AFB1A977D2B042619562E4B8D08B0C4A43CE8A0225D18C49FACA86FCC1E35B07 |
| FailedCleanupDeadline.h | B6922C3CA6D33F31D194229C78703D586E6BDC0009A96C79463E8DC581A1564A |
| FailedCleanupDeadline.cpp | F1EDD63CFCB10107C06895716CC5285BE217DBE5B61B14794B6EB17956C62613 |
| FailedCleanupRealCases.h | E8EE4841373ED51CCB34F27D4C47071B2FAF7D45F013D55D202142F7B790D1E3 |
| Inkeys.vcxproj | 887DE75602CB92D916E314D508980A679461B29EF08D0F5C1E454E6EA8BBD8FD |
| Inkeys.vcxproj.filters | 740461B3662C73E8149144A9EAC0CBFBC1980FABAE4B0896B224E662C219AF05 |
| InkeysHeadlessTests.vcxproj | 1EAD17EB430157181617828E6FFD933209D4F9BACFD17331E17689573CF63856 |

Root8源符合交付；C3-A仅AutoSave单行修补变化已复审。新Fixture仅主工程/filters登记；Headless没有加入Main/Supervisor/真实窗口fixture。Bar.PresentationProbe主+Headless登记由另一review所有，不升级为本C结论。

## Verification

- **Lint/静态格式：** scoped git diff --check HEAD输出空、exit0；本review不执行独立linter。最终源保持原UTF-8/BOM/CRLF；无产品机械自修。
- **TypeCheck/Build：** root首次 c3a-u2green-b2red-debug-arm64-build.status真实exit1，首意义AutoSave805类型错误；已在上述E897候选最小修补。第二 c3a-u2green-b2red-compilefix-debug-arm64-build.status仍exit1，log为Bar.Main.cpp285 WhiteboardActive C2668歧义、132 Warning/1 Error、65.93s。第二首因来自另UI writer的组合源，是源码问题，不是本机/Codex环境；没有据此改C3代码、工具链或声称链接成功。
- **Tests/GUI：** 本reviewer未运行。新C3所有case动态结果未验；旧Headless目标构建/预期B2 RED及parked属root记录，不能代替完整Build0或新真实case。
- **Safety：** 上表C3-A/C07逐caseCLEAR，在本源身份且最终完整Build0之后才可执行；C3-B/readers不放行。所有后续改动需对应增量review与新候选复验。
- 仅本报告被写，不改其它报告、源码/工程/Spec/账本、任务/HF或Git索引/refs。

## 2026-09-30 F-065 unused-reader 增量复审（覆盖上文未修1/2的旧状态）

本段只读复审 AutoSave.cpp **F502CEAD1F4F06CD60F09759309A728616339D392F4E39CD6CC8340FBD8DE533** 与 ShutdownSupervisor.cpp **DAF63688763734B632ECCF4CC9A99A26D271F6824F3580F09D9A5948A120D773** 的两处增量；不复用旧E897/AFB1为当前身份。Main 892DB044…B218DC89、Fixture BAB8B657…7D5154F、Host h/cpp 1B12F390…62F9E4814 / 823EA958…FE7FE10、AutoSave.cppm 0C7186F5…16F3A8C6均再次核对保持冻结，九selector的owner/门/普通截止/Main回退调用链未变。

**STATIC F-065 已修，动态正负例待验；原C3-A八入口＋Main C07的九selector CLEAR_SAFETY保持。** root的新完整Build0已取得，满足本候选编译/链接门；仍须root核当前EXE来源、顺序完成相关旧回归，再单case受控运行。C3-B轨迹/完整digest/fresh reader仍未实现、入口仍90 / NOT CLEAR，不因这个静态修补扩大放行。

### 先lease再读：实际源顺序

- AutoSave.cpp:584–647新DesktopFixtureReadLeases只在fixture层使用，禁止copy。Acquire以目录FILE_READ_ATTRIBUTES/文件GENERIC_READ、仅FILE_SHARE_READ、OPEN_REPARSE_POINT/BACKUP_SEMANTICS打开，取得真实DISK type与BY_HANDLE_FILE_INFORMATION并拒绝reparse、错误directory/regular类型；失败唯一句柄立即关闭，push_back分配失败也关闭再传播。RAII保活已登记的全部句柄。
- AcquireDirectoryChain:620–642从drive root逐组件到artifact/ownedRoot/desktop/date，拒绝不支持的路径形状、异常组件/尾空格点。readonly共享同时拒write/delete，防止已核对象在读取期间被替换或改reparse。这是窄fixture路径，不改普通产品UNC/目录写入合同。
- reader:835先声明NamedMutexGuard、836再声明leases。847先成功取得primary index lease，才851调用原ReadIndex；backup同理855→858。Missing完全跳过按路径重开；reparse/type/访问/sharing错误fail closed。878先lease选中的UInk，才879 strict load；index原始字节881仍在同一lease/mutex内读取。所有exit/catch均先析构lease，再释放mutex，避免自身只读lease阻住后续正常提交。
- 原有效primary优先/有效backup回退/两缺失NotFound/损坏失败保留，不NewIndex。ReadTextFile/ReadIndex/private strict loader及普通WorkerMain读取接缝沿上批实码，普通SubmitLoad/records不扩张；新增lease不放进普通worker热路。本段确认的是预读重解析/对象保活缺口的代码闭环，不把尚未运行的sharing、primary/backup/Missing/reparse正负例写成PASS。

### Receipt按真实case族，而非宽enum上限

- ValidCleanupReceipt:2090新增bool desktop。三个实际调用点都使用正确族：child expected按ReadDesktopCommitted，post-death reader observed同样按reader case，producer observed按DesktopHold/Release/NaturalClose的实际desktop变量。旧C3-A/C07 producer auth初始expected全零路径与结果判定均不调用这段reader语义，原鉴权强度和模式不变。
- Desktop核真实workspace0、pageIndex0/total1、正daily/session sequence、单有效画布/no retained、零PPT key/SlideIDs/各业务revision，固定canonical非零session UUID；localDate按Gregorian年月日/闰年核，不只看字符串形状。trigger保留真实Clear0/Exit1。
- PPT核真实workspace2、StableSlideId **enum0**、Slide/EndScreen **0/1**、zero-based normal页/独立结束页范围、固定601/602＋总2真实页/3active、无retained、非零key/mutation/target、bindingRevision1、零Desktop date/sequence/trigger。已对照Bridge.h:47–77/135–146和production whole-presentation capture:1194–1215的active/legacy/retained集合；未要求可能未使用的sessionRevision必须非零，也未虚构deviceCount==1。
- 公共saved stroke/point非零、source bytes/hash/GUID/unused reserved和大小限额是本有限fixture数据门，不是普通产品保存格式限制。后续C3-B formatter须按真实owned snapshot填写：Desktop普通Import/Load沿snapshot.canvases，activeCanvases可空，activeCanvasCount=1应按effective active（active空则canvases）计；PPT按完整active集计3。stroke/point是整份有效画布集，不能只数当前602空页或把legacy alias重复计数。当前formatter未写，故这些来源约束和全部geometry/digest/严格Load匹配仍留下一阶段审查，不能用validator标签冒充readability。

### 更新Verification

- 第三完整root构建 **PASS**：c3a-u2-b2-green-f065-debug-arm64-build.status.txt真实exit0；log明确完整Solution成功、0 Error / 4 Warning、28.42s。本reviewer只读status/raw，没有运行MSBuild；前两个build1保留为历史源码失败，不能删除或写成环境问题。
- 两增量scoped git diff --check HEAD输出空/exit0；保持原UTF-8 BOM/CRLF。数据validator、reparse/sharing/Missing/backup等动态用例、C3-B和新真实GUI场景仍未由本reviewer运行。
- F-065原未修1/2状态由本段静态已修覆盖；原未实现3与平台/RTS/低层/整体任务门保留。只追加本报告，不修改其它文档、源码、Spec、账本或Git索引/refs。

## 2026-10-01 C3-B 首批实际源码/safety复审（当前有待修阻断）

按新派发只读完整最新contract（SHA06804F015967580DFE1FF5E5EFBABAB66889C367A4F29EB7E3EC216B9FF6061F）、root/worker implementation、真实1186行fixture与后续root两行修补、Host新增NotFound事实、Supervisor oldDrawpad input/auth/parent death→reader链。未自修、Build/EXE/GUI或递归，未改其它报告。

### 当前判决

**NEEDS_REVISION / 新C10与reader结果验收未放行。** 已确认并由root修复一个fixture场景错误；仍有两项实际断言/轨迹问题需最小修补、冻结新hash、增量review，再完整新Debug|ARM64 Build0及相关回归。当前C3-B尚未构建/运行，旧19个producer、每轮四auth negative、C1/四Natural通过都只属旧候选。

C09/C10/C11与三个fresh reader现在已有真实代码，不再是旧error90 stub；未知case仍90。本段只评价静态源与受控生命周期，不凭“有实现”给Saved/readability/自然截止PASS。现有C3-A/Main主链的旧审查范围保留，任何新候选执行仍须最终编译/来源门。

### Findings (fixed)：C10改色错误离开PPT

- 原Fixture:857–860新建default ProductState；Bridge.h:203默认Desktop，StateBridge.cpp:40–45会清掉presentationTarget并整体replace。C10选回已有A的601后调用SetPenColor(B)，因此B可能被拒或写在Desktop，不能验PPT A+B。此为实际源问题，已直接交root。
- root明确接管该唯一函数，从真正ProductHost().ProductBridge().Snapshot取基线，再仅改tool/selection/width/color/autoSave五项，保留workspace/target/page/hasPage/设备等现值并加中文原因。当前SHA **A66C27B7D9FD810389EEF3D1B666DCB24807C07A97B188C2F6721015B4C1FBC1**，1188行；原452D2EDE是历史交付，不再当前。
- 已读getter/StateBridge.PublishState和Host.PumpBridgeState真实路径；当前C10已完成ready、fixture是唯一相关业务发布者，不因这处复制现值又构造默认workspace。这个修补静态关闭场景重置，不升级C10动态PASS；未改普通Bridge或其它四源。

### Findings (not fixed)：本批最小修订

1. **C10第二笔不能要求contentRevision按每笔递增。** WriteStoredStroke:700仍强制新contentRevision != beforeStroke。当前生产Controller:6596–6604只在history.HasContent布尔变化时递增；追加B在已有A的601为true→true，12467–12468虽标presentation mutation再调用发布，却不改该version。真实B可Stored/成功呈现而fixture最终等超时90。因此C10 hold/release均有可执行性阻断，corresponding reader虽然源码有实现也不能以这个producer得到有效基线。本reviewer没有改生产Controller/Host版本语义。最小fixture应区分原空/已有内容，保留C09/C3-A空→第一笔的旧条件；已有内容用真实Up消费/回收/Stored与其后真正成功呈现、后续whole-PPT A+B严格receipt佐证，避免拿较早live/改色帧的成功count冒B成功。WM_PAINT当前WindowControl:1515–1522只在gpuTransparent时RequestFullPresent，ULW不能未经实际入口核对便假设这个消息有效。
2. **F065 negative不能把NotFound和腐坏/reparse拒绝混为通过。** 当前corrupt-only约1064只核loadedSnapshot为空，symlink约1075只拒Loaded或snapshot，NotFound亦会写verified/reparse-rejected。已成功建立的corrupt或link对象应明确Invalid/IoError且无snapshot；Missing case只NotFound；创建symlink权限不足保持NOT VERIFIED。该问题只改fixture断言，不改普通strict reader算法/权限。当前不能凭这些弱断言宣称F065专项status动态门完整通过。

以上已交root，reviewer未自修。若root后续修补，须在本报告追加准确新源身份及实际增量核对，不能用A66或旧GREEN表述冒充最终源。

### 其它实际主链已核，无新的生命周期阻断

- C09/Natural真实ULW Host+private根，A真实Down/6逐消费Move/Up/Stored/成功内容帧→Clear accepted/command推进→actual accepted1/committed1/failed0/pending0。F065 strict reader给A全部真实index/UInk/model字段，不standalone造worker。第二B/Clear到真实60s/400ms delay event，hold未commit且pending存在，真正root SetOff/Host.Stop worker drain；release/Natural全部2 accepted/committed终态后Stop/Window join，再从磁盘读B封口，未把accepted当Saved。
- C10保留真实601/602＋独立EndScreen、same nonce storage session、actual empty Current Load NotFound及failed初始基线；Host仅hidden operation Load/status NotFound累原子计数，普通service计数/算法未变。后续Save/Load要求failed无新增，Trace保留actual absolute，不Reset/伪0/preseed；accepted仍含Load。
- baseline/final取全新PresentationAutoSaveService的真正Start/SubmitLoad/CloseAndDrain/actualcompletion。PopulatePptDisk仅取owned唯一entry作receipt metadata，完整schema/sourceRevision/strict application-owned binding校验仍由生产Load完成。文件在任何Read/Load前Readonly OPEN_REPARSE/DISK/type lease；state.disk和freshReader在outer shared_ptr State中，异常仍保留到固定90死亡，不提前析构正在工作reader。正常Read结束先真drain，再reset reader和disk。
- C10原UInk durable/index未换门复用，hash旧index的临时readonly lease在放continue前已离开作用域，未由测试自持index阻止atomic commit。hold真正Close/Stop待原worker，原15秒先于原30秒gate上限；低层I/O尚未返回仍不是这个测试证明范围。
- observed唯一PublishReceipt写点、observedSealed拒二写：hold A仅BaselineReady；release/Natural A保存在State私有值、final B仅Finished；reader实际result重读后仅ReaderDone，parent只在exact producer HANDLE死后读包。PPT target/session revision来自已鉴权expected作为新请求输入，observed是实际completion echo，不冒文件内独立版本；mutation/GUID/index/source/model都来自actual。
- FixtureValueEncoder以LE整数/float bit_cast、finite、DWORD length/optional tag、11种MessagePack variant tag、原序Map/Array/Extension type+payload、depth32/8MiB编码上限覆盖完整snapshot/device/hardware/canvas/stroke/operation/extra，没有排序/JSON化或只采端点。计数按effective active（Desktop fallbackcanvases、PPT3active）＋retained，不重复legacy镜像。SHA用现BCrypt能力，UInk hash/length来自真实ReadUInkFile.sourceRevision；尚无编译或数据验收结果。
- Root1024B/POD/argc不扩。只有两PPT reader允许initial Trace.oldDrawpad非零且<=UINTPTR_MAX，复制Trace清该项后其它全零；Desktop/producers继续全零。parent在原producer自然死并通过结果前提后取真实PID/HWND/封口receipt，同run/nonce/copied EXE用新map及原三个继承HANDLE启动reader。actual bindingToken由origin PID＋旧数值HWND＋固定owned descriptor重构；无OpenProcess/旧窗查有效性或发消息，数据不升级为权限。
- F065 clone只固定owned新子根：CREATE_NEW/CopyFile failIfExists、所有目录/源文件Readonly lease，relative简单叶名拒分隔符/colon/点点；不覆盖原index/UInk、不注入records、不删除未知文件、不写用户配置/Office。源码未创建hardlink，也不把已有regular file一定只有一链接当保证；所有读只在已验证owned路径，源bytes/identity/完整receipt仍核。symlink仅指本run owned原数据、权限/FS不可用独立NV，不修改系统。
- 错误前提/exception真正固定90 noreturn保留全部活对象；只有完成Host/Window/helper/aux HANDLE join才自然成功，parent超时仅精确own HANDLE清理且FAIL。reader回忆只是last durable readability，不是可见恢复或真正Restart；C04成功RTS静止、Win7、真笔/Office等独立门保留。

### 当前身份 / Verification

| 当前相关源 | SHA-256 |
| --- | --- |
| Fixture（root改色后） | A66C27B7D9FD810389EEF3D1B666DCB24807C07A97B188C2F6721015B4C1FBC1 |
| Host.h | E502A08950DE9F26D6B6E84B3910E5B7C439A8BE6B06100E1B16FAA4BA3159AB |
| Host.cpp | BAA95903091C33A96A7D8A7C647290A43AD1E615B0413AE0523159D485C46254 |
| AutoSave.cppm / cpp | 0C7186F5D3FB6C93898CFEA81D00BAC802B3F6CC24D439C7A0B509F416F3A8C6 / F502CEAD1F4F06CD60F09759309A728616339D392F4E39CD6CC8340FBD8DE533 |
| Supervisor（PPT旧HWND输入增量） | 785785AEB48EDB1BBCC3FE2D63DBB1982D2953F3B9BC5970A0C59A190643050B |
| Main | 892DB0445DDC0A4E9566FD4C1C8B224B81BC9AFA5DE26AAE6DC1CA66B218DC89 |

严格UTF-8/原BOM/纯CRLF已核；相关tracked scoped diff-check exit0，新Fixture未跟踪需对应no-index检查。未自行linter/type-check/Build/EXE/GUI；本批新完整构建尚未开始，UI/Draw另writer仍半写。两待修项解除及源码全冻结后才新完整Debug0、旧适用回归，再逐case release前置→hold三轮→exact自然死后的same/foreign freshreader；数据结果各自记录，不能借旧候选PASS。

## 2026-10-01 C3-B 两项最小修补独立增量复审（覆盖上一段NEEDS）

当前Fixture **2846921497D3F6296D9FBB892C883CAD776EE3198885BEA53938D5FD3D4FC457**，72986 B、1232 CRLF行、UTF-8无BOM。对照worker最新记录和实际WriteStoredStroke/F065 negative、WndProc/WindowControl/Controller/Presenter调用链，未仅凭消息/变量名批准。其它四源及Supervisor785785…643050B保持上一段身份，root SetPenColor的实际Snapshot及保workspace/target/page修补保留。

**STATIC GREEN / CLEAR_SAFETY（C3-B有限case及受信父调起的三个reader）。** 上段两项NEEDS已在这个准确源身份上静态关闭，没有剩余本批源码/受控寿命阻断。此结论只允许在所有writer停写、最终新完整Debug|ARM64 Build0、当前EXE/源码身份和适用旧回归门后，由root逐case运行；不是C09/C10/C11、readability、F065 negative或其它新动态PASS。旧C3-A九selector审查范围保持，但新PE仍应复验适用路径。

### Findings (fixed)

1. **已有A再B的版本误用已修。** Fixture:691–730仅在beforeStroke有内容且非pause时走新分支；初空/原C3-A pause的Up与原predicate:732–745逐字保留。先发实际Up，核同新stroke、inactive、terminalLocked、Up前之后的真实consumed sequence、本轮terminal+1、recycled+1、completed Drawing；不再要求true→true的hasContent版本递增，也没改Controller/Host的版本合同。
2. **不借较早live成功count。** 本笔terminal/回收条件满足后另取afterTerminal快照基线。向当前Service::Drawpad、PID/有效/hidden都核过的own HWND，以2秒SendMessageTimeout发无payload WM_DWMCOMPOSITIONCHANGED；失败/超时不继续PASS。随后核new successfulPresentCount、last success、新full attempt、保持同workspace/page/PPT ready/UI-ready/input门、ULW与primary requested/ready完整output revision、presented当前content版本。完整A+B生产保存/strict model/receipt验证仍在后续C10主链，不用这一pulse替代它。
3. **F065 status分层已修。** 当前corrupt-only:1107–1109、成功建立的link:1119–1121都只接受Invalid或IoError且snapshot空；Missing:1104只NotFound。权限/FS不支持创建symlink仍独立NOT VERIFIED/status evidence，不把NotFound或缺权限写成reparse rejection PASS。没有改reader算法、系统权限或外部数据。

### 已查清的真消息链及边界

四角色spec的Drawpad.windowProc是DrawpadMsgCallback；Window Service确实使用它作为WNDCLASS入口。Product.cpp:200–205运行中的product转ForwardProductMessage，再Host::ForwardMessage转WindowController::HandleExternalMessage。WindowControl.cpp:1501–1504无gpuTransparent条件、无payload解引用，实际store compositionChangedRequested并PublishControlWake；不是只有GPU路径有效的WM_PAINT。

Controller.cpp当前11350–11360实际ConsumeCompositionChangedRequest→RefreshAfterCompositionChanged→RequestFullPresent。Presenter.cpp:1014–1020在当前ULW无DWM updater分支，真实返回true；WindowControl:373–391的full request由原Controller消费到正常完整PresentFrame/Host ObservePresented。没有改变DPI/viewport/resize算法或选择已禁用的DWM模式。Host原presentCount/partialPresentCount沿true full参数计attempt，success由实际Present结果计；fixture区分“成功＋新full attempt”，不将其独立计数当作逐帧成功token或光学时刻。

本pulse只是已有Stored内容的软件呈现正确性佐证，不是首次落笔端到端延迟、真实系统composition变化或性能优化。窗口消息有界且限当前own HWND，后续Close/Stop/Window/helper/辅助thread真join及失败90保留栈的寿命不变；hold也不释放worker/gate/mapping。没有OpenProcess旧PID、操作旧HWND或新增普通产品故障开关。

### 本版本可执行范围与前置门

- **生产case：** C09-desktop-release、C11-natural-close、C10-ppt-release先核真实trajectory/保存终态；再C09-desktop-hold/C10-ppt-hold三轮，按原正式15秒和actual worker gate/drain自然死亡判。父清理/error90/缺gate/前提不足仍FAIL。
- **Reader：** C09-read-committed、C10-read-committed/foreign只经root在exact producer HANDLE自然死且封口receipt合法后，用同run/nonce/copied EXE及新1024B映射/三个继承HANDLE调起；不是给public reader-only绕过baseline。same/foreign实际productionLoad/完整receipt/family/F065状态各自记录，自然0且全部来源/模型一致才判通过。
- **旧路径：** C3-A/Main C07源码逻辑及限定CLR保留；相关回归/新版来源仍需要。所有模块下一次修补必须重冻结并对受影响路径复审/重建/复测。
- **仍未验证：** 本批新完整编译/链接、上述case/reader真实三轮、F065 sharing/reparse权限组合、C04成功RTS quiescence、Win7/Release架构、可见恢复、真正Restart/UEF及完整性能/发布门。不得以这个STATIC GREEN覆盖它们。

### Verification

本reviewer未Build/type-check/运行EXE/GUI，自始仅追加同一报告。五源hash核对、实际消息消费/ULW/full-present链、初空分支和setter保留、status逻辑完成静态核对；格式检查保持原UTF-8/BOM/CRLF，无无关源码修改。全Solution当前尚未构建，UI B2P2/其它writer工作由root确认停写后才能新candidate验证；此前Build0及19producer属于旧身份，不转为新版PASS。

## 2026-10-01 C3-B 精确失败诊断与DiagnosticsProcess增量复审

本段只读本批三源、真实事件/写点与SS包装函数；仅追加本报告，不自修/Build/run/GUI，不审成未登记UI/F Auth运行许可。当前Root明确旧C09-release/C11-natural/C10-release三positive都真实父65/child90，四auth负例通过；这是仍未达场景成功的实证，不以新诊断修补覆盖或删除。先前冻结全Build0及Headless26160/parked22032/PptCOM17700也不转记为本诊断版编译/运行通过。

**STATIC GREEN / 增量CLEAR_SAFETY：仅有限C09-desktop-release、C11-natural-close、C10-ppt-release诊断复验。** 保持上一批真实场景条件/数量/颜色/identity/15秒/等待预算；这是失败定位候选，不是保存故障实因已修。执行前所有writer停写、最终全Debug|ARM64 Build0/当前EXE与hash及相关旧回归仍必须；目前Bar/F在写，不开构建或新GUI。

### 实际诊断与借用寿命

- Fixture:117–128先原error90/resultFailed/PrerequisiteFailed发布，随后RecordFixtureFailure，再原固定90 noreturn。Record:59–115只收内部已auth launch；根据actual HostStarted取当前numeric runtime/PPT ready/bridge wanted/service绝对计数/NotFound与phase，不包含文稿内容或用户路径，不猜内部栈/产品死锁。
- 两个固定run根新文件使用CREATE_NEW、OPEN_REPARSE_POINT、WRITE_THROUGH、独占写；路径仅verified launch.directory加常量，root authorizer持该run/ancestor leases至返回/死亡。没有覆盖历史文件、原index/UInk或用户配置；诊断失败catch吞掉、写/flush结果不生成PASS。
- getter/写盘/flush/stderr为best-effort，仍可能阻塞；它不在产品deadline publisher或正常成功热路，不声称固定90立刻可达。此时header已FAIL，parent原bounded wait/TestChildGuard只清本轮精确HANDLE并记FAIL，即使诊断未写完/被父结束也不会变产品自然退出PASS。活State/context/mapping/事件仍在noreturn调用栈及root外层view/lease中，不为诊断提前析构。
- phase201–210与320+page系列只拆原准备/等待站点的数字见证；6000/5000ms等原预算和原复合条件保持。缺signal=0、已signal=1、Wait错误=2仅观测，不把“无信号”解释成未发生所有内部步骤。
- State新indexProbeEvents三个manual reset句柄只在授权Desktop第二请求前创建，先赋alias再设置fault snapshot；第一A仍原fault-empty。global alias只供这一child失败读事件状态，State/WindowSpec/envelope强引用保留至实际worker/drain/所有线程join或死亡；失败创建部分事件也到90保留栈。Close线程启动在alias设置完成之后，C3-A/其它case默认alias空，没有新增跨代复用或提前Close。

### 真CommitIndex来源与默认行为

AutoSave.cpp:491–523新增private CommitIndex的const faults参数，唯一ProcessRequest:816–817传自己原SnapshotTestFaults那份快照。entering在真实NamedMutex.Acquire前；acquired仅Acquire成功后；readCompleted在真实primary/backup读取/选择后或对应读入失败返回前。没有另读fault、伪造结果或移动NewIndex/CommitIndex/ReplaceFile顺序。

signalProbe只在非空时SetEvent，失败返回值忽略，不改变原保存终态。三成员默认nullptr，普通产品无新增clock/wait/I/O/logger，仅固定fault POD增长/空条件分支；不声称零CPU指令。旧writeDelay事件和真实60秒/400ms等待仍按原语义，不用probe制造保存成功。Acquire没返回、ReadIndex没返回与后续index操作可由三flag进一步定位，但尚无本候选数据，不能从名字宣布mutex死锁根因。

### RootSS DiagnosticsProcess范围

已核ShutdownSupervisor.h声明与cpp:2701–2721的五包装函数：QuoteArgument/CurrentImage/ProcessImage/SameImageFile逐句委托既有Quote/CurrentImagePath/ProcessImagePath/SameExecutableFile；StartInherited核STARTUPINFOEX.cb及lpAttributeList非空，再固定inherit=true、EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW调用既有StartSameExecutable。没有更改旧C/E鉴权函数、原入口或已有调用点，没有额外创建flags。

这层只复用转义/镜像fileidentity/带attribute list创建能力，不自己认证purpose、private tree、parent或三个HANDLE的exact名单。未来调用者仍须各自建立并持有名单/lease，核完整auth/错误early return。新Ui3PresentationFixtureAuth等未登记/未运行cpp与GUI F不在本段CLR范围；不能以共享header或StartInherited存在取得运行许可。

### 身份 / Verification

| 当前增量源 | SHA-256 |
| --- | --- |
| Fixture（1312行） | 9671E99A4A74EE0132559825FF6AB799F2FE05C0333AA328EDC2F2B816D6BA58 |
| AutoSave.cppm | A2AF7DD2ABDC87C6EF3C21840D82CE3E72C8409DB7A8FB6EEFA96D9DC99A4B8A |
| AutoSave.cpp | CC20BFEF3BA127117E8C95037ED412D9468CB0C08EE7699C77E12AA52486DABF |
| SS.h / SS.cpp | 1270AF02A1081C86E5E302BABF258178747A49B82EA27FDB2A51D5230B802C0A / C5F4BE3E9657FAE0C52FD5F7ABD02D1286115B78B9B443547CE487C341182EC3 |

Host h/cpp仍E502A089…3159AB / BAA95903…C46254。source hash和真实probe/所有权/原predicate/helper委托已核，相关scoped diff-check输出空/0；新Fixture/no-index格式需按实际新文件检查，未独立linter/typecheck/Build/GUI。没有本诊断版动态PASS，也没有保存/恢复结果或新GUI Auth安全结论。真实诊断结果出来后按第一有意义阶段定位最小修补，禁止靠放宽Wait/断言、重置数量或改环境把FAIL变绿。

## 2026-10-01 storage producer CRT日志捕获40行增量复审

本段只读Fixture **5DAC3852AB7CDD035175D5F6F3389087A64A7C4C254FFAD6EF58FC53C14878E6**（81194 B、1352 CRLF/noBOM）与implementation-report **0716F9D461C57834DCE275D3904FC1366206FF8E41E71445AFE4D32EA4E43DE8**。在内存中删除io.h include、PrepareStorageProducerLogs块、入口调用三个新增段，实际重新SHA精确得到已审 **9671E99A4A74EE0132559825FF6AB799F2FE05C0333AA328EDC2F2B816D6BA58**；不是只信报告说40行。Host E502/BAA及AutoSave A2AF/CC20再次核原样。

**STATIC GREEN / 增量CLEAR_SAFETY，仍仅原same3有限诊断复验：C09-desktop-release、C11-natural-close、C10-ppt-release。** 五storage producer的捕获函数范围有代码约束，其余Auth/C3-A/reader直接return不重定向。没有源内新阻断；新完整Debug0、当前candidate来源/相关回归与所有writer停写门仍未满足，不授权半写构建或新GUI/F流程。

### 真顺序、CRT和OS语义

- entry:1324第一IsAuthorized检查保持，1325才PrepareStorageProducerLogs，随后才State/Host/Window/fault/worker。其内部switch只五storage producer；失败打开/校验/分配的90发生前没有业务owner需要关闭。Root外层已auth private run/ancestor/EXE/path leases仍持至返回/死亡，两个固定run直接子文件没有外部参数。
- 已实际读本机SDK 10.0.26100.0 UCRT源：freopen.cpp:120–127 secure wide调用_SH_SECURE；:57–76在旧stream锁内重建FILE、openfile.cpp将真实新fd赋回FILE._file，不假设GUI默认fd1/2可用。corecrt_internal_stdio.h:636–650的N→_O_NOINHERIT、x→_O_EXCL仅w允许；lowio/open.cpp:263–276含TRUNC+EXCL仍CREATE_NEW（不会先创建再w截断），:307–311写_SH_SECURE share0，:695–698真实security属性不继承。
- Fixture分别检查reopened==原stdout/stderr、_fileno非负、_get_osfhandle真实DISK/file info无directory/reparse、无HANDLE_FLAG_INHERIT、大小0，再_IONBF。任一步不成立沿90 FAIL，不继续做场景或伪证输出。
- 没有SetStdHandle/dup2/新增继承名单/POD/argc/普通产品print；文件HANDLE由CRT持有，无本函数局部Close/fclose。成功自然返回时已有业务/Window/helper/辅助owner真join，CRT正常退出关闭；hold/error强退时保持到child死亡。没有让活worker借用提前关闭的FILE。CRT重开即便失败影响该child stream，也在全部业务之前，parent仍按FAIL，不触用户console/Office/配置。
- 现有CRT/stdout/endl/fprintf内容沿原代码。capture引入真实日志I/O/无buffer成本，仅explicit auth测试候选，不用于性能/首次落笔结论。诊断可能背压、capture不可用也不能升级PASS；原父exactHANDLE/POD/error90/时间上限与failure证据不变。

### 现失败口径和Verification

same3仍真实父65/child90；Desktop当前2accepted/1committed/1failed/0pending与probe1/1/1只证明read阶段返回，不证明Valid；PPT actualSaveFailed1也不等于初次合法NotFound。新文件取现有stage/status实证，不从新捕获名称猜保存实因、放宽条件或覆盖旧数据。新capture版尚未编译/运行，没有保存/恢复或新Root/GUI授权PASS。

独立no-index Fixture diff-check输出空（新增diff预期exit1），精确源重构/hash及SDK/实际句柄顺序核对已完成；reviewer未Build/run/GUI/自修，仅追加同一报告。RootSS1270/C5F4已编译的包装层旧结论仍限共享工具，不扩Auth/F许可。root在完整新Debug0之后才能same3复验并读取本轮CREATE_NEW日志，实际结果再决定最小修补。

## 2026-10-01 五源精确I/O diagnostics增量复审

只读新增Desktop/PPT私有I/O诊断及fixture finite setter；不源修/Build/GUI或递归，仍仅追加同一报告。已读actual diff、writer/CommitIndex/SavePresentation/SaveVersionedUInk两个callpoint和fault来源，而非因log字段名批准。

**STATIC GREEN / 增量CLEAR_SAFETY，限same3取证：C09-desktop-release、C11-natural-close、C10-ppt-release。** 新candidate尚未编译/运行；须B3及所有writer停写后新全Debug0/当前EXE来源/适用回归，再same3读actual numeric stage/error。原真实FAIL、历史private文件/数据保持；本批没有索引/持久化算法修复或新PASS，不凭unsupported flag/路径长假设归因。

### 实际默认行为和错误源

- Desktop/PPT existing fault末尾各新增bool false；旧aggregate前置字段和不足字段默认兼容。唯一true setter仍first-auth fixture RunDesktopStorage三case、RunPptStorage两case；readers/fresh fault副本默认false。Host、底层UInk、SS/Main/RootHeader及普通配置/环境/POD/argc不变。
- AutoSave.cpp331–367私有临时writer仅新增可选DWORD*，null路径不额外读错误。CreateFile false、WriteFile false、Flush false均在原失败现场GetLastError，早于原Close/Delete；写成功但written0沿原失败返回、error明确0，不拿残留LastError伪造。Create/Write/Flush/cleanup次数与条件相同，无重试/等待更改。
- CommitIndex使用ProcessRequest同一fault快照。primary/backup选择失败记录真实IndexReadState；原temporary write失败用已保存error，temporary ReadIndex/ContainsRequest失败及真实通过各有numeric stage。原ReplaceFile/Move BOOL=false分支立即捕publishError，先于log/Delete；success-marker不代替最终ReadIndex/ContainsRequest。最后read失败也用actual enum，logical validation error0不是Win32成功证明。原ReadIndex四调用点、NewIndex/append/schema/backup/验证及publication API/REPLACEFILE flags不改，default false不新增print/clock/I/O。
- PPT SavePresentation的Acquire/SelectStorageTrack已判IoError/EnsureFiles失败三站点只有error_available=0的有限stage，未在没有已存错误的上层借stale LastError。first/create与update均传原fault bool至既有SaveVersionedUInk，原8次SourceChanged重试、SaveUInkFile调用/返回status/revision/relativePath保留。
- stage4只在原saved非Committed时，从原saved.status/systemError和diagnostics首个真实非零systemError取证，并记同一实际path.size() WCHAR数。没有catch后回填、expected字段或推测原因；saved.systemError为0就不宣称该主错误available，即便另有diagnostic错误也分别输出。不改底层UInk writer/路径/manifest/Win7/registry、pending/durable或数量/timeout。

日志在已授权私有CRT文件中取证，有同步stderr成本且可能背压；不用于性能比较或新关闭deadline证明，默认产品不输出。原writer/root/timer/context/gate/lease所有权和自然死/90/父kill FAIL合同沿前审查；没有因新增诊断提前释放资源或放宽保存/readability门。

### 当前源身份

| 文件 | SHA-256 |
| --- | --- |
| Fixture | 9DBB4C60B1AC58EA8813F180B03AE06C9BD6251D01EC2CB7444F4B6437DE568F |
| AutoSave.cppm | A5833346838F2E47732116612A2CA3499B7A7FF1634B3A46BA3D826F382D5962 |
| AutoSave.cpp | 02C246B674C482E3C3C47346848F8FFB426D94EE1167B473A6DEA18809329432 |
| PresentationAutoSave.cppm | B54E83A5DA1993FE6487D275E84736E8A5D539DB35754F816C0E0E705DCB937A |
| PresentationAutoSave.cpp | 6D0FD0A7659379D77E7A3686DABD7FFE08FF9ECA8289F6044436CCC0E3971A8F |

implementation-report实际D8E75404D508E0E922D0C5F8C18C88B2C43AC2EF1CFDE52C495873254FE2F5EF；Host仍E502/BAA，SS仍1270/C5F4，Main892DB044…B218DC89。逐hash核，不用旧5DAC/CC20源指纹替当前。

### Verification / 未验证项

相关tracked scoped diff-check输出空/exit0；新Fixture需no-index对应检查。实际field默认、所有日志启用callpoint、native错误捕获及保存返回/流程保持已静态核；没有独立linter/type-check/Build/EXE/GUI。

现private日志仅已证首A Committed、第二Desktop index-commit Failed，PPT IoError rev1。Desktop203 WCHAR与重复GUID已由root调查排除；PPT temporary271仍hypothesis待真实error等来源，不从微软通用说明或flags名称升级归因。新诊断可能确认更精确失败站点，但没有声称已修复自然产品死锁、持久化或恢复。same3取证之后再按实证决定最小下一步，任何90仍FAIL；新F/B3/RTS/Win7/visible recovery/Restart不由本段授权或关门。