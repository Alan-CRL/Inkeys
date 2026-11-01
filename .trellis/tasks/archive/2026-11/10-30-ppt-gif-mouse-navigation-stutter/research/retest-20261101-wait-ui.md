# Research: PPT 控件等待提示与鼠标翻页阻塞链复核

- Query: 指定 PPTX 第 3→4 页控件翻页仍阻塞、按钮可有三点但中心没有等待状态且最后硬切；键盘正常。检查 UI 状态、线程/锁与等待寿命能否解释该现象。
- Scope: internal；仅当前工作区源码与现有任务/规范，不操作 GUI、Office，不改产品代码，不构建、不执行 git。
- Date: 2026-11-01

## Findings

### 1. 中心“加载/等待”没有接入普通 PPT 导航等待（已由源码确认）

- `Inkeys/Inkeys/UI/Ppt/Ppt.cppm:159-176` 的 `PagePresentationState` 具有 `Pending / BasicControl / InkReady`，并在 mode 改变时推进 `pageAckEpoch`。
- 这个 mode 仅供 `CanAcknowledge` (`Ppt.cppm:179-186`) 判定墨迹回执。`Ppt.cpp:121-139` 的 `PublishSnapshot` 向 PageControl 发布页码、target、ackEpoch、navigationWaiting、recovery，**没有发布 PageStateMode**。
- `PageControl.cpp:907-946` 的生产 `BuildPageContent`：白板 `switching` 会切为 Status，显示 Loading/Wait；PPT 只有 `ppt.recovery.kind != None` 才切为 Status。普通 navigationWaiting 只映射为 `content.navigationWaitingPrevious/Next` (`:915-916`)。
- `Bar.Scene.cpp:679-692` 对方向按钮 2/4 绘制右上角三个静态点，注释明确仅表示命令仍在等待，不替代旧页码或 ready。
- `Bar.Scene.cpp:335-343`：Numeric→Numeric 使用 `SetStringImmediate`，仅涉及 Status 的内容转换才走 `TransitionToString`。因此没有 Status 的普通 PPT 翻页，中心保留旧数字后硬切新数字，是现有明确实现。

若用户原意是“按钮有三点，但中心没有加载/等待文字”，这首先是当前 UI 表达缺口，并非必须由渲染线程卡住解释。不能拿按钮三点存在宣称中心等待动画已验收通过。

### 2. 三点只覆盖 native 业务请求，不覆盖整个页同步/输入关闭窗口（已由源码确认）

- `IdtPlug-in.cpp:149-189`：成功入队 Previous/Next，释放业务 mutex 后调用 `PublishNavigationWaiting(session, epoch, direction)`。
- `Ppt.cpp:316-327`：在 `stateMutex` 内校验真实活动 session、更新等待状态；释放锁后 `PublishSnapshot`。
- `IdtPlug-in.cpp:1443-1456` 的独立业务消费者弹出请求，`DispatchPptFocusBusinessAction` (`:636-657`) 在业务线程调用 `NextPptSlides` (`:504-517`)；该 native COM 方法为 void。
- `CompleteRequest` (`IdtPlug-in.cpp:1465-1487`) 在该业务请求作用域结束时立即 `CompleteNavigationWaiting`。这里不等待新的 descriptor、Draw3 Present 或 PageControl ack。
- `PptCOM/PptCOM.cs:1625-1642` 的 `RequestOwnerCommand` 对非 owner 调用只等 `Request(..., 2000)`；`NextSlideShow` (`:1724-1725`) 丢弃结果并以 void 返回。
- `SlideShowSession.cs:260-275`：两秒超时只取消尚未执行的请求；执行中的 Office 调用不会因超时停止。

由此存在两种合法但 UI 不充分的时段：

1. Office `View.Next` 已开始但超过两秒：public void 调用可先返回，native 请求结束、三点清除，而真正 Office 调用仍在 owner 中执行。
2. `View.Next` 很快返回，后续当前页读取/descriptor/Draw3/UI 门禁仍未就绪：三点也可早于整页同步完成清除。

这两条是代码可达性，并非本轮已实测发生。必须用 `PptOwner start/timeout/complete`、fast/full ms、`PptNav enqueue/complete` 及 `PptSync` 的相同 session/target 时间线区分。

### 3. 当前没有发现“点击线程持 UI/渲染大锁等 Office”的直接证据

- Pointer Down 在 `PageControl.cpp:2673-2765` 处理。真正 `InvokeDirection` (`:817-850`) 在短 `callbackMutex` 中复制函数，释放锁后调用；回调为向 native 业务队列入队，并非该输入 worker 直接同步执行 Office。
- 生产调用前已释放 page Scene 锁，代码注释 `PageControl.cpp:2761` 明确此边界。
- native 队列 mutex 在 `IdtPlug-in.cpp:166-169` 只保护 push；业务取出后也在锁外执行 COM (`:1456-1459`)。
- `Ppt.cpp:121-139` 拷贝纯值状态后才跨层发布。`PageControl.cpp:5639-5660` 在 drag/snapshot 短锁内更新纯值，随后锁外 `AcknowledgePage` / `RequestAll`。
- `RequestAll` (`PageControl.cpp:542-546`) 进入 RenderPipeline scheduler.Request；状态比较 `PageControl.cppm:817-831` 已包含等待字段，不会把三点变化当“未改变”。
- 渲染 `PageControl.cpp:2294-2316` 使用 try-lock，native candidate 回执通过纯值非阻塞消费 (`:2104-2206`)；普通页码/目标 pending 不直接禁止等待图标绘制。`ConfigureSurface` 会在 published revision 变化时重新调用 `BuildPageContent` (`:1174-1180`)。
- `AcknowledgePage` (`:2032-2049`) 的业务回调只发布匹配的 UI-ready (`IdtPlug-in.cpp:220-231`)，没有 Office 调用。

因此“共享渲染线程被 COM 同步阻塞”目前只是待验证假设；源码直接路径不支持这样下结论。Window owner、驱动、其它 RenderPipeline 客户端仍可能造成呈现延迟，本研究没有运行时栈或逐帧样本，不能排除。

### 4. 鼠标与键盘边界仍不同

鼠标路径进入 owner 的 COM 命令、包含 Next 前的 `GetCurrentSlideIndex` (`PptCOM.cs:1681-1696`)、fresh View 与 `View.Next` (`:1705-1713`)；键盘由 Office 处理后，Inkeys 才观察新状态。减少完整拓扑枚举并不消除这些当前页读取或 Next 本身的阻塞。

native 在 Next 后调用 `FocusPptSession` (`IdtPlug-in.cpp:655-656`)；当前该函数使用 Win32 `SetForegroundWindow` (`:609-625`)，没有再调用 managed Activate。故不要误记为“Next 2 秒 + Activate 2 秒”路径；Focus 的实际耗时仍可由运行时测量确认。

### 5. 建议 root 本轮收集的分段证据

1. 先核对实际进程 EXE 路径、内嵌 DLL 哈希与修复所在工作区；当前 root 发现运行进程来自 Inkeys-draw，此事实不能仅凭源码分支推定其 DLL。
2. 鼠标第 3→4、第 4→5 与键盘对应样本分别记录：实际输入时间、PptNav enqueue、PptOwner enqueue/start/timeout/complete、fast/full ms、raw/native_observed、descriptor_accepted、canvas_presented、page-ui-commit、input_open。
3. 如果三点出现并在阻塞中途消失，先对照两秒 timeout 和 native Complete；若 button 三点一直存在而中心旧数字不变，是上文缺少 Status 投影，不等于状态完全没有发布。
4. 若日志显示 Publish 已发生但三点未上屏，再补纯值 UI 诊断：等待状态 Publish/Complete、BuildPageContent flags、pixel submit/native receipt 的 session/epoch/revision 与时间。现有 fake 状态断言不等于真实阻塞期间可见像素。
5. UI 改进方向可将“命令 outstanding”和“页同步 pending”分别表达，继续保留旧数字和严格 ready 门，避免把命令返回当页同步完成；但本研究不实施该设计，也不以键盘模拟替代 COM。

## Files Found

- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp`：生产输入、内容构建、渲染与 native candidate 事务、纯值状态发布。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cppm`：等待状态/场次、水位、页回执和状态比较。
- `Inkeys/Inkeys/UI/Ppt/Ppt.cpp` / `Ppt.cppm`：PagePresentationState 内部门禁、待发布纯值快照和 UI-facing API。
- `Inkeys/IdtPlug-in.cpp`：PPT 业务队列、COM 调用、导航等待发布与完成、焦点交接。
- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp`：中心文字 Numeric/Status 过渡和三点绘制。
- `PptCOM/PptCOM.cs` / `SlideShowSession.cs`：唯一 Office owner 的命令及两秒有界等待。

## Related Specs

- `.trellis/spec/ppt-interop/native-session-ui3.md`，普通翻页快速描述符与导航等待反馈：纯视觉等待不替代 ready、命令状态与页面身份分开。
- `.trellis/spec/native-desktop/rendering-and-ui.md`，统一分页 Scene：Status 文字过渡、原生候选与真实回执、锁外业务/窗口调用。
- `.trellis/tasks/10-30-ppt-gif-mouse-navigation-stutter/research/investigation.md` 与 `validation.md`：上一轮 fast/full 和像素未验边界；本研究不将旧实机样本当本次已复现。

## External References

无外部依赖查询；本研究为当前本地源码控制流分析，没有将推测的 Office 行为写成外部事实。

## Caveats / Not Found

- 未运行任何 GUI/Office、构建或 git 命令；未修改产品、配置、规范或其它任务。仅写本文。
- 真实输入/可见等待/COM 阻塞时间由 root 实测；上文两秒超时提前清点是已确认代码可达性，不是实测归因。
- 运行进程工作区不同是重要产物身份核验点，但不能在 root 核对 DLL 之前宣布为旧版本误测。
- 本轮仍未知道具体哪个 Office 属性/方法消耗最长；现有 PptOwner 仅聚合完整命令和 fast/full，必要时继续拆当前页子调用。
