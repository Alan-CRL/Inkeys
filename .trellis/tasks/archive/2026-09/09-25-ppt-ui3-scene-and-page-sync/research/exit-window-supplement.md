# Research: PPT 真退出后的桌面穿透与窗口收敛

- Query: 追查原生 Esc、Office 自身退出及 Inkeys 退出后，选择 UI、Draw3 workspace/output 和真实 HWND 命中为何可能分离；寻找生产路径集成测试接缝。
- Scope: internal
- Date: 2026-09-25

## Files found

- `Inkeys/IdtPlug-in.cpp`：PPT 会话结束、Desktop workspace 发布及按钮退出路径。
- `Inkeys/IdtState.cpp`：工具桥接、双画布表面显隐调和、runtime revision 等待及白板窗口交接。
- `Inkeys/IdtMain.cpp`：生产窗口规格及各角色初始扩展样式。
- `Inkeys/Inkeys/Window/Window.cpp{,m}`：Window Service owner-thread 命令、成对显隐、样式写入、白板样式和 capture 收尾。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp`：bridge 消费、input admission、output/ready revision 和 workspace 回执。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`：绘制线程场景命令、slot 交接、目标帧成功 Present 与 ready 通知。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp`、`Draw3.HiddenWindowTest.cpp`：命令 wake、Drawpad 消息和现有隐藏 HWND 集成入口。

## Findings

### 已由源码直接确认

1. **真正退出没有统一工具/窗口收尾。** `PptInfo::EndSession` 仅清可信会话、UI/Freeze、缓存；没有调用 `ChangeStateModeToSelection` 或窗口表面调和（`IdtPlug-in.cpp:548-570`）。其外层只在可靠 Inactive 或旧 HWND 销毁/复用时发布 Desktop（`:652`、`:659-675`、`:756-760`）。`PPTLinkageMain` 中按钮成功后的选择切换只覆盖该业务请求，且当工具枚举已是 Selection 时跳过（`:969-992`）。原生 Esc/Office 退出不会执行这段按钮收尾。`ChangeStateModeToSelection` 会同时发布 UI 状态、Draw3 state 与调和（`IdtState.cpp:611-619`），因此生命周期路径与按钮路径并不等价。`EndSession` 还在同一循环内旧/新可信会话交接时使用（`IdtPlug-in.cpp:696-723`）；补收尾必须以退出代次校验，不能无条件覆盖新会话或新工具。

2. **“无法落笔”与“窗口拦截”是两个不同开关。** Host 的 `RefreshPresentationInputGateLocked` 只改 Draw3 `AdmissionBlocked`，非 Presentation 的期望在 runtime 尚为 Presentation 时仍阻止新 contact（`Draw3.Host.cpp:173-195`）。Selection 状态变化通过 `PublishProductState` 写 bridge（`IdtState.cpp:158-169`、`Draw3.Product.cpp:85-99`），而 `WM_MOUSEACTIVATE` 只返回 MA_ACTIVATE/MA_NOACTIVATE，既不是 `WM_NCHITTEST` 穿透也不隐藏 HWND（`Draw3.WindowControl.cpp:1297-1300`）。所以 UI 可以显示 Selection，同时 Draw3 拒绝落笔，而上一帧主 HWND 仍截获系统点击。

3. **真正可穿透的选择输出必须完成帧交接。** 生产主 `Drawpad` 起初清除 `WS_EX_LAYERED | WS_EX_TRANSPARENT`，DComp 可再带 `WS_EX_NOREDIRECTIONBITMAP`；辅助 `DrawpadPresentation` 固定是 layered + transparent（`IdtMain.cpp:1694-1695`、`:1746-1760`）。两者是由 Window Service 管理的前后 owned popup（`Window.cpp:822-869`）。Selection + 非白板选择辅助输出（`Draw3.Bridge.h:108-112`）；有内容时显示辅助、无内容且辅助全帧 alpha 归零时隐藏两窗（`Draw3.PresentationState.h:14-22`）。主窗口不能靠显示透明像素、AdmissionBlocked 或 MA_NOACTIVATE 被视为穿透。

4. **可具体构造的危险 Waiting 状态：主 HWND 已显示 → 切到 Selection/Desktop → 当前 output full Present 尚未 ready。** `ReconcileDraw3PresentationState` 在 `!firstFrameReady` 或 Selection 非白板且 requested/ready target、revision、content revision 任一不一致时直接返回 Waiting；没有改变已有主窗可见性，并把 `draw3PresentationRetryPending` 清为 false（`IdtState.cpp:260-289`）。正常调和只有 mode 同步调用和 StateMonitoring 中 runtime revision 改变或 retryPending 才再执行（`:416-428`、`:658-685`）。如果 Present/ready 没有后继 revision，当前主窗可持续拦截；250ms 唤醒本身不重试 Waiting。这个状态机空隙是**确定的源码行为**；它是否是报告的具体现场仍需 HWND/runtime 日志或生产事务复现。失败的 `SetDrawpadSurfaceVisibility` 则会置 retryPending（`:309-318`），与 Waiting 不同。

5. **窗口事务已有 owner-thread 通道，但返回值并未完整证明真实可见性。** `Service::Submit` 把跨线程命令交给窗口 owner 线程并等结果（`Window.cpp:1118-1158`）；`ApplyDrawpadSurfaceVisibility` 通过 DeferWindowPos 成对切换，失败时先隐藏两窗再只显示期望表面（`:1629-1689`）。成功的 `EndDeferWindowPos` 立即返回 true，未读回两窗的实际可见结果；fallback 只确认期望窗可见，没有同时核对另一窗不可见。`SetClickThrough` 只设置/清除 `WS_EX_TRANSPARENT` 再 `FRAMECHANGED`，未读回样式和命中结果（`:1453-1466`）。这不是选择模式的通用修复，尤其主窗在 DComp 下可能是非 layered。

6. **其余候选 HWND 不能预先排除。** `MagnifierHost -> Freeze -> DrawpadPresentation -> Drawpad -> PPT/Bar` 是创建顺序与 owner 链（`Window.cpp:795-870`）；`Freeze` 初始 layered/transparent（`IdtMain.cpp:1785-1790`），但白板模式成组改激活样式并保留运行时 click-through 位（`Window.cpp:1161-1270`）。`MagnifierHost` 仅在放大镜路径显式 Show/Hide（`IdtMagnification.cpp:141,192`）。白板退出才 Hide Freeze、恢复辅助 click-through，并为主 Drawpad 调用 `SetClickThrough(snapshot.selectionMode)`；这几处调用返回值有的忽略（`IdtState.cpp:861-893`）。实际故障须记录所有候选 role、visible/enabled/exStyle/owner 与 capture，不能仅假定主 Drawpad。

7. **Draw3 命令具有后继唤醒，但不保证成功 Present。** `PublishProductWorkspace` 改 bridge 并唤醒 Host（`Draw3.Product.cpp:102-109`，`Draw3.Host.cpp:1426-1435`）；`PumpBridgeState` 将非 Presentation workspace 命令入队（`Draw3.Host.cpp:673-746`）。窗口命令入队会 `RequestControlWake`（`Draw3.WindowControl.cpp:338-350`）。绘制线程在空 contact 时消费场景命令，切换文档 slot 后设 full Present 并 stage ready（`Draw3.DrawingController.cpp:5357-5369`、`:5746-5777`、`:4498-4533`）。只有成功 Present 且恢复无未决时才发布 workspaceReady（`:7623-7631`）；Host 观察后更新 workspace、清 commandScenePending、刷新输入闸门，并在与最新 bridge 不符时补发目标（`Draw3.Host.cpp:384-425`）。这条正常路径不依赖用户再点一次，但任何 Present/恢复失败都会使 ready 与窗口交接停住，需要安全隐藏和可恢复重试策略，不能伪造 ready。

8. **旧物理 contact 的生产收尾已有基础。** gate revision 变化时绘制线程将已接受的非终态持久 stroke 按最后位置收尾，清手势/瞬态并 `RequestFullPresent`（`Draw3.DrawingController.cpp:3472-3515`）；被 gate 拒绝的 contact 隔离到终态（`:3518-3550`）。Window Service 的 `CancelPointerCapture` 在 owner thread 向 Drawpad/四分页控件/Bar 发送 WM_CANCELMODE，并只对本线程实际 capture HWND 调用 ReleaseCapture（`Window.cpp:1334-1349`）。退出补收尾应只撤销与该会话有关的 capture，避免取消用户随后在 Bar/新会话中的操作。

### 推断与测试接缝

- 优先注入生产顺序：先 Selection 后 Desktop、先 Desktop 后 Selection；在 `SetPresentationTarget`、最后一帧 Present、`ObserveWorkspace`、`Reconcile` 各边界记录 session/service/runtime revision。Waiting 时同时记录 `firstFrameReady`、requested/ready output target/revision、content/presented revision、当前及期望 workspace、input admission、commandScenePending、capture。`HostRuntimeSnapshot` 已提供多数输出字段（`Draw3.Host.cpp:1235-1265`），但 `commandScenePending` 尚未暴露。
- 扩展现有 `Draw3.HiddenWindowTest.cpp:1545-1614` 的真实 Host + Window Service 路径，按 DComp-compatible 与 legacy 实际 presenter 走 Pen/Presentation→Selection/Desktop；验证服务的主/辅助 `IsWindowVisible`、style readback、owner、输出 revision 与实际 Present，无新输入时仍收敛。现有测试只单独测显隐命令和隐藏窗输出（`:589-640`、`:1597-1608`），没有将产品 `ReconcileDraw3PresentationState` 与生命周期并入同一事务，也不做系统命中测试。
- 故障注入点：让 Present 保持未 ready、让 `SetDrawpadSurfaceVisibility` 返回失败、在旧 workspace 回执后立即发布新会话/新工具、持 contact 退出；验证失败期间主非穿透 HWND 不保持可见，后续 wake 可恢复。实际穿透必须在人工桌面由系统命中测试；直接向底窗 `PostMessage` 只证明消息注入，不证明穿透。
- `LogDraw3PresentationFailure` 当前只在窗口提交失败时输出主/辅助 HWND 和 target（`IdtState.cpp:214-257`），不覆盖 Waiting、Freeze/MagnifierHost、capture、命令 pending。可新增默认关闭且限频的退出诊断，避免常规 16/250ms 轮询刷屏。

## External references

本次没有访问外部文档；Windows 点击命中与 DComp 具体运行结果须以当前机器的真实 HWND/系统命中验证。

## Related specs

- `.trellis/spec/native-desktop/index.md`：窗口线程、渲染线程和状态所有权。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：主画板与辅助 ULW 的实际呈现分工。
- `.trellis/spec/native-desktop/input-and-ink.md`：contact、Stored 历史边界。
- `.trellis/spec/native-desktop/draw3-integration.md:56-74,312-377`：单 Host、固定两表面、Selection 完整帧交接、失败显隐和隐藏 HWND 测试合同。
- `.trellis/tasks/09-25-ppt-ui3-scene-and-page-sync/{prd,design,implement}.md`：当前任务的已批准会话、UI-ready、contact 和焦点规则。

## Caveats / Not Found

- 未运行 Windows/Office、隐藏窗口测试或系统输入；不能把上述危险时序写成此次用户现场的已复现根因，也不能确定究竟是哪一个 HWND 命中。
- `WindowFromPoint` 或直接 `PostMessage` 不能替代真实下层窗口鼠标/触摸收到系统投递的验收。
- 现有 spec 的主 Drawpad“固定不带 WS_EX_TRANSPARENT”与 `IdtState.cpp:709-710,724-725,747-752,783-784,852-853,882-883` 的白板交接运行时切换不完全一致，实施后应按实际最终合同更新。
