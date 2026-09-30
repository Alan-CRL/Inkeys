# PPT 真 Office 诊断与跨模块交接独立静态复审

审查快照：2026-09-28 22:52:10 UTC，`HEAD=8b156fca59f0337a6afc6d722941666fcf143080`。主要文件 Git blob：`Inkeys/IdtPlug-in.cpp=814791f95e93a1a2527dff833b4e11048495d9e5`；交接路径快照 `Draw3.Host.cpp=63f6518d3aa1f419fb0835e954038df8579ea72f`、`Draw3.DrawingController.cpp=83a0e4e7b08a544c980c36fedc7dd7c33697b720`、`Draw3.ContactInput.cpp=2346d87e6cc4f94d9782d3187932410c140d4b8a`、`PageControl.cpp=e57b1f28345966c5cec9f3dd36844693402d2225`、`Window.cpp=f0107639bd39740eae521a094759e4f99b50358e`、`IdtMain.cpp=fb5b1bf222d44e1a63668a73e099a8803fcdb4e6`。Controller/ContactInput 此时由另一实施者修改，后两者和跨模块状态**不是最终冻结审查**。仅写此报告，不改产品/共享文档，不运行构建、GUI 或性能采样。

## Findings (fixed)

- 无；本轮是只读复审。

## Findings (not fixed)

### P2／初轮已确认、后续已修：`PptGate` 两个字段名与实际值不一致

- `Inkeys/IdtPlug-in.cpp:795-804` 的 `[PptGate] ... session={} target={}` 分别传入 `session.active` 和 `static_cast<bool>(trustedTarget)`，所以记录的是 **sessionActive/trustedTarget**，不是 `session.localSession` 或 target revision。真实日志在 `TestResults/release-hardening/ppt-office-own-7b879a9b762a413eac7489c409d9d883/log/idt1790634520330.log:21-33` 出现 `session=true target=true`，而同场后续 `publish_accepted revision=1/2/3`。多场放映时，即使真实 localSession 递增，这条 gate 日志仍会写 `session=true`，按字段名拼接 `[PptSync]` 或 `publish_accepted session=<localSession>` 会串场；`target=true` 同样不能拿来匹配 revision。
- 建议最小修正为显式 `sessionActive` 与 `trustedTarget`，若要关联再另记 `localSession/targetRevision`。不改变信任门或发布顺序；修后至少用第二场放映或静态格式断言复核。本 reviewer 按只读分工不改源码；主 agent 的修正及静态结果见文末增量节。

### P2／条件性：诊断阶段名是观察点，不能当完成时刻或性能分位数

- `IdtPlug-in.cpp:582-592` 的 `firstObservationQpc` 在 native 线程**看到 raw 页/总页变化**时更新，不是 Office 内部动作时间；若 descriptor 单独变化而 raw 未变，可沿用旧 QPC。`descriptorAcceptanceQpc` 在 `:749-759` 解析出可信 target 后取样；`target_published` 仅在 `PublishProductPresentationTarget` 返回非空且 revision 改变时记一次（`:828-856`），不代表 Draw3 文档、GPU 或 UI 已 ready。
- `document_ready/page_ui_ready`（`:860-879`）读的是一次 `ProductRuntimeSnapshot()`，先比完整 `ReadyIdentityFor(*cachedTarget)`，再由该线程首次**观察到**匹配状态时打印；日志墙钟时间含最多一轮 native 观测等待和日志调度。更靠近原事件的 `Draw3.Host.cpp:479-501` 在成功呈现后发布 `canvas_presented` 身份、`:1639-1652` 在实际 UI ready 回执时打印 `page_ui_ack`；`PageControl.cpp:1697-1715` 只有可见 surface 的 `pageCommit.Commit` 成功才回调，`IdtPlug-in.cpp:134-149` 又核 session/revision/page/EndScreen 身份。上述 stage 应按同一 `(localSession,targetRevision)` 关联；`presents=N` 是 Host 全局累计 successfulPresentCount，不是该页单独 Present N 次。
- 真 Office 单次日志的 accepted→documentReady→pageUiReady 差值约 47/4 ms、30/13 ms、31/9 ms（SlideID 256/257、EndScreen）只是**该线程/日志的观察间隔**，不含笔输入、光学呈现或重复样本。启用时 `Draw3.PptTiming.h:9-31` 同时写 stderr/OutputDebugString，`IdtPlug-in.cpp:847-877` 走 logger；诊断本身可能影响 16 ms 观测节奏。不能把三组数值称为完整端到端延迟或 median/P95/P99。

## 调用链与默认开销/隐私核对

- `IdtPlug-in.cpp:782-805` 的新增 `tracePptGate` 静态只在首次进入 PptInfo 轮询时读取一次 `INKEYS_PPT_TIMING`；默认未设时只多一处布尔分支，不新增 COM、文件、窗口或日志。开关为 `1` 时，gate 行只在 raw/descriptor 改变记一次；`publish_rejected` 也只在这种变化时输出，不能把**缺少**该行解释为没有 heartbeat 期拒绝。`publish_accepted`、ready 行有 revision 去重。已有 `TracePptTiming` 在每个调用翻译单元各自缓存同一环境门；运行中改变环境可能出现 gate 与 QPC trace 不一致，建议在进程启动前固定开关。
- 新 `PptGate` 输出 lifecycle/pageStatus、raw/descriptor 页数、`slideIds.size`、Office owner/descriptor PID、数值 session/target 状态；`publish_accepted` 输出 pageKind/pageIndex/SlideID。它**不输出**文稿路径、标题、墨迹、完整 SlideID 列表或 token。开启诊断会持久记录 Office PID 与单页 SlideID 等活动元数据，应只在隔离验收/用户明确开启时使用。`pageKind=EndScreen` 时 `slideId=0` 来自 `value_or(0)`（`:848-851`），是无 SlideID 的展示哨兵，不能读作 Office SlideID 0。
- `trustedPage` 在 `:669-673` 要求 native raw 页与 descriptor 一致；同次日志 `:25-26` 对 raw 2/2 先有旧 descriptor 1/2 导致 `target=false`，下一次 descriptor 2/2 才 `target=true`，这是预期的迟到同步而非 `PublishProductPresentationTarget` 拒绝。`GetWindowThreadProcessId` 只在显式诊断且 raw/descriptor 变化时读取 show HWND owner PID（`:788-804`），不改变身份判断。

## UI3 / Draw3 / 重启交互的阻断项清单

| 交接 | 静态已证 | 仍阻断最终整链 PASS 的范围 |
| --- | --- | --- |
| Office→Draw3 target | `IdtPlug-in.cpp:669-780,828-860` 核 lifecycle、页号、show HWND/PID、service/session/binding，target 返回 revision 后记录 accepted；H0 Bridge `PublishPresentationTarget` 对同目标幂等，`nextTargetRevision_` 在 Reset 不归零（`Draw3.Bridge.cpp:66-96,160-169`），现有诊断按 revision 去重不会因正常 Host Reset 重用旧号。 | 本机自建 PowerPoint 16.0 一场实际页1/2/EndScreen 已在 app log 见 SlideID256/257 与 revision1/2/3；WPS、第二场、COM busy、旧 HWND 复用及跨进程恢复仍未实测。|
| Draw3 ready→UI3 ack→输入 | `Draw3.Host.cpp:256-278,479-501,1639-1652` 以完整 identity 和成功 Present/UI ack 控输入；`PageControl.cpp:1697-1715` 仅在可见 surface commit 成功后回调，`IdtPlug-in.cpp:134-149` 再核会话/页。 | 真 Office 日志证身份/ready，**没有用户墨迹输入**，不能证明旧 contact 截断、新页可写、PPT UInk 落盘、EndScreen 墨迹或四 surface 在失败/重试时视觉正确。Controller/ContactInput 当时仍在改，需冻结后重审与相关 GUI/无窗回归。 |
| UI3 工具、窗口与退出 | `IdtMain.cpp:256-278` 首次正式退出启动监督并置 Window gate；`Window.cpp:211-215` 的 BeginShutdown 是单调原子，旧显示请求有 owner 门；崩溃 mode1 唯一 helper 和 mode0 确认见同任务崩溃报告。 | UI3 Bar/Setting/PPT 主栏可能继续响应时，Draw3 线程已卡住但 `ProductRunning()` 仍 true 的现场路径未有真 GUI 根因；普通 ContactInput Closing 等待、RTS/COM Shutdown、双 HWND capture/隐藏与 15 秒杀旧进程后的实际数据终态均不能从 PPT ready 日志推断。自动重启与新进程可见恢复分开验收。 |
| 发布兼容 | 本轮诊断不改 DComp/ULW/FLIP 或 Office COM ABI/工程资源。 | Win7 SP1仅KB2670838 的硬件FL11.0有/无→WARP、ULW透明/输入/resize/device-lost，真 Office/WPS版本/位数与最终 Release 各架构需按最新源码复验；无真机则列人工。 |

## Verification

- 独立读取当前 `IdtPlug-in.cpp` diff、PptTiming helper、Bridge/Host/PageControl/Window/IdtMain 实际路径、`ppt-office-com-apartment` 研究及唯一自建 Office root 的应用日志。`git diff --check H0 -- Inkeys/IdtPlug-in.cpp` exit0。未运行构建、Office、GUI、性能采样或测试；本机 Office 自动验收的构建/退出码取自任务记录，不冒充本 reviewer 复跑。
- 初轮结论：新增 opt-in 诊断没有发现默认热路径负担、文稿内容泄露或业务状态 race；两处字段误标会降低多场次日志可信度。当前实测只支持「本机 ARM64 Debug 隔离 PowerPoint 会话的目标与 ready 身份」；UI3/Draw3 输入、PPT 保存、重启及 Win7 全链保持上述门禁。开头快照是历史审查点，字段修正后的静态结论见下节。

## 2026-09-28 23:05 UTC 增量复核：诊断字段修正

- 主 agent 独占修正后的 `Inkeys/IdtPlug-in.cpp` Git blob 为 `0b457b9795700d6dfa9de439154237f6131f2294`。仅复核本次两字段改动及其上下文；Controller/ContactInput 仍在另一实施者手中，不把本节升级为 H0→HF 全量终审。
- `IdtPlug-in.cpp:795-804` 的格式串现为 `sessionActive={}`、`sessionId={}`、`trustedTarget={}`。逐项计数 17 个 `{}`，后续实参也为 17 个：前 10 个 lifecycle/page/descriptor/PID 数值不变，第 11/12/13 个分别为 `session.active`、`session.localSession`、`static_cast<bool>(trustedTarget)`，第 14–17 个为 trustedPage、trustedEndScreen、whiteboard、ProductRunning。格式/类型和顺序对齐；多场次 gate 行现在能用数值 sessionId 与 `publish_accepted session`、`[PptSync] session` 关联，布尔 target 不再冒充 revision。若要匹配 target，仍须取 accepted/ready 行的 revision，不可用 `trustedTarget=true` 代替。
- 新字段只多一个数值 `localSession`，不记录文稿路径、标题、墨迹、binding token 或 SlideID 列表；原 `INKEYS_PPT_TIMING=1` 静态门和 `(descriptorChanged || rawChanged)` 限频未变，默认未开时没有新日志/COM/窗口查询。`git diff --check H0 -- Inkeys/IdtPlug-in.cpp` exit0；文件当前为纯 CRLF 行尾、无额外 bare LF。**初轮两处 P2 误标从源码上关闭。**
- 现有真 Office `ppt-office-own-7b879a9b762a413eac7489c409d9d883` 日志出自旧格式二进制，只能证明旧问题和本机目标/ready 链；尚无新二进制的第二场放映或实际输出。17 项静态对齐不能替代新源码构建、运行或最终 HF 指纹。时间观察、真笔迹/PPT durable、重启、Win7 和未冻结输入层门禁仍按上节保留。
