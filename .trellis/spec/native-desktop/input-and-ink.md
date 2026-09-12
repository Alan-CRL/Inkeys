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

## Scenario: Draw3 笔速橡皮的尺度与历史动态

### 1. Scope / Trigger
修改速度橡皮、显示尺度消费、设备模式、Hover/Down 交接或宽度插值时适用。固定橡皮、其他工具的轨迹平滑和输入采集不属于此控制器。

### 2. Signatures
- 独立共享实现：`Draw3.SpeedEraser.h/.cpp`，命名空间 `Inkeys::Drawing::Draw3::SpeedEraser`。
- `DisplayScale` 保存显示 generation、发布 revision、monitor 身份、X/Y DIP/px、X/Y cm/px、physicalAvailable 和 directTouchMapped。
- `ResolveConfig(display, DeviceMode, touch)` 独立解析动作尺度与覆盖尺度；`Controller` 提供 Reset/UpdatePosition/Advance/PauseForReconnect/ResumeFromReconnect/Diameter/IsPaused/NeedsAnimation/Configuration。
- `WidthInterval` 携带实际像素宽度端点及本配置上下限；`InterpolateDiameter` 与 `ContactDiameter` 由产品和 headless 共用。
- `ProductState::paintDevice` 保留旧配置含义：0 为大屏、1 为笔电；设置变更调用 `SyncDraw3State()`。
- WindowController 用 `SetSpeedEraserDisplayScale/SpeedEraserDisplayScaleSnapshot` 和 `SetSpeedEraserDeviceMode/SpeedEraserDeviceModeSnapshot` 发布小型线程安全配置。

### 3. Contracts
- 只消费第一阶段 `physicalSize.available`，不从 raw EDID 或 DPI 反推物理有效性。大屏覆盖 1..12cm/24..288DIP，笔电覆盖 0.4..4cm/16..160DIP；圆形像素直径使用横纵密度几何平均。
- 直接 Touch 只有目标映射可靠时使用 cm/s。现有 Pen/Mouse 不具备可证明的直接触屏关系，速度使用 DIP/s。当前映射无法证明的多屏 Touch 同样回退；EDID 覆盖有效性与动作映射独立。
- 直接动作阈值 2..60cm/s，DIP 大屏40..900/s、笔电30..700/s。两种模式只换参数，不分裂动态算法。
- Host 启动、显示通知、窗口换屏/DPI变化低频更新；回调只发布/wake。绘制批次锁存尺度和模式；按原始 Down/Up QPC 判断重叠，不能因同帧先消费 Up 就丢弃旧标尺。断触继续用旧批次，下一批次用新 revision。热点不取显示快照或查询硬件。
- 控制器统计真实原始位置和QPC的80ms路程窗；40/160ms标量速度只允许更快放大，方向改变不算提速。时间窗满时裁剪过期部分并合并最短相邻区间，保留路程/时长；不得反复延长同一旧段，把过期高速拖进新窗口。
- 速度经对数归一化与smoothstep给目标，对数直径按时间跟随；清扫保持180ms与8%较小目标确认180ms并行。默认放大140ms、提速80ms、缩小240ms，比例变化率+6/-3每秒。不得串联中间尺寸等待或把加速度累积进尺寸。
- 真正Touch Down始终从最小起步；落点实际位移范围0.1..0.3cm或2..6DIP解锁。累计抖动路程、静止时间、预测、补点均不是启动证据。
- 动画时间与原始输入时间各司其职。重复/倒退原始时间不制造速度；迟到但更新的raw样本不能仅因帧时钟更晚而被当重复。
- Hover保留现有mouse/pen/inverted lanes及250ms交还边界；取消或配置不兼容不继承旧运动基准。断触暂停冻结，恢复重新锚定位置/时间，缺失段不伪装为极短真实运动。
- Contact光标使用已接受realPoint.r的两倍；Advance的待用尺寸不单独放大Contact光标。Hover可使用动态尺寸。真实宽度插值使用区间自带界限，不能再夹到20..200px。
- FinalizeStoredStroke保存realPoint.r*2；已有几何与烘干/历史复用同一数据。显示配置变动不重算历史宽度。

### 4. Validation & Error Matrix
| 条件 | 必需结果 |
|---|---|
| EDID失败/复制/未知拓扑 | 覆盖DIP回退，物理有效标志不伪造 |
| EDID有效但输入映射未知 | 可用物理覆盖，动作仍为DIP |
| 120ms以内快速折返/短停 | 直径跌落不超过10% |
| 持续静止或精擦 | 约1.3s回到小尺寸附近，静止最终结束动画 |
| 新Touch Down/原地抖动/长按 | 最小尺寸起步，不靠时间或抖动路程放大 |
| 断触重连且模型失败 | 不增加连接速度；完整控制器状态仍可回滚 |
| 显示revision/设备模式在批次中变化 | 活动批次不混代，下一批次应用新配置 |
| 存储宽度大于旧200px | 保留实际宽度，不因读写/烘干改小 |

### 5. Good / Base / Bad Cases
- Good：单屏Touch局部往返使用cm速度，大范围保持；慢擦确认与保持同时进行，之后平滑缩小。
- Base：未知Pen使用DIP速度，屏幕EDID仍可定义覆盖直径；无EDID时两项均明确回退。
- Bad：把重连位移除以1微秒、把target monitor的物理位移当数位板手部位移，或让Contact光标显示尚未产生的擦除宽度。

### 6. Tests Required
共享实现的确定性轨迹测试覆盖两模式、96/144/192 DPI、旋转/异向尺度、物理与DIP独立回退、Touch点擦/抖动、往返/突增/慢擦、迟到/重复时间、暂停与重连、宽度插值及Contact宽度。采样60/125/240/1000Hz与不同帧调度的关键宽度偏差目标<=5%。运行完整InkeysRepo.sln Debug|ARM64和InkeysHeadlessTests.exe --no-window；执行结果记录在当前speed-eraser-physical-scale任务中。真实设备体感和Win7运行兼容性不得仅凭ARM64构建宣称已验证。

### 7. Wrong vs Correct
~~~cpp
// Wrong：连接端点不是已观测到的高速输入。
controller.UpdatePosition(resumeX, resumeY, pauseTime + 0.000001);

// Correct：保留尺寸/状态，重新建立真实运动起点。
controller.ResumeFromReconnect(resumeX, resumeY, rawQpcSeconds);
~~~
