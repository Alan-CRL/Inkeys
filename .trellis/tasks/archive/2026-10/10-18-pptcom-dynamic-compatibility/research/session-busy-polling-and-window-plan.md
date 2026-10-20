# Research: 会话 busy、无事件轮询、窗口身份与最小修复边界

- Query: 核对 20261014a 的正常/无事件联动路径；制定类型接口不可用但 dynamic 可用时的状态发布、busy 恢复、重开、窗口回退、基本控制和页级墨迹隔离方案。
- Scope: mixed；当前源码静态调查为主，Win32 官方文档核对为辅。
- Date: 2026-10-18
- Phase: planning；仅本文件写入，无产品/规范/配置修改，无构建、运行 Office、GUI 或 git 操作。
- Evidence: 父会话此前做过生产 DLL busy/HWND 故障注入；本研究没有重复执行，仅把这些结果与当前源码相互核对。

## Findings

### Files found

| 文件 | 职责 |
| --- | --- |
| `PptCOM/PptCOM.cs` | ROT 绑定、类型事件、raw 页码、500ms owner、busy 重试、descriptor 刷新、旧窗口回退和命令 |
| `PptCOM/PresentationDescriptor.cs` | 纯值 descriptor、反射 accessor、当前页/State/完整拓扑读取与临时对象释放 |
| `PptCOM/SlideShowSession.cs` | binding/session/generation、生命周期、缓存发布、owner 邮箱、带 session 的 Exit |
| `Inkeys/IdtPlug-in.cpp` | 新 envelope 消费、raw 一致性、窗口准入、控制队列、Draw3/UI ready |
| `Inkeys/Inkeys/Business/PptSession.{h,cpp}` | envelope 严格解析与 native session token |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Presentation.{h,cpp}` | descriptor 解析、目标解析、stable/page-index 身份与旧页保持策略 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp` | 现有 stable、page-index、process-local 的存储隔离，不能绕过 |
| `Inkeys/Inkeys/UI/Ppt/Ppt.cpp` | native session 与分页 UI 发布；当前 UI 数字与 Draw3 target/回执相连 |
| `PptCOM.Tests/{Program,SlideShowSessionTests}.cs` | fake reader ledger、cache/session/exit/mailbox 测试 |
| `InkeysHeadlessTests/{ppt_session_tests,presentation_descriptor_tests}.cpp` | parser、token、descriptor/身份 helper 测试，尚不覆盖生产 PptInfo 全准入 |

### 1. 当前调度及正常、无事件分支

1. `PptComService` 先处理 descriptor pending 与 owner 退出请求，再判断慢维护截止；事件 `Wake` 不直接重复 ROT 扫描。依据 `PptCOM.cs:1214`、`SlideShowSession.cs:276`。慢维护正常间隔为 500ms，不是 native 的 16ms。
2. 类型 `Application` 可用时依次注册 Next/Begin/End/BeforeClose。前三项全完成才置 `bindingEvents=true, forcePolling=false`；类型转换失败或注册抛错置 `bindingEvents=false, forcePolling=true`。依据 `PptCOM.cs:1310`。部分订阅成功后失败没有回滚；所有权/事件审计研究应覆盖。
3. 有事件时，换页事件直接写 raw 当前页，推进 `PageChanged()` generation，再设置 pending，owner 可快速读取 descriptor。依据 `PptCOM.cs:829`。raw 总数并非每次换页都更新，文稿增删页等可能短暂与 descriptor 不一致。
4. 无事件时，`forcePolling=true` 每个维护周期读取 raw 当前/总页，并在周期尾强制读取完整 descriptor；不是仅轮询当前页。依据 `PptCOM.cs:1483`、`:1593`。完整 SlideID 拓扑因此可能约每 500ms 枚举一次。COM 调用增加属于确定的调用形状，是否造成实机延迟/busy 仍未测量。
5. 有事件时漏事件或事件实际不可用但注册成功，raw 页码轮询仍可能等距 `updateTime` 超过 3 秒；拓扑有独立 3 秒稳定复核。依据 `PptCOM.cs:1483`、`:312`。native 16ms 读缓存不能缩短这些上游等待。
6. `UnbindEvents` 仅在 `bindingEvents && pptApplication != null` 时清 `forcePolling`。开始服务只清 `bindingEvents`，没有显式清 `forcePolling`；正常后续成功注册会重设，但部分绑定、busy 中断时可能遗留上次能力状态。依据 `PptCOM.cs:348`、`:1200`。不应仅凭布尔默认值推断本次事件有效。

### 2. busy：当前行为及不能采用的简单修复

`RefreshPresentationDescriptorIfNeeded` 有三个独立问题/限制：

- `keepStable` 只核对 binding、session、旧 status，没有核对本次是否已换页、generation、raw 当前/总页或 SlideID。`PptCOM.cs:299`。
- 旧 getter 可能保留 stable，新的 session 发布仍使用本轮 busy candidate/Unknown。`PptCOM.cs:304`、`:310`。父会话注入已证实新 envelope 为 `TransientBusy/Unknown`。
- 不论候选是否 busy，仍设置约 3 秒的下一次稳定刷新截止。若无 pending/force 且旧 stable 与 raw 一致，恢复后不能及时重读。`PptCOM.cs:269`、`:312`、`:1588`。无事件分支通常会被 `forcePolling` 在下次维护覆盖，因此不能把“busy 必停 3 秒”泛化到所有设备/所有事件。

新 native 只对 `Valid` 且 descriptor/raw 页码相符的候选开普通页目标；同 session/binding 的未知目标保留旧画面并暂停输入。`IdtPlug-in.cpp:663`、`:800`。这项暂停本身有安全意义：新页尚未证明，旧 stable 不能宣布新页可写。

**禁止把修复简化成 `Publish(oldStable, "Valid")`。** 一次 busy 可能发生于真实换页、同页内容/拓扑变更或新场次；即使 raw 数字暂时未变，也不证明没有换页/重开。旧 getter 的 stable 是最后已知快照，不自动代表本次事实。新旧状态应使用同一套“候选选择与新鲜度”规则，但无需为了 JSON status 相同而冒充新鲜成功。

另外，`HandleBusyException` 会先 Observe Unknown、发布 Unavailable lifecycle，然后 Sleep(200) 并重试；10 秒超时后某些调用点退出/清理/重绑。`PptCOM.cs:524`。descriptor-only busy 不经过这条代码。这两种 busy 来源必须一起测，不能仅测 reader/helper。

### 3. 无事件结束/重开存在额外旧窗口风险

每个维护周期都新取 `ActivePresentation.SlideShowWindow`，但仅在长期字段为空或 `IsValidSlideShowWindow(old)` 为 false 时才替换。`PptCOM.cs:1428`。所谓 valid 仅是读取 `old.Active` 不抛异常，甚至不检查返回值。`PptCOM.cs:808`。

因此，若一次结束后旧窗口对象仍能响应属性，后续重开即使取得不同新放映对象，也可能继续用旧字段读 raw/descriptor。明确 `showCount==0` 时当前实现没有及时清空长期 `pptSlideShowWindow`。若类型 HWND 又始终为 0，`Observe` 的 HWND 差异检测也无法补救。`PptCOM.cs:1474`、`SlideShowSession.cs:80`。

这是静态确认的可达风险，不等同于所有 PowerPoint/WPP 都会保留可读旧窗口；实机需分别验证。修复必须比较**本轮实际取得的放映对象 COM identity**，不是仅比较旧对象可读性或等 HWND。

### 4. HWND 回退与现有身份强度

- descriptor 的 HWND 非 busy 失败会转 0；其余读取自洽时仍可为 Stable/Valid。`PresentationDescriptor.cs:228`、`:287`。
- 新 native session 路径只接受 descriptor HWND，只有旧 DLL 路径才调用 `GetPptShow()`。`IdtPlug-in.cpp:697`。这确实可能使 dynamic 页码正常而类型窗口接口异常的设备失去全部 UI/命令入口。
- 原 `GetPptHwndWin32` 仅匹配“可见窗口标题包含文件名和应用关键字”，唯一即返回；没有放映窗口类、COM 绑定进程、进程实例或 COM 文稿身份关联。`PptCOM.cs:1805`。文件名并非完整路径，同名文件、多实例、编辑窗、其他程序标题均不能仅凭唯一结果排除。
- descriptor 的 PID 当前从同一个 HWND 反查，native 再比较一次 PID。这是 OS 一致性检查，并非独立的“COM 文稿所属进程”证明。`PresentationDescriptor.cs:288`、`IdtPlug-in.cpp:701`。不得把新增 PID 字段当成已具备 COM→PID 绑定的证据。
- 现有运行期销毁检测包含 `IsWindow` 和 PID 改变，但不含进程创建时间；若 HWND 与 PID 同时被复用，或同进程内部复用，不能仅凭当前检查保证新旧场次区分。`IdtPlug-in.cpp:674`。UI focus 亦在操作前检查 HWND/PID，仍需匹配当前 session。`:464`。

## Proposed repair decisions

> 主会话整合说明（2026-10-18）：本文的“暂不新增无窗口UI”是最窄范围建议。用户将损坏设备基本可用性列为首要，最终 design.md 已纳入现有分页控件的基础控制/墨迹页码局部分离，不新建窗口。无可靠HWND时仍可按当前COM场次执行对象定向基本命令，但不得发送墨迹ready、抢焦点或猜测窗口归属。实施以最终PRD/design/implement为准。

### 5. 能力分层：优先保持可证实的基本 dynamic 功能

建议在现有 binding/session token 和窗口验证中做局部拆分，不建立新的通用能力框架。以下为规划规则，尚未实施：

| 可证实条件 | 保留能力 | 必须隔离/关闭的能力 |
| --- | --- | --- |
| IDispatch application/presentation/show 对象可用；本轮 owner 确认活动绑定/场次；类型/事件接口失败 | dynamic 上一页/下一页/结束、页数读取、提供方支持的总览与原生工具操作；无事件轮询继续 | 不因类型事件失败整体拒绝；每个可选成员失败仅该功能失败 |
| 上述条件成立，但无法证明 COM 文稿→进程→放映 HWND 的强关联 | managed对象定向控制和状态读取仍可用；有界重试窗口发现，native报告可诊断降级 | 本轮最小范围不新增无窗口UI，不承诺全部native控件入口可用；不自动抢焦点、不按猜测HWND激活、不开放页级墨迹/文稿sidecar |
| dynamic HWND 直读成功，或有证据的成员包装例外/受验证 Win32 回退成功，文稿与当前页自洽 | 基本控制 + 当前窗口 UI + 正常页 target；后续仍需 Present/UI ack 才开放输入 | 旧会话确认/命令、旧 ready 不可复用 |
| 窗口可信、SlideID/完整拓扑不可得，但 fresh 当前页/总数和绑定身份自洽 | 现有显式 PageIndexFallback，受现有 binding/session 存储隔离约束 | 不生成假 SlideID，不把部分拓扑合并到稳定 SlideID 存储，不自动以旧拓扑重排 |
| 已确认 EndScreen，但完整结束页拓扑暂不可得 | 基本上一页/下一页/结束控制，显示安全待定 | 不用最后一页或部分拓扑伪造独立结束页 ready |
| 核心 IDispatch 不可达、当前绑定对象未知，或本次 busy 尚无 fresh 页事实 | 保留同场既有可见内容、有限重试 | 不继续自动命令、不写新页、不把失败当“真实退出”或“真实结束页” |

基本控制不应继续以 Draw3 ready、完整拓扑作为共同前提。当前 `session.active` 在通过窗口门后才建立，并且整个 native 业务队列先 `IsCurrentPptSession`。`IdtPlug-in.cpp:714`、`:1030`。推荐本轮先修复正常可解析窗口的控制/墨迹能力拆分；可靠窗口完全无法取得时保留明确诊断，不为“多数功能”承诺而发明无窗口UI。若未来需要真正的native COM-only交互，会涉及session建立、可见性、数字发布、按钮门与窗口销毁判据，应另列可审批范围；`MatchesPptSession`虽能比较HWND=0，但不能因此认为全链路已支持。

`PublishPageState` 目前服务于 Draw3 target/ready，不能在旧墨迹仍显示时简单把 raw 数字塞入同一发布通路。仅控制/无墨迹UI需要单独的数据语义；本次推荐不增加该通路，不能借 targetRevision=0 伪造ack，也不能将managed方法尚可调用写成用户已可使用native UI。

对象定向的guarded Exit在设计上可以对owner捕获的COM放映根、expected session/generation校验后执行，不必通过HWND选择对象；但当前实现要求非零HWND（`SlideShowSession.cs:340`）。本轮最低要求是窗口fallback成功后此处复用同一受验证解析，不再重新走反射失败；是否加入无窗口Exit能力不得冒充完整native无窗口UI。获取View后仍须重验同一场次，调用捕获的根对象，禁止换用新字段退出新场次。Next/Previous既有接口没有expected session参数，实施时还必须处理native排队到实际COM执行之间换场的竞态；不能仅凭执行前native token检查就称作严格串行安全。此点与COM所有权/owner研究共同落实。

### 6. busy 候选选择与恢复规则

推荐最小正确性方案优先于无证据地消除每一次短暂暂停：

1. 内部保存最后一次成功的纯值快照及其 binding/session/generation/当前页/总数/当前 SlideID（有时）证据；根对象不进快照。generation 或 session 变化立即作废“本次页可写”权限。
2. required 核心读取 busy：发布 Unknown/TransientBusy；同 binding+session 保留画面、关新输入；新 binding/session 清除旧目标。绝不把完整旧 stable 直接重新发布为 Valid。
3. 当前页/总数/绑定对象本轮已经验证，只是可选 SlideID/完整 topology 暂忙或缺失：允许降为**本轮 fresh 的** PageIndexFallback；不得把旧 full topology 贴到新页候选。stable→fallback 的存储轨道必须沿现有隔离；不为平滑显示合并不确定墨迹。若不在本次最小实现中细分 reader 阶段，则维持 Unknown并快速重试，也不能伪装成功。
4. 若要同页复用旧拓扑，至少要求同 binding/session、没有未消费页/文稿事件、同一 show COM identity、本轮 fresh 当前 SlideIndex+SlideID+Count 与旧值一致，并有拓扑版本/复核保证。没有提供方拓扑变更版本时仍不能证明全拓扑未重排；本轮建议不新增这种推测式优化。
5. 恢复成功必须重新发布完整 fresh envelope；native 暂停时已清旧 UI-ready，同页恢复也重新走当前 target→Present→UI ack，禁止仅解除布尔门。
6. 为 busy/unknown设置独立恢复截止，不沿用 3 秒稳定复核截止；失败后**安排约 200ms 后重试，调度等待上限 500ms**，不是立即循环 `pending=1 + Wake`。重复 busy 不忙等。owner 等待取恢复截止与慢维护截止的较小值，先处理恢复/请求，后做 ROT 等慢操作。
7. 以上是调度目标：在 Office 调用能及时返回、无外部长期阻塞时，恢复后的下一次 core probe 不应因稳定定时器额外等待 3 秒。不能承诺真实 Office 总延迟严格小于 500ms；COM、ROT、序列化、全图扫描可能阻塞，需要实际测量。若仍保留全图同步扫描，日志明确 probe耗时，不能报16ms端到端保证。
8. 连续超过既有 10 秒 busy 窗口可按现有服务机制重试/安全重绑，先失效绑定再释放，但状态来源仍是 Unknown/rebind，不捏造 `Inactive`。一次 busy 不应触发真实退出位置保存/模式切换。

### 7. 事件失败与无事件 lifecycle 的明确规则

- 事件注册是可选加速能力。用本次订阅清单记录已成功注册项；任何 required 事件订阅失败即撤销已注册项，进入轮询。不以 `bindingEvents=false` 代表没有任何连接。新 binding/service显式重置本次事件能力状态。
- 无事件约500ms读取**轻量**的当前绑定对象、showCount/目标 show、View.State/当前页和 Count；有事件也保留轻量心跳，漏事件不再额外等3秒。事件仍提供快速唤醒。完整 topology 保留现有触发条件并避免仅因 forcePolling=true 就每500ms整份扫描；如果安全性仍需每次扫描，则先正确实现，不宣称性能已改善。
- 每次成功取得 show，都与长期 show root 作 COM identity 比较。相同 identity：释放本次 acquisition；不同 identity：确认 stamp仍当前后转移新 acquisition到长期字段，推进 show session/generation，释放旧长期所有权。不能因旧 `.Active` 可读取就跳过新对象。
- 成功读取 application showCount==0 是明确无放映证据，但在多文稿提供方上它只能表明该 application没有任何 show；正数不自动证明绑定文稿正在放映，还需该文稿的 show对象。只有明确关联目标才 Observe Active。读取异常/成员缺失视 Unknown；不能以 catch 推断结束。
- showCount本身不能变成新必需能力：若该集合/Count成员确实不存在，但本轮从绑定文稿取得目标show对象、show.Presentation关联匹配且View.State/当前页等提供独立活动证据，仍可Observe Active。其失败不应阻断已经独立证实的dynamic路径；反之仅“旧show对象还能读一个属性”不是活动证据。busy未能完成这些证据时仍Unknown，不能循环尝试若干包装方式把旧对象凑成成功。
- 明确结束后先使本场不可用、废弃命令/ready，再归还本场 window所有权；保留应用/文稿 binding是否合理由当前生命周期决定。重开可用 `Begin` 事件、目标 show COM identity变化、已观测Inactive→Active或可信窗口变化创建新 session，即使 HWND为0。
- 同一 COM show对象/同一HWND的结束重开全部发生在两次轮询之间、没有事件或其他可观察generation变化时，现有信息不能识别；不得用相同页码/标题推断发生或没发生。旧确认在这种不可观察间隙的安全边界必须写入设备验收，不声称mock已证明全部情形。
- raw整数与descriptor是不同时间采样。native发现页/总数不一致先暂停，不发布新数字到旧墨迹；owner尽快取得同一snapshot并发布，不删除比较。成功descriptor可用来由owner统一推进legacy raw（需避免事件泵送/迟到结果回写）；该方式是可选最小一致性修复，仍保留native防线和旧ABI。

### 8. 可信窗口回退：只能提升已建立的证据

优先级及准入条件：

1. 优先 current bound show对象的 dynamic HWND；失败分 busy、missing/not-supported、对象失效。busy 不立刻乱试其他对象。
2. 对确实不暴露 IDispatch HWND、而既有类型标量可读的提供方，允许**逐成员、有证据**的包装例外；类型接口失败只降级窗口能力，不能使 dynamic 主要控制失败。不能把已知 PowerPoint 的反射缺失直接等同于 dynamic一定缺失，需要生产 dynamic真实证据。
3. 若没有直接show HWND，可探测当前COM application/window对象是否能够**直接取得**一个关联窗口HWND→PID作为进程锚点，而不是在系统里任意挑`POWERPNT`/`wpp` PID。这些HWND成员在旧版PowerPoint/WPP的dynamic可用性本轮未证实，不能当成所有损坏设备必有的路径。取得文档窗口时，应从绑定presentation.Windows访问，并以window.Presentation的COM identity校验关联；该集合不包含放映窗口，不能把编辑窗直接当show。任一中间COM对象均按acquisition释放。若应用/文档窗口HWND也不可读，则没有锚点，不能从标题猜出COM所属PID。
4. 枚举所有潜在放映窗口候选，**不可先按可见性删除隐藏冲突**。先用经实际支持版本验证的放映窗口类与provider/executable关系排除编辑窗/其他提供方，再校验已锚定同一进程实例。要求COM图只有一个与绑定文稿关联的show，且该实例OS只剩一个候选；COM show.Presentation需与本次绑定presentation identity匹配，枚举前后复核show对象和session未变。只有这些条件全部成立才将窗口视作受验证回退；visible不是归属证据。标题仅作辅助排除。WPS若跨进程，只有独立可验证关联才允许扩展PID，不能凭进程名前缀判断。当前仓库未找到可直接复用的放映类名白名单，不能填猜测字符串宣称兼容2010/2013。
5. 多show、多实例、同名文件、只有编辑窗、类名未知、进程锚缺失任一仍有歧义：窗口能力未知；维持COM基本控制，页级墨迹/自动文稿保存继续隔离。该失败不能反过来结束COM活动场次。
6. native首次接受窗口时记录PID、进程实例（保留可查询/可等待的进程句柄或创建时间）、窗口类/线程等必要证据；进程句柄同样必须确定性关闭。操作前验证当前session、窗口存在、owner进程实例和候选归属；失去证据停止窗口操作/可写，不能单靠 `IsWindow`。进程已退出或owner实例改变可结束旧窗口会话。PID数值相同不代表同一进程实例。
7. HWND在同一进程同线程内销毁后立即复用仍不能仅靠PID/TID/class区分；owner的COM show identity/session generation是另一道必需屏障。不要宣传Win32轮询可原子证明“从未复用”。

**无进程锚而全系统只有一个放映候选：不建议作为本轮可写/持久化回退。** 唯一只是观测结果，不证明它就是ROT绑定文稿的窗口。强制改用process-local key只能防止日后跨文稿/跨进程恢复，不能阻止当场将墨迹或控制UI放到错误放映上；同一进程多个文稿也可能在后续采样交换。若要提供弱关联的临时覆盖模式，需要用户明确批准它的行为、UI边界与禁止恢复策略，属于额外产品范围。推荐本轮没有强关联就保留诊断降级，不能偷偷用标题+唯一候选+process-local冒充安全修复。

最小产品改动应集中于现有managed读写/owner、native `PptInfo` 的控制与画布门和 `PptSession` 的局部token检查；保留7字段envelope与12字段descriptor、旧IID/方法顺序。新增窗口证据可以保存在本地native token/owner纯值旁路；没有依据则继续unknown，不为保留schema填假PID/HWND。若后来需要跨ABI传递证据，不可偷偷放宽严格schema，应单独版本/IID设计并重新审阅。

## Validation plan after approval

当前测试只能证明reader/cache/helper，并未执行上述生产owner→native准入组合。建议复用现有测试工程，增加内部可注入时钟/提供方/窗口probe，调用生产选择与准入函数；不只复制if条件写测试。

| 场景 | 有效验证及预期 |
| --- | --- |
| busy→恢复、无页事件 | 调生产刷新/调度；stable后一次busy关闭输入；恢复在retry截止到达后被读取；不得等稳定3秒deadline |
| busy时已换页、同页重排、总数变化 | raw先变/后变/混合快照；旧stable不得开新页输入，fresh PageIndexFallback也不携带旧拓扑 |
| 新binding首次busy/旧候选迟到 | 旧画布不能作为新文稿目标；generation/session屏障拒绝迟到candidate/ready/确认 |
| required读取busy vs optional topologybusy | 分别验证Unknown与显式fallback；不将同一次部分topology标Stable |
| 事件注册部分失败 | 各订阅抛错点逐一验证回滚ledger；forcePolling始终启用且能开始、换页、结束、重开 |
| 无事件结束重开旧对象仍可读Active=0/1 | 新取得不同COM identity时一定安装新show并推进session；每笔临时/长期转移release配平 |
| 同HWND新COM对象/不同HWND同应用/同名新文稿 | session与文稿target均不复用旧确认/ready/ink；明确观测Inactive→Active创建新场次 |
| 类型/事件QI失败而dynamic可用，HWND0 | 证明managed核心访问未被类型接口失败阻断；可靠窗口fallback成功后native多数功能可用；完全无法关联时报告降级，不假造无窗口UI或可写ready |
| 窗口回退 | 独立COM进程锚+单show+受验证类+唯一候选才提升；标题唯一但PID不匹配/只有编辑窗/多show/同名文件必拒绝提升 |
| HWND/PID复用 | OSprobe模拟销毁后复用、同PID不同进程创建时间；旧token失败；取PID失败为Unknown不复用未初始化旧数值 |
| COM-only→窗口可信→临时HWND读失败 | 已验证同COM show与未变进程/窗口证据可保持窗口；新证据不足不挪到别的窗口；旧画布恢复需新ack |
| legacy DLL新接口缺失 | 保留旧ABI兼容；不通过移除新接口强制绕过新隔离 |
| Exit获取View泵送Begin/End、命令排队换场 | 捕获的旧请求不调用新窗口；取消/超时不得迟到执行；窗口不可用不阻断对象定向且同场的Exit |
| 释放/退出 | 所有probe和命令在成功/每个失败点ledger平衡；不得借旧“相等对象不释放”跳过本次取得，也不得额外释放借用alias；最终由真实提供方验证后台与下份文稿 |

审批后验证范围：managed console tests + native headless parser/真实准入helper/Draw3 ready测试；涉及生产Inkeys编译按完整 `InkeysRepo.sln Debug|ARM64`、当前VS ARM64原生MSBuild、同PowerShell PATH规范化、至少5分钟等待。未获得GUI授权前不启动Office/WPP；历史版本实际生命周期、窗口类和dynamic行为标NOT VERIFIED，不能以mock替代。

## Related specs

- `.trellis/workflow.md`：规划与实施授权分离；本轮仅research。
- `.trellis/spec/ppt-interop/index.md`：当前数据流、版本证据等级。
- `.trellis/spec/ppt-interop/com-contract.md`：当前InvokeMember默认要求与用户dynamic优先冲突，后续由主会话通过update-spec修正；本研究未修改。
- `.trellis/spec/ppt-interop/native-session-ui3.md`：当前descriptor HWND共同门也需按控制/画布能力拆分修正；保留target→Present→UI ack、会话/文稿/页隔离、Unknown不等于退出。
- `.trellis/spec/native/index.md`、`.trellis/spec/native-desktop/index.md`：已读包入口；若实施触及其他模块需继续读对应细则。

## External references

均于2026-10-18核对官方文档；它们说明Win32语义，不证明PowerPoint2010或WPP2013实机行为。

- [IsWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-iswindow)：HWND可回收，检查之后也可能失效，因此一次存在检查不能构成稳定身份。
- [GetWindowThreadProcessId](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid)：只返回创建线程/进程标识；失败时out PID保持原值，调用方必须先清零并核对返回值。
- [GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)：可取得进程创建时间，用于区分同PID的不同进程实例；查询失败不应假称实例已验证。
- [Presentation.Windows](https://learn.microsoft.com/en-us/office/vba/api/powerpoint.presentation.windows)：只返回指定文稿的文档窗口集合，不含放映窗口；不能把编辑窗HWND当show。
- [Office 12 DocumentWindow.Presentation](https://learn.microsoft.com/en-us/previous-versions/office/office-12/ff760491%28v%3Doffice.12%29)：可取得窗口所属的Presentation；这是可用于核对对象图的成员，不证明HWND可dynamic访问。

## Caveats / Not Found

- 未修改或运行产品；现象归因仍需20261014a反馈设备日志。不能把静态可达机制表述为每台用户设备的已确认唯一根因。
- “直接把旧stable发布给新session即可修复”是不安全过度推断；本轮明确纠正。busy期间暂停写入本身合理，重点修复恢复调度、可选能力共同门和无事件换窗。
- 旧Win32标题回退仅证明有兼容尝试，不能证明可靠识别目标或可以原样接回新准入。
- descriptor PID来源于descriptor HWND，尚未找到独立COM→PID锚；现有native PID比较也不能消除这点。
- 未取得PowerPoint2010/WPP2013真实窗口类/成员可用性、事件行为、退出时长；版本研究由并行调查补齐，本文不填猜测类名或MSO版本。
- 旧版application/document HWND的dynamic暴露、放映窗口类和WPS跨进程关系未证实；因此本方案明确有“可靠HWND全部不可得则诊断降级”的边界，不承诺已覆盖所有类型接口损坏设备。process-local仅是存储隔离，不能填补当场窗口归属证据。
- 无事件、相同对象/窗口、无可观察过渡的完整结束重开不能由有限轮询严格检测；必须保留此边界而非将其写成测试已覆盖的保证。
