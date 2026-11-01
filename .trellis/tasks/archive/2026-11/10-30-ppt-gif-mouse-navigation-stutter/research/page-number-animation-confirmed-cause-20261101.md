# Research: 页码跳变的现场定因与恢复/准备投影边界

- Query: 163049样本已确认短暂Recovering导致数字内容动画；该Recovering来自何处，最小修复应改哪里？
- Scope: internal；只读产品/诊断日志，不改产品或测试、不构建/GUI/提交，仅写本研究。
- Date: 2026-11-01

## Findings

### 1. 现场已证明的直接原因

采样`Build/Validation/ppt-navigation-20261101-163049-968-6df25be7`为正确1EB8...版本、exit0；PptWait 912条零掉样。独立解析19个content-policy：7次Numeric→Status、7次Status→Numeric全部policy3/setter3；另5次Numeric→Numeric为policy2/setter3，说明正常数字即时策略仍在。

右下surface1在真实3→4 target2附近QPC1196246954464进入recovery_kind1/Status，1196247662733返回Numeric，相差70.8269ms；当前页字段4、总数始终56。退出时两槽scale997/pct922，再次调用两个文字setter。启动page3就有同类3→Status→3，主数字和总数均可完全未变而发生动画。

`Ppt.Recovery.h:10–12`的kind1为Recovering；`PageControl.cpp::BuildPageContent`将任何非None recovery归Status；`Bar.Scene.cpp::UpdateContent`对任一端Status调用TransitionToString。`Bar.UI.cpp:821–839` active幂等仅按旧transition target判断，旧目标还是恢复文字时回数字会重新Start。`Bar.UI.cpp:896–930`使scale向0.8/pct向0，然后恢复；这是本次“缩小或淡出后重现”的直接现场原因，不是普通Numeric即时分支被删除。

### 2. 正常新目标也会发布Recovering，不等同出现错误

`Draw3.DrawingController.cpp:13912–13924`处理SetPresentationTarget/SetWorkspace时，若目标identity变化或explicitRecoveryRetry，会：

1. 用新command建立recoveryRequest；
2. sceneClosedAdmission=true；
3. 归零scenePreparationFailures/presentationFailures；
4. 无条件发布CanvasRecoveryStatus::Recovering、CanvasRecoveryReason::None、默认attempts0/retryAvailable=false。

这在实际加载/切场执行前发生，正常新页、首次进入、同场新目标也需要真实Present来结算，不能将Recovering一概理解为IO错误。

`DrawingController.cpp:9589–9592`在当前Presentation加载尚未完成、未进入manual-only时，也会发布Recovering/None（未安排重试）或Retrying/CurrentLoadIoError（已安排重试）。真正scene准备失败用:9598的Retrying/ScenePreparation并有attempt；真正Present失败用:16734的Retrying/PresentFailure，达到4次改ManualRetryRequired。冲突/拒绝/终端错误另有ProtectedConflict/Failed等。

正常加载重放后，`DrawingController.cpp:9613–9622`只在实际Present后的workspace-ready结算时发布None；`:16741–16751`也要求presentSucceeded、无未决加载/viewport恢复且实际场景身份匹配才清None。这解释正常快速准备为何能短暂进入/退出Recovering。

### 3. Host安全态、UI内容态被错误地混为一种显示含义

`Draw3.Host.cpp:794–804`保留reportedRecovery，经RefreshPresentationInputGateLocked投影并分配Host revision；`Draw3.Recovery.h:39–53`保留同desired匹配报告，fatal强制Failed；`:57–63`的CanvasRecoveryBlocksAdmission对所有非None recovery（含owner旧报告）阻断输入。

`UI/Ppt/Ppt.Recovery.h:35–64`只过滤目标/会话，不区分正常进展和故障来源；所有Retrying或Recovering都映射UI Kind::Recovering，且状态检查先于reason，所以IO/Present重试也会归这类。随后`IdtPlug-in.cpp:1433–1445`对任何UI recovery非None强制targetRevision=0/PageStateMode.Pending，并把显示页码绑定期望目标；这仍是安全防线。

`UI/Ppt/Ppt.cpp::PublishSnapshot`只在recovery.kind为None时投影pagePreparing；`PageControl.cpp::BuildPageContent`则在recovery非None时改Status/恢复文字。于是正常准备的Recovering排斥了generic三点，并对主/总页槽播放状态文字进出动画。准备态与故障态的显示分类不够细，是比纯文字setter更靠近根源的边界。

### 4. 两种修复路线及推荐

**优先评估：分开控制恢复状态与显示准备状态。** 正常新目标/首次Present的进展应保留Numeric页码与三点，保留真实错误、冲突、需重试文字和原Retry动作。可以在UI内容分类另有“准备进展”纯值，而不把内部recovery清为None；PptInfo仍按真实恢复态强制Pending/target0，Host的RecoveryBlocksAdmission及真实Present/UI ACK全部不动。

不能仅让PptRecovery.Project(Recovering,None)直接返回None来止动画：这会一并移除IdtPlug-in的安全覆盖。尤其同目标显式retry可能仍有同identity旧ready，`Host::RetryCanvasRecovery:2752–2772`只排新命令、依当前恢复态保持关闸，没有把每一项旧ready当场清零；不能因此提前显示InkReady/接旧ACK。

**来源判别存在一项重要限制：** `DrawingController.cpp:13912–13924`对explicitRecoveryRetry也重置成Recovering/None/attempts0，现有CanvasRecoveryState不保留explicitRetry来源。因此仅靠当前status/reason无法严格区分“正常新目标”与“用户对真实故障显式重试后的首帧进展”。若要求真实重试进度文字完全保持，需要保存最小来源/故障谱系；不能把所有Recovering/None默认当正常。

若产品允许所有Recovering/Retrying进度统一三点（失败/冲突/ReadFailed/PresentFailed/ManualRetryRequired文字和Retry动作仍保留），可以用单独显示分类避免来源问题；但这是明确的显示策略选择，不应在本轮只读调查中擅定。

**较窄备选：仅取消尚未显现Status→Numeric的返回动画。** 可减少本次短70ms状态抖动造成的数字缩淡，但它不阻止正常准备进入Status/恢复文字，也需要“已显现”的可靠软件判据，不能只按content.kind或未来target判断。它治疗短状态抖动，不如根源分类匹配用户三点偏好；不能全局禁状态动画或Arrow/Add语义动画。

### 5. 已知与未知字段

已知：现场recovery_kind1、旧新内容kind、policy3、两个setter重启、数字/总数、scale/pct以及同target时间关联。已证源码正常准备会发布Recovering/None。

未知：163049现有content-policy没有记录底层CanvasRecoveryStatus、reason、attempts、explicitRetry来源；不能把该次kind1直接等同于底层一定Recovering/None，也不能据无IO错误专用行完全排除Retrying。无需再做大人工矩阵；先利用现有可达代码和当前确认事实设计严格显示分类，并以生产无窗口夹具覆盖正常新目标、首次Present、实际重试及失败/冲突的控制/视觉分离。

## Files Found

- `Draw3.DrawingController.cpp`：正常恢复债务、IO/Present失败与成功Present结算。
- `Draw3.Host.cpp`、`Draw3.Recovery.h`：保留恢复报告、revision与输入阻断。
- `UI/Ppt/Ppt.Recovery.h`：底层status到UI recovery映射。
- `IdtPlug-in.cpp`：恢复态强制Pending/target0。
- `UI/Ppt/Ppt.cpp`、`UI/PageControl/PageControl.cpp`：恢复与三点显示互斥及Status内容。
- `UI/Bar/Bar.Scene.cpp`、`Bar.UI.cpp`：内容类型策略、文字缩淡时间线。
- `163049.../stderr.log`：19次实际content-policy记录。

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md:445`：普通数字即时、状态文字可打断动画。
- `.trellis/spec/ppt-interop/native-session-ui3.md`：Pending/InkReady/ACK与generic准备三点。
- 归档`10-30-page-status-transition-interaction`：数字/状态原设计。

## External References

无外部版本/SDK事实；仅本仓库与本地实测。

## Caveats / Not Found

本轮未实现修复、未运行构建/GUI。保留当前三点偏好，不恢复普通准备文字、不删故障门、不碰COM。底层reason及retry谱系未被当前现场包直接记录；不要把映射kind1扩写成已证具体错误原因。
