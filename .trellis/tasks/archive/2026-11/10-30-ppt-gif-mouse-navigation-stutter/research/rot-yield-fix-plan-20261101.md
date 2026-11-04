# 2026-11-01 依据细分实测执行的最小 ROT 让出修复

## 新数据与决策

新采样Build/Validation/ppt-navigation-20261101-122321-464-4c57406c，PID54148，正常退出0、版本/内嵌DLL匹配55EA18DF...，4816事件/2374span/15请求、0issues。最慢Previous epoch13总1610.020ms，邮箱1500.262ms：ROT剩余718.024ms +完整Observe782.218ms，owner109.064ms。Observe分散为show-count186.832、Slides总数187.371、当前页187.118、View62.064、State93.903、HWND16.134ms；原有释放也计时，未见账本异常。不是某个单一getter独占853ms，不能武断删某项安全校验。

用户已批准准确定位并执行修复。现在实施较小阶段B：已有健康binding时，排队导航到达则在ROT安全检查点取消本次非必要扫描，彻底释放所有本轮临时引用，再回原完整Observe与有界邮箱流程。保留全部Observe、安全代次、busy/兼容/墨迹门禁；不同时重写观察与扫描。不承诺消除Office单次RPC和完整Observe本身的耗时。

## 代码合同

- 生产scanner内部提供明确Completed/Deferred状态（内部形式由实现选择），不能把partial best或partial priority当完整结果。公共COM ABI/schema不变，现有GetAnyActivePowerPoint测试/调用入口尽量兼容；必要的内部测试注入只限scanner系统边界，生产普通属性继续真实dynamic，不引入反射/typed wrapper。
- 仅允许现有app/presentation/window root非空、已确认Active场次时因pending取消维护；未绑定、Unknown、恢复/断连路径保留原完整发现。
- pending查询只取受原gate保护的纯队列状态，不在queue锁内调COM/日志，不递归执行请求，不抢占已在COM中的调用。
- 检查点在原COM调用返回后/安全循环边界，提前退出必须先走原逐项finally（window/presentation/candidate/moniker/bindctx/comObject），再外层列表/枚举器/ROT清理。不缓存RCW跨轮、不借事件参数。不使用GC、FinalRelease或循环Release。
- Deferred时取消best选择别名，使原列表清理不再跳过partial winner；禁止把列表项和best当两笔取得重复释放。每笔取得仍单次释放或完整扫描成功时明确转移。
- 保守公平预算：健康binding最多连续让出一次ROT扫描；下一次到期的扫描强制完整尝试，之后可再次让出。保持原500ms维护调度，不另设任意秒数/硬实时承诺，已进入COM耗时仍不可控。无binding/Unknown不消费让出预算；完整尝试/cleanup如何重置需明确并测试。
- 返回owner后仍按原ObserveBoundShow→ProcessRequests顺序，不能直接使用cached stamp替代现场校验；不改变2秒超时、队列上限/有界排水、focus、等待UI或raw/descriptor/Present/ack准入。
- 新计时fixed事件rot-deferred/rot-forced等只用纯值，分析器保留旧v1兼容，数据不泄露文稿名/路径；日志失败不能破坏释放/IErrorInfo。

## 文件所有权

managed实施：PptCOM/PptCOM.cs、SlideShowSession.cs与必要既有managed测试；必要测试seam须保持生产路径原动态访问与真实finally，避免另建通用COM框架。其它privatehelper仅在最小化需要时添加，先告知。
独立检查：新增ROT提前退出所有权、partial winner、同identity重复取得、busy/异常和公平性；原freshObserve与墨迹准入不能退化。
主会话：任务/spec/产物/构建/日志分析与用户交接。不覆盖此前诊断改动，不自动commit/push，GUI仍由用户执行。

## 验收

- pending在扫描前、中、完整结束后、同identity重复/partial winner已选择后到达，Deferred不转移任何候选；每个提前退出/异常分支的账本配平，根对象保留。
- 未绑定/Unknown不让出；最多一次延期后完整尝试，不因持续请求饿死发现。旧请求在原完整Observe识别Count0/换窗/换文稿后零Invoke。
- 原dynamic-only、16细分开关等价、所有权/会话/busy/超时/停止测试继续通过；新失败/取消测试不调用GC，也不结束用户Office。
- managed Release|AnyCPU构建/测试与TLB字节比对、DLL交接、完整Debug|ARM64 solution、内嵌DLL/PrepareOnly通过，独立COM资源审查，BOM/EOL/minimaldiff。
- 用同一人工入口让用户3↔4重点Previous，对比ROT后续工作是否及时终止、队列改善，保留完整Observe耗时未解决的边界。不把“加defer”直接标为全部长尾已解决。
