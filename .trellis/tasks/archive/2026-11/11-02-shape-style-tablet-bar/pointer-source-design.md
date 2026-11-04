# R3 Pointer为根的输入来源仲裁设计（用户已同意）

日期：2026-11-03。任务shape-style-tablet-bar，分支bugfix/color。依据：research/touch-registration-log-analysis.md（M6板只发PT_PEN Pointer，无WM_TOUCH，统一WM_TOUCH后笔无法点击）、research/input-source-arbitration-design.md（讨论稿）。用户已同意方案并要求写实施计划；本文件为正式设计，随后按 pointer-source-implement.md 实施。

## 目标

对五个UI窗（Bar、PPT左下/右下/中左/中右）建立**不依赖时间**的接触来源仲裁，使一次物理接触只有一个业务来源：

- 手指、笔都能点击，覆盖“只有Pointer、没有WM_TOUCH”的设备（M6）与“Pointer+WM_TOUCH+兼容Mouse”的设备（旧板、Surface笔）。
- 保持：原有按钮/拖动/选色/FineDial/白板单击/PPT按下翻页与长按交互；触摸长按；Bar笔震动；拒绝长按右键类输入；真实鼠标/触摸板/Raw；Draw3画布与Hook无变化。
- 不引入“N毫秒内忽略”类去重。

## 派生关系与来源集合

一次接触的根是Pointer（A，WM_POINTER*）。WM_TOUCH（B）与带 `0xFF5157xx` 签名的兼容Mouse（C）是系统处理A（DefWindowProc）后派生的兄弟视图，彼此无顺序保证，不借时间无法判定“B还会不会来”。因此来源取根，派生视图按类型排除。

| 模式 | 业务来源 | 排除（按类型） |
| --- | --- | --- |
| PointerMode（`GetPointerInfo`等可选API可解析，Win8+） | A 的 PT_TOUCH/PT_PEN 接触阶段；无签名真实Mouse（含Raw补充） | B：读取并关闭句柄后丢弃，仅诊断计数；C：现有 `IsPointerGeneratedMouseMessage` 过滤保留 |
| LegacyTouchMode（Win7等无Pointer API） | B（现状，路由与五套TouchContext不变）；无签名真实Mouse | C：现有过滤保留。不新增笔兼容回退 |

模式是进程级常量：启动时一次解析，运行期不切换，因此同一进程不会同时存在两个接触来源。无签名Mouse与Pointer接触之间沿用既有“非鼠标租约”（`MouseChannel::BeginTouch/EndTouch`、`nonMouseActive`）做状态互斥，不是时间窗，也是“驱动另注入无签名鼠标按键”的唯一兜底。

## 单一准入函数

新增一个纯函数（放在UiPointer领域，如 `UiPointerInput.h` 内的 `Admission` 或独立 `UiPointerSource.h`），五窗WndProc、Bar的channel_callback和测试共用，不各写一份：

```
enum class Family { Pointer, LegacyTouch, Mouse, Raw };
enum class Verdict { Business, DropDerived, ObserveOnly, DefaultProc };
Verdict Admit(Mode mode, UINT msg, WPARAM wParam, ULONG_PTR extraInfo, PointerFlagsView flags) noexcept;
```

规则表：
- PointerMode：`WM_POINTERDOWN/UPDATE/UP` 且 `INCONTACT`（及DOWN/UP边沿）+ `FIRSTBUTTON` 且无 `SECONDBUTTON/THIRDBUTTON/笔桶键` → Business；非接触的ENTER/UPDATE/LEAVE → DefaultProc（保持悬停现状）；`CANCELED/CAPTURECHANGED/LEAVE`（有活动接触时）→ 取消；`WM_TOUCH` → DropDerived；签名Mouse → DropDerived；无签名Mouse → Business（真实鼠标）。
- LegacyTouchMode：Pointer消息 → DefaultProc/ObserveOnly（只做现有触觉被动观察）；`WM_TOUCH` → Business；其余同上。
- 关键不变量：对同一物理接触，各族中至多一个族返回Business。

## 接触包：只换前端，业务不动

- 以值型 `Contact`（device、id、`Kind`、屏幕点、message time、`primary`、`pen`、触觉token）取代 `Touch::Packet` 内嵌的 `TOUCHINPUT input`。当前使用点：`Context::Owns`、router（`DispatchDecoded/Continue/LogPacket`）、Bar `DeliverBarTouch`（含触觉）、Page `DeliverTouch`、Page测试包、`ContactTokens::BindDown/Touch`。两个解码器各产出 `Contact`：`DecodeTouch`（由 `TOUCHINPUT`）与 `DecodePointer`（由 `GetPointerInfo`）。router/Adapter只认 `Contact`。
- `DispatchDecoded` 保持“同批primary先处理”规则；Pointer每条消息一个元素，`POINTER_FLAG_PRIMARY` → primary。
- 业务 `Source` 统一为 `Source::Touch`（Session/lease/`ReplacedMouse` 规则不变），工具类型在 `Contact.pen` 与诊断 `tool=pen|touch`；不再用Pen/Touch低位拆接触，不依赖兼容签名低位。
- 屏幕点取 `ptPixelLocation`（M6日志中与 `ptPixelLocationRaw` 一致）；时间取 `dwTime`。
- 目标窗口：PointerMode下目标 = Pointer实际接收窗（隐式捕获保证后续UPDATE/UP回到同窗），仍走presented-hit探针与语义/注册代次复核；**不调用 `WindowFromPoint`、不做跨窗转交**（WM_TOUCH滞留旧窗问题只属于B）。LegacyTouchMode保留现有 `CandidateAt` 路由与 `QueryScope`。
- 生命周期：每次DOWN新建route generation（M6中同一pointerId在一次in-range内可连续点击上百次，pointerId不能充当单次按下身份）；UP/`POINTER_FLAG_CANCELED`/`WM_POINTERCAPTURECHANGED`/接触中的`WM_POINTERLEAVE`/窗口隐藏销毁 → 复用现有 `CancelTarget` 收尾，取消不生成Click。
- 解码点唯一：Bar当前同一消息经channel_callback与WndProc各见一次；Pointer接触只在WndProc解码，callback仅诊断。`GetPointerInfo(pointerId)` 必须在当次消息处理内调用并恢复调用者 `GetLastError`（沿用现有诊断helper约定）。

## 不转交DefWindowProc

接触阶段的Pointer消息处理后直接 `return 0`，使系统不再为这些接触派生B、C，同时不产生“长按→右键”合成。悬停消息仍 `DefWindowProc`。这是**待实测假设**：若实机仍有B/C到达，`Admit`的类型排除继续保证无重复，日志仅多出丢弃计数。五个窗口继续 `RegisterTouchWindow(hwnd,0)`（LegacyTouchMode必须；PointerMode无害），已有注册诊断保留。

## 触觉

- 现状：Bar WndProc对Pointer消息调用 `ObserveBarHapticPointer`（已动态取 `GetPointerInfo/GetPointerCursorId`），Pointer建立凭据，再由 `ContactTokens::BindDown` 在100ms内按 device/time/point 匹配随后到的WM_TOUCH，`Touch(...)` 在Up再次匹配。
- PointerMode：接触包即那条Pointer消息，凭据按 `pointerId + device + cursor + time` 在Down直接绑定（新增 `BindPointer`），Up按同一pointerId收尾；`Matches/recent/ambiguous` 路径仅LegacyTouchMode（Surface等经B的旧路径）保留。凭据无效或缺失只省略震动，不阻UI；分页不新增震动，Draw3画布触觉不涉及。
- 目的是保持行为，不是新增：同一次笔点击，震动凭据数量与现状一致（至多一个）。

## 兼容与边界

- Win7/无Pointer API：整体回到LegacyTouchMode现状；不新增硬导入（`GetPointerInfo` 等继续 `GetProcAddress`）。
- 不改变Draw3、Hook、RTS、布局、驱动设置、共享样式；不新增分页震动、多指并发、笔悬停Bar效果（悬停仍走现有过滤，作为后续独立项）。
- 笔橡皮端/桌面桶键：以 `FIRSTBUTTON` 且无第二键过滤；橡皮端是否需 `GetPointerPenInfo` 额外拒绝，列为实机核对项，不先加依赖。
- 不添加palm/置信度新策略：`POINTER_FLAG_CONFIDENCE` 仅诊断记录，与旧WM_TOUCH行为对齐。

## 风险与回退

| 风险 | 缓解 |
| --- | --- |
| 不转交DefWindowProc导致接触阶段副作用（捕获、系统手势） | 先以诊断探针验证（计划第1阶段）；隐式捕获、LEAVE/CAPTURECHANGED次序实测；失败则降级为“转交DefWindowProc + 类型排除”并接受长按右键合成，由用户决定 |
| Pointer消息对象与WM_TOUCH批次语义差异（primary、同批多指） | 复用DispatchDecoded，单元覆盖primary/fallback与ID复用 |
| 触觉绑定改写影响Surface笔 | 保留LegacyTouchMode旧路径与Pointer观察；用户设备复核 |
| 单窗口Pointer目标与旧五窗路由合同冲突 | 目标=接收窗但仍走同一probe/语义/注册代次复核与取消通道 |
| 改动面较大 | Contact/准入/解码器分阶段落地，每阶段无窗口回归；只回退本阶段增量，不动已确认共享样式与诊断 |

范围内预计文件：`UiTouchRouting.h`、`UiPointerInput.h/.cpp`、`UiPointerDiagnostics.h`、`Bar.Interaction.cpp`、`Bar.Haptics.h`、`PageControl.cpp`、`IdtMain.cpp`（模式初始化，如需要）、`InkeysHeadlessTests/ui_pointer_tests.cpp`、`message_tests.cpp`；工程文件仅在新增源码确需登记时最小修改。

## 实测补充（2026-11-03，research/probe-first-local-capture.md）

- 阶段1结论：接触阶段 Pointer 不转交 `DefWindowProc` 对 Surface 笔、M6（直连/驱动 Ink 开）、手指均成立（195 次接触区间内派生消息 0）；画布接触产生的 WM_TOUCH/签名鼠标仍会到 UI 窗，必须按类型丢弃。
- tablet3（Ink 关）无 Pointer，笔即真实鼠标，保持真实鼠标路径，不属于来源仲裁范围。
- 设计纳入两处缺陷修复：(a) Page 的 WM_MOUSELEAVE 不得清除活动 Touch/Pen 接触的按下状态（Pointer 模式同样需要，因为 MOUSELEAVE 是无签名的真实鼠标消息）；(b) PointerMode 目标=接收窗，downOrigin==target，别窗 WM_CANCELMODE 不再退休本接触。
- 触觉：保持现有凭据绑定；stop/continuous 与 discrete 的交替待加时间戳后单独判定，不在本次范围。
