# Research: PPT 翻页后的 controller / persistence 卡住审计

- Query: UI仍可操作但PPT翻页后画布不能书写；核查 target、command boundary、active contact、加载和呈现门禁，独立复核 raw/descriptor 缓存竞争。
- Scope: internal，静态生产代码审计；未运行Office、GUI、构建或新增故障注入。
- Date: 2026-10-17

## Findings

### 1. 独立复核：raw旧页/descriptor新页导致缓存目标不收敛（高优先级）

这是另一个研究方向提出后本代理独立核查的真实控制流问题，不是F2磁盘故障。以下每个分支均已直接检查生产源码。

假设已在A页可写：当前场次、binding、文稿、HWND保持不变，cachedTarget=A，raw A，Host document-ready/UI-ready=A。

1. `Inkeys/IdtPlug-in.cpp:576` 本tick先读取raw A；随后发生翻页，managed更新到B；`:608–617` getter返回descriptor B并提前推进observedStateRevision，descriptorChanged=true。
2. `:663–668` 比较raw A和descriptor B，trustedPage=false。`:694` 普通页进入块被跳过，因此`:742–748` 不把cachedTarget更新到B。
3. `:800–809` sameBinding时暂停旧target A；`Draw3.Host.cpp:2193–2206` 暂停会清除uiReadyPresentationTarget。
4. 下一tick raw=B，managed revision未变，getter返回空。`PptCOM/SlideShowSession.cs:186–197` 同内容不递增revision，GetSince相同revision返回空；因此普通heartbeat不会补发B。
5. descriptor仍为缓存B，trustedPage=true；但descriptorChanged=false且cachedTarget A非空，`:743` 跳过重新解析。`:763–773` 仅核对session/binding以及Slide/EndScreen种类，没有核对cachedTarget.pageIndex/SlideID是否与descriptor一致；trustedTarget变true。
6. `:827–857` 重新发布A并解除暂停，expectedPptUiTarget仍为A。`Draw3.Bridge.cpp:75–78` 同target去重；`Draw3.Host.cpp:1293–1304` 同targetRevision不重投切页。解除暂停并不恢复已经被清除的UI-ready。
7. A仍为document-ready，于是`:878–882` 将raw B配上A的targetRevision发布到UI。`:134–150` AcknowledgePptPageUi用expected A的pageIndex校验B页码，回执被拒。
8. `Draw3.Host.cpp:551–565` requirePresentationUiReady时持续关闭admission；UI线程与绘制线程无需死锁就能表现为笔/橡皮不工作。

关键补充：稳定停在B时，这个状态能持续；继续产生不同descriptor/state revision、退出重入、新session或重新建立target可打断条件，不能宣称即便再翻页也永远无法恢复。白板往返等状态变化也不能不测试就断言一定不能恢复。真实产品是否必然每次跨tick交错仍须复现；源码已足以证明该特定交错不会被普通同内容heartbeat自动纠正。该路径完全不依赖诊断开关、磁盘错误、持笔或物理触摸硬件。

最小生产回归建议：对真实PptInfo状态消费提供受控raw/getter采样点，A已ready -> tick rawA/newDescriptorB -> tick rawB/emptyResponse -> 多次rawB/emptyResponse；观测实际Bridge target必须最终B、UI实际page=B+identityB、ack被接受且新Down准入。使用A/B不同SlideID、同session/binding/HWND，且记录暂停确实清UI-ready。测试必须经过真实消费分支和ack检查，不能只复制几行布尔公式。再覆盖初次cachedTarget为空（不会中招）、legacy每tickdescriptorChanged=true（不会同样持续）、正常新B一致快照、下一次C变化恢复。修复方向应让尚未应用的descriptor在raw匹配后重新形成target，继续严格身份与页码核验，不能简单取消ack/安全门。

### 2. Controller正常翻页不会无限等待旧手指抬起

- `Draw3.DrawingController.cpp:10298–10349` AdmissionRevision变化时sealPresentationContacts将已接收笔迹合成终态，停止重连、惯性及Laser；物理route随后隔离。
- `:10352–10416` control wake先调用Host消费状态，再seal；commandBoundaryPending阻止越过FIFO边界接受新contact。
- `:12366` processCanvasCommands虽等active.empty，但`:14623`合成收尾后调用DiscardUntilTerminal回收runtime，不等待真实Up才能切页。
- `Draw3.ContactInput.cpp:867–925` Quarantined路由与admission不同：旧route隔离至真实终态，但ContactAdmitted只检查当前revision和handle。未发现“存在一个旧quarantined contact便全局禁止后续新Down”的直接门禁。池容量、RTS同ID生命周期仍属独立调查，不能据此宣称终态永不丢失。

因此“按着手指翻页 => active永远不空”不能仅由while(active.empty())反推成立。

### 3. 新的明确无界等待：PPT持久化root命名mutex

- `Draw3.PresentationAutoSave.cpp:137–159` NamedMutexGuard::Acquire，按规范化autoSaveRoot/presentation路径UTF-16 FNV-1a哈希命名 `Local\\Inkeys_Draw3_Presentation_Index_%016llx`，`:148`用WaitForSingleObject(INFINITE)。
- Save`:1157–1165`和Load`:1481–1487`都获取同一root锁，覆盖后续索引/文件工作；不是按文稿单独锁。
- 单一worker`:1723–1789`串行工作。若外部活owner长期持锁，本worker不会产生IoError，也不会完成本次Load。
- `Draw3.DrawingController.cpp:10380–10385` 在loadPending或unresolved时丢弃新contact；`:12730–12752`需要加载完成且initialized后才能stage ready。
- F2重试`:844–917`由明确IoError触发；尚未返回的wait不会进入重试。`:1924`CloseAndDrain无期限join，关程序也可能等待。

这是源码明确的无期限机制；实际用户是否运行同root第二实例/进程或遇到长存储操作未知。死掉的owner通常导致WAIT_ABANDONED，而代码视为成功；不能把“上次崩溃遗留mutex”当永久锁的解释。通常同文稿warm翻页不会等待保存完成，所以纯保存worker卡住并不自动等于画布卡住。必须发生冷槽/上一Clear区间加载，或加载排在已阻塞保存后，才形成上述pending输入闸门。

确定性最小实验：隔离root，辅助进程创建并持有对应命名mutex；真实PresentationAutoSaveService提交合法Current load；证实超过正常重试总间隔仍无completion，再释放mutex，确认同请求返回/绘制恢复。另做worker先阻塞Save、随后Load的队头阻塞场景。不得持有用户实际存储root锁；持锁进程必须保证finally释放和超时清理。本代理未执行此实验，不能声称已实测。

### 4. 既有加载保护与错误路径，不能重复算新缺陷

- `Draw3.DrawingController.cpp:788–792` unresolved = Presentation && !initialized && mutationRevision==0。
- `:810–925` IoError四次退避250/500/1000/2000ms用尽后manualOnly；解除外部占用不会凭heartbeat自动恢复。同目标由Bridge去重，真实翻页/重新目标能改变这一条件。这是已修F2有意保留的保护范围，不是本轮发现的F2回归。
- `:716–743` completion按文稿lane、bindingmode、slotGeneration和可复用拓扑路由，允许同slot晚到加载对最新页物化；`:12581–12635`采用latestTarget安装及stage。普通A->B快速翻页期间，旧load targetRevision不同并不意味着回执必被丢弃。
- `:12669–12752` initialized warm同文稿页切换不会重新读取每页文件，不能把所有普通翻页冻结解释为冷槽读盘。
- `:12694–12720` stage先reset，之后CPU切换、拓扑重建、page_count失败均continue不发布ready；资源分配失败/异常拓扑可能留下安全停写，但本轮没有找到正常有效普通翻页必然触发这些失败的证据。
- `Draw3.PresentationAutoSave.cpp:1699–1718,1791–1796` completion分配或payload复制抛异常会丢失回执，没有待处理请求超时恢复。这是低内存条件下的真实缺口，当前没有用户低内存证据，优先级低于第1条自然采样竞争。

## Files found

- `Inkeys/IdtPlug-in.cpp`：raw和descriptor消费、缓存目标、UI身份回执。
- `PptCOM/SlideShowSession.cs`：managed revision去重与GetSince缓存协议。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.cpp`：目标去重及稳定targetRevision。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp`：目标/文档/UI ready输入门禁及完成队列投递。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`：边界收尾、文稿槽、加载路由/重试和成功Present回执。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp`：admission与隔离route语义。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp`：串行worker、root锁与加载/保存生命周期。

## Related specs / External references

Read `.trellis/workflow.md`, `.trellis/spec/native-desktop/input-and-ink.md`, `.trellis/spec/ppt-interop/index.md`, `.trellis/spec/ppt-interop/native-session-ui3.md`。主要合同：未抬起旧contact不跨页且不无限等待Up；document Present + 同identity页码提交后才开放输入；相同revision getter不扫描Office；晚到completion不能激活旧目标。此次仅以本地实现为依据，无外部引用或版本支持推断。

## Caveats / Not Found

- research.jsonl不存在；按角色隔离未读取implement.jsonl/check.jsonl；已读父任务PRD/design/implement叙述。
- 未修改产品、测试或配置，未执行git操作，未构建/启动GUI；此报告是静态审计和独立交叉核查，不是完整Office复现。
- 用户不知道PowerPoint/WPS、是否持笔、卡住后PPT能否继续翻页。不能从反馈补齐这些条件。
- 已确认自然采样竞争的完整逻辑链；仍应由主会话用真实生产消费路径取得可重复实验后将其升级为运行期已复现根因。

## 独立实验复核补充（2026-10-17）

复核对象：`research/ppt-turn-original-probe.py`、`Build/CanvasFreezeInvestigation/ppt-turn-20261017/events/{probe.cpp,source-manifest.json,run.log,results.json}`。本代理仅只读复核并执行哈希/字符串校验，没有重跑build、probe或GUI。

- 独立Python检查通过：manifest列出的10个文件当前字节SHA256全部匹配；用AST读取生成器的excerpt标记、直接从当前源文件重新切片，5段摘录的SHA、起始行及在生成C++中的逐字包含关系全部一致。trustedPage、resolve条件、trustedTarget、完整AcknowledgePptPageUi和Host blocked谓词确为当前源码，未被替换成测试专用版本。
- 初值合理且有限定：稳定两页文稿，A已经document-ready/UI-ready，session/binding一致，后续descriptor改B但首次raw仍A。初值不要求输入设备或存储错误。probe省略HWND有效性/原循环包围条件，因此它假设有效且未变化的放映窗口；需由实际产品复现验证这个前提。
- 结论不是stub硬编码：真实ResolvePresentationTarget和StateBridge参与目标生成及去重；真实ack函数的页码谓词在stub前拒绝错页。stub的PublishProductPresentationUiReady反而无条件接受通过ack的输入，比完整Host更宽松；即便这样错误交错仍无UI-ready。模拟Present始终成功，说明该实验的闭门不需要GPU失败。暂停清UI-ready与实际Host2193–2206一致，但采用重述代码而非完整Host执行。
- 3个对照有效：首包raw/descriptor已一致可达B；raw先B、随后descriptorB可恢复；错误交错后强制出现新descriptorChanged可恢复。它们支持问题在“已收到但暂未应用的新descriptor丢失重算机会”，不支持所有翻页都卡。多个Probe共用UI-ready等全局变量，但均串行，构造时重置UI-ready/currentSession，tick发布时重置expected；末尾恢复对照也重新赋expected与ready，未发现全局串扰能制造所报告的主失败或使该恢复结论失效。
- 输出显示buildExit=0/runExit=0，错误交错10,000次稳定tick保持cachedTarget=1/revision=1/uiReady=0/blocked=1；正常及恢复对照cachedTarget=2/revision=2/uiReady=1/blocked=0。10,000次是同步逻辑迭代次数，不能转称真实运行数分钟、调度概率或Office完整复现。
- 本实验未执行完整PptInfo循环、真实COM getter及managed owner、PageControl异步表面提交、真正Host/contact管线或GPU。descriptorChanged参数由实验安排；它在产品上的可达性仍来自本报告第1节的原码链路（raw先读、getter后读、revision提前消费、同内容不递增），不是probe运行本身证明真实跨线程时序。
- `verified-final/results.json`确实记录同一SHA `4188e2eb39cca05f40ac68e1730cc85f343c1bfe122c1ee78c920a2dca075c7f` 的parked-retained、pending-topology、current-load三入口exit0且有对应PASS。这些通过支持已有持久化/拓扑回归未失效，不覆盖本次PptInfo采样竞争；不能用它们把摘录实验提升为完整生产循环验证。

独立评价：可称“当前生产原码关键分支摘录 + 真实Bridge/目标解析的确定性交错实验已复现持续输入门禁错误，静态完整链路经独立核查”；不能称“真实Office/WPS翻页现场已复现”或“完整产品端到端已验证”。
