# 画布等待、诊断输出与设置输入边界

本合同来自 `10-14-canvas-freeze-investigation` 及四个修复子任务。四项分别有确定的触发条件；不能由修复这些缺陷推定所有“UI 可操作而画布无响应”的用户现场具有相同根因。运行结果与平台边界见任务的验证报告。

## F1：Draw3 标准流诊断不得阻塞绘制与 RTS

### 1. Scope / Trigger

修改 Draw3/RTS 的 stdout、stderr 诊断、配对 debugger mirror、控制台初始化或主程序诊断输出器的退出时适用。真实 UInk/索引保存和显式开启的独立尾轨迹文件协议不属于可丢弃诊断。

### 2. Signatures

生产实现为 `Draw3.DiagnosticOutput.h/.cpp`，命名空间 `Inkeys::Drawing::Draw3::DiagnosticOutput`：

- `Available(Stream = Stream::Output, bool mirror = false) noexcept`。
- `Write(Stream, const char*, size_t, bool mirror = false)`、`Text`、`Printf`、定长 RAII `Line` 和 `Wake`。
- 唯一串行 owner 持有 `Session`，调用 `Start()`、`Stop()`、`Snapshot()`；`StopResult` 为 `Disabled / Stopped / IsolatedPending`。
- 早退命令 `Inkeys.exe --draw3-diagnostic-output-test`，严格要求只有该参数；harness 关闭或多余参数返回 2，不启动 GUI。固定 helper fixture 必须在 harness 关闭时不参与编译。

### 3. Contracts

- 生产者只尝试路由与队列锁，不执行外部 I/O、阻塞取锁或同步 fallback。固定 `RecordCapacity=1024`、`TextCapacity=4096`；满、争用、关闭时可以丢弃诊断，不能影响业务准入。
- `Printf/Line` 及 Cursor/FrameTiming 在格式化前检查目的端可用性；其他已审入口按诊断开关可先做定长预格式化，再由 `Write/Text` 检查接纳，不宣称全部调用点在无 sink 时无格式化开销。`Line` 用本地定长缓冲，长记录标记 `[truncated]`。API 保留调用线程的 `GetLastError`。Cursor 重新启用可重置序号；序号标识生产事件，不保证跨线程取号顺序等于入队顺序。
- 唯一独立 worker 持有标准句柄副本，在队列锁外写原 stdout/stderr；原配对 mirror 也由该 worker 执行。部分写继续，零进度/断管/错误禁用对应端并更新接纳掩码，其他端仍可用；本 Session 不自动重连坏端。没有任何可用端且无 debugger 时不建立 worker。
- Snapshot 区分 accepted/written/full/contention/closing/truncated/sinkErrors 等；存活的停读端恢复读取后可输出统一 drop 摘要。truncated 仅计统一输出器自己的截断，上游 Cursor 320 字节截断有独立文本标记；不可用端的入口早退也不全计入 closing，零计数不能证明所有上游事件/文本完整。不能把 lossy 日志当成完整事件链。
- `Stop` 从首次进入起共享同一个 1000ms 绝对期限，最多 100ms drain，再重复 `CancelSynchronousIo` 和短等待。`ERROR_NOT_FOUND` 不证明后续没有 I/O，必须覆盖最后检查与实际提交之间的窗口。
- 只有路由解除且真实 worker signaled 后才释放 state/event。超期只保留一份不引用 Draw3、HWND、COM 或 owner 栈的独立状态，禁止第二 worker，不无限 join、不 `TerminateThread`。普通 15 秒退出监督或测试父进程强杀不算正常 Stop 通过。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| stdout/stderr pipe 读端存活但停止读取 | 阻塞只发生在诊断 worker；生产者有界返回，队列不增长到固定容量之外 |
| 无目的端或初始化失败 | 不发布半初始化路由；重复 Stop 安全 |
| 单端损坏 | 禁用该端，其他端继续，不在业务线程补写 |
| 首次取消早于实际 WriteFile | 后续取消仍能结束真实 pipe 写入 |
| 无法取消的外部 I/O | `IsolatedPending`，独立状态保活，后续启动被拒绝 |

### 5. Good / Base / Bad Cases

Good：停读管道时仍能画出新笔迹并正常退出。Base：普通文件与畅通管道保持目的流和文本字段。Bad：把同步 `cout` 换成绘制线程排空的队列，最终仍在同一绘制线程调用 WriteFile。

### 6. Tests Required

真实生产自检覆盖双目的端、实际 pipe 背压、恢复、部分写、四生产者饱和、断管、取消提交间隙、隔离和 GetLastError；实际 Cursor/HRESULT/PptTiming helper 进入同一生产输出器，并在普通文件中核对字段；真实 pipe 背压由独立用例覆盖。完整产品另验证停读时新墨迹、工具、Clear、设置响应与自然关闭；普通 console/QuickEdit、真实触摸/笔及 Win7 分别记录。所有直接编译迁移源的消费者必须链接同一实现，当前包括 Inkeys、InkeysHeadlessTests、inkStrokeModelerTestTests。

### 7. Wrong vs Correct

Wrong：RTS 入队，绘制线程 Flush 时同步写控制台；Stop 第一次取消返回 NOT_FOUND 就永久 join。Correct：业务线程只发布定长记录，唯一输出 worker 承担外部 I/O；按同一绝对期限重复取消并保留无法结束的独立状态。

## F2：PPT 已提交 UInk 的暂时不可读必须保留 IoError

### 1. Scope / Trigger

修改当前进程 PPT 冷 slot 加载、严格导入、worker completion 或重试闸门时适用；与 [PPT slot 存储合同](draw3-integration.md#scenario-ppt-三态-slotuink-自动保存与当前进程恢复) 一起使用。

### 2. Signatures

`PresentationAutoSaveService` 的真实 Load worker 调用 `ReadUInkFile`；产品复用既有 current-load retry helpers 和 CLI `--draw3-ppt-current-load-retry-test`、`--draw3-pending-topology-load-test`，不新增无限重试入口或用户配置。

### 3. Contracts

- `ReadUInkFile` 返回 `IoError` 时，completion 保留 `IoError`；不能因没有 snapshot 将它先变成 `SourceChanged`，也不能当作不存在文件并安装空白画布。
- 只有该分类修正先于原有严格校验。源/key、workspace、文稿/页身份、revision、格式和导入检查继续按各自原语义处理；foreign session 仍按既有独立 storage track 策略隔离，不能简化成所有 foreign 一律拒绝。
- 当前目标自动重试沿用四次预算及 250/500/1000/2000ms 退避。目标/代次变化、退出与迟到 completion 的原门控保持；Loaded 且已有恢复点的路径在真实 materialize/install 成功前不提前开放准入；已严格确认 NotFound 的独立空槽仍按原语义初始化，无需虚构一次 materialize。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 已选中提交文件临时共享冲突 | IoError 进入有限重试；解除占用后同目标恢复原墨迹 |
| source/key/workspace/revision 或内容严格校验不符 | 保持原拒绝/隔离语义，不因新分支安装错误数据 |
| 四次预算用尽 | 保持原耗尽语义，不自动无限尝试 |
| 目标、generation 改变或 PrepareExit | 不让旧 completion 改动新目标 |

### 5. Good / Base / Bad Cases

Good：短暂独占读取冲突解除后恢复已保存笔迹。Base：未修改的 warm slot 与正常 cold load 行为保持。Bad：将任何无 snapshot 情形都标记为 SourceChanged，使暂时 I/O 错误直接进入 manualOnly。

### 6. Tests Required

生产 CLI 必须用真实 worker 保存非空 UInk、真实不共享文件句柄制造失败、真实 completion 驱动既有重试并 materialize/install，核对原笔迹；保留预算、目标/代次/退出用例，覆盖 source/key、workspace、revision、坏 header 和截断 tail。既有 `--presentation-autosave-only` 另验证 foreign track/旧文件保护；文件回归不代替真实 Office/WPS 联动。

### 7. Wrong vs Correct

Wrong：`IoError -> snapshot == null -> SourceChanged -> manualOnly`。Correct：先保留 IoError，其余结果继续原严格检查，成功后才安装对应目标数据。

## F3：橡皮 idle 交接必须保留下一次有效 Move

### 1. Scope / Trigger

修改 SpeedEraser 静止判定、coordinator Move mailbox、活动等待、deadline 或重启复位时适用。默认活动 Move 仍允许覆盖合并，不改成每 packet 内核通知。

### 2. Signatures

- `ContactInputCoordinator::ArmIdleMoveWake()`、`DisarmIdleMoveWake()`。
- `PublishMove(...) noexcept`、`TryReadSnapshot(...)`、`CaptureWakeGeneration()`、`WaitForWake(observedGeneration, timeoutMilliseconds)`。
- Controller 的生产 `EraserContactsIdle`、`WaitForEraserIdle` 同时被 Run 和 `--draw3-control-fence-test` 的确定性回归调用。

### 3. Contracts

- 单消费者在决定 idle 后先 arm；使用每个仍有效 runtime 的真实 mailbox sequence 与 `lastConsumedSequence` 复查，终态、失效 handle 或不稳定快照均返回外层重读，不能入无限等待。
- 成功发布且路由匹配的 Move 以 `acq_rel exchange(false)` 消费一次订阅，再调用既有 `SignalWake`。非法/被拒 Move 不消费订阅；无订阅的普通 Move 不额外唤醒。
- 等待沿用本帧消费前保存的 `frameWakeGeneration`，不能在复查后重新捕获代次吞掉通知。arm/disarm 同样为配对的 RMW；有 deadline 的 idle 分支也走该握手。
- RAII 在正常返回、退出和异常时 disarm；Reset 只能在原生产者与消费者均已停止的边界清除订阅。Up/Cancel/control、接触面积和重连语义保持。

### 4. Validation & Error Matrix

| Move 到达窗口 | 必须保留的证据 |
| --- | --- |
| 最后消费后、arm 前 | arm 后真实 sequence 复查发现变化 |
| arm 后、复查前 | 一次通知与 sequence 至少一路保留 |
| 复查后、实际 wait 前 | 帧初 generation 仍能发现已发通知 |
| 已进入 WaitForWake 且尚未返回 | 通知唤醒，不依赖 Up 或额外 control |

### 5. Good / Base / Bad Cases

Good：按住橡皮直到收敛，继续 Move 即继续擦除。Base：持续移动时仍合并 mailbox，不每包 SetEvent。Bad：等待固定 generation，但 Move 永远只覆盖 mailbox，必须 Up 或其他窗口控制命令才能恢复。

### 6. Tests Required

生产 helper 用 Touch/Pen/MouseLeft/MouseRight × 四个交接窗口 × 有/无 deadline 共 32 组，验证共享 idle helper 的通知协议（测试 deadline 为传入的人工 2000ms），不能替代完整 Run/Host/GPU 或真实 area/reconnect deadline 计算；第四窗口以 WaitForWake 进入计数及短时间未返回确认，不声称进行了内核阻塞采样。成功条件不能依赖 Up 或隐藏 Host 注入附加的 ControlWake；另测 deadline、退出、异常、终态及失效 handle。headless 覆盖无订阅、一次订阅、burst、多接触、拒收/隔离与 Reset。逻辑设备枚举测试不等于真实触摸/笔硬件验收。

### 7. Wrong vs Correct

Wrong：复查后捕获新的 generation，再等待；或任何 Move 都发内核通知。Correct：一次 arm、复查真实已消费序号、等待帧初 generation，离开等待即 disarm。

## F4：设置隐藏必须结束原生捕获和旧 ImGui 输入会话

### 1. Scope / Trigger

修改 Setting 的 Hide/HideAll、快速重开、WM_SHOWWINDOW、捕获与 session 生命周期时适用；保持 [Setting 统一渲染与业务队列](rendering-and-ui.md#setting-统一渲染与业务队列合同) 的线程所有权。

### 2. Signatures

`Window::Service` 的 Setting owner-thread 隐藏事务；`SessionState::SetVisible(bool)`、`InvalidateSession()`、`Resolve(epoch, hasSession)`、`CommitEpoch(epoch)`、`Release()`；Setting WndProc 的早期 `WM_CAPTURECHANGED`、`WM_SHOWWINDOW(FALSE)` 分支。

### 3. Contracts

- Hide、HideAll 与 Show 遇到 shutdown 的回退均在真实 HWND owner thread 检查 `GetCapture() == settingHwnd` 才 `ReleaseCapture()`；其他 HWND 的捕获保持，不全局取消 Draw3/RTS contact。
- ReleaseCapture 可同步重入 WM_CAPTURECHANGED，该消息在 ImGui guard 前返回。原生隐藏消息也在该 guard 前处理，只用短状态锁 InvalidateSession，锁外请求 Settings 帧，不新增 owner 等待 render 的边。
- 逻辑 Hide 锁存失效；Show 和旧帧 CommitEpoch 不能清掉。迟到的实际 native Hide 也失效当前 session，但不覆盖更晚的逻辑 Show 意图。
- 旧 backend/context/SRV/呈现资源仍由原渲染 owner drain/destroy。只有旧会话销毁或确认无旧 context 的新建边界才能 Release 消费失效，随后建立干净输入会话，不在 HWND owner 直接改 ImGui 全局 IO。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 内容区 Down，按住时 Alt+F4，再正常 Up | 隐藏后不再捕获；同位置工具点击立即生效 |
| 捕获已经转移其他 HWND | 隐藏设置不释放其他窗口的捕获 |
| Hide→Show 合并或旧 Resolve 后才 Hide | 旧会话必须 drain，旧 Commit 不能吞失效 |
| 新会话已建立，旧 FIFO native Hide 才到达 | 再次失效输入，保留更新的 Show 意图 |
| 退出时按住或重复 Hide | 幂等清理，不新增跨线程等待 |

### 5. Good / Base / Bad Cases

Good：按住设置时关闭仍能点选橡皮，重开首次点击正常。Base：普通 X、resize、occlusion、epoch 和正常退出语义保持。Bad：只在非 owner 的 Setting::Hide 线程调用 ReleaseCapture，或只修捕获而复用旧 backend 的按下状态。

### 6. Tests Required

真实 SessionState 头文件回归覆盖锁存与全部交错。默认 `InkeysHeadlessTests.exe` 的真实 WindowService 测试须出现 `PASS setting capture ownership`，验证 owner/其他 HWND/HideAll/shutdown；`--no-window` 不运行该捕获测试。完整产品另走内容区 Down→Alt+F4→Up→同位置工具点击，检查实际 capture、新线、重开首次点击、正常 X 和按住退出；独立 probe WndProc 不代替完整 Settings/ImGui 验收。

### 7. Wrong vs Correct

Wrong：owner 持 ImGui 锁清 IO，或 Show 直接清失效而复用旧 session。Correct：owner 只结束自己的捕获并发布失效，原渲染线程完成旧会话销毁，新的会话边界消费失效。
