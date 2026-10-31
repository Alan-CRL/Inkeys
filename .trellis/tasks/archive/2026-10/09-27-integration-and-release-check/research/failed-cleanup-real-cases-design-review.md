# C03/C05/C07/C08/C09/C10 真实夹具合同独立设计复审

日期：2026-09-30。Active task：.trellis/tasks/09-27-integration-and-release-check。本 reviewer 按分工只读新合同、当前 helper/Window/Main/Host/AutoSave/PPT/UInk、已审 C-P1/C-P2 与父最新交接；未改产品、Spec、工程或其它报告，未构建、运行 EXE/GUI，也未递归派发。仅写本文件。

审查对象为 root 修订后的 failed-cleanup-real-cases-contract.md，SHA-256 **6EE00C0A97B7B16308C2DAD644AF2807EACD138F4960DD4C04B104598D34E0F6**。同时已核实际新普通头 FailedCleanupRealCases.h，SHA-256 **9EC25422BC7CEBEC10F3CD686D559E91E0A68F09EE44A0FF06FAF5E3108674D4**；目前只有声明/静态断言，尚未编译或实现 authorizer/runner。不能将这些新名字、CLI 和 1024B 协议当作现已可运行。

## 结论

**GREEN_DESIGN。** 修订后六类场景能落到真实生产 owner/worker 与严格存储读者，有明确成功/放行反例、自然截止判定、独立新信封和文件/事件寿命。没有剩余阻止本批最小实施的设计问题；可按唯一 writer 分工进入源码实施，完成冻结后的实际源码与运行前 safety 审查后再运行。

本结论不是运行许可、源码 GREEN、1024B 三架构编译 PASS，也不升级 C-P1 十 primitive 或 C-P2 的既有 Build/Headless 为 C03–C11 的动态结果。C04/真实 RTS callback 静止、C06 独立旧 owner hold、Restart/UEF、Win7/真实设备和用户现场根因仍分别留门。

## Findings (fixed)

下列均由 root 在本合同中修订；reviewer 未修改合同或源码。

| 问题 | 实际证据 | 已核修订 |
| --- | --- | --- |
| 新 bool 默认成员初始化会破坏 trivial | 当前 FailedCleanupDeadline.cpp:13 断言 is_trivial_v；带成员 initializer 的 gates 不再 trivial | beginGateAfterWake 为 plain bool；调用继续 gates{} 零初始化；保留 trivial/standard-layout、三架构 size/offset 门 |
| C03 双 monitor 不保证 publisher 只调用一次 | Presenter local 与 terminal outer 各有 State/monitor，同一原 tick 均可过期；现 FatalAt 每个接管者调用 publisher | 允许幂等多调用；只核首次真实意图 CAS/首 tick 见证单发布，不按函数调用次数判 FAIL |
| 单槽 observed 与 release 的 A→B 覆盖矛盾 | 1024B 只有一个 child-owned observed Receipt，原“BaselineReady 后不可再改”与最终 B 要求冲突 | hold 只在 BaselineReady 发布 A；release/C11 的 A 保留在 fixture 私有值中，最终 B 在 Finished 发布一次；reader 在 ReaderDone 一次发布 |
| C05 使用包内不存在的字段名 | Trace 只有 oldDrawpad/oldSignalCancelled 等，没有 failedDrawpad/afterComplete | failedDrawpad 明确映射 Trace.oldDrawpad；完成用旧 Signal 取消、真实 Start/Stop 返回及管理 join 表达，不扩包 |
| Extra 摘要没有冻结实际递归类型编码 | uink_model.cppm:52–62 的 Extra 是 MessagePack Map，含 11 种 variant，不是字符串 | 每值有确定 variant tag；Map 原序 key/value、Extension type/payload、深度32和溢出/非 finite 拒绝；不排序/JSON化 |

## 真实调用点与最小实现核对

### Begin 必须先唤醒 monitor 再 hold

当前 helper.cpp:201–204 是 CAS→原事件门→SetEvent(wake)。永久停原 pre-wake 门时，monitor 可仍在 Dormant 的无限等待；这个前提已实际核到，不能靠父强杀、到期 Complete 或另调 SetOffSignal 制造 C03/C05 PASS。

新增 beginGateAfterWake=true 只给 exact-auth 新 child 的 scope；成功 CAS 后先执行真实 SetEvent(wake)，再复用同一 afterBeginClaimed/continueBegin 对暂停实际 owner。false 的旧顺序原样保留，C00 begin-cancel 仍测试 CAS 后/SetEvent 前 wake 强引用寿命。SetEvent 失败沿原 noreturn 管理失败，不发布假 fault-reached。所有 gate/State/业务 context 保持到真 join 或进程死亡。

Win32 gates 28B/首 HANDLE offset4、x64/ARM64 56B/offset8 是本次布局目标，不能只按手算记 PASS；新增 bool 占既有 padding，仍需三个架构的实际 sizeof/offsetof 编译，以及十 primitive 回归。用 gates{} 达成默认 false，不能靠删除现 trivial 断言绕过。

### 六类场景与对应反例

| 场景 | 真生产停点/前提 | hold 断言及最小反例 |
| --- | --- | --- |
| C03 presenter | 真 ULW required ConfigureWindow callback 返回 false；GraphicsReady、真实 style masks；Presenter local Begin→outer Begin 后 owner停在 after-wake 门，早于失败 cout/握手/释放 | 原 grace+15000、001A 自然死，Start/Stop 不返回、不新建下一代；release 放门后真实 Start=false、GPU/worker/附着和旧窗清理、Complete 真 join，超过旧 grace 仍活再普通0 |
| C05 before/created | required Drawpad.beforeCreate=false 或已创建 owned HWND 后 created throw；真实 RollbackCreation Begin，停在 promise/DestroyWindow 前 | before 的 oldDrawpad=0且不要求该 role destroyed；created 必须有有效 hidden HWND；hold 原001A，无第二代；release 真 rollback/DestroyGroup/failed StopUnlocked join、全部旧窗无效、Signal0，超过旧 grace仍活 |
| C07 Main ULW | 从 Main:2630–2673 提取同一 private span，两调用者共用；首 Host 的全部 style 请求拒绝，真实 first StartProduct=false 后旧 Host/Window完全清理 | 有实际 preferDComp 前提；旧链销毁与 first Complete/意图0先于唯一新 Window/ULW scope；真实成功 Present，过旧原 tick后再一笔 ingress→consumed→Stored→成功内容帧；普通 Close 自然清理 |
| C08 render | Host.cpp:337–396 ObservePresented 真成功快照发布之后，firstFrameReady/content/history 与当前成功 revision成立，且非首帧；另一 thread真正 SetOffSignal(1) | 原 ordinary deadline、armState2/4、0015/0016自然死，真实 StopProduct未返回；release放门后同 Close/Stop 自然0、owner/monitor全部join |
| C09 Desktop | 真 Host 写A→Clear→Committed；新增 enteringWriteDelayEvent 在 AutoSave.cpp:654–656 的实际 Sleep前；第二B/Clear已accepted/pending但未committed | 60秒真实 worker延迟、正式Close后真实 Host.Stop drain，原15秒先到；fresh reader只读最后A index/UInk，receipt完全一致；400ms release和无fault C11最终B committed，再独立reader核B |
| C10 PPT | 真 Host/Controller whole-presentation保存、same-session baseline A；真实 worker在 cpp:1061–1067 的新UInk durable/index未发布事件窗 | 正式Close后原15秒先于原30秒gate上限，Host.Stop未返回；fresh service无pending cache，严格读旧A/Base index；同session成功、foreign session ConflictDeferred无snapshot；release索引真提交后读A+B |

C03不是强制 DWM 入口拒绝或模拟 graphics bool；C07不是同一 Host 内部 DComp→ULW。C07在无实际 DComp 前提时输出前提不足，不能伪 PASS；首代全部失败、旧四窗销毁边沿/owner join和下一代独立 scope缺一不可，不能用 HWND 数值不同判唯一 generation。

C08所选 hasContent 可以支撑“已 Stored”前提：当前 Controller:6292–6305 查 history.LastVisibleItem，首笔 CPU Stored commit 后才调用 publishCurrentPageContent（当前约11997–11999）；不是 Down/live一到就置 true。仍须把真实 terminal、completedStrokeKind、本轮计数和成功 content/output版本一起核，不能仅凭 reached 事件名或旧非零 Present计数。新门只有 hidden injection+成对事件下启用；无新逐帧 clock、通用 callback 或 Controller计量修改。

C09/C10 的 accepted、pending 和 Committed严格分开。PPT accepted 包含Load，不能将其差分等同Save数。Host hidden persistence getter只在已成功 Start、当前 generation和不并行下一Start的测试入口读两个 Diagnostics；不把新锁加到普通帧，也不无锁读 Stop会改写的普通bool。

### Fresh 生产读者与恢复 receipt

- **Desktop 缺口真实存在。** Start 会清 records，SubmitLoad:1128–1141只从本service Committed records找path；新service直接SubmitLoad不会扫描disk。合同选择 auto_save module内部的窄隔离读者，并由 WorkerMain与它共用 private ReadCommittedDesktopPath，既能复用真 ReadUInkFile/Import/interval投影，又保持普通 SubmitLoad/UI冷恢复合同。不得注入records、先重写同文件或把 Invalid当恢复成功。
- Desktop严格读者在现 root/date NamedMutexGuard内用 ReadIndex/ValidateIndex；有效primary优先，否则只读有效backup；Missing不 NewIndex、损坏不重建、只选已验证最高 dailySequence 的唯一entry。现 ValidateIndex:395–416已核严格递增、唯一GUID/path/session-sequence、canonical身份/简单文件名和regular file。Loader沿926–955的 Complete/provenance/GUID/每canvas interval投影；receipt必须有实际 sourceRevision、date/session/sequence/trigger和真实model值。C09的primary原始字节/摘要要保持，不用fallback能力替代本例前提。
- **PPT只沿已有严格Load。** cpp:1170–1198核storage session/key/binding/SlideID集合、sourceRevision、application-owned extra与workspace GUID；新的reader用同一auth sessionIdOverride和全新service，不继承Host pendingIndexEntries。Target的sourceIdentity/key由private固定path重新Resolve；bindingToken由真实origin PID/旧owned数值HWND和固定descriptor重构。旧 HWND只是身份数据，不授予发消息权利。completion.target的targetRevision/sessionRevision是实际请求回显，不能伪称为文件内独立记录的版本。
- 生产/reader两阶段分别判定：原进程自然截止和最后durable点完整可读性都成功才总PASS。hold A只发布一次，release/C11最终B只发布一次；parent确认exact旧HANDLE死后，用已封口receipt创建新的immutable expected。索引完整字节、UInk实际sourceRevision摘要、全部identity、SlideID/EndScreen、stored几何宽度和style均比较，孤儿新物理version不算Committed。
- UInk snapshot真实点字段只有 x/y/width，style含kind/opacity/RGB/texture；未声明逐原始pressure恢复。Extra递归tag/顺序/深度、全部operations/strokes与canvas身份用同一fixture纯值编码，只在封口数据上处理；不以端点抽样或摘要代替target严格匹配。不改UInk codec、PPT session校验、功能gate或普通自动重启恢复。

### 新信封/普通头与所有权

实际新头的 enum顺序正好1–17，旧 purpose/magic48/128/96B不改变；Header64/Trace256/两Receipt352合计1024，trace/expected/observed的offset分别64/320/672，authorized offset48。普通POD使用DWORD/LONG/ULONGLONG/固定数组/volatile发布字段，没有HANDLE、业务/module对象、bool或容器；alignment/trivial/static断言已写，仍是未编译声明。

VerifiedCleanupRealLaunch是进程内普通capability（scenario、packet view、private directory），不在映射传指针；头中的authorizer/fatal publisher/正式Close见证/stage发布与两个entry目前只是函数声明。它们不会因存在于头中就自动有鉴权，实码必须使唯一入口来自early exact authorizer。worker应核IsAuthorizedCleanupRealLaunch，非法/旧信封/unknown case不能落普通wWinMain或普通产品fault setter。

新增purpose必须严格核精确继承父HANDLE/PID/EXE文件identity、三个最小继承对象、ownImage/private bin/目录树非reparse、mapping magic/version/bytes/case、nonce/reserved/初始字段和reader expected范围；不能用PID/路径子串或旧96B校验放宽。实际authorizer和所有early错误分支还需独立源码/safety review，包括拒绝失效父HANDLE、无继承/错父/旧mapping/非法case且无窗口/fault/artifact。originChildPid和数值 HWND仅receipt，不OpenProcess或操作旧窗。

FixtureChildState强引用由runner、WindowSpec lambda、各thread envelope拥有；HostStyleCallbacks/startupContext借它的成员，只活到真owner/管理join或进程死亡。进程寿命 publisher只原子发布真实NoWait前奏/首见证，不能捕获stack、分配、日志、I/O或拿业务锁。hold不得析构活动context/事件/mapping；release/no-fault依次Stop/Window join/Complete管理join/辅助线程HANDLE signaled后才释放。若事件/thread前提或join失败且仍活，错误退场须保留栈直到自进程死亡，不以关闭handle/返回析构冒充join；父清自己child只记FAIL。

私有路径仅 run/bin与artifacts/C09/C10，reader复用copied EXE直到两阶段结束；不递归清未知文件、不接用户Office/Inkeys配置。最小四角色和private message只针对本child owned hidden HWND；没有全局输入/Computer Use。真正RTS可用和成功Present是前提，不自然失败就改环境或替provider返回成功。

## Findings (not fixed)

- **未实施/未运行：** gate-after-wake、真实新夹具、Main共用span、Desktop事件/窄读者、Host事件/getter、authorizer/parent runner和完整receipt validator尚未取得实码/safety/构建/结果，故没有动态PASS。root新头的布局仅经静态字段核对，三架构编译未运行。
- **下一组范围：** C04 Disable/Remove失败与成功callback静止、C06旧Window owner独立hold不在首批enum；有明确后续分工，没有用C03/C05或watchdog覆盖它们。低层API未返回、内层日志/析构和catch前展开仍是原冻结未覆盖边界。
- **产品/平台：** 此处只判最后committed readability；普通fresh产品Desktop/PPT自动恢复、重新安装Controller后的可见恢复、Restart/UEF唯一新进程另验。Win7 SP1仅KB2670838/HW/WARP/透明/输入及Release完整门尚缺，FLIP与两DWM禁用/独立设备线程保持。受控gate/delay不称自然driver故障复现。
- 无剩余需要修改产品、公开业务接口或架构的设计发现；上述未固定项是下一阶段实码/证据及明确范围，未为其扩大当前修补。

## 实施与验收门

唯一writer按合同执行：root拥有Main/Supervisor/helper/新普通头/项目及所有build-run；一个worker只拥有新FailedCleanupFixture.cpp、Host.h/.cpp、AutoSave.cppm/.cpp。Window/Presenter/RTS/Controller/RuntimeMetrics/PPT算法不改；后续U2触Host需串行交接。模块内读者类型不复制进普通头，普通cpp用import调用真实module；不把主产品监督或真实窗口夹具拉进Headless，不创建新Solution。

实际源码冻结后先独立review鉴权/DTO/事件与所有异常寿命，再完整InkeysRepo.sln Debug|ARM64（原生ARM64 MSBuild、同invocation PATH规范、至少5分钟）、严格Headless/PptCOM和十primitive。release/no-fault先验证场景前提；各hold/C07与fresh reader顺序运行，所有输出目录/EXE槽root独占。不能沿旧PE或边写边构建复用PASS。

hold代表和C07/reader三轮，C11自然三轮，release每边界至少一轮。每轮保留candidate/源码身份、case/PID/exactHANDLE自然死、gate/原绝对tick、正确退出码、错误字段、真Start/Stop返回与窗口代次、成功content帧、保存计数与完整receipt；第一有意义失败保留。grace hold以原grace+15000到+2s、普通hold以原ordinary deadline到+2s判，不重新计15秒；父上限杀child、前提不足、缺gate、assert/fixture错误码或reader失败均FAIL。已有Build0、Header声明或同进程重建不得代替真实新进程reader/恢复与独立运行结果审查。

## Verification

- Lint/TypeCheck/Build/Tests/GUI：按research分工未执行；没有产品修改或新动态PASS。
- 静态已核真实前置/失败/成功/持久化调用链、当前helper pre-wake、POD字段尺寸和类型、修订合同/新头身份。新头static_assert未编译，C++类型/链接仍交root完整Solution。
- 本文件仅记录GREEN_DESIGN和后续实际源码/safety门，不更新其它报告、账本/HF、Spec、任务完成度或Git索引/refs。