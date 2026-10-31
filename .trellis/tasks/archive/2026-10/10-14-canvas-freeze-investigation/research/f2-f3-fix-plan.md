# Research: F2 / F3 最小修复实施设计

- Query: 在已确认的 F2 PPT 冷加载读取失败误分类、F3 速度橡皮休眠后 Move 不唤醒基础上，给出最小生产修复、真实组件回归和验收范围。
- Scope: internal；只读生产源码及既有测试。本轮只制定计划，不修改产品、不构建、不启动 GUI、不开始实施 task。
- Date: 2026-10-15
- Task: `.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation`

## Findings

### 结论与范围

1. **F2 仅修分类**：在 `LoadPresentation` 中把 `ReadUInkFile` 的 `IoError` 单独保留为 `PresentationPersistenceStatus::IoError`，进入现有 250 / 500 / 1000 / 2000 ms 四次重试。其余严格校验和终态保持。没有必要为此次分类修复增加人工恢复 UX。
2. **F3 单一优选方案**：保留普通 Move 的 mailbox 合并；仅在 Controller 准备进入速度橡皮 idle 等待时订阅一次 Move 唤醒，并在订阅后复查活动 contact mailbox。成功 Move 原子消费订阅后调用既有 `SignalWake`。订阅、复查、等待必须作为同一交接协议实现，不能只增加一个布尔标志。
3. 这些是具体缺陷的修复计划，不构成对原始 Settings → Explorer 用户报告的统一归因。F2 要求 PPT 冷加载路径，F3 要求速度橡皮同一 contact 的 stationary → Move 过程；不同报告涉及鼠标和触摸，不能推出同一现场两设备同时失败。

### Files found

| 文件 | 用途 |
|---|---|
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp` | 真正的 PPT worker、索引选择、UInk 读取与 completion 分类；F2 唯一必要生产行为改动。 |
| `inkStrokeModelerTest/draw3/uink_file.cpp` / `.cppm` | 产品复用的真实 UInk 文件读取、共享冲突有限重试与失败诊断；无需修改。 |
| `inkStrokeModelerTest/draw3/uink_model.cppm` | 全部 `UInkLoadStatus`、文档诊断与源 revision 定义。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 真实 current-load retry / 输入闸门 / materialize-install；F3 eraser idle 等待点和已有生产测试入口。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp` / `.cppm` | 产品 coordinator、mailbox、wake generation 和 event；F3 订阅协议。 |
| `Inkeys/IdtMain.cpp` | 既有生产早退测试参数；本计划优先复用，无需增第三套摘录测试。 |
| `inkStrokeModelerTestTests/presentation_autosave_tests.cpp` | 已调用真实 PresentationAutoSaveService 的服务回归，可沿用 fixture 与 completion 等待。 |
| `inkStrokeModelerTestTests/inkStrokeModelerTestTests.vcxproj:126` | 直接编译产品 PresentationAutoSave.cpp/.cppm。 |
| `InkeysHeadlessTests/draw3_contact_tests.cpp` | 产品 coordinator 的无窗口回归；`RunDraw3ContactInputTests` 在约 1351 行。 |
| `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:130` | 直接编译产品 ContactInput.cpp/.cppm，区别于 demo coordinator。 |

### F2：现有行为和最小改动

`Draw3.PresentationAutoSave.cpp:1468` 的 `LoadPresentation` 已先设置请求 target、slot generation、kind、pageGuid 和 intervalOrdinal。named mutex / track 选择有独立结果；无有效 index / pending entry 才能返回可验证的 `NotFound`（1533–1540）。索引会话、source、key、binding、slide 集合不符在 1550–1565 返回 `CrossProcessConflictDeferred`。

缺陷在 1567–1574：`ReadUInkFile(path)` 后，把任何非 Complete、缺少 document、invalid complete block、recovered content sequence、缺少 sourceRevision、revision 不匹配统一返回 SourceChanged。其中 IoError 并没有证明源身份发生变化，误入 Controller 的不可自动重试终态。

最小生产补丁位置：紧接 `read = ReadUInkFile(path)`，在现有严格 guard 之前新增 IoError 分支并直接返回 IoError；添加简短中文注释解释“读取失败不能视作 revision 已变化，交给现有有界重试”。不调整文件格式、reader 重试、retry budget、slot 生命周期或 UI。

#### 完整读取分类矩阵

`uink_model.cppm:439` 定义七种状态；`uink_file.cpp:1032` 是真实 reader。

| reader / 校验结果 | F2 后 worker 结果 | 是否允许 current-load 自动重试 |
|---|---|---|
| `IoError` | **IoError** | 是，严格受现有四次预算限制。 |
| `RecoveredTruncatedTail` | 保留 SourceChanged | 否，不把部分恢复内容当完整产品恢复点。 |
| `RecoveredInvalidTail` | 保留 SourceChanged | 否。 |
| `StoppedAtCorruptTail` | 保留 SourceChanged | 否。 |
| `RejectedHeader` | 保留 SourceChanged | 否。 |
| `LimitExceeded` | 保留 SourceChanged | 否；此次不重新设计其语义分类。 |
| `Complete`，但 document / revision 缺失、完整块诊断无效、content sequence 被恢复或 revision 不匹配 | 保留 SourceChanged | 否。 |
| `Complete` 且上述严格检查通过，但 importer 或 workspaceGuid 不符 | 保留 Invalid（1577–1584） | 否。 |
| `Complete` + 原 revision + strict importer 成功 | 保留 Loaded | 经真实 Controller materialize / identity / install 后恢复。 |
| 无对应已提交索引及有效 pending entry | 保留 NotFound | 只按现有 verified-empty lane 规则初始化。 |

`ReadUInkFile` 中路径规范化失败、ReadSnapshot 失败等均可产生 IoError（1038–1057），不限于短暂 sharing violation；内存上限 / bad_alloc 是 LimitExceeded（1065–1074）。`OpenWithRetry:195` 已对 sharing / lock 冲突做 reader 内部有限重试。因此不能承诺每个 IoError 都会恢复；只保证它获得原本应有的有界重试机会。索引存在但 UInk 文件暂时缺失也不能降级成 NotFound 后放开空画布。

该分类位置同时影响 Current 和 PreviousInterval 读取，但不应为 PreviousInterval 增添 current-load 自动重试机制；它的现有失败收敛合同保持。

#### 保留的 Controller 闸门

- `DrawingController.cpp:808` `PresentationCurrentLoadRetry` 已实现完整状态机：四次 delay（810–811）、target + slot generation 匹配（909）、pending 防重复、mutation / queued 与安全点条件、exit barrier 和 manualOnly。
- `OnIoError:836` 排期；预算耗尽仍在 848–857 进入 manualOnly。`OnTerminalFailure:870` 仍永久取消该轮自动排期。
- 实际 completion 分发在 12059–12077：IoError 才进入 OnIoError，其他失败进入 OnTerminalFailure，失败期间 `activePresentationPersistenceInitialized=false`。
- `PresentationLoadUnresolved:786` 及 Down gate（9850 附近）、破坏命令 gate（12262 附近）持续保护未成功恢复的槽；不能在“开始重试”时解除。
- `PresentationLoadedForLane:764`、`PresentationEmptyLaneVerified:778` 是不同校验。Loaded 还须 materialize（12055）及 `InstallLoadedActivePresentationSlot:1567`，后者要求 mutation 为 0，并成组安装 document/history/runtime/retained/target。
- `TrySubmitCurrentLoadAtRunSafePoint:949` 在真实 Run（13175）调用；idle 已计算 retry deadline（13255、13269、13287）。不需要新增 timer、线程、无限轮询或重试按钮。
- 同 target 显式命令有取消旧状态后再提交的路径（12135 附近），但 Bridge / Host 去重意味着“任意点一次工具”不等于一次真正 load retry。计划不把此行为包装成用户恢复操作。

**人工恢复 UX 延后**：本缺陷是错误分类绕开了已存在的恢复路径。长期 I/O 失败、预算耗尽和身份/损坏终态继续保持现有行为。若以后希望给用户解释错误或提供显式重试，应单独提出数据安全、scene identity 和 ready 合同；不应扩大本次 minimal diff。

#### F2 测试与验收：用真实 worker 接上现有生产 probe

优先扩展现有 `--draw3-ppt-current-load-retry-test` → `RunPresentationCurrentLoadRetryProbe`（IdtMain.cpp:1342；DrawingController.cpp:4598），保留它已验证的四次退避、pending 防重入、generation / target 变化、Exit barrier。当前 probe 的 completion 是人工构造，不能单独覆盖此次 worker 分类。

新增一组真实文件事务：

1. 在隔离临时 root 内用真实 `PresentationAutoSaveService` 保存合法 PPT snapshot、生成真实 index/UInk；必须含已知墨迹，避免空画布误放行也通过。
2. 用 Windows `CreateFileW` 对索引指向的真实 UInk 持有不共享句柄；随后用真实服务提交 Current load，等待真实 completion。保持锁直到该 completion 返回，避免 reader 内部 retry 恰好错过故障。
3. 断言真实 reader / worker 结果为 IoError，target、slot generation、load kind 不变；将这个真实结果交给现有 `PresentationCurrentLoadRetry` / `TrySubmitCurrentLoadAtRunSafePoint`，验证 deadline 前不提交、到期仅一次、pending 不重复、`PresentationLoadUnresolved` 保持 true。
4. 释放句柄，第二次真实提交应返回 Loaded，且恢复已知 document / stroke 内容。只有真实 `PresentationLoadedForLane` 及现有 materialize/install 路径成功才允许 ready / admission；不能仅凭 `status==Loaded` 手动置 true。
5. 保留原 probe 的四次耗尽、Exit、generation/target 变更用例；这些适合确定性时钟，不需每次真实等待 3.75 秒。文件 I/O 阶段使用真实 completion，并为 worker 设置合理有界测试超时。
6. 回归实际文件 revision 被替换/修改、损坏 header/tail、foreign session、workspace/lane 不匹配：结果不转成 IoError、不 schedule retry、不产生空文稿写回，原文件和 index 保留。已有 `presentation_autosave_tests.cpp` 的严格身份/损坏 fixture 优先复用，不再制造摘录 service。

完整安装/拓扑 gate 继续复用 `--draw3-pending-topology-load-test`（IdtMain.cpp:1330），以及已有真实 `InstallLoadedActivePresentationSlot` 测试（DrawingController.cpp:7150、7178）。新增用例应调用已有生产 helper；如现有 probe 无法覆盖某段局部 install，只做极小共用 helper 提取且 Run 和 probe 同时调用，不能再复制一段 gate。单独 helper 测试仍不能写成“跑过完整 Run/真实 PPT UI”，报告必须准确标出范围。

可把 worker 分类的失败矩阵放进现有 `presentation_autosave_tests.cpp`，Controller retry/installation 继续放现有生产入口；这两层共同证明效果，无需第三个独立 EXE 或新的构建配置。旧 `research/presentation-load-sharing-probe.cpp` 可参考合法 fixture，不能将其摘录 gate 当最终验收。

### F3：现有行为和唯一推荐协议

#### 直接代码依据

- `ContactInput.cpp:771–801`：成功 `PublishMove` 只提交同一 contact 快照并计数，不推进 wake generation。
- `PublishDown:737–768` 成功入队调用 SignalWake；Up/Cancelled（804–817）进入 Close，终态拥有现有唤醒路径。
- `SignalWake:455` 先 release 增加 generation 再 SetEvent；`CaptureWakeGeneration:1125` acquire；`WaitForWake:1130` 先查 generation，再进 event wait。已有逻辑能覆盖 event 在实际 Wait 前到达。
- `HasPendingWork:1027` 只看 ingress/control/command 队列状态，不能证明活动 contact 的 mailbox 没有新 Move。
- `DrawingController.cpp:13298–13327` 在消费 mailbox 之前记录 `frameWakeGeneration`。`RuntimeStroke::lastConsumedSequence:1663` 和 `consumeLatestSnapshot:9502` 已有精确序号。
- `eraserIdle:14471` 由实际物理接触、速度橡皮的无动画/运动年龄决定；14548–14551 无限重复 1000 ms WaitForWake，不再返回外层消费 mailbox。因此成功 Move 可以永久滞留，直到另一个 wake（例如 Up、Down、command）到来。
- `WaitForFrameDeadline:1164` 即使 event 提前醒来也继续等待帧 deadline。对每个 Move 都 SetEvent 会制造内核唤醒，未增加有效渲染频率。

#### 最小生产改动及交接顺序

修改仅限产品 `ContactInput.cpp/.cppm` 和 `DrawingController.cpp`，测试进入现有文件/入口。不改变 RTS admission、接触身份、命令 FIFO、快照内容、橡皮算法或帧率策略。

1. coordinator Impl 新增默认 false 的原子“一次 idle Move 唤醒订阅”；提供 consumer 的 arm/disarm 接口。名称可用 `ArmIdleMoveWake` / `DisarmIdleMoveWake`，语义是订阅已成功发布的 Move，不是队列控制标记。
2. 每个 **成功** PublishMove 在完成快照发布、通过身份/route 校验后，对订阅执行 `exchange(false, acq_rel)`；仅取到 true 的发布者调用既有 SignalWake。它不 enqueue General/Command marker，不计作业务 command，不重开隔离 contact。没有订阅时只多一个原子操作，没有 SetEvent。
3. Controller 在 `eraserIdle` 即将等待时先 arm（`exchange(true, acq_rel)`），再用真实 `TryReadSnapshot(handle)` 逐个复查未 ended、未 awaitingReconnect 的活动 physical contact，比较 `snapshot.sequence` 和 `lastConsumedSequence`。有新序号、终态或暂时读不出稳定快照时，保守取消本次休眠，回到外层消费。不能用 HasPendingWork 替代此复查。
4. mailbox 没新数据时，仍使用 **该帧消费之前保存的 `frameWakeGeneration`** 进入既有 WaitForWake；不得在最终复查之后重取 generation 吞掉已发生的 wake。保留面积/reconnect deadline；订阅至少覆盖速度橡皮 idle 的无期限分支，建议在同一 idle 分支把有 deadline 等待也纳入，以免新 Move 被延迟到面积期限。
5. 离开这次 idle wait 的任何路径均 disarm；用小作用域 guard 避免退出/异常路径遗留。arm、Move 消费订阅、正常 disarm 均采用 RMW exchange 的 acquire/release 交接，避免后续改成无序 load/store 后丢失发布可见性。`ResetForNextRun:1091` 在 producer 停止且 draw thread join 后把新增字段复位，保持原生命周期合同。
6. 保留正常帧 `WaitForFrameDeadline`、图形失败有限退避（14479–14485）、Up/Cancel 和 control/command 的原唤醒。不要为了 F3 往 Host 高频 Move 添加额外 PublishControlWake。

可把“arm → 复查 → wait → disarm”收敛为 Controller 同一私有 helper / scope，让 Run 和已有生产 probe 调用同一实现。只抽取本协议，不搬动整个渲染循环或修改 RuntimeStroke 所有权。

#### 为什么必须复查 mailbox

| Move 到达窗口 | 正确交接结果 |
|---|---|
| 本帧消费后、arm 前 | producer 未见订阅，可能不 SignalWake；arm 后按 sequence 复查必须发现它，立即退出休眠准备。 |
| arm 后、复查前 | producer 原子消费订阅并 SignalWake；复查也可能发现新序号，两条路径任一都能保证不睡死。 |
| 复查后、调用 Wait 前 | producer 看到订阅，generation 改变；WaitForWake 首次 generation 比较即返回。 |
| 正在 Wait | 同一 SignalWake/event 唤醒；下一外层帧读取最新合并快照。 |
| 醒来后、disarm 或下一帧期间 | consumer 已经运行，下一帧消费；下次准备休眠仍 arm 后复查，不依赖上一次订阅。 |
| 多 contact / 多个 Move | 首个成功 Move 消费一次订阅并唤醒，后续 Move 保留最新 mailbox；不会创建 N 个队列标记。 |

仅 `if (sleeping) SignalWake()` 没有 arm 后复查存在“最后读 → producer 未见 sleeping → consumer 开始睡”的竞态。只检查队列也一样漏掉 mailbox Move。

#### 不选的方案

- **每个 Move 无条件 SignalWake**：逻辑简单，但每个 RTS 包执行 SetEvent；正常帧等待会反复醒来再睡，违背 Move 合并/停帧的现有设计。不作为本次优选。
- **把无限 while 改成每 1000 ms 回外层**：能消除永久等待，但仍引入最长近 1 秒输入延迟和定时无工作帧；不是合格交互修复。
- **每 16/几十 ms poll**：隐藏协议缺口并增加静止 CPU/渲染工作。bounded fallback 可以作为另行论证的故障防护，不能代替这次协议；本计划不新增常驻 polling。

#### F3 真实生产回归与验收

第一层扩展 `InkeysHeadlessTests/draw3_contact_tests.cpp`，它直接编译产品 coordinator；第二层复用 `--draw3-control-fence-test` → `RunDraw3ControlFenceProductionProbe`（DrawingController.cpp:4237），以真实 coordinator、真实 idle helper、真实 SpeedEraser idle 条件验证恢复。若现有入口名称不理想，可在内部新增子用例函数，仍由该入口运行，不新增源码摘录 EXE。

必须覆盖：

1. 对 Touch、Pen、Mouse 分别运行 Down → 收敛/idle → 同 contact Move（不发 Up、不额外发 control wake）→ consumer 醒来且读到新 sequence/坐标。只有 Up 后才恢复算失败。
2. 用明确 barrier / 测试 hook 控制 Move 在 arm 前、arm 后复查前、复查后 Wait 前、已阻塞四个窗口到达；测试不得只靠任意 Sleep 碰概率。一次新 Move 应在合理调度容差内唤醒，例如测试等待上限 100–250 ms，不能靠原 1000 ms timeout 通过。
3. 无订阅时 Move 保持不推进 wake generation；idle 期间首个 Move 唤醒、同一 burst 后续 Move 合并，最终快照为最新值。静止无包时不产生额外渲染帧或定时 control wake。
4. Up、Cancelled、Down、General wake、Command wake 在 arm 前后均保留原顺序和终态；有限 area / reconnect deadline 如期生效，退出和 Reset 无遗留订阅。
5. 非有限 Move、旧 handle/generation、已被隔离或复用 contact 不产生非法快照或跨 contact 唤醒语义。遇到 mailbox 短暂写入不稳定应重试外层，不得在未确认情况下无限睡眠。
6. Command boundary 前后的旧 contact Move/Up、新 contact Down 和命令保留原 FIFO / admission；新机制不制造 nullptr marker，不消耗命令 reservation，也不把旧 ContactHandle 重新开放。
7. ARM64 无窗口并发压力回归；无输入 idle 的帧/唤醒次数不增长，正常高频 Move 不新增每包 SetEvent，帧率 deadline 和 SpeedEraser 几何不变。少量额外原子操作成本需记录，但不应先引入更复杂的无锁优化。

注意 demo 的 `inkStrokeModelerTestTests/contact_input_tests.cpp` 使用独立 `draw3.contact_input`，其 `TestWakeProtocols` 明确测试 Move 不 wake；它不等于产品 coordinator。不要为通过 F3 而仅改 demo 或仅跑 demo。产品“未 arm 时 Move 不 wake”仍成立。`InkeysHeadlessTests/wake_signal_tests.cpp` 测的是另一个通用 WakeSignal，也不足以单独证明 F3。

既有 hidden Host 注入路径在 `Draw3.Host.cpp:2111` 附近每个阶段额外发布 ControlWake，会掩盖缺陷；最终回归必须明确禁用测试额外 wake，或直接通过真实 coordinator + 生产 idle helper 驱动。真实硬件触摸验收与无窗口协议验证应分开记录。

### 计划验证入口与构建约束

本轮未执行以下动作。实施时按变更范围复用：

- F2：现有持久化测试集；`Inkeys.exe --draw3-ppt-current-load-retry-test`；`Inkeys.exe --draw3-pending-topology-load-test`。
- F3：`InkeysHeadlessTests.exe --no-window` 中生产 contact 组；`Inkeys.exe --draw3-control-fence-test` 内新增真实 idle 协议用例。
- 产品编译使用既有 `InkeysRepo.sln Debug|ARM64`、当前 VS 的 ARM64 原生 MSBuild，不能单独构建 Inkeys.vcxproj 绕过 PptCOM 依赖。保持用户规定的同一 PowerShell invocation PATH 规范化，至少 5 分钟 timeout，检查退出码及首个有意义错误。
- 保持源码原编码/换行，只改上述必要点；关键并发交接/严格分类原因加适量中文注释。测试通过不授权本研究代理改产品或创建 commit。

## Related specs

- `.trellis/workflow.md`：plan before code、研究落盘、增量推进。
- `.trellis/spec/native-desktop/draw3-integration.md:213`：Presentation lane identity、严格 importer、冷 slot 读取、未就绪输入/命令 gate；249–250 明确 revision 改变和 importer 不匹配不能放行。
- `.trellis/spec/native-desktop/input-and-ink.md:214`、235、247：橡皮收敛停帧与面积到期唤醒；逻辑、隐藏 Host、真实设备证据分列。
- `.trellis/spec/native-desktop/input-and-ink.md:427`：ContactInput generation、终态、隔离和回收合同；F3 不改变这些所有权规则。
- 用户 AGENTS.md：minimal diff、保留格式、默认无 GUI、复用现有构建和适度测试。

## External references

本专题仅以当前生产源码/测试/spec 制定最小实施设计，没有新增外部 API 合同推断。Windows event/RTS 的前轮调查见 `window-input-deep-audit.md`；此次不以模拟注入成功推导真实硬件或原始反馈的根因。

## Caveats / Not Found

- 尚未实施上述补丁，尚未编译或运行新增回归；文中测试均是实施验收计划，不能算已经通过。
- F2 修复的是误分类导致完全跳过既有 retry；超过四次预算仍失败的 I/O 不在本轮新增 UX/无限重试范围。真实损坏、源身份变化、foreign session 不会放开。
- F3 的强结论是特定 idle 协议的 Move 活性缺口；Up / command 会唤醒，所以不能据此宣称它解释跨工具、跨新 contact 的所有持续故障。
- arm 的原子顺序、mailbox 复查、原始 frame generation、清理路径必须同时实现和测竞态；删除任一环节会削弱证明。多设备输入枚举覆盖不等于已做真实触摸硬件验收。
- 本文不延伸 RTS lifecycle、窗口 owner/Z 序、F1 日志和 F4 Settings capture 调查，也不改动其他代理产物。