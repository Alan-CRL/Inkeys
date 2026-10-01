# C-P1 failed-cleanup primitive 夹具运行前独立安全复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。只读审查 `failed-cleanup-harness-design.md`、实际 ShutdownSupervisor/Main、两个 RED helper、旧信封与窗口/调度唤醒边界；没有修改源、工程/spec/账本，没有执行 Git、构建、EXE/GUI或递归派发。仅写本报告。

## 结论与准许范围

**CLEAR_SAFETY：root 可先在本仓根、完整最新 Debug|ARM64 构建后，逐案运行 RED `--shutdown-supervisor-tests --failed-cleanup-only expiry` 与 `allocation`。** 该精确 purpose 无窗口、不进入普通业务，只启动/结束本轮 copied child。RED 的 expiry 上限后 parent清理应记录FAIL，allocation旧无保护返回0也应FAIL。不是C primitive GREEN或C-P2产品生命周期验收通过。

其余列出的有限scenario没有发现新增GUI/用户数据安全越界，扩大运行前仍需核绿色helper实际monitor/State/HANDLE实现和下列测试逻辑门；本报告不将尚未实现的monitor真join写成已经确认。正式Host/Window/RTS/presenter均未接C信号，UInk和成功ULW反例仍在C-P2。

| 当前审查文件 | SHA-256 |
| --- | --- |
| `FailedCleanupDeadline.h` | `7B01FBC0F4831303CC1188E2E71A0A44C3D9CAE00EB20549CA4B26D6C94B428F` |
| `FailedCleanupDeadline.cpp`（RED） | `01D80F65426CBA971C78961D3AF81C2D34817514F0240C789A4A78A8D31D0FC7` |
| `ShutdownSupervisor.cpp` | `8EE46AD9663520EE092BDA983CEA03B119914122FA87F8C1B46E11F4B6BD1F9A` |
| `ShutdownSupervisor.h` | `4213E8342FD46B36961E98BA50F550978C86B7302542D03EA64B253F7BB1EBE1` |
| `IdtMain.cpp` | `FA9282CE30B381696001F5A4CDAEA8752D1C16C317491D72775C6DFD921F3BF8` |
| `failed-cleanup-harness-design.md` | `DE30420B552065B124F2878E626264DFE0469AA6505AF940996AD5EEDC69E1A1` |

## Findings (fixed)

本 reviewer 未改源码；当前没有需要为首批RED先修的安全阻断。测试完整性问题见下文，不能据安全CLEAR将其隐藏。

## 实际安全边界

- **独立信封/有限入口**：新常量purpose `--inkeys-internal-failed-cleanup-child-v1`，magic `0x1430FA03`。CleanupPrimitiveObservation固定96B、graceDeadline offset32，不复用旧48/128B。parent清零后填写version1/bytes/scenario；child要求argc9、有限scenario（当前含Earlier共八项）、DWORD/uintptr范围、magic/version/size/scenario及authorized未发布。mapping必须实际能MapViewOfFile 96B，拒绝后Unmap/返回专用非零。
- **精确身份**：`RunCleanupPrimitiveChild` 1706附近核三个HANDLE继承位、GetProcessId(parent)==指定PID且非self，实际parent image与给定绝对launcher路径的volume/file index同一；HasExactFailedArmChildIdentity核private root/bin非reparse/current copied image为bin/Inkeys.exe且实际文件identity同一。所有fault bool、global observation和gate只在全部通过后建立；不接受普通env/config故障开关。
- **永远early return**：`TryRunShutdownSupervisorEarly` 2140附近对新purpose不论认证成功/失败都return true；`wWinMain` 674附近首业务调用即返回exitCode。它不会像旧已授权startup-failure child那样继续真正产品启动。参数解析失败、未知scenario/父selector参数均非零结束，不调用配置、单实例、窗口、Office、更新/自启/shortcut/DDB/SuperTop/全局输入启动。
- **目录/进程所有权**：MakeUefTestDirectory从含InkeysRepo.sln/.trellis的仓根创建唯一TestResults/release-hardening/root/bin，逐级非reparse，CopyFile不覆盖；child工作目录是copied bin。parent继承列表只含自己的exact进程HANDLE、ack、独立mapping，CREATE_NO_WINDOW；omitInheritedHandles负例不给继承/attribute list。TestChildGuard和显式超时清理都仅TerminateProcess这个hProcess，不按名称查杀或操作其它Office/Inkeys。
- **parent上限不冒绿**：ack最多5s、case25s（expiry40s、负例3s），dead在清理前锁存；任何parent强杀不改dead=false，正常通过要求原dead=true/专用退出码/真实publication/sentinel。wrong-parent预期84、无继承预期83且零activated/published/durable。删除只针对本轮copiedImage，未递归清理未知文件。
- **Main无等待publisher**：`PublishFatalFailedCleanupNoWait` 299附近只CAS0→Close或保留原1/2/3、已构造processService的BeginShutdown原子store、普通1/2 offSignal原子store、WakeForStop、PublishedShutdownDeadlineTick原子read。未调用Arm/CreateProcess/CreateThread/隐藏队列/logger/CrashHandler/磁盘/COM/业务锁。Window::GetService返回已在Window.cpp140构造的对象；BeginShutdown211只原子写；当前Scheduler::WakeForStop1062只检查既有event并SetEvent，未创建渲染设备/窗口。
- **普通退出case的明确例外**：ordinary显式调用真正SetOffSignal(1)，所以会走既有监督helper和正式关闭前奏；它仍发生在新purpose的early子进程、产品owner未启动时。其RequestHide/CrashHandler shutdown/Magnifier stop是原正式路径，不应被概括成新无等待publisher的一部分。其它case只使用独立scope/静态publisher。
- **join gate保活形状**：只有Join创建child-local manual-reset entered/continue两event，continue保持unsignaled，传给FailedCleanupTestGates；两个Handle先于scope声明，Signal按值持State，fatal路径noreturn时所有局部栈/映射都保留至死亡。wrapper只在首publication以Interlocked填POD，WaitForSingleObject(entered,0)取monitorPaused，无文件/日志/业务锁；没有在parent假造WAIT_TIMEOUT。GREEN源码必须确认这个event真由Cancelled monitor在退出前发布，并真实等待其thread HANDLE超时，不能仅设置event作假。
- **RED边界**：两个helper当前没有event/monitor/control CAS。allocation故障使Prepare返回空State、Complete继续，afterComplete=1→普通0，parent拒绝001B；expiry Begin返回0而后真实child Sleep(INFINITE)，parent上限后owned清理FAIL。HasPublisher/owningSignal为接口占位，不证明绿色寿命；当前未安装产品scope。

## Findings (not fixed)：测试逻辑与最小补充

以下T01/T02为初审发现，已由文末新源码增量关闭；T03–T05仍保留。首批RED安全放行与当前GREEN实现/测试门分开。

### P1-T01：expiry需按原绝对deadline验收，并让重复Begin有时间差

当前expiry只要求graceDeadline>activateTick、publishTick>=graceDeadline、exit001A；通用fatal门按death-publish约14–21s。过短grace也可能PASS，尚未证明固定15s grace。正确合同的最终截止是原graceDeadline+15000；若expiry线程迟到才发布，正确接管只等原剩余时间，现按publisher重新要求至少14s会误拒正确实现。

最小修改：expiry单独验证grace约15000ms（明确短测容差），按原grace+15000验死亡/原截止，不从publish重新起算；publication是观察点。重复Begin现在连续执行可能处在同一GetTickCount64 tick，错误重置也会返回相同值；等待明确间隔后再Begin并断言原tick完全一致，过期/消耗路径同理。prepare/join仍可按其首次接管tick检查。该问题不影响先采RED安全，但须在GREEN判决前关闭。

### P2-T02：ordinary是组合监督反例，不能单独认证helper取更早tick

RED ordinary也可由原成功Arm的0015/0016约15s杀死并满足现父断言；helper可能完全没有作用。它可保留为“加入scope后既有普通监督未被破坏”测试，不能宣称已动态证明scope自己的ForceAt取min。

增加一个不被独立旧监督掩盖的同生产helper用例：进程寿命测试publisher返回已知更早绝对tick，或用受控纯值deadline见证核helper真正采用tick；保证错误重加15s明确失败。同时源码审Published getter和helper min。无需停掉/修改真实普通监督来制造绿色。

### P1-T03：取消竞争/下一scope隔离尚无动态用例

Cancel当前仅Complete→旧Signal.Begin()==0→等待16s，没有创建下一scope；Dormant只检16s无失败计时。未用header已有afterBeginClaimed/beforeExpiredClaimed事件覆盖Begin赢CAS但SetEvent前cancel、Cancel/Expired两种CAS赢家、迟到旧Signal碰下一代，以及wake HANDLE最后引用寿命。

最小补充：同生产helper以显式barrier分别暂停Begin CAS后和expiry CAS前，控制两种终态赢家；建立第二个Dormant scope，旧Signal不得改变它。事件/Signal/state持有到全部真实join，失败noreturn反例用owned child；成功用实际wait返回。不能拿当前顺序cancel或到期强退代替这些已冻结C00寿命门。

### P2-T04：join/prepare现场见证与GREEN实现还需核对

Join的monitorPaused证明entered event已发布；它不能单独证明thread仍持State、thread HANDLE未被close/detach、真实join的1000ms等待确已执行。最后绿色helper源码要核失败后保留HANDLE/State/栈且noreturn，并结合真实monitor gate；若需动态寿命断言，可增加默认空的析构/退出纯值见证，不加业务上下文。

allocation/monitor目前都验001B+publication/不返回，fault flags确在认证后设置；但没有逐失败站点已命中的独立见证。绿色代码须核forceStateAllocationFailure/forceMonitorCreationFailure分别触发实际Prepare分支，失败不能靠其它资源错误碰巧同码。version/bytes/scenario失败在源码拒绝，现父负例仅wrongparent/noinherit；建议补至少一个坏96B header拒绝反例，不让旧信封偶然授权。

### P2-T05：parent观察/PASS边界

正常green在child signaled后读结果，Interlocked publication有完整POD边界；timeout时child尚活，未发布的64位字段仅能当raw诊断，不能视作有效tick。原dead=false已阻止PASS；分析端仍须按activated/published有效位读取，避免将未发布/途中值解释成正式deadline。未执行、parentcleanup和sentinel保留都不等于UInk/Host恢复。

## D005观察字段与旧NoHold的界定

当前Main2220附近actualPptComResult仍在合成RecordPptComFailure前锁存：activation/module真实成功为S_OK，否则用已记录实际错误；仅给授权观察包。originalError、正式Arm/日志/提示、COM清理/return1保持原变量/顺序。新C publisher增加使Main整体hash变化，不改变该字段合同。

旧NoHold四案普通1/0与875/828/782/547ms退场证据已保留；D005旧raw8007001F是合成错误观察，不能作自然DLL失败证明。修后字段仍未有本轮新Build/复验，不记动态PASS；本报告不重跑或覆盖旧结果。

## Verification

- Lint/TypeCheck/Build：本 reviewer未运行；root独占完整InkeysRepo.sln Debug|ARM64、原生ARM64 MSBuild、同调用PATH规范和至少5分钟构建预算。当前身份仅静态检查。
- Tests/EXE/GUI：未运行；这是首批RED运行前安全CLEAR，所有GREEN、race、真monitor/HANDLE、D005字段和C-P2仍待上述实际证据。
- 不改变状态/commit/push/归档或发布门。后续源修改重新冻结并审相关增量；当前概念/RED实现不能升级为生产生命周期修复。

## 2026-09-30 首批RED与验收增量复核

root已记录完整Build0；C expiry父64、child17348认证/activated1、grace0/published0、40s仍活后owned cleanup FAIL；allocation父64、child30380跨过Prepare、afterComplete1并自然0，FAIL。鉴权负例PASS。同期Headless仅B01/B03/B04/B05四个UI3新RED失败exit1，不误写整个Headless绿色。本 reviewer未运行这些测试；动态记录由root提供，源码核对与上述预期一致。

新harness/source身份已更新于上表。**CLEAR_SAFETY保持，包含新增有限earlier场景**，root须按新源码完整构建后运行；两helper GREEN_IMPLEMENT授权不等于本报告已审其最终绿色实现。

- **P1-T01在验收源码层关闭**：expiry分支现在独立要求grace-activate在14990..15500ms，publish>=原grace，死亡>=grace+15000且迟到不超6000ms；不再套death-publish至少14s。child先Sleep250跨tick，再核重复Begin原deadline。未改变auth/普通业务；动态绿色尚待真正helper。
- **P2-T02在独立用例层关闭**：新增enum/parser/option/父cases中的Earlier，只在已验真child设置forceStateAllocationFailure、`g_cleanupEarlierDeadline=GetTickCount64()+6000` 和injectedFlag1。静态wrapper先调用真实Main publisher，才取显式更早tick；不Arm普通监督，不改生产getter。parent要求published/afterComplete0、001B、injectedFlag1、死亡在返还的绝对tick至其后1000ms，错误默认15秒自守会被拒。这是helper取min的合成deadline反例，不叫普通Close实际已发表的6秒合同。
- **ordinary已诚实降为组合反例**：仍由原正常监督验证15秒，设计现在明确不能独立证明helper min。
- **T03/T04/T05保持**：下一scope/Cancel-Expired CAS交错、绿色真实monitor HANDLE/State不释放和失败站点见证仍需补强/审代码。现design已明确cancel有限覆盖，不宣布C00/C-P2全部完成。

## 2026-09-30 C00新版实码/绿色原始结果

最终96B caller SHA-256 `D7E6B60E494339FCC31C58B3B45E1044604F0F2CD9F7B21159913108E6CB11D9`；两个helper当前绿色身份为Header `1D8DB0010A388CC21522F1D0F9CC7858CB8D59DFCB928431C6C3112A94AAD0A0`、CPP `95932CE0ECBCEE120F884FD834A9A72E3A85479A296420CD6D87BF9239F37F4C`。以上初审RED/8EE身份为历史；本增量CLEAR_SAFETY不复用其指纹。

实际新caller增添有限begin-cancel/expiry-cancel，cancel建立真nextScope。静态owning Signal在producer CreateThread前写，只在真实producer HANDLE signaled后清；Begin停CAS后，Complete先cancel/join monitor再放producer。producer未join则当前process自Terminate87，不先析构借用gate/State。expiry-cancel只停monitor，Complete独立Expired接管并保持原grace+15000。所有gate在child局部持有至join/死亡，auth/early-return/no-GUI范围保留，**CLEAR_SAFETY**。

已读取十组 `c-p1-<case>-green-debug-arm64.{stdout.log,status.txt}`，父自然exit0、各负例84/83 PASS。expiry28024原grace15000/总30047ms/001A；allocation31096和monitor31084约15031ms/001B；join27440真实monitorPaused1/afterComplete0，prepare+管理约1016ms后发布、再15094ms/001C；ordinary31320原tick+15ms/0016；cancel12592两个scope+lateSignal witness1/普通0超过16s；dormant32204普通0；earlier27108合成6s+31ms/001B；begin-cancel21868暂停1/afterComplete1/witness1普通0超过16s；expiry-cancel29444暂停1/afterComplete0/原grace+15015ms/001A。

T03下一scope/两个已设计竞争方向和T04真实monitor gate+noreturn寿命结合当前代码在本机primitive范围关闭；不是所有OS失败已动态注入。C-P1实际代码GREEN见 `failed-cleanup-code-review.md`，C-P2真正Host/RTS/Window/存储/正常ULW与fresh UInk仍未接线/验证。reviewer未运行，只核代码/原始证据。

D005修后raw/status也已核：natural父0/pid26972、child7096 S_OK/普通1/875ms；hold父0/pid21984、child27736 S_OK/0015/15032ms，各负例PASS。该观察字段动态缺口已关闭于授权合成site，未升级自然DLL失败或UInk恢复。
