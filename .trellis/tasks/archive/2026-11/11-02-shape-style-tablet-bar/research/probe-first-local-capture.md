# 探针首轮本地采集：诊断开关未开，日志无效

日期：2026-11-03。用户在本机用 Surface 触控笔2、可发 WM_TOUCH 的触摸设备、鼠标各跑基线与探针（touch-probe、mouse-baseline、mouse-probe、surface-baseline、surface-probe、touch-baseline，位于 Build/x64/Release/，hash 同 Release x64 探针包）。

## 结论

六份日志均 0 条 `ui_schema`、`ui_input`、`probe_*`、`PenHaptics`、`bar_touch`/`bar_pointer`，只有 Draw3 启动、Page、窗口生命周期行。探针 `Active()` 需要环境变量与“测试触控反馈”诊断开关同时存在；诊断开关当时未开，探针从未生效，探针运行与基线是同一行为。因此用户观察到的“探针下仍可点击”由此解释，不是探针失效的证据，也不能回答 WM_TOUCH/兼容鼠标是否随之消失。

这些日志不含任何触觉凭据记录，不能判断“震动变轻/忽大忽小”的原因。

## 关于分页震动

`git log -S PublishProductHapticClick` 在 PageControl.cpp 与 Bar.Scene.cpp 无历史命中；research/page-control-haptics.md 的静态结论是分页按钮从未接入笔触觉 Click 链（PRD 亦写“分页本轮不新增震动”）。因此分页控件无震动是既有行为，不是本任务回归。

## 下一步

开启“测试触控反馈”并完整退出软件后，重跑 surface-baseline、surface-probe、touch-baseline、touch-probe；日志开头必须有 `ui_schema`，探针那份必须有 5 条 `probe_mode`。

## 重跑结果（Surface 触控笔2，诊断开关已开）

文件：Build/x64/Release/surface-baseline.txt（2008 行）、surface-probe.txt（699 行）；两份均有 `ui_schema`，探针份 5 条 `probe_mode`。

### 探针（接触阶段 Pointer 不交 DefWindowProc）

- 33 次笔接触（全部在 bar 窗，PT_PEN）全部被吞：`WM_TOUCH` 0 条、带 Pointer 签名的左键兼容消息 0 条；仅有 2 次 `extra=0x0` 真实鼠标点击。对比基线同类消息均大量出现 → “不转交”对该笔成立，派生副本不再产生。
- 33 次 Up 全部 `known_down=1 same_window=1 in_down_rect=1`：Up 回到 Down 窗口，但本轮没有拖出按下窗口，也没有分页窗口接触（隐式捕获跨窗未覆盖；router 本就按接触身份全局找 owner，不依赖此项）。
- 长按圆圈/右键菜单现象不在日志中，需用户描述。

### 基线（现状）

- 29 次笔 Down（PT_PEN，bar 13 + 分页 16）：Pointer + WM_TOUCH（17 条窗口线程入口）+ 带签名兼容鼠标（58 条）三者同时出现，派生链得到确认。
- Bar 触觉：13 次 Down 全部 `match=matched`（candidates=1/1/1/1）、Up 全部 `matched_up`；12 次 `bar_click accepted=1`，12 次 `product click=published`，24 次 `send`（12 次 discrete + 12 次 stop，hr=0），waveform=0x1003，intensity=0.500，explicit_intensity=1。软件侧请求每次相同，未见漏绑定或失败。
- 分页窗口的 16 次笔接触无任何触觉记录，与“分页按钮从未接入触觉链”一致。
- `Draw3.HapticFeedback.cpp`、`Bar.Haptics.h`、`Draw3.HapticRequest.h` 相对 HEAD 无改动。

### 对“震动变轻、忽大忽小”的判断

本基线中应用发出的触觉请求恒定（0x1003/0.5），绑定 13/13 成功，所以不是漏绑或请求强度波动；若体感不同，原因更可能在笔/系统对该请求的执行或主观差异，需与旧版（相同笔、相同按钮）对比确认。无法由本日志证明或排除回归。

## 第二轮本地采集（1/ 目录，十份日志）：探针结论与两个新缺陷根因

文件：Build/x64/Release/1/{surface,tablet1,tablet2,tablet3,touch}-{baseline,probe}.txt。均有 ui_schema；probe 份有 probe_mode。设备：Surface 触控笔2；tablet1=高漫M6直连无驱动；tablet2=M6+驱动、Windows Ink开；tablet3=M6+驱动、Windows Ink关（均关鼠标模式/笔尖模式）；touch=手指。

### 设备来源分类

| 设备 | Pointer | WM_TOUCH | 带签名兼容鼠标 | 业务现状 |
| --- | --- | --- | --- | --- |
| surface / tablet1 / tablet2（baseline） | PT_PEN（单一 sourceDevice） | 有 | 有 | 三来源同时出现，现由 WM_TOUCH 进业务 |
| touch（手指） | PT_TOUCH（flags 0x16017） | 有 | 有 | 同上 |
| tablet3（Ink 关） | 0 条 | 0 | 0 | 笔=普通鼠标（extra=0），走真实鼠标路径，可点击；无笔凭据 |

同一块 M6 在本机（tablet1）出现 WM_TOUCH，而最终用户机器昨晚的 M6 日志无 WM_TOUCH：WM_TOUCH 是否出现取决于“机器+系统+驱动”，不是笔本身 → 以 Pointer 为根的设计是必要的。

### 探针结论

- 195 次接触（surface 70、tablet1 45、tablet2 54、touch 26）全部被吞；按每次接触的 swallow_down→swallow_up 区间核对，区间内 `legacy_touch_seen` 与 `legacy_signed_mouse_seen` 增长次数为 0。
- 累计计数只在“两次接触之间”增长（surface 1 次、tablet1 4 次、tablet2 4 次），对应在画布上书写：笔点落在 Draw3 画布（Pointer 目标是画布，不被探针吞），系统仍把 WM_TOUCH/兼容鼠标派发给已注册的 UI 窗。结论：不转交对被吞接触有效；画布接触产生的 WM_TOUCH 仍会到 UI 窗，必须按类型丢弃，不能当业务来源。
- tablet3 无 Pointer，探针不影响；手指探针计数全 0。
- 所有 Up 均 same_window=1 且 in_down_rect=1：本轮未做到窗口外释放，隐式捕获跨窗仍未覆盖（router 按接触身份全局找 owner，不依赖它）。

### 缺陷1（手指，白板书写后点翻页第一下无效）根因

touch-baseline：翻页窗口 17 次接触中 5 次 `button_release reason=no_button_click`（clicked=0, hit=4, should_invoke=1），5/5 在 Down 之后同刻都消费到一条 `WM_MOUSELEAVE`（source=mouse, extra=0，不被签名过滤）；12 次成功接触 0 次出现该消息。PageControl.cpp 的 WM_MOUSELEAVE 处理（约 2418 行）执行 `state.pressStarted={}`、`lastRepeat={}`、`scene.PointerLeave()`，清掉刚建立的按下状态，Up 时 clicked=0。Bar 的 WM_MOUSELEAVE 处理不清按钮按下状态，所以主栏无此问题。MOUSELEAVE 来自此前 TrackMouseEvent 武装后光标离开（画布书写后）的延迟投递，正好撞上下一次 Down。其余 3 次 no_button_click（surface 2、tablet2 2，均 hit=0）是点在窗口背景，不是缺陷。

### 缺陷2（Surface 笔，展开“更多”后点主栏第一下只收起菜单）根因

surface-baseline 4 次 `route_cancel reason=retired`，对应主栏 Down 后 20ms 被取消（button_gesture cancel_or_release_outside）。例：37.36s 笔 Pointer 目标是 bar，但 WM_TOUCH 仍投到上次接触所在的 bottom-left 窗（同一 pointerId 2809 在范围内持续）；router 经 WindowFromPoint 选 bar，downOrigin=bottom-left。随后 bottom-left 收到 WM_CANCELMODE（菜单收起）→ `Touch::CancelWindow(bottom-left)` 按 `downOrigin==window` 把 bar 的路由一并退休 → 主栏按压被取消，只有菜单收起。仅 surface 出现（tablet 系列 route_cancel=0）。Pointer 模式下目标=接收窗，downOrigin==target，别窗的 CANCELMODE 不会牵连。

### 缺陷3（Surface 笔震动）

- 主栏：13+ 次 Down 全部凭据 matched；36 次 discrete（waveform 0x1003，intensity=0.5）均发布于 bar 点击；另有 68 次 stop 与 32 次 continuous（waveform 0x100b，explicit_intensity=0，系统默认强度，来源是画布笔触 canvas_pointer）。强弱混合的可疑来源：discrete 与 stop/continuous 在同一支笔上交替；haptic 行无时间戳，无法在日志里判定 stop 是否截断了刚发出的 discrete。
- 翻页控件：无 PublishProductHapticClick 历史、日志中 36 次发布全部紧随 bar；“好像触发过”更可能是点在翻页窗透明区域后落到画布，触发 continuous。
- 后续可选：给 haptic 事件补 tick 以判定 stop/discrete 竞争（独立项）。

## 修复候选版本机验证（Surface、手指、tablet2、tablet3；2026-11-03 18:00 构建）

日志：Build/x64/Release/{surface,touch,tablet2,tablet3}-candidate.txt（tablet1 未测）。

- PointerMode 生效：四份日志 `touch_read=0`（未读 WM_TOUCH），Surface 有 `touch_dropped`（画布接触派发的副本被丢弃），无 `client_rejected`，无 `pointer_route` 的 stale/lost/cancel 记录；Surface 60 次、手指 30 次、tablet2 33 次路由 Down，均 accepted。tablet3（Ink 关）无 Pointer，走真实鼠标，18 次翻页点击全部 activated。
- 缺陷1（手指书写后翻页首击）：16 次翻页 activated，唯一一次 no_button_click 是 hit=0 的背景点击；用户确认不再复现。
- 缺陷2（Surface“更多”展开后主栏首击）：用户确认一次点击即可执行且菜单自动收起。Surface 34 次翻页 activated。
- SetCapture 重入担忧解除：无 `bar_invalidate reason=capture_changed`。
- Surface 3 次 route_cancel：1 次是翻页切换期间（wb_switching=1，worker 按设计拒绝）的点击，2 次是翻页动作已执行后的 WM_CAPTURECHANGED（页面切换抢走捕获）取消后续 Up，无功能影响。
- 触觉：Surface 主栏 20/20 Down 绑定；19 次 product/discrete 发布；2 次点击在 Up 之后被 `bar_invalidate reason=invalid_pointer`（紧随的 UPDATE flags=0x26000，无 INRANGE，笔已离开感应范围）使已完成的凭据失效，Up 阶段 `match=no_token`，该次震动丢失。`ObserveBarHapticPointer` 在“非有效”分支调用 `Invalidate(id)` 未保留已完成凭据（LEAVE 分支已用 `keepCompleted`）；这是此前就存在的竞态，候选版 20 次里出现 2 次。tablet2 21/21 绑定无丢失（该笔无 send 记录，设备无震动后端）。
- 输入延迟：窗口线程 WM_POINTER 消息处理延迟 Surface p99=16ms/max=31ms，手指 max=16ms，tablet2 max=265ms（单次）；输入路径本身没有卡顿证据。Surface 白板两页之间“卡顿”不是输入路径造成（页切换在 Whiteboard 窗口组），日志无页切换耗时记录；诊断输出开启时 Draw3DiagnosticOutput 有 75 次 contention 丢弃，可能叠加影响。需关闭诊断后复测，并与 INKEYS_UI_TOUCH_LEGACY=1 对比判断是否与本次改动相关。
- 翻页控件无震动：仍是既有行为（从未接入 Click 凭据链），属独立功能，见 research/page-control-haptics.md。
