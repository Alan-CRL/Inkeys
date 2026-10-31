# Pen 工具事务入口独立审查（2026-09-27）

## 范围与结论

三次只读核对 `HEAD` 到工作区的 `Inkeys/IdtState.h/.cpp`、`Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` 和最新 `Bar.RenderLoop.cpp` 差异，并沿 PPT 条件接管、Bar 独立 interaction/render 线程、Draw3 bridge/Window Service 和 FineDial 调用链检查。未执行编译、GUI 或性能采样。当前版本在源码控制流上关闭了 R1 的普通路径和候选清除双读窗口；跨线程普通字段访问仍未同步，真实 UI 视觉表现仍待验证。前两轮发现及修正过程保留在下文，最终结论以“第三版复审”为准。

## R1 首次审查记录：笔型事务先于 FineDial 取消（普通路径已修复）

- **变更证据**：硬笔、软笔、荧光笔点击在 `Bar.Interaction.cpp:5264-5270, 5306-5312, 5347-5353` 先调用 `ChangeStateModeToPenTool`，成功后才 `ClosePenTypeMenu` 和 `CancelThicknessFineDialSelection`。`IdtState.cpp:930-935` 在调用返回前先写新 `Pen.ModeSelect`，再同步执行 `SyncDraw3State`；后者在 `IdtState.cpp:657-668` 包含 Window Service owner 提交、bridge 发布和 presentation reconcile。`HEAD` 中三个点击入口均先关闭菜单/取消 FineDial 候选，再改笔型并调用 `ChangeStateModeToPen`。
- **交错条件与结果**：FineDial 正有候选值（`Bar.Interaction.cpp:1503-1517` 会设置 `thicknessFineDialCandidateActive=true`），用户切换到另一个支持 FineDial 的笔型；Bar 渲染在独立线程运行（`Bar.Initialization.cpp:184` 创建 interactionThread，RenderPipeline 另持渲染线程）。若渲染帧落在新模式写入与候选取消之间，`Bar.RenderLoop.cpp:1555-1561` 因旧 candidate 仍 active 而取消量程过渡，然后 `:1599-1600` 把 `thicknessFineDialLastPenMode` 更新成新模式。随后 interactionThread 清除候选；下一帧模式已“不变”，`fineDialPenModeChanged` 为 false，因此 `:1563-1598` 不再启动旧/新量程动画。可见结果可能是 FineDial 刻度或粗细视觉跳变。旧顺序不会出现“新笔型 + 旧候选”组合。
- **最小修正方向**：由 `IdtState` 提供内部持 `stateModeTransitionMutex` 的只读 preflight 查询，供 Bar 预判点击是否会改变顶层模式或笔型；预判变化时，先按旧顺序关闭菜单、取消 FineDial，再调用事务入口。预判为相同笔型时仍必须调用事务入口，以保留本次新增的“重复点击递增 revision，拦截旧 PPT 回调”语义。事务入口仍须在自己的锁内作最终条件判断并发布 revision，不能使用预判值直接写状态，也不要让 Bar 持模式锁执行 UI 清理。
- **并发边界**：只读预判与最终事务之间可能有 PPT 条件接管或其他模式切换。预判变化而事务最终 no-op 会提前关闭 UI；预判 no-op 而事务最终变化，仍可能遗漏前置清理。因此两段预判只能缩小常规路径窗口，不能声称完全消除竞态。若要求严格消除，需设计不在模式锁内执行 `ClosePenTypeMenu` / `CancelThicknessFineDialSelection`（其中可能触发窗口消息）的方案，或使 RenderLoop 在候选未取消时保留待处理笔型过渡，不提前消费 `thicknessFineDialLastPenMode`。这类修正须分别验证快速反向、重复点击、PPT 迟到回调和 FineDial 候选取消。
- **建议验证**：使用可控线程屏障让 render 帧插在模式写入后、候选取消前，断言新量程过渡仍启动；然后在实际 UI 上切换软笔/硬笔/荧光笔，并覆盖 FineDial 惯性候选活动、重复点击和快速反向。当前 AGENTS 禁止未经用户授权的 GUI 启动，本审查未执行这些步骤。

## 第二版修正的独立复审（历史结论，第三版已继续修正）

- **普通路径：源码验证已修复，运行未验证**。`IdtState.cpp:929-934` 的 `PenToolSelectionWouldChange` 在 `stateModeTransitionMutex` 内只读，`ChangeStateModeToPenTool` 在 `:937-955` 用同一判定再次检查。Bar 的 `SelectPenTool` 在 `Bar.Interaction.cpp:4922-4932` 对预检为变化的软/硬/荧光笔点击，先关闭菜单和取消 FineDial 候选，再进入最终模式事务。因此没有其他写入者插入时，render 线程不再能看到“Bar 新笔型 + Bar 旧候选”；首次 R1 的常规交错被消除。预检为不变时仍调用最终事务，`IdtState.cpp:944-949` 递增 revision，重复点击可继续使较早的 PPT `expectedRevision` 失效。
- **剩余 TOCTOU：条件性风险，未作真机复现**。`SelectPenTool` 在预检为不变时不会提前取消候选；若 PPT 在预检释放模式锁后、最终事务前用当前 `expectedRevision` 成功切到其他笔型，则 Bar 最终事务会重新切回用户点击的笔型，随后才在 `Bar.Interaction.cpp:4933-4939` 清理 FineDial。两次模式写入之间和最终事务内的 `SyncDraw3State` 期间，独立 render 线程仍可能看到候选 active，并在 `Bar.RenderLoop.cpp:1555-1600` 消费笔型变化，导致相应量程过渡没有机会在候选取消后启动。可构造的顺序为：SoftPen + FineDial 候选 → Bar 预检 SoftPen 得到 false → PPT 切 Highlighter 并被 render 观察 → Bar 最终事务切回 SoftPen → render 在 Bar 后收尾前观察 SoftPen + 旧候选 → Bar 取消候选。若 render 从未观察到中间 Highlighter，则没有可见的笔型量程变化；不能把所有这种竞争都称为用户可见卡顿。
- **风险归属与建议**：前述窄窗口涉及 PPT 异步接管本身没有 Bar FineDial 清理，不能仅靠 Bar 的只读预检完全解决。当前后收尾确保最终 UI 会收敛，但不能证明过渡动画连续。若本单元要求严格消除该时序，应在 render 侧不因 `thicknessFineDialCandidateActive` 就永久更新 `thicknessFineDialLastPenMode`，而把待处理笔型变化延至候选结束，或引入明确的跨入口候选取消合同；不得让 Bar 持 `stateModeTransitionMutex` 执行可能发窗口消息的 UI 清理。建议以可控线程屏障验证上述具体交错，再决定是否扩大修改。现状宜记作“普通路径已修复待验证；跨入口竞态未验证”，不能记作 R1 全部通过。
- **无新增死锁证据**：预检锁在 Bar 执行 `ClosePenTypeMenu` / `CancelThicknessFineDialSelection` 前释放；最终事务仍在锁外调用 `SyncDraw3State`。第二版没有把窗口 owner 或 Draw3 发布移入模式锁。

## 第三版 RenderLoop 修正的独立复审（当前结论）

- **原始 guard 的双读问题及最终修正**：首次 RenderLoop guard 在 `:1557-1560` 和 `:1599-1604` 分别读取原子 `thicknessFineDialCandidateActive`。候选可在两次读取之间被 interactionThread 的 `CancelThicknessFineDialSelection` 清除：第一次为 true 使该帧取消过渡，第二次为 false 又提交新 `thicknessFineDialLastPenMode/LastLogicalRange`，下一帧失去变化依据。当前差异在 `Bar.RenderLoop.cpp:1555-1560` 只捕获一次 `fineDialCandidateActive`，取消分支和 `:1601-1607` 的已处理笔型/量程提交分支共用它；此同帧双读交错已从源码控制流消除。
- **常规与异步 PPT 顺序**：Bar 预检变化时先取消候选再提交笔型，仍走原有正常过渡；若 PPT 在预检与最终事务之间插入，render 帧拿到候选 true 且新笔型时不会推进 lastPenMode/LastLogicalRange。候选取消后、FineDial 仍有效的下一帧会再次检测 `fineDialPenModeChanged`，按当前最终笔型至多启动一次量程过渡。若候选期间笔型来回变化并最终回到 lastPenMode，则没有净变化，也不启动过渡。`PptAnnotationTool::Laser` 保留非 Laser 的 `Pen.ModeSelect` 记忆，激光资格失效会关闭粗细滑杆，不应要求一个不可见的 FineDial 量程过渡。
- **取消与退出**：`CancelThicknessFineDialSelection` 清除候选但不把 `thicknessViewMode` 置为 Preview，因此继续停在 FineDial 的正常笔型切换可在下一帧补做过渡。`CloseThicknessSlider` 和渲染阶段的 `!thicknessSliderAvailable` 路径会关闭 FineDial 并清除候选；此时本帧取消过渡并更新最后笔型/量程，之后重新打开 FineDial 不会播放旧会话过渡，符合面板退出语义。已有过渡期间用户新建候选会取消原过渡；候选结束而笔型未再变化时不重复启动，候选交互已接管画面。
- **仍需验证的边界**：本次修改保存的是这一小段的候选原子快照，不是完整 `stateMode`/Bar UI 帧快照。RenderLoop 帧开头 `Bar.RenderLoop.cpp:993-999` 读取普通 `stateMode` 字段，稍后 `:1434-1545` 又读实时顶层模式、Laser 状态；PPT/Bar 写入可并发。`SetPenWidth/SetPenColor` 也未加入同一同步合同。F-009 的裸写部分已由事务入口处理，其 render 普通读残余及 F-002 仍待独立修复，不能把本次 R1 控制流闭环称为线程安全或 GUI 验证通过。
- **验证建议与状态**：确定性测试应覆盖候选 true→false 恰好发生在一个 render 帧期间、预检 false 后 PPT 接管再由 Bar 切回、候选期间多次笔型反向、候选取消但保持 FineDial、CloseThicknessSlider 退出后重新进入，以及新候选打断正在运行的过渡。当前为“源码控制流复审通过；自动/GUI 时序未验证”，不是完整 PASS。

## 已核对的其余合同与未完成项

- `IdtState.cpp:937-955` 将四种 Bar 工具的 `laserActive`/`Pen.ModeSelect` 与顶层 Pen 模式在同一次 `stateModeTransitionMutex` 临界区内写入；Laser 保留原有非激光笔型记忆。软/硬笔继续共用 Brush1 宽度与颜色；荧光笔、Laser 使用各自槽，见 `GetPenWidth/GetPenColor`。新命令在视觉无变化时仍递增 `stateModeTransitionRevision`，但跳过 `SyncDraw3State`；按当前桥接状态不变的合同，此行为可解释为有意的旧 PPT 回调失效处理，未发现必须重复发布的直接证据。
- `ChangeStateModeToPptAnnotation` 在 `IdtState.cpp:958-975` 仍在模式锁内检查 `expectedRevision`，随后更新工具和调用同一个顶层 Pen 转移 helper。`IdtPlug-in.cpp:871-875` 取版本后退出原生批注，`StartPptTakeoverAnnotation` 传入版本；真退出在 `:797-800` 使用 `ChangeStateModeToSelectionIfRevision`。本改动没有改为无条件选择态，也没有在 helper 内引入 COM 或磁盘工作。
- `SyncDraw3State` 继续按选择态决定 Setting owner，在锁外应用 Window Service owner，然后序列化 Draw3 bridge 并协调画布显隐；Bar 点击仍调用 `UpdateDrawButtonStyle` 和 `UpdateRendering`。Draw3 光标/输入由下游 bridge/tool 状态派生，未见本改动新增独立光标写入路径。是否与真实硬件光标呈现完全一致仍须集成/GUI 验证。
- **已有但未由本单元解决的风险**：`stateMode` 成员是普通字段；Bar render `Bar.RenderLoop.cpp:993-999`、按钮样式 `Bar.Button.cpp:847-856` 不持 `stateModeTransitionMutex` 读取，PPT/Bar 仍可并发写。新事务锁保证写入者之间串行，却不使跨线程读取自动成为一致快照。`SetPenWidth/SetPenColor` 也不持该锁。研究记录已把它列为后续独立单元，本次不能宣称总体 data race 收口。
- **测试缺口**：`InkeysHeadlessTests` 当前未搜索到 `ChangeStateModeToPenTool`、`ChangeStateModeToPptAnnotation` 或 revision 的生产入口测试。按子任务验收，跨入口等价、重复点击、迟到 PPT 和取消仍为未验证；不可把本次源码审查算作自动测试通过。
