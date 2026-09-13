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

## Scenario: Draw3 笔速橡皮的尺度、清扫证据与鼠标生命周期

### 1. Scope / Trigger
修改速度橡皮、显示尺度消费、设备模式、鼠标 Hover/Down/Up、笔交接或宽度插值时适用。2026-09-13 的交互规则替代旧版“提速加快放大、鼠标完整继承 Hover、1.3s 必须回最小”的规则。固定橡皮、其他工具中心轨迹和输入采集不在此控制器范围内。

### 2. Signatures
- 共享实现：`Draw3.SpeedEraser.h/.cpp`，命名空间 `Inkeys::Drawing::Draw3::SpeedEraser`；产品和 headless 编译同一源码。
- `Config::StandardDiameterPx()` 明确令本轮 Dstandard=Dmin；Dmax、厘米/DIP覆盖范围保持不变。
- `Controller` 保留 Reset/UpdatePosition/Advance/PauseForReconnect/ResumeFromReconnect/Diameter/NeedsAnimation/Configuration，增加可诊断的 `SweepEvidenceSeconds()`。
- `MouseLifecycle` 提供 Configure/ObserveHover/BeginContact/EndContact/CancelVisual/Advance/LogicalDiameter/VisualDiameter/NeedsAnimation。它不拥有轨迹容器；EndContact 接收已接受直径值并重置动态控制器。
- `WidthInterval` 与 ContactDiameter 继续使用已解析的像素范围/真实端点半径。Host/WindowController 的逐屏配置发布接口不变。

### 3. Contracts
- 只消费 `physicalSize.available`；不以原始EDID解析成功代替物理有效性，不从DPI反推厘米。大屏覆盖1..12cm/24..288DIP，笔电0.4..4cm/16..160DIP；圆形覆盖使用横纵密度几何平均。
- 可靠直接Touch用cm/s，Mouse/未知直接性的Pen用DIP/s；多屏Touch映射不可靠时同样回退。动作与覆盖尺度独立。速度区间仍为直接2..60cm/s、大屏DIP40..900/s、笔电DIP30..700/s。
- Host低频发布完整显示标尺；批次按原始Down/Up QPC判定重叠并锁存。逐点不重新查硬件，显示变化只影响后续批次。
- 保留80ms真实路程窗、有界历史压缩和过期裁剪；不计算净位移，不以固定样本数量定义快慢。原始状态与帧预览分离，重复/倒退时间不构造速度，迟到的新真实输入仍可重放。
- 持续证据按真实dt和归一化速率累积：160ms开始解锁、380ms满额、200ms衰减常数，速率强度区间0.20..0.75。超过80ms的孤立观测跨度不证明整个空档都在快擦。帧、静止包、预测和连接不能提供新证据或凭残留速度继续扩大。
- 尺寸目标保留对数速度+smoothstep映射，实际尺寸对数跟随且无超调。小尺寸增长tau280ms/比例限速4每秒，较大时渐变到160ms/6每秒；不再有突然加速绕过阻力的通道。
- 只有实际达到覆盖跨度50%且证据达到满额80%才进入清扫；尺寸降到跨度25%退出。保持与确认并行，从普通100/100ms按实际尺寸渐变到清扫650/680ms；缩小tau从160到300ms、比例限速从4到2.2每秒。较小目标持续存在时停止刷新保持，不能永久锁大。
- 鼠标Hover只更新定位，不驱动Controller。每个独立左/右键Down通过BeginContact重新锚定，清空旧速度/证据/收尾画面。StartKind::Hover仍是初始化类别，不能据此禁掉已经Down的控制器。
- 鼠标真实Up在接受终止点后立即Finish，重置逻辑与Controller，不参与启发式断触候选。收尾140ms只画非擦除轮廓，从已接受的实际直径缩到不高于标准值；没有鼠标移动也请求帧，终点停止唤醒。新Down立即截断收尾并小尺寸起步。
- finish标记避免烘干时重复交还，anotherOwner来自实际活动runtime集合；并发最后一个所有者才收尾。失败Down也先保存当前快照以正确清理；取消、切换配置/模式或清屏取消旧收尾。动画按实际呈现QPC推进，不能因耗时帧重启其时钟。
- Pen/倒转Pen保留既有Hover复制和250ms交还语义；Touch真实新Down仍最小起步，以实际位移范围0.1..0.3cm或2..6DIP解锁，累计抖动不解锁。真正重连冻结/恢复状态并重锚，缺失连接不计速度。
- 倒转笔重连资格从真实 `DrawingTool::Eraser` 枚举取值，不再保留过期数值2。Mouse明确Up与此恢复路径分离。
- Contact光标继续用已接受realPoint.r*2；收尾只改光标状态，不回写真实点、烘干或历史。FinalizeStoredStroke仍直接保存r*2，无固定20..200px夹取。

### 4. Validation & Error Matrix
| 场景 | 必需结果 |
|---|---|
| 高速Mouse Hover数秒后Down/静止 | Hover不超过标准，Down历史/证据为零，静止不长大 |
| 50..120ms短快划及随后帧 | 不膨胀成清扫尺寸；测试上限为标准的1.25倍 |
| 持续快擦 | 350ms开始可见扩张；800ms达到至少65%Dmax，1s达到至少85%Dmax |
| 已建立的大清扫短停/折返 | 280ms及500ms测试下降不超过10% |
| 持续慢擦 | 550ms仍保持，900ms有明显缩小；之后最终收敛并休眠 |
| Mouse Up无任何新移动 | 逻辑立即重置，140ms视觉收尾不擦除且能自行结束 |
| Up后20ms再次Down | 新光标/几何小尺寸，无旧速度/视觉残留 |
| 新Down后旧Hover/Up、重复Up、Cancel | 不恢复旧意图，不抢占仍活动的其他所有者 |
| 显示版本变化/并发接触 | 活动配置不混代，旧收尾不沿用新配置 |
| 断触恢复 | 保持真正接触状态，不把桥接位移伪装为真实高速 |

### 5. Good / Base / Bad Cases
- Good：鼠标快速找位置仍小，按下持续局部往返后逐渐扩大；大范围短停保持，主动Up立即退出。
- Base：物理覆盖无效时按DIP回退，标准/最大范围与上一阶段一致；Pen继承路径、Touch点擦和已存几何不变。
- Bad：只夹小Hover画面却累计隐藏速度；把目标一度最大当作已建立清扫；用待用预览直径启动Up收尾；依靠重新移动才能结束动画。

### 6. Tests Required
先在基线运行新增回归并确认失败，再实现新行为。测试使用产品MouseLifecycle和Controller，覆盖两设备模式、物理/DIP、96/144/192 DPI、60/125/240/1000Hz输入及30/60/144/240Hz帧；保留5%一致性和范围边界。测试共享所有权、原地抖动/长按、真实移动、重复/迟到时间、重连、标准语义、收尾无输入/被新Down打断/取消/配置变更、初始化失败和延迟呈现。完整InkeysRepo.sln Debug|ARM64与InkeysHeadlessTests.exe --no-window结果见当前任务validation-mouse-interaction.md。真实硬件体感与Win7运行不得由ARM64编译结果替代。

### 7. Wrong vs Correct
~~~cpp
// Wrong：新鼠标Down继承定位速度，Up烘干后才把旧清扫交回Hover。
runtime.speedEraserOc = mouseHoverController;

// Correct：独立鼠标Down清空意图；Up只把实际端点直径交给非擦除收尾。
mouse.BeginContact(runtime.speedEraserOc, x, y, rawSeconds, config);
mouse.EndContact(runtime.speedEraserOc, acceptedDiameter, x, y, upSeconds, anotherOwner);
~~~
