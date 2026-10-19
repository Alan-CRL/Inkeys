# Research: PPT 竖直分页控件拖动停住与锁/调度路径

- Query: 核对两侧居中竖直 PageControl 拖动卡住的线程、持锁、同步 owner 命令与回调链，区分当前局部停住反馈、已修复旧死锁与条件性重入风险，给出最小方案及无 GUI 验证边界。
- Scope: mixed；以内部静态源码为主，外部只核对 Win32 capture/message 语义。
- Date: 2026-10-18
- Baseline: 当前 checkout，主会话给定 HEAD `13783fe7`；本研究者未执行任何 Git 操作。
- Authorization: 仅研究；未修改生产代码、测试、配置或规范，未构建/执行测试，未启动 GUI/Office，未创建 commit。

## Findings

### 1. 结论与用户补充必须分开

用户补充：反馈安装版本很可能为 `20261010b`，也可能为 `20261014a`，尚未确认；所谓“卡死”是被拖动的控件停住，主栏仍可操作。该描述不能直接写成整个应用死锁。

本轮直接确认：

1. 当前 owner 拖动热路径对 `presentationMutex` 只 `try_lock`，失败先保留 latest-wins 绝对候选，再显式请求该 pair。渲染线程在同步等待 Window Service owner 前释放 `renderTransactionMutex`。旧的“owner 阻塞等呈现锁，而 render 同步等 owner”环在所查当前拖动路径已被打断。
2. 若两控件组碰撞或屏幕 clamp 使候选等于上一 feasible 布局，拖动可以在没有任何锁等待的情况下完全不移动。碰撞时回退整个 pair 的 X/Y，未按可行轴滑动；这是既有明确约束，不足以证明缺陷，不能擅自删约束/换 solver。
3. 单一 PageControl client 可以反复 `Retry`、保持旧帧或输入门禁，同时 Bar client 按下一帧继续运行。这与局部停住比全局锁环更相容，但本轮没有现场状态，不能宣布是哪一种。
4. 找到独立的条件性非递归锁重入链：Down 持 `renderTransactionMutex` 调 `SetCapture`；若另一个 PageControl HWND 仍持 capture，旧 HWND 的 `WM_CAPTURECHANGED` 可在同 owner 线程同步重入并再申请该锁。该链不依赖 `presentationMutex.try_lock`，但尚未实机触发，而且一旦成立会影响 overlay owner/共享渲染，不能作为当前“仅控件停住、主栏持续可操作”的根因。

如果“主栏可操作”指持续的新点击及视觉动画均正常，可以排除这些组件所共用 owner/renderer 的持续硬阻塞；若仅指某个已排队业务动作仍执行，则尚不能如此排除。

### 2. 文件与相关规范

| 文件 | 作用 |
| --- | --- |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp` | 四 surface 状态、WndProc、拖动 mailbox、直移、RenderSurface、owner 提交、回调和生命周期。 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cppm` | 生产纯策略、触摸锁存、drag tracker、clamp/碰撞/runtime layout、输入门禁与续帧判定。 |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp` / `.cppm` | Bar/四 PageControl/Settings/Freeze 共用的单串行 renderer；请求、续帧、Retry 和注销等待。 |
| `Inkeys/Inkeys/Window/Window.cpp` | overlay owner thread 创建/消息循环、同步 Submit、取消 capture 广播及窗口操作。 |
| `Inkeys/Inkeys/Window/Window.CommandCompletion.h` | 可选 deadline；普通未设 deadline 的命令最终使用 `future.get()`。 |
| `Inkeys/Inkeys/UI/Ppt/Ppt.cpp` | facade position/state mutex、layout 交接、页显示回执和设置保存分派。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | PageControl 光源通知、成功窗口范围发布；通知使用 PostMessage。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp` | Scene mutex/shared-light registry 与锁外 wake 回调。 |
| `Inkeys/IdtPlug-in.cpp` | PPT 业务队列、COM/焦点/设置线程；纯值 UI-ready 回执。 |
| `InkeysHeadlessTests/page_control_tests.cpp` | 现有 tracker、版本、input deadline、touch 与 runtime layout 测试；只有读取，没有执行。 |
| `.trellis/tasks/archive/2026-08/08-24-ppt-page-controls-bar-style/implement.md` | 第二/三轮设备验收修改及输入解锁续帧修复的历史记录。 |

已读取 `.trellis/workflow.md`、`.codex/agents/trellis-research.toml`、当前任务 `prd.md`、native-desktop index，并重点遵循：

- `.trellis/spec/native-desktop/rendering-and-ui.md:182`：共享串行调度器及逐 client Retry；`:438`～`:439`：owner 不等待呈现锁、latest-wins、两个 stale gate、ReleaseCapture 锁外。
- `.trellis/spec/guides/cross-layer-thinking-guide.md:36`：Owner WndProc→render/presentation 的等待/重入审查与 pending 所有权。
- `.trellis/spec/ppt-interop/native-session-ui3.md`：运行位置与保存分离、session/epoch/pair version、COM/配置业务在线程队列执行、拖动不抢焦点。

### 3. 线程与锁的实际所有权

| 线程/对象 | 实际职责与证据 |
| --- | --- |
| Window Service overlay owner | `Window.cpp:870` 的 `RunGroup(false)` 记录 `overlayThreadId`，`:985`～`:1031` 用同一个 threadId 创建包括 Bar 和四个 PageControl 的 overlay HWND；`:911`～`:938` 在这一线程 drain 命令与 DispatchMessage。 |
| 唯一 UI3 renderer | `RenderPipeline.cpp:649` 创建一个 `std::jthread`；`:807`～`:860` 按固定顺序串行执行客户端回调。每个客户端的 active 回调计数不代表独立工作线程。 |
| PPT 观察/业务线程 | `IdtPlug-in.cpp:108`～`:125` 投递业务请求；`:1092`～`:1105` 带 predicate 的 100ms condition wait 出队后释放业务 mutex；`:1108` 之后才做 COM、焦点或写盘。 |
| PageControl publication 调用方 | PPT facade、白板、显示器通知等；发布只持短值锁/发 Request。 |

PageControl 的四个关键锁是独立 `std::mutex`，定义于 `PageControl.cpp:254`～`:258`，不是递归锁：

| 符号 | 内容/锁顺序 |
| --- | --- |
| `T = renderTransactionMutex` | Surface Scene、AnimatedBounds、拖动与触摸输入状态；WndProc 的 Hit/Move/Down/Up/Cancel 以及 RenderSurface Configure/PresentScene 使用。 |
| `P = presentationMutex` | 渲染后的 Window Service SetBounds/Show/Hide 与 owner 纯直移互斥。render 普通 lock；owner 直移只 try_lock。 |
| `D = dragCommitMutex` | 两 pair tracker、handoff versions、direct-move revision 写入串行。短值事务。 |
| `S = snapshotMutex` | published PPT/whiteboard/pageCommit/revision。D 与 S 同时需要时固定 D→S；Snapshot 只拿 S。 |

另有 `callbackMutex` 只用于复制/替换 callback，实际 callback 发生在该锁外。`Ppt.cpp::stateMutex` 保护 facade 运行偏好；PagePresented 转发纯值回执时仍持该锁以串行同页暂停/恢复，不等同于持 PageControl T/P。

### 4. 完整持锁调用图

#### 4.1 owner Move：不能只看到 try_lock 一行

```text
Window owner / PageControlWindowProc(WM_MOUSEMOVE)
  T.lock                                       PageControl.cpp:2184
  校验 targetVisible/inputLocked               :2185
  Page 越阈值则取消 press、进入 dragging       :2194–2205
  UpdateDragLocked                            :2209 / :1373
    SnapshotForRender -> S（返回即释放）       :1395 / :510
    GetDeviceEpoch -> assetMutex（短锁）       :1403 / RenderPipeline.cpp:1198
    Clamp / 整组碰撞 / feasible 等价早退       :1409–1419
    PublishDragCandidate -> D→S               :1422 / :367–386
    计算 pair 绝对窗口目标                    :1428–1450
    P.try_lock                                :1464–1465
      忙：保留 pending + RequestDragPair      :1466–1488
      成功：顺序 SetWindowPos 两 HWND        :1490 / :1336、:1350
        第二窗失败时回滚第一窗                :1356
        成功才写两个 bounds 与 D/revision    :1501–1506 / :430–445
        仅短锁发布光源接收范围                :1510 / Bar.Interaction.cpp:7379
  T.unlock                                    :2210
  FlushCommittedDrag -> 短 D、短 callbackMutex、锁外 commit callback :2211 / :1553–1580
```

Move 虽然会等待 T，但正在 P 内同步等待 owner 的渲染分支不持 T（见下一节）。因此不能从“T 是阻塞锁”直接宣布与 P 的旧死锁仍在。

#### 4.2 renderer：T 的慢绘制和 P 的 owner 等待分为两阶段

```text
单 UI3 renderer / RenderSurface(index)                   :1743
  SnapshotForRender -> S                                 :1755
  GroupBudgetForFrame -> T，函数返回时释放              :1758 / :1597
  T.lock                                                :1781
    同一 directMove/publication revision 复核           :1784–1787
    ConfigureSurface / Scene / 输入锁 / 动画            :1796、:962
    PresentScene -> D2D GetDC / ULW / ReleaseDC / EndDraw :1929、:1160、:1186
  T.unlock（离开大括号）                                :1964
  long-press 业务 callback 在 T 外                      :1966
  P.lock                                                :1971
    再验 revision                                       :1974–1977
    ObservePendingDrag -> D                             :1978 / :405
    service.SetBounds / Show / Hide                     :2002–2007
      非 owner：入短 queue 锁并等 future                 Window.cpp:1376–1397
      owner：DrainCommands 先移走队列，再 Execute        :1400–1415
    S 短锁记录实际窗口提交                              PageControl.cpp:2036
    末次 revision gate                                  :2041–2050
    CommitDragSurface -> D                              :2052 / :417
    短锁发布光源区域                                    :2060
  P.unlock                                              :2062
  FlushCommittedDrag / AcknowledgePage                   :2063–2064
  如需恢复日志，另取一次 T                              :2066
```

`Window.cpp:1385` 对同 owner 线程直接 Execute，避免自己排队后等自己。跨线程 `Submit` 的 queue mutex 在 `future.get()` 前已经释放；DrainCommands 在 Execute 前也已释放 queue mutex，不能误画成“队列锁跨 HWND 操作”。普通 SetBounds/Show/Hide 没填 deadline，`Window.CommandCompletion.h:16`～`:22` 最终会无期限等已接受命令；这放大 owner 硬阻塞的影响，但不是凭本函数就能证明死锁。

本节只证明显式 Window Service 阶段的 T 已释放；D2D/ULW 内部可能慢或与系统消息发生复杂交互，不可由静态项目锁图证明绝无驱动/Win32 等待。没有现场栈，本轮不把内部实现猜测升级为确切等待环。

#### 4.3 Up/Cancel、callbacks 与短锁顺序

- Up：`PageControl.cpp:2341` 取得 T、处理 PointerUp 并清 `dragging/dragPending/long-press`，`:2367` 解锁；`:2368` 在短 D 内 Release tracker；`:2371` 的 ReleaseCapture 已在 T 外。pending 会显式 RequestDragPair，实际结束拖动再 RequestAll，`:2404`～`:2407`。
- Cancel/CaptureChanged：`:2428` 取得 T，`:2443` 离开作用域；Rollback 使用 D→S，若需恢复 Scene 再独立取得 T，`:2446`～`:2451`；ReleaseCapture 在末尾锁外，`:2467`。Window Service 广播涵盖四个 PageControl，`Window.cpp:1606`～`:1618`。
- D→S：PublishCandidate `:367–368`、Rollback `:462–466`、PublishPptState `:2947–2948`；这几个临界区没有调用 Window Service 或外部业务。所查路径未发现持 S 再等待 D 的反向边。
- Flush：`:1557` 读 committedLayout 后释放 D；`:1570` 复制 callback 后释放 callbackMutex；`:1573` 才调 `Ppt.cpp:136` 的 CommitPageLayout（facade stateMutex），不持 T/P/D/S。该 callback 只提交运行偏好，不写盘、不请求 Window Service。
- Acknowledge：`:1701` 的 S 在调用 `:1713` 前释放；callbackMutex 同样已释放。`Ppt.cpp:166`～`:173` 持 facade stateMutex 校验 epoch 后进入 native 纯值回执，`IdtPlug-in.cpp:141` 短取 pptSessionMutex，`:151` 释放后才发 Draw3 UI-ready。观察线程反向 PublishSession 等调用在 pptSessionMutex 作用域之外，见 `IdtPlug-in.cpp:575–579`、`:758–769`、`:784–788`；没有在这些点找到相反持锁调用环。
- 光源：owner Move 调用的 Enter/Left 是 `PostMessage`，`Bar.Interaction.cpp:7357`～`:7368`；Bounds 发布只持 region 短锁，`:7379`～`:7384`。没有同步等待 Bar 业务。
- Scene 的 shared-light registry→Scene mutex 与 wake 调用分开，`Bar.Scene.cpp:1908`～`:1926`、`:1934`～`:1953`；PageControl 给 Scene 的 hook 仅 RequestSurface，`PageControl.cpp:883`。Scheduler Request 是原子请求位+SetEvent，`RenderPipeline.cpp:951`～`:955`，不等 render callback 完成。
- `Scheduler::Unregister` 确有 condition wait，`RenderPipeline.cpp:936`～`:943`；它是 Release/退出同步注销，不在正常拖动 Move/Up 路径。PPT 业务线程的 condition wait 也不持上述 PageControl 锁，不能归为本次拖动阻塞。

### 5. 独立条件风险：Down 内的 SetCapture 重入

确切源码链及条件：

```text
前提：A、B 为同 overlay owner 的两个 PageControl HWND，
      A 仍有 capture，B 收到可接受的 Down。

B::WM_LBUTTONDOWN
  T.lock                                      PageControl.cpp:2258
  已通过可见/输入门禁/命中                    :2259–2272
  SetCapture(B)                               :2273
    使 A 失去 capture
    USER32 向 A 发送 WM_CAPTURECHANGED
      A::PageControlWindowProc
        Cancel/CaptureChanged 分支            :2423
        T.lock（同线程再次申请非递归 T）      :2428
          B 尚未返回，不能释放第一次 T        => 条件性自等
```

- 四 HWND 同线程是 Window.cpp RunGroup/CreateGroup 的直接证据，见第 3 节。
- 普通单鼠标按下拖动时，Down 通常先获得 capture，直到 Up/Cancel 才释放，不会必然有 B 的第二次有效 Down；不能写成“每次拖动都会死锁”。
- 可能入口是跨 HWND 的多触点/替换 capture 或重复输入。当前触摸 active id 是每个 `SurfaceState` 独立记录；一个空闲 surface 收到无 PRIMARY 标志的 Down 可依 fallback 规则接管，`PageControl.cppm:160`～`:174`，再同步转译 Down，`PageControl.cpp:2152`。它不是全四窗共享的触点互斥。但具体 Windows/驱动是否以该序列送达两个 HWND 需要实测，未在本轮验证。
- `WM_CAPTURECHANGED` 原始文档与 Mouse Input Overview 均使用 sent/sends；这不是本仓库 PostMessage 延迟投递。结合 SendMessage 的同线程立即调用语义，得到上面的重入链；这是基于正式消息语义的静态推论，不是 Windows 内部调用栈实测。
- 如果该链真的卡住，同一 overlay owner 的 Bar 新输入会受影响，renderer 很快也会等 T 或 owner；与“只有竖控件停住，主栏持续新交互正常”不吻合，应单列风险，不能替换现场根因。

待审批的最小防护建议：Down 拆成“短锁决定候选→锁外 SetCapture→短锁复核并建立本次输入/拖动”的事务。必须保留 token/session/epoch/mode/targetVisible/inputLocked 复核。不能简单把 SetCapture 挪到整个 Down 块末尾：

1. 长按启动依赖 `GetCapture()==hwnd`，`PageControl.cpp:2312`～`:2319`；保留 Down 立即翻页、原始按下时间与一次系统 repeat timing 快照。
2. 同 pair 的旧 A 在 CaptureChanged 中可能回滚同一个 drag tracker。如果 B 已在 SetCapture 前执行 BeginDragTracking，旧 A 的 cancel 可能误回滚 B 新 tracker。应先完成旧 capture 清退，再确认本次手势并建立新 tracker；若跨解锁状态变化则撤销本次 Down，不能覆盖新代次。
3. `PromotePptWindow` 和业务分派仍在 T 外；不能靠把 std::mutex 改为 recursive_mutex 掩盖状态重入。

### 6. 更贴合局部停住的状态路径

#### 6.1 碰撞/边界早退：现有设计，不等于新缺陷

`UpdateDragLocked` 始终从原始 Down 屏幕点和原始 layout 计算完整绝对 X/Y，`:1381`～`:1393`，之后：

1. `ClampPageControlLayout` 对当前 pair 限屏，`:1409`；middlePairWidth 不能小于 0，middlePairHeight 限制在竖直窗口主体入屏范围，`PageControl.cppm:1089`～`:1101`。
2. 以另一个已启用 pair 为障碍，`PageControl.cpp:1414` 的 `DragLayoutCollides` 调 `PageControlGroupsOverlap`；后者只检查 bottom 与 middle，不包含 Main Bar，`PageControl.cppm:1106`～`:1123`。
3. 相交则 `CopyDragPairPosition(pair, state.feasibleLayout, mergedCandidate)`，`:1415`～`:1416`。这会同时恢复该 pair 的 width 与 height，不是只取消碰撞轴。
4. 结果等于上一 feasible 就在 `:1419` 直接返回，不发布新 revision、不请求 renderer，也没有等待；当前 TraceDrag 没记录这条拒绝原因。

具体可见表现：

- 侧边控件往外拖到屏幕边、向上/下拖到主体入屏限制，会停在 clamp 边界；向相反方向仍可走，这是正确约束。
- 竖控件靠近底部横控件时，继续朝横控件所在方向移动会整组停住；对角移动只要 Y 仍使整体相交，X 即使本可小幅改变，也随整个候选一起退回。左右对称 pair 的另一端碰撞也可以阻止正在拖的一端。
- 从最后接受点改为纯水平方向，且固定 Y 仍可行，现有代码应允许移动；不是“只要接近一次碰撞就永久锁住所有方向”。原始起点仍决定候选，用户需要把指针退回可行区域。如果退回中心等明确可行方向、放手重新拖动仍永久不动，则碰撞不足以解释。
- 不应仅为消除“停住”修改屏幕和组间约束。若产品要碰撞边缘沿可行轴滑动，应作为显式体验决策另写验收，不是并发修复的必然组成。

#### 6.2 inputLocked 与按压清退

`inputLocked` 来源只有“目标隐藏 / layout transition deadline 未到 / whiteboard switching”，`PageControl.cppm:901`～`:905`。ConfigureSurface 在 `PageControl.cpp:962` 每次正常帧重算；Move/Down 入口为真即 return，`:2185`、`:2259`，HitTest 返回 HTTRANSPARENT，`:2164`。普通拖动本身不把 inputLocked 打开；不能笼统说“拖动拿了输入锁后没释放”。

当前 `ShouldContinuePageControlFrame` 包含 transition deadline，`PageControl.cppm:908`～`:915`；RenderSurface 使用该生产函数，`:1899`。所以“Scene 先 idle，永远无人唤醒 deadline 最终帧”的历史 bug 已有当前防护，不能重复宣称仍存在。若现场 locked，需查是否真有 mode/visibility/workspace 变更，或单 surface 配置失败导致没正常推进；deadline 有没有被重复延长、资源/scene 失败是否发生在 ConfigureSurface 成功之前。

`WM_MOUSELEAVE` 只取消 long-press/hover，不将正在 dragging 的手势清掉，`:2240`～`:2246`。Up 和 CaptureChanged 都清 dragging/dragPending；session/epoch 改变也在 `:1397`～`:1400` 清退。真实 capture loss 后本次手势停住且需重新 Down 是预期取消，不一定是永久状态卡死。

#### 6.3 pending、单 client Retry 与主栏正常

- 每条 candidate 先完整写 mailbox，再 release 发布 directMoveRevision，`:362`～`:386`；try_lock 忙/非纯平移/窗口失败都显式 RequestDragPair，`:1459`、`:1487`、`:1497`。没有“等下一条 WM_MOUSEMOVE”这一旧缺口。
- tracker 要同 pair 两个 surface 都成功才能清 pending，`PageControl.cppm:478`～`:493`。松手保留 pending/ownership，`:496`～`:511`；RenderSurface 成功窗口提交后按 pair version Commit，`:2052`，不会因另一 pair global revision 更大而永远不能确认。
- 一侧 `scene-configure/scene-bounds/resources/GetDC/ULW/EndDraw/window-commit` 失败会返回 Retry；另一个 client 可以成功。render 返回 Retry 的时刻没有卡在函数内部，Scheduler 继续其他 client 并在后续帧只重试对应位，`RenderPipeline.cpp:837`、`:884`～`:888`。
- 如果某个 client 一直失败，控件可以停在旧图像或保持 pending。现有 retry 报告有 `INKEYS_PAGECONTROL_PRESENT_TRACE=1`（默认关）和每窗真实 HWND/owner/visibility/bounds 信息，`PageControl.cpp:1602`～`:1694`；没有现场日志不能区分配置失败、资源失败、窗口提交失败或只是碰撞。
- pending 表示候选尚未被 pair 完整确认，本身并不关闭拖动输入；不能仅观察 pending 就认定 inputLocked 为真，或把 ownsLayout 当作 mutex。

### 7. 历史对照

已归档 `08-24-ppt-page-controls-bar-style/implement.md:82`～`:98` 记录：

- 第二轮：恢复多拖动入口；owner presentationMutex 只 try_lock；latest-wins 保留候选与显式 pair render fallback。
- 第三轮：删除 DeferWindowPos，改顺序 SetWindowPos+第二窗失败回滚；成功纯平移不请求 Scene render；revision payload 先写后发布；两道 stale gate；Up 请求最终吸收。
- `:112` 另记录 transition deadline 自续帧，保证 inputLocked 到期能自行解除。

主会话已进行只读 Git 核对并向本研究者提供以下事实（本文件将它明确记为 root 提供的历史证据，研究者未执行 Git）：

- `579d1c5eb358d368a0e42a2fc298b13d2dbc3f38`（2026-08-26，`stabilize PPT page control interactions`）是 `20261010b` 发布准备 `a50ad143`、`20261014a` 的 `5d319bbf` 及当前 HEAD 的共同祖先。
- `a50ad143` 与 `5d319bbf` 的 `PageControl.cpp` / `.cppm` blob 完全相同。
- `0eb1c7c5` 对这两文件仅增加 pageAckEpoch 传递/ack 比较，没有改 drag、锁或几何。

因此目前两个反馈候选版本已经具有旧 try_lock/latest-wins 修复。不能把这轮当成“尚未加 try_lock”来规划。用户精确安装包版本仍待确认，历史相同也不能证明真实设备路径/输入/显示条件相同。

### 8. 建议的最小处理顺序（待批准，不在本轮实施）

1. 保留既有 T/P 分段、D→S 次序、pending latest-wins、逐 pair version、顺序直移与回滚，不做线程架构重写、不把全部锁换 try_lock。
2. 为局部停住优先做数值状态定位：输入 source/surface/pair、dragging/dragPending、capture HWND、inputLocked 及原因、原始/Clamp/feasible XY、collision 结果、session/epoch/pair version、pending/mask、request/retry 阶段。记录“正常边界拒绝”和“未完成提交”两种不同结果；不记录文件路径/文稿内容。
3. 复用现有 Drag 与 Present trace 能取得的证据；现有碰撞/门禁早退没有明确原因，可在批准实施后加一个限频的拒绝理由点。不要靠移动鼠标或无条件 RequestAll 掩盖丢唤醒；不要取消输入安全门禁或把单侧成功当 pair 完成。
4. 条件性 SetCapture 风险如纳入本任务防护，以第 5 节分阶段 Down 边界修复，并回归 long-press、同 pair 换 capture 和 stale session。该项与用户局部停住候选分别验收。
5. 若日志显示合法候选在 try_lock fallback 后永远没有 client consume，查 request 位/注册/Retry 接线；若 consume 持续失败，修确切失败阶段；若所有候选均被 collision/clamp 拒绝，先按产品约束判断是否符合设计，不擅自修改 solver。

### 9. 有意义的无 GUI 回归设计及现有测试的限度

本轮只读已有测试，没有执行。`page_control_tests.cpp:235` 的 tracker 测试覆盖 latest-wins、松手所有权、两窗确认和 cancel。需要如实说明，`:268` 的“锁竞争注入”只是 `const bool presentationLockAvailable=false` 后手动构造 fallbackRequestedPair，没有真实竞争 presentationMutex，也没有调用生产 UpdateDragLocked/SetWindowPos/Window Submit；它证明纯 tracker 语义，不能证明系统无死锁。

待批准后可在既有 headless 项目内增加最小生产 seam，而非另造复制实现：

- **真实调度继续性**：实际 Scheduler 注册 Bar/左右 pair callbacks；注入一个 pair client 返回 Retry，其它正常，验证 Bar 后续 callbacks 仍执行、对应 pair 位保留、最终一侧恢复后完成双窗 mask。使用事件/有界 predicate，不用无限 sleep；无 HWND/Office。
- **真实拖动仲裁**：让生产提交仲裁入口接收可注入的呈现锁/两个 move 调用；测试另一线程持 P、输入最后一个 sample 后立刻 Up、没有后续 Move，生产代码确实 Request pair 且最终消费最新候选。两窗第二次失败时验证第一窗回滚、pending 未提前清除；不要在测试里自己写一个 fallbackRequestedPair 代替生产分支。
- **capture 重入边界**：生产 Down 小事务使用 capture 操作 seam，注入同步旧 Cancel。断言 capture 调用发生时 T 可被进入；旧 pair tracker 清退后才建立新 tracker、long-press 根据真实 capture 结果决定、Down 立即动作不重复、捕获失败/门禁变更不会留下新 tracking。无 GUI 只能验证生产拆分与注入顺序；真实 USER32 触摸交错仍须另做 HWND 验证。
- **局部状态不是死锁**：直接消费生产 candidate/clamp/collision 决策，覆盖单 X、单 Y、对角、另一侧先碰撞、退回可行方向；确认拒绝无锁等待、下一条合法候选可继续。只有审批明确要求轴向滑动时才改变期望。
- **过渡与失败**：生产 ShouldContinue/ShouldLock 的既有 deadline 测试在 `:799`～`:837`；再串联配置失败→Retry→恢复最后 deadline，断言最终输入解锁、没有依赖 Bar 新输入。不要仅多写几个等价布尔断言声称覆盖真实 WndProc。

编译/测试入口继续复用现有 `InkeysRepo.sln Debug|ARM64` 与 headless `--no-window`，遵守 ARM64 MSBuild、PATH 规范化及至少 5 分钟 supervisor；本轮计划阶段未运行。无 GUI 模式不能证明 OS capture、真实 HWND z 序、D2D ULW 与设备驱动交互通过。

### 10. 需要的故障证据

最有价值的是“不动时是否还持续新收到 Move/Up/Down、哪个 surface、两个组是否同时显示、鼠标/触摸/笔、拖向哪个位置、放手重新拖能否恢复”，以及上述数值 trace。

如果怀疑锁：需要同一时刻 overlay owner 与唯一 UI3 render 的栈，并标明各自等待对象；如果 Bar 新点击/动画持续正常，则优先抓 PageControl 状态与每 client result 而不是凭卡住外观寻找全局死锁。不要通过本轮禁止的 GUI/Office 运行来补证。

## External References

核对时间 2026-10-18；只将下列微软原始文档用于正式推断，没有把第三方兼容实现当成 Windows 实测：

- [SetCapture](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture)：capture 属于当前线程的指定窗口、同一时刻只有一个；后台窗口的可接收范围有额外限制。文档本身不保证任意重复 Down 一定发生。
- [Mouse Input Overview / Mouse Capture](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-mouse-input#mouse-capture)：capture 改变时系统向失去 capture 的窗口发送 WM_CAPTURECHANGED，未取得 capture 时屏外 Up 可能收不到。
- [WM_CAPTURECHANGED](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-capturechanged)：发送给失去 capture 的窗口，经 WndProc 处理；自己 ReleaseCapture 也会收到。
- [SendMessageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagew)：同调用线程的窗口过程立即作为子程序执行；跨线程发送等待接收处理。
- [PostMessageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postmessagew)：向窗口所属线程队列投递后立即返回；用于区分本仓库光源通知与 capture 同步消息。

`UpdateLayeredWindowIndirect` 原始页面两次通过 web 打开不可达；没有从非微软实现推断它必然发送某条持锁相关消息。本报告只引用生产中真实 ULW 调用点，并将该系统内部交互保留为故障栈/实机待查。

## Caveats / Not Found

- 没有故障设备 dump、线程栈、版本安装包、输入源或 PageControl 状态日志；不能确认用户局部停住的唯一根因，也不能宣称复现/已修复。
- 没有发现当前已接受单次普通拖动必然形成 D/S/P/T 互等环；发现的 SetCapture 自等必须满足另一个 PageControl 已有 capture 且当前有效 Down 抵达等前提。
- 条件性 capture 风险若成立的影响范围大于当前局部反馈，须分别说明，不能用它提高本轮结论的确定性。
- 已修复旧呈现锁环、transition idle 问题的当前防护在两种候选反馈版本均已纳入；历史源码事实由主会话只读 Git 调查提供，不是本研究者重跑 Git 得出。
- 未修改任何生产文件、测试、工程配置或规范；未构建或执行 headless/GUI/Office；该报告仅保存于当前任务 research/。
