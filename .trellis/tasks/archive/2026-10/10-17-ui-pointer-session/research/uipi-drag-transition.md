# Research: UIPI 前台下拖动离开工具栏后的输入延续

- Query: 管理员窗口前台时拖动起初有效，鼠标离开工具栏进入其区域后停止；回普通窗口不稳定恢复，回到原工具栏/停止位置附近才恢复。调查 Raw/原生流切换、捕获与可行的最小恢复策略。
- Scope: internal code/test investigation; external Windows capture/security interpretation由主会话查官方文档。
- Date: 2026-10-17
- Task: `.trellis/tasks/10-17-ui-pointer-session`；已读 workflow、prd/design/implement、correction、timeout-correction 与 native-desktop input/session spec。本研究只写本任务 research，不修改产品/测试/spec，不运行 GUI，不执行 git。

## Findings

### 已确认的源码事实

1. **Raw 身份一旦绑定，原生 Move 被永久抑制，并没有“Raw 仍在持续送达”的条件。** `Inkeys/Inkeys/Input/UiPointerInput.cpp:150` 的 `MouseGesture::AcceptNative` 验证本次 native generation/device/contact 和来源接管后，在 `:165-166` 直接以 `rawGeneration_ && !rawDisabled_` 拒绝 Move。`AcceptRaw` 在收到 Down 时即设置 Raw generation/device（`:195-205`），即使后续没有任何 Raw Move 也会触发此门控。有效原生 Up 保留（`:169-171`）。因此“曾收到一个 Raw Down”与“现在能接收 Raw 移动”目前被等同。
2. **没有已绑定 Raw 的静默饥饿检测，也没有自动原生移动回退。** `Provider::ExpirePendingMouse` 只检查 `state->armed` 且等待满 1000ms（`UiPointerInput.cpp:327-335`）；绑定时 `armed=false, receiving=true`（`:386-391` / `:478-481`）。接收状态不因缺少 Move 而改变，`RawMouseAvailable` 只返回注册成功标记（`:648-652`）。实际静止按住当然不能被推断为释放，本问题也不能用新的“拖动超时”解决。
3. **Bar 和 PPT 都真正使用这一共享门控。** Bar 在私有队列优先取 Raw 后，经 `barMouseGesture.AcceptNative(sample)` 处理 HiMsg 样本（`Bar.Interaction.cpp:675-712`, `:791-794`）。PPT 每次非嵌套消息前先 drain Raw（`PageControl.cpp:2271-2276`），再将原生 Move/Up 送同一个 policy（`:2353-2363`, `:2380-2387`）。所以如果 WM_INPUT 停止但普通 Move 仍到达，**两客户端都会主动丢弃仍有效的 Move**。这是可确定的实现缺陷，但不是本次实机丢包位置的证据。
4. **主按钮 Seek 本身没有建立 SetCapture。** `BarUISetClass::Seek` 从 `Bar.Interaction.cpp:6842` 开始，`:6859` 只准备可选 Raw；活动循环在 `:7369-7385` 等待消息、应用绝对点并检查 Up。该文件只有两处 SetCapture，分别属于粗细滑轨（`:1075-1084`）与二维选色板（`:1094-1102`）。所以主按钮越界依赖 Raw 或因窗口跟随而继续命中到的原生消息，不能假定 Seek 自身拥有全局捕获。Bar 维持 `MA_NOACTIVATE`（`:904-906`）。
5. **PPT 有捕获，但它仍是不激活窗口。** Down 之后 `SetCapture(hwnd)`（`PageControl.cpp:2714-2715`）；窗口创建有 `WS_EX_NOACTIVATE`（`:3002`），窗口移动用 `SWP_NOACTIVATE`。活动 mouse drag 且 rawTracking 时 `WM_CAPTURECHANGED` 被忽略（`:2410-2413`）；否则既有取消路径重置会话。不能把 GetCapture()==hwnd 等同于 Windows 必须把鼠标热点所在的全部外部区域消息送来。
6. **Raw 未能绑定与“绑定后停止”必须分开。** Provider 仍对 native Down 和 Raw Down 使用精确 messageTime+screen 匹配（`UiPointerInput.cpp:372-379`, `:458-461`）。不满足只会放弃可选补充，不影响原生拖动。由此，管理员场景起初可拖不证明 Raw 已绑定：也可能始终只是工具栏命中区域内的原生 Move。返回工具栏附近恢复，恰好也符合这种路径。

### 当前症状对原因排序的意义

主会话转达用户补充：移回普通窗口不稳定恢复，移回工具栏原来/停止位置附近才恢复。这个空间边界比“管理员焦点”本身更具体。它支持“工具栏失去鼠标热点命中后没有可靠的外部消息流，重新进入自身区域又有原生 Move”的解释；可能是 Raw 根本未绑定/没有送达，而不是共享门控拒绝了正在送达的原生 Move。

这里仍只是与源码一致的**推断**：没有 provider/client 事件日志，无法证明进入管理员区域前后的 WM_INPUT、WM_MOUSEMOVE、Raw claim 状态、捕获状态。主会话检查现有日志没有 pointer/raw/cancel trace，因此不能将某条 OS 流缺失写成测量事实。

另一个研究员负责边缘夹紧后的抓手偏离。窗口/抓手未跟上光标会更容易让光标离开当前命中区域，从而暴露原本被“窗口恰好跟着鼠标移动”掩盖的输入延续限制；这与输入问题可以同时存在，不能用修几何就宣称彻底消除跨权限输入限制。

### 已有队列、通知与取消路径

- `MouseChannel::TryPop` 每次取空后清 `wakePending`（`UiPointerInput.cpp:571-578`）；wake 只是合并通知，数据在队列内。PostMessageW 失败会主动生成 InputUnavailable Cancel（`:290-305`），不会静默永久停 drain。
- 已绑定队列溢出直接显式 Cancel（`:338-345` / Mailbox），不是固定空间边界；Bar/PPT 均消费同一代次取消。快速拖动有可能触发，但“回到工具栏区域恢复”并不单独证明溢出；需要 cancelReason。
- Bar 活动循环检查来源、桌面、mode revision、fold 状态（`Bar.Interaction.cpp:653-674`），PPT 活动定时器每 50ms 检查桌面并 drain（`PageControl.cpp:2250-2269`）。OpenInputDesktop 在同桌面只比较 desktop 名称，不检测前台完整性（`UiPointerInput.cpp:655-682`）。
- Raw Read 失败、后备 GetCursorPos 失败有明确 CancelAll（`UiPointerInput.cpp:694-724`）；非鼠标/提升鼠标被过滤（`:703-705`）。依照当前合同不能把失败伪造成 Up。
- 正常 WM_INPUT 采样用 `MessageDispatch::TryGet` 的原始 MSG.pt/time（`UiPointerInput.cpp:711-718`；`Window/Window.MessageDispatch.h:33-40`），没有精确派发快照才退回 GetCursorPos/GetMessageTime（`:719-727`）。这保留了队列时间线，但不证明跨权限可接收性。

## Safe minimal implementation options

### 1. 恢复“仍然到达的有效原生 Move”——可在现有范围内实施，但只能解决单流饥饿

若主会话决定实施，建议集中在 MouseGesture，不另加 Bar/PPT 旁路：

- 单独记录最近接受/观察的有效 Raw 活动时间及移动权威状态，不能只用 rawGeneration!=0 判断 Raw 正在接收。
- Raw 正常连续送达时保持单一 Raw 轨迹权威，保留同毫秒不同点的 excursion，防止双路重复累加 FineDial 速度。
- 当本代次有效 native Move 的事件时间明显晚于最后 Raw 活动，且 Raw 没有更新时，允许原生接替移动。应以输入事件时间及严格单调下限裁决；只看出队时墙钟会把队列阻塞误判成接收失效。
- 接替时将 Raw 重放下限推进到最后接受的原生 messageTime，并拒绝该时间及更早的 Raw Move；新 Raw Move 仅在时间下限之后才可重新接管，避免迟包回跳/重复。已有 `AcceptMovement` 只拒绝 `< latest.time`，同毫秒不同点合法；因此不能只删除原生门控，也不能只复用 `<` 比较来解决双流重复。
- Raw Up 监听与 Move 权威分离。回退到 native Move 不应调用 FinishMouse/永久 rawDisabled；保留同归属 Raw Up 结束真正拖动的能力。终态仍只接受一次，不能用 quiet timeout 猜 Up，不能把 candidate Raw Up 变点击。
- 不能让该策略改回上轮的一秒时长限制。补充未绑定取消仍然只撤销补充，原生拖动可持续任意时长。

**限制：如果离开工具栏后 Raw 和原生 Move 都没有送达，这个改动完全没有新的位置来源。** 用户返回工具栏附近才恢复的补充，更倾向于需要先证明是否属于此类。不能把单流回退描述为已解决这次全部现象。

### 2. 给主按钮增加 Capture——可能提升普通窗口越界，但不是跨管理员前台的保证

Seek 当前没有 capture 是明确事实；可由 owner 线程建立/释放 capture 来改善可覆盖范围，保留正常 cancel/reentry。但主会话查官方文档确认：后台窗口捕获本身有热点可见区限制。仅调用 SetCapture 不能保证本次管理员焦点且不激活的外部区域拖动，更不能在报告中说这是 UIPI 的解法。

### 3. 激活再捕获／UIAccess 是策略变化，不在本次可默认实施边界

- 真实拖动时激活 Inkeys 再捕获可能获得正常前台捕获语义，但会影响 PPT/用户焦点与快捷键，且前台激活本身也受 Windows 规则限制，需要明确产品策略。
- UIAccess 涉及签名、可信安装路径与部署资格；提升权限改变全应用权限。两者都不属于输入适配器小修，当前任务明确排除激活/权限/部署策略。
- 不改 hook 的约束继续有效。不能以 GetAsyncKeyState/全局缓存恢复按住循环，也不能承诺低完整性进程能保证读取所有更高完整性输入。
- GetCursorPos 即使可取位置，也不能证明左键仍按住或已松开；用它轮询移动仍需可靠终态。不能因未收到 Up 一直跟随，也不能静止超时伪造 Up。

## Recommended validation / narrow trace

无窗口回归应直接调用生产 MouseGesture：Raw Down 绑定+Raw Move 后静默，native Move 延续数秒，重复/迟到 Raw Move 与真实 Raw Up；原生 Up 优先、Raw Up 优先均只结束一次；旧 Raw/旧 native 代次不结束新手势；同毫秒 excursion 在正常 Raw 流中保留；重新接管后不回放 native 已消费时间段；零 Raw Down 继续保持上轮 999/1000/1500/5000ms 回归。

为区分实机两种情况，最小有界 trace 至少记录：client/session/native generation、Raw claim pending/bound/disabled、每来源 Down/Move/Up 到达和 policy accept/reject 理由、原始 messageTime/point、最后接受点、Cancel reason、capture hwnd/foreground hwnd、桌面检查结果。只记录会话起止/权威切换与限频摘要即可；没有这份 trace 前不能声称已测得某个 UIPI 拒绝。

## Files found

- `Inkeys/Inkeys/Input/UiPointerInput.h/.cpp` — MouseGesture 仲裁、Raw claim、私有队列、注册、原始消息采样和桌面检查。
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` — HiMsg/Raw 适配、主按钮 Seek、少数控件 capture 与 MA_NOACTIVATE。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp` — 本地 raw drain/原生输入、owner capture、定时器和 drag 状态。
- `Inkeys/Inkeys/Window/Window.MessageDispatch.h` — thread_local 原始 MSG scope、POINT/time 与 uptime 转换。
- `InkeysHeadlessTests/ui_pointer_tests.cpp` — 现有断言明确要求 Raw 绑定后拒绝 native Move（`:794-817`）；只在 explicit Raw Cancel 的 candidate 路径测试移动恢复（`:820-850`）。缺少已绑定后无 Cancel 静默的回退回归。

## External references / related specs

本研究未独立浏览。主会话已核对 Windows 官方 `SetCapture` 与 `GetCursorPos` 文档，引用和最终 OS 判断由主会话负责：

- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcursorpos
- `.trellis/spec/native-desktop/ui-pointer-session.md` — 当前写明 Raw 绑定后独占移动；若增加静默回退，需要同步合同与测试，不能让两客户端各自实现。
- `.trellis/spec/native-desktop/input-and-ink.md` — UI 会话与 Draw3 输入/光标独立，不能扩展到绘制输入。
- Task `correction.md`、`timeout-correction.md` — HiMsg 原有入口与可选 Raw、不改 hook、未绑定期限不取消原生拖动。

## Caveats / Not Found

- 没有运行 GUI/隐藏 HWND；没有管理员 foreground 的 WM_INPUT/WM_MOUSEMOVE 实际 trace。
- 无法凭截图、能初始拖动或注册成功判定 Raw 已绑定/持续可用。
- 没有修改生产源码、测试或 spec。第一项几何问题由另一研究员负责。
- 源码可确定的 native lockout 可以修复，但用户补充的空间恢复边界更支持“两条源在窗外均缺少”的候选；需明确分开验证和陈述。
## 主会话核对的官方边界（2026-10-17）

- SetCapture: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture — 完整捕获要求前台窗口；后台捕获只覆盖热点位于自身可见部分的事件。这是接收范围限制，不能据此声称已测得此次所有丢包都是 UIPI。
- GetCapture: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcapture — 返回调用线程的捕获；交互线程不能用 null 断言 HWND owner 未捕获。诊断应使用 GetGUIThreadInfo 指定 owner thread。
- GetGUIThreadInfo: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo — 支持读取指定 GUI 线程信息，初始化 cbSize，检查成功；Win7 可用。
- SetForegroundWindow: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow — 设置前台有明确系统条件，满足条件也可能失败；临时激活不能未经验证称为无条件保证，且改变产品焦点语义。
- GetCursorPos: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcursorpos — 只提供屏幕位置，受 input desktop/window station 权限约束；没有按键终态，因此不能单独替代完整拖动会话。
- Win7 适用的 UIAccess 策略参考: https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-server-2012-r2-and-2012/jj852244(v=ws.11) — 明确列出 UIAccess 可跨完整性读取 Raw/low-level hooks/键态，要求受信任签名，默认还要求安全安装位置。不是增加 RIDEV_INPUTSINK 或仅设置 manifest 即可取得的能力。

用户后续确认抓手偏移可在同屏普通前台复现。几何偏移与跨权限接收故障不能合并成单一 UIPI 结论；本轮具体实现以 edge-and-stream-correction.md 为准。
