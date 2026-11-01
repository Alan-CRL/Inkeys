# Research: 最小 ROT 让出修复的所有权与安全边界审计

- Query: 取消健康绑定的本轮ROT扫描，能否在不返回partial winner、不吞掉让出、不泄漏或双重Release的情况下减少导航排队，并保持原完整Observe与有界公平性？
- Scope: internal；只读产品/测试/规范，向实施代理发送设计反馈；未写产品代码或执行git/构建/GUI。
- Date: 2026-11-01

## Findings

### 1. 计划可成立，但必须保留的前提

新真实采样的Previous总1610.020ms，邮箱1500.262ms由ROT剩余718.024ms和完整Observe782.218ms覆盖。当前修复先取消后续非必要扫描，不同时拆Observe，范围较保守。它不能抢占已经进入Office的调用，也不能消除完整Observe本身的782ms；不能把减少ROT累加宣称为彻底消除长尾。

`PptCOM/PptCOM.cs:1357-1393`（当前读取快照）顶部仅在showObservationPending非零时Observe；慢维护为MaintainBinding/ROT→完整Observe→ProcessSlideShowExitRequests。必须继续这条慢维护fresh校验顺序。`SlideShowSession.cs:157-168` 的Matches/IsCurrent只读缓存生命周期、generation/binding，不能拿它替代尚未观测的Office现场变化。

### 2. 提前退出的四个关键位置

- `PptCOM.cs:897` 的while条件先执行enumMoniker.Next，它已经取得本轮moniker。**首次post-Next让出检查必须在本轮try/finally内部**；若放在try之前，新的Deferred返回会漏释放moniker。也不能在外层清理上一轮已释放却仍留在数组槽内的moniker。
- `:930-931` 的candidateApp=comObject / comObject=null是一笔取得的所有权转移配对；检查点只能在两句完成后，否则finally可能把同一笔取得按两个变量释放。
- `:969-970` 附近foundAppObjects.Add(candidateApp) / keepAlive=true是列表转移配对；中间退出会造成局部candidate和列表两处各释放一次。
- `:1059-1061` 的bestApp只是列表项选择别名；Deferred必须在进入外层finally前清空bestApp并将输出priority归零。否则`:1101-1103`原列表清理会跳过partial winner，既未返回给caller又未Release。

已把以上检查点约束直接反馈实施代理与主会话。行号来自本轮实施前快照；实际代码以语句和方法名为准。

### 3. 不采用会被catch吞掉的哨兵异常

scanner的Application、ActivePresentation、SlideShowWindow、Active、priority和per-moniker都有catch{} / catch(Exception)（例如`:939`、`:990`、`:1001`、`:1020`、`:1037`、`:1041`、`:1067`），异常式Deferred如果未逐层重新抛出，会被当普通接口错误吞掉并继续扫描。

当前实施代理选择显式return Deferred，设计正确：从任何受保护的try内return都会先执行原finally，不需改所有catch，不把正常让出记为COM失败。return前仍须清掉winner别名及priority；不能依赖方法尾部正常结束赋值。若用break，应注意dedup foreach里的break只退出foreach，不能误当结束scanner。

### 4. 取得账本与原dynamic必须保持

- `IRunningObjectTable.GetObject`直接返回Application时，candidate接收该笔取得并清空comObject；文稿moniker则comObject拥有ROT取得，Application是另一次属性返回，两者分别清理。
- candidate即便与已有列表项或长期target root同COM identity，每次返回仍是一笔新的取得。duplicate分支keepAlive=false必须释放本次candidate；已保活的列表取得只在外层释放/完整扫描成功时转移。
- activePres与ssWindow始终是当前候选检查的临时取得，按window→presentation先清；其后candidate与comObject/moniker/bindctx及最终列表/enum/ROT的原清理不能跳过。
- 生产ActivePresentation/SlideShowWindow若为了既有测试边界改走presentationAccessor.GetProperty，是可接受的局部受控分派：`PresentationDescriptor.cs`生产switch内部依旧是真正C# dynamic；不允许换成InvokeMember、PIA类型包装或为了fake使用反射。
- 不允许保留scanner RCW到下一轮，不在scanner持临时图时递归执行命令；Deferred只是纯值结果，callback参数也不能作为长期root。

### 5. 健康资格、pending与公平性

健康条件中的root非空最好用(object)root比较，避免新增dynamic运算绑定；Active/binding缓存只用于决定发现维护能否延后，不能授权基础控制或InkReady。Unknown、未绑定、恢复分支不让出；返回owner后仍完整Observe。

pending应与任务定义的导航相符：仅队列中的Next/Previous，最好排除已取消项；不要无意把ExitAnnotation/View/Activate都提升为同一策略。查询只在原gate取纯队列状态，不调COM或写日志，不持多个新组件锁。

最多一次Deferred后，下一次到期强制完整尝试；只有完整尝试（正常或原有失败返回）或FullCleanup重置策略，不能因“本次pending暂时消失”提前抹掉强制义务。保持原500ms维护期限及进入时的有界排水，不增加忙循环或任意秒数承诺。

### 6. 最小测试 seam 与应覆盖的实际生产路径

当前ServerHarness/FakeAccessor只接受FakeNode，直接dynamic candidate.ActivePresentation在FakeNode上不可用；可以复用现有生产accessor switch让fake进入相同scanner路径，而不是另造通用COM框架。

仅给系统ROT取得增加内部factory、给系统接口临时释放增加窄观察/替代delegate足够；生产默认仍调用原GetRunningObjectTable HRESULT+out及原ReleaseMarshalObject。**必须保留非零HRESULT但out非空的清理能力**，不能用只返回对象的Func丢掉失败时仍映射的取得。实际CreateBindCtx返回的RCW即使在fake扫描中也要真正Release。

Fake managed ROT/moniker并非真实RCW。测试可以证明当前生产scanner的logical acquisition/cleanup/转移次数、返回状态和公平性，但不能由fake零账本推断真实PowerPoint过程退出已验。fake GetObject和同identity重复都应登记新receipt；Root的原取得与本轮候选取得分开计。若注入release故障，应best-effort继续其余资源，不能把“Release发生外部错误”写成已成功释放，也不能不确定是否已Release后再补一次。

推荐回归矩阵：

1. pending在scanner前、Next返回moniker后、GetObject后、Application属性返回后、加入列表后、ActivePres/window/Active后及best已选后到达；Deferred所有候选零转移，winner/priority清空。
2. 同identity重复、未进入列表的局部candidate、partial winner→下一候选、每个普通异常/缺成员/busy点，逻辑账本恰好一次且长期root仍保留。
3. GetROT非零HRESULT+out非空、enum为空、CreateBindCtx与DisplayName/GetObject异常，原finally全部经过。
4. 健康/Unknown/未绑定、无pending、已取消pending、持续pending：最多一次defer后全扫描尝试，500ms原deadline与2秒wait不变。
5. 回owner后的原Observe识别Count0/换窗/换文稿，旧请求零Invoke；无事件/missing Count/COM重入保守路径不退化。
6. 新诊断writer失败不阻断清理/IErrorInfo；不调用GC/FinalRelease或结束用户Office。

## Files Found

- `PptCOM/PptCOM.cs`：ROT候选扫描、原逐项/列表finally、MaintainBinding、完整Observe及owner循环。
- `PptCOM/SlideShowSession.cs`：pure-value场次缓存、受gate保护邮箱、取消/停止与有界排水。
- `PptCOM/PresentationDescriptor.cs`：实际dynamic accessor与deterministic临时对象释放。
- `PptCOM.Tests/ManagedCompatibilityTests.cs`：ServerHarness、FakeGraph/Accessor账本、owner实际路径替身。
- `PptCOM.Tests/Program.cs`：DynamicOnlyNode与生产动态分派回归。
- `PptCOM.Tests/DispatchFixtureTests.cs`：原生IDispatch/连接点fixture，可证明更接近真实CLR边界；本轮未构建或执行。
- `research/rot-yield-fix-plan-20261101.md`：已批准最小修复合同。

## Related Specs

- `.trellis/spec/ppt-interop/com-contract.md`：每笔acquisition、borrowed root、dynamic、typed HWND例外和禁止GC/FinalRelease补漏。
- `.trellis/spec/ppt-interop/native-session-ui3.md`：pending先观察、slow维护、旧请求及raw→descriptor→Present→ack门禁。
- `.trellis/workflow.md`：研究与实现/质量检查分工及授权边界。

## External References

无外部查询。本文件是当前仓库代码/规范与测量计划的内部审计，不扩展Office/WPS支持矩阵，也不把fake行为当实际Office设备证据。

## Caveats / Not Found

- 本次读取时ROT产品修复尚未落盘；上文是实施前设计审计，不是新代码最终质量通过结论。新代码仍需独立实现后审查、编译与测试。
- 未运行任何GUI、Office、COM fixture、构建/测试或git操作；只写本研究文件。
- 个别legacy Console.WriteLine仍可抛出，普通扫描异常返回partial best为原有行为。新Deferred必须不经过其不受控输出路径，并确保新输出故障不影响finally；不建议顺手重构全仓日志。
- 减少ROT排队不等于取消进行中的COM或已定位/解决完整Observe的所有等待；数据与收益仍须用户新实测。
