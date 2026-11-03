# M6 Touch注册诊断日志分析：数位板无WM_TOUCH、无法点击

日期：2026-11-03。任务shape-style-tablet-bar，分支bugfix/color。输入：用户回传的M6日志（6533行，x64 Release诊断包，ui_schema build Nov 3 2026T08:25:29）。原件已归档到ignored的`TestResults/touch-registration/M6-touch-registration-user-capture-20261103.txt`。本轮只读分析，未改任何产品代码。

## 结论

**不是注册问题，也不是窗口/租约/捕获问题。这块数位板在本机本配置下把笔作为原生PT_PEN Pointer送达，从未产生WM_TOUCH；而五窗统一后的业务路径只接受WM_TOUCH（及真实Mouse），因此每一次笔点击都在入口被丢弃。**

## 证据

| 问题 | 日志证据 |
| --- | --- |
| 五个UI窗注册是否成功 | `touch_registration` 5条（bar/bottom-left/bottom-right/middle-left/middle-right）全部 `registration_result=1`，无失败GLE，flags=0，调用线程=窗口owner线程 |
| 注册后是否被改掉 | 启动后 `IsTouchWindow=1`（5/5）；332条 `pointer_down` 快照全部 `is_touch=1 flags=0x0 is_touch_error=0`，owner_matches=1，窗口可用 |
| 是否收到WM_TOUCH | 全文0条WM_TOUCH；所有raw的 `touch_count=0` |
| 笔以什么形式到达 | 332条 `pointer_info`（全部 queried，API存在，查询全成功）：`pointer_type=3`(PT_PEN)，`pointer_flags=0x16016`（DOWN\|CONFIDENCE\|PRIMARY\|FIRSTBUTTON\|INCONTACT\|INRANGE），单一 `source_device=0x10093`，`hwnd_target`与接收窗100%一致。分布：bar约234、bottom-left 49、bottom-right 49；middle-left/right无笔Down |
| 系统是否补发兼容Mouse | 是。每个笔接触后跟 WM_LBUTTONDOWN/UP（各335次），`extra=0xff51570a`（Pointer来源签名） |
| 业务是否收到笔点击 | 否。1466条 `filter` 全部 `reason=pointer_promoted_mouse`；`stage=queue` 中 WM_POINTER* 条数为0；笔的 Down/Up 从未进入 consume/action |
| 业务点击从哪来 | 仅6次 `extra=0x0` 的真实鼠标 Down/Up（bar 5次→`bar_click ... reason=no_pen_session`，bottom-right 1次），与采集说明中“可用鼠标辅助”一致 |
| 笔在画布是否正常 | `canvas_pointer/canvas_gate` 持续出现（cursor=10，identity_match=1），画布笔输入链正常，问题仅在五个UI窗按钮 |

## 排除项

- RegisterTouchWindow失败/被取消/线程不对：全部排除（启动与每次Pen Down两次读回）。
- 五窗之一漏注册：排除。
- 跨窗转交、lease、capture、`no_pen_session` 路径：未参与。笔事件在 filter 就被丢，根本没有走到 session/lease/capture（capture_acquire 仅出现在上述鼠标点击）。
- 之前担心的“8条contention掉样”：本日志不需要它解释，Pen Down 332次全部有完整 Pointer 记录而无任何 Touch。

## 为什么没有WM_TOUCH（置信边界）

- 日志只能证明：Windows 向这块板投递的是 PT_PEN Pointer，并在 Pointer 未被业务拦截后按默认流程补发带笔签名的兼容 Mouse。WM_TOUCH 本就是触摸的兼容通道，微软并未承诺每个笔接触都会生成 WM_TOUCH。
- 首块数位板确实出现过 `TOUCHEVENTF_PEN`(0x40) 的 WM_TOUCH（flags 0x5a），说明是否有笔 Touch 由具体驱动/输入协议决定，而不是 Inkeys 注册造成。
- 不能由此判定该驱动的 Windows Ink 开关状态：本日志不读取驱动配置，且用户尚未回传 Ink 状态与 ink-on 对照日志。
- 日志里夹杂的 `source=mouse, extra=0` 的 WM_MOUSEMOVE 与笔位置同步（±1px），像驱动/系统的光标同步移动，只有悬停移动、没有按键事件；只作为观察，不当作可点击来源。

## 与既有设计的关系

touch-fix-design 已明确决定“统一WM_TOUCH、拒绝所有带Pointer签名的兼容Mouse、不新增笔回退”，touch-registration-capture 也记录“Pointer-only业务缺口仍未修”。本日志把这个缺口实锤：凡是只发 PT_PEN Pointer 而无 WM_TOUCH 的数位板，在主栏与PPT分页上完全无法点击。

## 后续方向（待用户决定，未实施）

1. **Pointer-pen 作为一等业务来源（推荐）**：五窗对 `WM_POINTERDOWN/UPDATE/UP`（PT_PEN）建立与 WM_TOUCH 同一套 TouchContext 的值型适配（复用已有的 Pointer 旁观/GetPointerInfo 动态可选接口、hwnd_target 作为接收窗），仍拒绝 Pointer 来源兼容 Mouse；按 (device, contact) 去重，避免同一接触同时有 Touch 与 Pointer 时双触发。保持 Win7 无 Pointer API 时只走 WM_TOUCH。
2. 仅在“该接触没有 WM_TOUCH/Pointer 来源”时才接纳签名兼容 Mouse：与用户此前“拒绝兼容副本”的决定冲突，不建议。
3. 要求用户开启 Windows Ink 看是否出现 WM_TOUCH：仅作为诊断对照，不是修复；不保证产生 Touch。

## 仍可补的证据（非阻塞）

同设备同包的 Windows Ink 开启对照日志（`M6-touch-registration-ink-on.txt`）与数位板品牌型号、原 Ink 状态；若方案1落地，此对照可用来确认 Ink 开/关两种模式下 Pointer 与 Touch 的去重行为。
