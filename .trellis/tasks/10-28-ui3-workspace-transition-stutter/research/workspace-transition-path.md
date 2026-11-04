# Research: 三态切换、主栏状态与线程路径

- Query: 桌面 / PPT / 白板六方向切换如何触发 UI3 主栏动画，是否存在同步阻塞、重复发布和分阶段延迟；修复应保留哪些交接凭据。
- Scope: internal；仅源码、现有规范与任务记录，未构建、未运行基准或 GUI。
- Date: 2026-10-28
- Evidence: 当前源码直接确认的是调用/等待/失效结构；没有本轮故障采样，不能宣称某候选已被测为最大耗时根因。

## Findings

### 1. 文件与所有权

| 文件 | 职责 / 证据入口 |
| --- | --- |
| `Inkeys/IdtState.cpp` | `WorkspaceHandoff`（154）、短锁快照（263）、白板请求（1361）、工具迁移（1543）、统一协调线程（3326）、提交（3580）。 |
| `Inkeys/IdtPlug-in.cpp` | COM / 放映事实（1068 等）、目标发布（1170–1207）、可见性心跳（1268–1273），不在共享 UI 渲染回调内。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | 白板按钮只发布请求（582–589）；状态统一投影（931–1059），公共内容切换与去重。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` | `UpdateRendering`（286）、PPT 后台事实（400）、统一已提交工作区通知（500）。 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 已提交帧快照（1197）、主栏表现/布局锁（1676–2786）、布局批次（2544–2586）、工作区停靠转换（13257–13310）。 |
| `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h` | 三态到 Enter / Switch / Leave 派生（64–75），Desktop 基线只在首次进入捕获。 |
| `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp` | 真实全屏 Freeze 呈现（129–273）、背景/分页发布（347–405）、prepared / active 校验（410–430）。 |
| `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.BackgroundState.h` | 新 revision 清 prepared（43–53）、相同 active/revision 幂等、inactive ACK（123–133）。 |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp` | 固定串行顺序（54–61）和实际回调（804–837），全部目标共享批次时间。 |
| `Inkeys/Inkeys/Window/Window.cpp` | `Hide`/`SetBounds` 转同步 Submit（432/449）；Submit 默认期限及等待（1784–1832）。 |
| `Inkeys/Inkeys/Window/Window.CommandCompletion.h` | `Wait()` 使用条件变量到 deadline，超时不等于 owner 已退休（70–82）。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | RuntimeRevision 等待（2575）、关闭 transition fence 主动发布 ControlWake 和 runtime revision（2795–2816）。 |
| `Inkeys/IdtFreezeFrame.cpp` | `SubmitFreezeSurface` 持 Freeze 表面互斥锁执行真实 `UpdateLayeredWindowIndirect`（38–44）。 |

### 2. 六方向路径与 Freeze 负载

| 方向 | 接纳 / 准备路径 | 主栏最终转换 | Freeze 回调实际工作 |
| --- | --- | --- | --- |
| D→P | COM 可信会话发布 `Ppt::PublishSession` → `SyncPrimaryWorkspaceStateMode` / target 发布 → StateMonitoring 准备、Draw3/PPT UI-ready、native Complete | Committed=P 后 `SetWhiteboardActive` 也投影 Presentation chrome；`BarWorkspaceSceneState::Enter` 捕获 Desktop 基线，已有展开底栏跟随 PPT 屏幕 | `PublishActive(false,revision)` 可唤醒，但如果没有旧白板责任，inactive 已确认，不全屏绘制，也不必 Hide。 |
| P→D | 可信 EndSession → `CompletePresentationExitStateMode`（一次选择收尾）→ 新 Desktop request → 同一协调提交 | `Leave` 恢复 Desktop 基线与连续 Y 动画 | 同上，无旧白板责任时不全屏绘制。 |
| D→W | 白板公共按钮 `DispatchBarWhiteboardButtonAction` → `RequestWhiteboardActive(true)` → 真实关闭 Host Down → StateMonitoring 准备工具 / native / Draw3 output → 发布 active 背景 → 背景 prepared → native Complete → Committed | `Enter` 捕获 Desktop 基线，保持浮动/折叠，不强制展开 | active 的新 revision 清 prepared。首次需要整屏真实提交；共用 renderer、GetDC/ULW 和同步 SetBounds 后才能 ACK。 |
| P→W | 与 D→W 同一请求路径；已有 PPT 会话只作为后台事实保留 | `Switch`，不重拍 Desktop 基线，不经过 Desktop | 同 D→W，active 目标需要新 revision 的背景凭据；同屏也不能沿用旧 revision 冒充。 |
| W→D | 关闭白板时按最新可信 PPT 事实选择 Desktop → `PublishActive(false,revision)` → 背景真实 Hide ACK → native 准备/目标/完成 → Committed | `Leave` 恢复首次 Desktop 基线 | 退场执行真实 owner Hide；不是整屏重画。仍可能因同步 owner 等待延迟共享调度。 |
| W→P | 关闭时最新 primary=P；背景 inactive ACK 后准备真实 target 与分页 UI-ready；完整 owner Complete 后统一提交 | `Switch`，不先恢复 Desktop | 同 W→D 的 Hide 分支，随后分页需按 Pending 准备，不可等待主栏 Committed 才准备。 |

补充：W 最小化/恢复仍为 Whiteboard，改变 view / revision；后台 PPT 开始、结束或换文稿不抢走 W，关闭 W 时重新选最新 primary。快速反向切换会更新请求版本，但旧 owner 实际责任必须退休后才能让新请求侵入；`WorkspaceHandoff::Request`（170）和完整 token / generation 不能为减少等待而移除。

### 3. 已确认的共享调度阻塞路径（性能占比待采样）

#### 3.1 白板背景在与 Bar 同一个串行回调批次执行全屏软件绘制和 GDI/ULW

`RenderBackground`（Whiteboard.cpp:129）在 `Client::WhiteboardFreeze` 注册（298–299）。该 client 排在 Bar、四个 PageControl、Settings 之后（RenderPipeline.cpp:54–61），因此本批的背景慢会推迟下一批 Bar 的成功提交。D/P→W 常与分页、主栏按钮状态动画重合，不能只测 Bar callback 自身耗时。

进入 active / 新资源 / 新业务 revision：

1. `BackgroundState::Publish` 新版本会 `prepared_.reset()`（BackgroundState.h:50），同 active/revision 则直接 false，不重复唤醒。
2. `RenderBackground` 在尺寸/设备变化时重建，并在资源重建或 `!Prepared(revision)` 时 `Invalidate()`（Whiteboard.cpp:183–192）。
3. 真实整屏背景来自主屏 rcMonitor（60–65），不是仅主栏大小。`Render()`、`GetDC(COPY)`、`SubmitFreezeSurface` / ULW、`ReleaseDC`、`EndFrameDraw` 同一回调（205–239）。
4. `SubmitFreezeSurface` 另取得 freezeSurfaceMutex（IdtFreezeFrame.cpp:42），之后执行真实 ULW；该互斥不证明有竞争，需采样等待。
5. 窗口几何确认成功后才 `CommitPresented` / `ConsumeDamage` / `BackgroundState::Prepare`（Whiteboard.cpp:268–271）。无动画时 Idle（273），不会仅因 active 而永续全屏刷新。

唤醒来源除 `PublishActive`（Whiteboard.cpp:361）外，还有主题实际变化（389）及显示器主屏 bounds / DPI 与已渲染目标不符的订阅回调（314）。同一revision仍可因设备重建、主题/显示变化或失败Retry产生多次真实draw，所以不能只按revision计一次提交；无这些条件且成功静置时不持续draw。

不能把它归因为所有六方向共同全屏重画。D↔P 没有 owned 背景时只会轻量 inactive 确认；W→D/P 是 Hide。

#### 3.2 渲染线程跨 owner 同步等待，既可放大退场卡顿，也可与入场重画叠加

- active 提交后，Whiteboard.cpp:259 `service.SetBounds(Freeze, presentation, currentSurface)`；旧无版本兼容入口还会 Show（260）。
- inactive 且仍持旧白板表面时，Whiteboard.cpp:155 `service.Hide(Freeze, guard)`。
- Window.cpp:449 / 432 都调用默认 `Submit`。Submit 于1799设置1500ms deadline，非 owner 排队后1832 `completion->Wait()`；Wait 的条件变量见 Window.CommandCompletion.h:70。

1500ms 是等待上限结构，**不是实测每次耗时**，更不是可强制截断的 Win32 执行时限。即便重复尺寸的 owner 执行最终无写入，跨线程排队和等待仍实际发生。该路径处在共享渲染回调内，会阻止后续/下一批其它客户端推进；仅缩短动画 duration、加一个 UI 线程、改变帧上限不能解决。

建议优先采样 WhiteboardFreeze callback 的 draw/GetDC/ULW、owner 排队/执行、SetBounds/Hide 等待；若 owner 等待明显，应让状态/owner 层完成必要请求并主动回执，render 只消费或发一次有界请求后返回 Retry。不要改为无确认的 fire-and-forget；必须保留请求 revision、surface HWND/generation、真实 bounds/visible 和取消/恢复责任。异步接缝若已存在则复用，避免扩成第二套协调框架。

#### 3.3 失败重试存在重复全屏重画放大机制

`SetBounds` 返回失败时在 Whiteboard.cpp:265 `backgroundScene.Invalidate()` + Retry；任何 GetDC/ReleaseDC/EndDraw/ULW 失败会释放 target 并 Retry（249–253）。下一回调可能重新绘制整屏，而不是仅继续 owner 几何 ACK。恢复合同要求失败保留 dirty 正确，但应测得失败来源后区分“像素未成功”与“像素成功但 owner 未完成”，避免在可以复用当前像素且身份/epoch有效时无谓重建；不允许跨失效 epoch 复用或提前消费 damage。

### 4. 进入迟与动画掉帧是不同问题

`StateMonitoring`（IdtState.cpp:3346）每轮等待 `WaitForProductRuntimeRevision(runtimeRevision,250)`，之后才重查 request / background / native 结果。该 wait 只等 Draw3 runtime revision（Host.cpp:2575）。请求接纳关闭 fence 会主动 `PublishControlWake` + `PublishRuntimeRevision`（Host.cpp:2815–2816），所以不能概括为所有切换都纯轮询。

但是，背景 `Prepare`（Whiteboard.cpp:271）与 inactive commit（BackgroundState.h:132）只更新自己的状态，未在这些路径通知此 Draw3 waiter。背景 ACK 发生时若 Draw3 无其它进展，StateMonitoring 可能等到当前250ms timeout 才继续提交；原生窗口的 view 通知也仅置 atomic（IdtState.cpp:1349）。这说明存在 **ACK→协调消费的轮询延迟候选**，不是已经测到250ms，也不能据此解释已经推进中的低FPS。

主栏工具与 workspace 帧来自 Committed（Bar.RenderLoop.cpp:1197），停靠 `Enter/Switch/Leave` 也按同一帧的 Committed（13257）。因此 Draw3 / 背景 ready 慢的直接表现通常是主栏场景动画开始晚。提前把 desired 当 committed、跳过首帧 / owner ACK 会违反三态与失败恢复合同。若测得 ACK 延迟主导，可复用已有 wake 或一个轻量协调唤醒，把背景/原生完成通知纳入等待，保留250ms作为兜底；不要轮询得更频繁来掩盖。

建议分别记录：request accepted、Host fence、native prepared、target output ready、background prepared/inactive committed、native completed、workspace committed、Bar首次消费、Bar每次真实成功提交。前段是切换响应延迟；后段间隔与共享batch耗时才是动画卡顿。

### 5. 主栏重复投影存在，但尚无“每帧重启动画”的证据

- `SetWhiteboardActive`（Bar.Main.cpp:500–513）会在 scene 变化时结算 Recall，后者请求一次轻量刷新（Bar.Button.cpp:1748–1758），再同步 `UpdateRendering()`。
- 统一提交紧接 `RefreshToolState`（IdtState.cpp:3585–3586），会再次 `UpdateRendering` / StateUpdate。
- `SwitchWorkspaceStateMode` 在目标工具改变后也调用 Refresh（1577）；PPT后台事实发布也可能调用UpdateRendering（Bar.Main.cpp:417）。
- `UpdateRendering(true)` 通过静态 mutex 串行，先读取已提交工具，再做 StateUpdate（Main.cpp:286–295）。StateUpdate 再取得短时 presentationMutex（Button.cpp:944）；render 的主栏目标扫描也取得同一个 recursive presentationMutex（RenderLoop.cpp:1676–2786）。这是可测锁竞争与重复CPU工作候选。
- 但是公共内容 TransitionContent / style key、布局指纹与 batch CanJoin 已存在：同目标不应重启动画（Button.cpp:1053+；RenderLoop.cpp:2544–2586）。Publisher 的多次 Request 也按位合并。不能把“调用多次UpdateRendering”直接等同“动画被反复重启”。
- render本帧已经自己调用 `UpdateMainPresentation(state.stateModeSnapshot)`（RenderLoop.cpp:1677），因此可在证实重复投影显著后，把同一次 commit 的主栏通知合并为一次，并复用现有内容更新/唤醒合同；不要先删除所有生产者 StateUpdate，可能改变即时输入与历史反馈。

主栏投影当前同时读 committed Whiteboard、后台PPT active、Freeze与内容事实（Button.cpp:961–970）。后台 PPT active 是白板 Resume/EndShow 等业务所需事实，并非所有场景字段都能机械改为Committed；应保存一批一致输入、区分场景和后台会话用途，测得阶段性二次布局后再做最小化调整。

### 6. 按钮点击路径未发现直接 COM / 磁盘 / 模态等待

白板 clickFunc（Bar.Button.cpp:582–589）只分派恢复/请求/失败取消；`RequestWhiteboardActive`（IdtState.cpp:1361–1384）更新意图、focus request、关闭Host新Down。重文档准备在 Draw3 owner，原生 Prepare/Complete 在状态线程同步提交；没有证据把动画卡顿归因于白板按钮直接做COM或同步保存。

状态锁和协调锁只保护短快照，窗口同步在其外（IdtState.cpp:1548–1575 / 3557–3572）；不能在未采样时声称它们跨 `PrepareWorkspaceWindowPolicy` 持锁等待。`GetCommittedWorkspaceStateSnapshot` 确实要取 stateModeTransitionMutex（1263–1273），锁等可观测但目前未证明为重业务锁。

### 7. 现有测试能复用，但不覆盖完整耗时链

`Bar.MultiTarget.Test.cpp:550–559` 已覆盖 D↔P、D↔W、P↔W及反向、禁动画的主栏几何；它由 `workspaceScope_->Set` 直接发布场景，再驱动真实Bar渲染，不能证明生产 Draw3/背景/native 协调的总延迟。共享吞吐基准需要明确是否包含 `WhiteboardFreeze` 真实全屏回调及其 owner 事务；仅两个白板分页不能验证全屏 Freeze 干扰。

## 最小修复候选与验证点（供设计选择）

1. 先建立三态阶段时间及共享 raw callback/batch证据，定位开始迟、Bar自耗时、其它client串行拖慢、owner等待、重复布局这几类。复用UI3现有raw门，不逐帧日志/写盘。
2. 优先处理真实渲染回调里的 owner 等待；同 revision 同 HWND/generation只投递一次，ACK未知时立即返回，不把“排队成功”当真实背景完成。
3. 对像素已成功、owner ACK未完成的合法状态保留有界当前候选，重试尽量仅继续欠的确认；取消/epoch/设备失效必须废弃候选并正确保留旧已呈现状态。
4. 若阶段证据显示背景 ACK 后空等，则主动唤醒协调消费；不降低真正 target/background/native 就绪要求。
5. 若锁/投影证据成立，再合并单次 Commit 的主栏重复通知，不改变公共按钮、单一目标、动画时钟和布局批次合同。

验证矩阵：六方向×冷/热×1/1.5/2倍率×浮动/展开底栏/折叠；快速反向、后台PPT变更、最小化恢复、尺寸/DPI/设备变更、背景成功但owner延迟、Hide失败/超时/取消、stale HWND/generation、0/少/大量墨迹、静置无无限唤醒。对慢owner的确定性测试必须断言其它client仍推进、旧ACK不提交、完整凭据才Commit；软件呈现间隔不等于光学显示帧率。

## Related Specs

- `.trellis/spec/native-desktop/index.md`
- `.trellis/spec/native-desktop/rendering-and-ui.md`：公共按钮、主栏状态派生、共享串行调度器、脏区事务、workspace生命周期。
- `.trellis/spec/native-desktop/whiteboard.md`：首帧/背景/native真实凭据、三态统一交接、共享渲染/输入。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md`：raw/详细计时门、真实成功提交、有限采样与不得虚称GUI通过。
- `.trellis/spec/ppt-interop/native-session-ui3.md`：session/target/UI-ready与BasicControl权限。
- `.trellis/tasks/10-27-whiteboard-first-open-visibility/design.md`
- `.trellis/tasks/10-28-workspace-failure-recovery/design.md`

## External References

无。本次原因调查只依赖仓库当前实现，不新增外部API性能假设。

## Caveats / Not Found

- 本轮未构建、未运行软件基准、未启动GUI；所有性能占比、真实间隔和主观流畅度未测。
- 源码证实同步等待/串行负载路径，但未证实本机owner实际等待多久，未证实哪个候选为用户现场首要根因。
- 没有发现白板按钮click直接执行COM/文件I/O；不能以未发现推断其它完整系统链完全无阻塞。
- DComp/ULW/Draw3恢复、首帧与窗口责任已有正确性修复，本任务必须在此基础上减慢路径，不提前发布新工作区或清除输入门。
