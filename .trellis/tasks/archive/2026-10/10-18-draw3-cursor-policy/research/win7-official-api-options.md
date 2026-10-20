# Research: Win7 系统光标与触摸反馈的官方 API 边界

- **Query**: 查证 Win7 单指擦除双光标、抬笔系统光标残留和隐藏触摸光标首帧闪现的窗口级控制能力，区分 HCURSOR、Tablet feedback 与 Draw3 自绘光标。
- **Scope**: mixed；本地 Draw3 输入/窗口代码 + Microsoft 官方文档和官方 SDK 头文件。
- **Date**: 2026-10-18。
- **Task**: `.trellis/tasks/10-18-draw3-cursor-policy`；调用方明确指定目录。`task.py current --source` 返回 `(none)` / `none`，本调查不执行 `task.py start`。
- **阶段**: 仅调查和规划；本文件是本子任务唯一写入，没有代码、配置、spec、构建、测试、GUI 或 Git 操作。

## 最新范围澄清（2026-10-19）

用户要求系统触摸光标始终隐藏，无用户开关/隐藏偏好，旧 Beta 不迁移；当前似乎已隐藏，优先核对现有效果和 Win7。本文 API 能力事实仍有效，候选措施只在确认现有路径有缺口时采用。窗口脱离后的资源/属性恢复属于所有权清理，不是用户关闭隐藏的产品场景。

## Findings

### 先确定的结论

1. **Win7 可使用现有窗口级 Tablet property / `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 控制部分系统反馈，并以 `WM_SETCURSOR` 控制普通系统光标。** 两者控制不同对象，应分别调查。
2. **`SetWindowFeedbackSetting` 最低 Windows 8。** 新增该路径必须动态探测导出，Win7 继续使用旧路径；不能把静态导入或 Win8 API 当成 Win7 方案。[官方函数要求](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowfeedbacksetting)
3. **当前官方资料没有证明存在一个受支持的 Win7 单一窗口 API，可以在所有设备上、首次落指之前关闭全部触摸/笔可视效果，并保证零闪现。** 可以预先发布窗口策略并修复应用可控制的消息次序，但“用户报告的闪现已经消除”必须留给具体 Win7 硬件验收。
4. **`SetCursor(nullptr)` 不是关闭 Tablet feedback 的 API。** 它移除普通 HCURSOR，不能据此推断系统触点 UI 或 Draw3 自绘圆环已经隐藏。[SetCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcursor)
5. **Win7 不能依赖 Win8 的系统自动抑制光标语义。** `CURSORINFO` 中因触摸/笔而被系统抑制的 `CURSOR_SUPPRESSED` 明确标记为 Windows 8；这能解释平台差异需要专项处理，但不能直接证明当前残留的根因。[CURSORINFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-cursorinfo)
6. **小橡皮的身份仍未确认。** 用户描述不足以区分 HCURSOR、Tablet touch UI、旧 Pen/Mouse primary visual 或第三方驱动反馈。必须把 Windows 全局光标采样与 Draw3 `primary|touch` 呈现证据对齐。

### Files Found / 本地落点

| 文件与行号 | 直接确认的事实 | 对规划的含义 |
| --- | --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:40` | `kTabletInputFlags` 包含 multitouch、press-and-hold、pen tap、pen barrel、flick；未包含 `TABLET_DISABLE_TOUCHUIFORCEOFF` | 存在已接入的窗口级策略落点；当前禁用 tap/barrel 不能推出 touch UI 已禁用 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:309` | `AttachExternal` 在开放 WndProc 转发前写 `MICROSOFT_TABLETPENSERVICE_PROPERTY` | 初次接触之前发布策略是既有模式，可研究统一更新同一份 mask；不能等待首次 RTS Down 后才配置反馈 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1092` | `QueueSystemCursorRefresh` 使用去重的 `PostMessageW` 交给窗口线程 | 适合跨线程状态收敛；异步排队本身不能保证首次 Down 前已生效 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1261` | `ApplyWindowCursor` 先检查光标命中的窗口/子窗口，再依策略 `SetCursor(nullptr/defaultCursor_)` | 命中约束应保留；“仅影响当前 HWND”的注释是调用方范围约束，API 本身并无 HWND 参数 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1496` | 在 `HTCLIENT` 的 `WM_SETCURSOR` 中处理并返回 `TRUE`；随后直接返回同一 `kTabletInputFlags` 响应 Tablet query | 已有普通光标和 Tablet query 两个入口；应避免仅在后续 MouseMove 中补隐藏造成切换闪烁 |
| `Inkeys/Inkeys/Window/Window.cpp:1115` | 窗口类缺省 `hCursor` 回退为 `IDC_ARROW` | 审查 Drawpad 消息转发/初始化空隙和是否继续进入默认处理；不应全局改整个共享窗口类来解决一个 Drawpad 的策略 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1023` | 使用自定义 `IStylusSyncPlugin`；Down 的 Touch 通知见 `:1009`，Up 的 Touch 终结通知见 `:1016` | RTS 回调是接触事实来源，系统 HCURSOR 设置仍交 HWND 所属线程执行 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1318` | Up 清接触 Pen 样本，后续 InAir/Pointer 才恢复真实 hover；OutOfRange `:1142` 也清 Pen 样本 | Up 与 OutOfRange 不能合并为“鼠标已接管”；要分别检查样本清理、owner、系统光标刷新 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:2687` | `IRealTimeStylus3::put_MultiTouchEnabled(TRUE)` 后读回；`:2750` 仅添加自定义同步插件后启用 RTS | 本次扫描这些 Draw3 文件未发现创建 `DynamicRenderer`；不能把其 Enabled 当成当前光标修复入口 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:379` | `ShouldHideSystemDrawingCursor` 对 Eraser/Laser、Pen owner、Touch owner 主动隐藏 | 即使逻辑返回 hide，仍需验证 UI 线程真正设置的 HCURSOR 和独立反馈层；不能仅凭这一个布尔值声称无双光标 |

### 三类可见物必须分开

| 可见物 | 谁控制 | 可以使用的证据 | 不能据此推出 |
| --- | --- | --- | --- |
| 普通 Windows HCURSOR | 窗口所属输入线程/输入队列的 cursor 状态；`WM_SETCURSOR` / `SetCursor` | `GetCursorInfo` 的成功返回、`flags`、`hCursor`、位置；所在 HWND 和线程；实际 SetCursor 决策 | 一个成功调用不保证之后没有别的 SetCursor；HCURSOR 隐藏不代表触摸反馈也隐藏 |
| Tablet touch/pen UI feedback | Windows Tablet/Touch 子系统 | 预置窗口 property/query mask、新系统上的 per-window feedback 配置结果、实际画面 | 它不是应用 primary cursor；`SetCursor(nullptr)` 不负责逐项禁用这些效果 |
| Draw3 自绘光标 | Pen/Mouse primary visual、逐 Touch runtime visual | `owner`、样本、`visual source=primary|touch`、呈现/像素与位置尺寸 | 不能因为像橡皮就判为系统 HCURSOR；提交成功也不证明桌面最终图像 |

`GetCursorInfo` 自 Windows 2000 可用，查询全局光标，不是某 HWND 的私有状态；结构须正确设置 `cbSize`。Win7 可观测显示/隐藏和句柄；`CURSOR_SUPPRESSED` 是 Win8 语义，不能作为 Win7 必有结果。[GetCursorInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcursorinfo)、[CURSORINFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-cursorinfo)

### API 最低版本与适用范围

| API / 机制 | 官方最低客户端 | 已确认能力 | 本任务的限制 |
| --- | --- | --- | --- |
| `SetCursor` / `WM_SETCURSOR` | Windows 2000 Professional | 设置/移除普通光标；处理消息后 `TRUE` 阻止后续默认光标处理 | 无 per-contact feedback 开关；不能把调用绑定到一个传入 HWND |
| `WM_TABLET_QUERYSYSTEMGESTURESTATUS`、对应 window property | Windows Vista；property 由同一官方文档示例给出 | 按窗口/区域选择系统 Tablet 交互与反馈策略 | 文档没有“所有 Win7 驱动首次接触零闪现”的保证 |
| `IRealTimeStylus` | Windows XP Tablet PC Edition | 读取/处理笔与触摸 packet stream、同步/异步插件 | packet 处理不等于操作全部系统 feedback；没有查到 hide-all-feedback 属性 |
| `IRealTimeStylus3::MultiTouchEnabled`、`TABLET_ENABLE_MULTITOUCHDATA` | Windows 7 | 多接触 opt-in；官方示例说明 window property 在首指 Down 生效 | 控制数据分发，不是隐藏光标；添加策略时必须保留既有多点位 |
| `IStylusPlugin::StylusUp` / `StylusOutOfRange` | Windows XP Tablet PC Edition | 分别通知离开接触表面和离开 digitizer 感应范围 | Up 后仍可能悬停；不要把两者视为同一生命周期事件 |
| `IDynamicRenderer` | Windows XP Tablet PC Edition | 用 RTS 数据动态绘制 ink；Enabled 控制该 renderer | 非系统光标或 touch UI 的总开关；当前 Draw3 非其渲染链 |
| `IInkCollector::MousePointer` / `MouseIcon` | Windows XP Tablet PC Edition | 控制 InkCollector 所管理的鼠标指针；Custom + null MouseIcon 可交还处理权 | 当前 Draw3 使用 RTS，不能直接把 Collector 设置移植成 RTS 属性；没有额外引入 InkCollector 的必要证据 |
| `SetWindowFeedbackSetting` / `FEEDBACK_TYPE` | Windows 8 | 逐窗口设置 touch contact、pen/touch tap、press-and-hold 等反馈项 | Win7 不可用；每个反馈项分别调用并检查结果，BOOL 配置、flags 为 0；恢复可用 size=0/configuration=null |
| `GetCurrentInputMessageSource`、`GetPointerType` | Windows 8 | 查询当前消息设备来源、Pointer 类型 | Win7 缺少此来源证据；动态 API 不可用不能被当成“已确认真实 Mouse” |
| `ITablet::CreateContext` | Windows XP Tablet PC Edition | 低层 context 选项包含 `TCXO_DONT_SHOW_CURSOR` | 官方对 `ITablet` 明确不建议开发者使用；该 flag 文义为不设置 cursor **位置**，不能据名称宣称隐藏全部 cursor/feedback；不列为产品候选 |

版本来源分别见下方官方参考。`IRealTimeStylus2::FlicksEnabled` 本地确有关闭调用；本次该 API 的独立 Learn 页面未能打开，不据此补写未经确认的最低系统，也不把其等同触摸 UI 控制。

### Win7 窗口级 Tablet flags 的精确边界

Microsoft 官方 `tpcshrd.h` 当前定义的名称是 `TABLET_DISABLE_TOUCHUIFORCEON` / `TABLET_DISABLE_TOUCHUIFORCEOFF`。旧文章/示例中的 `SYSTEM_GESTURE_STATUS_TOUCHUI_FORCE*` 或 WPF 内部 `TABLET_TOUCHUI_FORCE*` 是相同数值的命名；实施时以项目所用 SDK 头文件为准，不能凭额外的 `DISABLE` 字样反推相反语义。[官方 SDK 头文件](https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/um/tpcshrd.h)

| 实际 SDK 宏 | 值 | 文档可确认的作用 | 规划处理 |
| --- | --- | --- | --- |
| `TABLET_DISABLE_PRESSANDHOLD` | `0x00000001` | 禁止长按右键交互，可减少等待长按判定带来的响应延迟 | 保留；它不等于所有光标消失 |
| `TABLET_DISABLE_PENTAPFEEDBACK` | `0x00000008` | 禁止 pen tap 的波纹反馈，官方示例标注在 pen up | 保留；不要用它证明普通箭头已隐藏 |
| `TABLET_DISABLE_PENBARRELFEEDBACK` | `0x00000010` | 禁止笔侧键按下的圆形反馈 | 保留；与 Touch 接触 UI 单独考虑 |
| `TABLET_DISABLE_TOUCHUIFORCEON` | `0x00000100` | Vista 官方 touch UI 文章用相同数值的 FORCEON 选择启用 touch UI | 隐藏策略不设置此位；不同时设置 ON/OFF 来猜优先级 |
| `TABLET_DISABLE_TOUCHUIFORCEOFF` | `0x00000200` | 同一文章用 FORCEOFF 选择禁用 touch input UI | 是 Win7 首选候选；在首个接触前同时进入 property 与 query 返回值，并按需求可恢复 |
| `TABLET_DISABLE_FLICKS` | `0x00010000` | 禁止 pen flick 交互 | 保留，与触摸光标项正交 |
| `TABLET_ENABLE_MULTITOUCHDATA` | `0x01000000` | RTS 多点数据 opt-in | 不得被新的反馈 mask 覆盖掉 |

`TABLET_DISABLE_TOUCHSWITCH` 虽在官方头文件/消息页列出，但本次未找到足以证明其控制 HCURSOR 或阻止所有触摸可视反馈的明确官方语义，不将它加入候选 mask。

整个 query 消息的最低版本是 Vista，官方也给出整个窗口 property 示例。Vista touch UI 文章明确说明可在 WndProc 选择 FORCEOFF；这支持其 Win7 兼容候选身份。**文档没有给出当前 Win7 设备上触摸反馈层、驱动附加反馈及首次 Down 的完整时序保证**，不能把“可禁用 touch UI”扩写成“所有触摸图像、所有 HCURSOR、所有自绘光标都不闪”。[Tablet query](https://learn.microsoft.com/en-us/windows/win32/tablet/wm-tablet-querysystemgesturestatus-message)、[Vista touch UI](https://learn.microsoft.com/en-us/windows/win32/tablet/touch-input-support-in-windows-vista)

### SetCursor / WM_SETCURSOR 与线程

- `SetCursor` 没有 HWND 参数；状态与调用线程负责的窗口输入队列相关，输入关联时还可能共享队列。应用须限于自己客户区或鼠标捕获期间，并在离开/交还控制时按窗口策略恢复。现有 `WindowFromPoint` 命中检查不应移除。[官方线程说明](https://devblogs.microsoft.com/oldnewthing/20220921-00/?p=107203)、[队列状态说明](https://devblogs.microsoft.com/oldnewthing/20061117-03/?p=28973)
- 普通无 capture 的移动会触发 `WM_SETCURSOR`；默认处理会先询问父窗口，再恢复客户区 class cursor 或非客户区箭头。处理并返回 `TRUE` 才会截住该默认链路。[WM_SETCURSOR](https://learn.microsoft.com/en-us/windows/win32/menurc/wm-setcursor)
- **只在 `WM_MOUSEMOVE` 中 SetCursor 会产生可解释的闪烁窗口**：先默认 class cursor，后应用 cursor。应让同一份策略直接参与 `WM_SETCURSOR`；这解决应用消息链中的可控闪烁，不覆盖独立系统 touch feedback。[官方时序说明](https://devblogs.microsoft.com/oldnewthing/20061121-15/?p=28943)
- RTS 同步插件通常运行在专用高优先级线程；从回调直接 SetCursor 不构成可靠的 UI 线程修复。跨线程同步调用还可能阻塞 RTS，不能为了“消除异步延迟”改成同步等待窗口线程。保留轻量状态发布、UI 线程响应和预置窗口策略。[StylusInput 线程要求](https://learn.microsoft.com/en-us/windows/win32/tablet/threading-considerations-for-the-stylusinput-apis)
- `hCursor=null` 仅代表没有 class 默认选择，**不是隐式隐藏所有光标**；用修改共享 class cursor 来修复一个 HWND 还会影响同类其他窗口。现有默认箭头事实需要审查转发是否足够早，但不自动授权改窗口服务的共用默认值。[重新计算窗口光标](https://devblogs.microsoft.com/oldnewthing/20220921-00/?p=107203)

### RTS、InkCollector 和底层 Tablet 机制的边界

`IRealTimeStylus` 负责 packet stream；同步/异步插件能处理或过滤 stream。`IDynamicRenderer` 则把 stream 画成 ink，二者不是系统 touch UI 显隐接口。`StylusDown/Up` 的 packet 本身不能删掉；通过删除接触包来隐藏光标既不受该接口支持，也会破坏输入生命周期。后续仍以现有自定义 RTS producer 为准。[RTS](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nn-rtscom-irealtimestylus)、[DynamicRenderer](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nn-rtscom-idynamicrenderer)、[StylusUp packet 规则](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-stylusup)

`IInkCollector::MousePointer` 的默认值按 drawing attributes 管理指针，`IMP_Custom + MouseIcon=null` 让 Collector 不再管理 mouse cursor；该机制属于 Collector，不是 RTS 的 hide-all 选项。`InkMousePointer` 枚举没有一个独立的 NoCursor 值。[MousePointer](https://learn.microsoft.com/en-us/windows/win32/api/msinkaut/nf-msinkaut-iinkcollector-get_mousepointer)、[枚举](https://learn.microsoft.com/en-us/windows/win32/api/msinkaut/ne-msinkaut-inkmousepointer)

`ITablet` 官方标注 “Developers should not use this interface.”；`TCXO_DONT_SHOW_CURSOR=0x80` 的说明实际是 context 不设置 cursor **位置**，不是文档保证的全部可视图像隐藏方案。无需为这个任务引入新的低层 context、私有 Wisptis 调用或驱动参数。[ITablet](https://learn.microsoft.com/en-us/windows/win32/tablet/itablet)、[CreateContext](https://learn.microsoft.com/en-us/windows/win32/tablet/itablet-createcontext)

### Win7 输入来源与抬起后的注意点

Microsoft 的兼容鼠标消息文档给出 `GetMessageExtraInfo` 签名：掩码 `0xFFFFFF00` 与 `0xFF515700` 比较，可识别 Tablet pen/touch 提升的 Mouse 消息；低位 `0x80` 在 Vista 起用于区分 touch/pen。它可作为 Win7 来源证据，但具体设备是否始终保留签名仍要采集，不能将某次缺失来源的新 API 或未知值硬写成真实 Mouse。[System Events and Mouse Messages](https://learn.microsoft.com/en-us/windows/win32/tablet/system-events-and-mouse-messages)

同一官方 touch UI 文章指出：即使用户只使用 pen/finger，也可能出现其他来源的 Mouse 消息，包括为 layered 窗口后方应用发送的移动。这与仓库现有“未知来源不能轻易恢复旧主光标”的设计相符。[Vista touch 输入说明](https://learn.microsoft.com/en-us/windows/win32/tablet/touch-input-support-in-windows-vista)

`StylusUp` 说明接触结束，`StylusOutOfRange` 说明离开感应范围；不能认为收到 Up 就必定再无笔 hover，也不能等待 `WM_POINTERLEAVE` 才清 Win7 的接触态。建议在既有状态机中分别验证 Up、OutOfRange、Cancel、禁用、销毁；最后触点结束只结束对应逐触点 visual，系统 cursor 继续根据当前设备归属和设置决定，真实新鼠标输入才能接管。[StylusUp](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-stylusup)、[OutOfRange](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-stylusoutofrange)

## 候选方案与后续验证表（本轮不执行）

| 顺序 / 候选 | 作用对象与时机 | 预期可验证结果 | 仍需证明的边界 |
| --- | --- | --- | --- |
| V0：现有诊断对齐 | Win7 首次接触、Move、Up、迟到 Mouse；对照 `GetCursorInfo` 和 Draw3 primary/touch visual | 判断小光标来自 HCURSOR、自绘 primary 还是反馈层；当前形状、时间、线程与 owner 可追踪 | 单个 SetCursor 决策日志不能代替实际 cursor 查询；普通截图可能不含硬件 cursor，应使用能记录可见效果的设备采集 |
| V1：首触前发布 FORCEOFF | 主 Drawpad property + query 返回同一 mask，保留 multitouch/现有抑制位 | 在 Win7 缺省 touch UI 开启的环境，首个 Down 前固定建立隐藏策略，启动与 HWND 重建后仍生效 | 每种设备与 Windows 版本是否覆盖当前闪现来源；附着到首次接触之间是否有生效窗口 |
| V2：统一 UI 线程 HCURSOR 策略 | HTCLIENT `WM_SETCURSOR` 与 posted refresh 读取同一即时策略 | 不先落回 class arrow 再隐藏；鼠标 capture/leave、切工具、穿透后能交还正确 cursor | 首次输入归属尚未知的时间窗；不能因预先全部隐藏而永久吞掉真实鼠标或非客户区 cursor |
| V3：生命周期收敛 | RTS Up/OutOfRange/Cancel + Window destroy/capture、切工具/穿透 | Win7 抬笔/抬指后无过期自绘光标或错误箭头；后续真实鼠标/笔 hover 立即接管 | 普通 Pen Up 后仍悬停与真正离开要分别验收；不能用固定延时猜接管 |
| V4：Win8+ feedback 补充 | 动态取得 `SetWindowFeedbackSetting`；窗口可接收接触前配置相关项 | Touch contact feedback 与 tap/press-and-hold 等按项控制；不可用时仍正常启动 | 该路径对 Win7 没有效果；不要由 API 成功推断所有系统反馈和应用视觉都消失 |

最小硬件矩阵需覆盖 Win7 触摸屏（单指、多指、最后一指 Up、快速首触）、Win7 笔（普通笔/笔尾、接触→悬停→离窗）、Touch+Mouse 接管、Win8+ 同样序列；各组都使用固定启用的隐藏策略，交叉测试 Draw3 mouse 原版/新式与绘制/穿透；无隐藏关闭的产品场景。上述均为未来实现后的计划，本轮没有运行。

不采用系统全局 Tablet 设置修改、注册表/驱动私有开关、全局鼠标钩子或 `ShowCursor` 计数循环；窗口局部需求保持在本应用已有 HWND、输入归属与呈现链中。

## External References（精确 URL）

| 官方来源 | 用于核实 |
| --- | --- |
| [SetCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcursor) | null 行为、共享资源、class cursor、最低 Windows 2000 |
| [WM_SETCURSOR](https://learn.microsoft.com/en-us/windows/win32/menurc/wm-setcursor) | HTCLIENT、父窗口/默认处理、返回 TRUE、最低版本 |
| [WM_TABLET_QUERYSYSTEMGESTURESTATUS](https://learn.microsoft.com/en-us/windows/win32/tablet/wm-tablet-querysystemgesturestatus-message) | flags、property 示例、最低 Vista、SendMessageTimeout 注意事项 |
| [Touch Input Support in Windows Vista](https://learn.microsoft.com/en-us/windows/win32/tablet/touch-input-support-in-windows-vista) | FORCEON/OFF 对 touch UI 的作用；其他 Mouse 来源 |
| [Microsoft SDK tpcshrd.h](https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/um/tpcshrd.h) | 宏拼写和数值，未借此推导未文档化语义 |
| [IRealTimeStylus](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nn-rtscom-irealtimestylus) | RTS 公开能力与最低 XP Tablet PC Edition |
| [IRealTimeStylus3::MultiTouchEnabled](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus3-get_multitouchenabled) | Win7 opt-in、第一指 Down 的 property 时机 |
| [StylusInput threading](https://learn.microsoft.com/en-us/windows/win32/tablet/threading-considerations-for-the-stylusinput-apis) | 同步/异步线程、同步跨线程调用会阻塞 packet stream |
| [IDynamicRenderer](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nn-rtscom-idynamicrenderer) | ink renderer 能力、最低 XP Tablet PC Edition |
| [StylusUp](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-stylusup)、[StylusOutOfRange](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-stylusoutofrange) | 接触结束与离开感应范围区别、packet 限制 |
| [IInkCollector::MousePointer](https://learn.microsoft.com/en-us/windows/win32/api/msinkaut/nf-msinkaut-iinkcollector-get_mousepointer)、[InkMousePointer](https://learn.microsoft.com/en-us/windows/win32/api/msinkaut/ne-msinkaut-inkmousepointer) | Collector 管理边界、Custom + null、枚举与最低版本 |
| [ITablet](https://learn.microsoft.com/en-us/windows/win32/tablet/itablet)、[CreateContext](https://learn.microsoft.com/en-us/windows/win32/tablet/itablet-createcontext) | 官方不建议使用，TCXO_DONT_SHOW_CURSOR 的位置语义 |
| [SetWindowFeedbackSetting](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowfeedbacksetting)、[FEEDBACK_TYPE](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ne-winuser-feedback_type) | 每窗口 BOOL 反馈设置、反馈项、最低 Win8 |
| [GetCursorInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcursorinfo)、[CURSORINFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-cursorinfo) | 全局 cursor 观测，CURSOR_SUPPRESSED 的 Win8 边界 |
| [GetCurrentInputMessageSource](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcurrentinputmessagesource)、[GetPointerType](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointertype) | 输入来源和 Pointer API 的 Win8 边界 |
| [System Events and Mouse Messages](https://learn.microsoft.com/en-us/windows/win32/tablet/system-events-and-mouse-messages) | Win7 可沿用的 GetMessageExtraInfo 签名、低位分类 |
| [Cursor recalculation](https://devblogs.microsoft.com/oldnewthing/20220921-00/?p=107203)、[follow-up](https://devblogs.microsoft.com/oldnewthing/20230525-00/?p=108247%2F) | 设置线程、窗口命中与跨线程/子窗口边界 |
| [Cursor-setting process](https://devblogs.microsoft.com/oldnewthing/20061121-15/?p=28943)、[virtual cursor state](https://devblogs.microsoft.com/oldnewthing/20061117-03/?p=28973) | MouseMove 后设置导致的闪烁；线程/关联队列状态 |

### Related Specs

- `.trellis/spec/native-desktop/input-and-ink.md`：已有 Touch 暂时视觉归属、兼容 Mouse 过滤、Win7 来源回退、RTS 与 Win32Mouse producer 边界、光标诊断合同。本报告与该合同交叉核对，不把未知消息重定义为真实鼠标。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：源码声明与实际 Win7 验证必须区分。本轮遵守用户要求不构建；未来编译仍按当前任务适用的 AGENTS 入口。

## Caveats / Not Found

- 未获得 Win7 真机完整光标日志、系统 cursor 采样或画面序列；双光标身份和抬笔残留根因保持待确认。
- 未找到一个官方支持、跨全部 Win7 digitizer/驱动、首帧之前关闭所有系统 contact 可视化的统一 API 或时序保证。该结论是本次资料范围内“未找到保证”，不是证明所有窗口级办法都不可能奏效。
- `TABLET_DISABLE_TOUCHUIFORCEOFF` 有官方语义和 Vista+ 入口，值得优先做隔离验证；不能仅凭 flag 名或同值旧别名承诺消除用户的具体小橡皮。
- 可获取的 Microsoft Learn 页面虽带通用授权提示，正文/Requirements 已由浏览工具返回；版本依据为正文要求表。Old New Thing 的 virtual cursor state 页面 direct open 曾返回 403，但官方域名搜索结果返回了所需正文；线程结论另由可检索的 2022/2023 官方文章交叉支持。
- 本轮只写本研究文档，无程序运行、编译、headless 或 GUI 验证结果。
