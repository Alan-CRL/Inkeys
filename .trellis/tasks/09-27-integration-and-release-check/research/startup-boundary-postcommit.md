# Research: 提交后启动失败、回退停止与生产故障注入边界

- Query: E02 与 F063 的实际调用链中，哪些等待已经受 Close/Restart 的 15 秒监督保护，哪些启动失败处理仍能在监督前阻塞？如何最小修正并做真实产品分支注入？
- Scope: internal；集成后的主产品，不以旧 Draw3 demo 推导生命周期。
- Date: 2026-09-30（Asia/Shanghai）。
- Active task: `.trellis/tasks/09-27-integration-and-release-check`。
- 分工：只读产品与规范；仅写本报告。未执行 Git、构建、运行、GUI、性能采样或进程操作。ShutdownSupervisor 与 SetOffSignal 的实现另有唯一写入者，本报告不修改它们。

## Findings

### 1. 摘要与分类

| 项目 | 当前代码事实 | 分类与处置 |
| --- | --- | --- |
| 正常 Close/Restart 的 Host drain | SetOffSignal 先 Arm，然后 Host 最终保存屏障、worker join、绘制线程 join | 当返回 Armed/FallbackArmed 时已有独立进程级截止；不是因为内部 wait 没有 timeout 就确认正常退出必死锁。保留正常排空已接受保存，补真实 worker 停滞注入。双重 Arm 失败由 E01 独立修补。 |
| D101/D201/D301/D401/D202/D102 | 现有六分支都在 PublishFatalStartupFailure 和显式业务清理前 SetOffSignal(1) | F063 已修的源码顺序；尚缺直接进入 wWinMain 这些分支的自动证据，不重新上报成未修。D401 受有意关闭的白板 gate 约束。 |
| D003/D004/D005、较晚 D002 | 它们调用同一 PublishFatalStartupFailure，却未先 SetOffSignal；helper 自身不 Arm，先等待/提示/Preview::Stop | **确认的同类致命启动退场顺序缺口**：前面已经存在共享 RenderPipeline/可能存在 Preview；已决定 return 却能在提示/资源清理前无监督等待。未动态复现，不宣称发生过永久死锁。 |
| Bar B001/B002/B004→主循环 | 有真实 ReportFailure 来源；main 先 ShowStartupMessage/Preview::Stop，最后 SetOffSignal | **确认的同类顺序缺口**，且此时 Draw3、窗口及业务线程已启动。应最小前移正式退场。 |
| Host::Start 已知失败的内部清理 | 在返回 false 前 join/drain/detach；绘制线程可能正释放 presenter/GPU，RTS 失败还会同步 Shutdown COM | 确认存在无 deadline 的等待，但当前无永久阻塞复现与等待环证据；属于启动失败清理的工程验证/有界处理缺口。不能认定返回 false 后 StopProduct 必入活动 Host 保存屏障。 |
| Window::Start / DComp→ULW Window::StopAndJoin | Start 的 future.get 与失败内部 StopUnlocked、重建的 owner join 均先于 fatal SetOffSignal | 确认无监督等待范围；若 owner 回调/API停住，唤醒/stop_token 不能抢占正在运行的回调。尚非已复现的产品死锁。需隔离注入后选择失败清理截止合同。 |
| 未完成初始化的普通构造/驱动调用 | D3D 创建、RTS CoCreateInstance、创建窗口及用户回调本身也可能长耗时 | 一般性阻塞假设。不能为消除静态报告，在正常冷启动或可恢复 DComp 失败时无条件占用全局 Close；这需要新的可取消启动监督设计及真实耗时依据。 |

### 2. 文件与职责

| 文件 | 职责与关键位置 |
| --- | --- |
| `Inkeys/IdtMain.cpp` | 唯一产品入口、退出意图、窗口 specs、DComp→ULW 重建、fatal 初始化退场：143–190、256–279、1767–1775、2147–2158、2174–2217、2444–2629、2636–2766。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp` | 唯一 productHost；StartProduct 持 productMutex；StopProduct 停新调用并 drain 已进入的 forwarding：10–43。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp/.h` | 外部双 HWND 附着；独立 Draw3 device/presenter/绘制线程；启动握手和失败清理；最终保存屏障：Start 1029–1305、Stop 1308–1375。Host API 是 `.h`，不存在 Host.cppm。 |
| `Inkeys/Inkeys/Window/Window.cpp/.cppm` | 双 owner jthread 创建/销毁、同步命令、lifecycleMutex：Start 153–198、Stop 200–204、StopUnlocked 690–715、RunGroup 732–809、Submit 1159–1179。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp/.cppm` | 主线程启用唯一 RTS producer，COM shutdown 与关闭剩余接触：2394–2612、2615–2649。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | Host 的非 owning HWND 附着、输入 coordinator 原子引用与退出标记：266–323、394–403、565–568。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp/.cppm` | 真 Desktop 保存 worker、现有测试延迟、join：654–656、884–916、1154–1162；testFault 的 writeDelayMilliseconds 位于 cppm:98–103。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp/.cppm` | 真 PPT 保存 worker、UInk提交后/索引前现有 event gate：1061–1066；CloseAndDrain 1522–1529。 |
| `Inkeys/Inkeys/UI/StartupPreview/StartupPreview.cpp` | Bar 启动状态→真实失败码，1116–1138。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp` | 缺失 HWND/未就绪停止的实际状态发布，121–137、163–168、199–202。 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | Render client 注册失败发布 B002，Rendering:915–930。 |
| `Inkeys/Inkeys/Helper/ShutdownSupervisor.h/.cpp` | 退出前独立截止与精确旧 PID HANDLE；当前 API 不提供可复用的可取消普通启动 scope。本轮只读，E01 owner 正在独立收口。 |

引用行号为本次读取工作区时的位置；后续 E01 编辑 IdtMain 或其它实现时可能位移，应按符号复核。

### 3. Host 启动/失败/正常退出的真实链路

1. `wWinMain → StartProduct → productHost.Start`。StartProduct 持 `productMutex`（Product.cpp:26）；Host 验证两个非 owning HWND、Reset 输入/Bridge、AttachExternal、启动两个串行保存 worker，然后建立绘制 jthread（Host.cpp:1033–1087）。
2. 绘制 owner 创建自己独立的 GPU 资源/presenter；style 回调通过 WindowService 同步 owner 命令完成（IdtMain.cpp:2480–2487 → Window.cpp::Submit 的 future.get）。它绝不借 UI3 device。
3. 主线程 `startupCondition.wait(graphicsReady || startupCompleted)`（1234–1235）。这把 unique_lock 在等待期间释放，不是绘制线程因为 main 永远占 startupMutex 的已知锁环。绘制线程图形准备成功后等待 RTS 决定（1142–1145）；main 同步调用 stylus.Initialize，发布 stylusDecision 后再等 startupCompleted（1253–1275）。
4. 图形准备失败：绘制线程已发 startupCompleted，继续 EndDrawingActivity、destroy Controller/presenter/renderer/graphics，再把 running=false。main unlock 后先 drawingThread.join，之后两个 CloseAndDrain、DetachExternal、清附着状态，最后 return false（1236–1250）。**join 不能抢占驱动资源释放；但正常失败清理并没有活动文档/已请求最终保存屏障。**
5. RTS/首帧失败：main 已收到 startupSucceeded=false，先 stylus.Shutdown，再 RequestExit/ControlWake/request_stop/notify/join，随后 worker drain/detach（1278–1297）。stylus.Shutdown 自己包含同步 COM disable/plugin remove/Release；没有证据支持把 request_stop 当成可取消任意 COM 调用。失败 Initialize 的内部也能调用同一 Shutdown（RealtimeStylus.cpp:2451、2605）。
6. 失败 Start 返回后，`StopProduct()` 只排产品新调用、drain 已进入 forwarding，再调用 Host.Stop。附着和 running 通常均已清空；Host.Stop 的早退分支只做已关闭 worker 的 CloseAndDrain（1311–1319）。**不能再假定它一定走 PrepareExitAutoSave 的活动 Host barrier。**
7. 正常已启动 Stop：关命令端→停 RTS→发布最终 PrepareExitAutoSave→等 controller CPU快照屏障→排空 worker→RequestExit/request_stop/join→Detach（1321–1374）。stop 状态不能绕过已接受保存。Close/Restart 的截止应由外部监督保障，而非给每个 worker 加 timeout 后误报 durable 成功。

没有发现应立即上报为确定的 mutex 互锁环。已确认的是几类同步调用/等待缺少启动阶段截止；真实挂起点要通过可控 owner/worker hook 或现场线程栈确定。

### 4. Window owner、DComp 重建与唤醒的边界

- `WindowService.Start` 持 lifecycleMutex，启动 overlay/setting 两个 jthread，然后 `overlayReady.get() && settingReady.get()`。若 overlay 返回 false，短路后调用 StopUnlocked，两个 owner 仍须 join（153–198）。所以 D101 分支的前置 Arm 只覆盖 **Start 已返回失败后的** 清理，覆盖不到 Start 自己的失败 join 或尚未返回的创建回调。
- RunGroup 会将创建结果写入 promise；创建失败后 DestroyGroup，再 COM uninitialize。正常循环在 idle 的 MsgWait 与 PeekMessage 每层检查 stop_token；StopUnlocked 请求 stop、SetEvent，再 join（690–809）。这是正确的 idle 唤醒基础，不等于能取消 DispatchMessage/CreateWindowEx/DestroyWindow/created/destroyed 回调内部的阻塞。
- `DestroyWindowFor → CleanupLifecycle → activeSpec.destroyed()` 在同一 owner 执行（1057–1117）。产品 magnifierHost.destroyed 是 ShutdownMagnifierWindow（IdtMain.cpp:2325），允许在私有子进程 specs 中包一层显式 test gate，从实际 DestroyGroup 制造 owner 无响应，不必先修改公共 Window Service。
- 同步 Submit 在 owner 自身直接 Execute，外部入队后 future.get（1159–1179）；没有发现简单的“owner 对自己排队等待”问题。它不能取消正在运行的 owner 命令。
- 可恢复 DComp 失败后的 main：`StartProduct` false → StopProduct → WindowService.StopAndJoin → 修改主 spec 清 NOREDIRECTION/LAYERED → 再 StartWindowService → StartProduct(allowDComp=false)`（IdtMain.cpp:2527–2546）。此时未接受 Close/Restart，正常重建必须保持可用；不应在每次可恢复 DComp 失败时调用 SetOffSignal。
- WindowService.BeginShutdown 是单调显示 gate，并会拒绝后续 Start。直接把 Close supervisor 借作可取消的重建计时器会永久破坏成功 ULW 回退；该方案不可接受。

### 5. 新确认的 fatal 顺序问题：最小修正候选

#### 5.1 集中的 PublishFatalStartupFailure 未建立监督

该 helper 本身先 ReportFailure、等最多350ms失败帧、ShowStartupMessage、淡出等待500ms、Preview::Stop（IdtMain.cpp:172–190）。Show 是同步调用，不能假设用户及时确认；Preview::Stop/RenderPipeline::Shutdown 涉及真实 owner/共享 scheduler。

有意 fatal return 的未 Arm 调用：

- D004：主 CoInitializeEx 明确失败，调用 helper 后 RenderPipeline::Shutdown，再 return1（1767–1775）；RPC_E_CHANGED_MODE 是现有被接受分支，不要误注入为本例。
- D005：嵌入 PptCOM 文件验证/activation/load 任一步失败后，helper→释放actctx/COM/RenderPipeline→return1（2147–2158）。不能以删真实 DLL/用户 Office 文件造故障。
- D003：FontCollection 初始化 FAILED，helper→RenderPipeline::Shutdown→return0（2206–2217）。返回码历史0可独立记录，不能为了本次顺序修补随意改为另一产品语义。
- 较晚 D002：共享 RenderPipeline 初始化 FAILED，helper→return0（2174–2180）。更早 D001/D002 使用直接 Show+return；在 UI 资源较少时同样缺 fatal 意图，但风险等级与已有双画布不同。

**建议最小实现：** 把确认 fatal 的 SetOffSignal(1) 放入 helper 的第一条业务操作之前，使每个 caller 都获得同一顺序；六个已修 caller 的重复 SetOffSignal 可在统一迁移中去重，首次 CAS 保持幂等。按同一已接受 fatal 合同处理直接 Show 的 D001/早 D002。无需改变失败帧预算、消息文本、资源逆序、版本渠道或正常回退。

注意此举只保护已经判定失败的后续提示/清理，不保护 CoCreateInstance、Font 初始化、Window Start 等尚未返回的正常构造。F063 原六分支的“先 Arm 可能使失败帧成为 best-effort”行为已经存在；不能再承诺所有失败场景都一定显示完整红帧。

#### 5.2 较晚 Bar failure 到主循环的真实来源

实际链路：

`Bar.Initialization::InitializeWindow false → SetBarStartupState(WindowMissing)` 或 `BarUISetClass::Rendering::Register false → SetBarStartupState(ClientRegistrationFailed)` → `StartupPreview::SetBarStartupState → Startup::ReportFailure(B001/B002)` → `wWinMain ActiveSnapshot.failed`。

现主循环处理顺序为：RequestFailureFrame→Wait350→ShowStartupMessage→RequestFadeOut/Wait500→Preview::Stop→**SetOffSignal(1)**（IdtMain.cpp:2735–2748）。此时已运行真 Draw3/窗口/多个 jthread。`StoppedBeforeReady` 的 B004 也有生产来源；B003 有状态到 failure 的映射，但本轮未找到发布 StartupFailed 的正常 caller，不冒称相同可达性。

**建议最小实现：** 在看到 startupSnapshot.failed 后立即 SetOffSignal(1)，再调用既有 helper 显示该 failureCode 的消息（或保持原序列但删尾部重复）。确认式用户主动 Restart 的 Cancel 语义不受此 fatal 路径影响。缺失线程栈不能升级为“用户截图卡死唯一根因”。

### 6. E02 的有界失败清理：可选合同与实施顺序

优先实施第5节的确认顺序问题与 E01 双重失败，然后运行第7节注入。一般构造等待与已经判定失败的清理要分开，避免为了静态报告引入所有启动阶段的统一强退时钟。

**失败清理的最小合同可选：**

1. 已判定失败的 Host 清理、WindowService 内部失败清理和 DComp 重建 owner join，分别使用可取消的 scope 截止；正常完成后 scope必须完全解除，不占 Close意图。scope到期时才发布不可撤销 fatal Close，停新显示，并通过统一监督/失败退场保障停止旧进程。scope应在 join/COM/GPU Release 前建立，不能在超时返回后销毁仍由 worker 使用的对象。
2. 若只给 thread join 加 bounded native wait，需要在超时后保持所有资源/附着/旧 generation alive，先进入 fatal Close；不能返回普通 false让 caller继续建立ULW。这样只能覆盖 join，不能覆盖同一 main线程已经进入的 RTS Shutdown/COM调用。必须如实限定，不冒称所有 Start无限等待都已解决。
3. 不建议把既有 ArmShutdownSupervisor当普通可取消 scope：现API一次性占Intent，WindowService BeginShutdown也是单调gate。若新建scope监视器，必须把创建失败纳入确定处理，不能再制造E01相同缺口；关闭Arm owner与主线程先冻结合同，再实施。

本研究没有为普通冷启动选定新的超时秒数。用户给定的是 **接受 Close/Restart 后15秒无条件退场**；失败清理scope的预算、在其到期才接受Close的总时长应明确记录，不能把两个时钟加起来后仍说“从启动开始15秒”。超时不得 TerminateThread、detach仍访问业务对象的thread或跨generation销毁HWND。

### 7. 可运行的隔离验证形状（待实现，不是已执行命令）

#### 7.1 隔离与通用判定

- 延用现有 `--shutdown-supervisor-tests` 父子形态；新的名字如 `--startup-failure-tests` 是**拟议入口**，现代码不存在，不可在账本写为已运行。
- parent在 TestResults 下建唯一普通、非reparse的私有目录，复制本次真实构建的 Inkeys.exe，使用真实嵌入DLL/资源和自己的 Config/opt/AutoSave/log。走实际 wWinMain 常规启动；不能用测试里复制的退场算法代替。
- 私有child只有显式测试参数与验证的parent握手/独占根同时满足才启用fault；默认hook null。可用现有 Win7可用的event/精确process HANDLE做握手；不引入KB2533623/API依赖，不留下普通UI崩溃按钮。
- 不操控用户已有Inkeys/Office/PPT；在会启用PPTLinkageThread的后期Bar测试中，用私有配置关闭外部联动或显式测试隔离该业务线程，记录此限制，不能对真实Office制造失败。
- parent记录childPID、StartTime/EXE路径、failure-code到达、intent接受/Arm结果/绝对deadline、故障gate到达、最终process-signaled、退出码、残留本child HWND。时间用同源QPC/GetTickCount64，旧PID是否结束用精确HANDLE。
- 旧版红灯：parent最多等待目标deadline加调度容差；超时后只终止已核对的测试child，记FAIL/超时，不记自然通过。绿色使用产品自己的监督结束旧进程；Close不出现新实例，Restart恰一新实例和独立ready证据。三轮重复，构建/采样串行。

#### 7.2 具体场景和 hook 所有权

| 场景 | 注入位置/方式 | 证明什么及限制 | 所需写入者 |
| --- | --- | --- | --- |
| Fatal D003/D004/D005 代表性注入 | 在实际初始化结果转换边界，显式fault把成功结果替换为已声明失败；继续进入原wWinMain if/原helper。另在ShowStartupMessage的测试hook前发到达event并hold；保留一轮真实消息框不响应测试。 | 证明真fatal分支在提示/清理hold前Arm、到期旧进程结束。是失败分支/顺序测试，不能声称触发了真实COM/字体驱动故障。D004注入FAILED HRESULT，不能用已有允许RPC_E_CHANGED_MODE。 | E01冻结后IdtMain唯一owner；公共initializer代码可不改。 |
| 晚期Bar B002状态流 | 私有fixture通过真实SetBarStartupState(ClientRegistrationFailed)→ReportFailure入口，防止fixture期间成功首帧先封状态；或在真实Register结果边界注入失败。主线程按ActiveSnapshot进入原fatal分支并hold提示。 | 首选可验证状态传播/处理，不把状态注入冒称真实Register自然失败；需记录Bar入口方式。 | IdtMain fixtureowner；若选实际Register hook，Bar.RenderLoop文件需和UI3 owner串行。 |
| Window::Start失败内部join | 私有windowSpecs让Drawpad.beforeCreate返回false；已创建的magnifierHost.destroyed包装原回调后wait私有event。在实际RunGroup失败DestroyGroup/StopUnlocked中hold。 | 真WindowService失败清理，验证其scope到期而非D101“假返回false”。可再解除gate验证自然false返回和整链销毁。 | IdtMain窗口spec fixtureowner；不必改Window.cpp注入。scope实现需Window唯一owner。 |
| Host图形失败清理join | 在第一attempt真实图形/presenter结果失败，Host绘制owner公布startupCompleted后、最终GPU释放/线程退出前进入可控gate；main实际join。 | scope解除正常返回false并ULW重建；gate永久hold应fatal有界退场。需要准确输出failed-start-cleanup，不称GPU实际驱动卡死。 | Draw3.Host.cpp/.h唯一owner；main挂privateoptions。 |
| Host RTS失败清理 | 在实际stylus失败结果边界注入false并进入原stylusDecision/failed branch；对清理前/COM shutdown前gate单列。 | 验原失败branch与producer静止，不能用skipInitialize代替真实RTS成功/失败解除资源行为。 | Host/RTS相应owner串行；RTS代码变更需补input/callback quiescence review。 |
| DComp→ULW正常回退 | 第一attempt通过实际style/presenter callback拒绝NOREDIRECTION兼容转换，真实failed Host完成清理；主程序整条Window链重新创建，第二attempt不失败。 | **不得Arm/不得offSignal**；旧HWND全部无效后才有唯一新generation；activeMode=ULW、成功Present、两个DWM模式禁用、FLIP不变。保留这一反例门。 | IdtMain+Host fixtureowner；实际Window样式/renderer只读断言。 |
| DComp回退Window owner停滞 | 第一attempt真实failed Start已返回；私有destroyed callback在后续Window.StopAndJoin hold（与上一例按faultphase分开）。 | 不将它误标正常Close违约；新scope必须在正确位置保护该join。普通ULW成功反例与永久hold都跑。 | main窗口specfixture；scope Windowowner。 |
| 原F063代表 D101/D201/D301/D202/D102 | 原wWinMain条件边界强制对应失败，hold提示或owner；D201可强制两次Host失败，D301保留已启动Host。 | branch coverage和后续真实清理按case记录；直接强制bool不是该initializer内部死锁证据。D401不开放产品gate，按理由N/A。 | E01冻结后main；相关hook仅需要时进入子模块。 |

#### 7.3 已建立 Close 监督的真实 Host/保存 worker停滞

不需要为此重写worker超时。

- **渲染owner**：在真实Host运行且已产生独立文档的情况下，让绘制线程在可控帧/命令消费gate停住；确认gateReached，再从独立控制线程调用正式CloseProgram/SetOffSignal。实际StopProduct将等最终CPU保存屏障/绘制join。截止前不能强行recycle任意contact；旧进程到期必须结束，parent只检查自己的窗口和数据。
- **Desktop保存worker**：复用真实 `SetDesktopAutoSaveTestFaultInjection({.writeDelayMilliseconds=60000})`。延迟在生产save路径的644附近/654–656执行，正常default为0。先用无fault的真实Clear/保存取得durable基线与index，随后使第二请求进入Writing/实际delay；确认该证据再Close。证明真实worker/Host.CloseAndDrain停滞，**不是证明内核磁盘I/O本身挂起**。
- **PPT持久化边界**：现 `afterUInkCommittedEvent/continueIndexCommitEvent` 在真实版本UInk落盘后、atomic index前gate最多30秒（PresentationAutoSave.cpp:1061–1066）。让gateReached后不放行，Close在15秒到期强退，parent重新读最后已提交索引指向的UInk。新孤立版本可以存在，不能把它当index已durable，也不能清未知文件。
- parent复验最后commit文件/index可读与对应workspace/page身份；pending未完成明确允许丢失。再运行无fault反例，正常Close必须自然排空，不得因测试把保存默认为latest-only或增加可见输入延迟。
- Restart另跑：只有精确旧HANDLE signaled后恰一新实例；FallbackArmed无法保证restart的边界继续独立记录。不要用外部强杀结果冒充真实UEF；本组主要验证正常关闭监督，UEF suite仍单列。

### 8. 集成依赖与验证门

1. E01 writer先冻结ShutdownSupervisor与SetOffSignal接口、双失败策略和testfault入口，main/reviewer确认后再交出IdtMain写入权。
2. 本轮fatal顺序修正适合由同一main-source实施者在E01冻结后做小批次；新私有启动fixture也由同一owner。Draw3 Host、Window Service任何scope接口必须先写共享合同，不让不同agent同时改公共文件。
3. 普通Close真实worker停滞fixture可复用Desktop/PPT既有fault注入，先不碰worker正常语义。Host renderpause如必要仅加默认空的显式测试hook；避免SuspendThread任意点暂停。
4. 验证顺序：保留旧版红灯→最小修补→同case绿灯/三轮→成功DComp/ULW反例→独立actualdiff/callchain review→Debug ARM64完整Solution、Release ARM64/x64/Win32与Headless/PptCOM/supervisor和适用hidden回归。只由一个build/test owner使用输出目录，不在构建时做性能测量。
5. 如建fixture专用编译宏，使用完整Solution的显式测试配置/CL变量，保留原 ABI，并在最终恢复普通构建复验无测试fault入口；不得关闭签名/警告/链接错误。主Solution `InkeysRepo.sln` 的规则优先于native旧demo命令。
6. 本机Win11 ARM64通过只升级本机/编译/隔离结果；Win7 x86/x64、Hardware FL11.0/无hardwareFL11→WARP、ULW FLIP成功Present仍在用户矩阵，不新增bitblt/DWM或额外KB。

## Related Specs

- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：受管线程、首次退出意图/Arm顺序、最后durable恢复点与Failed边界。
- `.trellis/spec/native-desktop/draw3-integration.md`：单Host/RTS、两个窗口/独立device、CPU保存屏障、DComp失败显示前重建、DWM禁用/FLIP保留。
- `.trellis/spec/native-desktop/input-and-ink.md`：Closing/代次、Reset须producer真正静止；本报告不替代E03并发交错调查。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：完整Solution、原生MSBuild、三架构/ABI/动态Win7 API。
- `.trellis/spec/native/quality-and-validation.md`：真实生产模块、故障分支/资源状态、Win7仅KB2670838和独立运行证据。其旧demo构建例子不覆盖主产品规则。
- `.trellis/tasks/09-27-integration-and-release-check/research/post-commit-completion-audit.md` 与父任务 `completion-and-manual-acceptance.md`、`handoff.md`：E01–E05分工及新的用户GUI授权、无computer-use、保留更新/旧PPT/功能gate边界。

## External References

无新外部文档查询或依赖变化；本报告的判断来自已读工作区生产代码/本地合同。用户提供的Win7 FLIP实测是兼容约束，仍不代替本轮设备运行证据。

## Caveats / Not Found

- 未动态注入，所有拟议case均NOT VERIFIED；没有真实GPU/COM永久停滞栈、没有用户报告现场dump，不能把本研究当现场卡死根因定位完成。
- 未读取 implement.jsonl/check.jsonl：research角色隔离禁止加载它们；已根据完整注入Active task读取prd/design/implement及相关spec/研究/父handoff。没有research.jsonl文件。
- 已知失败清理的deadline与普通冷启动耗时是不同合同；本报告未擅自设产品全启动超时，也未给正常drain加数据丢弃超时。
- `BarStartupState::StartupFailed→B003` 有映射，但本轮未找到实际设置该state的正常caller；B001/B002/B004有直接生产来源。
- 白板D401保持gate关闭；Win7、真实笔/Touch和Office durable/可见恢复仍需各自验收。一般No-window或构建通过不能覆盖这些项。
- 本报告只写这一research文件，无产品、spec、工程配置、任务状态、Git或进程变化。
