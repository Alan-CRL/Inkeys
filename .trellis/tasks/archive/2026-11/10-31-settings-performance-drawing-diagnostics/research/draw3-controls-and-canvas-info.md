# Draw3 绘制开关与画布信息调研

日期：2026-10-31。范围：仅源码与规范调研，不修改产品代码，不启动 GUI，不执行构建。本文件中的“事实”来自当前产品路径；“建议”供主任务设计使用，不代表已实施或已通过运行验收。

## 1. 已确认的现有能力

### 1.1 激光笔多指与粒子

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cppm:235`、`:236`：`StrokeModelConfiguration::laserParticlesEnabled` 与 `laserMultiTouchDrawingEnabled` 均默认 `false`。
- `Draw3.DrawingController.cppm:141`、`:145`：已有 `SetLaserParticlesEnabled(bool)` 与 `SetLaserMultiTouchDrawingEnabled(bool)`，不需要另写粒子或激光轨迹算法。
- `Draw3.DrawingController.cpp:9230`：粒子 setter 写原子期望值并 `input_.PublishControlWake()`；`:9241` 激光多指 setter 写原子值，之后的新 Down 读取它。
- `Draw3.DrawingController.cpp:3548`：`IgnoreAdditionalLaserTouch` 只拦 Laser 工具的第二根及后续 Touch，Pen/Mouse 与其他工具不受它影响。它调用 `DiscardUntilTerminal`，没有直接释放 producer 尚持有的 route。
- `Draw3.DrawingController.cpp:10766`：现有门禁只把尚未 ended、非 awaitingReconnect、实际仍物理活跃的 runtime 当作活动接触；`:10784` 在生产 Down 初始化调用 Laser 门禁。
- `Draw3.Host.h:415`、`Draw3.Bridge.h:230`：Host 的公开设置接口与 `Bridge::ProductState` 当前均未接入上述两个激光开关。`Draw3.Host.cpp:2036` 构造默认模型配置，因此产品目前使用默认关闭。

**重要的实际生效边界：** `Draw3.DrawingController.cpp:15463` 至 `:15469` 只在 `LaserTrailPhase::Inactive` 更新 `particlesEnabledEffective`。切换期望值并不会改变当前正在显示的激光笔画、Hold 或 Fade；它在整批激光完全消失后的下一笔生效。这个实现比 `.trellis/spec/native/runtime-and-rendering.md:201`、`:209` 的“关闭后下一帧 reset”描述更晚，规划应以当前代码为准，本任务不顺手改动此生命周期。激光多指沿用规范 `runtime-and-rendering.md:192` 的“只影响之后开始的 contact”。

### 1.2 通用多指开关

- 当前配置、Host 与 Controller 没有通用多指绘制开关；只存在激光专用的多指开关。
- `Draw3.RealtimeStylus.cpp:2688` 固定调用 `put_MultiTouchEnabled(TRUE)`，该项属于 RTS producer 的多点采集能力，不是现成的产品单指策略。
- `Draw3.DrawingController.cpp:10604` 的 `initializeStroke` 是两类 producer 汇流后的 contact 初始化入口；普通工具和橡皮均在同一 controller 下逐 contact 解析。
- `Draw3.DrawingController.cpp:10657` 至 `:10729` 会先调用 `touchGesture.OnTouchDown` 维护手势状态；生产画布平移仍被 `kCanvasNavigationProductIntegrationEnabled` 关闭。因此不能在手势登记后随意丢弃新 Touch，又不处理对应手势登记。
- `Draw3.DrawingController.cpp:10840` 后才进行普通 Touch 的断触接续匹配，Laser 不参与该接续。收尾模型、awaitingReconnect 候选不是仍然落下的物理接触。

## 2. 建议的最小产品接线

### 2.1 状态与线程

建议新增一组由 Host 保留的原子期望值（通用多指默认 true，激光多指及粒子默认 false），沿用 `Host::SetInterruptedStrokeReconnectEnabled` 的已有模式：

1. Setting 更新当前配置并请求既有持久化 FIFO。
2. 调用 Host 的产品设置入口，入口只保存期望值并 `PublishControlWake`，不借用 `drawing` 指针。
3. Host 的绘制 owner 在 `PumpBridgeState` 中应用变化。依据 `Draw3.Host.cpp:1631` 至 `:1644`：独立偏好在 bridge revision 的早退前消费，避免工具/颜色没有变化时设置丢失。
4. Controller 增加通用多指设置；两个激光设置复用现成 setter。启动创建模型配置时也带入这组偏好，避免第一次 Down 使用旧默认。
5. Host Start/Stop 不抹除期望值，启动前设置与 DComp 失败重建后的 Host 都沿用同一配置。依据既有 setter 的注释与行为 `Draw3.Host.cpp:2683`。

不用给 `ProductState` 添加与工具/页面状态无关的重复字段；可沿用 Host 独立偏好模式。`Drawing.*` 当前已管理 Haptics/Cursor/Eraser，新增产品偏好可沿其 schema 归属；不得恢复已退役旧 `deploy.json/Performance` 容器。

### 2.2 有效值与显隐

- 持久化保存原始激光多指偏好；运行时 `effectiveLaserMultiTouch = multiTouchDrawingEnabled && savedLaserMultiTouchEnabled`。
- 多指绘制关闭时隐藏“多指激光笔”行，不能因隐藏把其保存值清为 false；重新开启全局多指后恢复之前偏好。
- 粒子设置不依赖多指开关，始终显示；两个激光设置说明均包含 BETA。
- 激光多指只改变之后的 Down，粒子保持上节确认的下一激光批次生效边界；不重写或立即清空现有轨迹。

### 2.3 通用 Touch 准入与生命周期

建议“全局多指绘制”覆盖所有真实的 Touch 绘制接触，包括普通笔、荧光笔、形状、橡皮和激光；Pen/Mouse 不计入这个 Touch 数量，也不改变既有批次、输入所有权或倒转 Pen 行为。橡皮若要特别豁免，属于额外产品选择，当前源码没有该豁免依据。

最小行为为：开关 true 保持现状；false 时有第一根已准入且物理仍活跃的 Touch 才拒收新的额外 Touch Down，不强制取消已开始的笔画，不让被忽略的手指在 Move 阶段自动接管。其 Up/Cancel 完成后可重新 Down 获得准入。不要把已 ended、等待接续、仅 Hold/Fade 的 runtime 当作仍按下的第一指。

具体入口应在 `initializeStroke` 内决定，且在分配 stroke/model 与触摸手势登记之前完成拒收，或显式对已登记手势作一次对称撤销。需要复核配置禁用 Touch 橡皮等“不实际绘制”的入口，不让被业务拒绝的 contact 错占第一绘制指资格。

拒收动作必须复用 `ContactInputCoordinator::DiscardUntilTerminal`（`Draw3.ContactInput.cpp:870`）：

- Producing → Quarantined，由唯一 producer 在真实 Up/Cancel 自回收；
- Closing → ClosingDiscarded；
- 已终态 ConsumerOwned 可 Recycle；
- 重复处理不重复计数。

不能直接 `Recycle(handle)` 后丢弃 producing route，也不能设置整个 coordinator admissionBlocked 来做单指策略，否则会同时拒绝第一指、Pen/Mouse 或破坏全局场景门禁。

**已知历史风险：** `.trellis/tasks/archive/2026-10/09-27-crash-restart-recovery/research/laser-ignored-touch-slot-implementation.md:7` 记录了第二根 Laser Touch 直接 Recycle 导致 slot 泄漏的真实修复。现有 `RunIgnoredLaserTouchProductionTest`（`Draw3.DrawingController.cpp:6547`）覆盖 32 轮第二指、Up/Cancel、自回收、同址新 generation、首指保留、非 Touch 与非 Laser 负例，不能退回旧做法。

断触续接属于独立生命周期：应保留当前来源身份、时间与位置准入；通用单指策略不能随便把待续接候选当活指，也不能让被忽略的指绕过 gate 参与续接。现有 Laser helper 因 Laser 从不 reconnect，不能只改函数名就声称已覆盖普通工具的全部情形。

## 3. 画布信息：真实数据现状

### 3.1 Host 已有可跨线程快照

- `Draw3.Host.h:219`：`HostPresentationMode` 包含 Automatic、UlwDirtyRect、DirectCompositionVisualTree，以及首发禁用的历史 DWM 枚举。UI 只说明实际模式，不开放历史模式。
- `Draw3.Host.h:234`：`HostInputMode` 为 Disabled、RtsExtended、RtsBasicXY、Win32Mouse。
- `Draw3.Host.h:319` 的 `HostRuntimeSnapshot` 已有 running、firstFrameReady、lastPresentSucceeded、presentationMode、inputMode、present 计数、committedWidth/Height 等。
- `Draw3.Host.cpp:2447` 至 `:2480` 从原子/受锁状态复制该快照；Setting 已可通过 `ProductRuntimeSnapshot` 读取。快照不包含实际图形设备类别、适配器描述或交换链描述。
- `Draw3.Host.cpp:952` 在观察实际呈现后同步 presentationMode；`:1999` 启动 presenter ready 时也写入，不应根据配置里的 DComp 偏好推断实际模式。

### 3.2 实际设备与适配器

- `Draw3.GraphicsInitialization.cppm:17` 的 `GraphicsDeviceResources` 已保有实际 `driverType`、`featureLevel`、adapter 及设备对象；它们由绘制 owner 独占。
- `Draw3.GraphicsInitialization.cpp:111` 至 `:126`：关闭加速直接 WARP，开启则硬件失败自动回退 WARP。因此不能把配置 EnableHardwareAcceleration=true 显示成“当前硬件”。
- `Draw3.GraphicsInitialization.cpp:81` 至 `:107`：优先 11_1/11_0 列表，E_INVALIDARG 时仅重试 11_0；应输出实际 featureLevel，而不是请求列表。
- `Draw3.Diagnostics.cpp:746` 至 `:778` 的 `LogAdapterDiagnostics` 已展示：名称、VendorId、DeviceId、SubSysId、Revision、专用显存、专用系统内存、共享系统内存，并用 `CheckInterfaceSupport(IDXGIDevice)` 读取 UMD driver version 原始高/低位。
- 这些成功环境日志受 `StartupEnvironmentDiagnosticsEnabled` 控制（`Draw3.GraphicsInitialization.cpp:138`、`:160`）。设置页的信息块不能要求先开控制台或重启打开日志。
- 某些元数据查询失败时应单独标“不可用”及 HRESULT，不把整个绘制设备判为失败，也不把默认 0 当成真实硬件身份。

### 3.3 交换链与呈现

- `Draw3.TransparentPresentation.cpp:900` 至 `:927`：基础描述为 BGRA8、双缓冲、STRETCH、FLIP_SEQUENTIAL；GPU 透明路径 premultiplied alpha，ULW 是 unspecified alpha。
- `:858` 至 `:898` 先尝试 frame-latency waitable swapchain，创建、QueryInterface 或 SetMaximumFrameLatency 失败都会回退 ordinary。不能只显示模板常量或“尝试过 waitable”当成当前结果。
- `:822`：DComp 使用 `CreateSwapChainForComposition`，ULW 使用 HWND swapchain；ULW 的最终桌面呈现是 CPU 读回 + UpdateLayeredWindowIndirect，不等于没有交换链。
- `Draw3.Diagnostics.cpp:733`：已有日志通过真实 `IDXGISwapChain1::GetDesc1` 读取 size/format/bufferCount/swapEffect/alphaMode/scaling/flags。`Draw3.TransparentPresentation.cppm:122` 暴露 `SwapChain()`，但只允许绘制 owner 调用，不能把 COM 指针交给设置页。
- `Draw3.TransparentPresentation.cpp:1183` 至 `:1239` 有运行期 presenter/设备恢复，设备丢失可重建为不同实际后端；初始化快照不能永久显示初次成功的旧设备信息。
- 主 Drawpad 的 DComp 与长期待命的 selection-only ULW 前置表面同时存在。画布信息应说明“主画布呈现”与辅助表面，避免在主画布 DComp 时因为辅助 ULW 错报为整体 ULW。

### 3.4 RTS 与鼠标兼容

- `Draw3.Host.cpp:2225` 至 `:2256` 真实选定 RtsExtended / RtsBasicXY / Win32Mouse，最终枚举已有安全快照。
- `Draw3.RealtimeStylus.cpp:2509` 至 `:2534` 的初始化通过 `RtsStartup::InitializeWithFallback` 先 Extended，必要时 BasicXY；`:2525` 的诊断 callback 已拿到每次尝试的 profile、FailureStage 与 HRESULT。
- `Draw3.RtsStartup.h:63` 至 `:83` 规定只在 PacketDescription、InputConfiguration 或 Enable 失败时重试 BasicXY，不能显示成所有失败都执行两次。
- 最终 RTS 失败，完整停产后 Host 才开启 Win32Mouse；要分别输出“RTS 扩展属性”“RTS 基础 X/Y”“鼠标兼容回退”“尚未启用/失败”。
- 详细 `RtsInitializationTrace` 仅在 `DRAW3_RTS_DIAGNOSTICS` 下可见（`Draw3.Diagnostics.cppm:18`），产品未启用该宏；不应为信息块打开整个调试宏或逐点 trace。`RealTimeStylusInputImpl::startupResult` 在 `RealtimeStylus.cpp:2486` 保存当前失败步骤，但尚无面向产品的只读尝试摘要 API。

## 4. 建议的画布信息输出块

在软件调测页现有调试与 EDID 信息区域增加只读文本块，标题“画布信息”。建议字段分组如下，均展示已捕获的实际值：

1. 状态：Host generation、未初始化/初始化中/就绪/失败/已停止；失败步骤与结果码（有数据时）。
2. 设备：Hardware / WARP；实际 D3D Feature Level；硬件是否请求、是否发生 WARP 回退。
3. 适配器：实际名称、Vendor/Device/SubSys/Revision、UMD driver version、显存/共享内存；查询不可用单独标注。
4. 呈现：主画布 DComp / ULW；辅助 selection-only ULW 表面可另列，不能误替换主画布模式。
5. 交换链：composition / HWND、实际宽高、格式、bufferCount、swapEffect、alphaMode、scaling、flags、waitable/ordinary。
6. 输入：RTS Extended / RTS BasicXY / Win32Mouse / Disabled，附已有初始化尝试结果摘要（若本次新增捕获该摘要）。

这是 R5 的软件调测信息，不在性能页增加用户已拒绝的“渲染状态”条目，不额外做监视器或帧率显示。

### 安全发布方案

建议增加独立的低频 `HostCanvasInfoSnapshot`（名称可按实现习惯调整）及 Host getter；用固定值、有限长度文本或按值字符串保存元数据，不暴露 COM/HWND 对象供 UI 操作。

- 每次新 Host generation 开始先清空 validity 与状态，防止旧成功信息伪装本次成功。
- 在绘制 owner 的设备与 presenter 初始化/恢复成功或失败边界，用实际 driver/adapter/GetDesc1 采集并整块发布；Resize 只刷新实际交换链尺寸/描述，不每个 UI 帧重复查询驱动。
- 输入初始化发生在 Host 的外层 owner；它只发布自己的尝试摘要及最终 inputMode，由同一快照锁合并，不在绘制锁下再调用 COM。
- UI getter 只在短锁内复制快照；所有 DXGI/COM 查询和格式化在锁外。绘制 owner 不能等待设置页或控制台 I/O。
- 若将新 snapshot 嵌入现有 RuntimeSnapshot，必须避免后者在其他产品线程的频繁调用导致每帧分配/驱动枚举；独立 getter 更适合大块低频诊断。
- 保留原控制台输出，结构化采样应与日志共用数据源或在同一采样边界实现，不能捕获 stdout/stderr 再解析文本填卡片。

## 5. 实施时有意义的验证

- 配置 defaults/缺项/类型错误/越界、复制与 roundtrip；关闭全局多指后隐藏条目但原激光多指值不丢失。
- 扩展真实生产 gate probe：first/second Touch、32 轮额外 Touch Up/Cancel 自回收、提前终态、重复 discard、同址新 generation、Pen/Mouse、非 Laser 工具与橡皮、断触 awaitingReconnect 候选、开关运行中切换和触摸手势对称清理。
- 现有 `RunIgnoredLaserTouchProductionTest` 由 `DrawingController.cpp:8681` 的生产 CPU probe链调用；仓库记录使用 `Inkeys.exe --draw3-parked-desktop-exit-test`，这条入口不创建 GUI，但实施时仍应核对调用链没有改变。
- 设备选择已有纯策略测试 `Inkeys.exe --draw3-device-policy-test`（`IdtMain.cpp:1418`）；RTS fallback 有 `InkeysHeadlessTests/draw3_rts_startup_tests.cpp:246`，可扩展摘要捕获断言。
- 快照测试必须覆盖未初始化→成功、硬件失败→WARP、DComp→ULW、waitable→ordinary、RTS→BasicXY/Mouse、元数据查询失败、停止/重启与新 generation 不带旧字段；优先注入状态发布边界，不为了设置 UI 测试启动真实窗口。
- 完整 `InkeysRepo.sln Debug|ARM64` 构建及 `InkeysHeadlessTests.exe --no-window`，按根 AGENTS 使用本机 ARM64 MSBuild 与同 invocation 的 PATH 规范化。GUI/真实多点/真实设备恢复本轮未授权，不能把静态、构建或策略测试称为硬件或视觉验收。

## 6. 改动边界与风险摘要

- 必需接线集中在 `Other.Config.cppm`、Setting/i18n、`IdtMain.cpp` 启动同步、`Draw3.Host.h/.cpp`、`Draw3.DrawingController.cppm/.cpp`；画布信息可能另需 `GraphicsInitialization`、`TransparentPresentation`、`RealtimeStylus` 的窄数据提取/发布接口。
- 不更改 shader、粒子材质/预算、模型算法、工具默认值、producer 数量、RTS fallback 顺序、DComp/ULW 候选顺序、退出监督、持久化文档结构或 UI3 的 WARP 设备。
- 最大正确性风险是被忽略 Touch 的终态回收、手势登记残留和 reconnect 资格；最大诊断风险是将配置偏好/请求描述当成实际初始化结果、恢复后继续显示旧代数据。
- 全局多指统一约束橡皮等 Touch 工具、关闭不追杀已开始 contact、隐藏激光子项不丢偏好，是最小且与现有 Laser 约定一致的设计建议；若用户提出例外，应在 PRD 中单独确定，不由底层偷偷豁免。
