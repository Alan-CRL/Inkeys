# Work 1 第二单元独立审查：状态快照

审查范围：2026-09-27 当前工作区的 `IdtState.h/.cpp`、`IdtConfiguration.cpp`、`Bar.Layout.cppm`、`Bar.RenderLoop.cpp`、`Bar.Interaction.cpp`、`Bar.Button.cpp`、`Bar.EraserAttribute.cpp`、`Bar.Main.cpp`、`MouseHook.cpp` 的实际 diff 与相关调用链。只读审查；未编译、未运行 GUI 或性能采样。以下行号对应审查时工作区。

## 需要修正

### SR-1 [P1] Fine Dial 空轮询先推进，后刷新模式快照

- 证据：`Bar.Interaction.cpp:2222–2229` 在没有消息时先调用 `AdvanceThicknessFineDialPhysics()`；`Run():5403–5404` 只有 `PollInteractionMessage()` 返回后才刷新 session 的 `stateMode`。前者在 `1653–1669` 用 session 快照判断工具/量程，达到 settle 条件时在 `1730–1740` 调用 `CommitThicknessFineDialSelection()`；后者在 `1542–1559` 以当前 getter 比较，再调用 `SetPenWidth()`。`SetPenWidth()` 在 `IdtState.cpp:774–795` 会按**提交时**的全局工具写对应槽位。
- 触发：Fine Dial 惯性或回弹中，PPT 工作线程切换笔型/模式，但 Bar 队列此时没有消息。空轮询依旧按上一次 Pen 快照推进，可能把旧候选粗细提交到新笔型或形状，直到下一轮才发现状态变化。若新笔型仍有相同量程，仅刷新快照也不够，应验证手势开始时笔型/工具身份。
- 最小修正：在无消息的物理推进前刷新一次快照；给 Fine Dial 候选保存开始时工具身份/修订，在提交前用当前快照核对，变化则取消候选。加入生产逻辑回归：惯性中异步切软/硬/荧光笔、Laser、形状、Selection；断言新工具粗细不受旧候选影响。

### SR-2 [P2] Draw 按钮一次样式更新混用两代快照

- 证据：`Bar.Button.cpp:850–857` 先取 `stateMode` 快照，再由 `IsLaserPenSelected()` 二次取锁读取 `laserActive` (`IdtState.cpp:703–705`)。PPT 或交互线程在两次取样之间切换笔型时，`selected`、`highlighter`、`hardPen` 与 `laser` 可组合出不存在的状态，并被 `drawButtonStyleKey` 锁存。
- 影响：按钮图标/标签与实际工具短时不一致；若后续没有更新唤醒，错图标可能继续保留。`UpdateDrawButtonStyle()` 的静态 mutex 只保护样式写入，不使两次状态读取同代。
- 最小修正：此处直接用已有快照的 `stateMode.laserActive`。用交替 PPT/Bar 笔型切换覆盖 label/icon 与工具快照的一致性。

### SR-3 [P2] Eraser 属性面板独立取样，破坏 Bar 单帧状态合同

- 证据：`Bar.RenderLoop.cpp:996–1001` 在 `WakeAndSnapshot` 固定帧模式；`SubmitTargetsAndLayout`/`AdvanceAnimationsAndDeriveLayout`/`CalculateDirtyAndDrawPresent` 均消费 `state.stateModeSnapshot` (`1217`、`5336`、`7930`)。但 `AdvanceAnimationsAndDeriveLayout` 在 `7800–7805` 调用 `BarEraserAttributePanel::Advance()`，该函数于 `Bar.EraserAttribute.cpp:177` 又调用 `GetStateModeSnapshot()` 决定是否 `Close(owner)`。
- 触发与影响：模式在帧快照之后、Eraser Advance 之前切换，主栏其余部分仍按旧帧绘制，而 Eraser 面板按新模式关闭/保留，可能出现同帧面板、按钮和工具动画不一致。
- 最小修正：将当前帧的模式（或完整快照）作为 `Advance` 参数传入；不要在该渲染阶段重读全局模式。补并发交错的确定性状态/布局测试，真实逐帧视觉仍需 GUI 验证。

### SR-4 [P2] 交互 session 的部分快照派生仍无同代保证

- 证据：`Bar.Interaction.cpp:5525` 的 session 快照在消息入口刷新，但 `IsIndependentHoverAllowed()` 的 `1822–1835` 同时读取 session 的 `stateMode.Pen.ModeSelect` 与无参 `IsLaserThicknessPresetMode()`、`PenModeUsesThicknessPresets()`、`GetPenWidth()`、`GetBarLaserThicknessPresetDip()`；这些无参 helper 分别取新的全局快照 (`Bar.Layout.cppm:305–384`, `IdtState.cpp:844–846`)。`HandleDrawAttributePointerStage()` 在 `5032–5087` 也把独立快照得出的 preset 分支与 session 笔型合用。
- 影响：并发 PPT 笔型/激光切换可能让 hover、预设选中和提交值依据不同工具代际。锁消除了单字段读取的数据竞争，但并未使一次交互判定成为一致事务。
- 最小修正：需要同代判定的 Layout helper 调用统一传 session 快照；`GetPenWidth(stateMode)` 等使用已有重载。对于跨嵌套等待的长手势，另外在提交前核对开始时工具身份，不能只依赖等待时自动刷新。

## 已核实与剩余边界

- 当前 `IdtState.cpp` 新增的快照重载在 `ApplyPenModeTransitionLocked()`、`PublishDraw3State()` 和 `ChangeStateModeToShape()` 中使用；这些持 `stateModeTransitionMutex` 的路径未再调用会二次取锁的无参 getter。`SetPenWidth()`/`SetPenColor()` 在锁内选择并写槽位，解锁后才调用 `SetMemory()`；`SetMemory()` (`IdtConfiguration.cpp:1090`) 先复制快照，文件 I/O 在锁外。`ChangeStateModeToPptAnnotation()` 与 `ChangeStateModeToSelectionIfRevision()` 仍在锁内核对 expected revision，未被无条件 setter 替换。
- 生产工程编译 `IdtConfiguration.cpp`，其中 `GetMemory():1046,1057,1067,1078` 仍裸写全局 `stateMode`。全仓找到的唯一调用在工程以 `None` 登记的旧 `IdtDrawpad.cpp:2174`，所以当前生产路径不可达；不能将这个编译函数称为已同步写入口。若今后恢复调用，必须在解析完候选后以同一锁/正式命令提交，不得在文件 I/O 期间持模式锁。
- `Bar.RenderLoop.cpp` 的直接 `stateMode` 字段使用已由局部 `state.stateModeSnapshot` 遮蔽；Layout 的带快照重载也避免在帧内再次取锁。`MouseHook`、`Bar.Main` 与主要按钮读取已换为快照。`Bar.Button.cpp:853` 和 Eraser/Interaction 的同代缺口如上。普通 `GetPenWidth/Color/IsLaser*` 无参 getter 没有发现锁内递归调用；它们各自安全复制，但组合使用须同代检查。
- 本次未做编译、线程交错、GUI 或性能验证；`git diff --check` 当时退出码为 0。结论限于源码审查，不可记为本单元自动验证通过。尤其需要验证新增逐消息/逐帧锁调用的尾延迟，不能由静态审查推断无性能退化。

## 2026-09-27 修正后复审（最新结论）

仍仅做源码和 diff 审查；未构建、未运行 GUI。前文 SR-1 至 SR-4 是首次审查时的证据，以下状态以修正后的当前代码为准。

| 原问题 | 当前结论 | 证据与边界 |
| --- | --- | --- |
| SR-1 惯性空轮询旧快照 | 已修复待验证，但仍有下述 R1 | `Bar.Interaction.cpp:1658–1667` 物理推进前刷新 session 快照并核对修订；`1543–1565` 最终使用 `SetPenWidthIfRevision`。实际提交和取消交错未自动验证。 |
| SR-2 Draw 按钮跨代 laser | 源码修正 | `Bar.Button.cpp:850–857` 的模式、笔型与 laser 均来自同一快照。 |
| SR-3 Eraser 单帧跨代 | 生产渲染路径源码修正 | `Bar.RenderLoop.cpp:7800–7807` 向 `Advance` 显式传入本帧 `eraserSelected`。`Bar.Main.cppm:79–83` 的 optional 默认仅供现有 `Bar.EraserAttribute.Test.cpp` 调用；全仓检索未见另一个生产调用，故默认回退不是现有生产帧的跨代入口。 |
| SR-4 交互 hover/preset 混代 | 部分修正，见 R2 | hover 的 `IsLaserThicknessPresetMode`、`PenModeUsesThicknessPresets`、`GetPenWidth`、Laser DIP 已传 session 快照 (`Bar.Interaction.cpp:1835–1846`)；preset 分支也传快照 (`5048–5050`)。按下 token 与该快照仍非同代。 |

### R1 [P1] 新 Fine Dial 手势可继承旧代惯性候选并赋予新 token

- `BeginThicknessFineDialDrag()` 在 `Bar.Interaction.cpp:1573` 先把 `thicknessFineDialModeRevision` 设为传入的新修订，随后在 `1574–1583` 只要旧阶段是 Inertia/Settling 且候选仍 active，就沿用旧的 `thicknessFineDialVisualWidth`。`HandleThicknessAndFineDialPointerStage()` 在 `3577–3583` 捕获当前新状态/修订，但在 Fine Dial popup return 分支 `3615–3628` 先从共享旧候选取 initialWidth，再于 `4193–4195` 用新修订调用 `BeginThicknessFineDialDrag`。
- 交错：旧笔型 Fine Dial 正在惯性中；PPT 切到新笔型；下一次空轮询取消旧候选**之前**，用户按下 Fine Dial 浮窗。新的 gestureRevision 会包裹旧视觉候选，之后 `SetPenWidthIfRevision` 可能把旧宽度写到新笔型。即使两笔型量程相同也成立；模式修订的提交校验不会阻止，因为 token 已在手势开始处被改为新代。
- 最小修正：在继承候选之前比较旧 `thicknessFineDialModeRevision` 与新 `modeRevision`；不同时取消旧候选并从 `gestureState.state` 的当前宽度重新锚定。`BeginThicknessFineDialDrag` 不应先覆盖旧 token 再判断能否继承。加入“惯性→PPT 切笔型→无物理 tick→立即重新抓浮窗→抬手”回归，断言旧候选未提交到新笔型。

### R2 [P1] 预设按钮的 revision 与用于计算预设的状态并非同锁快照

- `Bar.Interaction.cpp:5048–5050` 用 session 的 `stateMode` 算 laser/普通 preset 分支；在 `5064` 才单独读 `StateModeTransitionRevision()`。若 PPT 在两者之间切工具，旧模式和新 revision 被组合；在 `5103–5109` 抬手使用旧分支及旧 `stateMode.Pen.ModeSelect` 算值，却把新 revision 交给 `SetPenWidthIfRevision`。这可把旧 Laser DIP 或普通笔预设写入新工具。抬手 `5093` 的版本比较不能发现按下前已经发生的切换。
- 最小修正：按下时一次调用 `GetStateModeVersionedSnapshot()`，用其 `state` 同时计算 laser/普通分支、笔型和预设值，使用其 `revision` 做最终条件提交；若按下时取得的状态已不适合当前面板，则取消该点击。需要 PPT 与 preset Down 边界交错的回归。

### 其他复核

- `GetStateModeVersionedSnapshot()` 在 `IdtState.cpp:703–707` 于同一 `stateModeTransitionMutex` 下复制状态及 revision；`SetPenWidthIfRevision()` 的比较和槽位写入在该锁内 (`IdtState.cpp:776–810`)，之后解锁才 `SetMemory()` 和 `PublishDraw3State()`。未发现此次新 API 引入递归锁或锁内磁盘 I/O。`SetMemory()` 仍通过独立快照写配置，所以在解锁后有别的工具/参数变更时，持久化的是之后的完整状态；这符合现有低频内存快照边界，但不保证每次 setter 与单个文件写一一对应。
- `Bar.Interaction.cpp:1550–1562` 的 `StateModeTransitionRevision()` 与 `GetPenWidth()` 分别取样，仅决定是否省略一次提交；最终 `SetPenWidthIfRevision()` 仍在锁内复验 revision，故这处未确认会把旧工具宽度写入新工具。`Bar.Interaction.cpp:4070` 的无参宽度读取在 Fine Dial popup 交接后可能读到新工具；它与 R1 的候选继承风险一起复验，不单独定性为已确认数据损坏。
- `git diff --check` 复审时退出码 0。当前两项 P1 应修正并做交错回归，不能把本单元标为已验证通过。

## 2026-09-27 第二轮修正复审（最新结论）

本轮核对最新 `Bar.Interaction.cpp`、`IdtState` API、全部两处 `BeginThicknessFineDialDrag()` 调用及 preset 按压退出路径；依旧只读，未构建/GUI。前一节的 R1/R2 是上轮状态，以下结论覆盖它们。

### R1 [P1] 仍未闭环：旧候选在进入 Begin 之前已复制到局部 `initialWidth`

- `BeginThicknessFineDialDrag()` 已在 `Bar.Interaction.cpp:1573–1588` **先比较旧 token**，跨代时取消旧候选、速度及 residual，然后才设置新 token；这修正了函数内部直接继承旧 `thicknessFineDialVisualWidth` 的路径。
- 但 Fine Dial popup return 的按下路径在 `3615–3634` 已先把旧共享 `thicknessFineDialVisualWidth` 复制到局部 `initialWidth/finalWidth/lastCandidateWidth`，未核旧 token。之后 `4195–4199` 把这个旧 `initialWidth` 以**按值**参数传给 `BeginThicknessFineDialDrag()`。函数内部的 `CancelThicknessFineDialSelection()` 清掉共享候选，却不改变传入的旧 `startValue`；接着 `1592–1607` 又以这个值发布带新 token 的候选。若后续轻点提交或拖动，旧笔型候选仍能写入新笔型。
- 最小修正：在 `3615` 读取共享旧候选前先比较 `thicknessFineDialModeRevision == gestureRevision`；跨代时取消候选并保留 `GetPenWidth(gestureState.state)` 初始化值。防御性地让 `BeginThicknessFineDialDrag` 跨代分支重置传入 `startValue`，或返回失败让调用者重新锚定，避免另一调用点以后重引入问题。需要相同“惯性→PPT 切笔型→无物理 tick→抓 popup→抬手”回归。

### R2 [P1] 源码修正待验证：preset 按下状态与修订已同代

- `Bar.Interaction.cpp:5068–5076` 在 Down 用一次 `GetStateModeVersionedSnapshot()` 得到工具、laser 分支和 revision，并检查当前模式适合该按钮；`5114–5121` 用 `pressState.state` 计算预设宽度，`SetPenWidthIfRevision` 在锁内复验修订。Release 的显式版本检查在 `5105`，即便检查后发生切换，最终 setter 仍可拒绝。
- 状态/控制流：按下快照已不适用时 `continue` 发生在设置 `*button.pressed=true` 之前 (`5076` 对 `5089`)，不会遗留按压态；`continueFlag=false` 已设置，会消费这一 stale UI Down，避免误触其他操作。等待中修订改变时 `5105–5108` 跳出捕获循环，循环后 `5169–5171` 清理按压态并请求重绘；未见此路径遗留 pressed flag。

### 其余路径

- `BeginThicknessFineDialDrag()` 当前只有 `4048–4049` 与 `4198–4199` 两个调用，都传 gesture 开始时同锁取得的 `gestureRevision`；Slider 的末端提交 `4796/4827` 也使用同一 revision 的条件 setter。`CancelThicknessFineDialSelection()` 与 `CommitThicknessFineDialSelection()` 清除候选 active、`thicknessSliderCandidateWidth` 和 token (`1521–1534`, `1542–1566`)。`AdvanceThicknessFineDialPhysics()` 在推进前检查 token (`1662–1670`)。
- `SetPenWidthIfRevision` 锁内检查，`SetMemory()` 保持锁外；Eraser `Advance` 生产路径显式传本帧模式，optional 默认只被工程内单线程测试调用。未发现这轮改动新引入递归锁或另一个生产 Eraser 跨代调用。
- 当前仍有 R1 的 P1，不能把状态快照单元记为已验证通过。`git diff --check` 本轮退出码 0；此审查未给出构建或运行结论。

## 2026-09-27 第三轮候选溯源（最新结论）

最新代码在 `Bar.Interaction.cpp:3618–3625` 给 popup return 的旧 `thicknessFineDialVisualWidth` 读取增加了 `thicknessFineDialModeRevision == gestureRevision`。该入口的跨代局部 `initialWidth` 现在保留 `GetStateModeVersionedSnapshot()` 的真实宽度；`BeginThicknessFineDialDrag()` 随后可取消旧惯性，前述 popup return 交错在源码层已修正待验证。

**R1 仍为 [P1]，还有另一处旧候选读取：** `Bar.Interaction.cpp:3784–3795` 在任何 `viewModeAtPress == FineDial` 时，无 token 检查便把共享 `thicknessFineDialVisualWidth` 复制到局部 `fineDialPressStartValue`。非 popup 的 Fine Dial 手势在 `4212–4214` 用此值调用 `ActivateFineDialDrag()`，最终进入 `BeginThicknessFineDialDrag()` (`4048–4049`)。旧笔型惯性→PPT 切新笔型→物理空轮询尚未取消→用户按住 Fine Dial 正常 Drag Zone，旧候选仍能通过按值 `startValue` 进入新修订并被发布。`Begin` 内取消旧共享候选无法清除已复制的局部值。

最小修正是给 `fineDialPressStartValue` 的候选读取同样加 `thicknessFineDialModeRevision == gestureRevision`；跨代沿用 `initialWidth`。更稳妥的入口约束是在 Down 捕获 versioned snapshot 后，先取消旧 token 的候选，再生成 popup/普通 Fine Dial 的所有局部初值。`thicknessSliderCandidateWidth` 的其他读取已检查：`4055–4058` 与 `4205–4209` 均发生在 `BeginThicknessFineDialDrag()` 发布新候选之后；`CommitThicknessFineDialSelection()` 的 `1543–1562` 最终由 token 比较加锁内条件 setter 防护。没有发现第三条旧候选→新 token 的独立路径。

预设 invalid Down 在按压标志设置前 `continue`，等待时版本变化的 `break` 后统一清 pressed；这些控制流未见残留按压。Eraser optional 默认没有另一个生产调用。`git diff --check` 本轮退出码 0；未构建或运行。

## 2026-09-27 最后一处修正复核（当前结论）

**R1 的旧候选→新 token 错写路径已在源码层关闭，状态为已修复待验证。** `Bar.Interaction.cpp:3618–3625` 的 popup return 与 `3787–3793` 的普通 Fine Dial Drag Zone 均要求 `thicknessFineDialModeRevision == gestureRevision` 才复制旧 `thicknessFineDialVisualWidth`；否则两个局部起点保留 `GetStateModeVersionedSnapshot()` 在 `3581–3587` 同锁取得的新工具真实宽度。两个 `BeginThicknessFineDialDrag()` 调用（`4050` 与 `4200`）均传同一个 `gestureRevision`。`Begin` 在 `1573–1588` 先比旧 token、跨代取消候选/残余速度，再设置新 token 和发布新候选。随后在 `4055–4058`、`4205–4208` 读取的 visual/candidate 已是 `Begin` 本次发布的值，不再是被取消的旧值。

若模式恰在 versioned snapshot 之后发生切换，手势仍持旧 `gestureRevision`；最终 `SetPenWidthIfRevision()` 在 `IdtState.cpp` 的模式锁内拒绝提交，物理推进也会因修订不符取消。当前审查未找到可让旧候选获得**新**修订并写入新工具的剩余路径。`git diff --check` 本轮退出码 0。该结论只是静态时序审查，指定交错回归及真实 UI 手感仍未验证；不能记为自动验证 PASS。

## 2026-09-27 最终 diff 复审（当前结论）

在最新工作区再次核对普通 Fine Dial Drag Zone (`Bar.Interaction.cpp:3787–3796`)、popup return (`3618–3635`)、两个 `BeginThicknessFineDialDrag` 调用 (`4050`, `4200`) 及其后读回 (`4055–4058`, `4205–4208`)：**R1、R2 均已在源码层修复，仍需交错运行验证**。两条入口只在 token 与 versioned snapshot 的 `gestureRevision` 相等时复制旧视觉候选；`Begin` 先取消跨代惯性，后设新 token；随后读回的是本次发布候选。最终宽度写入均经 `SetPenWidthIfRevision` 的模式锁条件检查。未发现新的旧候选跨代提交路径。

### Geometry 按压与业务语义

- `Bar.Interaction.cpp:3497–3506` 在 Geometry 命中并收到 Down 后取同锁 `geometryAtPress`，检查顶层 Shape；无效时 `continue` 的作用域是 `for (geometryButtons)`，发生在 `*button.pressed=true` 前，因此不残留按压态。`continueFlag=false` 已置位，过时的 UI Down 被消费，不落到 Draw Attribute 等后续处理。这是相对 H0 在状态已切走时仍可能响应旧面板的收紧，符合避免迟到输入修改新工具的目标。
- 有效 Down 的等待仍要求抬手落在同一 shape (`3507–3516`)，与 H0 一致；修订变化时 `3517–3518` 退出嵌套等待，之后 `3535–3538` 清 pressed、唤醒重绘并清理旧指针消息。Pointer 离开与等待关闭的行为仍沿 H0 既有路径；等待关闭直接返回 Shutdown 未在局部清 pressed 是原有行为，未由本次改动引入。
- 形状子型和粗细预设在抬手时使用 `SetShapeModeSelectIfRevision` / `SetPenWidthIfRevision` (`3523–3530`)。`IdtState.cpp:1043–1061` 对形状在模式锁内核 expected revision、更新子型和 revision，锁外调用 `SyncDraw3State()`；粗细在同一模式锁内核 revision、写当前 Shape 槽位，锁外 `SetMemory()` 和 `PublishDraw3State()`。重复选同形状只递增用户意图 revision，保持既有无 Draw3 重发语义。Geometry 关闭按钮仍只关闭面板；抬手前 revision 改变则取消，未改变正常点击语义。
- `SetShapeModeSelectIfRevision` 和 `SetPenWidthIfRevision` 都没有在锁内做文件 I/O、Window/GPU 调用。`GetStateModeVersionedSnapshot()` 的状态与 revision 同锁取得；本轮未发现新增递归取 `stateModeTransitionMutex` 或相反的锁顺序。

审查界限：主线程报告完整 Debug|ARM64 Solution 与 `InkeysHeadlessTests --no-window` 退出 0；本 reviewer 未重跑或检查其完整日志，不能由此推断上述罕见交错、实际 UI 手感或性能已通过。`git diff --check` 本轮退出码 0。SR-1–SR-4、R1–R2 的源码问题现在为“已修复待验证”；手势/PPT 并发交错、GUI 和尾延迟仍应留在验证账本。
