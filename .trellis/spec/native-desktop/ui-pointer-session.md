# UI 指针会话与拖动 Raw 补充

## 1. Scope / Trigger

修改 UI3 Bar 普通点击/连续交互、PPT PageControl 拖动/长按、WM_TOUCH 转译、鼠标 Raw Input 注册与边缘光效时适用。源任务为 `.trellis/tasks/archive/2026-10/10-17-ui-pointer-session`，点击回归、一秒取消与边缘/静默修正分别以其 `correction.md`、`timeout-correction.md`、`edge-and-stream-correction.md` 为准。保留现有键盘/鼠标 hook；Draw3 绘制输入不经过该 UI 会话。

## 2. Signatures

公共实现为 `Inkeys/Inkeys/Input/UiPointerInput.h/.cpp`，命名空间 `Inkeys::Input::UiPointer`。

- `Sample { kind, source, POINT screen, ULONGLONG tick, DWORD messageTime, uint64_t generation, uintptr_t device, DWORD contact, cancelReason }`。
- `Kind::{Down, Move, Up, Cancel}`；`Source::{Mouse, Touch, Pen}`。R2用户已批准五窗独立TouchContext：UI的笔/触摸业务按值进入同一Touch router；R3起来源按模式选取（PointerMode只认Pointer，LegacyTouchMode只认WM_TOUCH，见“触摸与客户端业务”首条）；Source::Pen枚举保留通用用途，不再作为Page兼容鼠标业务入口。所有带Pointer签名的WM_MOUSE在Bar/Page一致过滤，低位Pen/Touch分类仅诊断。
- `MouseGesture::{Begin(nativeDown), BeginDrag(), AcceptNative(sample), AcceptRaw(sample), SourceReplaced(), Latest(), Active(), Dragging(), Reset()}`：客户端上下文拥有的鼠标原生会话和可选 Raw 仲裁。只读 `RawBound()/RawDisabled()/RawActivityTime()` 供活动诊断。
- `MouseChannel::{BeginMouse(screen,nativeMessageTime), TryPop(sample), FinishMouse(generation), Cancel(reason), SetWakeWindow(hwnd,message)}`：拖动专用的可选私有补充传输。
- `MouseChannel::{BeginTouch(generation), EndTouch(generation)}`：跨消费者非鼠标资源租约；五窗各自TouchContext独立，只有已接纳目标申请/释放自己的租约，不等于新增全局Touch业务状态。
- `Session::{Begin(down), Accept(sample), Latest(), Active(), Reset()}`：触摸/笔或客户端归一化后的来源、设备、触点和代次。
- `AttachRawMouse(sink)`、`DetachRawMouse(sink)`、`RawMouseAvailable()`、`HandleRawMouse(lParam,activity)`、`CheckInputDesktop()`。
- `Window/Window.MessageDispatch.h` 的 `Scope(MSG)` 保存当前原始派发；`TryGet(hwnd,message,wParam,lParam,sample)` 精确验证当前消息，`PostedTick(time,now)` 换算 DWORD 时间回绕。

## 3. Contracts

### 原有入口与可选补充

- Bar 原生鼠标 Down/Move/Up 通过按值 SharedInputEnvelope 进入共享交互 worker，窗口线程固化完整屏幕点、消息时间与原生按压代次，并以值型 MouseGesture 保存 Down 当时的来源准入快照；交互线程只接纳仍有效的快照，不能让排队 Down 跨已结束的 Touch/Pen 接管复活。Bar 业务归属和 Raw claim 只在共享 worker 推进。普通按钮不申请 Raw 会话。PageControl 在 HWND owner 归一化来源、代次和原生采样，再将带精确 captureTicket 的 SurfaceInputEvent 投递同一 worker；业务回调不等待 Raw Down。
- `MouseGesture::Begin` 接受非零原生代次的 Down，立即取得业务归属，与 Raw 是否注册、是否收到/匹配 Down 无关。`AcceptNative` 的有效 Up 可以结束点击或拖动。不得在共享邮箱入队或消费层无条件丢弃原生 Up。
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
- `hDevice == nullptr` 可能是精密触摸板，不据此丢弃；触摸/笔提升副本按完整Pointer来源签名统一过滤，不能按低位在同一接触里拆分Pen/Touch。通知可合并，数据不以通知次数计数；活动计时/轮询兜底，不新增长期线程。

### 触摸与客户端业务

- **来源仲裁（R3，PointerMode）**：Pointer 是一次接触的根，WM_TOUCH 与带 `0xFF5157xx` 签名的兼容 Mouse 都是系统处理 Pointer 时派生的兄弟副本，无法在不借助时间的前提下判断“另一个还会不会来”。因此来源取根并按类型排除，不使用任何时间窗：
  - `Touch::PointerMode()`：可选 `GetPointerInfo`（GetProcAddress，无硬导入）可用且未设置 `INKEYS_UI_TOUCH_LEGACY=1`。模式为进程级，运行期不切换；Win7/无 API 为 LegacyTouchMode，行为与 R2 完全相同。
  - PointerMode 下业务来源只有：PT_TOUCH/PT_PEN 的接触阶段 Pointer（DOWN、`INCONTACT` 的 UPDATE、UP）和无签名的真实 Mouse（含 Raw 补充）。`WM_TOUCH` 读取并关闭句柄后丢弃（含画布接触派发到 UI 窗的那部分）；签名兼容 Mouse 沿用 Bar/Page 既有过滤。
  - `Touch::HandlePointer(origin,msg,wParam)` 由 Bar/Page 窗口过程调用，返回 true 时窗口过程直接 `return 0`，使系统不再为该接触派生 WM_TOUCH/兼容 Mouse/长按右键；悬停、PT_MOUSE、查询失败返回 false 保持默认处理。
  - 接触以值型 `TOUCHINPUT` 进入 `DispatchDecoded(origin,…,originIsTarget=true)`：`hSource=sourceDevice`、`dwID=pointerId`、`dwTime=info.dwTime`、`x/y=ptPixelLocationRaw*100`、PRIMARY/PEN 位取自 Pointer。与 Bar 触觉 `ObserveBarHapticPointer` 采集的是同一条消息，`BindDown/Touch` 的 device/time/point 精确相等。目标 = 接收窗，不调用 `WindowFromPoint`、不做跨窗转交；Move/Up 仍按接触身份全局找 owner，因此抬起落到别的窗口也能结束 Down 的路由。
  - 每次 DOWN 新建 route generation（笔的 pointerId 在一次感应范围内持续，不能当单次按下身份）。第二键/笔桶键 DOWN 只吞不入业务；`POINTER_FLAG_CANCELED` 撤销路由不产生成功 Up；同设备新的主接触、同 id 新 Down、Up/Update 查询失败都会先撤销疑似丢失终态的旧路由。`WM_POINTERCAPTURECHANGED`/`LEAVE` 暂不作为终态（SetCapture 重入可能同步触发，且窗口外拖动需继续），属已知待观察项。
  - PageControl 的真实 `WM_MOUSELEAVE` 若发生在活动 Touch/Pen 接触期间，仍排入队列（边缘光效需要离开通知），但 worker 不得据此清除该接触的按压/长按状态；它是迟到的悬停追踪通知，无签名，签名过滤拦不住。
  - 依据：research/probe-first-local-capture.md（接触阶段不转交时 195 次接触内派生消息为 0；WM_TOUCH 是否出现取决于机器+系统+驱动）。

- 五窗各自接收WM_TOUCH、独立Context，调用共享解码/转交规则；按真实Touch点选择目标后，由目标投值型消息到原共享worker。origin HWND不必等于target HWND，活动触点按hSource+dwID+窗口注册/本地代次区分，不修改HiMsg、不转发原handle；触摸位置来自样本，不读取系统鼠标替代。
- Bar 主按钮移栏/停靠、预设拖选、二维选色板、粗细滑轨/预览相对拖动/数值浮窗与 FineDial 复用补充机制，各自保留阈值、即时更新、提交和回退语义。
- PPT Previous/Next 点击和长按只走原生/本地触摸入口，不申请 Raw；移出按钮、Up、捕获取消或 workspace 失效停止重复。PPT DragHandle 立即拖动，Page/真实圆角背景按系统阈值进入；Whiteboard 不获得 PPT 拖动区。
- PPT 原生 Up 先验证归属再应用末段位置。漏收旧 Mouse Up 时新的真实 Mouse Down 可以取消旧按压并重新命中，不伪造点击；Touch/Pen 活动期间继续拒绝 Mouse。pair 直移/回退保持 try_lock、latest-wins、pending 和原持久化合同。
- 捕获保留原目标语义，但 GetCapture、全局按键缓存与 GetAsyncKeyState 不决定正常释放。统一Touch按真实Up/Cancel结束，Raw不能接管Touch终态；旧route/注册/捕获票据不能释放新手势。

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
## 场景：主栏/分页间歇输入诊断（2026-11-02）

### 1. Scope / Trigger

R2全程高漫M6日志呈现“Touch已进入Bar但无业务接受”和“Bar有Pointer却无Touch接纳”两种形态。新增诊断仅观察，不把其当作修复输入策略的授权；不能让触觉设备失败代替UI准入判断。

### 2. Signatures

- `Input/UiPointerDiagnostics.h`，命名空间 `UiPointer::Diagnostics`：`Enabled`、`ErrorScope`、固定容量 `MoveLimiter`、统一 `Log/Raw`、`LeaseEvent`。
- `MouseChannel::DiagnosticId() const noexcept -> uintptr_t` 返回诊断通道身份；客户端输出同一标识与 HWND/role，供关联共享租约owner，不参与业务比较。
- 沿用 `Experimental.Inkeys3.ConsoleOutput.Haptics`，默认false、重启生效；输出至已有DiagnosticOutput的Error通道，不新增开关/线程/普通文件sink。

### 3. Contracts

- 公共记录以 `[PenHaptics] event=ui_input v=1` 标识，包含tick/tid/role/hwnd/stage/reason、message名称和值、source/phase、contact/generation/device、原采样时间/点、suppressed/truncated；阶段细节包含channel/owner、capture票据、窗口rect、真实已呈现命中/语义版本。
- 原始WM_TOUCH批次和解码后的触点不是同一语义。入口不二次调用GetTouchInputInfo；成功仅沿现有读取记录逐点Down/Up/选择和拒绝，失败记录原错误。不能用原始WM_TOUCH头虚构Down或用GetMessageExtraInfo把Pointer武断标成Mouse。
- Down/Up/Cancel与拒绝/业务结果保留，Move/PointerUpdate/纯Move批次有界限频并报告suppressed；原始点/时间使用已有MSG快照，回退来源必须标明。不能用限频或固定按住时间改变session生命周期。
- lease Begin/End/Cancel/析构记录requester/owner及两侧generation；owner_snapshot=before_mutation明确owner字段是操作前快照，acquired后的新owner为requester，旧代或非owner忽略有原因。锁内只复制小状态，锁释放后格式化和入诊断队列。
- Bar记录原始消息与过滤、Touch选择、capture与队列/消费者会话、实际冻结命中和业务确认；PageControl四窗口记录角色、PPT/白板mode、可见/locked/语义版本、Pen与Touch准入/取消和方向动作/repeat。UI接受/回调派发与可选触觉queued/sent分开表述，派发不冒充PPT已翻页ACK。
- 诊断关闭快速返回，无新增昂贵系统查询/格式化/分配；开关判断必须覆盖系统查询实参。系统查询/输出保持GetLastError；不新增SendMessage/WindowFromPoint/合成输入、capture变更、默认消息处理或新硬Pointer依赖。
- 沿现有非阻塞有界输出，截断/丢弃统计可见。日志缺失必须结合原始入口/拒绝和掉样判读，不能从无bar_touch推出OS未送Touch，也不能从bar_click accepted=0推出UI失败。

### 4. Validation & Error Matrix

| 情况 | 需要可区分的证据 |
| --- | --- |
| 兼容Mouse被过滤 | 接收role/原消息/extraInfo及filter原因 |
| Pointer存在而Touch接纳缺失 | Touch批次是否到达、读取/选择、lease/activeSession首个拒绝 |
| 右下Touch进入Bar | 接收窗口rect、原屏幕点、实际冻结按钮/主体命中、channel与capture |
| 其他窗口持有非鼠标lease | requester/owner/generation与后续End/Cancel，不能只写generic rejected |
| stale/capture/语义取消 | 会话/票据/版本及首个实际原因，不能伪造Up |
| 高频Move/长接触 | 有界suppressed，终态仍可见，业务返回值不变 |
| 触觉绑定失败 | 与UI业务接受分开，原点击照常 |

### 5. Good / Base / Bad Cases

Good：一组M6操作可沿raw→touch_select→lease→capture→queue→consume→action或首个reject定位停点。Base：关闭开关时沿原输入路径，无诊断系统采样。Bad：用日志函数再次读取触摸handle、在provider锁内打印、把Move包都叫Down、只输出无窗口映射的owner地址或在logger里修复capture。

### 6. Tests Required

调用真实诊断helper验证关闭/LastError、固定容量/Move限频/终态释放与suppressed、格式/截断；保留既有UiPointer来源/session/lease回归。完整InkeysRepo.sln Debug|ARM64、--ui3-input-worker-test与适用早退探针、headless --no-window；确认BOM/CRLF与业务分支返回不变。真实M6现象仍需下一次用户日志，不能把新增日志或测试通过写作已修复。

### 7. Wrong vs Correct

Wrong：没有bar_touch便认定驱动不提供WM_TOUCH；每个WM_TOUCH都输出Down并刷满队列；在BeginTouch锁内调用同步日志。
Correct：批次到达/一次读取/逐点选择和lease结果分层观测；纯Move限频，边沿完整；锁内快照、锁外沿既有异步队列输出。
## 场景：五套独立TouchContext与跨窗转交（2026-11-03）

### 1. Scope / Trigger

触笔持续in-range跨Bar和四Page窗，原Touch接收HWND可能保持旧窗；Page逐条Pen/Touch分类又曾丢Up留下capture/lease。用户批准五个窗口状态独立、处理规则共用、薄层转交。此合同覆盖旧Page Pen兼容鼠标业务入口；不新增分页触觉、并发多指业务或全桌面输入路由。
**当前覆盖限制（同日追加日志确认）**：另一份旧版日志有87组主栏Pen兼容Mouse Down/Up与原生Pointer，却没有可见WM_TOUCH；旧版左右分页通过同签名Mouse成功。当前HandleMessage只处理WM_TOUCH，Pointer只旁观触觉，统一过滤后该流无业务入口，分页有条件性回归风险。详见当前任务research/additional-log-coverage.md。不能将本合同或headless PASS解释为Pointer-only场景已支持；也不能仅凭缺失日志断言驱动绝不生成Touch，须结合掉样/版本/注册结果。补充接入设计尚未实施。
追查Touch缺失时，必须区分“调用过RegisterTouchWindow”与返回成功/IsTouchWindow实际读回；调查时IdtMain未保存注册BOOL的证据盲点，已由下文注册诊断补齐接线；现场结果仍待新日志。不能从WM_TOUCH缺失反推厂商Windows Ink复选框，也不能把PT_PEN反推为该开关必开。Microsoft TOUCHINPUT定义TOUCHEVENTF_PEN=0x40，已有设备flags0x5a验证了Pen Touch可存在；这种支持不等于每次Pen Pointer都保证另发Touch。原生Pointer/窗口注册/驱动应用级配置须分别取证，详见当前任务research/no-touch-cause-investigation.md。

### 2. Signatures

- `UiPointer::Touch` 位于`Input/UiTouchRouting.h`，实现薄接线在现有`UiPointerInput.cpp`。
- `Context`每窗一份，拥有本地Preparing/Active、key/本地generation/capture相关身份；`Identity{window,registration,generation}`，`Packet{input,screen,messageTime,tick,origin,kind}`。
- `Probe{Hit::{Miss,Blocked,Accept},semantic}`；`Adapter{probe,deliver,cancel}`；`RegisterTarget/RetireTarget/DispatchDecoded/HandleMessage`。
- `CancelTargetWindow`只退休该逻辑目标；`CancelWindow`还处理原接收窗的生命周期；`RequestCancel/DrainCancels`传完整64位identity，私有wake仅信号。`QueryScope/QueryActive`只限定Touch原生命中查询。

### 3. Contracts

- 固定五槽登记指向Bar及四Page本地Context，不另存global currentTouch/active gesture业务FSM；同device/id先找已拥有的本地Context。原非Mouse资源lease仍保护Mouse/Touch互斥，不是第二份Touch业务状态。
- 原WndProc单次GetTouchInputInfo/CloseTouchInputHandle，route只传值型Packet。合格Down在所有内部锁外WindowFromPoint(Touch屏幕点)，唯一候选必须是五窗精确HWND；再以已呈现Probe复核，Miss/Blocked/外部/设置/画布不接纳、不后层搜索、不抢lease/capture。
- Bar完整probe与成功present同事务发布，包含同帧工具门、Grip/Body/Rigid映射、More及所有属性/浮窗/FineDial合法占用；不能用窗口rect/装饰阴影替代。Page复用同锁InputPresentation与scene.SamplePointer。Touch查询Scope内可见locked背景是阻挡，常规Mouse NCHITTEST策略不变。
- WindowFromPoint和SetCapture可同线程重入，回调前不持registry/provider/scene锁；之后核注册代次、本地mutation/identity和语义。Preparing已在目标本地保留，旧取消不得抹掉重入新代。
- Move/Up锁定Down owner，不因悬浮或当前候选改变而改目标；Down在UI外，后续Move不启动按钮。primary/fallback、不同hSource身份和单UI接触互斥保留。
- Page与Bar按完整IsPointerGeneratedMouseMessage统一过滤所有Pointer签名WM_MOUSE；Pen/Touch低位只诊断。真实Mouse/触摸板/native+Raw原业务不变，不把Mouse类别枚举或资源租约作为无关清理删除。
- worker接受的route/capture身份整体同锁快照；取消Mailbox按注册/本地代次保留有效新意图，零票据也必须回native owner退休Context/lease。native收到新输入前先drain取消，旧Up/取消/注销/同值HWND复用不得清新代。
- capture变化只取消实际逻辑target的旧route，不能因origin相同取消正在转交给别窗的Preparing；隐藏/销毁/注销等源生命周期则取消该来源关联route。取消不生成成功Up，不用固定timeout或GetAsyncKeyState猜终态。
- UI来源只认完整Touch身份与本地route，不依赖提供方dwTime必须严格递增；保留原生时间来源/回绕规则。同代Up略早于最后Move仍须结束且不倒退轨迹，旧于本次Down的可辨旧终态不得结束新代。
- PPT Down立即翻页、原开关长按无Move继续/移出或Up停止；白板有效Up单次；Bar连续属性/拖栏、可选触觉凭据及真实接受点保留。触觉缺失不得阻UI。

### 4. Validation & Error Matrix

| 情况 | 结果 |
| --- | --- |
| Touch投旧A、真实命中B | A不先申请资源，B本地接纳；后续Move/Up仍交B一次 |
| WFP非五窗、ProbeMiss/Blocked/旧注册 | 不Down、不捕获、不向后层/外部猜投 |
| SetCapture使源A丢旧捕获 | 取消A旧目标手势，不误取消Preparing B |
| 源/目标隐藏销毁、overflow、语义失效 | 匹配身份取消、释放lease/capture，零票据也不遗漏 |
| 同代旧一点Up/旧代迟Up | 前者结束不退轨迹，后者不影响新代 |
| 重复Touch或72d/7ad兼容副本 | 同接触最多一次业务，副本不另开Pen Session |
| 只有可见Pointer/兼容Mouse而无Touch | 当前无业务接入，不能标记已覆盖；统一过滤存在分页退化风险 |

### 5. Good / Base / Bad Cases

Good：笔不离感应范围，原Touch窗不变也可依次点Bar/左/右；按住从A移到B仍只由A结束。Base：原目标与实际候选相同沿原业务，Mouse不受影响。Bad：五Context再叠一份全局业务手势、原窗先capture再命中、按低位Pen/Touch拆Down/Up、只worker清状态遗留native lease。

### 6. Tests Required

真实生产转交/注册/adapter及既有worker接缝覆盖sticky origin、Down锁定、外部/Blocked/Miss、同毫秒/时钟来源、dup/旧代、多点primary、capture/probe/cancel重入、同窗新登记、零票据和旧取消后到。Bar完整区域与同帧snapshot须实证；PPT长按/白板Up、Mouse/Raw、Bar可选触觉原回归保留。完整Debug|ARM64与headless，Release|x64产物再交用户一次设备确认；未经GUI验证不写作实机已通过。

### 7. Wrong vs Correct

Wrong：Touch收到哪个HWND就由哪个窗开始业务；Page只放行Pen兼容Down却过滤Touch分类Up；源窗capture变化清掉别窗新route。
Correct：候选+真实已呈现命中选目标，本地Context锁整次接触，完整Pointer副本去重，按完整身份取消/收尾并保留诊断origin与target。

## 场景：Touch注册与Pointer来源诊断（2026-11-03）

### 1. Scope / Trigger

另一块数位板日志只有Pointer/兼容Mouse、未观察到Touch；先核五窗注册结果与实际状态，不改变输入策略。沿用Experimental.Inkeys3.ConsoleOutput.Haptics和ui_input v1；关闭时原注册仍执行，旁路诊断零额外查询。

### 2. Signatures

UiPointerDiagnostics.h：`TouchRegistration(role,HWND,ULONG flags,BOOL result,DWORD failureError)`、`WindowSnapshot(role,HWND,reason,UINT message,UINT32 pointerId=0)`、`PointerDownSnapshot(role,HWND,UINT,WPARAM,entry)`。NativeQueries/testNativeQueries仅作只读查询及headless测试接缝，不进入业务路由。

### 3. Contracts

- 原created里每窗恰好一次RegisterTouchWindow(hwnd,0)，立即保存失败GLE；不重试/改flag/改后续流程。成功registration_error=0且error_applicable=0，不能将残留GLE当失败。
- touch_registration记录实际调用结果；touch_window_snapshot记录IsTouchWindow/flags、owner tid/pid/匹配、valid/visible/enabled。查询前清本次GLE，false+0仅表示未注册观察，false+观察错误也不无条件认定系统故障；原调用者GLE由ErrorScope恢复。注册状态不等于已收到Touch。
- 在真实Raw(entry=wndproc)的每个WM_POINTERDOWN再取窗口快照和pointer_info；channel_callback与Move不做新增查询，避免Bar重复取证。启动快照不得标成输入Down。
- GetPointerInfo只在开关打开的原生消息线程动态解析/调用，Win7无导出只记录api_unavailable。查询成功才使用PT_PEN/PT_TOUCH等type、sourceDevice、hwndTarget、flags/ID/time与processed/raw坐标；失败清字段并标unknown。不依赖触觉能力，不猜驱动Windows Ink开关，不把sourceDevice等同Touch hSource。
- 继续原非阻塞有界sink，每条关键结果完整，拆开注册/窗口/Pointer三记录避免截断；不加输入合成、capture/lease、窗口枚举或新业务状态。

### 4. Validation & Error Matrix

| 情况 | 必须输出/行为 |
| --- | --- |
| 注册成功但旧GLE非0 | registered/error=0，保留调用后GLE |
| 注册失败 | register_failed及立即捕获GLE；不重试/阻断原流程 |
| IsTouchWindow false | 记录本次清零后的error与flags_valid，不能套旧错误 |
| 原生PointerDown重复或跨窗 | 每次按本WndProc查询；role/HWND和Pointer来源可关联 |
| 可选API不存在/查询失败 | api_unavailable/query_failed分开，type未知，不影响输入 |
| 开关off或仅Move/channel_callback | 无新增窗口/Pointer查询 |

### 5. Good / Base / Bad Cases

Good：启动注册true、Down仍is_touch=true但只有PT_PEN，可把注册与输入来源分别取证。Base：仅增加观察。Bad：调用注册就写成功、成功读旧GLE报错、以注册状态声称已收到Touch、用可选触觉失败阻止Pointer来源记录。

### 6. Tests Required

真实helper+只读查询替身和输出sink，核off零查询/输出、成功失败/error保真、IsTouchWindow false+0/新错误、owner异常、Pointer原始字段/来源/API缺失、非WndProc/Move跳过、重复Down边沿与不截断；不创建HWND。完整Debug ARM64、适用生产探针与headless --no-window，Release x64静态核产物后再交用户设备复核。

### 7. Wrong vs Correct

Wrong：RegisterTouchWindow后输出GetLastError但不看BOOL；GetPointerInfo失败时按extra低位猜Pen；给启动快照标Down。Correct：实际结果/失败码/注册读回分层，Pointer查询单独标有效性，沿现有开关和ErrorScope只观察。
