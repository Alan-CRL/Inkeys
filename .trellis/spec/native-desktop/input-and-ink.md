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

## Scenario: 橡皮统一DIP尺寸、动作资格与当前工具状态

### 1. Scope / Trigger
2026-09-13 本轮产品修正规则替代旧版厘米覆盖、standard=minimum、接触光标绑定历史末点、静止500ms不缩小的要求。仅涉及橡皮尺寸/动作控制、必要生命周期和几何尺寸断点、诊断与测试；不重写输入采集、模型、中心轨迹或渲染器。

### 2. Signatures
- `SpeedEraser::EraserSizes`：minimumDiameterDip=16、standardDiameterDip=32、maximumDiameterDip=160、touchStartDiameterDip=16、fixedDiameterDip=50；全部是直径DIP。
- `DiameterToCanvasPx(diameterDip, display)` 只读取DIP/px；`FixedDiameterPx(selectedDip,display)` 是无控制器的固定旁路。
- `ResolveConfig(display,mode,touch,sizes)` 保留动作尺度、产生DPI转换缓存；`Controller::DiameterDip()` 为业务输出，`Diameter()` 为现有像素几何的适配出口。
- `ContactSizeState` 分离effectiveDiameterPx、尺寸断点和旧几何；Update/MakeInterval/Accepted与产品共用。
- `AppendEraserSizeAnchor` 在下一次实际几何提交前追加同位置、不同半径的点，不覆盖旧点。
- `HostStartOptions::enableEraserDiagnostics` 默认false，`HostRuntimeSnapshot::eraser` 提供有界快照；隐藏注入门开启时自动启用。测试入口 `--draw3-eraser-hidden-test` 与旧全量 `--draw3-hidden-test` 分离。

### 3. Contracts
- EDID、物理尺寸、设备种类或大屏/笔电模式不能改写EraserSizes。DPI只把DIP换成像素，普通等比为dip*dpi/96、半径再除2；非等比保留现有圆形像素几何，使用DIP密度几何平均。历史仍保存已生成像素宽度，不追溯换算。
- 第一阶段physicalSize.available、原始EDID/业务有效性分离与活动拓扑判断保持。可靠直接Touch可用cm/s；鼠标/未知直接性的笔和不可靠映射用DIP/s。cm/px绝不能用于计算尺寸上下限。
- 固定模式明确使用50DIP作为原50px在96DPI下的基准标定；不是把旧保存的像素字段解释成DIP。旧eraserSize未被Draw3消费，本轮不迁移或重解释它。固定旁路不走速度、证据、保持或EDID；其他画笔单位行为不变。
- 清扫资格与尺寸曲线分开：笔电DIP进入800/退出600/大目标1900每秒；大屏DIP650/450/1700每秒；可靠Touch25/18/70cm/s。这些是集中可调原型参数，不是硬件定律。低于进入速度不能靠时间积满证据。
- 保留80ms有效路程窗、160ms历史保留、有界相邻区间合并与过期裁剪、原始/帧状态分离。证据始终泄漏，100ms开始/240ms满额/350ms泄漏时间常数；滞回退出不使阈值以下的普通动作产生新证据。
- 初始Mouse/Pen32DIP，Touch16DIP；普通真实Touch移动可按原位移证据过渡到32DIP，不要求清扫资格。长按/有界抖动不解锁。两种设备模式共用动态实现，只调动作参数。
- 最后有效移动独立于最后包时间，噪声阈值为DIP动作0.75、cm动作0.02。约280ms静止后从当前尺寸按200ms时间常数/4每秒对数限速回落；Mouse/Pen目标为32DIP，未解锁Touch回16DIP。不能叠加旧清扫确认再开始静止回落。
- `RuntimeSpeedEraserContactDiameter` 读取ContactSizeState，不读历史realPoints.back().r。完全无Move也按单调帧时钟推进并清理旧轮廓；稳定后复用输入唤醒等待，不继续请求渲染帧。既有WaitForWake的0是轮询，不能当作无限等待。
- 静止尺寸变化只记待用断点；恢复实际模型点时追加同位尺寸锚点，新移动段从回落后的半径开始。旧大圆/历史点保留；零长度半径过渡在现有胶囊着色器中退化为已存在的大圆。持久化逐点保存r*2，支持原文件格式。
- 迟到的断点之前输入不被伪装成之后的运动；真正awaitingReconnect继续冻结并重锚，合成连接不计速度。鼠标Hover无速度继承、新Down标准起步、Up立即结束逻辑及140ms视觉收尾、笔250ms交接、并发所有权和批次显示版本保持。
- 诊断只在启用时每帧发布，包含输入/模式/动作单位/DPI、DIP属性、速度/证据/状态、控制器DIP、最终光标px、下一段半径px、最后有效移动年龄、实际新段足迹及最多三个断点坐标/宽度。不逐点同步记录日志。

### 4. Validation & Error Matrix
| 场景 | 必需结果 |
|---|---|
| EDID有效/失效/不同物理尺寸及设备模式 | 同一DIP属性完全相同，只影响动作解释 |
| DPI96/144/192 | 像素变化后换回DIP仍为16/32/160/Touch16 |
| 普通速度长擦20秒 | 标准范围，不因时间充满证据 |
| 临界、中速、高速扫描 | 清扫资格显式，目标连续，无无条件贴最大 |
| 真实无Move、小噪声、同位置包 | 相同有效移动时钟，当前工具可见回缩，历史点不改写 |
| 恢复短Move | 新段实际足迹用小半径，无旧大半径插值拖尾 |
| 标准后继续按住 | 无多余帧；Move/Up/控制请求立即唤醒 |
| Up、快速再Down、Touch点擦、真重连 | 保持既有生命周期正确性 |
| 固定直径42DIP旁路 | 不受速度、时间、EDID、模式影响 |
| 尺寸锚点保存/读取/导入 | 同位旧/新宽度都保留，Undo/Redo可用 |

### 5. Good / Base / Bad Cases
- Good：标准32DIP普通擦除，明确快擦后扩大；原地停住可见回32DIP，恢复小移动从小半径开始，历史大圆仍在。
- Base：无EDID仍有完整DIP尺寸，只有动作解释回退；固定50DIP独立工作。
- Bad：厘米生成像素再反称DIP；只缩光标却沿旧半径插值；修改历史末点半径；以不断投递静止包替代无事件测试；把全量隐藏套件失败写成通过。

### 6. Tests Required
Headless覆盖尺寸不依赖硬件、固定旁路、普通20秒/中间速度扫描、真正无包状态推进、噪声/同位置、Touch过渡、生命周期、60/125/240/1000Hz与不同帧率，保留5%一致性。完整InkeysRepo.sln Debug|ARM64后执行--no-window；专项隐藏测试在干净DComp/ULW Host验证最终光标、历史点不变、收敛停帧、实际新增足迹、Undo/Redo与生产UInk断点文件往返。全量旧隐藏套件结果单列，详见当前任务validation-dip-idle.md。

### 7. Wrong vs Correct
~~~cpp
// Wrong：历史不能为了表示当前工具而被改小。
stroke.realPoints.back().r = currentRadius;

// Correct：当前工具独立推进，真正恢复几何时才追加尺寸锚点。
runtime.eraserSize.Update(controller.Diameter(), nowSeconds, stationary);
AppendEraserSizeAnchor(stroke, interval);
~~~
