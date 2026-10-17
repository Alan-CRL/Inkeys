# Research: 外部窗口交互中的 RTS 终态与系统生命周期深审

- Query: 在主画布隐藏、失焦、捕获转移、覆盖、最小化与桌面切换时，正常 Windows 事件能否造成真实 RTS 持续停投递或丢终态；不得通过人工省略 Up 代替真实系统证据。
- Scope: mixed；当前产品代码、Windows SDK 10.0.26100.0 接口、Microsoft 官方合同；提供独立真实 OS 探针源，子代理不运行 GUI、不构建、不改产品。
- Date: 2026-10-15
- Task: `.trellis/tasks/10-14-canvas-freeze-investigation`

## Findings

### 1. 本轮新结论

**目前没有证明“普通 Settings/Explorer 失焦或隐藏必定令 RTS 永久失效”的新完整链。** 深审得到的重要新证据是：RTS 对移出输入区域后的收尾有明确官方合同，不能把其他输入 API 的捕获规则直接移植成 RTS 漏 Up 证据。为验证合同之外的隐藏/禁用/覆盖边界，已提供会发完整真实 OS Down/Move/Up 的独立探针，待主会话执行。

主会话另已取得当前 HEAD 的直接反证：Settings 内容区域实际 MouseDown → Alt+F4 隐藏 → 正常 Up 后，`GetGUIThreadInfo` 仍报告隐藏 Settings 持有 capture，但随后真实鼠标新线成功出现。该结果证明“隐藏 Settings 捕获残留”与“RTS 画笔被阻断”不能直接画等号。证据由主会话执行，保存在 `Build/CanvasFreezeInvestigation/window-interaction/capture-hide.json` / `capture-hide.png`；子代理没有独立运行该实验。是否影响 Bar 工具切换由主会话继续验证。

### 2. 官方合同：三套通知不可混为一谈

| 合同 | 直接支持的事实 | 不支持的推断 |
| --- | --- | --- |
| [IRealTimeStylus::WindowInputRectangle](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-get_windowinputrectangle) | 空 rectangle 默认收集整个窗口，窗口 resize 后仍如此；一旦开始收集笔数据，会持续到抬笔，即使笔移出输入区域。 | 不能因未显式 SetCapture 或笔移出窗口就断言 RTS Up 会丢。该段没有明确保证隐藏/禁用/桌面切换后的每种边界。 |
| [RTS plug-in data](https://learn.microsoft.com/en-us/windows/win32/tablet/plug-in-data-and-the-realtimestylus-class) | InRange/OutOfRange 的总体描述还包括进入/离开 RTS 输入区域；同步与异步队列不同；Enabled/Disabled 由状态设置产生，异步队列可能延迟处理。 | 不能认为每个 OutOfRange 都是物理接触已经终止，也不能将异步插件清队列丢通知规则套到产品唯一同步插件。 |
| [IStylusPlugin::StylusOutOfRange](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-stylusoutofrange) | 方法参考将其描述为离开检测范围，参数是 tablet context 与 stylus ID。 | 文档没有承诺普通窗口隐藏必定先调用本方法，也没有明确要求无条件用它替代 Up。它与总体概览描述的范围需以真实事件序列验证。 |
| [WM_POINTERCAPTURECHANGED](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointercapturechanged) | Windows Pointer 捕获丢失后通常不会继续收到该 pointer 的后续通知；应用应结束相关处理并清理。 | 这是 WM_POINTER 消息合同，不能直接证明 IStylusSyncPlugin::StylusUp 不再到达。当前产品 Pointer 链只管光标/触觉线索，contact 来自 RTS。 |
| [WM_CAPTURECHANGED](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-capturechanged) | ReleaseCapture 本身也能产生 mouse capture changed；应用不应在该通知里再次抢 capture。 | 鼠标 capture 变化并不证明触摸 RTS 失效，也不证明当前产品的 RTS 鼠标 contact 失效。主会话已有反证。 |
| [WM_CANCELMODE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-cancelmode) | 显示某些对话框或禁用窗口时可发出；DefWindowProc 取消内部菜单/滚动条状态并释放鼠标捕获。 | 该文档不承诺发送 RTS Disabled 或 Up；普通 Settings Show/Hide 也不能自动等同 modal dialog owner disable。 |
| [StylusInput threading](https://learn.microsoft.com/en-us/windows/win32/tablet/threading-considerations-for-the-stylusinput-apis) | 同步插件一般运行在 RTS 高优先级线程，Enabled/Disabled 在修改状态的线程；插件同步跨线程调用可阻塞 ink collection。 | 只找到同步等待位置不足以证明本产品的正常窗口交互形成锁环，需要实际栈和调用闭环。 |

这些文档明确排除一个危险的“修复”捷径：**无条件将任意失焦、捕获变化或 OutOfRange 当成 RTS Up/Cancel。** 它可能截断仍被 RTS 正常收集、已经移出输入区域的合法笔画。若最终证实某个边界必须主动封口，需用已证事件、RTS identity 与产品场景代次限定，而不是广播清空所有 contact。

### 3. 对真实产品终态路径的针对性核对

#### 3.1 有 Up 回调但数据不完整，不等于遗留 producer

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1241–1250`：Up 先记录到达，再取得 writer。
- `:1256–1265`：packet 为 null 时主动 `CloseProducerContact`，删除 binding，并平衡 Touch End。
- `:1267–1285`：binding decoder 缺失或 packet 解码失败仍执行 Cancel 式 Close，不会仅返回而遗留 producing route。
- `:1291–1299`：正常 Up 发布后删除 binding，并平衡 Touch End。
- `:1170–1177`：同 `(tcid,cid)` 再次 Down 会先结束旧 binding；所以即使某次终态异常，也不能忽略后续正常重复 identity 的恢复能力。

因此，不能再用“窗口切换导致 Up 没 packet”解释永久孤儿。必须证明 **Up 回调根本不到达，或卡在 writer/下游调用内，或 Close 本身实际失败**。该区别也是探针必须同时记录 Win32 与 RTS 事件的原因。

#### 3.2 生命周期恢复与窗口状态不是同一个字段

- `Draw3.RealtimeStylus.cpp:1086–1112`：Enabled/Disabled 重置 decoder/contact；Enabled 随后 BeginLifecycle 并建 decoder。
- `:1442–1452`、`:1473–1479`：TabletRemoved、UpdateMapping 收拢旧 contact 后重新建当前 contexts。
- `:1455–1469`：Error 收拢活动 contact，但不故意将已发布 decoder lifecycle 永久关闭。
- `:2531`、`:2542`、`:2687`：唯一 RTS 绑定主 Drawpad，AllTablets 模式包含鼠标，并启用收集。
- `:2717–2734`：显式禁用/卸载在 Shutdown，并收拢 producer。

未找到普通设置关闭、Explorer 启动、主窗失焦直接调用 `put_Enabled(FALSE)` 或替换 HWND 的代码。`Draw3.WindowControl.cpp:1526–1528` 对 Show/Activate 仅请求呈现，不检查 RTS 内部状态；这一点意味着“如果系统 collector 自己停投递”，应用没有该处的恢复动作，但 **并不证明系统会在这些步骤停投递**。

#### 3.3 Pointer、capture 与 RTS 的边界

- `Draw3.WindowControl.cpp:1290–1337` 的 Pointer Leave 清 Pen 光标/触觉线索，不结束 coordinator contact。
- `:1339–1469` 的 Pointer Down/Move/Up 处理光标、Touch 视觉归属与 Pen 触觉线索；真正接触输入仍是 RTS。
- 该 switch 没有将 `WM_CANCELMODE`、`WM_CAPTURECHANGED` 或 `WM_POINTERCAPTURECHANGED` 转换成 RTS terminal。没有相应处理是静态事实，不是自动确认的 bug，因为需先建立两个协议之间的正确映射。
- `Window.cpp:1999–2008` 在选择态隐藏主 Drawpad 前尝试释放主窗 capture；这不会为 RTS 手工发布终态。全局退出隐藏组的释放在 `:1953–1963`，也属于窗口层行为。

### 4. 新增真实 OS 探针

文件：`research/rts-window-boundary-probe.cpp`。

探针创建两个属于自身进程的 Win32 窗口；独立 MTA 线程将真实 `IRealTimeStylus` 绑定画布窗口，使用 Free Threaded Marshaler 和同步插件，设置 AllTablets、关闭 Flicks、启用 MultiTouch。它与产品相同采用“RTS 创建线程和 HWND 线程不同”的结构，但 **没有产品 decoder、admission、Controller、renderer 或 Setting**。

每次回调仅写固定容量预分配事件数组，结束后写 CSV；没有控制台输出和 callback 内文件写入。记录实际 RTS Enabled/Disabled/Down/Packets/Up/InRange/OutOfRange/Mapping/Error、Win32 capture/focus/show/enable/pointer 消息、真实注入结果、foreground/capture/visibility 状态。

自动实验的四段顺序：

1. phase 0：一笔正常 OS Down/Move/Up，建立本机真实 RTS 正对照。
2. phase 1 → 2：另一笔真实 Down；接触仍按住时执行选定窗口动作；继续真实 Move，并始终发出正常 OS Up。
3. phase 3：恢复窗口，但不主动 SetForegroundWindow；再发一笔完整新接触。
4. phase 4：显式 SetForegroundWindow 作为恢复探测，再发完整一笔。

支持动作：`none`、`leave`（移出输入区正对照）、`hide`、`disable`、`minimize`、`cover`、`focus`、`capture`、`cancel`。

`capture` 先让 canvas SetCapture，再转交另一个测试窗口；`cancel` 先 SetCapture 后发真实 `WM_CANCELMODE` 给测试窗口。二者是明确可控的 API 边界实验，不能擅自称为普通 Settings 的原始触发序列。`cover` 则把另一个真实窗口覆盖在输入位置并尝试置前台。`focus` 只显示旁边窗口并尝试置前台。实际 foreground 被记录，API 未成功置前台时不能声称完成了焦点切换。

可切换 `--activate`、`--layered`、`--topmost`；默认画布是 NOACTIVATE，不强行抢焦点。该选择用于拆分“前台问题”与“窗口样式问题”，并不复刻产品的所有 DComp/owner 结构。

#### 编译与运行交接

子代理完成源码和 SDK 签名/常量静态核对；主会话已使用原生 ARM64 MSVC 编译初版及触摸参数修订版，退出代码均为 0。实际实验输出位于 `Build/CanvasFreezeInvestigation/window-probes/`；以下保留可复用命令示例。子代理没有启动 GUI 或自行构建。

```powershell
# 在已有 ARM64 MSVC 环境中执行，避免在研究目录产生 obj/exe。
cl /nologo /std:c++20 /EHsc /utf-8 `
  /Fo:Build/CanvasFreezeInvestigation/window-probes/rts-window-boundary-probe.obj `
  /Fe:Build/CanvasFreezeInvestigation/window-probes/rts-window-boundary-probe.exe `
  .trellis/tasks/10-14-canvas-freeze-investigation/research/rts-window-boundary-probe.cpp `
  /link user32.lib gdi32.lib ole32.lib uuid.lib

# 每次会显示测试窗口、操控鼠标或触摸输入，约 9 秒自动退出。
Build/CanvasFreezeInvestigation/window-probes/rts-window-boundary-probe.exe `
  --action none --device mouse --layered --topmost --out Build/CanvasFreezeInvestigation/window-probes/none-mouse.csv
```

先跑 `none mouse`；如使用 touch，另跑 `none touch`。基线得到真实 RTS Down/Up 后，才解释其他 action 的结果。`--device manual` 不注入输入，运行 30 秒；B/R/F/Escape 分别执行边界、恢复、前台恢复和结束。NOACTIVATE 窗口不保证取得键盘焦点，主会话也可向探针自身 HWND 定向发送这些按键消息；物理接触仍须完整抬起。

触摸注入设计遵守 [InjectTouchInput 官方合同](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-injecttouchinput)：按住期间周期性 UPDATE，Up 坐标与最后成功 UPDATE 一致，每次序列均结束；API 失败会标记 trial 无效并返回非零。这样不能把注入本身被系统取消误认为产品丢 Up。注入触摸是否进入当前设备 RTS 必须以 baseline 证实；单凭函数返回成功不能推断所有触摸设备等价。

#### 2026-10-15 主会话实测与触摸参数纠正

主会话已完成本机 Windows 11 ARM64 实验，画布样式为 NOACTIVATE + LAYERED + TOPMOST，真实 OS 输入进入真实 RTS：

- 鼠标 `none/hide/cover/focus/capture/minimize/disable/cancel/leave` 共九组，每组均实际收到 4 Down / 4 Up。Down 所在 phase 为 `0,1,3,4`，Up 为 `0,2,3,4`；跨边界旧接触有正常 Up，恢复后的两次新接触完整，汇总中 errors=0。证据：`Build/CanvasFreezeInvestigation/window-probes/mouse-matrix-summary.json` 及各动作 CSV。
- 原触摸探针 `InitializeTouchInjection(1,...)` + `pointerId=7` 的所有首次 Down 都返回 87（ERROR_INVALID_PARAMETER），退出代码 3，没有建立任何 RTS Down/Up。证据：`window-probes/none-touch.csv`。这是**诊断探针无效**，不能解释成产品丢 Up 或 RTS 卡死。
- 已仅修改研究目录独立探针：单触点默认 `pointerId=0`，保留 `--touch-id 7` 用于单因素对照；新增初始化结果、容量/ID、结构大小、屏幕范围、flags/坐标/可选字段日志。产品代码没有修改。
- 参数修订版默认 ID 0 的触摸 `none` 基线退出代码 0、真实 RTS 4 Down / 4 Up。初始化成功；本机结构大小 `POINTER_TOUCH_INFO=144`、`POINTER_INFO=96`，虚拟屏幕 `0,0,2880,1920`。证据：`window-probes/none-touch-id0.csv`。
- 同一修订版 `--touch-id 7` 的单因素复测已完成：初始化成功，四次 Down 仍返回 87，真实 RTS Down/Up 均为 0；坐标及 Down flags 与 ID 0 对照相同。证据：`window-probes/none-touch-id7.csv`。因此初版触摸基线失败在本机可归因于 maxCount=1、pointerId=7 这个参数组合，改用 ID 0 已实测纠正。触摸窗口边界矩阵已全部完成，实际状态与生命周期复核见下。

官方参数核对：[InitializeTouchInjection](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-initializetouchinjection) 明确 maxCount 是同时接触数上限，范围 1..256；[POINTER_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_info) 说明 pointerId 在接触生存期内唯一，**这两页没有明确写出 pointerId 必须小于 maxCount**。本机 A/B 已确认 ID 7 失败而 ID 0 成功，但尚未测试其他容量或其他 Windows 版本，不能把 pointerId<maxCount 这一推测写成微软文档保证。当前 Down flags 为 INRANGE|INCONTACT|DOWN，touchFlags 为 0，contact RECT 围绕坐标，orientation=90、pressure=512，符合 [POINTER_TOUCH_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_touch_info) 对有效字段范围的说明；时间戳均为 0，允许系统赋值。首次 Down 即失败也排除了“保持时间太长”或“Up 坐标与上一 UPDATE 不一致”作为这次错误原因。

这些结果缩小了候选：在本机已测样式和输入路径上，鼠标与注入触摸均未因下列实际达成的窗口边界而丢失 RTS 终态或持续停投递。它们不是“强触发必卡死”的证据。结果不替代物理触摸屏及用户机器验收，也不含产品中的 decoder、admission 和渲染状态。

#### 触摸完整矩阵、窗口实际状态与生命周期复核

已读取 `Build/CanvasFreezeInvestigation/window-probes/touch-matrix-summary.json`，并逐组检查 `{action}-touch-id0.csv` 的窗口事件和状态快照；鼠标对应 `{action}-mouse.csv` 也完成状态对照。触摸九组 `none/hide/cover/focus/capture/minimize/disable/cancel/leave` 每组均为真实 RTS **4 Down / 4 Up**，Down phase=`0,1,3,4`、Up phase=`0,2,3,4`，RTS Error 和注入失败均为 0。鼠标九组同为每组 4/4。两种输入各合计 36 Down / 36 Up。

下表中的状态值均取实际日志，不能仅由动作名称推断。`canvas-state` 字段依次为 canvas HWND、IsWindowVisible、IsWindowEnabled、IsIconic；`after-boundary` 依次为 GetForegroundWindow、GetCapture、本 GUI 线程的 hwndFocus、GetGUIThreadInfo 成功标志。这里 hwndFocus=0 表示探针 GUI 线程无焦点，不表示整个桌面没有前台窗口。

| 动作 | 已实际达成的状态／事件 | 结论范围 |
| --- | --- | --- |
| `none` | canvas 可见、启用、非最小化；phase 0–3 前台均为其他 HWND，phase 4 才变为 canvas。 | 无窗口边界正对照，证明 NOACTIVATE 后台画布仍收集该输入。 |
| `hide` | 鼠标和触摸均有 `WM_SHOWWINDOW(wParam=0)`，visible `1→0`；恢复时 `WM_SHOWWINDOW(1)`、visible 回到 1。触摸 CSV index 109/111 为隐藏消息与状态，145 为跨边界 Up。 | 隐藏真实生效；此期间旧接触仍有 Up，重新显示后的新接触正常。没有验证在隐藏期间新起笔。 |
| `disable` | 两种输入均有 `WM_CANCELMODE` 后 `WM_ENABLE(0)`；enabled `1→0`，恢复时 `WM_ENABLE(1)`、enabled 回到 1。 | 禁用真实生效；跨边界旧接触终态以及重新启用后的新接触完整。 |
| `minimize` | 两种输入均有 `WM_SIZE(SIZE_MINIMIZED=1)`；IsIconic `0→1`，恢复时 `WM_SIZE(SIZE_RESTORED=0)`、IsIconic 回到 0。触摸 index 109/111、150/152；保持最小化时收到 phase 2 Up。 | 最小化真实生效，不能因 IsWindowVisible 仍为 1 就误判未最小化。 |
| `capture` | `capture-established` 为 canvas；紧接 canvas `WM_CAPTURECHANGED(lParam=other)`，after-boundary GetCapture=other。触摸为 canvas `790336`→other `3280672`；鼠标为 `1507472`→`1182742`。恢复时 other 收到 `WM_CAPTURECHANGED(lParam=0)`、GetCapture=0。 | 鼠标 capture 转移真实生效；不等同于 Pointer/RTS capture 转移。两种 RTS 输入均正常。 |
| `cancel` | canvas 实收 `WM_CANCELMODE`，紧接 `WM_CAPTURECHANGED(lParam=0)`，after-boundary GetCapture=0。 | 被测窗口收到取消消息并释放 mouse capture；这是探针显式发送消息的边界实验，不是已证明普通 Settings 自动产生同一序列。 |
| `focus` | 障碍窗实收 `WM_SHOWWINDOW(1)`、`WM_ACTIVATE(WA_ACTIVE)`、`WM_SETFOCUS`。触摸前台从 `328484` 变成障碍窗 `1773234`，GetFocus 同为 `1773234`；鼠标从 `1445208` 变成障碍窗 `724574`。 | 前台／焦点转交障碍窗真实成功。但边界前 canvas 本来就没有前台或键盘焦点，故不是“canvas 从已激活状态失焦”的覆盖测试。 |
| `cover` | 障碍窗实收 `WM_ACTIVATE(WA_ACTIVE)`、`WM_SETFOCUS`、`WM_SIZE`（client 674×429）。触摸前台从 `328484` 变为障碍窗 `4001152`，GetFocus 同值；鼠标前台从 `1445208` 变为 `462632`。 | 障碍窗取得前台／焦点和尺寸变化均已证实。源码请求 `(100,100,700,500)` 与 canvas 同外框且 TOPMOST，但日志未记录障碍窗 GetWindowRect／IsWindowVisible／WindowFromPoint／完整 Z 序；不能把请求位置直接写成独立验证了像素级遮挡或命中。 |
| `leave` | 触摸边界后正常 Up 的注入坐标为 `(2860,1870)`；canvas 收到 `WM_POINTERLEAVE` 的 lParam 解码也为 `(2860,1870)`，远离原 canvas `(100,100,700,500)`。跨边界真实 RTS Up 在 index 146，OutOfRange 在 index 149。鼠标该次 Move 注入成功、RTS Up 完整，但旧版 CSV 未记录实际屏幕坐标。 | 触摸移出区域有系统坐标旁证，且正常终态没有消失；鼠标空间位置证据弱于触摸，不能仅据 action 名称断言坐标已独立测量。 |

所有 18 组在 phase 3 恢复后，canvas 都实际处于 visible=1、enabled=1、IsIconic=0，且前台仍不是 canvas，第一笔新接触已经成功。随后 phase 4 的 SetForegroundWindow 均实际使 foreground 与本线程 focus 等于 canvas，再一笔也成功。因此 **这些运行不需要显式抢回前台才能恢复收笔或新起笔**；phase 4 不是修复卡死的证据，而是一个没有故障的恢复探测。

生命周期必须按时序解释：

- 鼠标九组各有一次初始 Enabled；唯一 Disabled 均紧接主动 `RTS-stop-begin`，是探针退出时 `put_Enabled(FALSE)` 的预期结果。窗口边界 phase 2 没有 Disabled。
- 触摸九组各在首次注入的 phase 0 出现 `RTS-tablet-added → RTS-disabled → RTS-enabled`，之后才到首个 RTS Down；Disabled→Enabled 的间隔为 15–31 ms，重新 Enabled 比首个 Down 早 31–62 ms。此序列也存在于 `none`，发生在窗口边界前，符合新增注入 tablet 期间的 context 重建现象；其确切内部触发机制尚无直接系统栈证明，不能写成 hide/focus 造成的停收集。
- 触摸九组的另一次 Disabled 均是 `RTS-stop-begin` 紧接的下一个事件，phase=4，所有四笔早已完成。窗口边界 phase 2 及后续正常绘制阶段没有新增 Disabled，也没有“Disabled 后未重开而新接触不再到”的序列。
- 触摸每组都有四次 OutOfRange，但不能只看总数判断终态问题。`leave` 的移出区域 Up 正常到达后才出现对应 OutOfRange，未支持“OutOfRange 必须替代缺失 Up”的假设。

该矩阵是对这台机器、这些样式、这两种合成输入及上述实际动作的反证。尚未覆盖 canvas 已经取得前台后再失焦、真实物理触摸／笔设备、桌面／安全桌面切换、显示设备重新枚举和产品自身的状态切换。也不应因为日志未发现系统层停投递，就把现有用户报告当作不存在。

#### 完整产品的合成触摸基线限制（2026-10-15 补测）

主会话使用 `research/touch_inject_probe.py` 操作真实产品进程，未调用产品 RTS 回调。必须区分以下记录：

- `window-touch-product/touch-baseline.*` 最初在 Selection 状态，不能用来判断 Pen 是否能画。
- PID 29988 的 `touch-valid-baseline.json/png` 已正确切软笔，触摸从 `(500,480)` 到 `(900,560)` 无墨迹；26 次 InjectTouchInput 均成功，错误码均 0。之后同进程鼠标 `(500,680)` 到 `(900,760)` 成功画出红线。该运行诊断关闭。
- PID 872 的 `diag-comparison.json/png` 和 `diag-stdout.log` 重复同一对照，开启 Cursor 诊断；stdout/stderr 指向普通文件，不是停止读取的 pipe。相同 26 次触摸注入全部成功，鼠标能成笔。
- 上述现象均发生在 Settings／Explorer 操作**之前**，没有“先能触摸成笔，经过窗口操作后才不行”的转变。因此完整产品的触摸窗口操作实验尚未建立前置正对照，不能称为复现用户的窗口诱发卡死，更不能据此证明 F1、F2 或 F3。

对已保存日志的更精确判断：

1. 触摸不是完全没有进入产品：`diag-stdout.log` 的 seq 7–12 实际记录了触摸 id 193、PT_TOUCH、`WM_POINTERENTER`、`WM_POINTERDOWN`、`x=500,y=480`；seq 192 记录 `WM_POINTERUP`、`x=900,y=560`。中间有 UPDATE，兼容鼠标被正常识别为 promoted-pointer。Pointer ID 从注入者的 0 映射成接收者的 193，本身不能当成异常。
2. 该时间段只有随后鼠标的 RTS Down arrival/published（seq 229/230）及 Up arrival/published（seq 444/445），没有这笔触摸的 RTS arrival。`Draw3.RealtimeStylus.cpp:1141–1149` 的 Down arrival 记录位于 stateWriter 锁、decoder 与 publish/admission 前，Up 对应 `:1244`。因此这份证据不支持“触摸已经到达 RTS，随后被 decoder、F3 sleep 或 admission 吞掉”的说法。
3. 文件后来出现 `dropped=176`，但不能忽略序号范围，也不能把它简单当成整个实验无效：`Draw3.Diagnostics.cpp:561–571` 在入队前递增 sequence、满队列淘汰旧项；已核对 seq **1–458 共 458 条连续无缺口**，覆盖整笔触摸与鼠标对照。第一次缺口是 **459–634**，下一条 seq 635 的 tick 已为 11454375，晚于鼠标 Up 的 11447250。故“该完整保留时间窗没有触摸 RTS arrival”成立；结论不扩大到后续丢日志时段或所有触摸硬件，也没有得到系统内部停止投递的原因。

已知差异和能排除的误读：

| 维度 | 产品与独立探针的具体差异／共同点 | 当前能得出的结论 |
| --- | --- | --- |
| 接触结构与序列 | Python sizeof(POINTER_INFO/POINTER_TOUCH_INFO)=96/144；capacity=1、id=0、PT_TOUCH、flags=0x10006/0x20006/0x40000；touchFlags=0、mask=7、orientation=90、pressure=512。24 次 UPDATE 间隔约 25 ms，Up 沿用最后成功坐标。独立 C++ 使用同一 Win32 API 和字段类别，接触半径 4，Python 半径 3。 | 不再是初版 id7 的错误 87。26 次成功证明 API 接受了序列；产品 Pointer 日志进一步证明接触已进入画布消息路径。API 成功本身不承诺 RTS 插件一定收到它。 |
| 实际 HWND／样式 | 两次产品主 Drawpad 都 visible/enabled=1、capture=0，rect `(0,0,2880,1920)`；exstyle=`0x8200088`，即 NOACTIVATE、NOREDIRECTIONBITMAP、TOOLWINDOW、TOPMOST，**不含 LAYERED 或 TRANSPARENT**。Presentation 隐藏、Freeze 可见且 TRANSPARENT。独立探针是无 owner 的小型 LAYERED 窗口，SetLayeredWindowAttributes(alpha=255)，没有 DComp。产品启动日志明确使用 DirectCompositionVisualTree。 | 两者不是相同窗口呈现／命中条件。Pointer 已到达主画布意味着不能简单说“触摸被所有窗口挡住”；DComp／NOREDIRECTIONBITMAP 只是尚未隔离的差异，不是根因。样式含义参考 [Extended Window Styles](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles)。 |
| Owner 与创建时机 | 产品 `Window.cpp:1000–1017` 为 Freeze→Presentation→主 Drawpad 链；RTS 初始化时 Drawpad 隐藏，Selection→Pen 后才显示。独立探针 owner=null、窗口先显示再创建 RTS。产品样式设置见 `IdtMain.cpp:3065–3077`。 | 初始化可见性、owner 链和呈现方式尚未逐项对照；它们不等价于设置窗口死锁。 |
| RTS 配置 | 两者都绑定自身画布 HWND、AllTablets(TRUE)、Flicks(FALSE)、MultiTouch(TRUE)、同步插件+FTM、与 HWND 不同线程创建 RTS。产品日志实际打印 MultiTouchEnabled=true、tabletWindowFlags=0x1010019、startup gate open。产品额外请求 X/Y/Pressure/Tilt/Azimuth/Altitude/Width/Height（`RealtimeStylus.cpp:2554–2579`），探针用默认 packet description。 | 不应猜产品忘记开启多点。微软说明设备不支持的期望属性会被省略，并不据此承诺停止全部回调；不能直接把扩展属性请求判为缺失触摸的原因。[SetDesiredPacketDescription](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-setdesiredpacketdescription)。 |
| WM_POINTER 与旧消息 | 产品 Pointer 分支 `WindowControl.cpp:1339–1474` 更新光标／触摸线索后 break，最终 `:1826` 调用 DefWindowProc；探针也调用 DefWindowProc。产品 `IdtMain.cpp:3108–3114` 的 Drawpad created 回调没有调用 RegisterTouchWindow，注册仅用于 Bar/PPT（`:3115–3128`）；探针也没注册。 | 不能编造“产品 return 0 吞了 Pointer”“产品忘记 RegisterTouchWindow”或“Drawpad 同 HWND 注册 WM_TOUCH 与 RTS 冲突”。微软 [WM_POINTERDOWN](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointerdown) 描述 Pointer 的默认处理，不保证每条 Pointer 都转换成 RTS。 |
| 进程边界与安全 | C++ 探针在自己的进程里初始化 touch injector、创建窗口、创建 RTS；Python 从另一个进程向产品注入。产品还有 SuperTop/UIAccess 相关启动分支（`IdtMain.cpp:1964,2105`），当前采样未保存双方令牌完整性、UIAccess 与实际桌面身份。 | 无足够证据断言 UIPI/权限就是原因；Pointer 已收到及鼠标成功，也不支持“所有注入被安全边界拦截”。[UIPI 官方说明](https://learn.microsoft.com/previous-versions/windows/it-pro/windows-10/security/threat-protection/security-policy-settings/user-account-control-only-elevate-uiaccess-applications-that-are-installed-in-secure-locations) 只给出跨完整性消息限制，不能替代本机测量。不要把 WinRT InputInjector 的 inputInjectionBrokered capability 套到当前 user32!InjectTouchInput。 |
| DPI | 独立探针显式 SetProcessDPIAware；产品采用 PerMonitorHighDPIAware 并主动设置 DPI awareness；Python helper 自身没有改变 awareness。 | 文档提醒 Pointer 坐标可能受 DPI 虚拟化影响，但本次产品实际读到预期的 `(500,480)` 和 `(900,560)`，且画布覆盖这一区域；不能据此认定打错屏幕位置。[WM_POINTERDOWN](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointerdown)。 |

本补测确定了一个需保留的环境／输入路径差异：**合成触摸能到完整产品的 Pointer 消息链，但这段完整保留的诊断时间窗中未出现对应 RTS arrival；同进程鼠标 RTS 和绘制仍工作。** 当前不能区分 collector 的注入兼容性、窗口初始化／样式影响或其他未记录条件，不能把这个差异宣称为用户使用过程中卡死的根因。要继续触摸窗口操作归因，前提仍是完整产品先有同一种输入的成功成笔基线；本子任务没有继续启动 GUI、添加探针或修改产品。

### 5. 如何解释探针与产品实验

| 观察 | 可确认结论 | 后续最小步骤 |
| --- | --- | --- |
| `none` 没有 RTS Down | 当前合成输入路径没有建立正对照，或 RTS 启动失败 | 查看初始化 HRESULT；改用真实硬件。不能把后续没有 Up 当故障。 |
| boundary 后旧接触有 Up，恢复后新接触也有 Down/Up | 该 OS/样式/输入路径未出现 RTS 终态丢失或持续停投递 | 不再把该动作当已证原因；转到产品的消费与窗口层证据。 |
| boundary 后旧 Up 真未到达，但恢复后新 Down/Up 正常 | 已得到 OS 边界的终态缺口证据，尚不是全局画布永久卡死 | 按真实 `(tcid,cid)` 序列重放到生产 coordinator/controller，检验 duplicate Down 和生命周期恢复；不能省略恢复事件。 |
| boundary 后后续 Down 均不再到，显式 focus 才恢复 | 该 OS 探针证实前台相关的 RTS 停投递 | 在当前产品中对同一动作/样式重测，保留线程栈和真实前台状态，区分 collector 与产品 writer 阻塞。 |
| 产品画布墨迹仍更新但 Bar 无法切换，隐藏窗口仍捕获 | 倾向窗口交互/工具发布受影响；不是画布渲染线程已死 | 记录同一点击是否进入 Bar clickFunc 与 state/bridge revision；不能以 hover 或高亮当作业务执行。 |
| 所有触摸报告、少数鼠标报告 | 输入渠道分布线索 | 不能拼成同一现场多设备一起死；分别建立每种输入的正对照和失败事件链。 |

### 6. 与其他窗口最值得执行的产品动作

已在研究初期发给主会话，避免只看录屏：

- 无活动接触时，Settings/Explorer 取得前台并关闭；回到可见主 Drawpad 后用真实新 Down/Up 画线。此项检验 collector 持续性，不掺入孤儿旧 contact。
- 真实接触保持按下时执行窗口覆盖、隐藏或前台切换，然后完整正常抬起。必须记录系统送达了哪些 RTS/Pointer/capture 事件，不能从“用户抬手了”跳到“RTS Up 已到达”，也不能人为不发 Up。
- 失败后先取证，再试一次明确前台变化（例如任务栏/测试窗口后回画布）及新接触。恢复仅说明前台关联，不能据此命名为死锁。

未自动执行桌面/安全桌面切换、真实显示器重配置或设备移除。Windows 8 Pointer 与 Windows 7 RTS 的合同不同；本机 Win11 ARM64 的通过也不能替代 Win7 或用户触摸硬件验收。

### Files found / code patterns

| 文件 | 一行说明 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | 唯一同步 RTS 插件、终态 fallback、lifecycle 与 COM 线程 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | Pointer 光标/触觉线索与 Show/Activate 消息，不是 contact producer |
| `Inkeys/Inkeys/Window/Window.cpp` | Surface 显隐和 capture 释放；不同于 RTS terminal |
| `C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um/RTSCOM.h` | 核对实际 IStylusPlugin 签名与 RTSDI_SystemEvents 枚举 |
| `C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um/tpcshrd.h` | TabletPenService 属性与多点、Flicks 等窗口标志 |
| `research/rts-window-boundary-probe.cpp` | 真实 OS+RTS 两窗边界探针，完整配对系统输入，未直接调用产品回调 |

### Related specs

- `.trellis/spec/native-desktop/index.md`、`cpp-conventions.md`：不以静态候选代替真实线程证据，保持最小修改。
- `.trellis/spec/native-desktop/input-and-ink.md`：唯一 RTS contact producer、Mouse cursor 与墨迹不同、真实输入与 hidden injection 验证边界。
- `.trellis/spec/native-desktop/draw3-integration.md`：主/辅助 Drawpad 的输入所有权、显隐握手和独立图形设备。

## Caveats / Not Found

- 本轮没有将既有三类已验证问题重复作为新发现，没有新增“正常系统操作必定造成全局画布卡死”的证明。
- 独立 probe 只验证 OS 事件路由，不含生产 decoder/controller/renderer。主会话已编译并完成鼠标与注入触摸各九组、每组 4/4；窗口状态和生命周期已复核，仍不能把它们写成产品完整验收通过。
- 缺少 WM_POINTERCAPTURECHANGED 清理并不自动等于 RTS contact 漏终态。直接加 Cancel 存在截断合法笔画的风险，需要先证实映射合同。
- 传统 WPF Stylus.Capture 示例和未经复现的社区焦点报告未用作产品根因证据；本轮外部技术结论只依赖 Microsoft 官方 API/概览文档。
- 主会话捕获残留实验仅证明该次鼠标 RTS 绘制仍然工作；不自动排除 Bar 输入受阻，也不替代触摸设备测试。
