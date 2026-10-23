# Research: 可见夹具的合成墨迹输入与只读观察入口

- Query: 既有 HiddenTestContact seam 能否在明确 harness 的可见 Drawpad 中使用，以及实际笔划/世界首点/历史的最窄观察方式。
- Scope: internal
- Date: 2026-10-22

## Findings

### 结论

可以在明确专用 harness 的可见 Drawpad 上复用现有合成 contact 消息，**无需改变任何可见性 gate、切换输入后端、关闭RTS或手造Stroke**。它的名称是 hidden，但生产实现只以显式 StartOptions 开关控制，不检查 IsWindowVisible。现有测试的 invisible() 是其本地测试wrapper的权限约束，不能删掉wrapper后仍运行同一个旧hidden CLI。新可见live CLI需要自己明确授权、日志标注synthetic_contact=1并保持真实Capture/Tracker/StateMonitoring/窗口ACK。

### 最小现成 API

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h:250–251`：StartProduct前 `options.enableHiddenTestContactInjection=true`，其默认false。现有程序只在实际fixture设置；普通启动保持原值。
- `Host.h:172–190`：消息 `kDraw3HiddenTestContactMessage`、Down/Move/Up/Cancelled/flags。用 `SendMessageTimeoutW(ownedDrawpad, message, phase|kHiddenTestExternalPenFlag, MAKELPARAM(clientX,clientY), SMTO_ABORTIFHUNG|SMTO_ERRORONEXIT, timeout, &result)`，检查API成功且LRESULT==0。
- `Draw3.Host.cpp:2668–2673` ForwardMessage仅检查message+enabled，转私有 `PublishHiddenTestContact`，true→0、false→-1；调用者不直接访问private方法。
- `Draw3.Host.cpp:2539–2633` 合成实际 ContactSnapshot，实际时钟QPC、有限坐标/pressure/source，走唯一 ContactInputCoordinator::PublishDown/Move/Up/Cancelled，最后PublishControlWake。没有构造document Stroke或向renderer写点。Down服从原输入准入；跟随期间被block的Down不能算注入成功。
- `Draw3.Host.cpp:1829` 从options锁存enabled；没有HWND可见性检查。文件中IsWindowVisible仅用于约144窗口诊断，不是该注入gate。保持现成限制不变。

消息坐标被解释为有符号16位client像素，不要用screen坐标或大于32767的值。默认外部pen flag给正常笔路径，不需要touch/pen-tail/延迟Up等其它测试功能。同一source cursor ID应串行Down→有限Move→Up，真实terminal必须完成，再开始B，不与真实OS stroke并发。

### 不会改变的生产路径与会开启的诊断

enableHiddenTestContactInjection不决定RTS/backend。`Host.cpp:2176–2203` 仍沿原stylus.Initialize，自然选择RTS或原错误fallback。原输入producer仍存在，应在专用子进程/窗口内控制只发生一种contact源。

此开关还会开启：input.EnableDiagnostics（1854）、原eraser诊断（1980）、PenRuntimeDiagnostics observer（1993）、私有持久化恢复receipt（2007）及显式非零followTestRecoveryMaximumTiles预算（2011）。因此不应声称与全部普通产品配置完全相同。保持 `followTestRecoveryMaximumTiles=0`、不设置hiddenTestDisplayScale、不设置successfulPresentReachedEvent/continueSuccessfulPresentEvent，避免把测试预算、虚拟DPI或暂停present引入live case。渲染模式/硬件加速照原产品，不强制ULW只为通过。

### 实际笔划完成：可读原子快照

`Host.h:321–377` / `Host.cpp:2405–2460`：RuntimeSnapshot / ProductRuntimeSnapshot是锁保护或原子的可读事实，实际包括：

- inputDownPublished、inputMovePublished、inputTerminalPublished、inputRecycled；Down/terminal/recycle按目标笔划前后增1。
- firstFrameReady、lastPresentSucceeded、successfulPresentCount增长、requested/ready output target与revision一致、presentedContentRevision==contentRevision。
- currentPageHasContent、canUndo、canRedo、completedStrokeKind；普通Pen/HardPen完成应Drawing，SolidLine应Shape，不能混用。
- runtimeMetrics.contactSeen可额外核实际Controller消费。要用它时从启动设置 `enableRuntimeMetrics=true`、有限 `runtimeMetricsMaximumSamples`（原保存fixture用128），同时核metrics.enabled/invalid/drop，不把任意值0当未消费结论。

`HiddenWindowTest.cpp:3950–3978` 有现成组合断言。其外层invisible()不能搬来可见夹具，只复用上述事实条件。不要仅依赖contentRevision变化：它并非每增加一笔必增。

`HostRuntimeCallbacks::acceptedStrokeBegan`（Host.h:210）是实际Controller接纳起笔观察，`Bridge::AcceptedStrokeBegin`（Bridge.h:152）只含kind/workspace/pageIndex/sceneGeneration/hostGeneration及PPT身份，**不含世界点坐标**；可作为实际接纳身份receipt，不要扩为任意Canvas读接口。

`HostRuntimeSnapshot.pen` 是原 `Draw3.PenDiagnostics.h:10` 的只读pen尾部事实；含strokeId、inputSequence、realPointCount/modelUpdateCount/terminalLocked/awaitingReconnect、rawMove/rawUp及最多8个尾点。该observer仅enableHiddenTestContactInjection才装，不能凭尾点推断任意普通产品输入都已有该诊断。它不是完整笔划/固定世界首点快照。

### 首点 world 和 A 原始几何：读真实 UInk

现有Host没有公开“取Canvas/全部Stroke/世界首点”的线程安全入口，不能直接从其它线程读取drawing/Canvas。最窄复用原私有 autoSaveRoot，真实 Clear 或 Exit 保存之后由原文件读取/Import观察：

- `HiddenWindowTest.cpp:3997–4006` 复用 `ReadLastCommittedDesktopAutoSaveFixture(root, date)`；跨午夜读前后date。
- 保存先设置真实内存 `setlist.saveSetting.enable=true`，SyncDraw3State发布autoSaveEnabled，Host options显式private root，不触用户文件。
- `HiddenWindowTest.cpp:4105–4145` 用实际Exit snapshot的workspaceGuid/pageGuid/viewport/A points+width+style逐项对比；B `points.front()` 应等于client Down加**保存的实际viewport**，浮点必要容差0.001。普通Pen至少多点；不要要求不同时刻A/B完整建模数组相同。
- 真实ReadUInkFile返回Complete、ImportDraw3UInkDocument返回Success、版本10，才证明真实文件。`HiddenPersistenceSnapshot` 只可作worker事实，不能代替文件读取。

若只做可见跟随像素，没有运行保存，就明确world首点/原A几何未覆盖；不能用输入期望坐标自己拼一个snapshot冒充。

### Undo / Redo

使用既有 `PublishProductCommand(Undo/Redo)` 原命令入口。跟踪期间由原Host前置End，仍等真实Business/Window逆ACK，不直接Handoff。核Runtime undoCommandCount/redoCommandCount各增1、ready输出/内容/canUndo/canRedo吻合；单笔A时Undo后应内容空、canRedo真，Redo恢复。A+B时Undo应保留A，不可仅看command计数。

命令只证明走过历史路径；屏幕确实移除/恢复B仍需core正在实施的实际像素证据。不要为了观察而直接修改history指针或替换Document。

### 始终不可见的现成边界

- `RunScrollFollowInvisibleHostTest` 和 `RunScrollFollowPersistenceTest` 的 `invisible()` predicates在各个send/ready/Wait中会拒绝可见HWND。不要复用其完整CLI、不要修改predicate来让它变成可见测试。
- `Host::HiddenPersistenceSnapshot`（Host.cpp:2382）只检查startOptions.enableHiddenTestContactInjection；没有隐藏窗口条件。它允许live专用fixture读取有限counts/receipt，仍需显式harness和同generation、禁止并行下一Start。
- Controller hiddenDesktopRecovery receipt仍只观察实际Committed/Loaded分支，没有可见性魔法；不能手写它或使用伪completion。
- Render输出ready和WindowService真实可见ACK在新live case必须保持独立真实条件，不能因为用了输入seam就沿用hidden的Handoff seam。

## Files Found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h/.cpp`：显式contact开关/消息、唯一mailbox、原子Runtime、有限持久化诊断。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h`：实际起笔身份receipt，无点坐标。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenDiagnostics.h`：实际pen尾部诊断，无完整Stroke读接口。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp`：已验证的普通Pen/Shape完成和真实文件world首点断言；本地invisible约束不得放松。

## External References

只读内部源码，没有build/GUI/截图或网络操作；API runtime行为仍需live实际验证。

## Related Specs

`.trellis/spec/native-desktop/scroll-follow-product.md`：不可见软件测试的证据等级、普通Pen点验证、真实窗口ACK及UInk文件边界。

## Caveats / Not Found

- 合成Contact只覆盖mailbox以后的真实路径，不等于OS/RTS/硬件输入通过；日志和最终报告必须明示这个seam。
- 没有现成线程安全的Canvas完整几何只读快照；必须走真实文件，或将这一项留未测，不新增任意Canvas接口。
- 不建议同时有真实鼠标Down和synthetic Pen Down，避免无法归因的多source行为。

## 追加：真实 Exit 索引与文件的最小调用

`DesktopAutoSaveReader`不是当前导出类名，不需要构造reader。使用 `import Inkeys.Drawing.Draw3.auto_save;`；该模块 `Draw3.AutoSave.cppm:14` re-export `draw3.uink_draw3_import`。导出 namespace 为 `Inkeys::Drawing::Draw3`。

```cpp
const auto initialDate = Draw::CaptureDesktopAutoSaveTimestamp().localDate;
// 真实操作/Off/最终数据保存；StopProduct 返回后原worker已Drain。
Draw::StopProduct();
auto exit = Draw::ReadLastCommittedDesktopAutoSaveFixture(privateAutoSaveRoot, initialDate);
const auto finalDate = Draw::CaptureDesktopAutoSaveTimestamp().localDate;
if (finalDate != initialDate) {
    auto current = Draw::ReadLastCommittedDesktopAutoSaveFixture(privateAutoSaveRoot, finalDate);
    if (current.status == Draw::DesktopPersistenceStatus::Loaded) exit = std::move(current);
}
```

函数声明 `Draw3.AutoSave.cppm:114–127`，实现 `Draw3.AutoSave.cpp:869–944`。它规范化私有root、持实际非reparse目录/file lease、取原索引mutex、读合法index或合法bak，校验单调唯一dailySequence后选择最后committed entry；依entry相对路径读真实UInk，核文件GUID，返回read receipt与indexBytes。不是目录里猜最新mtime，也不是只读worker计数。索引位置是 `<root>/desktop/<localDate>/index.json`，文件位置 `<root>/desktop/<exit.localDate>/<exit.relativePath>`。

读取必须核：

- `exit.status==DesktopPersistenceStatus::Loaded`、`exit.trigger==DesktopAutoSaveTrigger::Exit`、`exit.loadedSnapshot`、`exit.sourceRevision`。
- `exit.loadedSnapshot->fileGuid==exit.fileGuid`、workspaceType==0、canvases.size()==1。
- `snapshot.workspaceGuid.Bytes()==final.identity.workspace`、`page.pageGuid.Bytes()==final.identity.page`。这里final为本次真实跟随Channel只读快照，final.session与此前Business同一session，自始至终不退休场景，不能拿另一次channel身份匹配。
- page.viewport.scale==1，x/y与真实final viewport相等或必要float存盘容差；普通Pen/HardPen A/B均Pen类型、多点，A点/style如有独立baseline则逐项精确比。B真实Down屏内client坐标 `(bx,by)`，`abs(B.points.front().x-(bx+page.viewport.x))<.001`，y同理。
- `storageSession` 是真实autoSave worker的canonical GUID，不是Business/Follow的数字session，不能直接比较。对全新唯一空root、一次Host启动且只有Exit保存，sequenceInSession/dailySequence应各1，index entries唯一且trigger=exit；若有真实Clear baseline，参考既有HiddenWindowTest.cpp:4098的同storageSession、sequence+1、不同fileGuid检查。避免把旧root的另一文件当本次结果。

`ReadCommittedDesktopPath`（AutoSave.cpp:696）已经要求ReadUInkFile Complete、无invalid complete blocks或contentSequenceRecovered、成功import snapshot/fileGuid匹配，再按真实interval投影。若报告还需显式v10/Read/Import状态，可复用 `HiddenWindowTest.cpp:4137–4146`，另外导入 `draw3.uink_file` / `draw3.uink_draw3_import`，对上述明确文件路径执行原Read/Import并核同GUID/viewport/points，不新增保存API。

Host没有将其私有autoSave worker的SessionId直接导出为产品public API；不要为测试新增。唯一新建私有root、唯一索引entry、实际同页GUID+文件GUID+view/points关联足够避免误读；记录storageSession原值作为本run的文件身份。
