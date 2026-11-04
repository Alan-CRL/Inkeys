# Research: 正常退出触发15秒兜底的只读定位

- Query: 用户从Inkeys菜单/设置正常退出后，诊断采集退出码为-515702762，是否为新ROT让出造成的COM泄漏，退出究竟卡在哪一步？
- Scope: internal；只读产品代码、规范和本轮采集，未构建/测试/操作GUI/Office/进程，未执行git或改产品/配置。
- Date: 2026-11-01

## Findings

### 1. 已确认的是退出截止触发，不是普通退出成功或CLR异常码

本轮目录 `Build/Validation/ppt-navigation-20261101-130949-399-0a27afde`，PID87084，EXE SHA7A7D7832...、内嵌PptCOM351A5292...与修复版匹配；manifest为`exited-nonzero`，exit_code=-515702762，对应无符号`0xE1430016`，采集本身capture_errors为空。用户明确说从菜单或设置正常退出。

`Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp:46-49` 定义该码为`kFallbackForcedExitCode`；`:146-157` 的进程内 `FallbackDeadlineThread` 等到绝对tick后无条件`TerminateProcess(GetCurrentProcess(), code)`。`:415-418` 在接受退出意图时先保存deadline并创建这个线程，随后才尝试外部helper。

**E1430016只证明进程内15秒截止线程最终结束了仍活着的进程，不能据它判断外部helper创建失败。** 即使helper已成功握手，自身兜底线程也存在，两者可在截止时竞争结束父进程。外部helper失败的专用事实应看`SetOffSignal`的FallbackArmed日志/Arm状态，不能从这一个最终退出码倒推。

### 2. 现有时间线不支持“descriptor循环一直跑到强退”

按manifest的QPC/UTC锚（10MHz）换算，最后一条managed PptPerf在约**13:11:43.954**，内容为maintenance descriptor begin/end；13:11:44之后没有managed事件。原生日志记录：

- 13:11:44.234：`[放大API线程][MagnifierThread] 定格请求协调器停止`。
- 13:11:44.311：最后一条`[PptNav] stage=session-clear`。
- 13:11:46.354：最后一条UI3Diag（Settings idle回调）；文件之后无新行。
- manifest ended_utc约13:11:59.365。该时间包含父进程观察/流排空，不是精确死亡tick，不能用减15秒求精确点击时刻。

此前仅看stderr尾部descriptor事件容易误以为它运行到进程死亡；真实记录不是如此。另一个易误读点：`PptPerfTrace.Begin("descriptor")`使用当前诊断上下文，其默认show_session/binding=0不代表服务的实际session被重置为0。

信号顺序与这段时间线相容：`IdtMain.cpp:341-371`先CAS共享`offSignalInterop`，再Arm，之后才发布C++ `offSignal`、Hide/Wake、CrashHandler停止及Magnifier协调器停止。managed while直接读取共享槽，所以可以比native线程先离开监测循环。

### 3. 不能由现有记录确定真正卡住的清理函数

主线程正常关闭顺序位于 `Inkeys/IdtMain.cpp:3789-3809`：

1. 清topmost observer；StartupPreview::Stop；Whiteboard::Shutdown。
2. join ui3Initialization、freezeFrame、stateMonitoring、magnifier、PPTLinkage、topWindow。
3. StopProduct；Setting::Shutdown；Window::StopAndJoin；RenderPipeline::Shutdown。
4. Display subscription/Shutdown、Draw3诊断输出器Stop。
5. **此后**才写“等待各函数线程结束”，然后最终线程状态等待及FreeLibrary/COM反初始化。

本轮没有第5步日志，也没有逐阶段begin/end、线程栈、dump或生产shutdown进度包，目录内仅manifest/stdout/stderr/native log/分析summary。缺失的logger消息本身还可能受异步sink影响，不能把它当成某一join已确认卡住的证明。现阶段只能确定正常退场未及时完成，不能选择某个组件修复。

已有几个可能等待点：

- StartupPreview.Stop (`UI/StartupPreview/StartupPreview.cpp:995-1010`) 会Unregister渲染回调、释放资源、停止owner；Unregister等activeCallbacks为0（`RenderPipeline.cpp:945-953`）。其资源PostControl只等500ms、ownerStop最多等1500ms后detach，但初始锁或Unregister仍可阻塞。
- Whiteboard.Shutdown (`UI/Whiteboard/Whiteboard.cpp:583-605`) 会等待display subscription、Unregister、窗口Hide与背景mutex。
- Setting.Shutdown (`UI/Setting/Setting.cpp:4812-4843`) 向渲染线程PostControl并等待settingSessionDrained，然后Unregister/业务队列Stop；本轮是否到达此点未知。
- `StateMonitoring`外层检查offSignal且runtime wait为250ms（`IdtState.cpp:3445-3480`），但源中还存在调用/锁与native操作退休；没有栈时不能由外层循环退出条件排除它。

这些只是源代码支持的候选，不是本轮实测归因。

### 4. PptComService也有未被当前span覆盖的退出阶段

`PptCOM/PptCOM.cs:1435` 的服务while读取`Thread.VolatileRead(ref *offSignal)`；其finally先Mailbox.Stop，再FullCleanup(true)，然后最多三轮RetirementRegistry重试。现有PptPerf只覆盖服务的日常维护/命令，并没有这些finally边界。

`FullCleanup` (`PptCOM.cs:494-527`) 顺序是失效binding、清工具缓存、UnbindEvents、释放window/presentation/application并清空槽。`UnbindEvents` (`:478`) 中 `TryDisconnect` 会调用Office连接点Unadvise；`ComEventConnection.cs:136-160` 的Unadvise与CP Release是同步外部调用，失败可以保留lease重试，但**已阻塞的外部调用没有本地返回保证**。Root SafeRelease是确定性单次释放，也可能本身等待；其正确性不能只靠最终循环日志判断。

原生 `GetPptState` (`IdtPlug-in.cpp:503-545`) 只有PptComService返回后才reset服务快照。`PPTLinkageMain`结束段 (`:1647-1674`) 先join PptInfo、Ppt::Shutdown，再最多5×500ms看GetPptState/PptInfo状态。退出过程中如果managed卡在Unadvise或root Release，当前日志可能显示日常维护已停止，但服务尚未返回；反过来，日常维护停止也可能是服务已成功完成退出。**两者目前不能区分。**

`PptInfo`在offSignal后还有EndSession (`IdtPlug-in.cpp:1475`)；本轮最后session-clear约44.311与其退场相容，但单行也不是整个PPT线程组完成证明。

### 5. 与本轮ROT修复的关系只能保留为待验证

新增ROT策略仅在healthy缓存资格/排队导航下取消尚未完成扫描；相关场景新日志有rot-deferred/forced，未新增Office线程或跨轮RCW缓存。当前GetPptState服务最后日常观察已经返回，不能直接把退出超时叫做“扫描仍卡住”或“新增COM泄漏”。

但本轮确实是修复版正常退出失败，不能把导航改善当作整个验收通过。之前正常退出采样与本轮关闭时的Office文稿/放映、Setting/桌面状态不同，单纯比较版本与退出码也不能排除回归。需先获得退出边界或栈证据，保持原15秒监督，不关闭签名/超时保护，也不以GC/FinalRelease或杀PowerPoint补漏。

### 6. 最小下一步证据建议

在**Arm之后**使用默认关闭的数字时间戳记录：SetOffSignal Arm返回/Native flag/Hide/Wake/Magnifier边界；主线程上述每个Stop/join的begin/end；PPTLinkage join/Shutdown/状态等待边界；managed服务finally/Unbind/根对象释放/CP重试的begin/end。不能在Arm前增加文件/日志等待而破坏退出保护。记录线程ID和同QPC，日志错误不得干扰清理，不添加Office读值或改变释放次数。

若能由用户人工复现并在15秒内收集进程线程栈，可进一步定位真实阻塞；AI不自行启动GUI、不kill或操控用户Office。本研究不实施日志或修复。

## Files Found

- `Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp`：绝对15秒截止、进程内兜底与外部helper握手，定义E1430016。
- `Inkeys/Inkeys/Helper/ShutdownSupervisor.h`：退出监督入口与结果契约。
- `Inkeys/IdtMain.cpp`：共享信号发布、Arm先行、主线程join/组件关闭及最终COM清理。
- `Inkeys/Inkeys/Helper/Helper.CrashHandler.cppm`：菜单/设置共用CloseProgram，先SetOffSignal再隐藏入队。
- `Inkeys/IdtPlug-in.cpp`：PptComService宿主、PptInfo、PPTLinkage结束段。
- `PptCOM/PptCOM.cs`：managed监测循环及未计时的FullCleanup/Unbind/root Release。
- `PptCOM/ComEventConnection.cs`：同步Unadvise与retirement重试所有权。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp`：直接退出按钮与停止时渲染会话排空。
- `Inkeys/Inkeys/UI/StartupPreview/StartupPreview.cpp`：owner/资源与渲染回调取消。
- `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp`：白板渲染/窗口/订阅退场。
- `Build/Validation/ppt-navigation-20261101-130949-399-0a27afde/`：本次原始记录；没有生产shutdown阶段包/dump。

## Related Specs

- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：受管线程退出、Arm-first/15秒监督、shutdown durability边界。
- `.trellis/spec/ppt-interop/com-contract.md`：共享LONG稳定地址、managed volatile读取、owner/CP确定性释放与真实退出单列验收。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：渲染callback/控制任务、业务队列与窗口owner的停止责任。

## External References

无外部版本/SDK结论；研究依赖本仓库源代码、规范与真实本地采样。未运行任何命令以改变系统/Office状态。

## Caveats / Not Found

- 没有进程死亡前栈、dump或逐阶段退出marker，无法确认具体被卡线程/函数。
- E1430016不是一般CLR异常码，也不意味着外部helper启动失败；只能确认进程内deadline兜底结束。
- 不能证明新ROT修复造成或没有造成该异常；不能将日志尾部descriptor解读为它一直运行到强退。
- 本报告只有读操作和本研究文件写入；无产品/配置/spec改动，无GUI、Office、构建、测试、git或进程操作。
