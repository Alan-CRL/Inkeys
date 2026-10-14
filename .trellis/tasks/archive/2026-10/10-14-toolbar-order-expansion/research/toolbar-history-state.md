# Research: 主栏 Undo/Redo 历史状态与统一表现更新

- Query: 用户报告组件添加覆盖分离式定格尺寸，并要求主栏所有尺寸/显隐从状态统一派生。本独立研究只调查 Recall/Redo 历史保护、计时器、锁、布局动画与真实无窗口回归。
- Scope: internal
- Date: 2026-10-14
- Baseline: 调度提示指定 feature/mode、HEAD 2dd3b344；本研究不执行 Git 操作或构建。

## Findings

### 文件与相关规范

| 文件 | 用途 |
| --- | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.RecallChrome.cppm` | 生产纯策略，点击保护、实时可用性、待调整布局与精简选择门禁 |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | 按钮状态更新、历史事件、墙钟到期及当前第二套尺寸/显隐投影 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | 真实 WM_TIMER 唤醒、timer 装卸、落笔关闭历史面板 |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp/.cppm` | 全局 Bar、统一 UpdateRendering、当前页/工作区状态、历史锁与 deadline |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 帧快照、布局扫描、目标批次、历史到期结算及动画推进 |
| `Inkeys/Inkeys/UI/Bar/Bar.RecallAttribute.cpp` | 非主栏历史弹窗；同 view 决定命令身份、主栏锚点与可见门禁 |
| `Inkeys/IdtState.cpp` | Draw3 runtime 快照向 Bar 发布内容/历史与监控线程 |
| `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp` | 已有真实生产 Bar 的 no-HWND CLI 回归，可直接延续 |
| `InkeysHeadlessTests/recall_attribute_tests.cpp` | 策略/视觉 helper 及生产源接线断言 |
| `.trellis/spec/native-desktop/rendering-and-ui.md:57` | Undo/Redo 属性与 3 秒布局保护合同 |
| `.trellis/spec/native-desktop/cpp-conventions.md:134` | StateMode 同代值快照、锁外副作用与过期意图合同 |
| `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:422` | 分离/合并、精简选择、Pierce 稳定列表、统一 Disable 合同 |

### 确认事实：现行历史保护是 3 秒，计时器自身只负责唤醒

`Bar.RecallChrome.cppm:8` 为 `RecallChromeIdleSeconds = 3.0`。用户口述“5s 计时器”不能作为更改产品时间的依据；第三鼠标光源另有 5 秒 grace，非 Recall 保护。保持现有 3 秒语义，PRD 应据此表述。

`Bar.Interaction.cpp:600–610` 的真实 `WM_TIMER/BarRecallChromeIdleTimerId` 分支：

1. 调 `PollRecallChromeIdleTimer(retryMs)`；已到期仅 `UpdateRendering(false)` 唤醒。
2. 未到期按剩余毫秒重装 `SetTimer`；无 pending 或不 armed 则 `KillTimer`。
3. 不直接读写按钮 hide/size，也不立刻消费 IdleElapsed。

`Bar.Button.cpp:1653–1676` 的 Poll 与 deadline 使用 `steady_clock`。deadline 早到回调会返回 retry，晚到的旧 timer 仍对最新 deadline 检查，不能把已有保护提前结束。动画 dt 被明确忽略；主栏 idle 的动画时钟暂停，恢复首帧 dt 可能为 0。

`Bar.Interaction.cpp:1171–1182` 装卸 timer 时先检查 floating_window/IsWindow；无 HWND 的 CLI 因而可调用这些真实入口，不创建窗口。实际系统消息投递不在无窗口测试覆盖范围内。

### 现存第二写入口与生命周期

`Bar.Button.cpp:899–909` 的 `StateUpdate(stateMode)` 依次 CalcState、PresetHoming、Draw/Whiteboard/Eraser/Geometry 样式更新。`PresetHoming(stateMode)` 已持有调用方取好的工具快照，但在 `:1465` 又调用 `ApplyRecallChromeVisuals()`。

独立投影 `ApplyRecallChromeVisualsLocked`（`:1552–1571`）在 recallChromeMutex 中：

- 调 `ResolveRecallChrome(owner.recallChrome, IsCompactSelectionBar())`；
- 直接写 Undo/Redo `.size` 与 `.hide`；
- 写 `barState.recallAttribute`。

`IsCompactSelectionBar`（`:1514–1518`）另取 `GetStateModeSnapshot()`、WhiteboardActive 与 CurrentPageHasContent，未使用 StateUpdate 的既有工具快照。跨事件或跨阶段可能将不同代工具/内容门禁拼到同一更新中；需要统一输入与统一最终投影，不需要改变纯策略。

| 入口（Bar.Button.cpp 行号） | 数据更新 | 当前投影与通知 |
| --- | --- | --- |
| `ApplyRecallAvailabilityLocked:1521` | Availability/pendingLayout，重装或取消 deadline/timer | 自身不投影 |
| `RefreshRecallAvailabilityLocked:1539` | 从真实 ProductRunning/ProductRuntimeSnapshot 读取 availability | 无 Host 时 false/false；可能更新 pending/期限 |
| `ApplyRecallCommandResultLocked:1573` | Accepted/拒绝结果及点击前事实，重装期限 | 锁内直接投影 |
| `NoteRecallHistoryAvailability:1638` | 去重后的事实与 pending | 锁内投影，锁外 `UpdateRendering(false)` |
| `ApplyRecallChromeVisuals:1624` | 还会 refresh availability | 锁内投影 |
| `RecallChromeView:1631` | 还会 refresh availability | 返回 Resolve；并非纯 getter，读取也可能重装期限 |
| `NoteRecallPopupDismissed:1705` | Dismiss，只抑制 popup | 锁内投影；自身无 render request |
| `ClearRecallForDrawing:1716` | SuppressPopup，保留 hold/期限 | 锁内投影，锁外 reset pointer |
| `SettleRecallChromeForWorkspace:1729` | ReturnToDesktop，保留 hold/期限 | 同上 |
| `AdvanceRecallChromeIdle:1741` | refresh 后检查墙钟；IdleElapsed 清 hold/armed | 锁内直接投影与 disarm；锁外 reset pointer；返回 changed |
| `Load:1387` | 配置加载覆写基础值 | 单独再套 Recall 投影，未统一所有按钮 |

本研究未重复全按钮写入库存与 Load 根因研究；另一研究负责该范围。

### 必须保留的历史策略

`Bar.RecallChrome.cppm:65–108`：实时 availability 只影响动作可用性/颜色与 pending，不立刻替代锁存 layout。静止目标为两者可用时上 Redo/下 Undo 两枚 2×1；仅 Redo 可用时 2×2 Redo；无 Redo 时 2×2 Undo。

首次主栏命令点击将所点命令设成 2×2 与 hold；之后同一展开交互连续点击不得反转主栏/弹窗身份，只延长期限。没有点击时，从 pending 目标变化开始等 3 秒；同目标逐帧读取不重启，目标回到当前布局可取消无 hold 的 pending。

`ApplyRecallChromeCommandResult:119–137` 与 `EngageRecallCommand:1587–1621` 保存点击前 availability。Draw3 Publish 后可能同步耗尽末步，Accepted 必须仍认可此次反馈。禁用/失败不发命令或成功反馈；已有 pending 时延长保护，但不新开 popup。

`ResolveRecallChrome:139–152` 的 compactSelection 只覆盖 popup 与两个主栏按钮显隐，不能丢 hold/deadline/layout；空页 Selection 到期仍隐藏。Whiteboard 的 Select 是移动工具，不能使用桌面精简门禁；非空页 Selection 和 ClickThrough 维持完整栏历史规则。

`Bar.RecallAttribute.cpp:121–137` 同一 view 选择当前 2×2 主栏 Undo 或 Redo，且按该锚点 IsVisible 门禁关闭；不要恢复旧“检查始终隐藏 Undo 导致 popup 错关”问题。属性弹窗是独立实体，不属于此次主栏按钮单写入口范围，仍推进自身 SVG/文字/颜色动画。

### 线程、锁和状态发布

- `UpdateRendering:Bar.Main.cpp:280–305` 用静态 mutex 串行状态更新与有限动作封口/Notify/RenderPipeline Request；updateState=true 时先取一个 GetStateModeSnapshot，进入 StateUpdate。
- 当前状态更新锁序为 UpdateRendering mutex → StateMode 快照锁（短暂）→ recallChromeMutex（PresetHoming 调投影）；历史 helper 内还会取 StateMode 与 Draw3 Runtime 快照。
- 当前大多事件在释放 recallChromeMutex 后才调用 UpdateRendering(false)，这是必须保留的边界。不要在 recallChromeMutex 内新增 UpdateRendering(true) 或统一入口的重入调用，否则与 UpdateRendering → Recall 的原锁序构成反锁。
- `EngageRecallCommand` 在 recall lock 内取点击前事实，解锁后 PublishProductCommand，再在锁内提交结果；禁止为了统一投影把整个 Publish 包在 recall 锁内。
- `IdtState.cpp:516–519` 用同一 Draw3 runtime 快照先 SetCurrentPageHasContent，再 NoteRecallHistoryAvailability。`:1742–1774` 监控线程等待 runtimeRevision/250ms 后协调；工具 setter SyncDraw3State 也会调用协调，不只 UI 线程。
- `SetCurrentPageHasContent:Bar.Main.cpp:353–358`、工具 RefreshToolState、PPT/白板切换会进入统一 update。内容真值与 availability 来自同一源快照，但当前是两次发布；本次应避免第二次历史发布写回另一套有效尺寸。
- `contentStateUpdatesReady` 是早期 CLI/初始化门禁；统一刷新在预设尚未初始化时必须安全。`Bar.Initialization.cpp:165–172` 的真实启动为先内容、再 PresetInitialization/Load/StateUpdate，维持此顺序。
- 属性面板 `Close:Bar.RecallAttribute.cpp:311` 会交换 barState.recallAttribute 并调用 NoteRecallPopupDismissed，PresetHoming 在 fold 时可调用 Close。统一 projection 不能在 recall mutex 下递归 Close 再次取同锁。

### 帧顺序与动画风险：结算必须早于布局目标扫描

当前 `Bar.RenderLoop.cpp`：

1. `WakeAndSnapshot:1143–1159` 锁存一帧工具与颜色。
2. `SubmitTargetsAndLayout:13721` 扫描可见按钮和 size，计算稳定目标宽度，决定展开侧、布局批次并提交尺寸/坐标/透明度目标。
3. `AdvanceAnimationsAndDeriveLayout:13744` 推进所有动画。
4. 在上述阶段尾部 `:8099` 才调用 `AdvanceRecallChromeIdle`，到期时该方法重写主栏 hide/size。

到期帧的目标可能已按旧状态提交，却在后面变成新语义；次帧才反映布局变化。不能以“timer 只唤醒”推断整个到期链已经符合单一派生入口。

最小调整建议：先处理 Recall due 的纯业务状态，再捕获/统一应用主栏 presentation，最后扫描宽度/判侧/提交所有动画目标。渲染入口使用本帧 StateMode 已取快照，一次读取同批 Whiteboard/内容/Freeze/history view。退出锁后刷新，不在 frame 的动画末尾改有效尺寸。返回 changed 的语义仍用于 Recall dirty/demand；不要丢到期后的唯一唤醒帧。

`mainBarLayoutChange:2448` 当前只比较目标总宽度和 IconOrder；Compact(twoOne+twoOne) 与 Undo/Redo(twoTwo 单枚) 常有相同 70 DIP 总宽，故尺寸/显隐变化不触发布局批次判断。统一实现若加入 presentation revision/fingerprint，应只把实际网格尺寸/可见性/list identity 等布局语义变化纳入，不把 hover、普通同值刷新或持续动画当成新布局。否则会不停重开 timeline。

保留 `:2464–2502` 的既有换边中点、早期加入/后半程重开批次，以及 `:3051–3070` 的隐藏 Undo/Redo/Pierce 锚点；只投影有效 size/hide，不 SetDirect 覆盖当前动画或改写 x/y/w/h。重复同状态刷新应保持 current/target/进度连续。

### 最小实现方向（供 implement，非已实施）

1. 保留 RecallChromeState、steady_clock deadline、纯策略、Accepted 点击前事实；业务入口只修改状态/期限与发起通知。
2. 删除所有事件、timer 结算与 Load 末尾的独立 Undo/Redo size/hide 写入；使其统一落在主栏 presentation 计算/应用路径。
3. StateUpdate 或等价统一入口一次捕获工具/内容/工作区/Freeze/history 输入。Recall projection 接收该快照的 compactSelection 与 RecallChromeView，不重新取 live 工具。
4. RecallChromeView 的历史刷新副作用需明确定位；为渲染布局和 popup 提供已锁存 view，避免两者在同一帧又从实时 Draw3 刷出新 pending/布局。不要为了“纯 getter”取消真实 availability 刷新而造成初次状态失真。
5. 每个事件释放 Recall 锁后才调用统一 update/Request。拒绝/禁用点击延期也应保持 timer，但未变的布局不应产生新 batch。
6. 到期结算移至布局目标计算前。Fold 关闭 popup 可单独写 popup 抑制状态，但不能触碰主栏有效 size/hide；保留目前不能提前清 hold 的合同。

最小安全接线：历史/命令/关闭/落笔/工作区事件保持锁内只更新 RecallState/deadline，解锁后沿原 `UpdateRendering(false)` 唤醒。不要普遍改成 true；NoteHistory 从 `IdtState::ReconcileDraw3PresentationState` 持 `draw3PresentationMutex` 调用，新增整套同步 StateUpdate 扩大回调重入与取锁范围。渲染在锁存本帧工具之后、目标宽度之前执行 due 状态结算和统一 projection；AdvanceIdle 不重入 UpdateRendering，返回 changed 由当前 frame 纳入 dirty/demand。若还有同步 projection 调用方，与 UpdateRendering 的既有串行边界协调，不新增反向锁序。

灰色历史按钮必须保留可点击延期：`Bar.Interaction.cpp:3099/3121/3778/3943` 会将 `BarWidgetState::Disable` 作为命中/动作拒绝门；Recall 目前通过 `Bar.RenderLoop.cpp:2558` 的 commandEnabled 产生禁用颜色透明度，而不一律把状态设成 Disable。统一按钮表现不能把 Undo/Redo 灰色误投影成真正 Disable，否则 EngageRecallCommand 的灰色点击延期已有保护逻辑不可达。Clean/Freeze 的真实 Disable 继续原行为，历史按钮仍以 commandEnabled 拒绝发布而允许有限反馈。
7. 配置/构造基础尺寸与有效尺寸区分，列表 Load 后统一派生全部按钮，不能保留“只给 Recall 再套一层”的特殊修补。

### 真正生产无窗口回归方案

已有 `Inkeys.exe --bar-eraser-offscreen-test` 在 `IdtMain.cpp:1197–1198` 配置/单实例/主窗口初始化前分派，使用真实全局 barUISet/预设/Load/UpdateRendering。延续 `Bar.EraserAttribute.Test.cpp:537–577` 真实历史门禁测试即可，不另起 GUI harness。

- 种入受锁保护的 RecallState/hold，调用真实 ArmRecallChromeIdleDeadline；不睡 3 秒，在 fixture 中将真实 deadline 移到 now 前/后，然后调用真实 PollRecallChromeIdleTimer/AdvanceRecallChromeIdle。
- 覆盖提前 timer → retry>0，deadline 未到时即使传大 dt 也不结算；重装新 deadline 后模拟旧唤醒仍不能提前结束；到期有 hold 与无 hold pending 均能 settle 并取消 armed。
- 真实 NoteRecallHistoryAvailability、ClearRecallForDrawing、NoteRecallPopupDismissed、SettleRecallChromeForWorkspace 后，调用新的统一生产 presentation/update 边界，验证单写入口和没有临时 extra column；不把纯 Resolve 输出当集成结果。
- 在 Pen/Shape/Eraser、非空 Selection、空 Selection、ClickThrough、白板/PPT 之间切换，跨 due/Load/组件增减断言 Undo/Redo 合法尺寸/显隐；空 Selection 必须始终不可命中，并保留 hold/deadline 直到真到期。
- 在 Compact↔单枚 2×2 的相同宽度转换中，使用真实布局批次/动画 helper 断言旧 current 保留、零 dt 不跳、同状态连续更新不重启、终态正确，左右排序不能颠倒 Redo/Undo。同样确认新 presentation revision 在尺寸改变但 width 不变时仍失效。
- 扩展生产 frame 接线检查，确认 due 结算与统一 presentation 在宽度扫描前，末尾 Advance 不再写主栏 hide/size；此源码断言只是静态补充，不能代替上述真实调用。
- 既有 `InkeysHeadlessTests/recall_attribute_tests.cpp:404–422` 检查旧 ApplyRecallChromeVisuals/PresetHoming 字符串。本次要按新单写合同更新断言，保留 pure 策略3秒/Accepted/opacity回归。

无窗口宿主边界：该 Bar CLI 未启动 Draw3 Host；`RefreshRecallAvailabilityLocked` 会回 false/false。可以验证真实 Bar state/timer/update/可见/动画入口，但注入 chrome availability 不证明真实 Accepted PublishProductCommand，也不证明系统 WM_TIMER 投递。现有策略测试验证 Accepted 语义；完整 solution 验证接口接线。不得为覆盖真命令而启动未授权的 Draw3 HWND/GUI hidden fixture。真实交互与 timer 消息视觉列为人工验收。

## External References

无。本研究只使用当前仓库实际生产代码、既有 Trellis 合同与测试，不需要外部 API 资料。

## Caveats / Not Found

- 未修改产品、规范、任务工件或任何非 research 文件；未构建、运行 GUI、Computer Use 或 Git 命令。
- 本文行号对应研究时源码，后续实施可能移动；角色只研究 Recall，不重复另一代理全主栏写入口库存。
- 未发现 Recall 5 秒 timer 或外部 detached/sleep 线程；现 production 为 WM_TIMER 3 秒墙钟保护。
- frame 顺序问题是直接源码确认；用户组件覆盖的确定根因由全按钮/Load 研究负责，不能将 Recall 时序风险当作已实测该根因。
- 是否需要全帧原子 presentation 快照或小规模统一入口由 implement 决定，本文不要求泛化新 UI 架构或修改属性/分页栏。