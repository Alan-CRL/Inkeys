# 输入来源仲裁方案（讨论稿，未实施）

日期：2026-11-03。任务shape-style-tablet-bar。起因见 research/touch-registration-log-analysis.md：M6数位板只发PT_PEN Pointer、不发WM_TOUCH，统一WM_TOUCH后笔无法点击。本文只做设计讨论，不改代码。

## 1. 问题模型：重复来自“派生关系”，不是“多个设备”

一次物理接触在系统里只有一个根：**Pointer（A，WM_POINTER*）**。其余都是系统在 `DefWindowProc` 处理A时派生的兼容视图：

| 视图 | 来源 | 是否必然出现 |
| --- | --- | --- |
| A WM_POINTER*（PT_TOUCH/PT_PEN） | 根，Win8+ | 手指/笔都有（M6与旧板日志均证实） |
| B WM_TOUCH | A经DefWindowProc派生 | 手指有；笔取决于设备/驱动（旧板有TOUCHEVENTF_PEN，M6没有） |
| C 兼容WM_MOUSE*（extra=0xFF5157xx） | A经DefWindowProc派生 | 笔/手指都有 |
| 真实鼠标/Raw | 独立物理源，不是派生 | — |

此前选B当来源，等于押注“每个接触都会派生B”，这个前提对笔不成立。B与C互为兄弟，都晚于A到达：只看B和C，无法在不引入等待的情况下判定“B还会不会来”，所以任何“先看到谁就用谁/N毫秒内不接受”的方案本质上都是时间启发式。结论：**来源必须选在派生树的根（A），并让派生边不存在或按类型整体排除**，而不是在叶子上去重。

## 2. 方案：Pointer为根，派生视图按类型排除

三条规则，互不依赖时间：

1. **能力模式一次定终身。** 启动时动态解析 `GetPointerInfo`（现有Bar触觉观察已这样做）。可用 → `PointerMode`；不可用（Win7）→ `LegacyTouchMode`，保持现状（仅WM_TOUCH，不新增行为）。模式是进程级常量，同一进程内不会同时有两个接触来源。
2. **业务来源集合按类型固定。**
   - PointerMode：业务来源 = WM_POINTER*（PT_TOUCH/PT_PEN 的接触阶段）+ 无Pointer签名的真实Mouse（含Raw补充）。
   - 被排除：WM_TOUCH（读取并关闭句柄后丢弃，仅计诊断）、带 `IsPointerGeneratedMouseMessage` 签名的兼容Mouse（现有过滤保留）。Pen/Touch低位分类仍只用于诊断，来源取自Pointer自己的 `pointerType`，因此不再受“同接触分类不稳定”影响。
3. **在源头不生成派生视图。** 五个UI窗对“接触阶段”的Pointer消息（DOWN；INCONTACT的UPDATE；UP；CANCELED/CAPTURECHANGED）处理后直接 `return 0`，不交 `DefWindowProc`；悬停（非接触的ENTER/UPDATE/LEAVE）仍交 `DefWindowProc` 保持现有行为。这样B、C一般根本不会产生，规则2只是兜底。待实测确认（见§5）。同时系统的“长按→右键”合成也随之消失，满足最初用WM_TOUCH要拒绝长按右键的目的。

真实Mouse与Pointer接触之间沿用现有的 **非鼠标租约**（`BeginTouch/nonMouseActive`，状态互斥，不是时间窗）：Pointer接触期间拒绝新的Mouse会话，反之亦然。这是对“驱动额外注入无签名鼠标”的唯一兜底，M6日志里该类事件只有悬停Move，没有按键。

## 3. 接入点：只换前端，不动业务

保留：五窗各自TouchContext、Adapter、lease/capture、presented-hit探针、worker里的长按tick、按钮状态机、Bar/Page业务回调、Draw3画布输入、Raw鼠标补充。

改动面（概念上）：
- 值型 `ContactPacket`（kind、屏幕点、device、contact id、message time、工具类型Touch/Pen、tick、触觉token）取代 Packet 里内嵌的 `TOUCHINPUT`。两个解码器各自产出该值：`DecodePointer`（PointerMode）、`DecodeTouch`（LegacyTouchMode）。Router/Adapter只认 `ContactPacket`。
- 目标窗口：PointerMode下目标 = Pointer实际接收窗（隐含`hwndTarget`），再走presented-hit探针；不用 `WindowFromPoint`，也不需要跨窗转交（WM_TOUCH滞留旧窗的问题只存在于B）。LegacyTouchMode保留现有路由。
- 接触生命周期：每次DOWN新增 route generation（M6日志里同一pointerId在一次in-range内可连续点击上百次，pointerId不能当单次按下的身份）；UP/CANCELED/LEAVE/CAPTURECHANGED收尾；仅接受 `FIRSTBUTTON` 且无 `SECONDBUTTON/笔桶键` 的接触，其余忽略（等价于拒绝右键类输入）。
- 触觉：现在是Pointer观察建凭据 → 100ms内按 device/time/point 与随后到的WM_TOUCH“事后匹配”（`ContactTokens::BindDown`，含ambiguous等拒绝分支）。PointerMode下接触包本身就是那条Pointer消息，凭据随包携带，`BindDown/Touch(...)` 的匹配与Up再匹配变成恒等，震动路径简化而不是削弱。LegacyTouchMode无Pointer，行为不变。Draw3画布侧触觉不涉及。
- 长按：PPT方向键Down立即触发，长按重复靠worker内部tick，与来源无关；UPDATE只是Move，没有“必须有Move”的依赖。
- 单一解码点：Bar当前同一消息经 channel_callback 和 WndProc 各一次（日志里每条各一份）。约束：业务只在WndProc解码，callback只做诊断与被动观察，避免进程内自我重复。

## 4. 为什么在组合上不会重复

对任一物理接触，业务来源集合里只有一个成员能产生它：
- 手指/笔 → 只有A进入业务；B因模式被丢弃，C因签名被丢弃，且多数情况下根本不产生。
- 真实鼠标 → 只有无签名Mouse进入业务；与Pointer接触之间有状态租约互斥。
- Win7 → 没有A，只有B（现状）。

不存在“两个来源都合格、需要选一个”的状态，所以不需要计时、不需要“相同消息50ms内忽略”。

## 5. 待实测确认的假设（先做小探针/诊断，再决定实现细节）

1. 对接触阶段Pointer不交DefWindowProc后，两块设备（旧板有B、M6无B）的 `WM_TOUCH`/签名Mouse计数是否降为0；若不为0，规则2的类型排除仍成立，只是日志里会出现丢弃计数。
2. 接触中笔/手指跨出窗口，UP是否仍送回DOWN窗口（隐式捕获）；以及 `POINTERCAPTURECHANGED`/`LEAVE` 的收尾次序。
3. 悬停仍交DefWindowProc时，是否出现与接触状态不一致的合成鼠标副作用（预期只有被过滤的带签名Move）。
4. Surface超轻薄触控笔2：Bar触觉凭据在PointerMode下仍能随Down/Up命中（需用户设备确认）。
5. Win10/11“长按右键”视觉圈是否消失。

## 6. 被否决的方案

- 时间窗去重/节流：用户明确排除，且本质是启发式。
- 以兼容Mouse为通用来源：失去多点触摸，并重新引入长按右键（此前改用WM_TOUCH的原因）。
- “收到Touch就用Touch，否则退回签名Mouse”：兄弟消息无法在不等待的情况下判定“Touch不会来”，必然需要计时。
- 保留DefWindowProc并只靠类型过滤：能避免重复，但仍产生长按右键合成与视觉反馈，作为兜底而非首选。

## 7. 验证方向（沿用无窗口为主）

- 真实helper的来源矩阵测试：{仅A、A+B、A+C、A+B+C、仅B(Win7)、真实Mouse、Mouse叠加Pointer接触}，断言每个物理接触恰好一个业务Down/Up。
- 触觉：Pointer携带凭据的Down/Up、取消、无凭据时只省略震动。
- 设备：M6板（仅A+C）、旧板（A+B+C）、手指、Surface笔；各自日志检查 `source_mode`、被丢弃的B/C计数与业务点击数一致。
