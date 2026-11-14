# Research: 多设备漫游输入与惯性来源

- Query: 在现有白板拖动/平移模式加入笔、笔尾、触控板漫游，并为惯性分输入类型设开关。
- Scope: mixed（生产源码只读、Microsoft 官方文档；不研究设置 UI）
- Date: 2026-11-08
- Status: 规划研究，未实施、未编译、未运行 GUI。任务继续 `in_progress`。已有屏幕尺度规划不是本阶段实施授权，`961ffdf8` 是保留基线。

## Findings

### 1. 已确认需求与最新仲裁决定

- 五类设置来源是 Mouse / Touchpad / Touch / PenTip / PenTail；默认 Mouse 与 PenTail 无惯性，其余有惯性。开关仅决定释放后运动，不应阻止跟手平移。
- 用户在本轮研究期间明确补充：**拖动/平移模式中全部来源的惯性均不再因笔靠近而急停，只在实际落笔按下时急停**；这覆盖先前“仅排除自身 hover”的候选设计。Pen/PenTail Down 停掉旧 coast 后应作为新导航 Down 处理。绘制模式的靠近急停属于后续用途，本轮不扩展绘制模式。
- Touch 活动批次仍先持有导航，后来 Pen 已有吞到终态的保护；扩展输入时应保留“活动接触不混源、直到真实终态”的既有框架，不把新增 Pen 当成可混入 Touch 质心的触点。
- 用户已确认：本阶段触控板仅支持**双指滑动平移**，按压后拖动暂不新增独立支持，仍保留现有鼠标路径；不为此扩大设备识别范围。

### 2. 现有输入分类已经足够识别 Pen / PenTail，无需新增 RTS 后端

| 文件及锚点 | 事实与用途 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cppm:29` | `InputDeviceType` 固定 Touch=0/Pen=1/MouseLeft=2/MouseRight=3；这是跨层数字契约，不应为设置类型重排或把 PenTail 编成另一个 RTS device 数字。 |
| `Draw3.ContactInput.cppm:56` | `ContactSnapshot` 已含 `isInvertedCursor`、`source`、QPC、phase、sequence、admissionRevision。 |
| `Draw3.RealtimeStylus.cpp:944` | 默认 Pen，经 `IInkTablet2::get_DeviceKind` 识别 Touch/Mouse；`BuildContextDecoder` 提供已有坐标解码和来源缓存。 |
| `Draw3.RealtimeStylus.cpp:890` | `SourceForCursor` 按 cursor ID 关联系统 Pointer 设备与 RTS context；冲突保留 Unknown/Conflict，不猜设备。 |
| `Draw3.RealtimeStylus.cpp:1257` | Down 将 `SourceForCursor` 和 `StylusInfo::bIsInvertedCursor` 填入快照，再发真实 Coordinator。Up/Packets/InAir 同样保存倒置元数据（1341/1395/1444）。 |
| `Draw3.RealtimeStylus.cpp:1662` | RTS Pen cursor mailbox 携带相同 source、inverted、inContact、QPC，不必另读压力猜笔尾。 |
| `Draw3.WindowControl.cpp:225` | Pointer 辅助路径将 `PEN_FLAG_INVERTED | PEN_FLAG_ERASER` 解释为 eraserHint，作为光标元数据；该路径在 RTS 模式不是绘制 contact producer。 |
| `Draw3.SpeedEraser.h:15` | `SourceKind` 区分 IntegratedPen/ExternalPen/Touch/TouchPad/Mouse；这属于设备事实，与 UI 上 PenTip/PenTail 条目不同维度。 |
| `Draw3.SpeedEraser.h:67` | 笔速橡皮 `InputEntry` 为 MouseLeft/MouseRight/Touch/PenTip/PenTail，**没有 Touchpad**；不直接把它复用成五类漫游策略枚举。 |

本节省略公共路径的锚点均位于 `Inkeys/Inkeys/Drawing/Draw3/`。最小方案是新增/扩展导航策略输入类型，使用 **Down 时真实 device + down.isInvertedCursor + source** 得到稳定分类；同手势 Move/Up 不因 hover 或瞬时设置翻转重新归类。来源与倒置身份应从既有快照复用，不能挪用橡皮功能是否启用来判断“笔尾”。

### 3. Pen 被拦在 Controller Selection 准入，之后还有 mailbox 急停和抑制假设

- `Draw3.DrawingController.cpp:2530`：`CanvasPanDeviceClass` 当前仅 None/Touch/Mouse。
- `Draw3.DrawingController.cpp:2593`：`CanvasPanClassOf` 只准入 Touch 与 MouseLeft；Pen 返回 None。
- `Draw3.DrawingController.cpp:2604`：`RouteSelectionDown` 仅 Whiteboard + Selection 才 Pan；同一活动批次限定 latchedClass 与新 deviceClass 相等，其他 DiscardUntilTerminal。
- `Draw3.DrawingController.cpp:2980`：真实 `HandleSelectionDown` 先判断 Pen 是否撞上 Touch 持有批次/已抑制 Pen，必要时 `TrackSuppressedPenDown`；其后统一 route。真实生产出队在 `15436` 调用该入口，正常 Pen/尾不必经过绘制 `initializeStroke`。
- `Draw3.DrawingController.cpp:2709`：`HasLiveSuppressedPenContact` 目前对 contacts 容器里**所有 Pen**检查终态，没有过滤 `panManaged`/disposition；新增 Pen Pan 后若不收紧，会把合法 Pen 当“被抑制 Pen”反馈到 `ObserveCanvasPanPenMailbox`。
- `Draw3.DrawingController.cpp:3008`：`ObserveCanvasPanPenMailbox` 合并 mailbox inRange/inContact 与 suppressed lifetime。该函数同时给生产优先级检查及导航更新使用（`18463`、`15512`），必须一起评估，不能只改一次 Step 调用。
- `Draw3.DrawingController.cpp:15516`：Pen mailbox inContact 当前无条件 `stopCanvasNavigation("pen-mailbox-contact")`。若仅放开 route，Pen 连续接触期间会每帧 Stop 自己的手势；应改为落笔边界取消旧 coast、随后让已归属的真实 contact 导航，不把持续 inContact 当新 Stop。
- `Draw3.DrawingController.cpp:15525`：现将 penInRange 传给 `CanvasPanInput::Step`；`Draw3.CanvasNavigation.cpp:1131` 在处理整批真实事件后，若 penHover 则 StopFling，因此同帧 Pen Up 产生 coast 也会马上被自己的 hover 消掉。依用户新决定，拖动/平移模式此 hover brake 应关闭（全部来源），并覆盖持续 hover + Pen/Mouse/Touch/Touchpad coast 测试。
- `Draw3.DrawingController.cpp:14145` 的 drawing-contact Stop 在绘制建笔路径。新合法 Pen Pan 必须在 Selection 分流返回，不进入笔迹/橡皮/重连初始化；离开移动模式仍走既有绘制路径。

建议保留现有活动批次互斥：Touch 可多指，Mouse/PenTip/PenTail/Touchpad 不混入别类 focus；新来源在空闲或 coast 时可接管，必须先取消旧运动并隔离 boost。现有 Touch 持有期间晚到 Pen 吞到真实终态，避免 Touch Up 后补启动；相反顺序由已锁存 owner 拒绝其他类，不在本轮随手引入 Pen/Touch 混合导航。

### 4. 既有顺序、水位、Stop epoch 和几何链可以复用

- `Draw3.DrawingController.cpp:2744` 的 `CanvasPanInput::Register` 锁存真实导航 epoch、Down sequence、panKey、显示 DIP 标尺；`Step`（2826）只从导航历史取有效 Pan 事件，latest 仅回收 suppressed/invalid 生命周期；保持该路径避免重新引入末点覆盖。
- `Draw3.DrawingController.cpp:2956` 的 `Push` 现用 `(panClass << 32) | tabletContextId` 作为 sourceKey；新增 PenTip/PenTail 后这两类必须不同（即使相同 tablet context），触控板也必须独立，不能跨来源续用旧残余 boost。真实 Down 在 `Draw3.CanvasNavigation.cpp:1044` 比较 sourceKey 后清 booster，这个时序不能提前到 Register。
- 配置只能切断释放/当前 coast，不能通过 `Stop()` 吞掉合法跟手 Move 来实现“惯性关闭”；终态仍须按既有 Coordinator/历史顺序闭合、回收一次。纯几何帧的行为应不受开关影响。
- 普通高频 Move 不应成为控制 wake：`Draw3.WindowControl.cpp:1152` 的 Mouse publisher 已在 CanvasPanMode 仅状态边沿请求 cursor render；保留 `961ffdf8` 回归。Pen publisher（1072）接触期已经不额外 wake，但 hover（1126）每次仍请求 render，若允许笔 hover 下 coast，必须检查这条路径是否重新打断活动 deadline；只在当前需求必要的 Pan 状态优化，保持非 Pan 光标反馈。
- `ApplyToCanvas`（`Draw3.DrawingController.cpp:2940`）统一应用内容像素位移并清越界轴；触控板也应汇入这一绘制线程视口更新，不直接在 USER32/DM 回调操作画布、背景或 D3D。

### 5. 当前 Touchpad 并没有独立导航输入链

| 当前路径 | 代码事实 |
| --- | --- |
| RTS TouchPad | `Draw3.RealtimeStylus.cpp:1208` 明确拒绝 SourceKind::TouchPad Down，注释“板面不进入擦屏接触链；桌面指针仍走原鼠标”。不能删除该门禁便宣称支持触控板双指手势，RTS 屏幕坐标也不是触控板设备相对轨迹。 |
| Pointer | `Draw3.WindowControl.cpp:272` 只在旧诊断名字中列出 PT_TOUCHPAD；`AuthorityForPointerType`（241）仅识别 Pen/Mouse/Touch。普通 Pointer 分支（1747）用于光标/触觉，没有 touchpad frame/history 或手势接线。 |
| wheel | `Draw3.WindowControl.cpp:2272` 的 WM_MOUSEWHEEL 只做来源/光标归属观察，不改 viewport；该文件未处理 WM_MOUSEHWHEEL / WM_POINTERWHEEL / WM_GESTURE / DM_POINTERHITTEST。 |
| Mouse cursor | `Draw3.WindowControl.cpp:109` 动态 GetCurrentInputMessageSource；`Draw3.PenCursor.cpp:543` 将明确 IMDT_MOUSE/IMDT_TOUCHPAD 都视为可接管真实指针。`WindowControl.cpp:2225` 发布鼠标cursor时仅可选标记用户指定的注入数位板，没有将 IMDT_TOUCHPAD 传播为独立来源。 |
| Win32Mouse contact fallback | `Draw3.WindowControl.cpp:1615` 把完整 Pointer 生命周期交 DefWindowProc；`Draw3.MouseInput.h:124` 的 MakeSnapshot 固定 source.kind=Mouse，只保留左右键。无法从下游还原触控板物理接触身份。 |
| 已有测试 | `InkeysHeadlessTests/draw3_contact_tests.cpp:1547` 只证明明确 IMDT_TOUCHPAD 可接管光标；`Draw3.HiddenWindowTest.cpp:286` 明确 wheel/hwheel 不生成 compatibility contact。二者不证明双指导航已存在。 |

没有找到现成触控板接触 begin/end、系统惯性阶段、全量 pad frames/history、Direct Manipulation 或 InteractionTracker 对接。主 Message/PageControl/Bar 的滚轮处理属于独立 UI，不是画布导航，可读但不应冒用其滚轮增量当 pad 手势。

### 6. Windows 官方事实：双指手势与鼠标式操作不能无条件独立区分

Microsoft 的 [Precision Touchpad Input](https://learn.microsoft.com/en-us/windows/win32/input-precisiontouchpad/precision-touchpad-portal) 说明：PTP 自 Windows 8.1 引入；mousing 会生成鼠标输入；普通应用未启用手势技术时通常获得 wheel；Direct Manipulation / InteractionTracker 能处理手势。该页也包含新的 pre-release API，不能据其新增内容推断所有已出货 Windows 都支持。

[RegisterTouchpadCapableWindow / Thread](https://learn.microsoft.com/en-us/windows/win32/input-precisiontouchpad/registertouchpadcapable)（文档 2026-04-15，仍标 pre-release）描述新的双指 WM_POINTER 选择机制及 IMDT_TOUCHPAD 来源；GetCurrentInputMessageSource/GetPointerDevices 这类无 HWND 查询只有 thread 注册时改变行为。文档仅标 Windows11，未给足以承诺全量出货的具体 build 边界。本机 SDK `10.0.26100.0/um/winuser.h` 搜索这些新注册/TouchpadInfo/STOPINERTIA 名均无匹配；尚未探测本机运行时 ordinal/导出，也未注册任何窗口/线程。

因此：

- 现有 `GetCurrentInputMessageSource` 中存在 IMDT_TOUCHPAD 枚举并不证明当前未注册线程能在所有硬件上区分 pad 鼠标式按压拖动。
- 不使用 wheel delta 是否小于 120、事件频率、RIM hDevice 是否空、鼠标速度曲线或设备名猜 pad。高精度鼠标也可产生细粒度 wheel，legacy pad 可能完全只发 mouse。
- 本轮已确认“触控板”定义为双指平移手势；按压拖动保留鼠标操作及鼠标惯性策略。未来若两种都独立归类，必须增加确切能力探测、明确来源传播和旧平台退化合同，不能承诺只加一个枚举即可全设备通用。

### 7. 稳定候选与不能混算惯性的边界

**稳定候选：Direct Manipulation (DM)**。现有 SDK `um/directmanipulation.h:365` 已有 RUNNING / INERTIA / READY 状态，`1931` 有 OnViewportStatusChanged，`391` 有 TRANSLATION_INERTIA；不需要升级 SDK 或更换产品输入后端。Microsoft 官方 [状态回调](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewporteventhandler-onviewportstatuschanged) 标 Win8+，但说明回调异步，不能在回调中重新 GetStatus 后当作同一个时刻。

[DM 配置](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/ne-directmanipulation-directmanipulation_configuration) 可独立启用 X/Y 平移与平移惯性；本轮不必启用 zoom、旋转或 rails。候选最小集成：只对真实 PT_TOUCHPAD 手势绑定 DM viewport，跟手与原生 inertia 的输出差值经有界 mailbox/队列交绘制线程；输入/模式/页边界保持同一 epoch，普通更新不打断帧节拍。拒绝非白板/非移动模式消费，未消费消息仍沿既有默认处理。

若使用 DM 原生惯性，**惯性开时消费系统 inertia，关时配置关闭并停止其现存运动；不能在消费原生 inertia 的同时再拿 wheel 尾段喂 CanvasPanDynamics 起一条自有 fling**。该方案允许其他四类保持当前产品曲线，而触控板保留系统手感；是否强制触控板也用同一摩擦模型尚未来自用户，应作为方案取舍写明。DM 无视口生命周期/初始化失败时保留 mouse fallback，不能把 fallback wheel 的尾流假装成有真实 end 的 contact。

**新 API 候选，暂不作为兼容基线**：RegisterTouchpadCapableWindow/Thread + GetPointerFrameTouchpadInfoHistory + Interaction Context2。官方 [TouchpadInfo](https://learn.microsoft.com/en-us/windows/win32/input-precisiontouchpad/getpointertouchpadinfo) 特别说明 pad ptPixelLocation 在整个手势中固定，真实轨迹在设备相对 HIMETRIC；开始时首两帧可能有系统判别造成的时间/位置空隙。不能用该固定像素坐标当触摸位置，也不能把判别缺口插出伪采样。[ReportWindowContentInertia](https://learn.microsoft.com/en-us/windows/win32/input-precisiontouchpad/reportwindowcontentinertia) 提供 STOP/END 协调，但同属 pre-release 新能力，不能硬导入破坏旧 Windows 启动。

后续定点研究已补足 fake viewport / 手动 Update / 真实 PT_TOUCHPAD 的 primary implementation 证据，详见下一节。它足够形成实施计划，但当前仓库的生命周期、RTS coexist、消息到达、系统停止与像素方向仍未实测，不等于本机已支持。

### 8. 预期最小生产改动与验证清单（待规划评审）

1. `Draw3.DrawingController.cpp`：扩展导航输入分类、PenTip/PenTail Selection 准入、仅抑制真正 suppressed Pen 的判断、持续 Pen mailbox 不自我 Stop、移动模式关闭 hover brake、按 Down 锁存来源/策略、统一消费 touchpad adapter。保留同一 pan/dynamics 与画布 apply 路径。
2. `Draw3.CanvasNavigation.cppm/.cpp`：如纯策略无法在 adapter 完成，增加最小惯性准许输入/释放 reason；几何不变、Up 仍闭合，disabled 不留 booster。不要同时调整已有释放/摩擦常量。
3. `Draw3.WindowControl.cpp/.cppm`（或独立小 helper）：DM 生命周期与输入转接、明确来源与 phase；避免高频 Pen hover / touchpad updates 重复 control wake。避免直接开另一渲染线程或新 DComp visual tree。
4. `Draw3.Host.cpp/.cppm`：仅在需要持有/启动停止 adapter 及配置传递时修改；不得让辅助 presentation HWND 也成为输入 producer。
5. 既有 `ContactInput`/RTS 数据布局原则上可复用；不要为新 pad 双指手势删除 RTS TouchPad 防误入绘制门禁。需要新 producer 的记录则专用稳定导航类型/生命周期，不伪造屏幕 Touch。
6. 自动验证更新 `--draw3-control-fence-test` 真实 Selection route，涵盖 PenTip/尾在白板Pan、Desktop/PPT拒绝、绘制照旧，Touch持有晚到Pen吞到终态、Pen持有拒绝跨类、全部来源coast遇hover不停/实际Down急停、Up同帧不丢真实末点、来源切换/关闭惯性无boost复活、overflow/Stop后旧流不复活。
7. Touchpad纯adapter回归必须覆盖 RUNNING→INERTIA→READY、inertia开关、当前惯性时关总开关/分开关、非Pan不吞wheel、DM失败/不可用、重复Pointer/wheel不双算、page/mode/size stop及旧异步回调隔离。模拟回调不是 OS 双指真机验证。
8. 保留真实 Mouse publisher 高频 burst/持续流 frame deadline 回归，并加移动模式 Pen hover 持续流下coast的 wake 检查，避免重现 `961ffdf8` 修复类别。ARM64原生 solution build、专项/完整headless按主任务规则；GUI/真实触控板需用户授权或手动验证。

### 9. 双指滑动已确认后的 DM 最小集成核对（本轮续查）

**结论：原生 DM 惯性方案具备形成实施计划的证据，无需为可行性再问用户一个实现选择。** 最终方案应明确“触控板采用系统手感，Mouse/Touch/PenTip/PenTail 保留现有产品曲线”，等待用户批准整个方案；这里不把外部代码的可行形状写成 Inkeys 已完成。DM 只输出数值变换，主产品现有 DComp 树、tile、背景和 Present 仍由绘制线程拥有。

#### 9.1 支持形状的 primary sources

- Microsoft [SetUpdateMode](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-setupdatemode) 与 [UpdateManager::Update](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationupdatemanager-update)：自有合成器可以手动拉取更新；Update 接受可空 frameInfo，提供者用于预测/延迟补偿。最小版本不伪造 frame timing，先传 nullptr。
- Chromium [DirectManipulationHelper](https://chromium.googlesource.com/chromium/src/+/HEAD/content/browser/renderer_host/direct_manipulation_helper_win.cc)（2026-11-08 读取，blob `c47ef8b241a4ef038794b02a793fbd6843ffc6db`）实际使用 fake viewport、MANUALUPDATE，按动画帧调 Update(nullptr)，不创建第二棵 DComp 树。其 OnPointerHitTest 只对真实 PT_TOUCHPAD 调 SetContact。这是该代码自己的 primary implementation，非 Microsoft 对全部平台的保证。
- Chromium [DirectManipulationEventHandler](https://chromium.googlesource.com/chromium/src/+/HEAD/content/browser/renderer_host/direct_manipulation_event_handler_win.cc)（blob `01ee61b5d6c2212598f44efdbd84823b361440b7`）把 content transform 差值分为 scroll/fling，READY 后重置 fake viewport，处理重复 READY 和重入期间销毁。可以复用其边界思路，不能连同 rails/zoom/整数截断照搬。
- Microsoft [AddEventHandler](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-addeventhandler)：回调由传入 HWND 所在线程触发，同类通知可以合并。故累计变换是可靠几何表达，回调次数不等于物理输入样本数。

#### 9.2 真实触控板接线和平台边界

1. 先创建 manager / update manager / fake viewport / handler，并激活主 Drawpad HWND；只启用平移 X/Y，不开 zoom/rails，不绑定辅助 presentation HWND。初始化以每项 HRESULT 成功为准。
2. 真正的 PTP 命中入口候选为 `DM_POINTERHITTEST`：由 GET_POINTERID_WPARAM 提取 ID、动态 GetPointerType 返回 PT_TOUCHPAD 才 SetContact。Chromium/Flutter 主实现采用这一形状，不把每条 WM_MOUSEWHEEL 当作 touchpad。普通 WM_POINTERDOWN 还可观察，但不能假定它始终提供同一 pad ID。
3. **文档差异必须记录**：Microsoft [DM_POINTERHITTEST](https://learn.microsoft.com/en-us/windows/win32/inputmsg/dm-pointerhittest) 表格将 wParam/lParam 标 unused，而上述 primary implementation 实际取 wParam 中 ID；页本身还标 pre-release。实现需动态查询成功、失败保持默认处理，并保留有界诊断。不能在无真实消息验证下宣称该细节已由 Microsoft 完整保证。
4. DM 基础 API 是 Win8+，PTP 设备定义是 Win8.1+，但 DM_POINTERHITTEST 文档最低 **Win10**。本阶段可验证的独立双指接线目标应为 **Win10/11 PTP**；不能把三者合并成“Win8.1 全支持”。Win7、Win8/8.1、legacy 非 PTP 或初始化/识别失败继续已有 mouse fallback，不新增五类识别承诺。
5. Win7 不硬导入 DirectManipulation.dll 或新 User32 API：CoCreateInstance 通过既有 Ole32 能力取系统 COM 类，失败/缺类返回 adapter unavailable；GetPointerType 复用现有动态解析（`Draw3.WindowControl.cpp:149`）。只用现有 SDK 接口，无需升级 SDK；具体 GUID 链接依赖在实施时按项目已有库核对。
6. [SetContact](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-setcontact) 会使识别后的输入由 DM 消费，WM_POINTERCAPTURECHANGED 在此表示接管，不能将其无条件当 gesture cancel。当前 WindowController 该消息仅处理 haptics/wake（1669），接 adapter 后须明确分流。普通 mouse/pen/touch 不 SetContact，避免与 RTS 重复 producer。

#### 9.3 Owner / 绘制线程方案（设计推论，非已验证行为）

- 当前 `Host.cpp:2103` AttachExternal 只附着外部 HWND；不能据 AttachExternal 调用线程就是 owner。`WindowControl.cpp:452` 已有“给 HWND owner PostMessage + 事件确认”的 Mouse start/stop 合同。新增 DM 初始化/销毁、Enable/Disable、SetContact、Update 和事件 handler **全部留 HWND owner 线程**，经类似明确请求/确认接入；不把 COM 裸指针借给 drawing thread，不在窗口过程写 InkCanvas/D3D。
- drawing thread 只发布“本模式允许/惯性策略/epoch/需要下一次 pump”的原子值，并在已有导航活动帧对 owner 合并 PostMessage 一次 Update 请求；最多一个 outstanding pump，owner 消费后 Update(nullptr)。初次命中立即做 owner Update 并发布活动通知，之后由活动帧维持 pump，READY/取消后停止。此方案无需新 16ms timer 或输入线程。
- owner handler 发布 `{epoch, sequence, cumulativeX/Y, phase, terminal}` 的数值快照/有界事件；drawing 仅对同 epoch 累计值求差，交既有 `ApplyToCanvas`，累计值可合并但 begin/phase/terminal 边界不得被覆盖。普通数值更新不调用高优先级 PublishControlWake；仅初次活动、真实终态、模式/页变更等边沿唤醒，以保留 `961ffdf8` 帧节拍。
- owner 与 drawing 之间异步 pump 可能增加一个帧间交接；这是需要计时验证的已知代价。验收要检查 pointer→DM output→drawing consume→Present 分段延迟和 mailbox 回压；不能仅以“系统独立处理线程”推断画布无延迟。若本方案出现明显额外滞后，再在同 owner 上优化 pump 节拍，不能私自直接跨 apartment 调用。
- 每次停止/模式离开/页命令/resize/失焦/销毁先提升 adapter epoch 并封口；owner Stop/Disable/移除handler/Abandon/Deactivate 后确认，晚到旧回调被epoch拒绝。DM COM 可转动嵌套消息循环，handler需要自持引用/存活保护；仅超时不等于 owner 已析构，沿已有 stop 握手故障处理，避免 UAF。

#### 9.4 坐标、方向和 READY 重置

Microsoft [坐标与变换](https://learn.microsoft.com/en-us/windows/win32/directmanipulation/directmanipulation-viewports-and-content) 指定：client为像素，viewport/content单位由应用选择，正向右/下；[SetViewportTransform](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-setviewporttransform) 负责 viewport→client。最小方案使用客户区像素 rect + identity viewport transform，输出 translation差值直接是内容像素位移；既有 ApplyToCanvas 将其反号应用到视口，不再额外颠倒系统自然滚动方向或重复乘DPI。DPI/客户区变化停止旧epoch并重建基线，不重新解释旧累计坐标。

选用 GetContentTransform（未snap的系统内容变换）还是 GetOutputTransform（最终含sync及像素吸附的视觉变换）必须在 adapter 固定。官方推荐自有手动合成使用后者，Chromium取前者转换为滚动；本项目需要浮点平移，可先固定 GetContentTransform 且不设 sync/display transform，并以亚像素连续/无人工反弹回归验证。该取舍应在实施设计中列明，不能每帧混取；不得照搬 Chromium 的 int截断。六元矩阵的 translation为[4]/[5]，scale/有限值必须检查。

fake viewport 只是手势计算，不是文档真实边界。READY时记录完本次最终差值，再无动画重置为identity，重置变换通知不得回推业务 viewport；重置可能产生第二次 READY，必须幂等。不要把 fake viewport的默认overpan/bounce或人工内容矩形边界加入无限白板的运动；这部分须用可控变换回归与实际PTP检查确认，仍以既有画布硬限位为最终边界。

#### 9.5 原生 inertia、停止与开关

[DM configuration](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/ne-directmanipulation-directmanipulation_configuration) 的 TRANSLATION_INERTIA 可与 X/Y分开配置；RUNNING→INERTIA→READY 状态由系统提供，不从wheel静默超时猜接触结束。关闭惯性时维持X/Y识别但去掉INERTIA；若当前已在INERTIA，先封口旧累计值并由 owner Stop，防已排队尾流继续移动。开启不复活旧motion。总开关与子开关只决定有效inertia。

[Stop](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-stop) 使viewport回Ready；本轮不配置mandatory snap，避免Stop仍动画到snap point。模式/页/显示目标等完整取消还需Disable或拒绝SetContact，只有Stop不保证排队旧手势不会又被认领。实际Pen Down、其他来源新Down、画布限位清理同时停止旧pad motion，并沿批次所有权仲裁；Pen hover不发停止。由于活动viewport系统可自行命中续接，跨来源和模态变化必须实际封口，不能只在最初SetContact处检查。

DM 自己接管触控板输入与coast，相比wheel不会丢失“是否惯性阶段”的语义；关闭后停在哪一系统帧仍需实测。现有自有Fling与pad原生INERTIA在任何时刻只允许一个活动来源，绝不能native transform + own fling双算。

#### 9.6 若强制全部使用现有向量摩擦曲线，额外代价

DM callback提供合成后的累计变换与系统阶段，未提供逐指真实50ms样本史、每个物理样本可信QPC或等价产品release velocity。OnContentUpdated可合并；用回调到达QPC估速会依赖owner/绘制帧率，不能宣称复用了当前“真实采样与消费FPS无关”的释放合同。GetInertiaEndTransform的终点也不等于可靠初速度。

可选路径是接受“DM变换序列估速”这一较弱新合同并重新标定/测试，或另取可信高保真pad原始数据（新API及具体OS能力，或更大HID设备方案）再做系统手势识别/自然滚动/停止协调；二者都超出最小原生惯性adapter的代价。故当前推荐采用系统触控板惯性，并在最终审批方案明确，不为统一公式引入一次未经用户要求的输入后端改造。

#### 9.7 本轮技术未知的终态

可以据以上资料安排：fake viewport初始化/真实hit-test → owner pump与累计变换adapter → 开关/Stop/epoch → Coordinator/Canvas整合与接线回归。未阻塞设计的事实已有来源；仍须在实施期验证的项目是：目标Win10/11实际PTP消息、owner线程COM初始化条件、RTS coexist、人工边界/READY重置、异步pump额外延迟、INERTIA即时取消和teardown重入。未执行GUI原型、运行时API探测、编译或任何生产改动。没有据此声称本机/Win8.1/所有触控板已经支持。

## Related Specs

- `.trellis/spec/native-desktop/canvas-pan.md`：当前可靠输入、释放、boost、诊断和鼠标节拍合同；新增输入只覆盖用户明确改变的 hover/设备准入/惯性策略部分。
- `.trellis/spec/native-desktop/input-and-ink.md`：RTS唯一producer、Pointer/Mouse光标与contact分离、Win32Mouse fallback，不把 accepted-mouse 当contact准入。
- `.trellis/spec/native-desktop/whiteboard.md`：Whiteboard Selection 门禁、页级视口与切换边界。
- `.trellis/tasks/11-08-chromium-touch-fling/multi-input-stage-plan.md`：本轮规划权威入口；旧scale-stage-plan继续延期。

## Caveats / Not Found

- 已按角色注入读取工作流/相关spec与目标代码，没有加载 implement/check jsonl（research角色隔离）。源码均只读，只写本research文档。
- 没有 GUI、真实触控板消息采集、运行时新API导出探测、DM技术原型、编译/单测；未改变任何生产行为。不能把文档中候选标为已支持。
- Win32Mouse fallback 的 Pen/Touch 提升原本归单点鼠标，旧平台不能凭空恢复 PenTail 元数据。该现有退化仍需列入最终兼容范围，不能许诺所有后端都能区分五类。
- 触控板按压拖动独立分类已被用户明确延后。文档中的未来能力边界仅供后续参考，本轮不实施。
