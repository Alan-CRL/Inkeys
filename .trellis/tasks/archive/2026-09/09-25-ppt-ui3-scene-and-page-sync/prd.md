# PPT UI3、切页同步与主栏放映场景修复

## Goal and approval
修复 PPT 控件位置/保存、有效缩放、可信页同步、主栏场景、仅主栏退出确认和焦点。2026-09-25 用户已在独立后续消息明确 PLEASE IMPLEMENT THIS PLAN，批准完整方案及最小 PptCOM 补充，规划审阅门已通过。
基线 bugfix/pptui / 94e07b2599adab9de4286aa5fb7526e2c1c681f6，本地与远端 dev 相同；初始工作区干净，无本会话活动任务。十个其他任务不动。

## Requirements and acceptance
- R1：记忆开关不限制本场拖动。区分运行偏好、候选/成功提交、保存请求和磁盘成功值；版本化成对交接，旧快照/设置刷新/旧完成不能回写新位置。取消/丢捕获/窗口失败不永久锁定；临时 DPI/屏幕适配不保存。
- R2（用户补充）：拖动不写盘。开启和关闭记忆瞬间都保存当前位置；真正结束放映且记忆开启时保存；关闭后不更新保存位置直到重新开启，重置除外。每次进入读保存位置、缺失用默认。重置无论开关均更新运行/保存默认值。正常软件退出视为会话结束；白板覆盖、结束页、无效状态不算退出。
- R3：旧 Settings JSON 不得回写旧保存位置；真实文件成功才标记保存。失败保留旧文件/当前位置/同一重试快照，不采集关闭记忆后的临时位置。
- R4：布局、Scene、文字、命中、阴影、呈现使用同一有效缩放；覆盖乘积>4、屏幕适配<.5、资源上限、白板分页。保持左右两对联动。
- R5：不预测 Next 页码；保留文稿/binding/session/target/SlideID/index 保护；区分文档切换、成功呈现、页码提交、输入开放。安全合并未接受目标，不丢笔迹/保存事务。
- R6：切页收尾已接受持久笔迹到旧页，隔离仍按下 contact 至物理终态；停止跨页重连/惯性，取消瞬态 Laser；不无限等 Up。目标画布和可见页码提交后接受新输入。
- R7：Esc重入、文稿切换/同名、重排/重绑定不串页；保留正确的 parked/warm 旧 target 恢复后比较 SlideID 顺序，不将普通翻页当结构变化。
- R8：进入展开低栏=>居中低栏，主体下沿在放映屏幕底边上5 DIP；展开普通悬浮保持。收起普通保持位置/收起；收起低栏无跳变解除吸附。退出展开=>居中低栏/正常工作区高度；收起保持。场景幂等、白板优先，主栏与四控件互不排斥。
- R9（最新决定覆盖原请求）：仅主栏 EndShow 点击确认。PageControl结束图标/滚轮/长按/原生键盘/Office退出不新增确认，保留已有非点击行为。默认取消，关闭/失败不退出，不提前切模式。single-flight和会话校验防止旧确认退出新会话。
- R10：不重新启用 Draw2；保留非PPT快捷键、颜色编辑、控件系统repeat参数。有效进入/明确业务按钮后一次性交还当前PPT焦点；尊重设置/颜色/确认框，绘制/拖动/轮询/渲染不抢焦点。
- R11：可关闭分段计时，native观察不是COM事件时间；只报告真实基线/修改后数据与条件，未实测 Office/WPS/硬件标 NOT VERIFIED。

## Constraints and verification
用户批准最小托管补充：事件唤醒现有 descriptor owner、缓存会话状态和预期会话退出；保留旧 COM GUID/顺序/descriptor、页码读取、ROT、Application绑定和Office/WPS兼容。Draw3单Host/producer/独立设备不变。
保持编码/换行/minimal diff/关键中文注释。完整Solution Debug|ARM64，原生ARM64 MSBuild，规范化PATH，>=5分钟。允许headless/hidden-window/offscreen，不启动交互式GUI。不commit/push；journal/archive用--no-commit；未满足验收不提前归档。
验收使用生产交接测试、实际Scene/offscreen、Draw3 hidden、托管状态/释放测试、i18n check、diff check；人工矩阵保留为未验证。

## Supplemental requirements (true exit and writable EndScreen)

The 2026-09-25 follow-up extends the active task, preserving R1–R11 and the user's later decisions on deferred position saves and main-bar-only confirmation. Baseline: bugfix/pptui 9d432cd6c68bc7e1c04933702e3dc85ef4e6e669, clean at investigation start. This follow-up authorizes investigation and implementation; no new Office GUI control, Draw2 path, Host or input producer.

- R12: On **authoritative real show end**, except whiteboard ownership, restore Desktop Selection across stateMode, bridge/window selection, desired/runtime workspace, input admission, presenter target, and actual visible/hit-testable HWNDs. Cover native Esc, Office end, confirmed main exit, direct PageControl end, destroyed show HWND. Cancellation/Unknown/temporary concealment/B/W/end black page must not trigger it.
- R13: The exit transition is idempotent and versioned. A stale exit callback/visibility command cannot overwrite a newer pen selection, whiteboard or new show. Settled selection lets input pass to underlying desktop; desktop ink remains visible through the appropriate transparent output, and an empty desktop removes unnecessary interception. Explicit new pen mode remains writable after old contact isolation. Failed Present/window command must leave a safe, retryable state rather than a fullscreen interceptor.
- R14: PowerPoint's actual terminal black page (`View.State==5`) is its document's distinct logical writable page, initially blank. B/W temporary screen, transient COM failure, Unknown and Inactive never create that page. Its strokes, eraser/shape, history and Clear are separate from the last normal SlideID. Return/reentry restores each page; native click and selection retain PPT's non-inking behavior. No opaque black Inkeys backing and no added Office slide.
- R15: EndScreen has its own valid target/ready/UI-commit/input authorization, internal unsigned storage identity/pageGuid and durable UInk roundtrip, with stable document identity independent of show revision. Public page display remains `- / N`; COM `totalPage=N`, normal slideIds and slide count remain real PPT values. End-at-start, single-slide, reordered/changed topology, old file compatibility, save failure and stale completion are covered without fake SlideID or UINT_MAX.
- R16: Measure and record actual Window Service role visibility, owner, enabled/exStyle/hit/capture and Host/bridge/output revisions on true exit with a default-off bounded diagnostic. Run production-path integration for both presenter classes and UInk encode/decode/import/Host cold restore; no-window/hidden/offscreen builds as appropriate. Visible physical click-through against a lower real window is NOT VERIFIED unless an explicitly allowed real desktop test is actually performed. Previous build/test logs are background, not this follow-up's result.

## Regression supplement: selection page visibility and paired controls

Baseline for this follow-up: `bugfix/pptui` at `2aeca374ea4c863eafe851ea41b52ca21018dd14`; worktree was clean at turn start. Earlier Office testing is the user's observation, while the source-only causal analysis and this turn's tests must be recorded separately. Preserve R1–R16, the typed EndScreen, UInk and real-exit hit-through contracts.

- R17: When the current page changes, Host publishes the complete `(hasContent, contentRevision)` payload. A different page with the same boolean still advances revision and wakes consumers; an unchanged payload is idempotent. In Selection, A/B/EndScreen ink is presented through the auxiliary transparent ULW and the main Drawpad stays hidden. Empty pages hide after the existing clean-frame barrier. Pen mode remains Pen across page changes, Selection remains Selection; ready/admission/identity checks stay intact.
- R18: Enabled PageControl pairs eventually converge to two onscreen, rendered, hittable surfaces at large scale, even after first resource creation, old scene/backing state, entry animation or a recoverable one-sided failure. Pair drag and four-PPT-control avoidance stay; Bar never becomes an obstacle. Runtime fit may reduce the effective scale but never writes the user preference. Failure and recovery must be observable per surface without treating a valid offscreen entrance point as a final-layout error.
- R19: Add a pre-fix failing production regression for same-bool/new-revision, then a Selection sequence A→B→A→empty→B→EndScreen→A with ready/present/content and auxiliary-output checks. Add PageControl multi-DPI, large-scale, pair/failure/animation tests through actual Scene/Window Service where supported. Keep prior held-contact, save/cold-read, UI ack and desktop hit-through tests. Real Office/WPS, physical input and desktop system hit-testing remain NOT VERIFIED unless actually run.
