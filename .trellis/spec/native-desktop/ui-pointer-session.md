# UI 指针会话与拖动 Raw 补充

## 1. Scope / Trigger

修改 UI3 Bar 普通点击/连续交互、PPT PageControl 拖动/长按、WM_TOUCH 转译、鼠标 Raw Input 注册与边缘光效时适用。源任务为 `.trellis/tasks/archive/2026-10/10-17-ui-pointer-session`，点击回归、一秒取消与边缘/静默修正分别以其 `correction.md`、`timeout-correction.md`、`edge-and-stream-correction.md` 为准。保留现有键盘/鼠标 hook；Draw3 绘制输入不经过该 UI 会话。

## 2. Signatures

公共实现为 `Inkeys/Inkeys/Input/UiPointerInput.h/.cpp`，命名空间 `Inkeys::Input::UiPointer`。

- `Sample { kind, source, POINT screen, ULONGLONG tick, DWORD messageTime, uint64_t generation, uintptr_t device, DWORD contact, cancelReason }`。
- `Kind::{Down, Move, Up, Cancel}`；`Source::{Mouse, Touch, Pen}`。Pen 保留 PageControl 既有兼容消息路径，不要求 Raw Mouse。
- `MouseGesture::{Begin(nativeDown), BeginDrag(), AcceptNative(sample), AcceptRaw(sample), SourceReplaced(), Latest(), Active(), Dragging(), Reset()}`：客户端上下文拥有的鼠标原生会话和可选 Raw 仲裁。只读 `RawBound()/RawDisabled()/RawActivityTime()` 供活动诊断。
- `MouseChannel::{BeginMouse(screen,nativeMessageTime), TryPop(sample), FinishMouse(generation), Cancel(reason), SetWakeWindow(hwnd,message)}`：拖动专用的可选私有补充传输。
- `MouseChannel::{BeginTouch(generation), EndTouch(generation)}`：跨消费者非鼠标租约，Touch 和 PPT 笔兼容共用。
- `Session::{Begin(down), Accept(sample), Latest(), Active(), Reset()}`：触摸/笔或客户端归一化后的来源、设备、触点和代次。
- `AttachRawMouse(sink)`、`DetachRawMouse(sink)`、`RawMouseAvailable()`、`HandleRawMouse(lParam,activity)`、`CheckInputDesktop()`。
- `Window/Window.MessageDispatch.h` 的 `Scope(MSG)` 保存当前原始派发；`TryGet(hwnd,message,wParam,lParam,sample)` 精确验证当前消息，`PostedTick(time,now)` 换算 DWORD 时间回绕。

## 3. Contracts

### 原有入口与可选补充

- Bar 原生鼠标 Down/Move/Up 继续通过 HiMsg 进入交互线程，窗口线程固化完整屏幕点、消息时间与原生按压代次，并以值型 MouseGesture 保存 Down 当时的来源准入快照；交互线程只接纳仍有效的快照，不能让排队 Down 跨已结束的 Touch/Pen 接管复活。实际业务归属和 Raw claim 仍只在交互线程推进。普通按钮不申请 Raw 会话。PageControl 原生消息立即进入自己的 owner 上下文，不等待 Raw Down，也不路由到 Bar 业务线程。
- `MouseGesture::Begin` 接受非零原生代次的 Down，立即取得业务归属，与 Raw 是否注册、是否收到/匹配 Down 无关。`AcceptNative` 的有效 Up 可以结束点击或拖动。不得在 HiMsg 入队或消费层无条件丢弃原生 Up。
- 只在连续交互入口准备可选 `MouseChannel`。主按钮、Page/背景与粗细浮窗等仍按各自原有阈值判定实际拖动；候选阶段可以用 Raw Move 提供越界轨迹，`BeginDrag()` 之前的 Raw Up 不能提交点击、折叠或翻页。已在 Down 开始连续改值的选色板/预设拖选、直接 FineDial 等按原语义进入连续操作。
- Raw Down 只建立补充身份，不再次执行业务 Down。接受的 Raw 样本通过 `Latest()` 保留原生业务代次/设备/触点身份。客户端必须依据接受后的 Kind、坐标与时间执行业务，Cancel 不得沿用原消息的 WM_LBUTTONUP 提交；实际拖动中比最近移动更旧的 Up 仍结束会话，但位置与时间不回退。
- 实际拖动的有效原生 Up 与 Raw Up 只能结束一次；结束后忽略迟到终态。旧按压的原生代次和 Raw 身份不得完成下一次按下。结束只退休本会话，不全局 Clear 后续消息。
- `SourceReplaced()` 独立于 Raw claim，捕获跨窗口 Touch/Pen 租约版本；接管即使在两条鼠标消息之间已经开始并结束，旧会话仍失效。活动轮询显式取消业务/反馈。非鼠标租约期间不允许新物理 Mouse Begin；旧 End 不能释放新租约。`AcceptNative` 不能把被接管的普通 Up 当成功终态。

### Raw 传输与失败

- Bar HWND owner 统一注册鼠标 `RIDEV_INPUTSINK`，不使用 `RIDEV_NOLEGACY`。PageControl 不重复注册；光效开关/休眠只停止观察和渲染唤醒，sink 销毁才统一注销。
- `WM_INPUT` 在 owner WndProc 复制，经私有有界通道由客户端消费，绝不调用 HiMsg/Window::Enqueue 转发。provider 锁内不执行业务。
- 可选通道暂保留时间+屏幕点+代次的保守 Raw claim，以免认领无关的历史点击；待匹配最多 1000ms 后产生 generation=0 的补充 Cancel；该期限仅结束补充尝试，不限制原生拖动时长。该匹配不是 Windows 对两类消息元数据相等的保证，不匹配只表示补充不可用，不能阻断原生点击。Raw Up 早于异步消费时通过历史按序重放。
- `RawMouseAvailable()` 只说明注册成功，不能证明当前完整性级别/前台环境可读。`RIDEV_INPUTSINK` 不能视为 UIPI 豁免；不得将本实现宣称为跨权限输入保证。
- 原生和 Raw 读取原始派发 MSG.pt/time，避免历史事件时间配当前光标。私有 POINT 不压缩到 short。无匹配 MSG 的直接派发兼容入口才使用既有回退。
- 队列保持同一来源的轨迹和时间。普通鼠标移动沿原生路径；连续手势 Raw 活动新鲜时由 Raw 提供单一轨迹，保留同毫秒不同点的 excursion，避免双通道重复累加 FineDial 速度。绑定前和补充失败后仍可消费原生移动。有效本代次 native Move 的事件时间晚于最后有效 Raw 活动至少50ms时，可接续移动；50ms只切换移动权威，不是按住期限，不生成 Up/Cancel，不关闭 Raw Up 监听。迟 Raw 或旧代次不能刷新活动时间。有效同坐标 Raw 仍刷新 freshness；native 接续即使同坐标不产生业务移动，也推进严格 Raw 回放下限。下限及同毫秒历史 Raw 不再回放，更新的 Raw 可恢复权威。迟绑定同样跳过已消费原生时间下限；此前被原生合并的 excursion 无法可靠重建。静止/停留仍使用墙钟，释放后惯性不占会话。
- 未绑定 Raw 的取消（generation=0）只撤销补充能力，即使原生业务已经 BeginDrag 也继续接受原生 Move/Up。非零取消代次证明该通道实际取得过 Raw 归属，包含队列溢出丢失初始 Down、消费者尚未见 Down 的情况；只有此类已绑定的实际拖动传输失败/溢出才显式 Cancel。取消语义统一由 MouseGesture::AcceptRaw 裁决，两个客户端不得以 `|| Dragging()` 扩大取消；不能伪造 Up 或启动惯性。旧时间/已知旧代次取消不得影响新手势。桌面失效、模式/工具版本变化、隐藏/销毁与来源接管仍显式取消。
- `hDevice == nullptr` 可能是精密触摸板，不据此丢弃；触摸/笔提升副本按来源签名过滤。通知可合并，数据不以通知次数计数；活动计时/轮询兜底，不新增长期线程。

### 触摸与客户端业务

- Bar 触摸保持 WM_TOUCH -> 带来源的 ExMessage -> HiMsg；PageControl 保留本地 WM_TOUCH 转译。活动触点按 hSource+dwID+代次区分，不修改通用 HiMsg；触摸位置来自接受的样本，不能读取系统鼠标替代。
- Bar 主按钮移栏/停靠、预设拖选、二维选色板、粗细滑轨/预览相对拖动/数值浮窗与 FineDial 复用补充机制，各自保留阈值、即时更新、提交和回退语义。
- PPT Previous/Next 点击和长按只走原生/本地触摸入口，不申请 Raw；移出按钮、Up、捕获取消或 workspace 失效停止重复。PPT DragHandle 立即拖动，Page/真实圆角背景按系统阈值进入；Whiteboard 不获得 PPT 拖动区。
- PPT 原生 Up 先验证归属再应用末段位置。漏收旧 Mouse Up 时新的真实 Mouse Down 可以取消旧按压并重新命中，不伪造点击；Touch/Pen 活动期间继续拒绝 Mouse。pair 直移/回退保持 try_lock、latest-wins、pending 和原持久化合同。
- 捕获保留兼容语义，但 GetCapture、全局按键缓存与 GetAsyncKeyState 不决定正常释放。笔兼容保留既有捕获取消；Raw 不能接管 Pen Up。

### 主按钮抓取与活动诊断

- Seek 使用绝对屏幕点减抓取偏移再限位。`ScaleBarDragGrabOffsetForZoom(screenOffset, oldZoom, newZoom)` 保留按下时逻辑抓点，仅按实际呈现的新旧 zoom 比例缩放。显示 serial/monitor 变化仍重基准布局与 tracker，但不能用当前 pointer 减已 clamp/滞后的呈现点重抓，否则可能锁存边缘距离。形态屏障仍不重抓。
- 活动 Seek 使用既有 logger 输出 `[UiPointerDrag]` 起止与每500ms摘要；无输入等待时也记录。native/Raw 计数明确是交互消费者拿到的样本与策略接受数，不代表 Raw provider 的全局接收量。对比 accepted、真实 cursor（仅诊断）、grab、desired/presented、applied/presentedTranslation、display serial/zoom、foreground 与指定 HWND owner 的 capture。不能在交互线程用 `GetCapture()==nullptr` 断言 owner 没有捕获；用 `GetGUIThreadInfo(ownerThread)` 并记录成功标记。
- Raw 与原生两流同时缺失时没有可接续的位置来源。限频日志和无窗口测试用于界定问题，不证明 UIPI 已绕过；同屏普通前台也可复现的几何问题不能直接归因权限。

## 4. Validation & Error Matrix

| 场景 | 结果 |
| --- | --- |
| Raw 注册失败/无 Down/匹配失败/启动超时 | 原生点击与已开始的原生拖动都继续；1000ms 不限制拖动时长 |
| 主按钮/Page 候选尚未过阈值 | Raw Up 不点击；有效原生 Up 按原规则完成 |
| 拖动 Raw Up 已先于消费者入队 | 保留轨迹，跨阈值后同一归属结束一次 |
| 原生 Up 与 Raw Up 任意先后 | 第一有效终态结束，后到不重复业务 |
| 快速下一次按下/迟到旧 Up | 原生代次和 Raw 身份隔离，不串入新手势 |
| Touch/Pen 接管后快速结束 | 旧普通鼠标会话仍显式取消，不依赖 Raw 通道 |
| 未绑定补充取消/历史溢出（代次0） | 仅撤销补充，原生实际拖动继续 |
| 已绑定实际拖动传输失败/队列溢出（非零代次） | 明确 Cancel，无成功 Up；未拖动候选保留原生点击 |
| 有效 Raw 连续活动/同毫秒轨迹 | Raw 单一移动权威，保留不同点与 excursion，不叠加原生轨迹 |
| 已绑定 Raw 静默、有效 native Move 晚至少50ms | native 接续并推进回放下限；保留 Raw Up，静默不作终态 |
| native 接续后收到旧/同毫秒 Raw，再收到更新 Raw | 旧段不重放；更新 Raw 恢复单一权威 |
| 边缘限位后重复显示发布或实际 zoom 更新 | 原逻辑抓点保持；不得从受限呈现位置重抓 |
| Raw/native 都没有新样本 | 不猜 Up，不制造输入；活动摘要记录停顿供实机判别 |
| Raw 晚于已接受原生 Move 才绑定 | 不重放原生时间下限及同毫秒；更早被合并的路径不作恢复保证 |
| 桌面/模式/workspace/销毁取消 | 清理反馈、归属及当前补充，不提交旧业务 |
| 光效关闭或休眠 | 共享注册保留，光效唤醒仍独立门控 |

## 5. Good / Base / Bad Cases

Good：Raw 未到而原生 Down/Up 正常，按钮照常执行；越界拖动 Raw Up 先结束后迟到原生 Up 被忽略。Base：原生窗口消息始终是命中、点击和按压入口。Bad：把 BeginMouse 返回值作为点击准入、丢弃所有原生 Up，或拿 Raw 候选 Up 提交主按钮折叠。

## 6. Tests Required

- 生产 MouseGesture/Session/MouseChannel 的无 HWND 回归：Raw 不可用/缺失/不匹配/超时下原生点击及实际拖动跨999/1000/1500/5000ms继续、候选 Raw Up 禁止提交、早到释放、双终态顺序、相邻代次、单一移动来源、迟绑定时间下限、终态不倒退、溢出前未消费 Raw Down、Touch/Pen 接管开始结束及旧 End、排队 Down 来源快照、绑定后静默接续、50ms事件边界、静止 Raw freshness/native 回放下限、迟 Raw 与恢复 Raw、DWORD时间回绕、native 接续期间 Raw Up。
- 生产 Bar 抓取纯策略回归覆盖较大越界后回归、重复相同 zoom 的显示发布和真实 zoom 改变后的逻辑抓点保持；静态审查 Seek 实际调用该策略。它不能替代 HWND/设备输入验证。
- PageControl 生产阈值与共享策略测试：候选点击、Raw 越界转拖动、原生 Up/Raw Up 顺序、旧终态；现有 pair、长按、触点 source/primary/fallback 继续通过。
- 完整 InkeysRepo.sln Debug|ARM64、InkeysHeadlessTests --no-window 与适用的初始化前生产探针；保留编码/BOM/CRLF，Raw/Touch 局部任务须核对 hook 源码与安装/卸载未改变；单独授权的 hook 任务另遵守 [mouse-hook-and-autocollapse.md](mouse-hook-and-autocollapse.md)。
- 管理员前台、真实 Win7 触摸越界、触摸板、笔、UAC/锁屏与跨窗口设备输入需单独验证。无窗口 PASS 不能证明实际 WM_INPUT 接收能力或全面绕过 UIPI。

## 7. Wrong vs Correct

```cpp
// Wrong：普通点击被可选 Raw 能力阻断，原生 Up 被丢弃。
if (!channel.BeginMouse(down.screen, down.messageTime)) return;
DiscardEveryNativeUp();
if (rawEverBound) IgnoreEveryNativeMoveForever();

// Correct：原生消息建立并结束交互；仅连续操作准备私有补充。
gesture.Begin(nativeDown);
if (continuousCandidate) channel.BeginMouse(nativeDown.screen, nativeDown.messageTime);
if (crossedOriginalThreshold) gesture.BeginDrag();
if (gesture.AcceptNative(nativeSample)) ApplyByKind(gesture.Latest());
if (continuousCandidate && gesture.AcceptRaw(rawSample)) ApplyByKind(gesture.Latest());
```
