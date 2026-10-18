# Input and Ink

本页用 `【直接确认】`记录实现事实，用 `【合理推断】`记录修改时的审查建议，用 `【待确认】`记录没有运行验证或维护者契约的内容。传统实现事实不自动成为未来新输入系统的强制设计。

## 输入归一化链

`【直接确认】` 当前产品输入主链位于 `Draw3.RealtimeStylus.*`、`Draw3.ContactInput.*` 和 `Draw3.DrawingController.*`；Draw2 输入源码保留但不参与产品编译或启动：

~~~text
主 Drawpad 选定输入后端（RTS / Win32Mouse）─> ContactInputCoordinator mailbox
                                        │
                                        └─> DrawingController / document / runtime history / final backbuffer
~~~

`RealTimeStylusInput` 是主 Drawpad 的唯一 RTS producer，并保留 `SetAllTabletsMode(TRUE)`；辅助 DrawpadPresentation 不绑定 RTS、WndProc mailbox 或输入门禁。`CSyncEventHandlerRTS`/`TouchMode` 描述仅属于保留的 Draw2 历史实现，不应反推 Draw3 的产品选择模式。

Draw3 Host 在图形资源准备后才选择 RTS 或 Win32Mouse 输入，退出时先停止选定 producer，再唤醒并结束绘制线程；`DrawpadMsgCallback` 只把主 Drawpad 消息转发到唯一 Host。

`【直接确认】` 在 RtsExtended / RtsBasicXY 模式，`WindowController::HandleWindowMessage` 的 `WM_MOUSE*` 接受/拒绝仅控制 `PublishMouseCursorSample` 光标反馈，绘制 contact 由 RTS `StylusDown -> coordinator.PublishDown` 提供；Win32Mouse 模式才由独立兼容分支发布绘制 contact。正常RTS模式不得用 `accepted-mouse` 或 `mouseContact` 证明绘制 Down 已被准入，也不得把光标兼容过滤直接当成落笔失败的根因。

`【直接确认】` 产品未定义独立 demo 的 `DRAW3_RTS_DIAGNOSTICS` 宏；旧 `RecordCallback/FlushRtsCallbackTrace` 不会因开启 ConsoleOutput 而出现。产品 `ConsoleOutput.Cursor` 的运行期开关通过现有有界队列提供 `rts-input event=Down/Up reason=arrival/published/失败原因`，Packets 每 plugin 至多每秒一次。`published=1` 是 producer mailbox 发布证据，仍须分别验证 controller 准入与 GPU 像素。

`【直接确认】` `present success=1 visuals=1` 仅证明帧提交返回成功，不能证明 cursor shader 已写像素。启用 `ConsoleOutput.Draw3` 后，`[Draw3Diag][ulw-pixels]` 在实际 dirty/full 提交时每 presenter 首次及最多每秒一次区分 `sourceAlphaNonzero/sourceMaxBgra` 与 `finalDibAlphaNonzero/finalDibMaxBgra`。Primary 命中底层能使源全透明时的最终 DIB alpha 为 1；黑墨 RGB 可以全零。需使用底层合成前的源 alpha 判断墨迹，源/最终 DIB 正常且 ULW 成功也不能等同桌面可见。`--draw3-ulw-copy-benchmark` 的 8 个统计用例覆盖非原点脏区、RowPitch、黑墨、白光标及底层前后差异。

`【合理推断】` 需要让鼠标、笔、触摸行为一致的改动，优先放在两条输入已汇合的位置；如果只改 `CSyncEventHandlerRTS` 或只改 `DrawpadMsgCallback`，需明确另一条路径为何不适用。

## Scenario: Draw3 触摸擦除与主光标归属

### 1. Scope / Trigger
修改 Draw3 的 RTS Touch 通知、Pointer/Mouse 兼容消息、Pen/Mouse 主光标或系统箭头显隐时适用。Touch 接触圆环与主光标是两条独立呈现路径。

### 2. Signatures
- `WindowController::NotifyTouchContactBegin/End`、`CursorOwner()`、`SetTouchCursorSuppressed(bool)`。
- `ResolveDrawingCursorVisualAuthority(persistentOwner,touchCursorSuppressed,touchPanActive,realMouseTakeoverDuringTouchPan)`。
- `ShouldIgnoreMouseCursorMessage(..., INPUT_MESSAGE_DEVICE_TYPE inputSource)`；Win8+ 动态获取 `GetCurrentInputMessageSource`，Win7 可为 `IMDT_UNAVAILABLE`。

### 3. Contracts
- Touch Down 临时接管视觉归属为 Touch，隐藏系统箭头与旧 Pen/Mouse 主光标；持久 `cursorOwner_` 不写 Touch。Touch Up/Cancel 不恢复旧 Hover，直到可确认的新 Pen 样本或真实 Mouse/TouchPad 消息接管。各活动 Touch 橡皮圆环仍按自己的 runtime 生成，Up 后随 runtime 清除。
- Touch Pan 已由真实 Mouse 接管时，后续 Touch 指头不清 Mouse 样本；Pan 结束后若 Mouse 样本仍有效，保留 Mouse 归属。Pen 新 Hover/Contact 解除 Touch 抑制，Win7 不依赖新 Pointer API 才能恢复笔光标。
- Touch/Pen 提升的 `WM_MOUSE*` 先用 promoted 签名、消息时间屏障与可用的 `INPUT_MESSAGE_SOURCE.deviceType` 过滤；`IMDT_MOUSE/IMDT_TOUCHPAD` 可立即接管。Win7 来源不可用时保留 RTS Touch 与旧过滤链，不使用固定延时压制真实鼠标。
- API 成功返回 `device=IMDT_UNAVAILABLE + origin=IMO_SYSTEM` 的 `WM_MOUSEMOVE` 在触摸抑制仍有效时必须拒绝；不论按键位、触点坐标是否一致或距离 Touch Up 多久，均不创建 Mouse 样本、不改变 owner。明确 Mouse/TouchPad 来源继续走既有接管策略。
- 来源 API 缺失/失败或其它未知来源仍保留已有位置回退；API 查询失败不能被当成已确认的系统注入。Win7 的真实设备行为仍需单独实测。
- WindowControl 必须直接使用 `FilterMouseCursorMessage` 的过滤结果；来源、时间屏障、系统来源、位置回退和按键例外均位于同一个生产入口，调用处不得再按按键条件绕过其中的规则。日志拒绝原因复用该结果。

### 4. Validation & Error Matrix
| 输入 | 主光标与系统箭头 |
| --- | --- |
| 单指/多指 Touch Down、Move、最后 Up/Cancel | 主光标与箭头隐藏；活动擦除圆环仍在，最后一指终态后消失 |
| Touch/Pen 提升的 Mouse 消息 | 不发布 Mouse Hover，不抢占 Touch/Pen |
| Touch Up 后来源未知且停在末触点的 MouseMove | 不解除 Touch 抑制，不重新绘制主悬停圆环 |
| 后续真实 Mouse/TouchPad 或 Pen 样本 | 立即恢复该设备的既有光标策略 |
| Touch Pan 中真实 Mouse 已接管 | 后续 Touch 指头不清 Mouse；Pan 停止不回弹到 Touch |
| Win7 无输入来源 API | 仍用 RTS、promoted 签名及时间屏障；新 Pen Hover 可恢复 |

### 5. Good / Base / Bad Cases
- Good：当前产品选 Pen 而 Touch 入口配置为 Eraser 时，只显示触点擦除圆环，抬指后仍无小光标；之后真实鼠标移动或笔悬停正常恢复。
- Base：没有 Touch 输入时沿用 Pen/Mouse 原有光标策略。
- Bad：只在 Touch 活动计数非零时隐藏系统箭头，最后 Up 后旧 Hover/箭头立即回弹；或把 Touch 写成持久 owner 导致新设备无法接管。

### 6. Tests Required
Headless 覆盖有效视觉归属、Touch 系统箭头显隐、兼容 Mouse 消息来源过滤、真实 Mouse/Pen 恢复和 Pan 接管；另须调用生产 `FilterMouseCursorMessage` 覆盖系统按键态 Move → Touch Up → 迟到系统 Move → 真实 Mouse 接管，并检查 mailbox 与主光标解析结果。完整 ARM64 Solution 构建。隐藏 Host 测试与真实纯触摸、Pen+Mouse+Touch、Win7 设备验证分别记录结果，不能把纯逻辑通过等同硬件验收。

### 7. Wrong vs Correct
- Wrong：Touch Down 只清 Mouse mailbox，仍按旧 Pen/Mouse owner 解析主光标，并在 Touch Up 后依赖 Windows 自动隐藏。
- Correct：Touch 暂时接管视觉归属并保持到真实新设备输入；兼容 Mouse 不得伪装成接管事件。

## Scenario: Draw3 光标控制台诊断

### 1. Scope / Trigger
追查 Pen、Mouse、Touch、荧光笔、橡皮或激光笔光标的归属、重影及抬起后残留时适用。

### 2. Signatures
- 配置键 `Experimental.Inkeys3.ConsoleOutput.Cursor`，默认 `false`；实验选项“光标调试信息”在下次启动生效。
- `SetCursorDiagnosticsEnabled(bool)`、`RecordCursorDiagnostic(format, ...)`、`FlushCursorDiagnostics()` 位于 Draw3 diagnostics module。

### 3. Contracts
- 关闭时不向控制台输出光标诊断；开启时先建立 Debug 控制台，再启用记录。诊断只观察状态，不改变鼠标、笔、触摸的归属和光标显隐。
- `InitializeDebugConsole` 保留已有 stdout/stderr 文件或管道重定向；两路均重定向时不创建控制台，只有一路重定向时只初始化缺失输出。采集必须保留启动至退出的全量文件，不能用末屏替代。Win7 PowerShell 2 / 旧 CLR 的 HashAlgorithm 释放使用 public Clear()，不能假设直接 Dispose() 可见；脚本 parser 通过不等于旧 CLR 方法验证。
- RTS/窗口回调只把定长事件写入有界队列，不直接等待控制台 I/O；绘制线程输出序号、时间、线程和事件，队列溢出必须报告丢弃条数。
- 输入日志需包含 Touch 生命周期、Pointer/Mouse 来源与过滤结果、Pen/Mouse 样本、owner 和系统光标决策。绘制日志需区分主光标与逐触点光标，覆盖形状、位置、尺寸、透明度、Laser 笔尖和本帧呈现结果；移动时可限频，来源和可见性变化必须立即输出。

### 4. Validation & Error Matrix
| 条件 | 结果 |
| --- | --- |
| 开关关闭 | 不格式化或输出光标日志，原光标策略不变 |
| 开关开启且 Touch Up 后重现圆环 | 可从输入接受结果、归属变化、最终视觉来源和呈现结果追溯生产者 |
| 日志生产速度超过控制台消费 | 保留有界内存并报告丢弃数量 |
| Release 构建 | 不显示实验控制台开关，不要求用户开启 Debug 控制台 |

### 5. Good / Base / Bad Cases
- Good：错误圆环出现时有 `mouse`/`pointer`、`owner`、`visual source=primary|touch` 和 `present` 的同序列证据。
- Base：正常单指擦除仅报告一个 Touch visual，最后 Up 后报告 visual 消失。
- Bad：只打印 `SetCursor` 结果，把 Draw3 自绘的圆环误判为 Windows 系统箭头；或在每个 RTS 包中直接写控制台。

### 6. Tests Required
运行 i18n `sync/check`、完整 ARM64 Debug Solution 构建、`InkeysHeadlessTests.exe --no-window` 与 `git diff --check`；真实输入日志需由用户在设备上复现后核对。诊断构建通过不等于原光标缺陷已修复。

### 7. Wrong vs Correct
- Wrong：控制台记录单一“光标可见”布尔值，无法区分系统箭头、主自绘光标和每个 Touch 接触圆环。
- Correct：同时记录输入来源和最终视觉来源，并明确日志丢弃及呈现成功状态。

### UI3 Bar 接触消息归一化合同

`【直接确认】` Bar 注册的 `WM_TOUCH` 会把主接触（包括当前系统上报的 Pen）转换成带内部来源标记的 `ExMessage`。Windows 同时可能为同一接触派发 Pointer 兼容鼠标消息，其 `GetMessageExtraInfo()` 的来源签名为 `0xFF515700`（掩码 `0xFFFFFF00`）。

`【直接确认】` Pointer 兼容鼠标副本必须在 Bar 的窗口过程和 HiMsg 入队回调两个入口都丢弃；不带该签名的真实鼠标消息仍正常入队。低位 `0x80` 只能用于诊断 Pen/Touch 分类，不能决定一次接触的输入归属，因为同一支笔的 Down/Up 可能出现不同低位值。触摸长按右键抑制依赖这条单一 `WM_TOUCH` 路径，不得重新保留 Pen 兼容鼠标分支。

按钮点击完成必须等待明确的 `WM_LBUTTONUP`；`WM_MOUSEMOVE` 的 `MK_LBUTTON` 缺失不能被解释为抬起，否则会把接触或窗口激活阶段的移动误判为第二次点击。

## RTS 初始化与鼠标兼容回退（2026-10-18）

### 1. Scope / Trigger

产品 Draw3 启动输入初始化适用；历史 `IdtRts.cpp::SafeRTSInit` / `useMouseInput` 不参与产品编译，不能作为当前兜底已接线的证明。

### 2. Signatures

- `RealTimeStylusInput::Initialize / UsesBasicPacketDescription / EnableContactPublication / Shutdown`；生产共享编排和 callback lease 位于 `Draw3.RtsStartup.h`。
- `HostInputMode::{Disabled,RtsExtended,RtsBasicXY,Win32Mouse}`；`HostStartupStage::InputReady` 对应 `Milestone::Draw3InputReady`，保持原进度槽/权重。
- `WindowController::SetMouseCompatibilityEnabled / CloseMouseInputAdmission` 和 `Draw3.MouseInput.h` 的生产鼠标状态；只向现有 ContactInputCoordinator 发布。

### 3. Contracts

- 按完整RTS→基础X/Y→Win32Mouse串行选择，每档最多一次。基础档覆盖扩展SetDesired失败及设置成功后Enable失败，使用新RTS实例；保留all-tablets/多点设置，不枚举额外开关或固定延时重试。
- 每次可恢复RTS尝试有独立Dormant失败清理scope，停止/排空并真实Complete/join后才能下一档；成功实例恢复Host signal。Disable失败时必须成功get_Enabled读回明确FALSE，才可继续Remove及排空；已加入插件但仍启用或状态未知、Remove失败、排空未证明或deadline过期均不能启用下一producer。
- 同步插件所有业务回调先取单原子关闭位+在途计数的lease，Shutdown先关门，再无内部锁Disable/Remove，等最后lease的event通知后才能终止contact和释放借用对象；不能只看计数零越过最后通知尾部。
- 候选RTS可建立decoder但不发布业务Down，选定输入后才开放；就绪前按下必须抬起后重新Down。不能在drawing consumer存活时ResetForNextRun清失败候选队列。
- 每代只有一个绘制producer。RTS模式WM_MOUSE仍只处理原光标，不重复落笔；Win32Mouse从正常RTS的promoted/Touch barrier过滤之前接收真实鼠标及系统已提升的单点鼠标消息，Pointer不再提供第二条绘制流。
- 鼠标左右键复用MouseLeft/MouseRight语义；单活动流、另一键先结束旧流，迟到旧Up不结束新流。保留有符号客户区坐标和未知压力/面积；Move不能凭空创建Down。
- 捕获由owner线程设置/释放并读回。CaptureChanged/CancelMode/隐藏/销毁/Stop取消活动contact；MouseLeave不是Up。停止须owner串行确认和转发排空，之后才能撤Coordinator/Reset；RTS Shutdown不能代替鼠标停产证明。
- 鼠标降级记录warning和真实input_mode，不冒报rts_ready成功，不新增阻塞提示或设置项。基础档不承诺原压感；鼠标档不承诺压感、多点、笔尾/触觉，系统不提升鼠标消息时不保证对应Pen/Touch可用。

### 4. Validation & Error Matrix

| 情形 | 必须结果 |
|---|---|
| 完整RTS成功 | 不试X/Y，不启用WM_MOUSE绘制 |
| 扩展描述失败，或扩展Enable=0x80070057 | 安全清理旧attempt后用新实例试X/Y一次 |
| RTS不可用且安全停产 | owner确认Win32Mouse后才打开输入就绪门 |
| Disable失败但成功读回Enabled=FALSE | 继续必需的Remove和drain，全部完成后才可降级，与publisher是否存在无关 |
| 已加入插件且Disable失败读回仍启用/状态未知、Remove/drain失败或Expired | 不启用新输入，不释放仍借用对象，受控致命收口 |
| 重复/迟到Up、捕获丢失、Stop交错 | 恰好一个终态，新代次不继承旧contact或mode |

### 5. Good / Base / Bad Cases

Good：扩展Enable失败→X/Y成功；或两档失败→鼠标就绪并经原Controller绘制。Base：完整RTS保持原能力。Bad：只重试SetDesired、吞停产错误、RTS和WM_MOUSE同时发布、把光标移动当作绘制兜底。

### 6. Tests Required

生产共享RTS编排/清理scope/lease/publication gate故障注入；生产鼠标状态→真实Coordinator；完整Solution Debug|ARM64与Headless `--no-window`。这些不能证明真实COM/驱动接受X/Y、实际首帧、owner捕获或系统Pen/Touch提升，现场/授权窗口验收单列。

### 7. Wrong vs Correct

Wrong：依赖旧Facade转发或把RTS false改为true放行。Correct：选择真实输入后端、完成旧producer停产，并通过同一Contact/Controller/历史保存链。通用进程自动重启不是本合同的替代。

## 历史 Draw2 笔画生命周期

`【直接确认】` `IdtDrawpad.cpp::MultiFingerDrawing` 为每个活动接触点处理临时 `StrokeImageClass`/`DibSurface`。可见行为包括：

- 普通画笔用 GDI+ 曲线绘制采样点；
- 荧光笔走半透明合成，当前代码可见 alpha 130；
- 橡皮直接修改基础 `drawpad`，范围可受压力、速度或固定宽度影响；
- inverted pen 和部分右键路径切换为橡皮语义；
- 形状工具使用独立状态和预览/落笔逻辑；
- `IdtDrawpad.h` 对 `StrokeImageClass::endMode` 注释为：1 绘制/合并到画布，2 不绘制/不合并。

`IdtDrawpad.cpp::DrawpadDrawing` 周期合成基础 `drawpad` 与 `StrokeImageList` 中的活动笔画，再写入 `window_background` 并更新分层窗口。完成笔画还会与 `RecallImage` 历史以及放映中的 `PptImg` 页级画布交互。

这些是 `【历史/兼容】` 的现有 Draw2/GDI+ 墨迹模型；图像承载已迁为 `Inkeys.Graphics.DibSurface`，不再依赖 EasyX `IMAGE`。新渲染实现不必复制所有全局结构，但改动当前路径时不能忽略其层次和页级状态。

当前 PPT 的接触收尾、代次门禁和物理 Up 隔离见 [PPT 会话合同](../ppt-interop/native-session-ui3.md)；不能根据下面的历史 Draw2 共享状态推导生产键盘或页事务。

## 历史共享状态与并发风险

`【直接确认】` `TouchList`、`TouchTemp`、`TouchPos`、`StrokeImageList`、`drawpad`、`RecallImage` 和 `PptImg` 在不同函数/线程中读写；代码并存 `shared_mutex`、`mutex`、`IdtAtomic` 和显式锁。`MultiFingerDrawing` 当前由 detached thread 执行，画板合成和其他状态线程同时运行。

`【待确认；风险观察，不是已确认缺陷】` 静态扫描不能证明所有共享访问已同步，也不能证明 detached 笔画线程在快速退出时发生竞态。未来改动前应逐个调用点确认：

1. 接触点和临时 `DibSurface` 的创建、读取、最终合并与释放分别由谁执行；
2. `endMode` 的最终处理者是否唯一；
3. 橡皮直接写基础层时与合成线程如何协调；
4. 清屏、撤销、恢复、冻结帧和 PPT 换页替换/复制画布时，活动笔画如何收束；
5. `offSignal`、线程状态和 detached worker 的退出顺序。

`【合理推断】` 新访问应沿用被访问对象已有的锁/原子入口；若发现既有读写没有一致协议，应先记录问题和复现证据，不能仅靠 Spec 宣称线程安全或擅自大范围重构。

## 工具状态与两套悬浮栏

`【直接确认】` `IdtState.cpp/h`、`IdtDraw.cpp/h` 和 `IdtHistoricalDrawpad.cpp/h` 提供画笔、橡皮、形状、清屏、撤销/恢复等共享行为。UI 反馈有两条分支：

- `Inkeys/Inkeys/UI/Bar/Bar.State.*` 是唯一产品悬浮栏路径；
- `IdtFloating.cpp` 只作为不编译的 UI2 迁移参考；
- 两者最终都会影响传统画板/工具共享状态，但不能把 `Bar.State` 称为所有 UI 模式的唯一状态源。

`【合理推断】` 新增当前画板工具时，按实际范围核对工具状态、两套可达 UI、输入 begin/move/end、临时/基础层、历史、PPT 页级墨迹、配置和 i18n。只有维护者确认某条 UI 已退出产品范围后，才能缩减对应验证。

## 高频路径与建议验证

`【直接确认】` packet、笔画采样和 `DrawpadDrawing` 是高频路径；`IdtDrawpad.cpp` 可见 `prepareCanvasQueue` 等画布复用机制。

`【合理推断】` 在这些路径新增磁盘 I/O、COM 调用、重复资源加载或无界分配前，应测量影响；改变采样、平滑、压力或画布池时记录帧率和笔迹延迟。该建议不是声称当前实现已有量化性能门槛。

建议按改动范围手工覆盖：mouse down/move/up、压感笔与 inverted pen、单/多点触摸、各工具、快速/长笔画、窗口边缘/多显示器、清屏/撤销/恢复、PPT 翻页及活动笔画时退出。

`【直接确认】` `InkeysHeadlessTests` 覆盖 Draw3 bridge/timer/纯逻辑；`--draw3-hidden-test` 通过隐藏主/辅助 HWND 覆盖唯一 Host mailbox、真实绘制线程、history/Clear、双 target 和退出路径。真实笔、触摸屏与驱动设备矩阵仍需维护者提供。

## Scenario: 橡皮DIP尺寸、Touch响应与接触面积辅助

### 1. Scope / Trigger
2026-09-14 同步已接受的 Mouse/ScreenPenHybrid 行为与第七轮 Touch 规格。2026-09-15 的精细区合同见下文：仅替代最小到标准区间的正速度立即增粗、固定120ms无确认以及独立idle旁路。2026-09-23 的 Touch 场景修正又替代了旧“统一物理清扫曲线”要求；2026-09-25 仅授权 Touch/ScreenPenHybrid 标准以上的增长抗短脉冲调整。精细算法、屏幕笔速度映射/补偿、面积换算/倍率和Hover/Down/Up所有权仍不变，不重写输入队列、模型、中心轨迹或渲染器。

### 2. Signatures
- `EraserSizes` 全部是直径 DIP；`ResolveSizes(BaseSize)` 的当前预设 B=24/32/40，分别给出 minimum=0.5B、standard=B、maximum=5B、touchStart=0.5B、fixed=B。原始 `EraserSizes{}` 默认值只用于兼容直接调用；`DiameterToCanvasPx` 仅在坐标边界换算，`FixedDiameterPx` 完全旁路动态控制。
- `ResolveConfig(display, mode, InputSource, sizes)` 分离真实来源、响应模型与标尺。来源通过当前 RTS context 的能力及精确 Pointer cursor 对应关系缓存，不改变真实 InputDeviceType。
- `Controller::UpdatePosition(x,y,seconds,contactArea,terminal)` 只消费真实输入；`Advance` 不制造运动证据；`AreaDiagnostics`、`NextAreaWakeSeconds` 暴露有界状态与过期唤醒。
- `ContactSnapshot` 保留 `rawContactSize`、既有 `contactSize` 和 `contactAreaUnits`。面积单位状态不影响正常位置、压力、倒转和接触身份。
- `DevelopmentOptions::touchContactAreaAssistance` 默认关闭；绘制设置“橡皮擦”块和程序调测共用 `Experimental.Inkeys3.Draw3.TouchContactAreaAssistance` 持久化配置，启动时读取保存值，由独立接触批次锁存。实验页重复功能入口已移除，独立控制台诊断开关仍保留。
- `ContactSizeState`、`AppendEraserSizeAnchor` 与保存的逐点半径保持单一尺寸时间线。
- `HostRuntimeSnapshot::eraser.needsAnimation` 表示发布该快照时的动画需求；`idleSeconds` 同样是发布时值，不是读取时自动增长的时钟。

### 3. Contracts
- EDID、设备种类及模式不得改写 DIP 基础属性。圆形像素几何继续使用横纵 DIP 密度的几何平均；历史只保留已经生成的像素宽度，不能按后来配置重算。
- 间接设备采用 DIP/s，不撤销系统鼠标加速、不猜鼠标硬件 DPI。Mouse/External Pen/TouchPad/Unknown 自动选择间接响应；Integrated Pen 和直接 Touch 分别选择自己的响应。
- Win7 自动识别保持保守 DIP 回退；Pointer API 动态探测。不能将 TABLET_CONTEXT_ID 强转成设备 HANDLE，也不能按整台机器是否存在屏幕笔来分类。
- 标尺依次考虑显式绑定的手动测量、可靠物理映射、适用的逻辑显示分辨率/DPI经验尺度、DIP。复制拓扑不能重新成为可信物理尺寸；经验尺度不写回 EDID 或 cm/px。
- 经验增益保持 `1 / clamp(min(max(Wdip,Hdip)/1920, min(Wdip,Hdip)/1080), 0.5, 4)`；它不是屏幕英寸或毫米估计。
- 间接响应精细/进入/退出/大目标速度为 100/800/600/1900 DIP/s。屏幕笔物理响应为 20/120/80/350 mm/s，DIP回退为 80/480/320/1400 DIP/s。
- 屏幕笔仅对标准以上增量应用 `g=(0.25/rho)^beta`，默认 beta=0.5，rho 为可信表面的 mm/DIP；基础属性、精细区与固定尺寸不参与补偿。本轮不调整这些已有响应。
- Touch 动态目标直接用 DIP，不做 rho 整目标补偿。可信物理或有效手动标尺的小屏端点为 30/90/60/250 mm/s，教室大屏端点为 30/350/250/1300 mm/s；DIP 回退仍是 100/240/160/700 DIP/s，经验回退仍是 100/120/80/400 reference DIP/s，不将 mm/s 阈值套用到回退单位。
- Touch 场景强度仅用于 DirectTouch 物理/手动路径的 enter/exit/large：长边 320–1200 mm 之间用 smoothstep 得到有界 `w`，三项为 `90+260w / 60+190w / 250+1050w` mm/s。Laptop 显式选择限制 `w≤0.25`，LargeScreen 显式选择保证 `w≥0.25`；内部 Automatic 仅在调用者请求时按可信尺寸取原权重，未知尺寸退 Laptop/DIP。此长边是经验先验，不是用户动作或字迹的测量，不按整屏比例计算速度。已保存的 0/1 场景选择不被 EDID 覆盖；标尺、场景、响应模型分别解析，接触期间锁存。Surface 约 28cm 的 Laptop 路径继续严格使用小屏端点。
- 同一有效参数必须供 `SweepActionSpeed` 后的资格、证据、退出和 `ReferenceTargetDiameterDip` 目标共同使用。普通动作低于进入资格不能凭持续时长充成最大；大屏 B=32、中灵敏度时 100/200/300/400/432.376/600/800/1000/1300 mm/s 的公式参考目标约为 32/32/32/32.42/33.11/42.16/67.15/109.40/160 DIP，实际控制器还受起步、证据与时间响应影响。
- 2026-09-25 暖状态抗短快划：Touch 历史窗仍为50ms，证据 start/full/decay 改为80/180/300ms，标准以上增长 tau=220/180ms、对数增长限速4/6每秒；ScreenPenHybrid 证据改为80/200/350ms、增长 tau=200/160ms，原有对数限速4/6每秒不变。Mouse/间接模型、场景速度阈值、缩小/保持及精细参数不改。上述证据是泄漏积分及许可尺寸，并非固定等待时长；低于资格的普通运动不能靠时间充满。面积主导的普通 Touch 增长沿用原120/100ms与6/8每秒，不借面积给清扫证据。面积开关关闭时同样使用 Touch 速度模型。
- `SweepEvidenceCapDiameterDip()` 只读计算证据对应的尺寸上限；帧级 `[TouchCurve]` 附带有效证据/增长参数和该 cap。cap 不代表当前确已获得新运动资格；无Move帧可能仍报告短窗速度但清扫用速度为零，面积下限也可独立高于 cap。分析100ms动作必须用有界内存测试采样，250ms控制台快照不足以逐点重放。
- 2026-09-27 仅替代上条 2026-09-25 中 DirectTouch/ScreenPen 的“缩小/保持不改”边界：已有大尺寸允许短时高于证据 cap，不能据 cap 直接夹小；保持续期与回缩确认取消需本步真实动作及足够证据支持当前尺寸。被拒绝的增长请求与无可靠资格的近尺寸目标可暂缓本步下降，不得反复清零持续成立的回小意图。接受的增长、可靠的近尺寸目标仍可重置，短折返与连续清扫保留。Mouse/间接模型、原速度目标、增长参数、面积下限和精细区合同不变。
- 真实路程按时间积分，折返不作净位移抵消；预测、补点、缺失连接和面积变化不提供速度资格。实际位移解锁仍为物理 1–3mm 或回退 2–6 动作单位。
- Touch 的移动目标不能因为每包误差小于 settle tolerance 就立即吸附。仅目标稳定时允许小误差收敛，否则高回报率会绕过阻尼。此修正规则不改变冻结的间接/屏幕笔响应。
- 非Touch的Up/Down与Hover改用下文的按入口连续尺寸会话，替代Up Reset、140ms夹小和250ms交还期限。纯Hover仍不新增清扫证据；真正Touch每次新接触仍小起步，面积与真实断触协议不变。
- 面积辅助仅适用于真实、映射可解释的屏幕 Touch 笔速橡皮，强制响应模型不能伪造真实 Touch；Mouse、Pen、TouchPad、固定橡皮均不使用面积下限。
- WIDTH/HEIGHT 必须实际存在于返回的 packet description。保留原 per-context 换算；只有宽高与对应 X/Y 的长度单位、分辨率相符且在声明范围内，才认可为画布像素。PROPERTY_UNITS_DEFAULT 表示未知，不能默认当像素。
- 已确认的面积按 `wDip=wPx*dipPerPixelX`、`hDip=hPx*dipPerPixelY` 换算一次，不使用 EDID、压力或 WM_TOUCH 的百分之一像素规则。
- 面积默认拒绝范围外 2..96 DIP、长宽比超过3.5、非有限/非正值和离群跳变。拒绝阈值与辅助上限是两件事，巨值不能被夹成64DIP后使用。
- 稳定真实拖动确认50ms后锁定本接触的参考；面积参考可与位移解锁并行准备，但实际下限须通过原位移保护。参考不随重压、摊开或噪声反复变大。
- 2026-09-27 仅替代“相对旧参考失配后本接触永不恢复”的边界：硬无效（单位、有限值、绝对跨度、长宽比等）仍拒绝并平滑释放；通过硬检查的相对离群样本，只有独立候选在连续真实拖动中稳定达到 160ms 才能更新比较参考。每包须与本轮首个候选的固定宽高接近，不能靠滤波候选缓慢漂移穿过稳定门。候选遇硬无效、正常回到旧参考、静止、采样空档或真实重连即清空；帧预览与合成连接不积累恢复时间。初次 50ms 建立不变，首次确认的面积下限作为本接触不可抬高的上界，后续多次恢复的下限仍受它和原配置上限共同约束；新 Touch 接触独立建立上界。接受恢复只影响后续真实输入，不追溯增大已经积分的几何。
- 下限为 `clamp(1.10*max(wDip,hDip)+6, standard, min(maximum,max(standard,64)))`。自定义 standard 大于64时不反转 clamp 上下界。它是有界拖擦下限，不是手掌分类或压感橡皮。
- 合成 `max(speedTarget, contactFloor)` 后继续平滑；面积下限无需清扫资格，但只在真实移动中提高已接受尺寸。静止的新面积/新包不能反向放大当前工具。
- 缺包最长保留2s，显式无效值宽限200ms，过期参考按180ms释放。真实重连平移这些时钟；Up的零面积不是新参考。
- 达到辅助下限后停帧，由既有等待机制在面积过期时唤醒。不能让下限抬住尺寸却一直以16DIP作为未达成目标请求帧。
- 原始输入与帧预览状态分离；静止只更新当前工具和待用尺寸断点，恢复实际几何时追加同位小半径锚点，不覆盖历史或恢复已擦内容。
- Touch笔速输入恢复时，若距离上次成功建模的时间乘输出采样率将超过单次输出上限，复用模型Reset/Update在最后已接受位置建立短时间种子，再提交真实输入。不清空历史结果、转换游标、接触身份或尺寸/面积控制器，不增加模型输出上限；此路径不用于Mouse/Pen或其他橡皮模式，种子不提供运动证据。
- 原始面积有效性独立于实验开关；关闭辅助仍可显示已确认的DIP宽高。未确认单位明确显示unverified，不标成可信像素；referenceFresh单独表示参考是否过期。
- 诊断每帧可关闭发布，磁盘/控制台输出沿用帧级限频，不逐点同步日志。包含真实来源、请求场景、解析 Touch 场景/权重/来源、模型、标尺/单位、像素/DPI/手动尺寸、有效四阈值、B/增益、短窗报告速度、经资格路径的清扫速度、140ms fineSpeed、资格/证据、目标/实际 DIP、面积及对应 contact 的最终光标与几何直径。`cursorPx` 不可从其他光标的列表首项取值。
- 实验页沿用已保存的 `ConsoleOutput.TouchArea` 键但展示“输入与橡皮诊断”。启用时，Host 在显示快照/诊断配置发布边界输出 EDID 解析状态与原始/可用厘米、活动分辨率、有效 DPI 和失效原因；活动分辨率不是 EDID 时序。每约250ms的 `[EraserInput]` 只对应选中的一个 contact 或橡皮 Hover，带帧时间、接触ID/代际、实际设备、输入画布坐标、可见光标坐标、面积、目标/实际 DIP 与光标/几何像素尺寸。隐藏光标及非橡皮工具的橡皮尺寸必须明确无效；原 `[TouchArea] contact` 仍表示橡皮接触。短窗报告速度可在无新线段的帧仍为正，此时清扫资格速度可为零；稀疏快照不能作为原始轨迹逐点重放。
- 同一限频诊断中的 Touch/ScreenPen `[EraserFollow]` 另列原始短窗目标、面积/精细后的有效目标、证据 cap、实际可执行增长目标与 actual；最近真实输入清扫速度/年龄不等于无Move当前帧的清扫动作资格。精细低区可沿已有时间状态继续趋向 B，不能把该现象误记为新清扫证据。附带 hold 剩余、decrease 确认进度/门槛、当前动作、累计 reset 次数和最近原因。需要短事件全程时由有界内存帧 trace 结束后导出，不能为美化日志在 `Advance` 帧补造运动证据。
- 面积参考恢复沿用 `[TouchArea]` 原 250ms 门，额外报告锁存参考宽高、当前/参考比例与离群轴、恢复候选宽高/真实移动累计时间、首次下限上界、恢复次数，以及参考 fresh/正在释放/候选确认中。`areaActive` 可以包含低于 B 的释放尾端，`areaAboveB` 才表示已接受的面积下限当前仍高于标准尺寸；两者不能混称。高频因果检查使用测试内存轨迹，不在 RTS 包路径逐点写盘。
- 面积开关独立发布并锁存，不进入 Mouse/Pen 的显示标尺变更判定，避免点击 Touch 开关使精细 Hover 重置。

### 4. Validation & Error Matrix
| 场景 | 必需结果 |
|---|---|
| Mouse/间接冻结轨迹 | 改动前后浮点位模式一致；ScreenPen 速度映射和 beta 仍冻结，标准以上增长时间按2026-09-25合同替代 |
| 同一 Touch 场景、不同分辨率/DPI/方向的正确物理轨迹 | 单位换算与 DIP 目标/采样率结果一致；不要求跨场景同 mm/s 同目标 |
| 不同可信物理场景的普通动作和主动清扫 | 各自普通区不误触顶，中间尺寸可控，持续快速清扫可达；不恢复整目标物理补偿 |
| 未知单位/缺失/零/负值/巨值/离群 | 辅助拒绝或平滑释放，正常触摸仍接收 |
| 已有参考失配，但后续硬合法面积稳定真实拖动 | 160ms 独立确认后更新比较参考，恢复下限不超过首次确认上界；静止、尖峰、硬无效和合成重连不充当恢复证据 |
| 新Down/原地长按/起点抖动 | 小尺寸，不因面积或时间开启大洞 |
| 普通慢拖、无清扫资格 | 可确认并使用有界面积下限 |
| 静止后面积增大 | 不扩大真实擦除；参考不呼吸 |
| 无Move/同位置包/数据过期 | 正确休眠或释放；不能等待休眠快照的年龄继续增长 |
| 恢复实际移动 | 当前半径起步，历史不改；UInk保留尺寸断点 |
| 面积开关、并发、真重连 | 按批次锁存，各接触独占状态，连接不计新运动 |
| 固定模式、强制Touch的Mouse/Pen | 面积不能越过真实输入和固定旁路 |
| 60/125/240/1000Hz与不同帧率 | 相同观测轨迹的关键尺寸误差不超过5% |

### 5. Good / Base / Bad Cases
- Good：面积关闭先验收更轻的Touch响应，再打开比较慢拖可见性；鼠标/笔不调参。
- Base：不可用面积只关闭辅助，新Touch速度模型和固定DIP尺寸仍正常工作。
- Bad：用面积填清扫证据、把未知单位伪装成实测值、覆盖历史半径、只改光标、放宽全部容差来掩盖错误。

### 6. Tests Required
完整 `InkeysRepo.sln Debug|ARM64`、`InkeysHeadlessTests.exe --no-window` 和专项 `--draw3-eraser-hidden-test`。DComp/ULW隐藏测试覆盖低速辅助、休眠/过期、恢复足迹、固定旁路、Undo/Redo和实际UInk往返。诊断快照休眠后不会继续更新，等待条件使用 needsAnimation 等明确状态，不能等待 idleSeconds 自行越过任意阈值。模型原有单次2000补点上限未更改；Touch笔速恢复使用有界位置重锚，并单独覆盖超过上限的长停顿和长按Up，不把无输出的模型错误归为保存格式故障。真实Surface、大屏、外接数位板及Win7仍需单列人工验证。

### 7. Wrong vs Correct
~~~cpp
// Wrong：每包小误差都吸附移动目标，会使高采样率绕过阻尼。
if (abs(current - target) < tolerance) current = target;

// Correct：Touch只对稳定目标收敛吸附；等待静止状态不依赖快照年龄增长。
if (targetIsStable && abs(current - target) < tolerance) current = target;
// 等待实际状态，再确认 frameSequence 没有继续增长。
if (!snapshot.eraser.needsAnimation) CheckFrameSequenceStops();
~~~

## Scenario: RTS Touch contact-area metadata and relative-length conversion

### 1. Scope / Trigger
Applies when reading RTS WIDTH/HEIGHT, interpreting PROPERTY_METRICS or logging TouchArea diagnostics. In this 2026-09-14 metadata scope, speed response, size curves, DIP defaults, area multiplier/ceiling, input position collection and EDID semantics were frozen. The 2026-09-23 Touch physical-scene curve above supersedes only that historical speed-response freeze.

### 2. Signatures
- `ResolveContactLengthTransform(axis, span, positionScale)` returns per-axis status, span-to-axis and span-to-canvas factors.
- `ConvertContactArea(rawWidth, rawHeight, widthTransform, heightTransform)` preserves raw values; unknown/invalid metadata leaves pixel values unknown.
- `RealTimeStylusInput::TraceTouchAreaDiagnostics(source)` reads cached contexts only; an empty source lists Touch contexts, a supplied source requires matching context and generation.

### 3. Contracts
- Actual returned packet properties determine GUID, index, units, resolution and declared range. Requested properties are not proof of returned support.
- For supported inch/centimeter units: `spanToAxis = axisResolution / spanResolution * spanUnitCm / axisUnitCm`; `spanToCanvas = spanToAxis * abs(positionScale)`. PositionScale is the existing packet-XY-to-canvas linear mapping.
- Different resolutions and convertible length units are valid relationships, not failures. DEFAULT is unknown; angular or undocumented extended unit semantics are not guessed.
- Width/height are lengths, never translated positions. Do not subtract logical minima or multiply the context ink-to-digitizer factor again. The existing position path is untouched.
- CanvasPixels is set only for an explained conversion and in-range finite packet values. DIP conversion occurs once in the existing controller. Bounds/aspect/outlier/startup/idle/history policies remain unchanged.
- Conversion status crosses the existing contact snapshot with raw/converted values. Unknown units, invalid resolutions, invalid declared ranges, unsupported units and bad transforms have separate diagnostic reasons.
- Runtime tracing copies the existing bounded RTS cache under its reader gate and a short plugin-lifetime guard, then formats outside both locks. No per-packet hardware query or log. Enabling diagnostics replays cached Touch metadata; source/generation changes refresh it at most once a second.
- A metadata dump without a known cursor explicitly says mapping is pending. It must not substitute Mouse/Pen metadata or the primary display for the active Touch source.

- 20261007-233715 全面现场：WARP87终态完整、matrix69绑定全PASS；CursorWithDefaultInk(mask0/15)全零，而FullWithoutDefaultInk(mask0/15)恢复848像素，末尾原PS仍零/CursorOnly恢复。此双向分界定位DefaultInk源码群触发共享PS执行异常，不等于唯一opcode已证；其未guard的导数/discard尾部、薄线helper和SV_Position声明仍关联。已测“非默认类型提前返回”仍失败，正向包裹输出块须核实际DXBC；去薄线只作诊断，不作降画质修复。Fields读取/校验SV_Position.xyzw成功，不能称Win7没有该语义。修复验收须另测真实type0/3、非零Destination上的Retain/擦除及全工具。

### 4. Validation / Error Matrix
| Case | Required result |
| --- | --- |
| Same units, different positive resolutions | Apply resolution ratio |
| Inch/centimeter units | Apply one physical-unit ratio |
| Missing property/unit | Retain raw values; explicit missing status |
| Invalid resolution/range or unsupported units | No usable canvas dimensions; exact metadata reason |
| Packet outside declared range or nonfinite | Reject, not clamp into an acceptable finger |
| Nonzero logical origin / reflected position axis | Relative length ignores translation and uses absolute linear scale |
| 96/144/192 DPI / anisotropic axes | One per-axis DIP conversion; unchanged auxiliary floor formula |
| Diagnostic enabled after context creation | Dump cached Touch properties without rebuilding/querying hardware |
| Source context/generation no longer cached | Report no matching Touch, not another device |

### 5. Good / Base / Bad Cases
- Good: an explicitly declared tenfold resolution difference produces a tenfold ratio, independently of observed sample magnitude.
- Base: metadata remains unknown, so area assistance stays off while speed erasing and raw diagnostics still work.
- Bad: divide by 10/100 because a value looks too large, assume all packet properties share the context scaling factor, or relabel unverified numbers as pixels.

### 6. Tests Required
Use the production converter in headless tests for unit/resolution/range/DPI cases and pass its synthetic output through the existing hidden eraser ingress tests. Preserve no-Move expiry, point erasing, old radii, new geometry, undo/redo and persistence checks. Synthetic success is not Surface packet acceptance.

### 7. Wrong vs Correct
- Wrong: `rawWidth * contextScaleX` followed by an equality-only metrics check.
- Correct: resolve the width-to-X length relationship from returned metrics, then apply the cached position linear mapping exactly once.

References: [PROPERTY_METRICS](https://learn.microsoft.com/en-us/windows/win32/api/tpcshrd/ns-tpcshrd-property_metrics), [PROPERTY_UNITS](https://learn.microsoft.com/en-us/windows/win32/api/tpcshrd/ne-tpcshrd-property_units), [GetPacketDescriptionData](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-getpacketdescriptiondata).

## Scenario: 精细区平台、迟滞与双向确认（2026-09-15）

### 1. Scope / Trigger
替代“非零速度立即增粗”和“最小到标准固定120ms、无确认”的旧规则。该 2026-09-15 轮次只改变尺寸意图与跟随；当时 RTS 位置、真实路程统计、清扫短窗及参数、面积解释/倍率/上限、DIP 属性和固定橡皮旁路不变。后续 2026-09-25 的 Touch/ScreenPen 标准以上证据/增长参数按前述新合同替代；本节精细区参数继续适用。

### 2. Signatures
- `Config::fineHoldSpeed/fineReleaseSpeed` 与该模型 `fineToStandardSpeed` 同单位，默认分别为其0.20/0.35倍。
- `fineWindowSeconds=0.140`；进入/缩小确认100ms，解除/恢复确认160ms。
- `fineShrinkTauSeconds=0.200`、`fineGrowthTauSeconds=0.260`；对数缩小/增长上限4/3每秒。
- `Controller::FineDiagnostics()` 输出长窗速度、held、进入/解除/方向确认进度和已许可方向；`Diagnostics::fine` 沿现有快照输出，活动接触控制台增加 `[FineBand]`。

### 3. Contracts
- 参考低段：速度<=fineHoldSpeed时目标minimum；到fineToStandardSpeed之间按偏移归一化smoothstep连续上升至standard。标准以上参考公式不变。
- 单位示例：间接20/35/100 DIP/s；物理屏幕笔4/7/20 mm/s；物理Touch6/10.5/30 mm/s；笔DIP回退16/28/80，Touch DIP/经验回退20/35/100（均为各自动作单位每秒）。
- 长窗以完整140ms为分母，包含零位移及无Move时间；路程不作净位移抵消。沿用有界64段存储和相邻合并，保留时间取短窗/参考窗/精细窗最大值；不对中心位置另加平滑或取整。
- 连续低速建立held，进入后只有达到更高release速度的证据才累计解除；迟滞带内不增加解除证据。反向短脉冲只消耗部分确认，不按每次目标微变重启整个计时。
- 方向确认和held确认并行；解除held即携带恢复许可，不再串联第二个160ms。原清扫资格满足后直接旁路低区确认，不能强制先16->32再等待高区。
- 已有lastMovementTime/idleStart继续识别有界亚像素抖动；idle历史直接记入低速确认，随后仍使用同一精细跟随，不走另一套快速回缩。
- 最小目标统一按double求log，稳定目标持续40ms后在既有误差阈值内精确收敛，防止float/double残差留下永不休眠的动画。移动目标不能靠每包小误差不断吸附。
- 从高区缩小时保留高区主要过程，精确拆分跨standard的时间，剩余时间交给低区跟随，边界连续。
- 面积先形成自己的有效下限；不得被held压到minimum。面积主导时保留既有面积许可/响应，不把面积当作高速证据，不重新拦截稳定参考建立。
- 新状态分别存在sampleState/frameState中；帧预览不反写真实输入。确认进度为时间长度，重连时保留，已有绝对时钟和历史按原规则平移；合成连接不参与运动。
- 第八轮精细算法继续适用；非Touch跨段交接由下文连续会话合同替代，不再以Up Reset或140ms夹小实现。纯Hover不预充大清扫，Touch新接触、取消和固定旁路继续隔离。
- ContactSizeState、尺寸断点、历史半径、撤销和保存格式不变。面积开关继续持久化，不恢复为重启重置。

### 4. Validation / Error Matrix
| 场景 | 必须结果 |
| --- | --- |
| 0..0.20倍细段上界的持续速度 | 从standard或minimum收敛/保持精确minimum |
| 已held后迟滞带内长期弱移动 | 不因时间累满退出证据 |
| 单个像素跳步、慢挪与停顿交替 | 不周期增粗，不抹掉已有精细意图 |
| 持续普通移动 | 有限确认后连续恢复，不跳档 |
| 清扫资格成立 | 不增加低区串联等待 |
| 有效面积下限高于minimum | 保留下限及真实移动许可 |
| 完全无Move、亚像素有界抖动、面积过期 | 正确回缩并最终停帧 |
| 帧先行、迟到输入、重连 | 真实状态独立，桥接不提速 |

### 5. Good / Base / Bad Cases
- Good：15 DIP/s持续慢擦到最小，停住再慢挪不增粗；125 DIP/s持续移动后解除并平滑恢复。
- Base：面积有效时精细意图仍可held，但实际尺寸尊重已接受的面积下限。
- Bad：每次非零包解除held、把idle再串联完整确认、只平均非零位移速度、给预测点积证据，或用整数取整掩盖残差。

### 6. Tests Required
headless先在旧代码运行红灯用例，再验证平台/迟滞/单跳/双向耗时、1728组0.5/1/2px量化与稀疏输入（96/144/192/288 DPI，60/125/240/1000Hz，多帧率/相位/轴向）、休眠和原面积保护。隐藏DComp/ULW测试必须检查产品光标、当前半径、新段尺寸断点、历史不回写、Undo/Redo及真实UInk往返，不能以独立控制器通过替代。

### 7. Wrong vs Correct
- Wrong：`minimum + (standard-minimum)*SmoothStep(speed/fineToStandardSpeed)`，任意正速度立即离开最小。
- Correct：先扣除fineHoldSpeed再归一化；held/解除证据独立，确认后按实际时间的对数阻尼跟随，稳定下限精确停帧。

## Scenario: 五入口橡皮配置与非Touch连续尺寸会话

### 1. Scope / Trigger
2026-09-15，基线e071ff3a。仅统一五入口配置、修复交接与首点/光标解析；接触中的速度/精细曲线、阈值、物理补偿、面积转换及倍率不变。本合同替代旧非Touch Up重置、标准尺寸夹取和250ms交还规则。

### 2. Signatures
- `InputEntry::{MouseLeft,MouseRight,Touch,PenTip,PenTail}` 分别对应0..4；真实设备身份仍是独立的InputSource。
- `EraserKind::{Fixed=0,Speed=1}`；`PenResponseChoice::{Automatic=0,ScreenPen=1,Tablet=2}`。
- `InputSettings` 保存完整五入口表及平台自动识别可用性；`ResolveInput(...entry,settings,policy)` 统一解析。
- `Bridge::Tool::ConfiguredEraser` 是普通橡皮；显式FixedEraser/SpeedEraser继续强制相应类型。
- `SessionConfigCompatible` 校验入口、来源context、有效映射、DPI、单位及计算参数，忽略不影响计算的revision/识别补全。
- `MouseLifecycle` 复用为非Touch会话容器，产品分别持有左右键、笔尖和笔尾实例；BeginContact返回所有权票据，EndContact只可退休同票据。
- `Controller::LeaveContact` 建立离面尺寸状态；普通离面不调用冻结时间的PauseForReconnect/ResumeFromReconnect。

### 3. Contracts
- 正式配置键：`Drawing.Eraser.MouseLeft/MouseRight/Touch/PenTip/PenTail`，以及`PenTipResponse/PenTailResponse`。-1只作缺失迁移哨兵，不是UI枚举。
- 缺失宽度默认Speed。旧磁盘EraserSetting.EraserMode明确等于2时，仅在新键缺失时把左键/Touch/笔尖迁为Fixed；右键/笔尾的旧隐式Fixed不迁移。已有0/1选择不被重置。
- Win8+默认Auto；Win7缺失笔响应默认Tablet，已有Auto或未知值运行时回退Tablet。UI隐藏Auto但保存1/2稳定值，不使用删除选项后的下标。能力探测复用GetProcAddress，不静态依赖GetPointerType。
- 手选ScreenPen只改变响应，不伪造IntegratedPen、映射或EDID。调测显式强制优先，诊断报告debugOverride。
- 面积辅助移到正式块下方，兼容键不改；Touch为Fixed时无效但保留保存值。两处可达开关共用状态，控制台开关独立。
- 产品状态经StateBridge发布完整表；接触可锁存批次表，但每个contact按自己的entry解析，不复制首个contact的最终Fixed/Speed。
- 右键/笔尾临时擦除保持selectedTool和effectiveTool分离，不改变普通笔尖/左键绘画或选择态。原笔尾触发开关继续有效。
- Up立即停止该段几何。尺寸/精细状态与有时效的清扫记忆进入独立会话，不Reset、不夹到standard。无Hover离面目标为min(离面直径,standard)，按真实时间用已有idle响应演进。
- 纯定位Hover不增加清扫证据；已有大余态可短暂保留并回落。后续真实Hover可继续按第八轮精细规则回细，不能无限保大。
- Down先从原始事件状态计算事件时刻直径，允许大于standard；随后重锚位置并清除空档测速段，不冻结空档、不用其位移提速。帧预览不反写事件基线。
- 并发/旧Up/延后烘干以入口与票据隔离；同入口重叠不能无条件继承另一所有者。不同tablet context/实际映射或计算配置变化重建；诊断开关不重建。
- 当前可辨识来源粒度是tablet context与有效映射，不宣称识别同一digitizer上的每支物理笔；实际多设备仍需人工验证。
- 普通独立Down创建新RuntimeStroke与几何列表，尺寸会话不持有连接几何。真实断触匹配仍用原判据，只有既有协议接受时才可连接并冻结/恢复其runtime；尺寸连续不是连接证据。
- Touch完全绕过跨新接触会话，每指小起步、面积参考和真实重连不变。
- 待接触光标与Down都调用同一解析器；Down-only首帧保留事件直径，首点半径为该像素直径/2。后续帧按实际时间演进，不能把晚采样光标误当作Down时刻。
- 非Touch长期无输入采用有界离面推进并停止无意义唤醒；笔离开感应范围可隐藏并惰性推进，鼠标退出画布/取消/Host销毁清理相应状态。

### 4. Validation / Error Matrix
| 条件 | 必须行为 |
| --- | --- |
| 五入口不同Fixed/Speed | 各自解析，首点/光标使用同一配置 |
| 左Fixed右Speed、笔尖Fixed笔尾Speed | 同批不串最终模式 |
| Win7 Auto/未知值 | Tablet安全回退；UI下标不改枚举语义 |
| 同来源无害revision/cursor元数据变化 | 不从精细16重置为32/50 |
| 20/50/100/200/500ms及2s普通离面 | 自然回落，事件本身不Reset，不补线 |
| 旧票据Up/烘干晚到 | 不覆盖已开始的新段 |
| 不兼容来源/映射/DPI/Fixed变化 | 明确退休/重建，不套用另一来源 |
| 真正Touch新Down | 不继承上一指尺寸或面积参考 |

### 5. Good / Base / Bad Cases
- Good：160DIP擦除后20ms再次Down仍用连续余态；下一落点的独立几何列表仅含新段点。
- Base：无保存选择时五入口笔速，原面积辅助值继续有效。
- Bad：仅保留大光标而逻辑已经夹成32、把所有Up当作断触、只因整个Config不等就Reset、或用笔尖配置处理笔尾。

### 6. Tests Required
完整Debug|ARM64、headless和DComp/ULW隐藏入口。覆盖真实Config写读、Win7纯策略、五入口类型、混合批次、普通绘画不被抢占、无害元数据交接、非Touch多种离面间隔、独立首点列表和原第八轮/面积/Undo/Redo/UInk回归。首帧事件数据与后续帧数据按各自QPC解释；不得把合成测试或静态UI检查称为真实设备/Win7验收。

### 7. Wrong vs Correct
- Wrong：`if(preview.config_ != config) Reset(...standard)`，或Up把逻辑尺寸夹小但播放大光标动画。
- Correct：按有效计算配置和来源检查，复制独立尺寸会话并在事件时刻演进；新段重锚，只复用尺寸记忆，不生成空档几何。

## 光标诊断 schema=2 补充合同

- `mouse-source`、`mouse-state phase=before/after`、`mouse-filter` 和 `mouse` 用每个窗口的 `event` 编号关联；多线程日志仍使用全局 `seq`。这些快照是分别读取的观测，不是 RTS 与窗口线程之间的原子事务。
- 来源需区分 API 不存在（api=0）、调用失败（api=1 ok=0 error）、成功返回未知（api=1 ok=1 device=0）；同时记录 origin 和 InSendMessageEx 的 sent 标志。sent=0 不能单独证明来源是物理设备。来源查询保存并恢复调用方 LastError。
- 末触点记录包含实际比较坐标、消息时间、via=pointer/compat-mouse。兼容 Mouse 无完整 Pointer ID 时 touchId=0，不能把 extra 低位猜成完整 ID；primary=0/1/2 分别表示未知/非主触点/主触点。
- 按键位只能说明消息状态，不能确认物理鼠标来源。schema=2 日志已证明 `!buttonDown` 例外会放行 device=0 origin=IMO_SYSTEM 的 Move；修复将已确认的系统来源判定置于按键/位置回退之前。纯函数通过不等于设备验收。
- Raw Input 只观察现有 Bar 接收入口，记录初始注册状态及后续注册/注销成功或失败。落笔可能注销接收；缺少 raw-mouse 行不证明没有真实鼠标操作。不得为了诊断在第二个 HWND 重复注册同一设备类。
- 定长日志截断必须包含 `[truncated]`；丢失/截断时事件链检查不能报告证据完整。verify_cursor_trace.py 检查真实输出的来源接管到呈现序列，其无匹配结果不等于全部光标行为正确。

## Scenario: Draw3 Contact Closing 与拒收路由回收

### 1. Scope / Trigger

修改 `ContactInputCoordinator` 的 Down/Move/Up/Cancel、`Recycle`/`DiscardUntilTerminal`、Host Reset 或 Controller 对已出队 Down 的拒收分支时适用；不得为减少等待删除必要输入样本。

### 2. Signatures

- `ContactInputCoordinator::{PublishDown,PublishMove,PublishUp,PublishCancelled,TryReadSnapshot,Recycle,DiscardUntilTerminal,ResetForNextRun}`。
- route 在低 3 位编码 `Free/Initializing/Producing/Closing/ConsumerOwned/Quarantined/ClosingDiscarded`，高位为 contact generation；`ContactHandle` 同时携带 record 地址与 generation。
- `ContactClosePauseForTesting` / `PauseNextCloseAfterRouteClosedForTesting` 只供显式隔离测试，默认 hook 为 null。
- Controller 内部 `RejectStrokeInitialization(ContactInputCoordinator&, ContactHandle, const ContactSnapshot&) noexcept` 是三处 acquire/Reset/Down Update 失败共用的拒收接缝；实际身份只用 record+generation，不按 Down 的 key 重查当前 producer。

### 3. Contracts

- `PublishDown` 成功入队后 record 地址在该 generation 内稳定。普通 Move 只更新同一身份的真实快照；Up/Cancel 先精确 CAS `Producing/Quarantined→Closing`，等已进入的 writer，发布终态，再转 `ConsumerOwned` 或由 producer 自行 Free。Down 合法坐标是坏终态包最后退路，不能让非有限终态把 route 永远留在 Closing。
- 消费者 `DiscardUntilTerminal` 看到 Producing 时转 Quarantined 并保留物理 route，producer 的终态自行回收；看到 Closing 时精确 CAS `Closing→ClosingDiscarded` 后立即交出 handle，唯一 Close producer 完成终态后 `Free+ReleaseSlot`。若与 `Closing→ConsumerOwned` 竞态输了，则由消费者 `Recycle`，不能同时释放两次。`TryReadSnapshot` 前后均按同一 generation route 拒绝已 Free/Quarantined/ClosingDiscarded 的 handle。
- `Recycle` 处理已完成 ConsumerOwned；并发 Closing 时也交给 producer 延后回收。不能对仍 Producing 的被拒收 Down 直接 Recycle并丢弃handle。Laser拒收第二Touch和Controller三个初始化失败都走精确Discard，物理Up/Cancel自动回收。后者仍保留原runtime cancelled、speed eraser handback、handle清空/inUse=false与错误返回；consumer不能按key伪造物理Cancel，串行A.Down→A.Up→B.Down时旧A失败会误取消新B。两个record初始generation可同为1，必须同时核地址。
- `ResetForNextRun` 只有在上一代所有输入 producer（RTS、hidden mailbox/Window 注入及其它 callback）真正静止且唯一绘制 consumer 已 join 后可清 route/freeMask/排队指针；当前 Host 依赖 RTS/Host Stop 时序，未来若改变 callback 生命周期必须补显式 in-flight 证明。诊断累计 `downPublished/recycled` 不在 Reset 时归零，测试必须在每个 RunMode 取本轮 baseline 后比较增量；需要当前资源占用时使用 `occupiedSlots`，不能用跨代绝对计数。未入队Down的Abort仍由该producer自清。正常RTS Down/Up/Disabled受同一writer/reader gate，不能把任意Coordinator并发视为真实RTS Abort竞态；任意独立Closer或COM停止失败的条件边界单列，未证明全链quiescence。

### 4. Validation & Error Matrix

| 交错 | 必须行为 |
| --- | --- |
| Discard 先于 Up | Producing→Quarantined；物理终态后 producer Free，quarantined/recycled 恰一次 |
| Up 已在 Closing 且 writer 暂停 | 普通 Discard/Recycle 有界返回，producer 放行后终态 Free；不能占住绘制线程 |
| Close 已转 ConsumerOwned | 消费者 Recycle 一次；二次 Discard 不双释放 |
| 无效终态坐标 | 使用最后有效位置或合法 Down 回退闭合；内存本身损坏时拒伪造可复用槽 |
| 旧 handle / 同址新 generation | 旧读/回收失败，不能触碰新 contact 或位图 |
| Laser 第二 Touch 被拒 | 首指仍活动；第二指 Up/Cancel 后 occupiedSlots 回基线，反复接触不耗尽固定槽 |
| 旧A初始化失败且同key新B已接受 | 只Discard A地址/代次，B的Down/Move/Up正常，重复旧A不能触碰复用新代次 |
| Producing的初始化失败 | 保持Quarantined/occupied直到物理Up或Cancel，不能提前free或伪造Touch End |

### 5. Good / Base / Bad Cases

- Good：在生产 Controller 普通页边界/拒收调用点，Close 已 CAS Closing 且被受控暂停时，Discard 调用本身有界返回并允许其它命令继续；放行后回收槽且新 generation 可复用。若只运行 Coordinator 状态机测试，不升级为完整页切换 PASS。
- Base：正常 Up 已 ConsumerOwned，原有消费者同步 Recycle 路径不变。
- Bad：Closing上无限YieldProcessor、直接丢handle、或consumer拿旧handle的key调用PublishCancelled并回收旧handle，导致改的是新route。

### 6. Tests Required

- 真生产ContactInputCoordinator的可控CAS后hook做旧版红→绿，Up/Cancel、预Quarantined、逆序ConsumerOwned、重复Discard、无效终态、同槽复用与旧代次拒读；测试线程先放行再join，禁止SuspendThread。
- 三初始化失败必须实际调用同一RejectStrokeInitialization；现有 --draw3-parked-desktop-exit-test 的独立 [Draw3InitRejection] 核同key旧A/新B、stale复用、Producing不早退、Closing放行后单释放。真实共享helper红→绿与全部三个modeler/池失败分支动态注入分开，前者不升级普通PPT全Run/硬件PASS。
- Controller 拒收 Laser 第二 Touch 的生产判定 helper 需同测试入口执行；正例多轮与非 Laser/多指开启/首指不活跃等负例分开，隐藏 Host 和真触摸设备分别记录。`occupiedSlots`、`inputDownPublished/recycled` 为资源和诊断口径，不能冒充像素 Present。
- 完整 Solution Debug|ARM64 与可得 Release、Headless `--no-window`、Draw3 隐藏 Host；必须另有生产 Controller 普通页边界/拒收调用点的 Closing 交错测试，不能用 Coordinator 单测替代。真实 RTS callback quiescence、Win7 Hardware/WARP/ULW FLIP、持续 GPU 失败与用户现场卡死各自保留门禁。

### 7. Wrong vs Correct

~~~cpp
// Wrong：拒收第二根仍 Producing 的手指后丢 handle；Recycle 只处理终态。
input.Recycle(secondTouch);

// Correct：隔离其物理路由，终态由 producer 安全回收。
input.DiscardUntilTerminal(secondTouch);

// Wrong：失败旧A的key可能已属于新B。
input.PublishCancelled(tabletId, contactId, cancelled);
input.Recycle(oldHandle);
// Correct：consumer拒收只修改旧A的地址+代次，物理terminal负责回收。
input.DiscardUntilTerminal(oldHandle);
~~~

## Scenario: Win7 GPU 透明读回分界诊断

### 1. Scope / Trigger
Win7 输入已发布并被 controller 消费，而连续笔/橡皮操作的 ULW staging 源 alpha/RGB 全零时适用。缺少 Pointer API 不足以解释已接受的鼠标 RTS contact；staging 全零也不能单独证明 GPU 原纹理没有写入。

### 2. Signatures
- 显式早退 CLI `--draw3-renderer-pixel-test` / `--draw3-renderer-pixel-test-hardware` 分别调用 `RunRendererPixelTest()` / `RunRendererHardwarePixelTest() noexcept`，位于产品配置、单实例和主窗初始化之前。复用 `CommandLineToArgvW(GetCommandLineW(), &count)` 识别首个参数（不区分大小写）；引号及尾随空白合法，匹配 flag 后必须 `count==2`，额外参数返回 2，不能落入普通启动。
- `Collect-Win7.ps1 [-ProbeOnly] [-SkipProbe] [-CaptureRoot <new-directory>]`：默认先 probe 后普通应用；ProbeOnly 仅 probe；SkipProbe 仅普通应用；两开关同时设置或目录已存在时拒绝，不能覆盖既有采集。
- 单入口 `Run-Win7-Diagnostics.cmd` 执行同目录 Windows PowerShell 编排脚本，唯一时间目录下依次生成 `01-pixel-probe`、`02-normal-ulw`、`03-full-ulw`，probe阶段分别运行WARP与Hardware两个隔离进程，再运行两次同 EXE 普通应用；历史 PixelTest/Full-ULW 入口保留研究资料，但不混入新单入口包。
- `[Draw3PixelTest]` 输出 case、HRESULT、资源身份、纹理描述、viewport、BGRA 统计、pipeline invocation 和设备状态；新增上传字节和三个诊断 PS 对照单独记录，不覆盖原四正式用例的退出判定。
- 显式环境变量 `INKEYS_DRAW3_ULW_FULL_UPDATE=1` 配合 `Run-Win7-Full-ULW.cmd` 进行正常应用对照；仅该精确值启用，由 presenter 初始化时读取，不写产品配置。
- `[Draw3Diag][cursor-gpu]` 受既有 Draw3 诊断开关控制，包含资源/Map/上传/Draw 前后状态；`after-draw-issued` 只表示 Draw 已发出。

### 3. Contracts
- 独立 WARP FL11_0、128×128、永不显示的 layered HWND 与普通 FLIP_SEQUENTIAL；复用生产 InkRenderer 和嵌入 shader，不读配置、不创建产品主窗、不在实际画布注入颜色。
- backbuffer/offscreen 各测已知 BGRA 清屏、正式白色 Circle 32px/opacity=0.5 光标（GPU shape=4）；这四项及设备正常共同决定退出 0。两项关闭混合仅为隔离对照，不决定正式用例通过。六项均成功也不能覆盖 EraserGrip shape=6、1920×1080、生产 dirty CopySubresourceRegion/阻塞 Map；独立测试使用全量 CopyResource 与显式同步。
- 清屏核所有像素与预期 BGRA，光标核非零计数、中心预乘白和透明角；RTV 与读回纹理通过 canonical IUnknown 身份比较。 独立Draw先清透明Destination，常色/精简PS的production_blend通过不覆盖非零底图的Retain贡献；关闭混合仍保留双输出链接，不能据此唯一归因PS算术/JIT。需要验证叠加或擦除修复时，另核非零底图，不将本轮隔离探针扩大为完整合成验收。
- 原六例完成后，独立读回 b0 已写48字节和 t0 已写前32字节；未写的 DISCARD 内容不读。正式 VS + 诊断常色/字段校验/Cursor-only PS 分别验证 Draw 写入、真实 stage 输入和完整正式 PS 的执行差异。字段校验输出固定 alpha=1，RGB 编码错误位；以所有覆盖像素的错误位并集验收，不能以 RGB=0 误报透明。每个诊断 PS 核实际 ISGN register/mask/interpolation 与正式 VS；源和生成步骤保存，Win7 不需要新增运行时 D3DCompiler，SHADER301–304 与原8个CSO不替换。
- 追加五项独立 sequence：Cursor-only 对照后立即恢复原完整 PS（创建新变体前）、完整源原样复编、Cursor type=4 入口 `.5` 常色哨兵、完整 Cursor 分支移到 laser9/10 前、最后再恢复原完整 PS。总计18项：六基础+七上传/PS对照+五 sequence；diagnostic/sequence 不覆盖 production_cases=4 和原失败退出。所有变体保留其余功能源/资源/签名，不作为生产替换。原样复编若只 RDEF 不同且 ISGN/OSGN/SHEX/STAT 相同，只是元数据/对象创建对照，不能宣称机器指令变化。
- 全面matrix69包括compact整数/float路由与full转换前运行期哨兵/float路由各baseline/fullybound共8例；原PS对s0、b1..3、八2D和四Array资源组16种组合，每次恢复真实原绑定重测共32例；有限Laser CB追加1例；保留Cursor的六群FullWithout/CursorWith及CursorFamilyOnly共13变体双绑定26例；末尾原PS/Cursor控制2例。PSGet实际资源、非目标别名、初始化dummy、DXBC资源/路由和源生成身份分别验证；原六诊断字节数组保留，不替换正式资源。变体不覆盖production4退出合同，单独matrix摘要不能从exit0推导全部PASS。
- 硬件fixture不回退WARP；仅DXGI_ERROR_UNSUPPORTED/NOT_CURRENTLY_AVAILABLE初始化返回3并明确unavailable，其它初始化或运行错误仍失败。首行probe_schema=comprehensive-20261006同时记录请求driver和87/69期望，后续记录实际adapter。采集器先核WARP schema再启动hardware CLI，旧EXE缺schema时禁止发未知flag（否则可能启动产品GUI），记录skipped-schema-mismatch及incomplete。单入口核manifest EXE SHA/schema并保存身份；summary=win7-comprehensive-2，完整性与GPU/光学结果分开。
- 完整type4入口常色仍在类型guard之后：FAIL不能证明进入该分支；字段Near正确不等于ftoi/ieq实际路由已验。比较full/mini须同时核真实DXBC和声明/绑定资源，不将NULL sampler或未用branch的NULL资源单独判为合同违规；下一实验优先同原PS分组有效绑定/恢复NULL及compact类型路由哨兵。仅有flat默认collector文件不能当单CMD三阶段已执行或full override已测试。
- `INKEYS_DRAW3_WIN7_DIAGNOSTICS=1` 仅在既有非 IDT_RELEASE 诊断初始化内接受长度1且值1，与局部 Draw3/Cursor 开关 OR，建立 Debug console 后记一次启用；不赋值持久化配置。单入口子进程设此值；probe/普通轮显式清除继承的 full override，末轮 full=1，finally 恢复调用进程原环境。
- summary 的阶段 captured/captured-with-failure 只表示有真实退出身份和完整双流，非像素/视觉通过；缺流/无退出/未完成三阶段则 incomplete。probe失败仍继续两轮，保留子采集器异常双流与真实 EXE SHA。每轮用新空白测试页先普通笔5秒、等3秒，再橡皮/激光各3秒，正常退出后将观察单列保存，不能先切橡皮掩盖首笔刷新时机。
- 独立 WARP 与正常硬件应用各按实际 adapter 记录。硬件源/DIB 非零不能宣布桌面可见恢复，WARP 自检全零也不能推广到同机硬件。系统光标、真实Pen/Touch与激光各有验证边界。
- 全量 ULW 对照只把 `UpdateLayeredWindowIndirect.prcDirty` 置空，保留原 GPU dirtyCopy、DIB更新范围、render schedule 和 presentFull 请求；`usedDirtyRect` 与日志反映实际参数。未设置或其它值时完全沿原提交；不得凭该对照先宣布 Win7 需要永久全帧。
- 现有 sampled ULW 像素行补 Copy+Map、CPU rows、USER32 调用分段毫秒耗时；诊断关闭时不计时，不加 GPU query 或逐帧输出。跨日志长时间差没有分段证据时不能预先归因 USER32。
- 运行期原始 source 统计在 staging Map 后、CPU alpha 底层合成前保存，Unmap 后才提交 USER32；日志在 ULW 返回后打印这些已保存值，不能解释成“ULW 返回后才读取源”。source 已零时，纯 DIB/USER32/桌面可见性问题不能单独定因。
- ULW 使用 swapchain backbuffer 作绘制/复制资源但不调用 Present1；创建成功/GetDesc1=FLIP 不等于已经验证 DXGI FLIP→DWM 呈现。用户既有 Win7 ULW 实测与公开支持声明分别记录，保留现有 FLIP 合同；阻塞 Map 会促使 runtime 提交，不能仅凭无显式 Flush/Present 判读回失效。
- 对照 Demo 时核实际 EXE 的 SHADER301–304 bytes、真实 profile 和 DXBC 执行/签名块；完整 CSO hash 的 metadata 差异不必意味着指令差异。shader 字节身份不扩大为整个 EXE/C++ 身份，当前源仓库也不自动等于旧实测二进制。
- query 每项最多等待 3 秒，读回 Map 使用 DO_NOT_WAIT；全面自检每可用后端87项（旧18+新matrix69），collector 每后端等候600秒（旧18项150秒），单步 query 仍最多3秒；超时及取得子进程后的异常仅尽力终止并等待该精确子进程，禁止按名称杀进程。普通采集失败仍保存结果并继续正常应用采集；`-ProbeOnly` 在记录结束时间后早退，WARP exit=0且Hardware exit=0或明确unavailable/exit3时返回0，失败/超时/身份不符返回1；exit0不代表matrix69全部通过。
- 运行期光标首个可见请求及最多每秒一次查询实际管线；关闭诊断时不查询或格式化。不更改 Draw、上传、混合或呈现策略。
- Win7 PS2 collector 使用 public HashAlgorithm.Clear()，保留完整 stdout/stderr 和 exe SHA。probe 的 `Start-Process` 用 `-NoNewWindow` 与重定向复用采集器控制台（该 CLI 只建永不显示的 HWND）；PS2 的 `-WindowStyle Hidden` 与重定向不在同一参数集。启动后立即保留 `.Handle`，等待退出后读 `.ExitCode`，不能先 `.Refresh()` 导致结果丢失；null 退出码记录明确 error。
- Windows PowerShell 实测可给 `lpCmdLine` 添加尾随 U+0020，不可对原始全串严格匹配自检 flag。宿主解析/参数集模拟与本机 WinPS5.1 实测都不能替代 Win7 PS2 现场结果；不得根据 API 成功或纯本机 PASS 宣称 Win7 恢复。

### 4. Validation / Error Matrix
| 结果 | 判别边界 |
| --- | --- |
| collector 参数集错误、无 pixel-test 输出 | 自检未启动，不能把正常应用 exit=0 当自检 PASS |
| 匹配 GPU flag 但有额外参数 | 启动链之前返回 2，不创建普通窗口或加载配置 |
| ProbeOnly 子进程非零、超时或退出码不可读 | 保存具体结果后返回 1，不启动正常应用 |
| 单入口任一阶段缺原始流/退出身份，或未完成三阶段 | summary=incomplete；不能把前一阶段成功当完整采集 |
| probe 非零而后两轮成功 | 保留 captured-with-failure/总退出1并继续；不误记 probe PASS |
| 原PS反向复验成功，前置原PS失败 | 保留暖机/执行顺序差异，不能单凭早期结果定唯一shader指令缺陷 |
| 两个 Clear 失败 | 创建/格式/拷贝/readback/设备状态，先看具体 HRESULT |
| Offscreen Clear 正常而 backbuffer 异常 | swapchain backbuffer 或对应资源路径 |
| Clear 正常、正式 Cursor 全零 | Map/CB/SRV/shader/viewport/混合或绘制管线 |
| 关闭混合有像素而正式混合没有 | 进一步调查生产混合路径；不能直接将对照态作为修复 |
| 独立正式用例正常、实际帧仍全零 | 产品实际资源绑定/上传/目标切换与帧执行顺序 |
| 原始源和最终 DIB 正常、桌面仍不可见 | USER32/DWM/dirty ULW 与窗口层级，结合分段耗时和显式全量提交对照 |
| Clear 正常、正式与关闭混合 Draw 同为零，但 VS/PS invocation非零 | 执行次数不证明非零输出；继续核上传字节、字段校验与常色/Cursor-only 对照 |
| 字段正确且 Cursor-only 有像素，完整正式 PS 为零 | 正式大PS的控制流/字节码/运行期执行差异；尚非唯一opcode或driver缺陷 |
| 全量ULW对照刷新恢复且GPU/DIB内容等价 | USER32 dirty 提交相关证据；不将对照态直接升格成生产修复 |

### 5. Good / Base / Bad Cases
- Good：关联正式 Map HRESULT、实际目标身份与像素结果，再据同一设备证据修改生产行为。
- Base：普通 CLI 之外不开启自检；正常画布无新增标记或模式切换，未显式设置全量对照环境变量时不改变 ULW 参数。
- Bad：将 Draw(void) 或 ULW success 当成出墨证明，或者仅因 Win7 不支持 Pointer API 就跳过已有 RTS contact 正证据。

### 6. Tests Required
完整 solution Debug|ARM64、候选 Release|x64 和两架构新像素自检；核上传字段、诊断 PS profile/signature、常色/字段/Cursor-only 单独结果，保留正式四项退出判定。Full-ULW对照在同机同包记录变量身份、实际prcDirty与分段耗时；本机不启动交互GUI，Win7视觉对照单独标待实测；Headless --no-window 验证纯逻辑回归。真实 Win32 argv 验证 exact/quoted/case/trailing-space/tab 与额外参数；在 Windows PowerShell 用新 EXE 和最终脚本实际采集，核每可用后端87例单独结果、matrix_cases=69、production_cases=4、diagnostic_cases=7、sequence_cases=5、visible=0、完整输出和无产品配置/普通启动记录。collector 的成功/非零退出/超时/启动与等待异常/不可读退出码，以及双流大输出，须核完整末尾标记、精确子进程清理和 ProbeOnly 返回；普通分支用 mock 证实失败后仍采集，不能启动真实主窗。Win11 WARP 与 Win7 PS2 现场独立记录，不互相代替。 新单入口用实际 WinPS mock 核空格路径、四次顺序调用（两个probe加两次普通应用）、继承 full=1 的清除/恢复、两条观察、probe/普通非零继续、缺输出/启动异常和大双流；实际新 EXE ProbeOnly 另验，不启动真实普通 GUI。

### 7. Wrong vs Correct
- Wrong：以 Hardware 非零源推翻用户“切工具才刷新”，或以 WARP 自检结果代表同机硬件；`ULW success=1` 或 `Draw issued` 推断 shader 已产出可见墨迹；`CompareStringOrdinal(lpCmdLine, ..., flag, ...)` 假设宿主没有追加空白。
- Correct：按 argv 识别早退自检，先核其输出确实存在；Clear/readback、正式 Draw/readback、真实光标 Map/绑定、实际 staging/DIB 和桌面可见性逐段取证；保持 unresolved Win7 任务 in_progress。


## Scenario: Win7 完整 guarded shader 候选（2026-10-08）

### 1. Scope / Trigger
本段记录 2026-10-08 guarded 诊断包的历史合同；2026-10-09 默认 Win7 WARP 修复以末节为准。真实旧 Win7 WARP 已确认完整 PS 输出为零，字段/常色/Cursor-only 正常，移除 DefaultInk 的隔离变体恢复；只定位到程序群/控制流影响，尚未证明唯一 JIT 指令或候选实机恢复。用户要求全部 shader 效果保持，删除功能变体只可用于隐藏自检。

### 2. Signatures
- 原 GPU CLI 保持 `--draw3-renderer-pixel-test` / `--draw3-renderer-pixel-test-hardware`，hidden HWND 永不显示；schema `guarded-20261008`，每个可用驱动 105 项=原87+候选18，旧matrix69不变。
- `Run-Win7-Diagnostics.cmd` 为唯一入口，同 EXE 顺序执行 `01-pixel-probe`、`02-original-shader`、`03-guarded-shader`。capture schema `win7-guarded-1`，package id `Inkeys-Win7-Guarded-x64-Release`。

### 3. Contracts
- `INKEYS_DRAW3_WIN7_DIAGNOSTICS` 和 `INKEYS_DRAW3_WIN7_GUARDED_SHADER` 必须各自精确为单字符 `1` 才选择候选；否则继续正式 shader 资源。正常初始化记录实际选择、实际 byte SHA256、size、CreatePixelShader HRESULT；失败按初始化失败处理，不回退冒充成功。
- 候选完整源从正式 HLSL 生成，数学/细笔/AA/光影/双源输出/输入输出签名保留，只给 DefaultInk 尾部添加正向控制块。未知 type 原 d=0 输出边界也须保留；核实际 DXBC 中相关指令位于 guard 内，不能仅看源缩进。正式 HLSL/CSO及旧23诊断数组保持原身份。
- probe 显式保存/清除/还原 guarded 开关；wrapper 三阶段 full ULW 开关均 unset，guarded 空/空/1，finally 恢复调用者三个环境值。候选开关不持久化；实际产品两轮仍按既有方式保存测试页。
- 18 候选项=9场景×backbuffer/offscreen，覆盖原版/候选 cursor普通混合/关混合/非零底图、type0普通/细笔/变半径、type3荧光笔、erase/draw Retain。使用真实 resolve blend；不宣称此 primitive fixture 覆盖 MAX/MIN 多层历史累积全部行为。
- 每项独立数学/像素检查；原版正确才全图逐通道容差1比较。原版失败需 `reference_status=unavailable comparison=unavailable`，不能把不可比图像当等价。唯一终态及独立 `guarded_cases=18 guarded_failures=N reference_unavailable=N` 摘要。
- 每驱动 owned-child 截止900000ms；旧170×3秒query与新增36×3秒读回，最坏查询约618秒，另留初始化预算。真实硬件初始化不可用exit3，不回退WARP、不记PASS。原版CLI失败仍继续两轮画布，完整性、原版exit和候选status分别记录。

### 4. Validation / Error Matrix
| 条件 | 处理 |
| --- | --- |
| 任一开关不是精确1 | 正式PS；不根据Win7版本自动启用候选 |
| 候选CreatePixelShader失败 | 初始化失败，日志如实保留，不能静默切原版 |
| 缺失/重复case、105/69/18/schema/manifest不符 | incomplete/身份拒绝；不能把CLI0当候选PASS |
| 原版FAIL、候选18PASS | 保留原版非零exit和失败摘要，后两轮继续；Win7视觉仍待验收 |
| 原版图像不正确但候选正确 | reference unavailable；可判断候选独立输出，不宣称相等 |
| 超过900000ms | 终止并等待精确自有PID、释放句柄、保留raw，后续驱动继续 |

### 5. Good / Base / Bad Cases
- Good：保留完整数学并核实际字节码、真实type0/3和非零底图，再用同EXE两轮原版/候选观察真实墨迹、细笔、荧光笔、橡皮/激光、撤回重做、选择穿透和定格返回。
- Base：不开明确诊断环境仍用正式资源；生产HLSL与ULW策略保持。
- Bad：把NoThin/FullWithoutDefaultInk当最终应用修复，或把参考unavailable与候选PASS写成像素等价。

### 6. Tests Required
完整Solution Debug|ARM64和Release|x64，双架构WARP/独立Hardware105项及Headless --no-window；候选全图比较和独立公式须同时通过。核源数学、DXBC guard/signature/资源及正式EXE SHADER301–304与旧包/原始日志SHA。实际Windows PowerShell新EXE ProbeOnly与mock三阶段，核失败继续、完整raw、严格身份/计数/重复/缺失/ref比较矛盾、空格路径、大双流、父环境恢复和自有子进程timeout。WinPS5.1不能替代Win7 PS2；CLI直接创建候选不等同本机正常GUI LoadShaders分支已运行，实际选择与光学效果待Win7两轮记录。

### 7. Wrong vs Correct
- Wrong：只保留SV_Target0的常色PS却沿用双源blend；只验证非零而不验证原版效果；为通过测试修改产品shader的边缘/光影数学。
- Correct：合规输出/实际混合状态下按各工具真实输出公式验证，再比较原版和候选；原版与候选同时不符合断言时先查断言/fixture。未完成Win7验收前保持任务in_progress，不把本机Win11 PASS记成修复完成。


## Scenario: Win7 WARP 默认完整 shader 修复（2026-10-09）

### 1. Scope / Trigger
实际 Aero 开启/关闭 Win7 现场均证明原完整 PS 最终输出透明，数学完整的正向尾部隔离恢复画布。12 个有效参考图逐通道零差，6 个 Cursor 参考不可用，不称 18 个全部等价。只将已验证的候选用于默认 Win7 WARP；旧 WARP 内部唯一 opcode、Win7 Hardware 和一次荧光笔定格变深尚未定位/验收。

2026-10-10 验收更新：同一新 EXE（SHA256 `2CDE02EDEE54AECDC08825C8557D792E956CFC0DEEA646ECF29974E2EF6108D8`）在实际 Win7 WARP 与 VMware Hardware FL11_0 上完成对照；Hardware 原版/完整候选正常，105 项 PASS、18 有效参考逐通道零差，WARP 原版仍失败而候选 18 项 PASS。用户另确认直接双击 EXE 两种后端均正常，这项人工正常启动证据与 CMD 的 0/0/1 对照日志分别记录。荧光定格异常本次未复现；新光标横向卡住须按原始输入另查。

### 2. Signatures
- `InkRenderer::Init` 接受设备创建时的实际 `D3D_DRIVER_TYPE`；由 `TransparentPresentationController` 传递 `GraphicsDeviceResources.driverType`。测试调用同样传真实驱动；不按 adapter 名或 vendor 猜测。
- OS 复用 `GetWindowsVersion()` 的动态 RtlGetVersion，不依赖 manifest 的 GetVersion 返回或尚未初始化全局版本。
- GPU CLI / schema / 105=87+18、matrix69 不变。唯一 CMD 新包身份 `Inkeys-Win7-WARP-Fix-x64-Release`，三阶段 original0/original0/guarded1。

### 3. Contracts
- 自动选择：OS major6/minor1 且实际 WARP => 完整 guarded；其他 OS、Hardware/unknown => 正式原资源。
- `INKEYS_DRAW3_WIN7_DIAGNOSTICS` 精确单字符1时允许 override；guarded 精确0强制original、精确1强制guarded、unset/非法值回自动选择。诊断未启用时 ignored override 不能关闭修复或影响其他后端。
- 默认使用同一现场成功数组28980 bytes、SHA256 `2171DDA6E709A3F4CD444E6281AB64635C3F969CD71722662B04DA0DD8055384`；不重新编译另一块 DXBC 冒称同一现场程序。Create失败返回初始化失败，不静默回原版。
- probe RAII 显式 diag1+guard0，保存并恢复两个父环境值。原版87基线和18候选独立；wrapper full三阶段 unset，finally恢复diag/guard/full。
- 保留所有 DefaultInk/薄线/AA/光影/工具/双源 Add-Retain 数学及正式四资源。未来修改正式 HLSL 数学时须同步生成并验证完整兼容变体，不能让 Win7 使用旧公式；新执行字节如不同须重新记录身份与目标环境验收。

### 4. Validation & Error Matrix
| 输入条件 | 选择/处理 |
| --- | --- |
| Win7 WARP，普通启动或只有非法诊断值 | 自动 guarded |
| Win7 WARP，diag1 + guard0 | 明确故障原版对照 |
| 其它 OS/Hardware，diag1 + guard1 | 显式完整候选对照 |
| 任意环境，diag不是精确1 | 不接受 override，按实际OS/driver |
| guard unset/空/00/01/空格/其它非法值，diag1 | 自动策略，不当0或1 |
| 候选CreatePixelShader失败 | Init失败，原HRESULT记录；不伪造fallback通过 |
| Hardware请求FL11_0不可用 | 保持严格exit3，无WARP假冒Hardware |

### 5. Good / Base / Bad Cases
- Good：真实driver沿初始化传播，兼容路径选确切成功完整字节；正常双击即恢复，不需要诊断CMD。
- Base：Win11 WARP/HW、Win7HW保持原资源；ULW与输入没有新增策略。
- Bad：只按Win7不看后端扩大补丁，或者将受坏参考影响的全透明图当作等价，或忘记probe强制原版后把修复后的105项冒充历史基线。

### 6. Tests Required
共享真实选择函数的OS/backend/精确环境矩阵回归（独立前缀，不混入105终态），实际driver桥与初始化失败链审核；完整Solution Debug|ARM64和Release|x64，两架构双后端105项、Headless --no-window。实际WindowsPowerShell ProbeOnly与mock0/0/1、父环境恢复、失败继续/严格identity；PS5.1不代替真实Win7PS2。保留成功数组、正式HLSL/CSO/四资源、旧包和42原始日志身份。

### 7. Wrong vs Correct
- Wrong：Part2仍故障就称默认修复失败，或者把DXGI创建成功与GPU非透明像素/最终桌面可见混为一项。
- Correct：Part2显式guard0保留原版，普通启动按auto；核实际所选byte SHA、GPU源/DIB和人工观察。`completed-with-failures` 包含刻意运行的原版 WARP 失败，不代替独立 `guarded-status` 和实际画布验收。2026-10-10 用户已确认新 EXE 两种后端普通启动正常；仍将偶发输入、定格观察与主画布修复验收分开记录。
- 输入坐标边界：先解码 `mouse-source` 的原始 `lParam`，再对照 `mouse event`、sample 和 laser-tip。连续原消息 X 已为 0 且 accepted=1 时，不能把横向卡住归因于 shader/ULW；若没有系统 `GetCursorPos`、裁剪范围或虚拟机鼠标接管证据，也不能仅凭 Ctrl+Alt 恢复唯一归因 VMware。

## Scenario: Draw3 笔锋长度约束与停笔重平滑

### 1. Scope / Trigger
修改主产品普通笔模拟笔锋的形状、停笔追赶或完成尾段时适用。源任务 `.trellis/tasks/10-11-pen-taper-shape-and-stop-smoothing`；独立 demo 的行为不随本合同改变。数学/无窗口模型验证不等于实际设备的可见手感验收。

### 2. Signatures
- `RebuildL0DrawPoints`、`BuildCompletedPenTail`、`FinalizeStoredStroke` 共用主产品收锋规则。
- `RunPenTaperProductionProbe()` 与早退出 CLI `Inkeys.exe --draw3-pen-taper-test`；无 HWND，不进入正常 Host/RTS/配置启动链。
- `ResolvePenDisplayTime` 仍优先使用已锁定的物理 `terminalDisplayTime`。

### 3. Contracts
- L0 包含完整笔锋和 prediction；100% 起点沿用末端时间回看 configured live-tip duration 的原有计算。不得把 L0 最早点当成收锋起点，也不得改变 prediction 长度或 L1 保护窗口/提交游标。
- 原候选笔锋内以中心线弧长和未收锋最大笔身直径 D 计算有效长度 L；`S(x)=u*u*(3-2*u), u=clamp(x,0,1)`，端头比例 `1-0.7*S(L/(2D))`。L>=2D 为30%，L=D为65%，L=0.5D约89%，L=0为100%。有效区间内再按归一化弧长SmoothStep从100%连接到端头比例。
- 每帧从未经模拟收锋的真实/预测半径重新生成；不把上一帧已收锋半径作为基础输入。保留胶囊公切线安全约束，L1共享连接点和真实中心线路径不改。
- 停笔的显示年龄以最近真实运动和既有预测时间参考计算，合成model输出不能刷新年龄。有效长度按现有T平滑压缩并以上一帧长度封顶；真实Move/新Down/Reset解除旧限制。实际位置变化及成功重连须明确清除长度缓存，不能只比较时间值，因为不同真实Move可具有相同QPC时间戳。前段恢复笔身宽度，剩余过渡整体重算，不能以恢复为粗半径的圆头直接覆盖旧细尾。
- 压缩边界在段内时只对派生几何加入插值连接点，不改`realPoints`、基础压感/速度估算、L1前缀或CPU/GPU结构布局。旧/新L0 bounds共同进入dirty。
- 活动态、完成尾段及Stored使用同一形状求解；缓存的长度/时间须与运动及显示时间兼容。较晚live帧的缓存不得污染较早物理Up；Up锁定后延迟处理不继续老化，长停同位Up不重新收尖。
- 硬件压感、HardPen及禁用taper的工具沿用现有行为；没有新配置或UInk格式迁移。

### 4. Validation & Error Matrix
| 条件 | 必需行为 |
| --- | --- |
| 零长度/重复点/同一时间戳的追赶点 | 弧长求解有界，无除零、NaN或突发整段变粗 |
| 粗笔短锋 | 依据L/D限制收细程度，不强制到30% |
| 停笔后模型继续追加点 | 不刷新真实运动时间，不重新拉长已消退的有效笔锋 |
| 新真实Move/对象复用 | 解除旧停笔长度限制，恢复正常收锋 |
| 物理Up时间早于最后live计算 | 忽略不兼容的未来缓存，按物理Up时间生成完成几何 |
| 终态迟到/保存/重放 | 已锁定宽度不继续随帧时间变化 |

### 5. Good / Base / Bad Cases
- Good：长细锋停下后在原范围内压缩过渡，长度不足2D时端头逐渐变粗，停稳为100%；完成/Stored同规则。
- Base：正常移动的100%起点及L0/L1/预测保护窗口保持原设计。
- Bad：整段L0从最早点开始收尖、按重复点时间越界删除细尾、或使用上一帧收锋半径累计收细。

### 6. Tests Required
生产probe覆盖长度比例/阈值连续性、粗细及小数宽、弧线与稀疏/重复/同时间点、真实模型30/60/120/240FPS及预测模式的逐帧停笔、L1连接/dirty/真实点不改写、续画/多笔、快速/长停/延迟Up与Stored。完整`InkeysRepo.sln Debug|ARM64`和`InkeysHeadlessTests.exe --no-window`；可见GUI/实体设备结果单独记录。

### 7. Wrong vs Correct
- Wrong：`pointAge -> radius立即回到base -> 粗端帽覆盖旧细尾`；或`ApplyTaper(previousL0DrawPoints)`累计缩放。
- Correct：`原笔锋范围 -> 原始半径 + 弧长 -> 压缩有效长度 -> 整段SmoothStep -> 公切线安全 -> 新/旧L0 dirty`。
