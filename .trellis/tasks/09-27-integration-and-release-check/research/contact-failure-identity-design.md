# E03：初始化拒收的接触身份与普通页边界验证设计

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。负责人：`contact_failure_identity`。基线 HEAD：`e32a5fc06096c1e4ab88a29866c60ebe323fd722`。

本文件仅为设计。尚未改生产或测试；等待主会话 `GREEN_DESIGN` 与 `E01_UNIT_FROZEN` 两个信号后实施。主会话独占构建/CLI 槽，所有动态结果目前为 **未验证**。

## 行为缺口与代码证据

`DrawingController::Run::initializeStroke(handle)` 在入口验证 record 地址与 generation，再复制该 record 的不可变 Down（Controller.cpp:5202–5206）。以下三个失败分支拥有旧 handle，但把 consumer 的拒收转成按 tablet/contact key 寻找当前 producer 的 Cancel：

| 分支 | 现有拒收代码 | 应保留的其它清理 |
| --- | --- | --- |
| `acquireStroke()` 返回空 | Controller.cpp:5709–5717，`PublishCancelled(key, cancelledDown)` 后 `Recycle(handle)` | 直接 `return false`；尚未取得 runtime，不虚构 runtime 释放 |
| `runtime->stroke.modeler.Reset(modelParams)` 非 OK | 5805–5819，同上 | 错误报告、`runtime->cancelled=true`、`handBackSpeedEraserController`、清 handle、`inUse=false`、返回 false |
| `stroke.modeler.Update(kDown)` 非 OK | 5857–5879，同上 | 同 Reset 分支；不制造成功笔画、历史或光标提交 |

`acquireStroke` 扩展池在 modeler.Reset 非 OK 时返回空（5153–5171）。`PublishCancelled` 通过 `FindProducing` 只寻找同 key 的 Producing/Quarantined record（ContactInput.cpp:473–491、807–812），并没有 consumer 所持 record/generation 的参数。

### 合法的串行 producer 时间线

1. RTS 同一个 key K 发布 `A.Down → A.Up → B.Down`。A 已 ConsumerOwned，但其 Down 还在 FIFO，consumer 尚未初始化 A。B 使用另一槽，仍是 Producing。
2. consumer 取出 A，随后遇到上述任一失败分支。旧 `PublishCancelled(K)` 找到 B 并把 B 变成 Cancelled；之后回收的却是 Ahandle。
3. B.Down 已被接受，后续 Move/Up 失去对应正常路由，形成条件性的输入失效。它不要求两个 RTS writer callback 并发，writerMutex 不能保护来自 consumer 的这条反向 key Cancel。

初次使用的两个不同 record 都可能 generation=1。身份须比较 `(record地址,generation)`，不能以两次 generation 相等或大小推导它们属于同一接触。

源码已确认错误目标修改的可达条件；尚未动态注入。不能把它称作用户截图画布卡死的唯一根因，也不宣称正常配置中这三种初始化失败普遍发生。

## 最小修改边界

- 唯一生产修改范围为 `Draw3.DrawingController.cpp`。将三处相同的路由拒收收敛到内部 `RejectStrokeInitialization(ContactInputCoordinator&, ContactHandle)`（暂定名）后，最终实现只调用现有 `DiscardUntilTerminal(handle)`。
- 先以旧行为实现这一真实共用 helper，三个真实失败分支均调用它，再加入 regression 并交 root 运行红灯。随后仅改 helper 为精确 Discard；runtime 其它清理逐句保留。
- helper 不接触 GPU、文件、COM、窗口或业务锁，不按 key 重查目标、不读取新 generation 的 Down，不引入另一取消算法。
- 删除失败分支后续的无条件 `Recycle(handle)`。Discard 已处理 Producing→Quarantined、Closing→ClosingDiscarded、ConsumerOwned→Recycle 与 stale handle 拒绝；不提前归还仍物理按下的 route。
- 正在 Producing 的初始化失败接触仍占其槽直到真实 Up/Cancel。这是现有拒收合同，避免迟到 Move 被误认成新接触；不从 consumer 伪造物理 Up 或 Touch End。
- 不修改成功初始化、modeler 更新率、采样、宽色/pressure、cursor 原有清理、history、保存、PPT gate 或设备线程分工。不开放导航/白板等关闭入口。
- 现有 `RunParkedDesktopExitAutoSaveTest()` 无 HWND、无磁盘，已从真实产品 `--draw3-parked-desktop-exit-test` 执行，可在末尾追加同 module 内的 `RunRejectedStrokeInitializationProductionTest()`。这样不用改 IdtMain、工程文件或新增 public probe API。输出单独用 `[Draw3InitRejection]`，不把它的 PASS 混成 parked-save 或完整 GPU 初始化通过。
- 本单元预计无需 ContactInput.cpp/.cppm 或 Headless 修改。若后续 Abort 定向失败 hook 必须添加，将另报小合同并等 root 冻结接口；不得在本单元顺手修改状态机。

## 生产共用清理的 red→green

测试执行三处真实失败分支共用的同一个生产 helper，不复制正确算法。阶段 1 不声称已逐个触发真实 modeler/扩展池失败；三调用点通过独立 diff/call-chain review 核对保留清理，完整 Run 的显式故障注入作为后续覆盖。

### I01：旧终态 A 与新同 key B

- 用真实 Coordinator 串行发布 A.Down/A.Up/B.Down，再按真实 FIFO 获取 Ahandle、Bhandle。
- 断言 A、B 地址不同（generation 可相同）；失败 helper 只接受 Ahandle。
- 断言 B 初始仍为 Down，B.Move 可被接受且可读、B.Up 保持真实终态；Ahandle 拒读。旧 key helper 会把 B 改成 Cancelled，自然红灯。
- 回收 B 后 occupiedSlots=0，A/B 终态及回收计数各一次。红测的必要尾部清理不得影响前面的实际失败断言。
- 复用 A 所在槽取得 C 新代次，重复旧 A helper、Recycle/读均不能触碰 C。另构造 stale A 与同 key 当前 C，用地址+generation 而不是只比较 key 验证。

### I02：正在 Producing 的初始化失败

- 拒收后该槽仍 occupied，Quarantined 不可读，Move 不再被消费；真实 Up 与 Cancel 两种终态分别归还一次。
- 二次拒收不重复计数或释放；无新 Down、伪 Up；真实 terminal 后再 Down 成功。
- 旧 helper 在物理终态前就 Cancel/Recycle，测试应红，不能以 Up 返回 false 作为正确结果。

### I03：真实 CAS Closing 暂停

- 复用现 `PauseNextCloseAfterRouteClosedForTesting`，producer 对精确 key 的 Up/Cancel 在成功 Closing CAS 后暂停。
- consumer 调真实失败 helper，最多 100ms 观察其返回；此时槽仍 occupied、handle 拒读。放行 producer 后最终 terminalPublished/recycled 各增加一次、occupied 归零。
- 不使用 SuspendThread、不 detach、不销毁仍引用暂停对象的线程。任何断言失败均先 release pause，再 join producer/consumer 后返回失败。
- Producer 先完成再 helper、consumer 先 Discard 再终态两种顺序同测。

### 命令和证据

root 串行使用完整 `InkeysRepo.sln Debug|ARM64`（原生 MSBuild，同 invocation PATH 规范化，至少 5 分钟），再运行现有 `Inkeys.exe --draw3-parked-desktop-exit-test`，保存 red/green stdout、stderr、退出码、EXE SHA-256 和 source 指纹。

root 后续重跑严格 Headless、相关 Draw3 CLI/hidden 与 Release 三架构。worker 不起任何构建/测试进程，不抢当前 E01 验证。绿色只证明共享初始化拒收路由及单释放合同，不能外推真实 RTS 驱动、GPU、Win7 或主观落笔体验。

## 普通页边界 Closing 的必要后续单元

当前正常 `sealPresentationContacts`（Controller.cpp:6307–6350）在 AdmissionRevision 改变后，用最后已消费实点调用原 `completeModelUp`，清 gesture route，最终在烘干/历史提交后精确 Discard（10345–10358）。本次 identity helper 的 Coordinator probe **不替代该普通页命令的全 Run 验证**。

### 必须验证的真实链路

1. 隔离隐藏 ProductHost/双 HWND，以稳定 SlideID 的 A 页完成成功 Present/UI ack；普通笔 Down/Move 逐次等待 production sequence 被消费，冻结其最后真实点。
2. 对该旧笔在真正 Close CAS 后暂停 producer Up，改真实 PPT target 或调用精确 presentation input suspend，使 AdmissionRevision 改变。只点击同一 Desktop 的 Clear 不一定改变 revision，不能把等待 active 收尾误判为页边界死锁。
3. 在 producer 未放行时，观察真 `Controller::Run::sealPresentationContacts` 与后续 B workspace/command 进度，至少取得 B 成功 Present/ready。旧笔只进入 A 的 history/UInk 一次，B 无旧笔；暂停槽不提前复用。
4. 放行 producer 后单次回收；B ack 后新笔正常；Clear/Undo/Redo 的既有 FIFO 不丢命令。每条线不得靠 latest-only/降样本使断言通过。
5. 另测已排队但被 admission 拒收的旧 Down 在 Closing 暂停，走 processCommand 的现有 `DiscardUntilTerminal` 分支，不能在 B 重开时补画 A 的笔。

### 所有权依赖

真实 fixture 需 Host 内部 input pause/control 暴露、HiddenWindowTest 的真实 Product/Window/Present/保存观测。Host.h 为普通 header，而 pause 类型附着 contact module，不可随意跨 global/module forward declare 制造 ODR。

本 worker **没有 Host、HiddenWindowTest、Window 或 Main 的写入权**，因此不从 Controller probe 伪造同名“PPT页切换 PASS”。建议 root 冻结一项显式 hidden-test driver 合同后，由 Host/fixture 唯一写入者实现；本 worker 只在需要时提供 Controller 只读状态/最小 default-off 测试接缝。不要为这个 fixture 开放现关闭的 canvas navigation；普通笔即可覆盖 active route，gesture 的既有 Discard 状态机另验证。

## Abort、Reset 与硬件范围

- `AbortUnqueuedDown` 在 PublishDown 入队失败时由原 producer 自清；真实 RTS Down/Up/Disabled 受同 stateWriterMutex 和 packet reader gate，不能把任意 public Coordinator 并发当成实际 RTS Down→Up 交错。identity 修补消除 consumer 反向取消另一未入队 record 的旁路。
- 现没有定向 Down enqueue-failure hook；可在独立小单元加一次性 default-off hook，核 rejected+1、accepted Down 水位不增、无指针出队、occupied 回基线和下一合法笔。不得改 Abort 正常算法来满足不可达人工交错，也不新增 blanket timeout 提前 free writer 使用的 record。
- ResetForNextRun 只在全部 producer 静止和 consumer join 后合法。正常 Host/RTS Stop 的现有所有权与失败 COM quiescence 分开；失败 HRESULT 后真实 callback 是否仍进入是残余假设，需要 RTS/Host 专项证据，不由本单元声称已解决。
- Win7 SP1 仅 KB2670838、Hardware FL11.0/无硬件 FL11.0→WARP、ULW FLIP、两 DWM 禁用、真 Pen/Touch 与现场驱动环境仍独立验收。

## 已读上下文与交付

已读取完整 hook 的保存文件（工具输出再次截断后按实际文件继续）、implement.jsonl 和所列规范/研究、子任务 PRD/design/implement、父 handoff/completion、input-closing-quiescence-postcommit 与 startup-boundary-postcommit；读取 native-desktop index/input/draw3/errors/conventions/build、native quality（旧独立 demo 构建入口不覆盖主 Solution）、guides/reuse/cross-layer，并执行 get_context --mode packages（单仓库）。当前共享 E01 和父记录改动已识别并保留。

设计通过且 E01 冻结后，每个单元交付改动文件/符号、行为变化、实际 red/green 原始路径和退出码、独立未验证硬件路径、剩余风险。由 root 安排独立 review/修正复验/规范记账。无 commit、push、切支、worktree、归档或任务结束，无 computer-use，不操作用户 PID/真实配置/Office 文档。
