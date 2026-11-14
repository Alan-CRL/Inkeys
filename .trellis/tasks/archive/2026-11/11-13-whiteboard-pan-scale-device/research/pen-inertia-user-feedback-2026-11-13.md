# Research: 屏幕笔似乎没有惯性、数位板正常的只读调查

- Query：用户反馈屏幕笔似乎感受不到白板惯性，而数位板正常；先回答原因，不修改代码。
- Scope：mixed；当前生产源码、主会话只读配置/日志与历史对照结果、独立公式、Microsoft 官方 Pointer 文档。未运行产品、GUI或测试。
- Date：2026-11-13。
- 当前代码基准：主会话已先提交 `c2f21bd0761fe50696c8839c6e9ad73bd644f469`，任务保持 `in_progress`。
- 写入范围：仅本研究文件；不修改产品、测试、配置或规范，不提交或归档。

## Findings

### 1. 确定事实：屏幕笔没有被统一关闭，而是使用更短尾滑的独立参数

`Draw3.DrawingController.cpp:2675–2719` 的 `ResolveCanvasPanMotion`：

- 实际 Pen/PenTail 且 `source.kind==IntegratedPen` 时选择屏幕笔系列。
- 正确来源映射、业务物理尺寸及各轴校验通过，选择 `ScreenPenPhysical`；否则用 `ScreenPenFallback`。
- `ExternalPen` 和 `Unknown Pen` 继续 `IndirectDip`，不把显示器毫米当数位板行程。
- `Push` 在 `Draw3.DrawingController.cpp:3247–3262` 以真实 Down 锁存的 panClass 读取惯性开关；没有单独把 IntegratedPen 的惯性关掉。

参数定义在 `Draw3.CanvasNavigation.cpp:154–176,197–216`：

| 参数 | ScreenPenPhysical | ScreenPenFallback | 外置板的 IndirectDip |
| --- | ---: | ---: | ---: |
| 动作单位 | mm | 系统 DIP | 系统 DIP |
| 起步距离 | 1.5 | 8 | 15 |
| 反向/末段可靠距离 | .15 | .75 | 1 |
| 最低释放速度 | 15 mm/s | 60 DIP/s | 30 DIP/s |
| tau | .20 s | .20 s | .30 s |
| 常量减速 c | 350 mm/s² | 1750 DIP/s² | 1000 DIP/s² |
| 最终模长 cap | 1800 mm/s | 9000 DIP/s | 12000 DIP/s |

单位不同，不能把 15 和 30 直接比较成“屏幕笔更容易起滑”。直接屏幕笔真实毫米测速，而板面动作通常已经映射为屏幕像素，IndirectDip 使用映射后的光标运动。相同手臂动作并不必然对应相同数值或相同可见速度。

主会话只读当前默认产物配置 `Build/ARM64/Debug/Inkeys/Config/main.json`：`Enabled=true, Mouse=false, Touch=true, Pen=true, PenTail=false, Touchpad=true`，CanvasPan 诊断为 false。该工作区默认配置确实开启 Pen；不能把用户现象直接解释成 Pen 开关关闭。当前没有运行中的 Inkeys 进程，无法据此保证用户此前启动的 EXE 路径或真实来源分类。

### 2. “短尾”能解释较不明显，但不能自动解释快速甩动完全没有尾滑

同方向、无 boost、未限位的闭式为：

`T=tau*log1p(v0/(c*tau))`，`D=tau*v0-c*tau*T`。

独立公式对照，未运行 C++，也不是本轮硬件测量：

| 屏幕笔物理释放速度 | 理论尾距 | 理论停时 |
| ---: | ---: | ---: |
| 100 mm/s | 7.58 mm | .178 s |
| 300 mm/s | 36.69 mm | .333 s |
| 500 mm/s | 70.64 mm | .419 s |

例如仅作为单位说明取 `rho=.20 mm/DIP`，同样可见 1000 DIP/s 释放：屏幕笔物理域约尾滑 105.51 DIP/.270s，IndirectDip约168.03 DIP/.440s。这个 rho 不是用户当前实测值。

因此目前参数确实使屏幕笔比外置板短、更快停；但可靠的 300–500 mm/s 释放仍有数厘米尾距。若用户实际是“快甩后立即完全停住”，仅凭参数短尾不足以下定论。

### 3. 释放护栏本身可以合法产生 0 速度

这些是可观察的拒绝/衰减路径，不表示本机已经命中：

- 最近同向 50 ms 尾段至少3点、跨度至少20 ms，否则 `InsufficientSamples` / `InsufficientSpan`。见 `Draw3.CanvasNavigation.cppm:34–40`、`.cpp:326–337,400–401`。
- 反向累计达到本批门槛会清旧尾段和 boost；屏幕笔物理门槛 .15 mm。新方向证据不足不回用旧快速度。见 `.cpp:299–318`。
- 两次到达间隔小于2 ms、段位移超过本 profile cap 允许的距离（含量化界）会 `TimingRejected`。屏幕笔物理路径用1800mm/s作为这项尺度；不是拿旧12000DIP/s直接当毫米。见 `.cpp:280–296`。回调 arrival 的压缩/批次情况需要实际数据，不能从源码断言设备不支持。
- Up距最后有效Move≤30 ms保持，30–70 ms衰减，≥70 ms为 `Stale` / 0。末段速度上限还会截掉50ms拟合残留的旧高速。见 `.cpp:995–1029`。
- 最后有效速度必须严格超过本 profile最低释放值，且已越过起步slop；否则 `Stationary` / `WithinSlop`。见 `.cpp:517–544,1027–1051`。

数位板的光标映射及旧DIP门槛与这些物理门槛不同，所以数位板有惯性不能证明屏幕笔实际释放估计必然合格。

### 4. 已排除两条错误解释

1. **不是产品启用了“笔靠近刹停”。** `Draw3.DrawingController.cpp:3110` 附近明确设置 `in.penHover=false`；纯内核支持这个输入位不代表生产传了 true。普通 hover 不应停止本次导航。
2. **不是所有正常 Pen Up都漏记终态。** 当前 `CanvasPanInput::Step` 的导航历史循环对所有 Pen/PenTail Up/Cancelled 都更新 `frame.penTerminalQpc`（`.cpp:3058–3061`），`updateCanvasNavigation` 保存到 `suppressedPenTerminalQpc`（`.cpp:16529–16530`）。不能沿用“只给被抑制笔记录Up”的过时假设。

### 5. 有条件可证的取消缺口：晚到 Pointer contact可被当作新落笔

以下代码链确定存在：

1. RTS正常Up先通知真实token终态，再调用 `PublishDefaultPenCursor(token)`，随后发布coordinator Up。见 `Draw3.RealtimeStylus.cpp:1388–1401`；最终调用 `ClearPenCursorSampleForContact` 在 `:1746–1748`。
2. `WindowController::ClearPenCursorSampleForContact` 在最新样本来自 Pointer 时直接返回；它有意不凭一个RTS token去清另一个无法证明同笔的Pointer样本。见 `Draw3.WindowControl.cpp:1703–1720` 的 `if (!penCursorFromRts_ ... ) return`。
3. WM_POINTERENTER/UPDATE/DOWN发布sample时，`sample.inContact`由Pointer状态得来，`sample.qpc`却是在消息处理时重新取 `QueryPerformanceCounter`，不是保存原Pointer消息的 `PerformanceCount`。见 `Draw3.WindowControl.cpp:2552–2555,2587–2614`。
4. `IsPenContactSampleFresh` 只判断 `inContact && sampleQpc>terminalQpc`，没有该Pointer接触的Down身份/原始事件时间。见 `Draw3.CanvasNavigation.cpp:89–94`。
5. 在无已登记接触的自由滑行期间，如果这个样本被认为仍新鲜且未抑制，`updateCanvasNavigation`会直接 `StopFling()`。见 `Draw3.DrawingController.cpp:16511–16518`。

所以若在某次真实屏幕笔释放附近出现这样的相对交错：

```text
RTS Up 已取得时间 t_up，导航随后可靠释放并清 registered contacts
某个仍标 inContact 的 Pointer 消息较晚被窗口处理，赋得 qpc > t_up
RTS token清除不碰这个 Pointer-latest；或 Pointer在清除后再次发布
下一导航帧把它当新鲜接触，走 !HasContacts -> StopFling
```

就可能产生“刚开始滑就被抓停”的效果。两条输入通道的时间语义与身份没被这条停止启发式完整统一；它适合解释为什么应重点调查屏幕笔，而不能只看短尾参数。

**但没有本轮真机时序，不能认定这就是用户已经命中的根因。** 代码允许这条条件链，不等于每次屏幕笔Up都会发生；正常Pointer Up会清mailbox，较旧的contact样本也会被Pen终态QPC挡住。

主会话只读历史对照确认：1b819d2d的DrawingController约16077已存在同类mailbox→`!HasContacts`→StopFling启发式；旧Window `ClearPenCursorSample`约1633为不带token的直接清除。本轮token清除变得更保守，增加了“Up时间已采但Clear前Pointer发布”的保留窗口；不能把所有晚到Pointer风险都说成本轮才有，也不能因此推断外置板一定没有Pointer。

### 6. 为什么目前不能精确断言某一释放分支

当前默认配置CanvasPan diagnostics=false，主会话检查的两份普通日志中CanvasPan/pan-context/release记录为0。没有用户这次屏幕笔操作的source、profile、selected速度、releaseReason、Up/Pointer相对时间，不能把某个护栏或上述取消链当作已发生的硬件事实。

已有诊断位置：`Draw3.DrawingController.cpp:16549–16568` 的 `release reason=... samples/span/age/timing-resets` 和 `release-profile profile/unit/selected-motion/tau/c/target-distance/duration`。它们可区分“本就没有合格释放”和“已产生非零尾滑”。不过mailbox直接StopFling的入口本身没有专用来源/停止原因日志，不能单靠日志沉默唯一认定取消原因；后续若要证实双通道问题，须同时看到实际Pointer接触状态与RTS Up的相对到达。

本轮只调查说明，不开启诊断、不修改配置或任何产品代码。

## Files Found

路径根为 `D:/Project/Inkeys/Repo/Inkeys`。

| 文件 | 作用 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 真实来源/profile解析、导航终态、mailbox启发式抓停和生产诊断 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp/.cppm` | profile初值、可靠释放护栏、friction与freshness条件 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | 实际Pen Down/Packets/Up和token清除顺序 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | Pointer/Rts发布、token-aware Clear、Pointer消息处理时QPC |
| `Inkeys/Inkeys/Other/Other.Config.cppm:440–447` | Master/Pen/PenTail等默认惯性开关 |
| `Build/ARM64/Debug/Inkeys/Config/main.json` | 主会话只读的当前默认产物实际偏好，不保证用户此前启动路径 |

## External References

- [GetPointerInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerinfo)：返回与调用线程最近取出的Pointer消息关联的信息；积压消息可合并。它不是“处理时刻的全局最新接触状态”。本报告据此把应用重取QPC与消息原始信息的区别明确列出，而不声称文档保证了RTS/Pointer的跨API顺序。
- [POINTER_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_info)：`PerformanceCount`是Pointer消息的高精度时间字段；当前PointerDetails未将它保留到导航freshness路径。现有代码未使用不等于本轮已批准替换时间系统。

以上为2026-11-13读取的Microsoft官方文档摘要，无版本升级或新API建议。

## Related Specs

- `.trellis/spec/native-desktop/canvas-pan.md`：profile参数、可靠释放、hover不刹停和Pointer身份边界。
- `.trellis/spec/native/runtime-and-rendering.md` 的白板移动模式合同：选择模式准入与终态、光标/触觉范围。
- 本任务 `design.md` / `research/pen-authority-identity.md`：当前已批准短尾目标与不可猜跨API同Down的约束。

## Caveats / Not Found

- 未执行GUI、C++、命令行测试或硬件回放；数值只是独立闭式计算。
- 用户进一步说明光标只在移动模式消失，该视觉策略由主会话另行调查；本报告不把它当作所有绘制模式均坏掉。
- 没有真实ScreenPen source/profile/Up/Pointer日志，不能保证当前运行的设备确实走ScreenPenPhysical，或证明其实际命中TimingRejected/Stationary/latePointer取消。
- 已确认accepted Pen Up终态保护存在，hover传false，默认Pen开；不能以这些不存在的遗漏作为修复理由。
- 条件性Pointer抓停入口和参数短尾均有确定源码证据；用户本机完全无惯性的最终根因仍缺一次真实释放证据，不能伪装已重现。
