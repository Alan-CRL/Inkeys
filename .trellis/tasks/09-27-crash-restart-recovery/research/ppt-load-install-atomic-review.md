# F-040 PPT active load 安装异常原子性独立复审

日期：2026-09-28。本人未参与 F-040 实施。本轮只读 `Draw3.DrawingController.cpp` 当前 diff 与 F-038/F-039/F-031 生产调用链；未改产品/其它测试/父账本，未重跑构建、CLI、allocator fault、Office 或 GUI。任务边界与此前发现见 `ppt-loaded-retained-install-review.md`、`ppt-pending-topology-load-review.md`。本文件只评估 F-040 的 active helper，不把同一巨型 Controller 文件中其它性能/故障改动算作通过。

## 结论

F-040 **在 `InstallLoadedActivePresentationSlot` 内**修复了 F-038 review 指出的“先移动权威槽、后复制 target”的异常半安装风险；未发现本补丁新增的 P1。这个结论是代码与 Debug 编译静态断言层面的，不是已通过内存分配失败注入。生产 completion 的外层仍有 P2 风险，见下节；不能将“helper 失败无状态变化”扩大为“整条 load completion 失败无状态变化”。

| 顺序/合同 | 实际代码证据 | 判定 |
| --- | --- | --- |
| 准入先于准备 | `Controller.cpp:1002-1007` 在 `loaded` 空或 `active.mutationRevision != 0` 时立即返回；未读写 active document/history/retained/target/file/revisions，也不消耗 loaded。 | F-038 新笔迹拒绝门保持，迟到 completion 不能通过此 helper 覆盖用户状态。 |
| 唯一深拷贝先做 | `:1017-1024` 在任何 `std::move(loaded->...)` 前 `preparedTarget.emplace(latestTarget)`；字符串、slideIds 等分配可能抛，catch 打 `reason=target_copy` 并返回 false。 | 拷贝失败时 helper 的 active/loaded 引用目标均未写，原已加载 slot 在函数返回时仍完整；报错不是 success，也未把空 target 装进去。`std::fputs` 是同步错误日志，不能作为 nonblocking 或 durable 日志保证。 |
| 提交段无抛移动 | `:1008-1016` 对全部五类可移动权威字段做 `std::is_nothrow_move_assignable_v` 编译期断言：`optional<InkCanvasCollection>`、`vector<CanvasPageRuntimeState>`、`RetainedPresentationSlides`、`optional<Bridge::PresentationTarget>`、`optional<UInkGuid>`。`:1026-1035` 只把这些类型 move-assignment 或标量赋值写入 active。 | 类型在本翻译单元完整，Debug|ARM64 Solution 已由主 agent 编过，断言已实例化；target 的第二次赋值改为预备对象的 noexcept move。`loaded` 成功后内容 moved-from 是预期，不会被当作新请求再次使用。静态断言防止未来成员类型变化破坏本段合同。 |
| 同槽身份 | 生产 `MaterializePresentationSlot(:789-985)` 以同一最新 target 构造 document/pageRuntimeStates/retained/file/revisions；completion `:6750-6774` 用同一局部 `latestTarget` materialize 并调用 helper。helper 把已准备 target、`latestTarget.pageIndex`、loaded 的 document/history/retained/file/三 revision 一起安装。 | F-031 builder 后续读取 activeRetainedSlides，同 F-038/F-039 已修的值源保持；未重新从 parked 或外部全局 map 取页。 |
| parked 不变 | parked completion `:6786-6805` 仍以 `loaded && parked->mutationRevision==0` 门限整体 `*parked = std::move(*loaded)`，binding migration submit 使用 parked 自身 retained map。 | F-040 没有修改 parked 安装/worker/格式；其运行与异常保障不能由 active helper 的静态断言代替。 |

## 剩余 P2：外层 completion 不具备同样的失败原子性

1. **已确认的状态语义缺口，非 F-040 新引入。** 生产 active completion 在调用 helper 前先写 `activePresentationLoadPending=false`、`activePresentationPersistenceInitialized=true`（`:6764-6769`）。若 target 预拷贝失败，helper 返回 false，document/history/retained 等权威字段确实原样，但 caller 随后销毁局部 `loaded` 并执行 `restoreAfterDocumentSlotSwitch`、`stageWorkspaceReady`（`:6781-6784`）；本槽不会因此自动重试，可能继续显示旧/空页。旧 durable UInk 没有在这个分支被直接删除，但不能把用户可见加载判 PASS。最小处理应在本 completion 边界区别 `loaded` 构造失败、mutation 已使旧结果过期、target 预备失败：过期可以忽略；准备失败需保留明确 retry/失败状态，避免把未安装的快照当成已完成恢复。实现应复用现有异步 load admission 和 revision 门，不能无限自旋或覆盖新笔迹。
2. **上游仍可能抛，F-040 catch 覆盖不到。** `Bridge::PresentationTarget latestTarget = ...` 在 `:6750-6753` 先深拷贝 optional target/strings/vector；发生 `std::bad_alloc` 会在进入 helper 前抛出。`Host.cpp:1197-1214` 捕获整个 `drawing->Run()` 的异常并 `window.RequestExit()`，但该 catch 不是 F-026 的两条显式 fatal 快照分支，不会自动为已完成文档提交最终快照。此为内存压力下的条件性 P2/数据保全缺口，尚无 allocator fault 复现；不能借 F-040 helper 的 noexcept 承诺整个 `PresentationPersistenceCompleted` 均异常安全。若继续收口，可先在分配可能发生的局部建立受控失败返回/日志，再验证 Host 退出与上次有效 UInk 不受影响。
3. **测试范围。** F-038 `RunPresentationLoadedRetainedInstallTest` 已直接调用生产 `Materialize → Install → Build`，覆盖正常安装与 mutation=9 的迟到拒绝；F-039 生产 materializer CLI 覆盖 T1→T2 的双向页身份。当前没有 fault-inject allocator 使 `preparedTarget.emplace` 真抛，也没有 parked 整槽 move 异常注入或真实 Office 载入。因此 F-040 当前仅是静态强保证加编译门，未证明罕见 OOM 时进程/窗口/数据恢复端到端正常。

## 检查过的证据与门禁

主 agent 提供的当前 Debug|ARM64 `InkeysRepo.sln` Build 与 `--draw3-loaded-retained-install-test`、`--draw3-pending-topology-load-test`、`--draw3-parked-ppt-retained-test` exit0 是**自动正常路径证据**；本 reviewer 未重跑。本人读实际 helper/completion/diff 与 F-038/F-039 研究，并独立执行 `git diff --check -- Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`，退出0。后续若主 agent 在本次审查后修改 helper/caller，需重新审最终 diff；Office/WPS 多文稿、磁盘故障、Win7 SP1+仅 KB2670838 的 HARDWARE FL11.0/无 FL11.0→WARP 与真实 DComp/ULW/FLIP 呈现均非本无 HWND 证据覆盖。跨进程 PPT 自动可见恢复入口仍未开放，不因异常原子性修补改变状态。

## Follow-up：caller 失败分流补丁独立复核（2026-09-28）

本节复核上一版报告之后的产品改动；上文指出的 `latestTarget` 上游**按值**复制与 target-copy 失败后直接 ready 两处，在当前源码已部分修正，应以本节为最新判断。本人仍未构建、执行 CLI、allocator fault 或 GUI。

| 项目 | 当前实际行为 | 结论 |
| --- | --- | --- |
| latest target 借用 | completion 在 `Controller.cpp:6753-6756` 从当前 active target、parked target 或 completion target 选择 `const Bridge::PresentationTarget&`，取消进入 helper 前的一次无保护深拷贝。materializer/installer 都在同一绘制线程的当前 completion 命令中使用；active 安装在复制到 preparedTarget 后才覆盖原 active target，且 `latestTarget.pageIndex` 读取先于覆盖；parked 整槽赋值之后不再解引用该 reference。 | 上文第 2 项所说的 **该处**分配异常已消除，没有发现悬空 target 引用的使用。不能推广到整个 Controller 不会分配。 |
| 失败标志 | helper `:1002-1027` 先将 `*targetCopyFailed=false`，`loaded` 缺失或 mutation!=0 的早退不标记；只有 `preparedTarget.emplace(latestTarget)` 抛异常的 catch 才置 true，且权威 move 全在其后。 | 标志准确表示该次 target 准备失败，正常缺文件、materialize 失败与迟到新 mutation 不被误分类为 OOM。catch 的同步 `fputs` 不保证非阻塞日志。 |
| completion 失败分流 | active completion `:6767-6795` 先清 `loadPending`；若 targetCopyFailed，仅置 `persistenceInitialized=false` 并 `continue` 当前命令循环，跳过 `restoreAfterDocumentSlotSwitch` 与 `stageWorkspaceReady`。其它结果仍设 initialized=true，成功迁移/恢复/ready 顺序原样。 | 上文第 1 项“复制失败后直接当作已恢复并 ready”已修正；旧 active document/history/retained/file/revisions 未被 helper 改写，未给这次失败的 loaded 快照成功回执。局部 `loaded` 在本命令结束会销毁，因此这里只保留**重新读取文件的机会**，不是保存待提交快照。 |
| 同 target 条件重试 | `SetPresentationTarget :6820-6847` 在目标等于当前 active 时，只有 `!persistenceInitialized && !loadPending && mutationRevision==0 && observer_.presentationLoadRequested` 才构造 `PresentationLoadRequest`；请求接受后 `loadPending=true`，同一命令不会自排下一次。 | 若以后**确实收到**同 target 命令，路径可达且不会因同一命令忙循环；新笔迹使 mutation 非零时不会让旧快照覆写。observer 拒绝/抛异常时标志保持 false，等下一次外部命令。 |
| 正常/parked | 非 targetCopyFailed 的 active completion 仍按 F-038/F-039 安装或安全跳过，随后恢复 GPU/发布 ready；parked 分支 `:6796-6817` 的 loaded/mutation 门和整槽 move、同槽 retained submit 未改。 | 普通 `Loaded`/`NotFound`/无效载入语义和 parked 槽不因这次 caller 分流改变；正常路径 CLI exit0 只能证明这些有限场景。 |

### 仍在的 P2：重试条件有代码入口，但静止同页不会自动触发

`targetCopyFailed` 后只把 `persistenceInitialized=false`，没有向 WindowController 队列重新投递同 target，也没有保留定时/次数/退避任务。Host `PumpBridgeState` 在 `Host.cpp:859-909` 对桥 revision 未变、`restoreLatestScene=false` 直接返回，且只有 targetRevision 不等于 `requestedPresentationTargetRevision` 或 restoreLatestScene 才生成新 `SetPresentationTarget`；`StateBridge::PublishPresentationTarget` 对相同目标返回原 revision、不发布变化（`Draw3.Bridge.cpp:75-79`）。本次 persistence completion 自身不置 restoreLatestScene。故在 Office/用户保持同页静止的常见路径中，**没有可证明的自动同 target 重试来源**；页/场景真正变化或其它显式重放以后才可能再进该分支。代码没有忙循环，但“自动重试已恢复”不能标 PASS。更严格地说，失败后 admission/ready 保持等待，直至后续目标边沿；真实窗口状态未验。

最小处理取决于要承诺的能力：若目标是仅等待下一次外部页面/场景变化，明确记录为“可在下次目标边沿重试”，并保留不能自动恢复的人工门禁；若要求同页自动恢复，应由已有控制事件或有界退避在**同 key、target/session revision、mutation==0、未在 loadPending** 时投递一次 load，拒绝/再次失败后有限频/次数，不能从绘制热循环自唤醒导致 busy spin。生产 `PresentationAutoSaveService` worker 的严格读回/身份门不可绕过。

### 另一个 P2：重试分支前仍有未捕获 target 深拷贝

即使外部再次投递同 target，`SetPresentationTarget` 在 `Controller.cpp:6823` 先执行 `const Bridge::PresentationTarget target = *command.presentationTarget;`，其字符串与 SlideID vector 拷贝仍可因分配失败抛出；分支内 `try` 只包住 `retry.target = target` 与 observer 投递（`:6832-6844`）。所以 F-040 的异常边界并未覆盖**重试入口**。异常会越过 Controller `Run`，落到 `Host.cpp:1197-1214` 的外层 catch/request-exit；该 Host 异常路径没有调用 F-026 的两条明确 fatal CPU 快照分支，可能丢尚未排队的已完成文档。最小修正先借用 `*command.presentationTarget` 做同目标比较/门禁，待确认需要请求时在 try 内完成唯一深拷贝；不同目标主路径也要单列其既有复制异常风险。此项仅为条件性 OOM 静态风险，未注入内存故障，不能称已复现。

主 agent 的后续 Debug|ARM64 Solution 与 loaded/topology/parked CLI exit0 属于正常路径自动证据；本人没有重跑，且这些 CLI 不强制抛 `bad_alloc`、不证明真实 Office 同页会重新发 target。目标 `Controller.cpp` 的 `git diff --check` 在本次 follow-up 仍退出0。F-031/F-038/F-039 同槽数据身份和 F-040 helper 内提交次序没有因 caller 分流发现新的 P1；上述两个 P2 不应并入“已验证通过”。
