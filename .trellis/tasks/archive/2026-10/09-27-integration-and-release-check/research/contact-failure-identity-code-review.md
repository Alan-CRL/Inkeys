# E03 初始化失败接触身份：独立实际代码复审

日期：2026-09-30（Asia/Shanghai）。Active task：`.trellis/tasks/09-27-integration-and-release-check`。已重读实际check context/PRD/design/implement、输入Closing研究、E03设计与设计复审、Controller真实diff、三失败调用者及Coordinator状态机。只读源码与既有日志，不构建/测试/GUI，不改产品、实施者文件或共享账本。唯一输出为本报告，任务仍in_progress。

## 结论

**当前实际E03源码静态复审GREEN，未发现本补丁新增的阻断性正确性或安全问题。** 三个真正的初始化失败调用者已复用精确handle Discard，成功路径及runtime/speed-eraser清理保持原样；新probe调用同一个生产helper，未复制一套正确算法。主会话完整Debug ARM64Solution与真实产品CLI绿色均自然exit0，raw日志已实际读取，结论限于本机这一共享helper与身份/寿命合同。

当前Controller Git blob `07af3dd61ce85cf7bae500afa52087996db5d157`，SHA-256 `57bc84e47c40e24a20fb246b178bc18ed7626319bb7a31ffe6a442f4e0ce6597`。基线HEAD `e32a5fc06096c1e4ab88a29866c60ebe323fd722`；本单元只改 `Draw3.DrawingController.cpp`，E01/Main/header/ContactInput不由本worker修改。

## Findings (fixed in candidate)

### 三失败调用者不再按key取消新接触

- `RejectStrokeInitialization`（Controller.cpp:1839–1844）只调用 `input.DiscardUntilTerminal(handle)`，不读取record中的新key/Down，不找当前同key producer，不伪造物理Cancel。
- `initializeStroke` 的三处调用为 acquireStroke空（5900–5904）、modeler.Reset失败（5994–6002）、modeler.Update(kDown)失败（6050–6058）。原来重复的key Cancel及后续Recycle全部替换为同helper；没有遗漏某处或保留第二次Recycle(Producing)。
- acquire空仍直接return false，没有未取得runtime的释放。Reset/Update仍逐句保留错误输出、cancelled标记、handBackSpeedEraserController、handle清空、inUse=false、return false。成功初始化/modeler/sample/pressure/width/cursor/history路径没有随本补丁修改。
- helper保留未命名 `const ContactSnapshot&` 作为真实red→green最小接缝；不读取该参数，没有新增复制、分支或资源开销。它是内部函数，不构成公共ABI变更，本次不要求审美性质的参数删改。

### 地址和generation共同决定route身份

Coordinator生产代码未改；`ContactInput.cpp::DiscardUntilTerminal:858–882` 使用 `TrySetExactState(record,generation)`，不是generation数值相等即同接触，也不按keyCancel。

| 状态 | 精确拒收结果 |
| --- | --- |
| Producing | Quarantined，槽仍占，后续Move拒绝，真实Up/Cancel经Close最终释放 |
| Closing | ClosingDiscarded，立即交出consumer handle，暂停的唯一Close producer恢复后释放 |
| ConsumerOwned | 原Recycle精确转Free，释放旧A，不触碰另一record的新B |
| Quarantined/ClosingDiscarded | 重复调用有界no-op，不重复计数 |
| stale generation | CAS失败且route代次检查拒绝，不触碰复用槽的新C |

`Close:564–640` 的两终态CAS与ReleaseSlot/计数由唯一成功owner执行。新helper没有把仍物理按下的route提前free，不能使后续Up误归到槽位新一代；旧A已终态时仍允许单次正常回收。

## 新测试实际覆盖与夹具寿命

- `RunRejectedStrokeInitializationProductionTest:3345–3526` 在现有真实产品CLI里运行；Parked入口（3917–3919）把本probe的返回值加入最终退出码。它没有独立模拟正确Discard算法或改Main/工程/register；所有case直接调用上述生产helper。
- I01用真实FIFO产生A.Down/A.Up/B.Down，断言record地址不同，检查B Down/Move/真实Up不被旧A失败取消；随后复用A的同地址新generation C，重复旧A helper/Recycle/读不得改C。测试没有错误要求跨record的generation一定不同。
- I02分别用Up/Cancel，核失败Producing仍occupied1/recycled0/Quarantined、快照和Move拒绝，重复拒收不释放；物理终态后occupied0/recycled1/terminalPublished1/Quarantined0，再验证下一合法Down/Up/释放。
- I03使用既有真实Close CAS后pause hook，对Up/Cancel各运行一轮；等待consumer100ms时producer仍暂停，断言handle不可读且slot仍占。无论consumer观察成功/失败，都先resume再join producer和consumer；pause未entered分支也先resume再join producer。断言写在join后，取消hook后再核唯一terminal/回收与重复Recycle。没有SuspendThread、detach或正常断言失败后提前销毁pause对象。
- 线程/内存创建的系统异常会按本noexcept测试函数的既有进程错误边界失败，未承诺系统资源耗尽时夹具仍可返回PASS；该显式CLI不在正常产品初始化调用路径，未由此增加生产挂起入口。

## Findings (not fixed / evidence limits)

未发现本次实际diff必须修补的新增问题。保留如下范围：

- 本probe验证三个真实失败分支共用的拒收helper，不实际使真实acquire/modeler.Reset/modeler.Update逐一返回错误。因此三个caller的runtime/speed-eraser清理目前是实际diff/call-chain静态证据，不写成三个完整故障分支动态通过。
- I01主要核旧/新身份与occupied槽；I02/I03核精确terminal/recycled计数。未单独给I01所有A/B计数重复添加断言，不据此宣称其它生命周期已证明；状态机独立F057测试另有回收合同覆盖。
- I03旧版本因F057 RecycleClosing已具有有界交权可能本来通过；不能把它记成E03新修复的红→绿，也不要求全部新case红。
- 普通Controller::Run/PPT AdmissionRevision→旧页收尾→成功Present/B ready→历史/UInk身份属于独立fixture，本helper绿灯不能替代。AbortUnqueuedDown、RTS callback静止/Host Reset、Win7/真Touch/Pen与用户现场根因仍分别保留。
- 本单元不改成功样本消费与模型更新率，没有省略必要输入换取性能，也不开放关闭的功能gate。没有真实GPU/输入到像素、可比HC/H2体验或UInk恢复结论。

## Verification

### 本reviewer的只读核对

- `git diff --check HEAD -- Draw3.DrawingController.cpp` exit0；实际Git blob与worker冻结SHA-256一致。
- 文件保留UTF-8 BOM、10979行CRLF、裸LF0；无全文件格式diff。
- Lint/TypeCheck/Build/Tests/GUI：按分工未独立运行。

### red证据

已实际读取 `TestResults/release-hardening/e03-contact-identity-red-debug-arm64.{stdout,stderr}.log`；主会话完整Debug ARM64Solution红build exit0、产品CLI PID17248自然exit1，退出码由主会话确切进程等待交接。build日志末段0 Error/10 Warning已核。

旧DesktopExit/FatalClosing/FatalActiveInk/LaserIgnoredTouch四个tag仍PASS；新I01的B错误取消、Move/Up失效及stale复用失效有5条FAIL，I02两种物理终态的提前释放/重复拒收/真实终态回收有6条FAIL，共11条；I03未新失败。这与H0三段原key Cancel/Recycle的可达行为一致。主会话在red阶段只提取旧生产逻辑和三调用点，green只改shared helper；当前真实diff显示三调用者与probe确实共享同一函数。

### green证据

已实际读取 `e03-contact-identity-green-debug-arm64.{stdout,stderr}.log`，旧DesktopExit/FatalClosing/FatalActiveInk/LaserIgnoredTouch四tag均PASS，新增 `[Draw3InitRejection] PASS: shared production failure cleanup identity and lifetime`，无新FAIL；主会话确切CLI PID34784自然exit0。主会话完整Debug ARM64Solution green build exit0，`e03-contact-identity-green-debug-arm64-build.log` 末段0 Error/10 Warning已核。冻结源码SHA-256再次读取仍为 `57bc84e47c40e24a20fb246b178bc18ed7626319bb7a31ffe6a442f4e0ce6597`，运行后没有产品修补。

red PID17248自然exit1→green PID34784自然exit0证明共享生产拒收helper的11个旧失败断言关闭，同时保持原4项回归；不把它扩大到完整modeler失败分支或普通PPT页边界。严格Headless仍由root执行，Release三架构和hidden最终矩阵尚无本轮结果；任何后续产品blob变化需按影响增量复核。
