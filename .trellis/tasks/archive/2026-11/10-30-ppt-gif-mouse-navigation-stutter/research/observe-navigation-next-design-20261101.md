# Research: Observe 与导航优先级的下一步安全设计

- Query: 一次 ROT 让出仍出现 1.64s 控件翻页后，能否让已排队 Next/Previous 在新鲜场次身份验证后执行，再刷新页码/descriptor，而不扩大旧场次命令和旧画布写入风险？
- Scope: internal；只读代码、规范和最新采样，仅写本研究文件；不实施产品修改。
- Date: 2026-11-01

## Findings

### 1. 新证据指向两个仍保留的串行阶段

最新采样 `Build/Validation/ppt-navigation-20261101-135913-372-bfe291f9` 的 summary 有 47 条请求、0 issues；7 次 rot-deferred、7 次 rot-forced。以下不是光学换页时间，也不能从无错解析推出产品验收通过。

| 请求 | native 总时长 ms | managed 邮箱 ms | 邮箱中的 ROT 剩余 ms | 随后的完整 Observe ms | owner ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Next epoch39 | 1636.5365 | 1238.9103 | 585.2994 | 653.5884 | 396.8595 |
| Previous epoch47 | 1013.0081 | 873.2697 | 77.0967 | 796.1505 | 138.6909 |
| Previous epoch4 | 925.2565 | 762.4963 | 3.5412 | 758.9417 | 161.2532 |

epoch39 的 owner 另含 Next 末页 precheck 250.1071ms、View getter 62.4652ms、Invoke 83.9622ms。既有 COM 调用自身的时间并没有因调度改变而消失。

代码中 `MaintainBinding` 在一次 Deferred 后下次到期强制完整尝试（`PptCOM/PptCOM.cs:1196-1208`）；慢路径始终 ROT→完整 Observe→邮箱（`:1484-1489`）。因此 forced ROT 的剩余时间叠加完整 Observe 是设计内仍存在的长尾，并非让出分支未触发。不能再通过增加同样样本证明这个组合已经消失。

### 2. 哪些原检查承担安全职责

| 原读取/检查 | 当前职责 | 拆分时的处理 |
| --- | --- | --- |
| fresh ActivePresentation + COM identity (`:1312-1342`) | 阻止借旧 presentation root 对外部已切文稿操作；末文稿关闭有独立 Presentations.Count 证据 | 不能使用缓存身份替代；不同文稿沿用原服务退出/清理，不执行旧请求 |
| fresh SlideShowWindows.Count (`:1343-1359`) | Count=0 明确结束旧场次；不能凭旧窗口仍可读认定仍活动 | 不能省。0 必须立即按原合同退役场次/root；不能仅重读后把一次已观察到的 0 吞掉 |
| fresh Presentation.SlideShowWindow + identity (`:1362-1374`, `:1405-1417`) | 无事件时识别换窗；HWND=0 也能推进新 session | 不能只保留旧 root。相同 identity 的新 getter 返回仍有独立取得/释放 |
| fresh HWND + 非零变化 (`:1397-1410`; `SlideShowSession.cs:80-89`) | 同 COM identity 但可靠窗口能力变化也能推进 session；未知 HWND 不等于结束 | 不能普遍后置后仍称现场场次等价。初版可将 HWND0/解析后备/变化全部留原路径，不能禁止它们的现有基本控制 |
| fresh View.State (`:1383-1389`, `:1421-1422`) | 明确 Done 才识别结束页，并决定 polling 的旧 Next(check) 保护 | 跳过会改变结束页准入。最保守版本保留读取，Done/缺成员等走原完整路径 |
| fresh current + total (`:1376-1396`) | 页数范围与页码自洽；非法/读取失败将生命周期置 Unknown，当前 Matches 不再允许命令 | 它们不只是显示字段。省略会把原先因页证据无效而拒绝的控制扩大为允许，须明确新的“控制身份有效、页面未知”合同，不能声称纯等价重排 |
| generation/binding 前后复核 (`:1400-1410`; `SlideShowSession.cs:166-168`) | COM 读取可泵送 Begin/End/PageChanged；迟到候选不得提交 | 必须复核；它是防迟到屏障，不是 fresh COM 身份查询的替代品 |

Count 缺失的旧提供方用 fresh Window.Presentation identity + fresh View/页证据保底（`:1365-1396`）。新优化不能据此删除旧分支或改为依赖 PIA；初版留完整路径即可，不改变既有支持范围。

### 3. 对“最小 proof 后马上排水”的审查结论

**尚不建议直接实施身份-only快捷分支。** 仅 fresh 文稿/Count/窗口足以提供比 cached stamp 更强的对象身份证据，但还没有证明它等价于当前页面无效/结束页时的命令拒绝行为。当前规范明确要求观察页面/场次后处理邮箱；新合同必须先在任务设计中写清，而不能只给方法命名为 proof 后认为已满足。

有两个可选边界：

1. **严格保持当前准入。** 所有原来能导致 Unknown/Inactive、更新 polling、推进 session 的读取仍须完成后才执行请求。当前数据中大多数 Observe 子项都有这些职责，简单拆方法不能免除其耗时。保留此边界应优先修等待提示，继续如实报告长尾；不要用新的日志层数冒充性能修复。
2. **明确调整控制与页面证据的关系。** 只对已经健康的同场导航，建立 fresh object-directed control proof；页面可暂为 Unknown，画布始终 suspended，current/total/完整 descriptor 在命令后从头重取。它利用现有“基础控制与 InkReady 分离”的方向，但新行为仍需设计审查和覆盖原拒绝分支，不能默认为已授权的等价优化。

若选择第 2 种，以下是一份可审查的最小候选设计，而非已经通过的实施方案。

### 4. 候选方法与状态合同

#### 4.1 入口资格和可返回结果

增加内部 `TryPrepareQueuedNavigation`，仅由唯一 owner 在 ROT 返回且其全部 temporary 已释放后调用。先冻结最多一项队首导航的纯值 request/stamp；正常稳定 Active、同 session/binding、有合法稳定 descriptor、没有恢复重试、polling=0 才走候选。新绑定、Unknown、EndScreen、恢复、队首非导航直接原完整 Observe。

返回纯值枚举如 `Ready / FullObservationRequired / SessionEnded / Disconnected / Stale`，加 proof 中的 session/binding/generation/HWND 与本轮限定请求身份；不返回 View/Window/presentation/ROT RCW，不将临时对象存入调度状态。proof 只在紧接的一次有界导航准入中有效，命令、事件、重绑、失败后即作废。

最小候选保留 fresh ActivePresentation identity、Count>0、fresh Window identity、fresh 非零 HWND 相同，以及 fresh View.State 明确普通放映；任何 Count 缺失、Window 替换、HWND0/变化/后备、Done/State 缺失/busy 或代次变化都不发出 Ready。留原完整路径不等于取消这些提供方的基础控制。

该资格是降低首次实现范围，不是对支持范围的重新声明。是否允许 State 普通但页码不自洽时进行基础导航，是本设计真正需要写明的行为决策。若仍要求原页码自洽准入，就必须同时取得 fresh current/total，收益基本回到完整 Observe。

#### 4.2 必须先退役旧页面可信度

仅 `showObservationPending=1` 不够：它不会更新 `GetSlideShowState` 已序列化的旧 Valid；也不要调用 `ObserveUnknown` 后忽略 Matches 失败去硬执行，后者会改变 lifecycle/generation。

候选需要一个 owner 内纯值的“同场页面 pending”发布：保留 Active session/binding 的控制生命周期，发布 pageStatus=Unknown 和无页面可信度的 Unavailable descriptor（不能把旧 current/total 填回去冒充新鲜基本页）；与 legacy descriptor getter 同步失效。不要清除内部稳定拓扑身份缓存，后续仍按原规则重新核对。

此 pending 应在开始新的 proof/提前执行区间前发布，并保持到**命令后全新** Observe 和 descriptor 正常完成。重绑或 stale 不能回滚成先前 Valid。publish 失败/过期时不得把旧 proof 升为 Ready。

native 已具备接收此状态的关键路径：Unknown page 不能是 trustedPage (`Inkeys/IdtPlug-in.cpp:1112-1116`)，同场且非 basic 时暂停旧 target 输入 (`:1299-1307`)，同场 Pending 保留旧显示数字 (`:1424-1429`)；恢复必须重新 Present/UI ack，不能由 proof 直接恢复 ready。仍需真实生产 helper/headless 回归证明，而不能只在 managed 测试断言 JSON。

此合同只能保证比旧串行 Observe 更早发布失效，不能声称 COM 前的跨线程原子输入 ACK。native 有界采样为 16ms；若产品要求命令执行前已成功封口旧 contact，就需要使用既有 Draw3/业务屏障建立明确 ACK，这已超出单个 managed 方法拆分。

#### 4.3 邮箱不是“有 Next 就全部执行”

`HasPendingNavigationRequests` 只检查队列存在导航 (`SlideShowSession.cs:303-315`)；`ProcessExits` 则取进入时数量后执行所有 Operation (`:400-428`)。Ready 后直接调用现有 `ProcessSlideShowExitRequests()` 会把 ExitShow/ExitAnnotation/View/Activate 一起提前，违反 narrow proof 边界。

最小实现应只处理一项已冻结的队首 Next/Previous，保持 FIFO；不要跳过队首非导航去寻找后面的 Next。冻结时与执行时复核同一 request、取消/超时和 stamp。可以在原 ProcessExits 实现增加受限前缀/计数的内部入口，复用 TryBeginExecution/Complete/diagnostic 的原 finally，不另造取消协议。不能在 proof 之后重新计算整个队列大小，让期间新来的非导航请求混入。

Ready proof 的使用只决定这一次控制准入；原命令自身 View 获取、最终 stamp 复核、Next(check)/polling 末页保护仍保留。不能仅为了节省已测 precheck 250ms就删除它；无事件外部状态仍可能在两次 COM 调用间变化。

#### 4.4 执行后从头观察，不续跑旧候选

无论导航完成、取消、失败或等待方超时，proof 和命令前未发布的 current/total/HWND 候选全部废弃。回 owner 顶层重新取得 full Observe；不能在旧观察中间递归执行请求后继续提交局部值。直到本轮新鲜证据通过 generation/binding、原 descriptor 身份和 native Present/UI ack 后，才恢复 InkReady。

命令执行后的 full Observe/descriptor 不因持续生产者无限后移；限定一项导航后至少完成一次原详细刷新，然后才服务下一轮。原 500ms 慢维护、2s request wait/cancel、8 项队列上限与 ROT 一次让出预算先保持，别同时改两个公平策略。

### 5. 原兼容与错误分支必须逐一保留

| 条件 | 候选处理 |
| --- | --- |
| Count=0 | 按原顺序退役旧 session/窗口、清 raw 和 HWND、发布 Inactive；旧导航零 Invoke；之后重开是新场次 |
| fresh ActivePresentation 不同/断连 | 不发 Ready，沿原断连/服务清理与重新发现；不对旧 window 执行 |
| Window identity 更换（即使 HWND0）/可靠 HWND 更换 | 先完整 Observe 推进 session，旧请求自然拒绝；不转移 proof 中临时窗口 |
| Count 或 State 缺失、HWND0/解析后备、WPS 旧分支 | 原完整 Observe，保持 existing dynamic/基础控制/轮询；不给新支持声明 |
| busy | 不发 Ready；沿原 Unknown/约100ms恢复，不能立刻换 typed 调用或反复读取到成功后硬跑请求 |
| 每个 getter/Release 泵送 Begin/End/PageChanged | 代次屏障拒绝旧 proof；不要把错误归因于“正常 pending”后忽略 |
| current/total 非法但其它对象身份稳定 | 设计决策门；若要求保持原命令拒绝，必须仍读并拒绝，不可隐式放宽 |
| Next 结束页/Check/polling1 | 原保护完整保留；新路径不更新 polling 为猜测值，异常退 full |
| request 未开始超时 / 已开始后超时 | 前者零 Invoke，后者允许原晚完成但不重试、不写到新场次 |

### 6. 实际收益边界

epoch39 完整 Observe653.5884ms 中，Slides total139.6019ms、current156.2448ms。若仍保留同次 View/State/HWND 和其它身份读取，后置这两组理论只减少约295.85ms；本次总量估算仍约1340.69ms，还不含新的分阶段额外调用/日志/释放成本。它本身不能证明解决“1.23s”要求。

如果只保留身份、不读 State/current/total，理论省得更多，但这是上文未完成安全证明的方案，不能据估算先实施后再补安全条件。forced ROT 残余585ms与命令自身397ms仍在，且 Office 每次调用/Release 没有硬耗时上界。

降低 forced ROT 预算或永久允许 Deferred 可以减少部分碰撞，却会弱化新应用发现公平性；这不是无风险参数调优。分块扫描如跨轮保存枚举器/候选 RCW 直接违反当前资源边界；仅保存纯值游标也需要重新定义 moniker 变动/去重/全局 winner 新鲜度，已超过最小修复。

因此建议当前先完成已被用户直接证实的等待提示链修复；调度方面把本文件候选作为**待审查的控制/页面合同变化**，不要在本轮测试正常退出后无依据宣称退出或性能故障根治。

### 7. 最小文件边界与实施门

若采纳候选，预期产品仅涉及 `PptCOM/PptCOM.cs`（pure pending、现场 proof、owner 顺序）、`PptCOM/SlideShowSession.cs`（必要的导航队首受限排水），回归在 `PptCOM.Tests/ManagedCompatibilityTests.cs`/现有 mailbox 测试。只有验证发现现有 native Pending/封口不符合合同才涉及 `IdtPlug-in.cpp` 和对应生产 helper；必须先说明跨层扩大原因，不能静默绕开输入安全门。

实施前：把“页面未知但新鲜同场对象可否进行基础导航”明确到 design/PRD，审查 pending→native suspension/old-number保留的端到端行为，再限定首轮 provider/状态资格（只选择走原分支的优化资格，不删除兼容分支）。若坚持完全不改变当前拒绝行为，本候选不能直接实施。

必要回归：

1. 实际 service/队列路径：请求在 ROT 内、proof 各 getter/Release 前中后到达，全部临时释放后才执行，最多一次 Invoke；队首非导航与 proof 期间并发非导航不被提前执行。
2. 轮询/no events、Count0重开、换文稿、换 COM window/HWND0、同 COM window 非零 HWND变化；旧请求零 Invoke，session 正确推进。
3. Count/State缺失、busy/断连/异常标量；原兼容路径与retry保留，无跨线程 root访问。
4. 每个 COM 点重入 Begin/End/PageChanged、Stop、超时/晚完成，proof 不跨代、不复用，后置 full 候选从头重取。
5. pending publish 前后同时调用两个 getter：旧Valid不可复活，生命周期Active/页面Unknown不被误当 InkReady；native保留旧数字、封口旧contact、新Down拒绝，恢复同页也需新Present/UI ack。
6. 每笔 acquisition ledger、同identity多次返回、子先父后 Release、borrowed root不释放、writer异常/IErrorInfo、无GC/FinalRelease；开关诊断不改变行为。
7. 限定一项有界导航后详细刷新必运行；连续producer不会饿死descriptor/ROT/事件退役。
8. 完整 Debug|ARM64 solution与同次DLL/TLB/资源交接；用户人工性能/三点可见性/正常退出及下一文稿独立验收，不自动GUI或结束Office。

## Files Found

- `PptCOM/PptCOM.cs`：ROT让出预算、完整Observe、pure getter发布、命令末页保护与owner顺序。
- `PptCOM/SlideShowSession.cs`：session/generation、Active命令匹配、纯值序列化和有界多Operation邮箱。
- `PptCOM/PresentationDescriptor.cs`：既有实际C# dynamic accessor和各次临时COM读取/释放。
- `PptCOM.Tests/ManagedCompatibilityTests.cs`：真实owner harness、polling/缺Count/换窗、顺序与scanner释放回归。
- `Inkeys/IdtPlug-in.cpp`：raw/envelope验证、同场Unknown暂停输入和Pending保留数字、target/Present/UI ack交接。
- `Inkeys/Inkeys/Business/PptSession.cpp:14-33`：Unknown/Unavailable只有本次页证据自洽才保留BasicControl，不能用空descriptor伪造有效页。
- `Build/Validation/ppt-navigation-20261101-135913-372-bfe291f9/summary.json`：本轮47导航请求和维护重叠数据。

## Related Specs

- `.trellis/spec/ppt-interop/com-contract.md`：actual dynamic、每笔acquisition与禁止GC/FinalRelease、受限HWND例外。
- `.trellis/spec/ppt-interop/native-session-ui3.md:200`：先观察再有界邮箱；`:300-341`规定现ROT策略后仍完整Observe；基础控制/墨迹门禁与Unknown保留边界见`:21-38`。
- `research/owner-scheduling-options-20261101.md`、`research/rot-yield-design-audit-20261101.md`：已有现场proof与所有权审计；新数据确认其已注明的完整Observe收益边界仍存在。
- `.trellis/workflow.md`：研究仅落盘，不把尚有设计判断的建议静默写进产品。

## External References

无外部查询。结论基于本地源代码、项目规范和采样，不扩展Office/WPS版本支持或引用未核实的外部时效事实。

## Caveats / Not Found

- 本报告没有实施或证明候选快路径，也没有运行GUI、Office、构建、测试或git操作。
- 用户已证实三点不可见，由另一研究代理独立定位；本报告只研究调度与安全合同，不认定渲染根因。
- 一次正常退出并不能定位之前E1430016原因，不能替代PowerPoint真实进程退出/下一文稿测试。
- 行号来自2026-11-01当前工作区；并行改动后应以方法/语句定位。
