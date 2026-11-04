# Research: 持续悬浮跨窗口的笔/触摸路由与 HiMsg 边界

- Query: 另一数位板在笔始终 in-range 时悬浮跨窗口后目标不可点击，OutOfRange 后恢复；Bar/PageControl 窗口注册、HiMsg 与默认消息处理是否生成/改投 Touch 或改写兼容鼠标来源，应用缺哪个交接。
- Scope: mixed（当前源码、Release x64 用户日志、微软官方 Win32 文档）
- Date: 2026-11-02
- Task: .trellis/tasks/11-02-shape-style-tablet-bar
- Log: Build/x64/Release/M6-input.txt

## Findings

### 确定结论

1. **HiMsg 没有 Pointer/Pen → WM_TOUCH 合成、触点ID生命周期缓存或 extraInfo 改写逻辑。** 当前核心为单个 HiMsg.hpp；没有 WM_TOUCH/WM_POINTER/RegisterTouchWindow/SetMessageExtraInfo 分支。
2. **四个 PageControl 窗口根本不绑定 HiMsg。** IdtMain.cpp:3539 对四个分页 role 设 bindMessages=false；不能把 PageControl Down/Up 的 0xff51572d → 0xff5157ad 分类变化归因 HiMsg。
3. **应用自己的消息契约已经明确禁止按这一个低位分类拆交互接触，但 PageControl 正在违反它。** Message.cppm:33 注释明确同次接触 Pen/Touch 低位分类不保证稳定，归属只认 Pointer 来源；:41-42 说明 IsPenGeneratedMouseMessage 仅供诊断显示。PageControl.cpp:2863 用它建立 Pen 会话，:2817/:2903 又过滤低位 Touch 的抬起，丢了结束路径。用户日志两处把这一因果链完整记录下来。
4. **主栏的业务只按收到 WM_TOUCH 的 HWND 本地处理，WM_POINTER 只是触觉旁观。** 它没有将落在旧 Touch 窗口的输入与当前悬浮 Pointer 目标做跨窗统一归属。新日志实际显示 Touch 接收窗在同一 in-range pointer 生命周期内固定，而 Pointer 目标随位置跨窗口；因此“目标窗没有 Touch”是本次日志中的事实，不是设备始终不支持 Touch。
5. **注册/样式中没找到应用显式设置这种 Touch 锚定窗口的代码。** 目前只能定位到 Windows/兼容输入路由与应用 per-HWND 假设不匹配；不能给这段 OS/驱动行为指定未经证实的内部原因，也不需要先强证它才定位应用丢 Up/租约滞留缺陷。

### 窗口创建与注册（源码事实）

- IdtMain.cpp:3438-3442 的 touchRegisterFuc 对 HWND 调 RegisterTouchWindow(hwnd,0)，随后 DisableEdgeGestures。:3565-3578 把同一个回调用于 PptBottomLeft/Right、PptMiddleLeft/Right 和 Bar。
- RegisterTouchWindow 返回值当前没有持久记录。日志已有这些窗口可收到 Touch 的证据，不能凭“未检查返回值”认定本次注册失败。
- Window.cpp:1350 起将 spec.windowProc 作为真实 WNDCLASSEX.lpfnWndProc；:1499-1502 只有 bindMessages=true 才给该窗绑定 Message::Channel；:1579-1581 创建后调用 spec.created(hwnd)，所以 Touch 注册发生在有效窗口的创建完成路径。
- IdtMain.cpp:3539 四个 PageControl role 显式 bindMessages=false。Bar 保持绑定并在 :3544-3548 设置 QueueWindowMessageInLayoutSpace 回调。
- Message.cpp:94-103 只是把 filter/close policy/child binding 传给 HiMsg::BindWindow；无 Pointer/Touch 选项或来源转换。
- 所有 overlay 从 IdtMain.cpp:3447-3448 的 WS_POPUP|WS_CLIPCHILDREN、WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW 继承；PageControl 与 Bar 没有不同的 Pointer 注册目标设置。
- Other.Gesture.cppm:16 的 DisableEdgeGestures 只设置 Shell 窗口属性，未修改消息来源、Pen/Touch 分类或 contact routing。
- PageControl.cppm:130 的 Tablet gesture flags 与 Bar.Interaction.cpp:3060 附近返回同组禁用反馈/轻拂等标记；未发现以 pointerId/首次进入窗维护 Touch 目标的应用状态。

### HiMsg 实际做什么、不做什么

- HiMsg.hpp:699-729 的 BindInternal 用 SetWindowSubclass 将同步观察挂到已有 HWND；只保存 HWND/core/subclass identity，没有笔ID或范围生命周期字段。
- :799-841 的 SubclassProc 以原始 hwnd/message/wParam/lParam 调上层 callback；没有调用 SetMessageExtraInfo 或对来源签名做计算。
- :843-848 区分 Handled/QueueAndHandled 和 Discard。Discard **只跳过 HiMsg 队列**，不是吞掉后续真实 WndProc。
- :850-867 可选 MakeMessage 只把已识别的常规 Mouse/Key/Char/Window 消息复制到 EasyX 风格结构；:631-648 未识别消息（包括 Touch/Pointer）在非ForceQueue时返回空，不读取/关闭触摸handle。
- :880-905 除明确Handled、Close policy、Destroy bookkeeping外，最终 DefSubclassProc 继续真实窗口过程。Bar 回调的 Discard 后真实 Bar WndProc 仍有自己的去重过滤；这解释双观察点，不能把两条 entry 日志当作OS重复投递。
- 全文核对未发现 WM_TOUCH/WM_POINTER/TOUCHINPUT/RegisterTouchWindow/SetMessageExtraInfo 支持。HiMsg/Main.cpp 是库示例，并不提供生产输入转换器。

### 同次接触分类变化是已证实的应用缺陷

Message.cpp:61-77 判断：高24位0xff515700为 Pointer 生成鼠标；bit0x80=0显示Pen，=1显示Touch。其余低7位可相同。

日志L6174-6188（右下窗）：

- L6174 原生 WM_LBUTTONDOWN extra=0xff51572d；L6176 申请 lease 成功（right channel、generation=1）。
- L6177-6180 真命中 hit=2、SetCapture 成功、ticket=26、Pen Down 入队并消费。
- L6183 同一范围 pointer1272 的 Pointer Up 到右下窗。
- L6186 原生 WM_LBUTTONUP extra=0xff5157ad；L6188 直接走 touch_promoted_mouse 过滤。低7位仍为0x2d，改变的是0x80分类位。
- 后续日志L6192-6193 旧Touch窗左下尝试接触，却被 owner=right/generation1 拒绝。缺少UI Up之后还保留Pen会话和租约，已经形成完整应用因果链。

日志L7103-7124（左下窗）重复同样模式：

- Down 0xff51572d 建立 left/generation48，ticket34，真实 hit=5。
- L7115 Up 0xff5157ad 到左下，L7117 被Touch副本过滤；L7123-7124 右下新Touch被left旧owner/generation48拒绝。

这不需要假设原始 Up 消息丢失：**Up 已到正确 HWND，是应用依据逐消息分类把它过滤了。** Source::Pen 会话未走到 PageControl.cpp:2891 的 EndTouch，也未走到 :3162-3171 的正常 Up/capture/reset 收尾。Window/HiMsg 没有把它改成另一种输入，尤其这两个 HWND 未绑定 HiMsg。

### 主栏失效的另一层：Touch 与 Pointer 目标不同

主会话的日志统计（由主会话提供，本文核对相邻例证）：

| in-range contact | Pointer Down目标 | Touch Down目标 |
| --- | --- | --- |
| 1263 | Bar14 / 左6 / 右46 | 仅Bar71 |
| 1272 | Bar15 / 左12 / 右6 | 仅左34 |
| 1273 | Bar26 / 左21 / 右7 | 仅右57 |

主会话得到137次同contact、50ms内跨目标Down匹配；这是本文件现场的事实模式，不推定所有驱动都如此。

- 例如L7110 在左下已获得capture时，WM_TOUCH仍投右下 HWND（rect=2390,1729,2890,1929），屏幕点524,1831明显在左下；L7114相同contact1273的Touch Up也在右下。
- PageControl.cpp:2984 WM_TOUCH 接纳后按当前收到的 index 和本地 nativeTouches/client 解释；:3046 BeginTouch 先申请全局lease，:3110附近才核对真正背景/hit后决定能否建立本地会话。没有将此Touch与另一窗刚收到的Pointer目标归一。
- 错目标 Touch 可先获 lease 后因本地背景命中失败触发 WM_CANCELMODE / ResetNativePointer；日志L6168-6172就是左下无背景命中后释放 generation42。它释放后，右下的兼容Pen Down才能建立 generation1，而其Up又被过滤，转为持久阻挡。
- Bar.Interaction.cpp:3089 WM_TOUCH 同样只使用本窗 producer；:3164 先 BeginTouch、:3120附近申请SetCapture后入队，worker之后才检查呈现命中。:343 的Pointer观察只管理触觉凭据，不替代按钮Down/Up。
- Bar.Interaction.cpp:9758 Queue callback 与 :3237 WndProc均过滤全部 Pointer 生成鼠标。若当前Pointer/兼容鼠标已在Bar，而Touch还在旧Page，Bar业务就没有替代来源。这解释“Pointer到了但主栏点不动”，并不要求M6完全没有WM_TOUCH。

### 应用当前对范围退出/跨窗的处理边界

- Bar 的 ObserveBarHapticPointer 对 WM_POINTERLEAVE/WM_POINTERCAPTURECHANGED 只处理触觉token失效。PageControl没有这两个消息的UI Session退休分支，未处理Pointer最终均DefWindowProc（PageControl.cpp:3174；Bar默认分支:3254）。
- PageControl.cpp:2733 ResetNativePointer 是确定释放途径；正常Up、capturechanged/cancel、隐藏/销毁、semantic失效均能到达部分收尾。PointerTimer只复核现有会话合法性，不保证原始终态被捕获。
- PageControl.cpp:3030-3035 真实Touch Down若发现本窗仍是非Touch会话，会先取消旧Pen，再重新申请Touch。若OutOfRange后Touch锚定到了这个新窗，该分支可使旧Pen租约恢复；也可能OS另送WM_CAPTURECHANGED触发取消。**应以恢复日志的实际释放理由确认，不能只凭“远离即恢复”认定某一个取消分支。**
- 在原范围内悬浮跨窗（in-range Leave/Enter）与真正OutOfRange不是同一事件。现有应用没有完整的跨窗Touch→当前Pointer业务归属交接；也不能简单把每个Leave都当成成功Up或清空所有窗口会话。

### 官方语义与外部资料

- [System Events and Mouse Messages](https://learn.microsoft.com/en-us/windows/win32/tablet/system-events-and-mouse-messages)：定义0xff515700来源签名、0x80 Pen/Touch位及低7位cursor ID。该文并未保证本程序每次收到的分类位都能作为交互会话稳定归属；“同次不稳定”的应用约束来自现有代码注释和这份实测日志。
- [WM_POINTERENTER](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointerenter)：已有悬浮pointer跨入另一窗口也会发Enter，不要求先离开检测范围；捕获变化后不保证Down/Up或Enter/Leave均成对。
- [WM_POINTERLEAVE](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointerleave)：in-range悬浮跨出窗口与真正离开检测范围可区分，不能把两者混为一谈。
- [WM_TOUCH](https://learn.microsoft.com/en-us/windows/win32/wintouch/wm-touchdown)：Touch不遵循普通鼠标HTTRANSPARENT区域；不能据普通鼠标命中假定Touch落到相同目标，也不能不经完整归属就跨窗转发触摸handle。

### 可证伪预测与最小后续边界

1. 在同一次in-range范围内，从Touch锚定窗跨到另一窗，应看到Pointer目标随位置改变而Touch留旧窗；回原锚定窗可能恢复触摸业务，未必一定要离开感应范围。
2. 当Page兼容Pen路径发生Down=Pen/Up=Touch位变化，只要不在中途按签名位丢终态，旧Pen lease不应继续占有；这是可做无窗口真实入口回归的确定缺陷，不需再让用户采同粒度日志。
3. 修复不能只是定时强制EndTouch或拿全局最近笔补Up。应维持同一接触归属、一条权威来源、旧代终态不完成新会话及Capture取消安全。
4. 对主栏还需解决当前Pointer目标与收到Touch的旧窗不一致时的业务归属。是统一Pointer后端、在既有窗口层协调Touch、还是兼容来源会话方案属于修复设计选择；本研究不代替主会话/用户批准，也不把“放开过滤”当作完整解法。

### Files found / Related specs

- `Inkeys/IdtMain.cpp`：Touch注册、overlay样式与Page/Bar HiMsg绑定差异。
- `Inkeys/Inkeys/Window/Window.cpp`：原生WndProc注册、Message Channel子类绑定与created回调。
- `Inkeys/Inkeys/Message/Message.cpp/.cppm`：签名识别与已明确的会话分类合同。
- `Inkeys/additional/HiMsg/HiMsg/HiMsg.hpp`：完整库subclass/队列/forward实现。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp`：Pen兼容、Touch、本地Session、捕获和收尾。
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`：Bar只接Touch业务及Pointer触觉旁路。
- `Inkeys/Inkeys/Other/Other.Gesture.cppm`：Shell边缘手势属性。
- `.trellis/spec/native-desktop/ui-pointer-session.md`、`pen-haptics.md`：输入来源/代次/终态和可选反馈隔离合同。

## Caveats / Not Found

- 未发现HiMsg改写extraInfo、按Pointer ID锁窗或生成WM_TOUCH；四个Page窗口未绑定它。
- 找到确定的应用丢Up/lease未退休缺陷和跨窗Touch/Pointer业务归属缺口，不宣称已经解释Windows/驱动内部为何采用此Touch目标。
- 所有窗口使用同一注册回调；不能仅凭RegisterTouchWindow返回值未检查就认定注册失败。
- 主会话汇总的全contact计数和137匹配未在本子代理重复全量计算，本文直接核对了两组完整日志和源码链；最终数字以主会话生成分析为准。
- 本轮只写此研究文件，无产品/spec修改、无Git操作、无编译/GUI/设备测试。
