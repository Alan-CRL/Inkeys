# Research: Draw3 光标归属、终态与 Win7 回退

- **Query**: 调查 Win7 单指触摸橡皮双光标、抬笔后疑似系统光标残留；追踪 RTS、窗口线程、主自绘光标、逐触点光标及系统 HCURSOR 的归属和生命周期。
- **Scope**: internal；只读生产源码、现有测试及指定历史任务，不执行实现、编译、测试或 GUI。
- **Date**: 2026-10-18
- **Task**: `.trellis/tasks/10-18-draw3-cursor-policy`；子代理 session 的 `task.py current --source` 返回 none，主会话已明确指定本目录并确认其 current 指针，不修改任务指针。

## Findings

### Files Found

以下行号均为本轮实际读取的当前文件行号；历史任务内保存的旧行号不能直接套用。

| File Path | Description |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1009` | RTS Touch Begin/End 通知；Down/Up/OutOfRange、坏包和 Reset 终态 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:798` | 有效 CursorOwner；RTS mailbox、Touch 抑制、系统光标刷新和 Pointer/Mouse 消息 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cppm:333` | 持久 owner、Touch 抑制、posted 去重及鼠标系统光标开关的原子状态 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:48` | 主样本选择、系统显隐策略、来源过滤及逐触点 visual 构造 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:10882` | 主自绘光标与逐 Touch runtime 光标的独立组装 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp:206` | 产品 Drawpad WndProc；只转发到当前运行 Host |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:2375` | Host 转发到外部 WindowController |
| `Inkeys/Inkeys/Window/Window.cpp:1115` | 窗口类默认 `IDC_ARROW` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Diagnostics.cpp:521` | 运行期 Cursor 日志格式、生产线程和 tick |
| `InkeysHeadlessTests/draw3_contact_tests.cpp:1216` | 生产过滤入口、Touch 序列及有效归属测试 |
| `.trellis/tasks/archive/2026-09/09-23-touch-eraser-cursor/validation.md:56` | 以前系统注入 Move 的修复与验收边界 |
| `.trellis/tasks/archive/2026-10/10-02-win7-main-canvas/research/input-cursor-2026-10-03.md:3` | 另一场 Win7 呈现故障的输入/光标证据边界 |

### Code Patterns

#### 1. 必须分别观察四类可见来源

**已确认**：同一接触附近看到两个圆环，不能仅凭大小把其中一个认定为系统光标。

| 来源 | 当前生产机制 | 终态与判别线索 |
| --- | --- | --- |
| 主自绘光标 `primary` | `PenCursor.cpp:48–64` 按有效 owner 从 Pen/Mouse mailbox 选样本；Touch owner 返回 nullptr。`DrawingController.cpp:10970–11018` 解析并添加主 visual | `visual source=primary`；Eraser 可受 Mouse/Pen 各自尺寸配置与会话影响，外观可小于 Touch 圆环 |
| 逐触点自绘光标 `touch` | `DrawingController.cpp:11043–11060` 遍历未 ended、未 awaitingReconnect、设备 Touch、工具 Eraser 的每个 runtime，按其 `lastModelSnapshot` 和本 contact 半径生成 | `visual source=touch`；`MakeTouchEraserDrawingCursorVisual` 在 `PenCursor.cpp:532–543` 强制 opacity=1；不从系统指针位置生成 |
| Windows 当前 HCURSOR | `WindowControl.cpp:1286–1291` 计算 hide 后调用 `SetCursor(hide ? nullptr : defaultCursor_)`；`defaultCursor_` 在 `:305` 加载 `IDC_ARROW` | `system hidden=...` 只记录应用当次决策；当前未采集 `GetCursorInfo`，不能凭此证明稍后 OS 的实际 cursor handle/显示态 |
| Windows Tablet/触摸反馈 | `WindowControl.cpp:40–45` 定义 tablet flags，`:310–313` 在首 contact 前写 HWND 属性，`:1504–1506` 返回 gesture flags | 当前 Draw3 路径未发现 `SetWindowFeedbackSetting` 调用；这些 flags 也未包含 TouchUIForce/TouchSwitch 项。实际 Win7 反馈外观、驱动行为及其是否受 SetCursor 控制，当前源码不能证明 |

主 visual 和逐 Touch visual 最终都在 `DrawingController.cpp:14908–14909` 调用 renderer；它们使用同一种 EraserGripCircle 外观，但来源与尺寸真值独立。`DrawingController.cpp:10981–11011` 会为主 Eraser 分别读取 Pen/Mouse 来源配置和会话尺寸；`:11047–11053` 为 Touch 按实际 runtime 半径生成。因此“额外光标小一些”与错误 Mouse primary 同时存在相容，但不是定因证据。

#### 2. 线程与系统光标调度

**已确认**：产品 Drawpad 的消息在 Window Service overlay 线程中派发，应用绘制线程另外运行。

- `Window/Window.cpp:230–239` 启动 overlay/setting 两个 jthread；`:875–888` 在对应线程创建组窗口，`:934–940` 派发消息。
- 主 Drawpad 绑定 `DrawpadMsgCallback`，辅助 DrawpadPresentation 绑定 `DefWindowProcW` 且固定穿透：`IdtMain.cpp:3195–3207,3235–3243`。
- 产品转发链为 `Draw3.Product.cpp:206–214` → `ForwardProductMessage :76–87` → `Host::ForwardMessage :2375–2386` → `WindowController::HandleExternalMessage :350–356`。这些入口直接调用，没有重新投递该 WM_MOUSE 消息或改写来源字段。Host 启动前/停止中/未附着时走默认处理。
- `Draw3.Host.cpp:1659,1880` 启动独立 drawing jthread；RTS 回调只向 sink/mailbox 发布状态，绘制循环读 mailbox 组装 visual。
- `QueueSystemCursorRefresh` 在 `WindowControl.cpp:1092–1097` 用原子位去重，再 `PostMessageW(WM_APP+1)` 到 Drawpad。投递失败会清去重位；没有同步执行 SetCursor。
- owner、Touch 抑制、工具和系统光标策略变化会请求绘制唤醒及 posted refresh，见 `:757–785,1100–1130`。`WM_APP+1` 在 `:1491–1494` 清去重位并应用；`WM_SETCURSOR/HTCLIENT` 在 `:1496–1501` 立即应用并返回 TRUE。
- `ApplyWindowCursor` 在 `:1261–1273` 先用 `GetCursorPos/WindowFromPoint` 检查实际命中主 Drawpad 或其子窗口；不命中则记录 `system-skip` 并返回。它没有使用 RTS 位置替代系统 cursor 位置。
- 窗口类有默认箭头：`Window/Window.cpp:1111–1116`。因此默认处理、posted refresh 延后及命中其他 HWND 都是需要与日志关联的边界；当前没有证据证明它们就是本次 Win7 残留的实际原因。

这一调度天然存在“RTS 先改变原子状态，窗口线程稍后应用系统 HCURSOR”的时间间隔；源码中没有固定光标隐藏定时器。不能把间隔静态存在等同用户报告的落笔闪现已复现，也不能在 RTS 线程直接调用 SetCursor 并假设能代替窗口线程生效。

#### 3. Touch Down / Up / Cancel 的生产状态

**已确认**：Win7 的 Touch 接管并非完全依赖 WM_POINTER。

1. `RealtimeStylus.cpp:915–946` 从 tablet kind 判定 Touch/Pen/Mouse；若查询不到 kind，初始设备类别为 Pen，是否发生需日志确认。现代 pointer 来源/硬件信息只在 API 存在时补充。
2. 生产 Down 在 `:1188–1191` 通过 Down publication gate 后，先按 decoder 设备通知 Touch，再解码坐标。通知 helper `:1009–1013` 只对 Touch 调用 `NotifyTouchContactBegin`。
3. `WindowControl.cpp:889–911` 写时间屏障并增加 RTS 活动 Touch 计数；通常置 `touchCursorSuppressed_=true`、清 Mouse mailbox，再排队刷新系统光标。唯一此处例外是活动 Touch Pan 已由真实 Mouse 接管，后续指头保留 Mouse。
4. `CursorOwner()` 在 `:798–804` 通过 `ResolveDrawingCursorVisualAuthority` 得到有效 owner；`PenCursor.cpp:405–411` 在 Touch 抑制有效时返回 Touch，而不把 Touch 写入持久 `cursorOwner_`。
5. `NotifyTouchContactEnd` 在 `WindowControl.cpp:921–939` 只平衡计数并记录日志，不解除 Touch 抑制、不恢复旧 Hover，也不主动调用 QueueSystemCursorRefresh。正常设计是假定终态前系统已按 Touch 策略隐藏；若 Win7 在终态之后另外恢复系统箭头，当前代码没有专门的 Touch End 重应用行为，此为待取证的时序假设。
6. `RealtimeStylus.cpp:1193–1224,1252–1255` 在 duplicate Down、binding/decode/publication 失败时平衡 Touch End；Up 的无包、坏 decoder/坏包、正常分支在 `:1284–1327` 均尽力关闭 producer/binding 并平衡已知 Touch contact。
7. Disabled/TabletRemoved/Error/UpdateMapping 路径见 `:1113–1124,1477–1520`；`ResetActiveContactState :1631–1644` 按活动 Touch binding 数补齐 End。其操作不会解除 Touch 抑制，避免 Cancel 恢复旧 Hover。
8. `WM_POINTERDOWN` 在 `WindowControl.cpp:1602–1606` 只是补时间屏障，不再增加活动数。计数由 RTS 权威维护，不能把缺少 Win7 Pointer 消息当成完全没有 Touch 生命周期。

#### 4. Win7 未知 Mouse 来源的确定边界与未确认故障路径

**已确认的当前行为**：`GetCurrentInputMessageSource` 和 GetPointerType/GetPointerInfo/GetPointerPenInfo 都动态解析，见 `WindowControl.cpp:65–91,109–175`；Win7 缺失时不会硬链接新 API，source snapshot 默认 unavailable / succeeded=false，Pointer detail 为未知。

`FilterMouseCursorMessage` 是产品调用的真实入口，`WindowControl.cpp:1859–1894` 直接按其结果早退；内部顺序为：

1. `PenCursor.cpp:449–463` 拒绝 promoted 签名、早于/等于 Touch barrier 的消息、可识别的 Pen/Touch 来源；明确 Mouse/TouchPad 可接管；没有 Pointer API 且存在有效 Pen sample 时保留 Pen fallback 拒绝。
2. `:490–496` 的 `system-touch-move` 需要来源查询成功、device unavailable、origin system，并要求 Touch 抑制仍有效。来源 API 缺失/失败不满足该条件；以前对有 API 平台的修复不能覆盖为 Win7 实机已修复。
3. `:498–504` 回退只拒绝 Touch 抑制有效、未知来源、末触点已知且坐标完全相等的无按键 WM_MOUSEMOVE。带按键位 Move 保留 button bypass；末点未知或坐标不同也不会被此回退拒绝。
4. 通过其余 Pen compatibility 和 terminal 检查后，`:1953–1969` 解除 Touch 抑制、持久 owner 设 Mouse、发布 Mouse sample；按键位直接成为 `inContact`。最后 Touch Up 不负责纠正此已接管的 Mouse sample。

**可达假设，尚未证实本次 Win7 运行**：若触摸产生一条没有 promoted 签名、晚于 Touch barrier 的未知 Move，且 Pen sample 无效，只要带按键位或末触点不同/未知，它就可能走 Mouse 接管。随后活动 Touch runtime 仍绘制自己的 Eraser，同时主 Mouse Eraser 也可显示；终态兼容 Mouse Up 又可能被过滤，使 Mouse sample 暂留 contact。这与以前事件链相似，但 Win7 本次没有实际日志，不可直接称为根因。

**必须保留的冲突**：在没有现代来源 API 的平台，真实 Mouse/TouchPad 的消息同样可能是未知来源。现有测试明确要求未知来源下的真实移动及时接管，且带按键从窗口外拖入也保留路径；简单拒绝全部未知消息、全部带键 Move、所有 Touch 后消息，或加固定冷却时间，都不能从当前证据推出。需要来源更强的证据后才能收紧边界。

#### 5. Pen 终态本应隐藏系统箭头

用户“抬笔”若泛指抬指，应按上节 Touch 链排查；若是实体触控笔，现有实现另有明确路径。

- `RealtimeStylus.cpp:1596–1615` 只为 decoder=Pen 发布 Pen sample；`PublishDefaultPenCursor` 实际调用 ClearPenCursorSample，并非恢复默认系统箭头。
- 正常 RTS Up 在 `:1318` 清 Pen sample；坏 Up 在 `:1286,1300` 清样本并终结 producer；OutOfRange 在 `:1142–1151` 清样本。后续真实 InAir `:1347–1373` 可以重新发布 Hover。
- `WindowControl.cpp:963–1000` 的有效 Pen sample 解除 Touch 抑制，并通常接管 Pen owner；`:1003–1019` Clear 会清 mailbox、请求绘制/系统刷新，但没有将持久 Pen owner 重置为 Unknown/Mouse。重复 Clear 若 mailbox 已无变化会提前返回。
- 新系统 Pointer Up 在 `:1623–1627` 也清样本，终态坐标不作为 Hover；PointerLeave 在 `:1530–1545` 清样本并保留相应 owner 解析；Win7 不具备这条额外 Pointer 保底时仍有 RTS Up/OutOfRange。
- `PenCursor.cpp:379–402` 对 owner=Pen 总是 hide；对 selected Eraser/Laser 更是无条件 hide，不要求 sample 有效。即使 Pen sample 已清而旧 Mouse mailbox 仍有效，owner=Pen 也不会从旧 Mouse 选 primary（`:55–60`）。

因此，“正常 Pen Up 清样本后策略立即恢复箭头”与当前源码不符。要解释实体笔抬起的系统箭头残留，至少需要查明终态是否到达、owner 是否被未知 Mouse 改写、Apply 是否因命中其他 HWND 跳过、posted 刷新何时执行，以及残留是否其实是 Tablet feedback。不能只添加一次 Clear 并宣称问题解决。

### 现有测试及其证据边界

| 位置 | 真实覆盖 | 未覆盖 |
| --- | --- | --- |
| `InkeysHeadlessTests/draw3_contact_tests.cpp:1123–1213` | 生产 PenCursor：Pen/Mouse/Touch opacity、Touch owner 无主光标、Eraser 接触/悬停、Mouse Highlighter alpha | OS HCURSOR、Tablet feedback、WndProc 与 RTS 真机事件顺序 |
| `同文件:1216–1278` | 生产 FilterMouseCursorMessage：系统来源带键/无键、末点不同/未知；Mouse/TouchPad 接管；Win7 缺 API 下位置回退、带键放行和真正移动 | 无来源 API 时的设备身份判真，测试只是明确传入状态 |
| `同文件:1281–1340` | 生产 filter + mailbox + visual 组合，回放旧 event 51/60、Touch Up 和真实 Mouse 恢复 | Windows 来源查询、真正 WndProc、硬件 RTS/窗口线程交错 |
| `同文件:1343–1399` | 持久 owner 与 Touch 有效归属、旧 Pen/Mouse 抑制、Pan Mouse 接管、签名/时间屏障与原位回退 | 真实 Win7 系统光标时序 |
| `Draw3.HiddenWindowTest.cpp:1807–1831,1911–1923` | 隐藏 Host contact 注入；Touch 来源、坐标、半径与 cursor diagnostic 同 contact，Up/Cancel 终态 | 物理设备生成的提升 Mouse 消息和 Windows 反馈 |

生产 headless 工程明确编译 `Draw3.PenCursor.cpp`（`InkeysHeadlessTests.vcxproj:133–134`）。`inkStrokeModelerTestTests/pen_cursor_tests.cpp` 与 `contact_input_tests.cpp` 是独立 demo 的旧模块测试，工程绑定到 `inkStrokeModelerTest/draw3/pen_cursor.*`（`inkStrokeModelerTestTests.vcxproj:184–185`），不能当作当前产品实现证据。例如该 demo 的 `pen_cursor_tests.cpp:211–212` 仍要求非 Eraser 的 Touch 不隐藏系统光标，与产品策略不同；本任务不顺手同步 demo。

本轮没有执行任何上述测试。仅按现有源码描述覆盖面，不续用历史通过状态冒充本轮通过。

### 历史任务应如何复用

- `09-23-touch-eraser-cursor/research/schema2-system-origin-findings.md` 已用连续日志证明过：系统光标日志 hidden=1 时，额外圆环仍来自 Mouse primary；旧 event=51/60 可通过 origin 明确判为系统注入。其 `validation.md:56–72` 记录后来实现与人工验收，并明确最后日志不含 Pen 或 Win7。可复用取证方法和失败序列，不能复用平台验收结论。
- `10-02-win7-main-canvas/research/input-cursor-2026-10-03.md:5–16` 记录的是另一组 Win7 鼠标 Eraser CPU visual 已有而像素不可见的日志：api=0、accepted-mouse、无 Pen sample。它没有触摸双光标/实体笔抬起的本次证据，且日志身份与源码版本存在界限。
- 该文 `:18–42` 区分了 RTS contact 与 WM_MOUSE 光标链，并指出 Eraser/Laser 会主动隐藏系统箭头。`present success=1 visuals=1` 不能证明用户看到的像素；同样不能仅凭光标看起来像系统指针倒推输入被阻断。

### 可直接使用的诊断指标与最小后续验证资料

以下是后续实施/验收所需证据，不是本轮已执行的测试：

1. 先复用 `Experimental.Inkeys3.ConsoleOutput.Cursor`，保留启动到最后 Up 后数秒的完整 schema=2 日志、EXE 构建身份和实际工具/设置。`Draw3.Diagnostics.cpp:521–543` 记录 tick/thread/seq；跨线程入队可交错，不能只凭两行相邻推断 Windows 派发顺序，也不能忽略 dropped/truncated。
2. 单指与多指分别采样，避免碰鼠标；覆盖“工具栏选 Eraser”和“工具栏选 Pen、Touch 入口为 Eraser”。同时核对 `rts-input Down/Up reason=... device=...`、`touch-begin/end count`、`mouse-source api/ok/device/origin`、promoted/extra、tick/barrier、`mouse-filter` 的 systemReject/positionReject/buttonBypass、`owner` 和 `mouse-state` 前后。
3. 若同帧存在 `primary=1` 和 `touch=1`，并有未知 Mouse 接受→Touch 抑制清零→Mouse sample 的链，足以定位应用错误接管；若只记录 `touch=1` 却仍见第二光标，则 primary 假设缺乏支持，应查实际 OS HCURSOR/反馈。`WindowControl.cpp:1305–1312` 的 system hidden 只记录策略变化，不能证明两次决策之间 OS 未覆盖。
4. 对实体笔分别测试接触→Up 后仍在感应范围→新 InAir Hover→OutOfRange，含普通 Pen/Highlighter/Eraser/笔尾。正常 Up 后无 primary，新的 InAir 可以出现 Hover；不要把合法悬停当残留。Touch End 后则没有合法 Touch Hover。
5. 对系统光标残留，现有 `system-skip trigger=... target/hit` 可排查 HWND 命中；`system trigger=posted-refresh|WM_SETCURSOR` 可证明应用何时应用策略。当前未记录实际 cursor handle/显示 flags、WindowFromPoint 命中 role、队列投递到执行延迟和每次相同决策调用，若这些日志不足，应在后续获准实施后补最小取证，而不先改输入模型。
6. 来源不明故障与真实鼠标恢复必须成对验收：Touch 接触中/最后 Up 后，用真实 Mouse/TouchPad 原位点击、移动、从窗外按键拖入，确认没有因过滤修复失去接管；Pen 新 Hover 同样应及时恢复。Win7 设备验收与 Win11 ARM64 headless 结果分别记录。

### External References

本报告只追踪仓库实现与已有历史证据，没有重新搜索外部 API 文档。`WM_SETCURSOR/SetCursor` 的 OS 保证、Win7 TouchUIForce/TouchSwitch/Touch feedback 可用性和落笔闪现能否通过窗口级 API 完整解决，应以同任务的官方 API 研究为准；不从符号名或源码注释推断 API 保证。

### Related Specs

- `.trellis/spec/native-desktop/input-and-ink.md:7–23`：生产输入后端、WM_MOUSE 光标与 RTS contact 的证据分离。
- `同文件:27–65`：Touch 临时视觉归属、终态不恢复旧 Hover、未知来源回退与真实 Mouse 接管合同。
- `同文件:67–100`：schema=2 诊断与硬件验收边界。
- `.trellis/spec/native-desktop/draw3-integration.md:306`：已有 ConfigureDrawingCursor / ResolvePrimaryDrawingCursorVisual 集成入口。

## Caveats / Not Found

- 未拿到本次 Win7 双光标与抬笔残留的完整日志或可核对 EXE 身份；本报告没有确认单一根因。
- 只知道当前源码会怎样处理某组输入参数，不知道该 Win7 驱动实际生成何种签名、tablet kind、按键位、终态及后续系统光标操作。
- 不把 `system hidden=1` 写成 OS 实际隐藏已经验证，不把 `present success=1` 写成光标像素已显示，不把以前有现代来源 API 的人工验收写成 Win7 已通过。
- 本轮只新增本研究文档；未改代码/spec/config、未运行 Git 操作、编译、测试或 GUI。
