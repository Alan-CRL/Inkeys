# Research: 候选 1 保存快照的所有权与终态释放

- Query: Desktop/PPT 保存任务结束后，active/legacy、合并与编码临时副本能否正常释放；未轮询、失败、替换、停止时由谁持有。
- Scope: internal；只读生产/测试源码，只写本研究文件；候选 6 不修改。
- Date: 2026-11-05
- Evidence: 当前工作区源码静态调查；本代理未编译、未运行程序、未修改产品或测试。

## Findings

### 结论与判定边界

1. **正常保存终态不把大保存快照留到空闲。** Desktop 在每次 worker 迭代结束前销毁搬出的快照；PPT 在向完成队列发布前用空请求替换保存字段。两条路径都不依赖 UI/Controller 随后取走 Save 完成回执。
2. **成功 Load 回执有意持有大快照直到消费。** 不轮询、Controller 有活动接触、Host 预留完成槽占用时可延长其存活；正常消费者消费后不会把原 Load 回执跨 idle 缓存。
3. **Clear 恢复点、文档/Redo/retained slides 是另一类有意持有。** 保存未成功时可能长期留一份清空前的 CPU 墨迹，以便 Undo；不能将其等同于 worker 保存副本泄漏。
4. **保存元数据并非全部归零。** Desktop 会话 records 和 PPT 文件/索引权限账存在长期元数据，规模随请求/文稿/文件变化；未见其中保存笔迹点数组。操作结束不保证进程 Private/WS 精确返回原值。
5. 现有测试支持功能与账面 pending 收敛，**尚无原始大点数组被 delete 的直接断言**。最终“运行验证通过”应由主会话另记，不能把本静态报告包装成堆追踪。

### 文件定位

下列行号是本次读取时的当前源码行号；路径均相对仓库根。

| 文件 | 职责与关键证据 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cppm:43` | Desktop request 按值保存 snapshot；Completion 仅 Load 有 shared_ptr；Diagnostics 是账面估计。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp:1078` | Desktop service 的 queue / records / completions、worker 与终态移交。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cppm:68` | PPT Save/Load request 和 Completion、值/共享指针消费 API。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp:1678` | PPT WorkItem，queue→working→completions 的同节点 splice。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:47` | PPT completion 的服务→Host pending→Window 预留槽移交；Desktop Pump 为局部 completion。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:559` | Window 取命令时 move/pop，PPT 预留完成槽取出后 wake；Detach 清理命令。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:15131` | 完成命令消费为单次 processCanvasCommands 调用的局部变量；物化/过期拒绝后返回释放。 |
| `inkStrokeModelerTest/draw3/uink_draw3_export.cppm:43` | Snapshot 内部 points / strokes / operations / canvases 等是按值 vector，没有共享笔迹全局缓存。 |
| `inkStrokeModelerTestTests/desktop_autosave_tests.cpp:367` | 幂等/完成排空/queuedSnapshotBytes 归零；其他测试覆盖失败、索引、加载。 |
| `inkStrokeModelerTestTests/presentation_autosave_tests.cpp:2801` | latest-wins、异常、终态发布分配失败、重启与加载。 |
| `inkStrokeModelerTestTests/contact_input_tests.cpp:3386` | 现有测试专用全局 new/delete，可复用作释放地址观察；入口在 3455/3463。 |

### Desktop：保存请求与 worker

生产捕获在 `DrawingController.cpp:1596–1600` 将独占 snapshot move 给 observer；`Host.cpp:1322–1331` 再 move 到 service.Submit。服务 `AutoSave.cpp:1312–1325` 创建 request 并 move snapshot；`SubmitLocked:1228–1236` 将 request move 入 Record。queue 与 records 的两个 shared_ptr 指向同一个 Record，**不是两份 snapshot**。

| 持有位置 / 状态 | payload 与释放点 | 说明 |
| --- | --- | --- |
| 入队前的 by-value 参数、临时 request | Closed/Invalid/Existing 返回即析构；接受则 move | `1215–1255`、`1305–1337`；重复请求不创建第二份常驻 Record。 |
| queue + records + worker 的 Record | 等待/写入时按值持有一份 request.snapshot | `1104–1107`、`1145–1149`；worker 取走 queue 元素，records 仍持身份。 |
| 写出/索引局部对象 | ProcessRequest 返回时释放 | `790–867` 包含 catch；CommitUInk 的 exported / session 也是局部（756–787），有深复制但属峰值。 |
| Committed / Failed Record | `1204` snapshot move 到单次循环局部 completedSnapshot；该局部在 `1212` 结束析构 | 成败均执行，不是 vector.clear；`1205`估计归零。日志/PushCompletion 发生在析构前，不能仅在 pending 变0的瞬间声称已 delete。 |
| Save Completion | 只有 operation/status/trigger/fileGuid；无保存快照 | `1187–1192`；未 poll 也不留原保存点数组。 |
| records 幂等表 | 保留 request ID、session、timestamp、文件路径、GUID、终态；snapshot 已搬空 | `1088–1107`、`1203–1205`；Start 清表 `1285–1288`，service/Impl 析构也释放。会话内不是零元数据。 |
| worker idle | 下一次迭代先构造空局部再等 condition | `1134–1143`；上次 completedSnapshot 不跨 wait 留在栈上。线程闭包仅存 Impl 裸指针 `1293–1294`，没有捕获 snapshot。 |

`Diagnostics.pending/queuedSnapshotBytes` 在 `1199–1205` 更新；估计函数 `1016–1039` 仅覆盖工作区名字、legacy canvases、strokes 与 points 的 size，不是所有容量、operations/active/retained/extra 的真实总堆字节。不能用其绝对字节值归因全部 snapshot。

失败没有 worker 自动无限重试；Failed Record 同样释放点载荷。同 saveRequestId 的 SubmitPrepared 被幂等挡回 Existing（1220–1224），不会将旧保存副本重新装进 records。

### PPT：保存、替换与终态

`DrawingController.cpp:14321–14328` 的 request 是局部，move 到 observer；`Host.cpp:1348–1355` move 给 SubmitSave。`PresentationAutoSave.cpp:1887–1891` move 到 prepared.save，预建轻量 completion；`1920` 入队。

| 持有位置 / 状态 | payload 与释放点 | 说明 |
| --- | --- | --- |
| queued WorkItem | save.snapshot 按值完整持有 active/legacy/retained | 等待是实际业务积压，直到处理/被合法替换；不改队列政策。 |
| latest-wins 替换 | `1910` 对旧 WorkItem 做 move assignment，旧保存快照随旧成员被覆盖释放 | 只同轨/文件/槽代次；Clear 是 FIFO 障碍，不能跨界合并（1895–1912）。 |
| working | `1789` splice 同一节点，未复制；worker 引用 item 和 completion | `1791–1808`；全局/闭包未捕获快照，thread只捕获 this（1867）。 |
| SavePresentation 局部合并/编码 | 每个函数/分支作用域返回或异常展开即释放 | Merge `1099` 深拷贝 request、`1139` legacy镜像；buildDocument `1259–1288` 局部 merged/exported，调用即释放 merged。Save内部的读取/导入/session亦为局部。 |
| 发布 Save 完成前 | `1756` completed.front().save={}；`1757` load={} | 整个请求空值移动赋值，不是只 clear 一层points；外层和嵌套 vector 原值释放。所有正常/失败/catch终态都经 `1827` PushCompletion。 |
| 未poll的 Save completion | 同一节点已搬空 save/load，仅轻量 completion | `1766` splice 到 completions，不因消费者慢而占原保存点数组。 |
| 异常 | catch把预建 completion.status设为IoError，再共同清payload并发布 | `1810–1814`、`1827`；不是失败分支漏过释放。 |
| worker idle | working已 splice 空，引用局部结束后condition.wait | `1783–1789`；不持上轮保存snapshot。 |

`pendingIndexEntries` 不是“待重试墨迹缓存”：类型是 PendingKey→IndexEntry（650–653），IndexEntry只有路径/身份/slideIds/revision（275–289）。failIndexCommit保留索引元数据（1438–1455），成功按key erase（1457）；同根Start可保留自修复元数据（1855–1857）。

`ownedVersions` 也是权限/路径账而非文稿副本：`552–568` 有 files/namespaces/knownReferences，`UInkOwnedVersion::Impl` 只含 path 和 PinnedArtifact（`uink_file.cpp:1017–1021`）；PinnedArtifact是handle、revision及flags（109–122），不持文件内容buffer。`files` erase_if（696–702）可能保留顶层 vector capacity，但元素不含笔迹点数组；根/会话变化通过空对象替换（1858–1859）。此元数据增长问题不等同候选1大snapshot泄漏，候选6也不涉及。

### 元数据规模与是否随保存次数增长（静态字段账，非实测）

- **Desktop records 确实会随已接受的唯一保存请求单调增长，直到下一次成功Start清表或Impl销毁。** Record有4个成员，其中request有8个直接字段；大snapshot搬空后，仍有map key一份ID，加request内ID/session/三个timestamp字符串，共6个窄字符串，request.proposedFileName与record.committedPath共2个宽字符串，以及GUID/状态/空snapshot容器控制对象。没有按terminal淘汰records的路径。按当前标准格式：3份36字符GUID字符串 + 日期10 + 文件时间9 + createdAt29 = **156字节可见窄文本**；proposedFileName正常为23个wchar（46字节），再加`2×committedPath字符数`。即每成功记录至少`202 + 2L`字节可见文本内容（L为完整路径字符数），还未计结尾零、capacity/SSO、allocator、map节点、shared_ptr控制块及Record/snapshot固定sizeof。这个公式不是Private估计，也不声称每份都有独立堆分配；失败record的committedPath可为空。
- **Desktop Save completions** 未消费时也是每终态一条小型节点，正常Host消费后释放；其中无target拓扑、没有点数组。Load completions是另一类大payload，不能混入这一“小型”结论。
- **PPT没有Desktop式每save永久Record。** queue/working/completions节点被消费或替换后销毁；普通同identity pending请求会合并。Snapshot的canonical `ReadUInkFile`/imported/merged/session是局部，没有常驻canonical snapshot字段。
- **PPT Save completion仍携带target拓扑。** `Bridge.h:105–121` 的target含4个字符串（sourceIdentity/presentationName/provider/bindingToken）和`vector<int32_t> slideIds`。合法target最多10000页（Bridge.h25/171），故一份满拓扑的**元素载荷最多40000字节**（不含capacity/节点/字符串）。如果刻意不消费，终态数量×拓扑可累积；正常Host会唤醒/移交。它不含strokes/points/operations/extra，不能称为保存墨迹常驻。
- **PPT pendingIndexEntries按失败identity/generation增长，不是每次save必增。** 一个PendingKey有6项（其中3个字符串），IndexEntry有7个字符串、slideIds和标量revision等；同key失败赋值会替换旧entry，成功同key erase。不同slotGeneration发生索引提交失败时可各留一条，同根Start不清老generation，根变更才clear；所以不能承诺该map有固定条数上限。每个应用生成的有效entry的slideIds载荷最多40000字节；这里没有Json::Value/UInkExtra/strokes/points。若长时频繁新generation且持续索引失败，这是独立的元数据增长观察项，非普通成功保存的点载荷泄漏。
- **PPT ownedVersions.files不是save次数单调增长的活元素表。** 每次授权写出可push一条，随后current/backup/pending引用判定会erase过期authority（689–758）；正常重复写同一批文稿可收敛。files的vector容量可能保留高水位。namespaces与knownReferences按track根缓存；同autosave根/同进程session最多当前5个track根（799–807），根/session变更会整体重置ownedVersions（1858–1859）。每根knownReferences包含索引当前/备份/失败自修复所需文件路径集合，随文稿/引用数变化，并非一个保存一份墨迹。
- 类型证据：snapshot的extra是`UInkMessagePackValue::Map`即value vector（`uink_model.cppm:46–62`），不是Json::Value共享指针；Save局部Json索引不被存入Record/WorkItem长期字段。Desktop搬空的snapshot里不再保留原extra/points容器载荷；PPT save={}替换原值。只剩空容器控制对象或Debug STL小型迭代器代理，不应与大像素/笔迹数组混计。

### Load 完成回执：应保留但必须被消费

- Desktop `ReadCommittedDesktopPath:703–735` 读取、导入并 move到 `shared_ptr<const Snapshot>`；读取/导入中间态返回释放，**loadedSnapshot在completion里存活**。
- PPT `LoadPresentation:1597–1664` projected为局部，最终 move到 completion.loadedSnapshot；正常Current可能仍有 active/legacy双投影。PreviousInterval `1623` retainedCanvases.clear销毁内部对象但保留该外层capacity，直到整个Load快照销毁；不是常驻worker缓存。
- 两服务的 `CloseAndDrain` 只停止接收、排空worker并join（Desktop1371–1380、PPT1979–1987），**不主动清理未取走的Load completions**。因此“只调用CloseAndDrain后必然完全释放Load快照”不成立；成功Start清completions，服务析构也清。停止后复用同一service而不Start/不消费可保留加载payload，这是API可达、有意保留终态的边界。
- 正常Host由completion callback唤醒（`Host.cpp:1371–1375`）；ConsumeBridge无论General/Command都Pump两服务（2020–2021）。PPT Drain先存 `pendingPresentationCompletion`，Window槽未空就保留，成功移交后reset（47–59）；Window取reserved completion后额外wake推进后续，不依赖新输入（WindowControl559–580）。
- Desktop Pump的 completion 是函数局部并move进CanvasCommand（Host1963–1974）。PPT Host最多额外保留一份pending，但service仍可有多份合法排队Load。
- Controller仅在active接触为空安全点消费（DrawingController15160、16749–16754），所以接触持续中可延长Load存活；并非处理完成后的普通idle。command是processCanvasCommands调用局部（15131–15159）；空队列break后lambda返回（16102），上次回执同时释放，无最后一条大completion跨帧缓存。过期/不匹配continue也按相同局部生命周期释放。
- 正常安装会物化业务文档（Desktop15262–15266、PPT15447–15525）；该文档有意留下，原Load快照不被拷贝到长期shared_ptr字段。Host Stop会reset pending并Detach清Window命令（Host2583–2593、WindowControl385–394），但服务里尚未Pump的Load终态只能等Start/析构或显式消费；不能凭Stop代码保证其瞬时为零。

### 应与冗余保存副本区分的业务存活

| 存活对象 | 释放/保留条件 | 证据 |
| --- | --- | --- |
| Desktop Clear前恢复canvas | Committed且Clear/GUID匹配才reset；失败仅退diskSaveRequested，保留Undo兜底 | DrawingController15234–15248；第二次有效Clear替换老恢复点15805–15809，恢复成功15266/15856也reset。 |
| PPT Clear boundaryFallback | 匹配文件/轨道/interval的Committed才reset；失败或保存未接受保留 | 15294–15338、15812–15816。 |
| ClearRedoContinuation | Clear撤销后保留后继CPU分支；PPT slot有Redo不淘汰 | 519–540、789–796；不是可随idle抛弃的临时编码内存。 |
| 活动/停放PPT document、retainedSlides | 已提交且无load/Redo等保护条件才允许淘汰；未提交/失败槽保留 | DrawingController771–796、15342–15343、15524–15525。 |

主会话/另一研究代理负责capture、导出和白板完整审计；本表仅给出保存服务消费端必须保留的业务边界，不变更其语义。

### 最小运行验证建议

1. 先运行现有共享测试 `--desktop-autosave-only` 与 `--presentation-autosave-only`（contact_input_tests3455–3464）；这两入口无GPU/产品窗口。产品`--draw3-ppt-completion-delivery-test`（IdtMain1793）补Host/Window真实FIFO移交，但当前主要是NotFound小回执，不代表大payload释放。
2. 若要直接证明保存点数组实际释放：复用测试专用全局operator delete（contact_input_tests3406–3424），增加固定容量、不分配的地址观察表；构造大snapshot后登记原 points、strokes、canvases、operations等独占分配地址，再move提交。等worker终态且稳定空闲、故意不poll完成回执，确认这些地址经过delete；同时覆盖成功、failUInk/索引失败、PPT异常和排队替换。无需修改生产服务API/策略。只比较地址，不读已释放内存；避免观察表自身分配/重入。
3. Load用weak_ptr：消费后保留weak指针，确认caller重置最后strong owner后expired；故意不poll时应仍未释放，Start清回执后应expired。可结合真实Host reserved槽与Controller消费验证，区分终态持有与泄漏。
4. Private/WS时间曲线只是进程提交/驻留现象，不能独自证明对象存活；测试构造的原snapshot若未move而被fixture保留，会使“保存后还有一份点数据”成为测试自带保留。不要通过强制GC、WorkingSet修剪、改变架构或候选6来制造回落。

## Related Specs

- `.trellis/spec/native-desktop/index.md`：产品Draw3/Window/UI与设备边界。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：异步值快照、真正move、资源所有者和统计口径。
- `.trellis/spec/native-desktop/memory-resource-lifecycle.md`：维持已批准资源生命周期、L0/L1/L2和双device隔离。
- `.trellis/spec/native-desktop/draw3-integration.md`：正式Host、保存/恢复/输入安全点与功能边界。

## External References

无新增外部资料。结论由本地生产实现和现有测试定义支持；未以第三方文档代替当前源码证据。

## Caveats / Not Found

- 本报告是静态所有权审计，未取得ETW分配栈、live heap快照或采样数据；正常静止是否留OS分配器高水位由运行验证补充。
- 没发现保存快照在worker成功/失败终态后被大缓存永久保留的确定缺陷；但未poll的Load回执、保存失败的Clear恢复点以及未持久化业务槽确实可以继续持有大内存。
- 原CLI测试检查功能/计数，未直接检查vector capacity或原分配delete；pending/queuedSnapshotBytes变0早于Desktop completedSnapshot局部析构，必须避开该短时观测竞态。
- 不修改候选1去重策略、候选6、文档表示、UI/画布device或图层架构；不安装/升级工具，不启用注册表堆追踪，不打开Office。
