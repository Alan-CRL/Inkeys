# S5 P3/P4 全范围独立复审

2026-10-29。本轮延续 P1/P2 检查，覆盖新增直移、lease、原生提交/取消、工作区异步政策和进展唤醒。P1/P2 的冻结版本已经通过完整构建和实际测试，详见 `s5-p1p2-validation.md`；这些结果不自动覆盖本轮新代码。

## 覆盖的完整链路

- Window：单次 CAS role/pair lease；UI ULW 与原生 bounds/回滚互斥；像素版本；固定 candidate mailbox；稀疏 promote/refresh；原有命令恢复写集；异步 Prepare/Complete 的 payload、phase、native truth 与通知顺序。
- PageControl：发布绝对 pair 目标、最新候选合并、完整 pair mask 后才更新成功几何、最后 Up 的续接、单窗 ULW 的旧帧门禁、旧 hit/damage 与当前像素身份。
- Bar：成功 HWND/generation/pixel revision 基线，原 translation/held/release 表面与 deferred barrier，真实完成后重基准、命中和光源缓存；测试 sink 不代替产品异步路径。
- Whiteboard：Freeze lease、真实背景像素版本、prepared/final active/inactive 回执、内容/设备/窗口失效以及协调进展通知。
- StateMonitoring / Host / Product：沿原状态机推进 Prepare→ready→Complete→Committed；取消后 Executing 责任；源补偿；前台上下文；停止态通知；250ms 无进展采样及既有四次失败/退避。
- UI 同类入口：SyncDraw3State 发布业务与 native desired，实际原生 reconciliation 留在现有协调线程；Freeze 按钮/快捷键使用显式异步刷新入口。

复核了任务 reading-map、`ui3-window-call-audit.md`、rendering-and-ui 中底栏/直移/成功帧合同，以及 canvas/whiteboard/PPT 的身份、失败补偿和焦点限制。P3/P4 的新增规范必须与最终源码保持一致，不将本报告作为真实 Office/WPS/DWM 或设备验收。

## 发现与当前状态

| 项目 | 证据与影响 | 当前处置 |
| --- | --- | --- |
| 原生已提交、邮箱回执尚未发布时取消 | completion 已 Committed 后清 mailbox current，会把真实已移动结果降为 Cancelled/mask 0，且不会再回滚；Bar 因而丢失实际 translation | 已直接核对 TryCommit 的原子裁决、Committed/CancelRequested 状态和退休读取接口；Bar 在回执不可读时保留责任，role lease 保留至回执/回滚完成。生产邮箱红测失败输出在工具会话中观察到，未独立落盘原始红测日志/退出码；最终二进制验证见 s5-final-verification.md |
| 主栏中央形态屏障未统一退休 queued move | `FinishBottomDockTransition(true)` 的 workspace/display 调用没有输入路径的手动 Cancel；只在 Request 入队前检查 serial 不能阻止稍后 owner 搬旧位图 | 已核对中央屏障在偶数稳定发布前请求保留回执的取消，以及 Request 发布后 serial/session/deferred 复查；fixture 走真实 Begin/Finish，覆盖 queued 和 completed-unread |
| Pointer Down promote 在队列锁争用时丢失 | Submit(wait=false) 可拒收，而 Down 调用方丢弃 false；普通 Show 已不补 promote，旧兄弟顺序可能永久保留 | 已核对独立固定 NativeEventMailbox；promote 全角色共用最新意图，root refresh 使用另一槽组，均绑定 HWND/generation/原1500ms期限。owner 复制后走原 tracked 恢复，失败以原期限内250ms重试；发布不再争抢普通命令队列锁 |
| 单窗成功帧的像素版本复验 | pair/Freeze 已有 role lease 与像素版本，单窗 receipt 消费尚需核验捕获像素版本和当前 lease，避免同 RECT 的新像素被旧回执确认 | 已核对单窗回执持 role lease、精确匹配 captured / actual / current presentationRevision 后才提交 Scene |
| 前台 fallback 证据 | Complete 入队时若前台为白板，等待 owner 期间用户切到外部窗口，入队前的 allowFallback 不能证明是隐藏导致的切换 | 主会话确认这是原同步排队路径已有的边界；本次保留捕获一次/重基准一次和原 gate，不新增焦点传输协议。明确列为真实 Office/前台切换验收限制，不据 fixture 声称通过 |

P1/P2 已修复的回执 known/current-native 复验、恢复掩码防忙转、安全 latch 幂等和静态 pending 新反馈推进继续纳入本轮回归。首个 native 错误与 rollback 错误现已分别保存在 CommandCompletion，补足上一轮诊断建议。

最终源码复查：上述四项实现问题的修正已逐项查看，没有剩余阻断完整构建和动态回归的源码发现。保持“源码条件通过”，当前 P3/P4 二进制尚须完成实际门禁；不据 P1/P2 的旧哈希或 helper 探针宣布新版本全部通过。

## 验证边界

- 本复审不与主会话并行构建或性能采样；主会话负责最终完整 `InkeysRepo.sln Debug|ARM64`、当前哈希的 hidden/offscreen/state/headless 和性能复验。
- 已查看新增生产 helper / 原生 fixture 断言；确定性模型与真实 owner/ULW 测试必须分别记录，不能由 helper 测试推出完整 StateMonitoring/Office 实机通过。
- 已检查新增真实异步政策 API 接线 fixture：使用保持屏外、画布隐藏的 Desktop policy 调用 RequestPrepare/Complete，核 payload、phase、Current、observer 时序、同代几何失效、queued cancellation 和真实 Executing 后取消退休。执行结果仍由主会话记录；只构造 WorkspaceWindowReceipt 的旧测试不能替代这条接线。
- 本轮仅写复审记录，产品改动由文件负责人处理，避免覆盖共享工作树。
- 最终静态 `git diff --check --ignore-submodules` 退出 0。本复审未并行运行另一套构建或性能负载。
