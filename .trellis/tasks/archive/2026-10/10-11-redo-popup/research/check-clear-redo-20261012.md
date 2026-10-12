# 2026-10-12 独立检查：最近 Clear 完整 Undo/Redo 链

## 范围与依据

依据本任务顶部 Clear 合同、设计和 clear-history-contract-20261012.md；用户新增需求覆盖旧 spec 中“跨 Clear Redo 不提供”的历史边界。检查范围是产品 Draw3.DrawingController.cpp 与既有 Draw3.HiddenWindowTest.cpp，主代理统一构建、执行无窗口/隐藏 Host 测试和同步 spec；检查代理不改源码、不重复构建、不启可见 GUI、不 commit/push。

## Findings (fixed)

1. 拓扑重建遗漏 Clear 后继 redo
   - 初稿仅在 clearRedoContinuation 非空时保留完整页/runtime。RedoClear 安装 B 后 continuation 被清空，但 B.history.LastRedoItem 仍存在，visible-only 重物化会丢 B 的隐藏 redo。
   - 已报告并由实现代理将保留条件补为 forward 或 LastRedoItem，独立复核。
2. PPT 直接切换的淘汰入口绕过新保护
   - 生产 SwitchPresentationCpuSlot 内原来直接调用 ShouldEvictPresentationSlot，clean durable A 可能切换时立即被淘汰；只修改晚到保存完成的局部 canEvict 无法覆盖此入口。
   - 已改为共享私有 CanEvictPresentationCpuSlot，直接切换和保存完成均拒绝淘汰有 forward/redo 的槽。隐藏回归补完整 A canonical durable 后 park 与 RedoA1 后 dirty park。
3. 可用性与真正 UndoClear 门禁不一致
   - PPT/Whiteboard 空页 previousClearUndoAvailable 默认 true，原发布逻辑不检查 ordinal/fallback，会发布 canUndo=true 而实际命令 no-op；Desktop 全局恢复点原来也未验证当前页归属。
   - 发布逻辑补 ordinal/fallback；DesktopRecoveryBelongsToPage 私有共享检查按不可变 pageGuid 用于发布、申请与完成。没有把单 canvas 文件的 pageIndex=0 误当 controller 当前页索引。
4. 迟到旧区间加载可覆盖新笔迹
   - Desktop Loaded 原来只核文件 GUID，加载期间的新笔迹可被迟到安装覆盖。PPT pending+ordinal 的候选安装合同原来没有显式验证仍在 floor；继续追踪确认现有 PresentationInputSuppressed/PresentationCanvasCommandSuppressed 已阻止 pending 时用户直接落笔，因此 PPT guard 补强不等同真实输入复现的覆盖缺陷。
   - 新 CPU Append 取消该页 interval pending；PPT active/parked 安装复核 floor。Desktop Load 使用 wasPending、Desktop workspace、页 GUID、floor 和请求时 history.Revision。拒绝只清 pending，不消费恢复点，因此可以重新请求；新 Stroke 再 Undo 到空也不会让旧 Desktop completion 生效。
5. 恢复使用快照旧 viewport
   - 原内存/磁盘 A 物化采用历史快照 viewport，会在当前视口已变化时跳回旧值。
   - 内存与 active/parked 磁盘候选安装前 SetViewport 当前目标 Canvas 的值；RedoClear 的 B 候选亦保持当前 viewport。候选成功前不改权威页面。

6. Hidden fixture durable oracle 与计数/真实文件合同不符
   - 初版 committed==accepted&&failed==0 不可能适用于 Save/Load 共用 accepted 的服务；初始 NotFound 也计 failed。已独立确认 fixture 确实启用真实 root/worker/callback，问题属于测试统计口径。
   - 新版在每次 Start 成功后的 commands 入口捕获本 run 基线，以 committed/loaded/failed/accepted/NotFound 的本 run 增量比较排空和预期缺文件；Service 诊断跨 Start 累计、Host NotFound 每 Start 清零，不能比较生命周期不同的原值。未修改 Host/Service；再读最新索引中 A 的 source/key、更新 mutationRevision、最新相对文件路径，严格导入并核 file/workspace GUID、A ordinal0/twostrokes。
   - 新 oracle 初稿曾遗漏 allowEndScreen=true 并错误要求 activeCanvases.size()==1；已报告并修正，count 共用 Bridge::PresentationDocumentPageCount，按 SlideID 验正常 A，另验证独立空 EndScreen 与不同 GUID。
   - PushCompletion 在诊断 mutex 内同时推进计数和加入完成队列；Host ConsumeBridge 先 PumpCompletions，再 PumpBridgeState/Command，完成 ACK 在 target 切换前入 Canvas FIFO，因此首次 park 覆盖 clean/durable 槽，不只 dirty warm 槽。旧 A 本序列 root0 仅一笔或 ordinal>=1，结合新 revision/最新 index/root0/twostrokes 排除伪通过。最终必须执行两个 renderer，覆盖第二次 Start 的计数寿命差异。
## 生产路径复核

- ClearRedoContinuation 是同页 InkPage 与完整 CPU runtime 值副本，不含 GPU 后像；副本清空嵌套 continuation，不形成引用环。只在副本上 Undo 到空，保留全部 StoredStroke、redo 顺序、before/after、raster token 和隐藏项。
- Undo 当前区间优先普通 Stroke；到 floor 才恢复最近 Clear。A 物化后 floor/ordinal 为 0，previousClearUndoAvailable=false，最多恢复最近一次，不进入更早 Clear。
- Undo A 不清 clearRedoAvailable。Redo 普通候选优先；栈空后才取 Clear continuation。RedoClear 先准备完整副本，安装 B 空可见态，随后正常逐笔 Redo B。
- PPT 恢复 A 为 canonical 新根；RedoClear 按当前根 ordinal+1 安装 B，不继续使用旧后继 ordinal。保存格式与 UInk 导出不变，会话 forward 独立于 durable fallback 生命周期。
- 新 StoredStroke CPU Append 成功后立即丢 redo/forward、清旧 pending，后续 history/GPU 失败不复活旧分支。
- active/parked PreviousInterval 保留原 slot generation、lane/key/binding/file/page/ordinal 验证，再准备候选并 Replace；失败前不消费 fallback/恢复点。Desktop 按自身不可变页身份与有效历史请求验证。
- 页/槽切换保留 CPU 值；拓扑对保留页 GUID 的 forward/redo 页复制完整值；GPU 热像/合成缓存沿既有换页全失效和权威重建，不把 GPU 缓存当历史真值。
- 新增迟到负例同时验证生产追加会清 pending/historyRevision 改变、返回 floor 不复活旧请求，并恢复 probe history 不影响其后既有用例。PPT pending 输入闸门仍存在，CPU probe 不等同真实 PPT 输入复现。
- 新增完整链 Hidden Host 测试覆盖 Desktop/PPT A1/A2→Clear→B1/B2、Undo 到空并停止、Redo A→Clear透明态→B、新笔迹取消旧分支、DP跨页与PPT durable/dirty park。测试检查真实命令计数、内容与canUndo/canRedo，而不是仅构造 UI 布尔状态。
- 静态 guards 与字节格式检查通过；两个源码/测试文件 BOM 与 HEAD 一致、CRLF、零 bare LF，diff 无整文件格式变化。

## Findings (not fixed) / 验证边界

- 当前源码复核未发现剩余本轮行为缺陷。新增两个生产 CanInstall 私有 helper 被真实完成/安装入口和既有 Controller probe 共用；probe 使用真实 Contact + CommitRuntimeStoredStrokeCpu，实际 Undo 到 floor，覆盖 pending/workspace/GUID/null/floor/ordinal、旧 Desktop revision 仍拒、PPT 已取消 pending 仍拒、保恢复点与新请求 retry。已独立复核，不是重复布尔表达式；已随最终 hidden 新二进制执行通过。
- 隐藏 Host/纯 CPU 测试与可见桌面/真实 Office 操作不同。未启可见 GUI，也未验证重启后的会话 forward（设计明确不新增持久化格式）。
- 拓扑保留以现有页 GUID 可匹配为依据；删去幻灯片再重新插入的完整会话隐藏历史不是本轮已有证据，不能从普通重排测试推断。

## Verification

- Lint/静态：diff --ignore-submodules=all --check、guard 和 BOM/CRLF 检查通过；项目未配置独立 lint。
- TypeCheck/编译：主代理统一执行的最终 InkeysRepo.sln Debug|ARM64 退出 0，0 warnings、0 errors（Build/clear-redo-debug-arm64-20261012-run-counters.log）；既有 inkStrokeModelerTest.sln Debug|ARM64 退出 0，74 处既有 warnings、0 errors（Build/clear-redo-native-arm64-20261012.log）。没有调整工程、依赖或工具链。
- Tests：主代理执行新二进制 InkeysHeadlessTests --no-window 退出 0，Recall/EraserAttribute failures=0、PASS animation correctness（Build/clear-redo-headless-20261012.log）；最终 --draw3-hidden-test 退出 0，Desktop/Presentation 在两种 renderer 的 ClearRedoContinuation 均 PASS，all hidden integration checks PASS（Build/clear-redo-hidden-20261012-run-counters.log 与 .stderr.log）。既有 native console tests 退出 0，ThinGPU 5184 cases failures=0，InkHistory/UInk/contact 全通过（Build/clear-redo-native-tests-20261012.log）。旧 binary baseline 只作基准，不作为本轮通过证据。

- 结论：本轮需求范围内无剩余阻塞项。最终第二 renderer 的通过同时验证 Service 跨 Start 计数与 Host 每 Start NotFound 计数寿命不同的 fixture 修正。任务保持 in_progress，未 commit/archive；检查代理未重复构建或启动可见 GUI。
- 环境：最终实际构建/测试由主代理在沙箱外用同一配置执行；PDB/IPC、TEMP 写入限制和既有 ILK 增量链接异常属于已定位的本机/Codex 环境问题，详见 verification-clear-redo-20261012.md，不计为本次源码缺陷。
