# Research: PPT turn event and target publication audit

- Query: PPT 翻页后 UI 存活而画布不响应；审计 COM 事件到 native/Draw3 target 与输入准入。
- Scope: internal; read-only product audit plus isolated CLI original-source excerpt experiment; no GUI or solution build.
- Date: 2026-10-17
- Baseline: parent reports F1–F4 committed as 68cd297d; no git operation by researcher.

## Findings

### P1: 一个具体的跨快照交错可遗失新页 target，并永久等待不可能匹配的 UI ack

证据级别：源码可完整推导的确定交错，后续真实 Bridge/Presentation + 原码摘录 CLI 实验证实（见追加验证）；尚不是完整产品或用户现场归因。独立于 F2 文件 IoError，无需文件失败、持笔、控制台或窗口 owner 异常。

前提：真实产品 requirePresentationUiReady=true（Draw3.Product.cpp:28）；同一放映、同一 binding、同一拓扑，A 页已 ready，cachedTarget=A；切到 B 后不再发生不同 descriptor 内容更新。

1. `Inkeys/IdtPlug-in.cpp:576–577` 先抓取 rawPage/rawTotal；本轮取到 A。
2. 此后 managed 回调/owner 发布 B：`PptCOM/PptCOM.cs:829–851` 回调更新 raw、PageChanged、请求刷新；`RefreshPresentationDescriptorIfNeeded` 读图并发布不可变缓存（269–317）。native 的局部 raw 仍为 A。
3. `IdtPlug-in.cpp:608–620` getter 取得 B，新缓存和 observedStateRevision 已提交，descriptorChanged=true。
4. `:663–667` raw A / descriptor B 不匹配，trustedPage=false；`:743–748` 因 trustedPage=false 跳过 ResolvePresentationTarget，旧 cachedTarget=A 保留。
5. `:797–806` 暂停 A 输入；`Draw3.Host.cpp:2193–2205` 暂停同时清除 uiReadyPresentationTarget。
6. 下一轮 raw 已为 B；getter 使用刚才已消费的 revision 返回空，descriptorChanged=false。raw 与 descriptor B 匹配，但 cachedTarget=A 非空，`:743–748` 仍不重建 target。
7. `:766–776` 只验证 cachedTarget 的 session/binding 及 pageKind，并未比较页/SlideID/完整目标与当前 descriptor。因此旧 A 被当作 trustedTarget 再发。
8. `Draw3.Bridge.cpp:75–78` 对相同 A 去重返回原 targetRevision；`Draw3.Host.cpp:1293–1304` 不为该原 revision 重发切页命令。
9. `IdtPlug-in.cpp:854–859` 将 expectedPptUiTarget 设成 A 并取消 suspension。A 文档仍 ready，`:879–884` 却将 buffer 设置成 raw B，`:908–913` 发布 B 页码 + A revision。
10. `AcknowledgePptPageUi`（`:132–149`）检查 expected target 的 pageIndex，拒绝 B 页码为 A target 作确认。`Draw3.Host.cpp:551–564` 因 UI ready 缺失持续 AdmissionBlocked；新 Down 被关闭，而 UI 线程可以继续运行。

持续条件：`SlideShowSession.cs:192–205` descriptor/session/pageStatus 内容指纹相同不会增加 stateRevision；`:210–212` 同 revision getter 返回空。即使 managed 每三秒重新读取 B，只要内容未变也不恢复 descriptorChanged。native 的 16ms 轮询也不会补建 cachedTarget。因此在静止的 B 页上可以无限保持该逻辑状态，而不是普通一帧延迟。再次切到另一页且快照一致、重新进入放映等可能解除；不能描述为任何操作都无法恢复。

这条机制没有任何线程必须死锁；它表现为状态机关闭输入且永远等不到匹配回执。Draw3 旧页内容可能停留，页码可能已更新到 B，工具 UI 仍可操作。具体视觉仍需产品验证。

### 建议完整生产路径验证（尚未执行；已完成下述摘录组件实验）

- 在 PptInfo 单轮状态消费边界建立测试缝，保留实际 target 构造、trusted 判断、ack 校验、Host gate，不只重写条件的 toy model。
- 预置真实产品 Host requirePresentationUiReady=true；A target、成功 Present、A UI ack 已完成。
- 在 raw=A 读取后设置屏障，令测试 session getter 返回 B 的新 envelope；下一次及之后 getter为空且 raw=B。
- 验证首轮暂停并清 UI-ready；第二轮应发布 B target 并获得 B 文档 ready，实际代码预计仍发 A；调用真实 ack 函数应观察 B/A revision 不匹配拒绝。
- 不添加额外下一页/控制唤醒来掩盖；多次相同 B refresh/空 getter 后，记录 bridge target pageIndex/SlideID、raw/descriptor、expected UI target、documentReady/UIReady、AdmissionBlocked。新 Down 的接受/拒绝是最终判据。
- 配套：raw 先更新/descriptor 后更新、总页数变化、A→B→C 快翻、同页 Unknown→Valid、EndScreen→普通页、whiteboard 覆盖恢复，确保修复不放松一致性/身份安全。
- 最小修复方向供主会话讨论：不要将“本轮收到新 envelope”当成“缓存 target 已匹配当前可信 descriptor”；缓存解析结果必须在可信条件恢复时补齐，且 trustedTarget 不能仅验证 kind/session/binding。用户本轮仅调查，本代理未修复。

### 其他审计结果及边界

- managed getter 只获取缓存；未发现其持锁进入 Office 的直接死锁闭环（PptCOM.cs:196–219；SlideShowSession.cs:210）。COM 枚举在 owner Refresh 中，慢/挂起仍可能令状态不再推进，但此轮无 Office 栈或长调用证据。
- 事件漏失有三秒 raw 轮询及 owner descriptor 刷新路线（PptCOM.cs:1483,1585–1593）；不能单凭事件存在就宣称不会卡。P1 的故障恰好发生于缓存 revision 已消费后，周期刷新相同内容不足以补救。
- managed Console.WriteLine 在事件/服务中仍同步（PptCOM.cs:831,852 等）；仅在开启 PptCOM 诊断且输出背压时有风险，不适用于未开启诊断的现场推断；本轮没有将其当新确认根因。
- Begin/End 与候选发布使用 generation/session 屏障（SlideShowSession.cs:39–62,130–175）；本轮没有证明 rapid begin/end 可以越过屏障。
- Host/Bridge 相同目标去重是正常幂等设计；此处问题在 native 把过期缓存认成当前可信目标，不能通过取消所有去重或绕过 UI ack 来修。

## Files found

- `PptCOM/PptCOM.cs`: PowerPoint/WPS acquisition, event callbacks, raw page writes, owner descriptor refresh.
- `PptCOM/SlideShowSession.cs`: immutable envelope revision/fingerprint, lifecycle generation and owner mailbox.
- `Inkeys/IdtPlug-in.cpp`: raw/envelope reconciliation, target cache, UI buffer and ack validation.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.cpp`: target identity deduplication/revision publication.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp`: target commands and admission gate.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp`: product requires UI-ready acknowledgements.
- `InkeysHeadlessTests/ppt_session_tests.cpp`: existing envelope/parser tests, not the full PptInfo reconciliation loop.

## Related specs

- `.trellis/spec/ppt-interop/index.md`
- `.trellis/spec/ppt-interop/native-session-ui3.md`: raw/descriptor identity, same-page suspend/re-ack, target→Present→UI ack admission.
- `.trellis/workflow.md`; parent prd/design/implement read. research.jsonl not found; role-isolated implement/check manifests not loaded.

## External references

No external facts required; this is source-control-flow analysis. Office/provider version unknown. No external documentation used.

## Caveats / Not Found

- User facts only: after PPT turn canvas freezes; UI remains responsive. PowerPoint vs WPS, further PPT navigation and held input unknown.
- No full production-loop/Host reproducer executed and no GUI/solution build/product edits. Isolated source-excerpt CLI experiment below confirms the logic with explicit stubs; do not claim a full-product reproduction.
- Existing F2 may still be relevant for some incidents, but does not cover the P1 cache/admission chain. Deferred 11 owner/Z-order failures are outside this audit.

## Executed isolated original-source experiment (2026-10-17)

- Generator: `research/ppt-turn-original-probe.py`; source snippets are extracted automatically from the current product source, not hand-translated predicates. Each excerpt records original first/last lines and SHA256. Real `Draw3.Bridge.cpp` and `Draw3.Presentation.cpp` are compiled unchanged, using real target types and resolver/deduplication.
- Output: `Build/CanvasFreezeInvestigation/ppt-turn-20261017/events/`. `probe.cpp`, `source-manifest.json`, `build.log`, `run.log`, `results.json` preserve generated code, all relevant source hashes, actual compiler path, EXE hash and exit codes.
- Compile: dynamically discovered Visual Studio native ARM64 CL, C++20 /MT /Od, existing matching JSONCPP provider. Not MSBuild and not a solution rebuild. Final invocation used `C:/Users/alan-/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`.
- Build exit 0; run exit 0. Bad order first tick reports `raw=1 descriptor=2 cachedTarget=1 revision=1 trusted=0 uiReady=0 blocked=1`; subsequent 10000 ticks report `raw=2 descriptor=2 cachedTarget=1 revision=1 trusted=1 uiReady=0 blocked=1`.
- Control 1: aligned B raw/envelope on first observation publishes page 2 revision 2, ack accepted, blocked=0.
- Control 2: raw B arrives before descriptor B: first tick pauses, next new B descriptor resolves page 2, blocked=0.
- Control 3: after the stuck sequence, a new descriptor notification forces rebuilding B and opens the gate. This proves the harness can recover and is not configured to reject every ack.
- Important boundary: the COM envelope delivery/descriptorChanged flag is supplied by the harness; actual COM serialization/scheduling is not executed. Successful renderer Present is simulated for the accepted target. Host suspension and publish-ready endpoints are stubbed to their relevant state effects; the original admission expression and original AcknowledgePptPageUi predicate are executed. No real contact/Down or GPU is exercised. Thus this is production components + automatically extracted original-code evidence, not full PptInfo/Host or Office reproduction.
- Ten thousand deterministic iterations demonstrate persistence under unchanged state, not elapsed-time soak or proof of every possible recovery action. The separate managed-source fingerprint audit explains why ordinary equal-content refresh need not produce a new notification.
- Initial runner WindowsApps python alias was inaccessible inside restricted execution. An authorized isolated invocation ran it; command quoting of VsDevCmd was corrected in the probe runner. Final rerun with provided bundled Python succeeded without escalation. These were probe execution/environment issues, not product failures.
