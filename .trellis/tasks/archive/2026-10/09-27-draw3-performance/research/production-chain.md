# Research: 集成 Draw3 生产链路与无窗口性能切入点

- Query: 追踪当前产品输入到成功呈现、历史和 UInk 保存链，提出独立可测的 Draw3 性能候选；重点核查长笔画 protected-prefix 扫描。
- Scope: mixed（本地生产源码、Trellis 规范与 Microsoft 官方平台文档）
- Date: 2026-09-28

## Findings

### 文件与职责

| 文件 | 当前作用 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp` | 唯一产品 Host、主 Drawpad WndProc 转发。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | bridge、RTS、绘制线程、controller、presenter、保存 worker 生命周期及产品回执。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.cpp`、`Draw3.WindowControl.cpp` | UI 快照/命令与绘制线程命令 mailbox、输入准入门。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp`、`Draw3.ContactInput.cpp` | RTS packet 解码、Down/Move/Up/Cancel 发布、contact slot/队列/一致快照。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`、`Draw3.InkPrediction.cpp`、`Draw3.StrokeGeometry.cpp` | 模型输入与 prediction、L0/L1/dirty、工具和接触状态、完成笔画。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererPrimitives.cpp`、`Draw3.Renderer.cpp`、`Draw3.RendererLaser.cpp` | 点/primitive 上传、D3D 绘制、L2/L1/L0 和 Laser 资源。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp`、`Draw3.GraphicsInitialization.cpp` | DComp/ULW 选路、硬件/WARP、交换链、Present1 或 ULW 读回提交。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkDocument.cpp`、`Draw3.InkHistory.cpp`、`Draw3.InkHistoryGpu.cpp` | Stored Stroke、tile footprint、Undo/Redo、GPU preimage/cache/replay。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp`、`Draw3.PresentationAutoSave.cpp` | Desktop 与 PPT 各自的异步 UInk worker、durable 提交与 completion。 |
| `InkeysHeadlessTests/draw3_contact_tests.cpp`、`animation_tests.cpp` | 现有无 HWND 生产 ContactInput 微基准入口。 |
| `inkStrokeModelerTestTests/runtime_benchmark.cpp` | 旧 demo 的窗口和 SendInput 基准；当前无 GUI 任务不能运行，也不能代表产品链路。 |

### 生产链路、线程与可观测边界

1. `ProductHost()` 是进程内单例，`DrawpadMsgCallback` 仅在产品运行时把主窗消息转交 Host；外部两 HWND 的创建/可见性由 Window Service 拥有，Draw3 只 `AttachExternal` 主窗。证据：`Draw3.Product.cpp:10,19-29,200-205`，`Draw3.WindowControl.cpp:266-298`；规范 `draw3-integration.md:53-64`。`Host::Impl` 自有一个 bridge、RTS、`ContactInputCoordinator`、renderer、presenter、controller、`std::jthread` 和两类保存 worker（`Draw3.Host.cpp:112-142`）。Host 在绘制线程创建独立设备/呈现资源、等 RTS producer 决议、初始化 controller 并执行 `DrawingController::Run`（`:1082-1202`）；正常退出先关命令生产和 RTS，执行最终保存屏障、排空 worker 后 join（`:1320-1348`）。renderer/document/history/RTS 消费属于绘制线程，UI 只发布值快照和命令（`draw3-integration.md:61-65`）。
2. RTS `StylusDown` 锁定 context/source，解码位置、压感、倾角、方向和 QPC，并 `PublishDown`（`Draw3.RealtimeStylus.cpp:949-1003,1136-1221`）。`Packets` 当前**只取回调最后一个 packet**再 `PublishMove`（`:1318-1359`）；`StylusUp` 发布终态，坏包仍闭合 producer（`:1224-1278`）。因此 callback packet 数、已发布 Move、绘制线程实际消费的 snapshot 数必须分开；不能把 packetCount、帧数或成功 Present 数互代。此处是当前行为事实，不是授权继续丢必要转角/压感样本。
3. `ContactInputCoordinator::PublishDown` 取得带 generation 的 slot、将 Down 入 `BlockingConcurrentQueue` 并唤醒（`Draw3.ContactInput.cpp:606-637`）。Move 用 writer latch/seqlock 原子字段覆盖该 contact 的**最新** snapshot，不入队且不触发活动帧唤醒（`:241-297,640-670`）；Up/Cancel 先关 route 再写终态，避免与 Move/slot 复用竞争（`:480-510,673-703`）。`HasPendingWork` 的 queue 近似深度只代表 Down/control，不能当 Move backlog（`:811-824`）。page/PPT 准入 revision 在 Down 锁定，失效接触丢弃至真实终态（`:744-773`，`Draw3.DrawingController.cpp:3528-3567`）。
4. UI bridge 命令有 FIFO 和发布时的场景身份；绘制线程在 control wake 消费 completion、命令、最新状态并转为 canvas command（`Draw3.Bridge.cpp:119-157`，`Draw3.Host.cpp:928-1024`）。`WindowController::QueueCanvasCommand` 仅合并相邻未接受的绝对 PPT target，不跨 Clear/Undo/保存屏障（`Draw3.WindowControl.cpp:400-412`）。`processCanvasCommands` 在 `active.empty()` 才消费，保留接触边界（`Draw3.DrawingController.cpp:5548-5560`）。
5. Controller 先出队 Down、按 generation 和准入门建 `RuntimeStroke`；每帧对活动 contact 最多读取一份 latest snapshot（`Draw3.DrawingController.cpp:3528-3567,6860-6868,3256-3265`）。`updateContactModel` 调用固定库 modeler，速度橡皮有长停笔重锚边界（`:2365-2390`），位置/时间/pressure/tilt/orientation 的有效值进入 `Input`（`:3278-3355`）。随后模型 `Predict`、`RebuildPredictedPoints`、`CommitStablePrefixToL1` 和 `RebuildL0DrawPoints`；高亮笔、普通笔、橡皮、Laser 与 Shape 路径各异（`:7047-7102,7103-7253,7255-7352`）。L1 是稳定当前操作，L0 是可替换笔锋/预测，L2 是 Stored 底层；Laser 保持独立 coverage/color/粒子，永不写 L2（`:7253`，`cpu-gpu-contracts.md:202-229,283-301`）。
6. `RebuildL0DrawPoints` 从 `committedIndex` 后复制真实尾部，加 prediction、taper/tangency 并计算 dirty；高亮笔还重建 sweep primitive（`Draw3.StrokeGeometry.cpp:1257-1308`）。稳定点通过 `CommitStablePrefixToL1` 按保护窗口提交、保留连接点（`:1311-1355`）；橡皮真实点直接进入 L1（`:1358-1381`）。`InkRenderer::DrawStroke` 用动态 structured buffer `Map`/`memcpy`，更新常量缓冲并 `Draw`，分批共享端点（`Draw3.RendererPrimitives.cpp:48-102`）。高亮/Shape 使用各自上传和 draw（`:104-216`），GPU 时间需用独立 query/回读测量，CPU `Map`/`Present` 耗时不等于 GPU 完成。
7. 本帧 `frameDirty` 闭合稳定增量、旧/新 L0、Laser、粒子和 cursor，再 clamp；只在非空时由 `CompositeLayersToBackBuffer` 合成 L2、L1、L0 和瞬态层，一帧最多一次 `PresentFrame`（`Draw3.DrawingController.cpp:1493-1521,7345-7352,7710-7814`）。`PresentFrame` 包围 presenter 调用的 QPC 软件时长；Host 只在 `succeeded` 增加成功计数，回执还携带输出 target/revision/content revision（`Draw3.Host.cpp:336-383`）。这些是“API 成功返回/软件可见”的代理，不能宣称扫描输出像素或光学延迟。`RuntimeMetricsSession` 虽定义 landing/帧/Present 记录（`Draw3.RuntimeMetrics.cppm:23-54`），产品 Host 构造 controller 时没有传入该可选参数（`Draw3.Host.cpp:1161-1162`，`Draw3.DrawingController.cppm:65`）；不能把旧 demo 指标视为当前产品全链路数据。
8. 完成 Up 后 `FinalizeStoredStroke`/Shape 先追加 CPU canvas，再建 `BuildStrokeTileFootprint`、`CanvasRuntimeHistory::AppendStroke`、捕获 GPU preimage、把 Stored 笔画重画/resolve 到 L2；文档成为真值后，即使 GPU 失败也不回滚历史（`Draw3.DrawingController.cpp:7407-7555`，`Draw3.InkHistory.cpp:583-611,902-933`）。GPU history 对 tile 做有序 replay/cache（`Draw3.InkHistoryGpu.cpp:753-825,1170-1257`）。Desktop Clear/Exit 在绘制线程按可见 history 拷贝完整 CPU UInk 快照，Clear 先提交请求再清页（`Draw3.DrawingController.cpp:1656-1759,5979-6046`）；Host `ObserveDesktopAutoSave`/`ObservePresentationSave` 送 owned worker（`Draw3.Host.cpp:523-568`），workers 串行写入/加载并发 completion，最终 `CloseAndDrain` 是退出屏障（`Draw3.AutoSave.cpp:884-995`，`Draw3.PresentationAutoSave.cpp:1015-1077,1188-1196`）。快照构造仍在绘制线程，可单独测量；磁盘编码/提交是 worker 成本，须另列 durable 完成时间，不能算为一帧 Present。

### 值得独立计时的候选（尚非瓶颈结论）

| 候选与现有证据 | 无 HWND 生产代码测法 | 必须守住的行为 |
| --- | --- | --- |
| **长笔画保护前缀扫描**：`ink_prediction_detail::FindProtectedStartIndex` 从 0 开始，依次检测 `points[i+1].time < back.time - max(0,duration)`（`Draw3.StrokeGeometry.cpp:224-232`）；每个活动帧普通笔/高亮笔在 `CommitStablePrefixToL1` 调用（`:1311-1353`），Laser 的 `PlanLaserIncrementalRanges` 也调用（`Draw3.InkPrediction.cpp:155-176`）。单次 Θ(n)，若每帧增长点数则整笔可 Θ(n²)；**真实占比未测**。 | 在既有无窗测试/早退 CLI 中直接链接集成 `Draw3.StrokeGeometry.cpp`/`Draw3.InkPrediction.cpp` 的生产函数，用固定序列测 256→数万真实点、不同尾保护时长/预测回缩、每帧扫描次数与 ns/点；对照从 0 扫描结果。不可使用同名 demo 实现。 | 不能直接用 `committedIndex` 作无条件起点：prediction duration 可变化，阈值会后退。安全候选是保存上次**实际**边界和阈值；当本次阈值不小于上次且已验证前缀未改，才从边界继续，否则从 0 重扫。旧 while 是前缀条件和严格 `<`，不需要假设所有点全局按时间排序；需验证末点替换、Reset、reconnect、相等时间、非有限时间、连接点和 L1/L0 像素等价。`realPoints` 一般增量追加，但末点确有替换分支（`Draw3.StrokeGeometry.cpp:407-422,1026-1035`）。 |
| **活动几何重建**：`RebuildL0DrawPoints` 每次 clear/insert/dirty 扫尾，高亮还重建 primitives（`Draw3.StrokeGeometry.cpp:1284-1308`）；Laser 每帧调用该函数，却不走普通 L1 提交（`Draw3.DrawingController.cpp:7116-7118,7253`），可能随整条 Laser 增长。 | 用生产几何函数对短/长、慢/快、折返/停动、prediction 变化轨迹计时和分配；输出点、bounds 与当前实现逐值对比。 | 不改几何顺序、半径/压力、转角、预测回缩、dirty 覆盖；不能因优化跳过必要样本。 |
| **输入 slot 查找与并发一致性**：每 Move `FindProducing` 扫占用位图（`Draw3.ContactInput.cpp:368-387,640-660`）；现有无窗基准仅串行单 contact publish+read（`InkeysHeadlessTests/draw3_contact_tests.cpp:434-497`）。 | 复用 `--draw3-contact-benchmark`，另加真实生产 coordinator 的双线程/多 contact、不同 slot 容量、240/1000Hz 轨迹，分别计发布、读取、拒绝/争用、有效消费；原有 3 warmup/11 measured 轮仍可作单点对照。 | Down/Up/Cancel、generation、准入 revision、QPC、压力、倾角、顺序和接触隔离；现有 mailbox 会合并 Move，不能把吞吐高误报为逐样本保留。 |
| **Stored/history 长文档**：Up 时 `BuildStrokeTileFootprint` 两次建 tile 列表并追加 history（`Draw3.InkHistory.cpp:583-611,902-933`）；Undo/Redo/page switch 的 GPU cache/replay 可能随 tile/stroke 数增长（`Draw3.InkHistoryGpu.cpp:753-825`）。 | 使用生产 document/history CPU 模块构造 10/100/1000/更多笔、多页、宽/窄橡皮，测 append、footprint、Undo/Redo、可见 tile 收集、保存快照字节和内存；GPU replay 单独用无窗口 offscreen D3D 能力测。 | Canvas-local tile 真值、Stroke 顺序、擦除、redo branch、PPT `PresentationKey`/slide identity、save/restore 完全一致。 |
| **GPU 上传/合成与 ULW 输出**：`DrawStroke` 每批映射点 buffer 和 CB（`Draw3.RendererPrimitives.cpp:55-93`）；ULW 逐帧 dirty GPU staging `Map`、逐行复制、逐像素扫描 premultiplied alpha 后 `UpdateLayeredWindowIndirect`（`Draw3.TransparentPresentation.cpp:244-327`）。 | 可在无 HWND 的 D3D11 离屏纹理上单测生产 shader/primitive 上传、dirty copy/readback 和 CPU alpha 检查；将 Map、copy、scan 与 GPU query 单列。若现有 `InkRenderer::Init` 的 swapchain 依赖阻碍直接复用，须标为待建立的受限离屏夹具，不能称作 Host Present 数据。 | `BGRA8` 预乘、透明零 alpha、dirty rect、L0/L1/L2 operator、Resize/device-lost 和硬件/WARP 一致；真正 DComp/ULW 提交仍需 HWND/授权设备。 |

### 三轮以上可比协议与验证边界

1. **先冻结基线再选瓶颈**：继承 `.trellis/tasks/09-27-baseline-and-acceptance/design.md:5-8` 的 H0/环境/口径，不用 UI3 数据外推。每份原始记录写明源码/构建指纹、Debug 或 Release、ARM64、OS/补丁、CPU/GPU/驱动、FL、硬件或 WARP、presenter、DPI/分辨率/刷新率、电源、场景、轮次。当前 `--draw3-contact-benchmark` 已有生产模块与无窗边界的独立静态审查（`baseline-and-acceptance/research/draw3-benchmark-review.md:5-23`），但只覆盖单线程输入协调器，不是完整链路；`runtime_benchmark.cpp:1-240` 会启动可见 HWND 和 SendInput，本任务不运行。
2. **每个候选独立前后三轮**：固定同一 Release|ARM64 配置和轨迹，串行执行 baseline A1/A2/A3 与变更 B1/B2/B3（最好交错 A/B 以观察热/电源漂移），每轮先 warm-up，再保存逐样本原始文件与计数，不与 UI3 构建/采样并发。报告每段 median/P95/P99/max、>16.67ms 长帧比例、CPU 工作、GPU query、内存/显存/句柄、cache 命中/容量趋势与轮间噪声；收益必须超过基线波动，不能只报最优轮。保留短笔、长笔/长文档、缓慢和高速、折返、停笔/续写、抬笔、取消、切工具、多接触，以及 Pen/Highlighter/固定和速度 Eraser/Laser/开放 Shape；PPT 页切换与 Desktop/PPT UInk 各自列场景。
3. **计数与打点**：在生产入口分别记 RTS callback 原始 packet 数和解码/发布数、Down 队列等待、contact latest sequence 跨度与消费数、modeler/Predict、几何/dirty、upload/CPU draw 提交、GPU query、合成、`Present` attempt/success、Up→最终稳定、history append/cache/replay、snapshot 构造、worker durable completion。QPC 的 Down→首次**成功** Present 和 Up→最终稳定是软件代理；`Host::RuntimeSnapshot` 的 success/dirty/revision 可做可见状态回执（`Draw3.Host.cpp:336-383`），不得冒充显示器扫描或光学延迟。活动帧空闲、静态 Laser hold 必须观察零无谓 Present。
4. **语义与像素门禁**：每轮输入轨迹逐 contact 比较 Down/Move/Up/Cancel、sequence/QPC、压力/tilt/orientation、工具/尺寸、有效消费、最终 Stored 点/宽度/顺序、Undo/Redo、Clear 跨界恢复、PPT page/slide identity、UInk round-trip、worker durable 结果。对涉及 renderer/dirty 的候选，独立 offscreen D3D11 HW/WARP 回读 BGRA8 Add 与 R16 Retain；比较直线/浅斜/折返、细线、重叠、L0/L1 拆分、Shape、Laser、擦除和透明区逐像素，容差只含事先固定的 BGRA8 量化（通常 1/255）而不因失败临时放宽（`cpu-gpu-contracts.md:202-229,267-269,283-326`）。脏区外像素须不变，脏区内覆盖旧/新笔锋、cursor/粒子、prediction 回缩；记录真正 GPU readback pass 与静态断言各自范围。
5. **不可用的自动结论**：实际 RTS 驱动采样、Window Service 双 HWND、DComp/ULW 最终系统合成、真实 Pen 笔感、Win7 透明/Resize、光学延迟均超出纯无窗夹具。当前仓库规则不允许本任务启动 GUI；此研究未运行隐藏 HWND 测试，更未测真机。后续获授权的 GUI/设备验证必须与无窗口、软件 Present、像素回读分别报告，不能用一项替代另一项。

### 平台和呈现兼容约束

- `InitializeGraphicsDevice` 先试硬件，失败试 WARP；`D3D_FEATURE_LEVEL_11_1` 列表得 `E_INVALIDARG` 时重试只含 `11_0` 的列表，当前代码只接受 FL11 路径，且 device/context/DXGI factory 属于 Draw3（`Draw3.GraphicsInitialization.cpp:48-143`，`draw3-integration.md:61-65`）。Win7 SP1+KB2670838 是项目目标，不能由当前 Windows 11 ARM64 机器静态代码推断为已通过（`platform-and-resources.md:3-16,50-65`）。
- 自动 presenter 顺序仅 **DComp → ULW**；`TryInitialize` 明确拒绝历史 DWM 模式，恢复也走同一列表；辅助 presentation target 始终为 ULW（`Draw3.TransparentPresentation.cpp:90-93,644-679,820-875,904-939`，`draw3-integration.md:378-382`）。ULW 的 HWND swapchain 保留 `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`、双 BGRA8 缓冲、dirty readback、预乘 top-down DIB，未画像素 alpha=0（`Draw3.TransparentPresentation.cpp:614-625,244-327`）。不得把 FLIP 改成另一模式作为未经授权的 Win7“修复”。
- Microsoft [Platform Update for Windows 7](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7) 写明 Win7 的 D3D11.1/DXGI1.2 只有部分功能、无 DComp、普通硬件与 WARP 不超过 FL11_0；[D3D11CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice) 说明缺 11.1 runtime 时传入 11_1 会 `E_INVALIDARG`。Microsoft [DXGI_SWAP_EFFECT](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect) 文档把 FLIP_SEQUENTIAL 的 D3D11 支持写为从 Windows 8 开始；本仓库 `platform-and-resources.md:3` 则记录用户在 Win7 SP1+仅 KB2670838 的 ULW FLIP 实测。两者存在文档与项目经验的张力：保留用户已验配置，同时把具体 OS、补丁、GPU/驱动、HW/WARP、active presenter、窗口样式和场景留作真机复核，不能把任一泛化为所有 Win7 设备保证。

### 相关规范与既有研究

- `.trellis/workflow.md`：研究应落文件，实施/检查各有角色边界。
- `.trellis/spec/native-desktop/draw3-integration.md`：产品 Host/窗口/线程、PPT、Desktop/PPT UInk、presenter 与 ABI 合同。
- `.trellis/spec/native-desktop/input-and-ink.md`：产品 RTS→ContactInput→Controller 主链、Win7 输入来源与触控/笔边界。
- `.trellis/spec/native/platform-and-resources.md`：Win7 目标与实际验证须分开；其中部分独立 demo 描述不覆盖当前产品首发选路。
- `.trellis/spec/native/runtime-and-rendering.md`：低层绘制/输入、wake 和工具行为；开头 demo 生命周期以产品 `draw3-integration.md` 为准。
- `.trellis/spec/shaders/cpu-gpu-contracts.md`：结构布局、operator 数学、dirty/AA、Laser 多 contact、像素回读约束。
- `.trellis/tasks/09-27-baseline-and-acceptance/design.md`、`research/draw3-benchmark-review.md`：冻结的测量分界、现有生产 ContactInput 无窗微基准范围。

## Caveats / Not Found

- 本次是静态研究：未修改生产代码/测试，未运行构建、性能采样、D3D 离屏、GUI 或 Win7 真机；没有实测成本分布、瓶颈归因和提升百分比。现有 `RuntimeMetricsSession` 没接产品 Host，`QueryVideoMemoryUsageMiB` 有定义（`Draw3.Renderer.cpp:136-151`），未见其被产品 Host 调用形成长文档时间序列。
- `InkeysHeadlessTests.vcxproj:126-133` 编入的是生产 Bridge/Presentation/ContactInput/PenCursor；`inkStrokeModelerTestTests.vcxproj:127-209` 的多数同名几何、renderer、history 和 prediction 实现仍来自独立 `inkStrokeModelerTest/draw3`，但 AutoSave/PresentationAutoSave 是生产文件（`:118-125`）。新增无窗几何/历史基准必须证明链接到 `Inkeys/Inkeys/Drawing/Draw3`，不可把 demo benchmark 结果移作集成 Host 证据。
- `FindProtectedStartIndex` 的 Θ(n²) 是长期增长轨迹下的算法上界，不等于当前设备上已确认热点。带 hint 的等价证明依赖上次已扫描前缀不被修改；末点替换、重连/Reset 与动态保护时长都必须走参考扫描对照。Laser 与普通笔复用同一 helper，但状态游标不同，不能一起无证改动。
- 无 HWND 离屏夹具可检验 CPU 模型/几何和部分 D3D shader/operator；实际 `InkRenderer::Init` 当前依赖 swapchain（`Draw3.TransparentPresentation.cpp:697`），是否能最小化接入完全生产 GPU pass 须先审接口。任何夹具数据都不等于 `Present1`/`UpdateLayeredWindowIndirect` 系统提交或眼见像素时延。
