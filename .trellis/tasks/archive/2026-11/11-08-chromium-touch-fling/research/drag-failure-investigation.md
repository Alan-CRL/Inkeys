# 2026-11-12 拖动全失效：调查结论与现场证据

> 11-12 后续更新：已完成系统 DLL/匹配微软 PDB 的静态调用链核查及独立隐藏 HWND 对照。现在已在本机直接复现：同 HWND 先启用 RTS 后 DM Activate=0x80070005，反序则 RTS Enable=0x80070005；DM Activate 失败回滚会走 Deactivate→UndelegateInput，立即第二次 Activate 又能成功。完整新增证据与限制见 [dm-rts-coexistence-probe.md](dm-rts-coexistence-probe.md)。以下保留此前现场梳理，不代表后续调查尚未完成。

## 范围与当前结论

延续 `11-08-chromium-touch-fling`，对比体验基线 `961ffdf8` 与当前工作区的触控板扩展/上一轮修复。本轮只调查，未修改产品代码、测试、配置、工程或已有规范，未编译、启动应用或执行 GUI/Computer Use。

**故障已收窄到真实接触输入与窗口/DM 的交接；同 HWND 的 DM 注册影响既有 RTS 是目前最强根因候选，尚未证明具体失败 API 或完成系统对照。** 不把此前 flags 模型测试 PASS 再当成根因或恢复证明。

用户最初回答“切回画笔仍能画”，随后明确纠正为：鼠标/触摸/笔尝试拖动后，切回画笔也不能继续绘制；笔 hover 能跟随，落笔光标固定在 Down 点、不出墨，抬起后恢复跟随。后一次反馈取代前一次判断。用户还确认重启后无需先使用触控板即可触发，故不能将旧 PTP Running 残留作为首次失败的必需原因。

## 第一份新现场日志：DM 初始化是关键时间边界

日志为用户直接粘贴，非 11-08 旧附件。产品 PID=31948，UI thread=22996，drawing thread=25496；CanvasPan 频率为 10000000 ticks/s，构建标记为 Nov 12 2026。

| 顺序 | 原始锚点 | 证明与限制 |
| --- | --- | --- |
| 启动 | RTS Extended ready；input-ready open；reasons 最终归零、desired=1 actual=1 | 启动时输入成功，白板已就绪。desired/actual 是 Workspace 枚举，不是输入开/关 bool。 |
| DM 前 | CanvasPan seq2–680；qpc240479279868 至 240494105709；多个 context 的 rts-batch published=1 | Coordinator 当时确实收到真实 Move。不能据此证明后来的故障阶段仍收到输入。 |
| 首次 DM | seq681，qpc240498149214，UI thread；touchpad-init result=0x80070005 action=unavailable | 实际初始化发生 E_ACCESSDENIED；日志没有 stage，不能指称 Activate/CreateViewport/Enable 中某一个已确定失败。 |
| 离开移动 | seq682，qpc240587869978，drawing thread；leave-move-mode | 距失败约8.972秒，绘制线程仍能处理模式变化，不支持永久 consumer 死锁为唯一解释。 |
| 再次 DM | seq683，qpc240685325006，UI thread；explicit-pass-through/manual/interaction-events=1 | 后一次初始化成功；不是所有尝试都固定权限失败。 |
| 再离开 | seq684，qpc240693019452，drawing thread；leave-move-mode | 初始化后约0.769秒又离开。 |

该粘贴在 seq681 之后没有 rts-batch，全段没有 pan-register、pan-reject、touchpad-hit-test 或 touchpad-contact。它没有证明用户每个动作的精确 QPC，且 rts-batch 排除 Pen，因此仅据它不能断言“所有 RTS callback 都在此刻停止”。

`ContactInput::PublishMove`（Draw3.ContactInput.cpp:834–883）找不到匹配 Producing route 必返回 false；成功则确实更新 mailbox 和导航历史。生产 `kInterruptedStrokeReconnectSimulationEnabled=false`，不存在本次 simulation 吞点却返回 true 的例外。

## 第二份新日志：窗口输入/光标与 RTS 接触流分离

另一轮日志的 HWND=0x1B1082、UI thread=21728、drawing thread=10892；不能把两轮的 seq 或线程身份拼成一次运行。

- Cursor seq24925–26038：鼠标完整 Down/Move/Up，tick24399453–24399671。窗口 WM_LBUTTONDOWN/Move/Up 均 accepted=1；frame 仍运行，明确 selection=0、pan=0。该片段没有对应 rts-input Down/Packets/Up。
- Cursor seq26039 起，Pointer id513/514/515 和随后516/517/518 有真实 Touch Down/Move/Up；窗口持续收到，touch-begin 记录为 tracked=0/count=0，片段仍没有对应 RTS 到达/发布记录。
- 日志存在有界输出 contention 丢弃，不能声称字节完整、绝无任何漏行；但不能把 UI 的 accepted-mouse 误作 RTS 已成功 PublishDown。
- frame 中的 mouseLifecycle 是速度橡皮的 HasPosition/NeedsAnimation，不是笔画接触数量；touch-begin tracked=0 是 Pointer 路径的视觉屏障参数，不是它自己证明 consumer 拒收。上述字段不单独充当根因证明。

源码解释：正常 RTS 档的 WM_MOUSE 只更新光标（Draw3.WindowControl.cpp:2635 起），真正绘制 Down 由 RTS StylusDown→Coordinator 发布（Draw3.RealtimeStylus.cpp:1180–1288）。笔 Pointer 也可独立 PublishPenCursorSample（Draw3.WindowControl.cpp:2398–2420）。因此“hover 正常/鼠标 Down accepted”与“原绘制接触通道中断”可以同时成立；不需要将它解释成墨迹物理参数或渲染线程整体卡死。

## 当前最应调查的代码边界

1. **DM 与 RTS 共用主画布 HWND。** Host 在 Draw3.Host.cpp:2464 将 RTS 绑定主画布；TouchpadNavigationOwner::Initialize 在 Draw3.WindowControl.cpp:387 为同一 window 创建 viewport，并在 :399–400 Activate/Enable。新输入系统注册本身发生在任何 PTP SetContact 之前，符合“没碰触控板也触发”的时间条件。
2. **退出移动模式未退休 manager。** SetTouchpadNavigationEnabled(false) 在 :842–851 清 bridge 并发 owner 更新；Apply 在 :421–435 仅 Stop/Disable/Reset viewport。真正 RemoveEventHandler/Abandon/manager Deactivate 位于 Shutdown（:466–477），通常到 Host::StopInputProducer（Draw3.Host.cpp:2568 起）才执行。故“业务 enabled=false”不能当成“已经撤销 HWND 上的系统输入注册”。这是持续跨模式故障的高优先级生命周期候选，尚未证明在本机就是唯一恢复条件。
3. **初始化失败未定位具体阶段。** :403 只有最终 HRESULT；Shutdown 结果也没有出现在该日志里。E_ACCESSDENIED 的发生点以及失败后是否有剩余系统注册，现有日志不能回答。
4. **Host 只在启动选择输入后端。** RTS 初始化 ready 后继续作为唯一 producer；触控板 Attach/Activate 没有验证既有 RTS 仍能收到 contact 的共存握手。补 viewport flags、frontend 状态不等于补上这条实际共存证据。

当前源码没有证明 Windows API 规定 DM 与 RTS 必然互斥。Microsoft 关于 RTS/WM_TOUCH 的互斥说明不能直接替代 DM/RTS 的结论。这里的优先级来自新增注册路径、现场时间顺序和跨模式症状的共同指向，而不是将未找到的 API 保证写成事实。

## 上一轮为何没有解决/测试为何没有暴露

- INPUT 只承诺已关联 contact 对 UI 线程可见，不直接保证 RTS 专属 callback；EXPLICITHITTEST 只适用于 RUNNING/INERTIA 中新的 WM_POINTERDOWN。上一轮把这些范围外推成“所有 RTS/鼠标已恢复”的依据不足。
- `draw3_contact_tests.cpp:2097–2105` 自定义 deliverThroughPolicy：看到两个 flags 就直接调用 ordinary；ordinary 直接 PublishDown/Move/Up。它验证了模型预设和下游 Coordinator，没有验证系统是否真实递送。
- production frontend probe（Draw3.WindowControl.cpp:733–755）也使用 fake viewport；数值 CanvasPanProbeRig 绕过 RTS/实际 DM owner。
- 隐藏 HWND 测试还有确定撞号：WindowControl.cpp:44 的 DM pump 与 HiddenWindowTest.cpp:84 的 capture probe 同为 WM_APP+0x3D5；HiddenDrawpadWindowProc:88–93 会先处理并返回，挡住测试窗口的 DM owner pump/stop。这解释测试覆盖缺口，不能当产品真实故障根因。
- DM 官方允许合并同类 callback，而当前未确认 Running 时的 Content 只缓存、Ready 可被忽略，短 PTP 手势存在漏处理风险。它尚未在现场单独证实，也不能解释未碰 PTP 的全部拖动失败。

## 下一步应取得的决定性证据（本轮不改代码）

现有新日志已经避免继续在惯性公式/摩擦或 SetContact 伪 Running 上盲修。进一步确定到具体 API，需完整的 Cursor+CanvasPan 启动至故障边界，或后续获准进行最小诊断对照：同一输入后端下，只比较是否执行真实 DM 初始化/注册，并观察 RTS contact 恢复；同时逐阶段记录 Initialize 和退休 HRESULT。不要把关闭“触控板惯性”当禁用 DM，当前它只门控惯性尾段。

本轮未执行该源码对照或 GUI，未宣称唯一 API 根因已证明、全部设备已恢复。相关独立研究见 drag-failure-frontend-investigation.md 和 drag-failure-admission-investigation.md。

## 官方资料

- [Viewport options](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/ne-directmanipulation-directmanipulation_viewport_options)
- [Processing input with DirectManipulation](https://learn.microsoft.com/en-us/windows/win32/directmanipulation/directmanipulation-processing-input-with-directmanipulation)
- [AddEventHandler](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-addeventhandler)
