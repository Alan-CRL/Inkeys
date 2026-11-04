# Research: 唯一 Office owner 的安全调度与维护让出选项

- Query: 已证实 ROT/Observe/annotation 占住唯一 owner 后，如何减少导航排队且保持新鲜场次验证、dynamic、确定性释放及墨迹门禁？
- Scope: internal；只读当前产品代码、现有测试、规范及人工采样报告；不实施产品修改。
- Date: 2026-11-01

## Findings

### 1. 已有性能证据与尚不能推断的部分

`research/navigation-perf-results-20261101.md` 的人工采样：Previous 总1355.671ms，managed邮箱1198.6715ms，owner执行156.1920ms，focus0.0864ms；同一 owner 上 maintain-binding 的剩余345.2583ms与随后的observe853.3597ms覆盖其中1198.6180ms。Next也有同类703.5293ms排队。annotation还覆盖另一条127ms队列等待。

这已足够证明 owner 排程链是本轮主要空档，但不是“Observe 内某个特定属性慢”的证明。新子阶段数据尚待用户采集；本报告不把读页码、读HWND、读Count中的任何一项写成已测根因，也不把本轮1356ms逐段回填到旧1228ms事件。

### 2. 当前控制流实际保证什么

- `PptCOM/PptCOM.cs:1312-1361`（`PptComService`；新子span编辑后行号会偏移）快速循环先交换 `showObservationPending`，**仅当该标志非零**才调用 `ObserveBoundShow`，然后处理邮箱；慢维护到期后固定执行 `RetryRetiredEventConnections → MaintainBinding/ROT → ObserveBoundShow → mailbox → descriptor → mailbox → annotation → mailbox`。
- 因此不能声称当前每条命令都在执行前进行一次完整现场观察。flag=0的热循环可以直接执行命令。规范和测试明确要求：已有观察提示时先取得新事实，慢维护的现场结果不能被旧stamp替代。
- `PptCOM/PptCOM.cs:1696-1753`（`ExecuteOwnerCommandCore`）执行前核对请求 `Matches(Session)` 和 `IsCurrent(Stamp)`；取得 View/Navigation 后再次核对。Next还读取当前页并保留 `check/polling` 结束页保护。这些是缓存代次+对象定向操作防线，不会自行发现尚未收到事件的另一个show。
- `PptCOM/SlideShowSession.cs:166-168` 的 `IsCurrent` 只比较 **generation和binding**，不是一次live COM身份查询。只有owner观察、binding失效或事件提示推进这些纯值；无事件时未观察到的外部变化不会自动反映在此函数。

结论：不能仅凭“stamp仍current”就宣称新的ROT后/Observe前执行点已获得新鲜现场证明；也不能以已有热循环的缓存路径为依据，额外扩大轮询模式的陈旧窗口。

### 3. Observe各检查的安全职责

| 当前检查 | 职责/失败行为 | 能否直接视为可删除/后置 |
| --- | --- | --- |
| `application.ActivePresentation` 与绑定presentation的COM identity相等 | 防止业务继续针对已切换文稿；不相等时service退出并统一清理/重发现场 | 不能直接省略，特别是无事件/轮询 |
| 读ActivePresentation失败后独立 `Presentations.Count==0` | 确认最后文稿关闭并废弃binding；普通错误/busy不能误判关闭 | 保守恢复分支应原样保留 |
| `SlideShowWindows.Count==0` | 明确Inactive，清窗口root和raw页码；不是“旧Active仍可读所以仍放映” | 不能用cached active替代 |
| 本次 `presentation.SlideShowWindow` 与长期window的COM identity比较 | 无事件下识别真实show替换、推进session；同identity新取得仍单笔释放 | 不能直接省略或只比较HWND |
| Count缺成员时本次 `window.Presentation` identity + fresh View/页证据 | 兼容旧提供方/WPS接口能力差异，确保Count缺失不被误判Inactive或盲信Active | 不能直接切到“有window就允许”路径 |
| 当前View/State、SlideIndex与总页数合法性 | 证明fresh有效页、明确Done/EndScreen，维护raw页码与`polling`及基本控制 | 可能拆分但不是可随意跳过；Next末页/EndScreen保护和未知状态必须保持 |
| `ResolveSlideShowHwnd` | typed受限标量例外+有进程/文稿关联证据的Win32后备；非零HWND变化可推进session | 不能普遍视为无关紧要；HWND=0又不能阻断已确认COM场次的基础控制 |
| generation/binding复核 → ObserveWindow → root安装/转移 | COM可泵送Begin/End/PageChanged，迟到观察不得安装新root或发布旧结果 | 必须保留；不只在命令前检查一次 |

代码锚：`PptCOM/PptCOM.cs:1191` 的 `ObserveBoundShow`；`PptCOM/SlideShowSession.cs:65-136` 的 `Observe/ObserveWindow`。同工作区实现代理正在增加子阶段，行号后移不改变本报告的控制流判断。

HWND尤其需要条件化对待：`Observe`通过 `ObserveWindow(..., hwnd)` 更新缓存，`SlideShowSessionCache.Observe:80-89` 对非零HWND变化推进场次；与此同时合同允许HWND=0仍做基础控制。`ResolveSlideShowHwnd` 原 `:1496-1534` 的fallback还可能读应用HWND、show Count、window.Presentation、FullName和Name，并执行Win32候选关联。若子阶段证明此处慢，需要区分“可信窗口能力刷新”和“对象定向基本控制”；不能直接永久使用旧非零HWND，也不能以0窗口禁止WPS上下页。

### 4. 更改执行顺序的三个高风险边界

**轮询模式未观测变化。** Begin/End事件接口不可用时，root的旧Active/可读View不证明它仍是当前show。当前 `ObserveBoundShow` 依靠fresh Count和COM identity补上这条防线；新优先级不能只看 `showObservationPending==0`。

**COM重入后的纯值过期。** `PptCOM/PptCOM.cs:804-843` 的事件回调只推进generation/refresh hint，不替换root。正常有事件时，读取期间的回调会令最终 `IsCurrent` 失败。但若在Observe中途执行命令，命令在无事件提供方中只置pending，不保证generation改变；旧current/total/HWND局部值仍可能通过cached stamp检查。**任何实际让出并执行命令后，都必须丢弃旧的未提交观察候选，重新取值，而不能继续发布前半段结果。**

**画布准入延迟。** `Inkeys/IdtPlug-in.cpp:1001-1102` 读取raw与descriptor并判断可信页，`:1292-1350` 维护suspended/target/input-ready；如果Office已翻页但新调度把全部raw/descriptor观察推迟，native可能仍同时读到旧raw+旧Valid描述符。不能只让按钮/COM更快，却延长旧页可写窗口。最小身份proof仅可授权对象定向控制，不可发布StableSlideIds/InkReady；必须验证contact封口、及时的pending/未知证据和原target→Present→UI ack链。

### 5. 可安全让出的资源边界

#### 5.1 Observe

当前临时对象包括 `activePresentation / freshWindow / showPresentation` 以及内层 `View/Slide/Slides`；末尾finally按每笔取得释放，只有真正安装freshWindow时将唯一取得转给root并置本地null。`FullCleanup:474-505` 则先失效binding，再解绑和释放长期window→presentation→application。

最保守的服务点是完整Observe返回、其finally已释放全部temporary之后；不能在View/Slide仍活着且观察候选尚未复核时递归排水。若以后拆为身份阶段和详细阶段，各阶段都应先结束自身的temporary作用域，再向owner主循环返回**纯值**结果。原root只能由同一owner安装/清理，不把RCW存进“稍后继续”的调度状态。

#### 5.2 ROT

`PptCOM/PptCOM.cs:872-1114` 的扫描拥有ROT、枚举器、moniker、bindctx、ROT返回文稿、候选Application、ActivePresentation、SlideShowWindow和去重列表。`bestApp`通常只是列表项的选择别名；完整成功最后只转移一笔获胜取得，其余finally释放。

一个合理候选是**安全中断本轮发现，而非在扫描内部递归执行命令**：

1. 在已有外部COM调用返回后的检查点发现pending；不试图取消进行中的调用。
2. 明确返回内部 `Deferred` 状态，不将partial best/priority当完整扫描结果。
3. 让单moniker的finally先释放 View/窗口/文稿/candidate/moniker/bindctx，再让外层finally释放全部列表、枚举器和ROT。
4. 延迟扫描不向caller转移获胜项；当前列表清理有“跳过best”规则，因此必须明确取消best的选择别名，确保每笔列表取得都释放。不能仅 `return null` 而仍让best跳过清理。
5. 回到owner主循环后完成符合当前支持分支的新鲜现场验证，再有界执行请求。下次扫描重新取得对象，不跨轮缓存枚举器/列表RCW。

这是建议设计，尚未实施或实测。只在单monikerfinally后检查最简单，但不能中断该moniker中剩余多个COM查询；若以后在每次调用后检查，则每个提前退出点都必须走同一scope清理与Deferred结果路径。维护耗时和输入延迟收益需要实测。

#### 5.3 不安全或收益有限的简单做法

- 仅在ROT开始前看一次队列：只能降低碰撞概率；本轮两条长请求均在ROT已运行时到达。
- 在ROT后立即 `ProcessExits`，然后继续旧Observe候选：可能使用未验证show/过期页证据，不满足安全证明。
- 在扫描内调用会再次触发Observe/重绑的通用处理函数：容易使scanner持有的借用root/候选与`FullCleanup`交错，扩大确定性释放审计。
- 另开线程访问同一Office图或用timeout撤销RPC：违反当前owner/COM契约，也不会让Office已执行命令消失。

### 6. 建议的最小分阶段策略

**阶段A（当前获批实施）：** 保持全部行为，采集Observe原有成员/帮助函数子阶段。报告每个子项总时长及与排队区间的重叠，不把父子重复相加；用户人工复测同一文件/Previous。当前已证明owner级原因，不再扩大无关渲染/焦点修改。

**阶段B（数据到后再确定）：** 首先据最慢子项选择一种局部边界，不同时重写扫描和观察。可先实现内部 `Deferred`/pending 检查及ROT安全取消，避免当前扫描的后续非必要查询继续占用owner；但如果Observe仍占853ms，不能把这个单独步骤宣布为问题完全修复。保留“无binding必须发现应用”和“未知/缺Count必须保守恢复”的完整路径。

**阶段C（必要时）：** 将Observe拆为“命令前的最小新鲜身份/场次验证”和“页/窗口能力/descriptor维护”。新阶段间只返回纯值proof，含已核对binding/session/generation、窗口身份验证结果与窗口能力状态；不能用时间TTL或cachedIsCurrent冒充现场验证。稳定、已确认路径或许能推迟部分读取，但Count缺失、Unknown、busy、window替换、Inactive、结束页及不可信窗口等分支应先走原完整逻辑，直到有逐分支安全证明。收到事件、重绑或执行命令都会退休旧proof/未提交候选。

这里不指定“某getter一定可省”，也不自行决定新的PowerPoint/WPS支持范围。兼容分支被删除、改成仅现代Office或依赖typed接口都是另一个需明确批准的范围。

**公平性与边界：** 沿用 `ProcessExits` 进入时的有界快照排水，不因持续新请求循环饿死维护；ROT/窗口能力/annotation的延后必须有明确最大延期和重试预算，并有测试证明最后仍会发现新show/新应用。最大延期是设计决策，不能临时凭经验写一个秒数。更长的已进入COM调用仍不可抢占；“调用返回后让出”不是硬实时延迟承诺。

### 7. 必须覆盖的回归与证据

1. pending在ROT/Observe各子调用前、中、后到达；已有调用返回后才让出，不能额外/重复Invoke Next/Previous。
2. 无事件模式：窗口更换且HWND=0、Count0后重开、ActivePresentation变文稿；旧请求零Invoke，新session正确推进。
3. missing Count：fresh window.Presentation错误、View不可用/缺成员、动态基础控制仍可用、window能力0；未知不冒充Active/Inactive，原支持路径保持。
4. busy注入到ActivePresentation、Count、View/State、HWND：返回Unknown/TransientBusy与独立重试，不从busy切typed回退或清根猜结束。
5. 每个COM读泵送PageChanged/Begin/End：候选在正确generation屏障被丢弃，root没有从事件参数借用长期保存；命令View取得后仍再次核对请求。
6. identity阶段后发生真正页面/窗口变化：不能发布预命令的current/total/HWND，不让旧raw+旧Valid延长墨迹准入；验证旧contact封口、新Down和真实Present/UI ack。
7. scanner提前退出：同identity多笔取得、partialbest、重复moniker、异常、每个defer点账本恰好一次；无跨轮RCW保留，无GC/FinalRelease，无root双释放。
8. 已开始请求超过2秒：等待者返回失败/未知，owner仍晚完成；不自动重试、不把晚成功写到新场次；未开始请求超时后永不Invoke。
9. 持续producer、8项队列上限、Stop/drain与公平预算：原有“本轮进入时的有界邮箱”保持；新应用发现/未知恢复/CP解绑重试不会永远延期，且无忙循环。
10. 既有fake ledger之外，最终仍需用户Office实机对照，包括及时退出与下一文稿，不以kill进程或卸载DLL达成通过。

现有可复用测试：`ManagedCompatibilityTests.cs:102` 轮询/同identity取得；`:194` commands/callbacks；`:327` 断连/末文稿关闭；`:845` 有pending先观察；`:875` bounded drain；`:907` command优先于descriptor。`SlideShowSessionTests.cs:243` 旧确认零acquisition；`:360` stopped owner拒绝。新方案应扩展这些生产路径替身，而非只测试另写的优先级模型。

## Files Found

- `PptCOM/PptCOM.cs`：唯一owner、ROT候选取得/转移/释放、现场观察、命令与生命周期处理。
- `PptCOM/SlideShowSession.cs`：纯值session/generation cache、stamp与owner邮箱；2秒等待/取消/晚完成。
- `PptCOM/PresentationDescriptor.cs`：实际dynamic dispatch、HWND受限标量例外、reader及temporary确定性释放。
- `PptCOM/ComEventConnection.cs`：raw事件仅发DISPID hint；不把回调VARIANT物化为RCW。
- `PptCOM.Tests/ManagedCompatibilityTests.cs` / `SlideShowSessionTests.cs` / `Program.cs`：现有生产helper/owner harness、异常点和acquisition账本。
- `Inkeys/IdtPlug-in.cpp`：raw/descriptor可信页与输入suspend/target发布；不能仅用managed调用耗时代表整页ready。
- 当前任务 `research/navigation-perf-results-20261101.md`、`observe-substage-plan-20261101.md`：已测原因、下一阶段授权边界和未测成员。

## Related Specs

- `.trellis/spec/ppt-interop/com-contract.md`：实际dynamic、逐次取得、owner、事件、busy和typed HWND例外。
- `.trellis/spec/ppt-interop/native-session-ui3.md`：场次、窗口能力与基础控制分离；raw/descriptor/Draw3/UI ack输入合同；bounded owner调度和数值诊断。
- `.trellis/workflow.md`：按阶段实施，研究不得覆盖产品代码或规范。

## External References

无外部查询或新增外部版本事实。本研究依据当前本地代码、测试与已批准可执行合同，不扩展PowerPoint/WPS支持声明。

## Caveats / Not Found

- 未进行GUI、Office、COM runtime、编译、测试或任何git操作；仅写本研究文件。
- 当前另一代理只增加Observe子span；本文源码行号对应研究读取时快照，可能随同轮子span插入而后移，应以方法名和条件语句定位。
- 尚无成员级实机耗时，不宣布某一getter/窗口fallback为慢点；尚未实现或证明新的最小校验proof/Deferred scanner。
- 当前缓存热循环本身不是每条请求现场扫描，这一既有边界必须准确描述；不能把新方案标为任意外部状态切换下绝对实时/绝对识别，也不能借此任意放宽现有安全分支。
- 最大维护延期、触发预算及可后置字段需设计批准与实际数据决定；本报告没有代用户定这些产品策略。
