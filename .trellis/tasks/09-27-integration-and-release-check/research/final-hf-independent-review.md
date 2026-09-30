# H0 到当前 HF 候选的最终独立只读复审

审查时间：2026-09-29；审查范围冻结于当前工作树，`HEAD=8b156fca59f0337a6afc6d722941666fcf143080`，分支 `chore/publish`。本 reviewer 只读了当前 diff、调用链、任务记录和已有隔离日志，没有构建、运行测试、启动窗口、修改产品代码、修改 spec 或修改共享任务账本。以下结论引用的是当前工作树 blob；已有 PASS 只作为证据引用，不冒充本轮动态复跑。

## 结论

当前候选不具备“可以发布”的独立证明。F057/F060/F059 的当前代码路径和已有红绿证据相互一致，PptGate 字段格式、Win7 presenter 选路和 FLIP 约束未发现新的静态错配；启动失败分支的监督建立顺序已修正，但退出监督在双重建立失败时仍没有 15 秒保证，Draw3 Host 的退出保存等待/worker 排空在该失败边界下仍无独立超时。这一条件性 liveness 门与用户报告的画布卡死及“任何结束/重启 15 秒强制”直接相关，应保留为发布阻塞。Win7 真机、真实 Touch/笔、正式 `IDT_RELEASE` CI 门和最终 HF 指纹仍未由本审查关闭。

## Findings

### P1-01：双重监督建立失败时没有无条件 15 秒退场

状态：`confirmed`（静态）；已有隔离测试未覆盖“fallback 线程和 helper 同时建立失败”。

- 当前 `ShutdownSupervisor.cpp` blob `8301a55fbcd3bbeddfeb83f51303364bbe83512b`：`ArmCore()` 先尝试创建本进程 `FallbackDeadlineThread`（`ShutdownSupervisor.cpp:296-301`），helper/握手失败后以 `fallbackArmed ? ArmResult::FallbackArmed : ArmResult::Failed` 返回（`:379-386`）。当两个线程/进程建立都失败时，返回 `Failed`，此时不存在本地截止线程。
- 当前 `IdtMain.cpp` blob `a45440114603e1d6a9ba3629cb4e3178706b5837`：`SetOffSignal()` 在 `:256-279` 记录 `ArmResult::Failed` 后仍继续 `CrashHandler::Shutdown()` 和后续清理；没有直接 `TerminateProcess` 或另一个不依赖线程建立的截止机制。
- 因而正常 Close/Restart 在该触发条件下可能等待卡住的 Window/Draw3/保存清理超过 15 秒。已有 helper/fallback 红绿测试证明的是各自至少建立一个监督时的行为，不能覆盖双重建立失败。

最小处理方向：在接受正式意图的同一低层路径增加不依赖线程创建的有界兜底，或把 `Failed` 明确变成发布不允许的结果并提供故障注入验证。不能用现有 `FallbackArmed` PASS 代替此边界。

### P1-02（范围收窄）：Host 退出屏障与 worker 排空仍无内部超时

状态：`P1 residual; startup ordering fixed pending fault injection`（D101/D201/D301/D401/D202/D102 已前移监督建立；DComp→ULW fallback 与 Host 无界 drain/ArmResult::Failed 链仍未关闭）；未有真实 worker/绘制线程永久阻塞的动态注入。

以下两条早期 bullet 保留为上一指纹的静态证据；当前 IdtMain 启动失败顺序以本节增量复核为准。

- 当前 `Draw3.Host.cpp` blob `63f6518d3aa1f419fb0835e954038df8579ea72f`：`Host::Impl::Stop()` 在 `:1325-1358` 先发布最终保存屏障，然后在 `:1343-1348` 无超时等待 `exitAutoSavePrepared || !running`，之后又调用 `autoSave.CloseAndDrain()` 与 `presentationAutoSave.CloseAndDrain()`（`:1356-1358`）。
- 如果绘制线程在 Controller/Present/Contact 路径卡住，`running` 不会变为 false，最终屏障也不会回执；如果保存 worker 的 I/O 卡住，`CloseAndDrain()` 同样无界。该等待发生在 `Host::Stop()`，不只发生在通过 `SetOffSignal()` 已经成功建立 supervisor 的正常进程退出路径。当前 `IdtMain.cpp:2557-2566,2570-2583,2591-2601,2616-2627` 四个启动失败清理分支都先调用 `StopProduct()`/等待其它客户端，再调用 `SetOffSignal(1)`；这些分支一旦卡在前序清理，15 秒监督器根本尚未启动。DComp 初始化失败时 `:2526-2545` 也先 StopProduct/重建窗口，属于另一条未 Arm 的启动回退路径。
- 当前 `ContactInput::DiscardUntilTerminal()` 的 F057 修补避免了消费者在 `Closing` 上永久等待，但它不能为 Host 的保存屏障、绘制线程 join 或 worker drain 提供边界。因此不能把 F057 红绿证明扩展为“画布卡死必然 15 秒退场”。

最小处理方向：为 Host stop/保存屏障建立明确的 deadline 和失败态，或者保证所有调用点在外部 15 秒 supervisor 已成功 arm 后才允许无界 drain；故障注入必须区分“正常保存”“绘制线程卡住”“保存 worker 卡住”。

增量复核（当前 IdtMain blob a45440114603e1d6a9ba3629cb4e3178706b5837）：上述 D101/D201/D301/D401/D202/D102 启动失败前序已由后续修补覆盖；父任务记录的 Debug ARM64 与 Release 三架构 Build、Headless、PptCOM、supervisor 和 36/36 CLI 均退出 0，但启动故障注入尚未由本 reviewer 复跑。DComp 正常失败回退的 `:2527-2545` 仍在最终 SetOffSignal 前调用 StopProduct/StopAndJoin，属于未 Arm 的剩余 P1；此外 Host 内部无界等待在 ArmResult::Failed 时与 P1-01 形成阻塞链。正常 Close/Restart 已返回 Armed 或 FallbackArmed 时，外部 supervisor 提供进程级 15 秒边界，但不覆盖上述两种失败边界。

### P1-03：正式发布宏仍未在源码中打开

状态：`confirmed`（发布流程门）；不属于产品运行时 bug。

- 当前 `IdtMain.h` blob `32cddfde8356d0f22af18b2e0bc0e1d2db4e800c` 的 `:20` 仍是注释 `// #define IDT_RELEASE`。
- `.github/workflows/build-windows.yml` blob `4a4a9bd8dbb6b6b4e4e413aa676eb73516015984` 的默认 `validate_release=true`，`:52-69` 会要求文件存在精确的 `#define IDT_RELEASE`；缺失会直接失败。
- 任务记录中的 `CL=/DIDT_RELEASE` 隔离 Rebuild 只能证明外部宏可编译，清除宏后的 Rebuild 也不能替代正式源文件宏和 CI 门。不能把该候选称为正式发布构建已通过。

## 已关闭或当前静态未发现新阻断

### F057 ClosingDiscarded

当前 `Draw3.ContactInput.cpp` blob `e9b1792417d7cb40c14ddca969ae84ab09a83777`：

- `Recycle()` 在 `:837-856` 只把 `ConsumerOwned` 释放；对 `Closing` 使用精确 CAS 转为 `ClosingDiscarded`，把最终释放交给唯一 Close producer。
- `DiscardUntilTerminal()` 在 `:858-882` 用 generation/state 精确 CAS，`Producing -> Quarantined`、`Closing -> ClosingDiscarded` 和重复调用均不重复释放；`Close()` 在 `:564-640` 对 `ClosingDiscarded` 进行最终 `Free/ReleaseSlot`。
- 当前 `ContactInput.cppm` blob `72f565da7c8fe80f8a66e403f045ca803bd12854` 的公开注释与上述 owner 合同一致。已有真实 Closing producer 红 1 到绿 0、Headless/完整构建及独立 review 证据支持该窄口径。

保留边界：`AbortUnqueuedDown`、Host Reset 前所有外部 callback 的 quiescence、普通 Controller 页切换的完整生产交错和真 Touch/Win7 仍未证；不能从状态机单测升级为真实现场根因已解决。

### F060 Laser 第二 Touch

当前 `Draw3.DrawingController.cpp` blob `6b77b771c45479a5a35a65c07280853fcd8c752a`：`IgnoreAdditionalLaserTouch()` 在 `:1839-1847` 对禁用多指时的第二 Touch 使用完整 `DiscardUntilTerminal()`，而不是直接 `Recycle(Producing)`；调用点 `:5356-5359` 位于 Down 已进入真实 runtime 批处理前。该路径保持第一根 Laser contact 生命周期，并等待第二根真实 Up/Cancel 后由 producer 回收槽。已有 32 次第二指红到绿和独立 review 可支持该静态结论；真实多指 Touch、设备丢失和 Win7 仍是人工门。

### F059 设置页与 15 秒入口

当前 `Setting.cpp` blob `aba73fd320d244291b026fad8191c16e27af7f50`：

- 直接按钮 `:1576-1591` 先调用 `Setting::Hide()`，随后直接调用正式 `RestartProgram()`/`CloseProgram()`；确认式 Restart 的 worker 分支 `:228-255` 只在 MessageBox 返回 OK 后调用正式入口。
- `QueueRestart/QueueClose` `:458-468` 也直接转发全局入口，未等待旧业务 FIFO 的 I/O 完成。`Hide()` 的 mutex 只保护状态/入队短区（`:8539-8549`），没有把 worker 的 Execute 放在该锁内。

这关闭了“按钮把结束命令排到可能卡住的旧业务队列后面”的静态问题，但 `Hide()` 本身的消息/渲染交接和真实按钮命中未动态证明；P1-01/P1-02 仍决定最终 15 秒保证边界。

### 崩溃/重启链

当前 `ShutdownSupervisor.cpp`、`Helper.CrashHandler.cpp`/`.cppm` blob 分别为 `8301a55fbcd3bbeddfeb83f51303364bbe83512b`、`61b00ff9009e881b853c0dd6fb292362eb614a6d`、`30ff2119b9e5ae067f35c54c18a1551e87572a77`。静态复核未发现旧进程 HANDLE 身份验证、旧进程真正死亡后唯一拉起、mode1 CAS loser 报告截止、mode0/2/3 报告 watchdog、pending `.dm_/.tx_` 不冒充正式产物等新错配。已有隔离真实 `RaiseException` 和三架构最终 suite 日志应分别解释为 dump、报告、重启和 15 秒结束的证据；本 reviewer 没有复跑。

未关闭：P1-01 双重建立失败、mode3 `EXCEPTION_CONTINUE_SEARCH` 的 OS 行为、Win7 DbgHelp/真 GUI 用户确认、绘制/Office 中崩溃和最后 durable UInk 可见恢复。

### PptGate 字段

当前 `IdtPlug-in.cpp` blob `0b457b9795700d6dfa9de439154237f6131f2294`：

- `INKEYS_PPT_TIMING=1` 是静态一次读取、默认关闭；`:782-804` 的 17 个 `{}` 与 17 个数值/布尔参数顺序匹配。
- `sessionActive`、`sessionId=localSession`、`trustedTarget` 语义正确；`:828-877` 的 accepted/document-ready/page-ui-ready 以同一 session/revision 关联。日志不写文稿路径、标题、墨迹或 token。
- EndScreen 的 `slideId=0` 是无 SlideID 哨兵，不应当作真实 SlideID 0。已有真 PowerPoint 页 1/2/EndScreen 日志支持身份/ready 链，但没有证明真 Office 笔迹 durable 或跨进程恢复。

### P2-04：更新链仍只有同源哈希，来源认证按用户决定保留

状态：`accepted residual risk`（用户明确将自动更新安全认证排除本次修改）。

- 当前 `Net.Update.Download.cpp:150-164,211-223` 保留 HTTPS 优先、HTTP 回退；`Net.Update.cpp:152-160` 和 `UpdatePathSafety.h` 也继续接受 HTTP。旧 Inkeys2 兼容回退在 `IdtMain.cpp:1153-1176` 优先尝试 `智绘教.exe`，当前文件名缺失时仍可恢复。
- 版本元数据的 MD5/SHA-256 仍与下载包同源，没有发布者签名或独立信任根。路径、ZIP entry、大小、原子 stage/replace 和普通文件/reparse 检查已收紧，但它们不提供来源认证。该残余不是本轮误修，应在最终报告标为用户接受的 out-of-scope 风险，不能写成安全 PASS。

### P2-05：PPT 双轨/版本化存储的动态范围仍受限

状态：`static aligned; dynamic partial`。

- 当前 `Draw3.Presentation.cpp:368-377` 明确禁止旧 page-index 按 ordinal 升级；`Draw3.PresentationAutoSave.cpp:465-492,522-650,831-1100` 对 StableSlideId、page-index sidecar 和版本化 `.uink` 做身份/路径/事务选择。
- 这与用户“保留旧文件，新会话独立保存”的决定一致；已有 storage red→green 和三架构 hidden Host 日志支持生产无窗窄口径。真实 Office 每页墨迹 durable、断电/磁盘满、跨进程恢复仍未由当前 reviewer 动态复跑，旧文件和新会话不能写成同一文件迁移已通过。

## Win7 / presenter / 设备兼容复审

当前 `Draw3.TransparentPresentation.cpp` blob `25ae2a26345b0fc46dcc66d5641eb287e8a58c81` 与 `Draw3.GraphicsInitialization.cpp` blob `8d5d080835759eb570ffec817e87aebb632f5c31` 的静态合同如下：

- `kTransparentPresentModes` `:94-97` 只有 DComp、ULW；`TryInitialize()` `:689-701` 拒绝两种 DWM 历史模式，DComp API 不可用时直接跳过到 ULW。
- `CreateSwapChain()` `:635-646` 固定 `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`，没有 bitblt/swap-effect 降级。该工作区 spec 已记录用户在 Win7 SP1 仅 KB2670838 的实测约束，不能被微软通用文档冲突覆盖。
- `GraphicsInitialization.cpp:49-73,77-99` 先尝试 Hardware 11.1/11.0，Win7/旧运行时 `E_INVALIDARG` 时只用 11.0 重试；Hardware 失败才使用相同 11.1/11.0→11.0 逻辑的 WARP。静态上覆盖“有 FL11.0 Hardware”和“无 FL11.0 时 WARP 11.0”两条分支。
- `LoadSystemLibrary()` `:47-62` 从 System32 绝对路径动态装载 dcomp，未对 Win7 引入静态 DComp 函数导入；但类型/设备/真正 ULW `Present`、resize、device-lost 和输入仍需 Win7 SP1+KB2670838 真机。

当前没有证据证明 Win7 的 Hardware FL11.0、无 FL11.0→WARP、DComp 缺失→ULW、FLIP 成功 Present、透明/穿透和 device-lost 全链通过。不能以本机 Win11 ARM64/WARP 或 PE import 静态检查替代该矩阵。

## Touch area 新观测门

当前 `Draw3.HiddenWindowTest.cpp` blob `840ed4a8fa516485c2d7686983a9bf56b6bf793f`：

- RunMode 首次 Host snapshot `:910-911` 建立本轮 `down/recycled` baseline；面积 Down 前 `:1795-1800` 比较本轮增量，避免跨 RunMode 累计数造成旧假失败。
- 新 area Down 在 `:1803-1810` 检查 contact generation、Touch 类型和 y=140；35 个 Move/30 ms 轨迹和 `>38.5 && <40.0` 阈值在 `:1817-1837` 未降低；`:1824-1834` 的 `movesPublishedSoFar` 只表示快照时进入 `PublishMove` 的数量，不是绘制消费或成功 Present。
- 任务记录保留旧版 0/1/0，并记录新观测门后的合成轨迹自然退出；这不能裁定旧失败唯一根因，也不能替代真 Touch/Win7 或输入到像素的尾延迟测量。旧失败应继续作为发布验证缺口。

## 生成物和验证范围

- 本轮独立计数父任务 `audit-coverage.tsv` 为 611 行、611 个唯一 SHA，其中 587 个 `in_h0=yes`、24 个 `no`；`git rev-list --count 0da01f3e299d1cf56d96b84374e7a7f9406e71a8..8b156fca59f0337a6afc6d722941666fcf143080` 也为 587。这只核对定义集合的数量/唯一性及 H0 线索，未在本轮重新逐一深读 611 个补丁；各行 review 深度与不可见 refs 缺口以父审计记录为准。611/611 不等于 611 个行为动态通过或 HF diff 已审完。
- 当前工作树仍有四个未跟踪 shader `.cso`：`inkStrokeModelerTest/{inkPixelShader,inkVertexShader,laserParticleEmitCS,laserParticleUpdateCS}.cso`。它们是构建生成物，不能混入源码交付；删除动作不在本 reviewer 权限范围内，最终 HF 指纹应明确是否排除/清理。
- 当前 `Inkeys/Cache/**` 含大量 `.obj`/缓存构建产物，属于既有忽略输出；本审查未把它们计入源码文件审计，也未执行清理。
- 本轮未运行构建、测试、GUI、PowerPoint 或性能采样。父任务已有的三架构 Solution/Headless/PptCOM/UEF/hidden logs 必须按其实际源码指纹引用；最后一次修改后不能复用早期 PASS。最终 HF 应用 `HEAD + 非忽略工作树内容指纹` 表示，不能只写 HEAD。

## 独立结论

当前静态候选具备较好的窄口径修复证据，但至少 P1-01、P1-02、P1-03 和 Win7/真实输入/正式发布等门尚未关闭。父任务最新记录显示 Debug ARM64 与 Release 三架构 Solution Build、Headless/PptCOM、三架构 supervisor 和 36/36 产品 CLI 已退出 0；本 reviewer 未复跑，且这些结果不能覆盖 DComp fallback 未 Arm、ArmResult::Failed 故障注入、真实按钮、Win7 与真输入。该报告不标记 Trellis completed、不标记首发就绪；在双重监督失败路径、DComp fallback 前序和 Host 无界 drain 得到有界合同/故障注入证据前，不能声称用户报告的画布卡死与所有结束/重启均已可靠收口。
