# Input and Ink

本页用 `【直接确认】`记录实现事实，用 `【合理推断】`记录修改时的审查建议，用 `【待确认】`记录没有运行验证或维护者契约的内容。传统实现事实不自动成为未来新输入系统的强制设计。

## 输入归一化链

`【直接确认】` 当前产品输入主链位于 `Draw3.RealtimeStylus.*`、`Draw3.ContactInput.*` 和 `Draw3.DrawingController.*`；Draw2 输入源码保留但不参与产品编译或启动：

~~~text
主 Drawpad RealTimeStylus ─> ContactInputCoordinator mailbox
                                        │
                                        └─> DrawingController / document / runtime history / final backbuffer
~~~

`RealTimeStylusInput` 是主 Drawpad 的唯一 RTS producer，并保留 `SetAllTabletsMode(TRUE)`；辅助 DrawpadPresentation 不绑定 RTS、WndProc mailbox 或输入门禁。`CSyncEventHandlerRTS`/`TouchMode` 描述仅属于保留的 Draw2 历史实现，不应反推 Draw3 的产品选择模式。

Draw3 Host 在图形资源准备后才初始化 RTS，退出时先停止 producer，再唤醒并结束绘制线程；`DrawpadMsgCallback` 只把主 Drawpad 消息转发到唯一 Host。

`【合理推断】` 需要让鼠标、笔、触摸行为一致的改动，优先放在两条输入已汇合的位置；如果只改 `CSyncEventHandlerRTS` 或只改 `DrawpadMsgCallback`，需明确另一条路径为何不适用。

### UI3 Bar 接触消息归一化合同

`【直接确认】` Bar 注册的 `WM_TOUCH` 会把主接触（包括当前系统上报的 Pen）转换成带内部来源标记的 `ExMessage`。Windows 同时可能为同一接触派发 Pointer 兼容鼠标消息，其 `GetMessageExtraInfo()` 的来源签名为 `0xFF515700`（掩码 `0xFFFFFF00`）。

`【直接确认】` Pointer 兼容鼠标副本必须在 Bar 的窗口过程和 HiMsg 入队回调两个入口都丢弃；不带该签名的真实鼠标消息仍正常入队。低位 `0x80` 只能用于诊断 Pen/Touch 分类，不能决定一次接触的输入归属，因为同一支笔的 Down/Up 可能出现不同低位值。触摸长按右键抑制依赖这条单一 `WM_TOUCH` 路径，不得重新保留 Pen 兼容鼠标分支。

按钮点击完成必须等待明确的 `WM_LBUTTONUP`；`WM_MOUSEMOVE` 的 `MK_LBUTTON` 缺失不能被解释为抬起，否则会把接触或窗口激活阶段的移动误判为第二次点击。

## RTS 初始化与回退

`【直接确认】`：

- `IdtRts.cpp` 的不同初始化路径包含关闭 flicks、启用多点触摸、注册 stylus plugin 等操作；
- 初始化/插件注册失败会写日志并允许鼠标回退；
- 画板窗口处理 tablet gesture、触摸注册和光标显示；
- 实际输入模式还读取配置和硬件/窗口状态。

`【合理推断】` 现有 SafeRTSInit 和鼠标回退是启动兼容边界。改变 RTS 初始化时应保留可诊断的失败路径，除非任务明确决定取消回退并验证启动行为。

`【待确认】` 仓库没有给出各 Windows 版本、触摸屏、数位笔驱动和 RTS 版本的正式支持矩阵。

## 笔画生命周期

`【直接确认】` `IdtDrawpad.cpp::MultiFingerDrawing` 为每个活动接触点处理临时 `StrokeImageClass`/`DibSurface`。可见行为包括：

- 普通画笔用 GDI+ 曲线绘制采样点；
- 荧光笔走半透明合成，当前代码可见 alpha 130；
- 橡皮直接修改基础 `drawpad`，范围可受压力、速度或固定宽度影响；
- inverted pen 和部分右键路径切换为橡皮语义；
- 形状工具使用独立状态和预览/落笔逻辑；
- `IdtDrawpad.h` 对 `StrokeImageClass::endMode` 注释为：1 绘制/合并到画布，2 不绘制/不合并。

`IdtDrawpad.cpp::DrawpadDrawing` 周期合成基础 `drawpad` 与 `StrokeImageList` 中的活动笔画，再写入 `window_background` 并更新分层窗口。完成笔画还会与 `RecallImage` 历史以及放映中的 `PptImg` 页级画布交互。

这些是 `【历史/兼容】` 的现有 Draw2/GDI+ 墨迹模型；图像承载已迁为 `Inkeys.Graphics.DibSurface`，不再依赖 EasyX `IMAGE`。新渲染实现不必复制所有全局结构，但改动当前路径时不能忽略其层次和页级状态。

## 共享状态与并发风险

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
2026-09-14 同步已接受的 Mouse/ScreenPenHybrid 行为与第七轮 Touch 规格。替代旧厘米覆盖、Mouse/Pen 静止回标准、Touch 整目标物理一致的描述。鼠标和屏幕笔的参数、补偿、Hover/Down/Up 保持冻结；本轮只改善 Touch 与可关闭的面积辅助，不重写输入队列、模型、中心轨迹或渲染器。

### 2. Signatures
- `EraserSizes` 全部是直径 DIP：minimum=16、standard=32、maximum=160、touchStart=16、fixed=50；`DiameterToCanvasPx` 仅在坐标边界换算，`FixedDiameterPx` 完全旁路动态控制。
- `ResolveConfig(display, mode, InputSource, sizes)` 分离真实来源、响应模型与标尺。来源通过当前 RTS context 的能力及精确 Pointer cursor 对应关系缓存，不改变真实 InputDeviceType。
- `Controller::UpdatePosition(x,y,seconds,contactArea,terminal)` 只消费真实输入；`Advance` 不制造运动证据；`AreaDiagnostics`、`NextAreaWakeSeconds` 暴露有界状态与过期唤醒。
- `ContactSnapshot` 保留 `rawContactSize`、既有 `contactSize` 和 `contactAreaUnits`。面积单位状态不影响正常位置、压力、倒转和接触身份。
- `DevelopmentOptions::touchContactAreaAssistance` 默认关闭。实验选项和程序调测共用 Host 的同一个临时选项，不写正式配置；由独立接触批次锁存。
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
- Touch 动态目标直接用 DIP，不做 rho 整目标补偿。Touch 精细/进入/退出/大目标：物理 30/90/60/250 mm/s；DIP 100/240/160/700 DIP/s；经验 100/120/80/400 reference DIP/s。
- Touch 历史窗 50ms，证据 start/full/decay 为 25/60/180ms；增长 tau 120/100ms，对数增长限速 6/8 每秒。原有缩小/保持参数不全局改动。面积开关关闭时同样使用新 Touch 速度模型。
- 真实路程按时间积分，折返不作净位移抵消；预测、补点、缺失连接和面积变化不提供速度资格。实际位移解锁仍为物理 1–3mm 或回退 2–6 动作单位。
- Touch 的移动目标不能因为每包误差小于 settle tolerance 就立即吸附。仅目标稳定时允许小误差收敛，否则高回报率会绕过阻尼。此修正规则不改变冻结的间接/屏幕笔响应。
- Mouse 和 Pen Hover 在 minimum..standard 内预览，不积累清扫；Down 只继承新鲜、兼容的安全尺寸并清空动量。Mouse Up 保持约140ms非擦除收尾；Pen保留250ms交接及真实断触语义。关闭面积时持续精细/静止可回 minimum。
- 面积辅助仅适用于真实、映射可解释的屏幕 Touch 笔速橡皮，强制响应模型不能伪造真实 Touch；Mouse、Pen、TouchPad、固定橡皮均不使用面积下限。
- WIDTH/HEIGHT 必须实际存在于返回的 packet description。保留原 per-context 换算；只有宽高与对应 X/Y 的长度单位、分辨率相符且在声明范围内，才认可为画布像素。PROPERTY_UNITS_DEFAULT 表示未知，不能默认当像素。
- 已确认的面积按 `wDip=wPx*dipPerPixelX`、`hDip=hPx*dipPerPixelY` 换算一次，不使用 EDID、压力或 WM_TOUCH 的百分之一像素规则。
- 面积默认拒绝范围外 2..96 DIP、长宽比超过3.5、非有限/非正值和离群跳变。拒绝阈值与辅助上限是两件事，巨值不能被夹成64DIP后使用。
- 稳定真实拖动确认50ms后锁定本接触的参考；面积参考可与位移解锁并行准备，但实际下限须通过原位移保护。参考不随重压、摊开或噪声反复变大。
- 下限为 `clamp(1.10*max(wDip,hDip)+6, standard, min(maximum,max(standard,64)))`。自定义 standard 大于64时不反转 clamp 上下界。它是有界拖擦下限，不是手掌分类或压感橡皮。
- 合成 `max(speedTarget, contactFloor)` 后继续平滑；面积下限无需清扫资格，但只在真实移动中提高已接受尺寸。静止的新面积/新包不能反向放大当前工具。
- 缺包最长保留2s，显式无效值宽限200ms，过期参考按180ms释放。真实重连平移这些时钟；Up的零面积不是新参考。
- 达到辅助下限后停帧，由既有等待机制在面积过期时唤醒。不能让下限抬住尺寸却一直以16DIP作为未达成目标请求帧。
- 原始输入与帧预览状态分离；静止只更新当前工具和待用尺寸断点，恢复实际几何时追加同位小半径锚点，不覆盖历史或恢复已擦内容。
- Touch笔速输入恢复时，若距离上次成功建模的时间乘输出采样率将超过单次输出上限，复用模型Reset/Update在最后已接受位置建立短时间种子，再提交真实输入。不清空历史结果、转换游标、接触身份或尺寸/面积控制器，不增加模型输出上限；此路径不用于Mouse/Pen或其他橡皮模式，种子不提供运动证据。
- 原始面积有效性独立于实验开关；关闭辅助仍可显示已确认的DIP宽高。未确认单位明确显示unverified，不标成可信像素；referenceFresh单独表示参考是否过期。
- 诊断每帧可关闭发布，不逐点同步日志。包含真实来源、模型、单位、像素/DPI/手动尺寸、速度/目标/实际DIP、原始/换算面积、有效性、参考/实际下限及最终几何半径。
- 面积开关独立发布并锁存，不进入 Mouse/Pen 的显示标尺变更判定，避免点击 Touch 开关使精细 Hover 重置。

### 4. Validation & Error Matrix
| 场景 | 必需结果 |
|---|---|
| Mouse/ScreenPen冻结轨迹 | 改动前后浮点位模式一致 |
| 面积关闭、不同可信密度的同物理Touch运动 | DIP目标一致，不恢复整目标物理补偿 |
| 未知单位/缺失/零/负值/巨值/离群 | 辅助拒绝或平滑释放，正常触摸仍接收 |
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
Applies when reading RTS WIDTH/HEIGHT, interpreting PROPERTY_METRICS or logging TouchArea diagnostics. Speed response, size curves, DIP defaults, area multiplier/ceiling, input position collection and EDID semantics are frozen.

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
