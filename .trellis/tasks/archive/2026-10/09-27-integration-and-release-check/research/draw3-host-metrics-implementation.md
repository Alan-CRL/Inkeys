# Draw3 U3-H Host 指标生命周期实施

日期：2026-10-01。Active task：.trellis/tasks/09-27-integration-and-release-check。

## 授权及修改边界

Root 授权 WRITE_ALLOWED_U3_H，依据完整读取的 draw3-content-and-host-contract.md §7 与 draw3-content-and-host-design-review.md U3-H GREEN_DESIGN（R1–R5 已关闭）。只修改 Host.h/.cpp、HiddenWindowTest.h/.cpp 和本报告。其它 writer 正在处理 F067；保留现 Host C3 FailedCleanupSignal、successful-present 两事件、presentationNotFound 与 F065 reader 合同。没有修改 Main/工程/Controller/RuntimeMetrics/RTS/Presenter/AutoSave/UInk/Helper/UI/spec/父账本。

最小差距是生产 Host 没有持有并借给真实 Controller 的 Session。接线位于 Start/真实绘制 callback/两失败 join/正常 Stop，不在 runner 复制 metrics 算法。每 run 创建新 unique_ptr Session，Controller 借用穿 startup/Run/异常/析构；真 join 后才封口、取本 run input baseline 差并离线调用现生产 WriteJson。固定 metadata 在 GPU owner 按值复制，进度只发布原子值。defaultoff 没有 Session/旁挂 heap/新增指标 clock/I/O。Root 批准 max1..32768，越界仅 unavailable/产品继续；Controller 无公开 unavailable getter，真实 startup ClearCanvas 的非零 frameSerial+presentAttempts 保守验证实际接线，不能给空报告。

不实现 16+200 runner、PhaseBoundary/JSON 扩展、Move/Up latency、thread CPU、Laser 成本、功能 checkpoint/fixed replay/UInk 恢复；不改设备/线程/DComp/ULW/FLIP/两个 DWM gate/Win7 合同。只 ULW 小 smoke，DComp 与失败 GPU/RTS 注入未验证。没有构建、EXE、GUI、Computer Use 或 Git 操作；Root 唯一占构建/运行槽。

## PATCH_READY：四源停写，等待 Root 独立检查及构建

2026-10-01 本单元已完成源码写入及静态封口，没有任何动态 PASS。所有后续编译/运行均由 Root 串行执行；其它 writer 状态不能由本报告推定冻结。

## 实际接口与调用链

- HostStartOptions 尾追加 enableRuntimeMetrics=false 与 runtimeMetricsMaximumSamples=32768，保留既有 aggregate 初始化和 C3 全部字段。显式请求只有1..32768合法；0/32769及 serial耗尽标 unavailable，不钳另一个 population，产品继续原启动。
- Host.h 尾追加 HostRuntimeSnapshot.runtimeMetrics，载荷为固定状态/计数和 HostRuntimeMetricsMetadata（128 wchar adapter描述）；不导出 Session/GPU/COM/record/GUID/getter。WriteRuntimeMetrics(const wchar_t* absoluteOutputPath) const noexcept 只供串行生命周期 owner；caller负责新私有绝对目标与旧报告读完再 Start。
- BeginRuntimeMetricsRun 仅在 running/attached/drawingThread.joinable 均否之后释放旧 Session/清本run值。新有效 Start 在 Controller 构造和 Run 前一次准备 unique_ptr Session；借给真实 DrawingController 第七个 metrics 参数，haptics 默认参数仍保留。新Session不Reset，不把同址generation或contactOrdinal混入前run。
- input.EnableDiagnostics 使用旧 enableHiddenTestContactInjection OR enableRuntimeMetrics；原 hidden-on 诊断保留。正式 defaultoff 不创建 Session/Controller旁挂、不新增 metrics QPC/adapter query/I/O；固定 Host 状态随原 Impl 存在。默认关闭的 RuntimeSnapshot 仍返回真实 allocatedBytes 原子观察，不能仅凭 enable=false 声称未分配。
- DrawingController::ClearCanvas→PresentFrame→实际 Presenter 返回→生产 PresentReturned/Commit→原 ObservePresented→Host的 owner Snapshot 原子进度。初始真实 frameSerial/presentAttempts均非零才证明 Controller 诊断旁挂生效；Controller自身Prepare失败会关闭真实计数，这时Host conservative unavailable，拒正常空报告。没有新 getter、假Present或自造landing。
- CaptureRuntimeMetricsMetadata 在绘制线程复制实际 graphics.driverType/featureLevel、presenter/output/rawRevision、QPC frequency和有限adapter描述。启动/释放前才GetDesc；正常 opt-in presented callback仅抄数值，caller/GUI线程不访问GPU。Run结束在 graphics清空前保存最终tuple。当前Session JSON仍schema2；Host metadata/runSerial由caller取值写后续manifest，不伪称已进入raw新字段。
- 原 Stop顺序保持 command关闭→stylus Shutdown→退出保存屏障→两worker CloseAndDrain→RequestExit/wake/request_stop→drawingThread.join→detach/清隐藏注入。此尾后 SealRuntimeMetricsRun 才 InvalidatePending、保存finalSnapshot/finalInput及发布sealed。running=false/requestStop/observerinactive均不代替join，Stop早退也要求线程不可join。
- 五类启动失败（参数/禁DWM、Attach、thread创建、graphics join、controller/RTS join）标startupFailed；后两类有真实join。ownerCreated使无线程失败不会伪称joined。Run catch标runFailed；失败/未封/未join/defaultoff/unavailable均拒Write，保持原失败信号与日志/清理次序。早参数失败会清除旧report资格，不能导出上一个成功run。
- finalInput使用本run启动前真实Coordinator计数baseline差，slotCapacity/occupiedSlots是最终占用值，Reset不假定累计计数归零。Write只调用现生产Session::WriteJson及CREATE_NEW，捕获排序/分配/格式化异常返回false/exportUnavailable，不在Stop或worker drain写盘。

## 共同预算

Controller真实DrawingControllerMetricsState::Prepare/FitsBudget保留；其实际runtimeState在现U2报告为45296B、基础helper45216B，源码static_assert(runtimeState<=64KiB)。Host保守预留完整65536B而不把helper当全部Controller状态。共同planned auxiliary = 65536 + sizeof(Host::Impl::RuntimeMetricsState) + sizeof(RuntimeMetricsSession)，实际Host固定载荷另static_assert<=4096。Session构造后、Controller分配前核 allocatedBytes <= 33554432-auxiliary；仅满足时继续指标，Controller自己的共同32MiB检查仍再执行。公开payloadByteUpperBound是Session实际allocatedBytes加此保守上界，不冒称Controller实际sizeof或allocator metadata。

Host实际sizeof/effective32768/数值预算仍待 Root ARM64编译与smoke动态取证。fixture首on容量128、第二on容量32768均要求共同upperBound<=32MiB，没有新增热数组、扩容或功能readback缓冲。

## 有界真实 smoke 与 Root Main 注册提案

实际已定义：Draw3.HiddenWindowTest.h/.cpp 的 int RunHiddenWindowRuntimeMetricsSmoke(const wchar_t* privateOutputRoot) noexcept。

Root注册建议：Inkeys.exe --draw3-host-metrics-smoke --output-root <新任务私有绝对root>。须在普通配置/单实例/Office/业务初始化前严格early分派；只接受准确selector与output-root，未知/重复/缺失参数拒绝。沿现 hidden入口需要的COM初始化边界调用，不引入普通metrics配置。Root先在忽略TestResults/release-hardening下create-new root并保留所有失败目录，独立实码安全检查后才运行，本writer没有修改Main/工程。

smoke复核绝对/规范化/有限MAX_PATH、现存各父非reparse；CREATE_NEW固定子目录u3-h-host-metrics，拒重用。复用MakeHiddenSpec及真实Service四窗，320×240、-32000、visible=false、own PID，主WndProc仍DrawpadMsgCallback→ForwardProductMessage→ProductHost。只新legacy窗口/强制ULW，autoSaveRoot为空、ProductState自动保存关闭；不调用旧RunMode的显示/capture/Office/EXE旁保存段。Stop guard声明晚于StyleContext/Service，异常和早退先真实StopProduct→StopAndJoin再析构回调/HWND。

有限检查U3H00–U3H10：

- 未启动Write拒绝且无文件；真实defaultoff Start无Session指标/导出。
- 0与32769两种非法容量产品仍完成真实Start/Stop，标unavailable且拒空报告。
- 同一真实ProductHost两次on运行，第一128、第二默认32768；每次新runSerial、零旧contact计数、真实startup metadata/帧/attempt，活动Write拒绝且无文件。
- 真实窗口owner的 bounded SendMessageTimeoutW值mailbox发送external Pen Down(40,48)→Move(160,132)→Up(160,132)，实际SolidLine工具；等Controller contactSeen=1、真实成功Present confirmed=1、真实终态recycle/content，不用poll时间算latency。timeout不重发Down，无栈payload悬空。
- 真Stop后joined/sealed且pending=0，无invalid/drop/overflow。首次生产WriteJson到run-one.json/run-two.json，再实际解析schema2 raw，核一笔landing、Present计数、本run Down/Move/terminal/recycled均1、occupiedSlots0。读取完第一report再下一Start，第二输入不能累成2。
- 同路径再次Write拒覆盖并保留原JSON。最后nullptr参数的failed Start拒export此前sealed run。该失败只是原真实参数拒收，不冒称GPU/RTS/thread创建故障动态覆盖。

首次Root命令须在独立source/safety检查放行后执行，并保留 stdout/stderr、exit、report和当前PE/source hash。不运行现有全hidden集成套件来替这个selector；其中旧持久化目录/显示/capture行为不是本小smoke边界。

## 静态验证与未验证项

已完成：四文件UTF-8无BOM/全CRLF/无NUL扫描；相对派发实际字节的内存Myers差异检查（没有Git）：Host.h +34/-0、Host.cpp +262/-9、HiddenWindowTest.h +2/-0、HiddenWindowTest.cpp +217/-0。仅Host必要启动/停止/observer接线删除9行，无其它源码改动或整文件格式diff。机械括号扫描平衡，三处真实drawingThread.join、五处startup失败seal、两个Stop seal、唯一个生产WriteJson/全局InvalidatePending调用已定位；这些不代替C++编译。C3 failedCleanup、两个present事件/门、NotFound和HiddenPersistenceSnapshot原合同保持。

未运行：compiler/MSBuild、lint/typecheck、EXE/GUI/benchmark/Computer Use/Git；按Root唯一槽分工。本单元没有动态PASS或代码compile成功结论。Root最低门是完整InkeysRepo.sln Debug|ARM64（原生ARM64 MSBuild，同PowerShell去重复PATH并MSBUILDDISABLENODEREUSE=1，至少5分钟）→严格InkeysHeadlessTests --no-window/现parked/PptCOM→新private smoke，按受影响范围串行及独立实码review。最终Release/其它架构与DComp单元由Root后续安排。

尚待：实际on/off成本和全工具/复杂轨迹、16+200/phase边界/三Release轮、Move/Up与thread CPU、Laser软件成本/生命周期、owner checkpoint/full BGRA/功能等价、fresh生产UInk/Office恢复、GPU/光学、RTS producer quiescence及Win7 SP1仅KB2670838/两GPU矩阵。成功软件Present不是光学或GPU duration；本smoke只U3-H接线与生命周期，不关闭完整U3-F/性能/恢复门。

## 冻结文件身份

| 文件 | SHA-256 | 字节 / CRLF行 |
| --- | --- | --- |
| Draw3.Host.h | 88F13F2A85E83CF71BC9AFC4F618B99BE26B0D1B1A795D1147398693163064A1 | 12379 / 334 |
| Draw3.Host.cpp | 1F6940648B0FB4FE956883EEF112400EC9DDD9BA3EBA41149365D432BA864F59 | 99234 / 2000 |
| Draw3.HiddenWindowTest.h | 479F1FB68899A5060D1D8400D02B3A8C4E8AF7B99EA220C4F123C27F192BE350 | 813 / 15 |
| Draw3.HiddenWindowTest.cpp | F930AFB0A867CECCBD67D5D487EB0E0E7B2E9F7F2C4976E751430AFEC4702200 | 167062 / 2879 |

## Root接管晚Start异常窄修补（未新编译）

原writer已停写，Root交回时线程上限阻止新worker turn，Root唯一接管Host.cpp两处。独立review追到真实Display::Subscribe的make_shared/vector可抛；之前drawing初始化成功就startupFailed=false过早，caller末Subscribe抛后真Stop/Seal可导出失败Start为成功run。现drawing只标!initialized失败，清false移到caller Subscribe返回成功之后、return true之前。其它初始化/停止/Display/driver/Session配置/样本不改，原默认failed覆盖false/throw。晚异常动态注入未新增，实际成功Subscribe smoke与新完整Solution仍待，不能把旧PATCH_READY hash当新通过。

Root新Host.cpp SHA256: 5B1D26939146C7ED05B2700EEA14E0E561D7DDF5792ED43A7066A71299C62642
