# Research: 生产架构、状态入口与验证边界

- Query: 当前生产 UI3、Draw3、状态统一、崩溃/重启/恢复的入口与调用链是什么；现有诊断、测试和构建能证明到哪一步？
- Scope: internal
- Date: 2026-09-27

## Findings

### 文件与职责

| 文件 | 当前职责 |
| --- | --- |
| `Inkeys/IdtMain.cpp` | `wWinMain`、启动配置、Window Service/UI3/Draw3 初始化、退出清理与主动重启。 |
| `Inkeys/IdtState.h/.cpp` | 顶层工具状态、笔宽/颜色、Draw3 状态快照、窗口呈现调和、模式 revision。 |
| `Inkeys/IdtPlug-in.cpp` | PPT COM 会话消费、目标页/ready 门禁、批注接管和退出条件切换。 |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm/.cpp` | UI3 共享 D3D11 WARP epoch、固定客户端顺序的单调度线程、诊断采样与设备恢复。 |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.Diagnostics.h` | UI3 生产日志共用的固定大小数值聚合。 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | Bar 快照、动画/layout、光影、dirty、D2D/GDI/ULW 成功提交及回退；现为超大实现文件。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp`、`Bar.UI.cpp` | 光影 mask/geometry/brush 缓存及 SVG 解析、栅格化、位图上传。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp`、`Draw3.Host.cpp` | 唯一产品 Host、bridge、独立 RTS/绘制线程、自动保存 worker 和运行快照。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp`、`Draw3.ContactInput.cpp` | RTS packet 解码、接触路由、Down/Move/Up/Cancel 发布。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`、`Draw3.Renderer*.cpp` | 生产 modeler/prediction、几何、三层画布、GPU 数据更新、合成与 Present。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` | DComp/DWM/ULW 选择，主/辅助呈现与成功观察值。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp`、`Draw3.PresentationAutoSave.cpp` | Desktop/PPT UInk、索引和串行异步保存/加载。 |
| `Inkeys/Inkeys/Helper/Helper.CrashHandler.cppm/.cpp` | Close/Restart 信号、顶层未处理异常过滤器、dump/report、崩溃重启请求。 |
| `InkeysHeadlessTests/animation_tests.cpp`、`render_scheduler_tests.cpp` | `--no-window` 纯逻辑入口和实际 Scheduler/诊断合同测试。 |
| `inkStrokeModelerTestTests/contact_input_tests.cpp`、`PptCOM.Tests/Program.cs` | 独立 Draw3 demo 与共享 UInk/保存代码测试、PPT fake COM 合同测试。 |
| `InkeysRepo.sln`、`Inkeys/Inkeys.vcxproj`、`PptCOM/PptCOM.csproj` | 主产品与无窗口测试、三架构配置、PptCOM DLL/TLB、Draw3 shader 资源链。 |

### Work 1：状态写入者与规范入口

1. `StateModeClass` 的 `StateModeSelect`、`StateModeSelectTarget`、`StateModeSelectEcho` 是不同字段，另有 `laserActive`、笔型/形状/宽/色记忆；目前都是普通字段，见 `Inkeys/IdtState.h:38-114`。`ChangeStateModeToSelection/Pen/Shape/Eraser` 在 `stateModeTransitionMutex` 内同步三态、更新对比色并增加 revision，离锁后 `SyncDraw3State()`；条件版选择和 PPT 批注入口在同锁校验 expected revision，见 `Inkeys/IdtState.cpp:846-950`。不能把迟到 PPT 结果改成无条件 setter。
2. `SyncDraw3State()` 先发布 Setting owner 期望值、锁外应用，再发布 Draw3 快照并调和表面；`PublishDraw3State()` 在模式锁内序列化工具/宽/色/selection/auto-save 到 bridge，见 `Inkeys/IdtState.cpp:169-182,647-664`。`Draw3.Bridge.h:192-205,227-241` 把产品快照、workspace、PPT target 和 FIFO 命令分开；`Draw3.Product.cpp:85-99` 合并状态。这些分别是 authoritative/requested/applied 候选，需逐字段确认再迁移。
3. 主栏四个顶层工具按钮走 `ChangeStateModeTo*`，见 `Bar.Button.cpp:261-265,297-304,354-361,400-406`；颜色/粗细走 `SetPenColor/SetPenWidth`，见 `Bar.Interaction.cpp:1558,1985`。PPT 原生批注退出成功才条件切模式；真实放映退出保存进入时的 revision，之后 `ChangeStateModeToSelectionIfRevision` 防旧退出覆盖用户新工具，见 `IdtPlug-in.cpp:497-505,660-664,797-800,867-875`。
4. 仍有窄范围旁路写入：形状子型 `Bar.Interaction.cpp:3500-3502` 直接赋值后 `SyncDraw3State`；Laser/硬笔/软笔/荧光笔在 `:5229,5272-5275,5319-5322,5365-5368` 先改 `laserActive`/`Pen.ModeSelect` 再切 Pen。它们未必行为错误，但子型与副作用不是同一个事务，需要用实际线程、快捷键、PPT、设置恢复和重复点击构建迁移表。
5. `IdtState.h:65-112` 为普通非 atomic 状态；模式入口仅写侧加锁，UI3 Bar 渲染与按钮样式在 `Bar.RenderLoop.cpp:635-676`、`Bar.Button.cpp:847-929` 直接读取；Scheduler 实际启动独立 `std::jthread`，见 `RenderPipeline.cpp:408-440,575-614`。**跨线程读写是高优先静态并发嫌疑，尚未做线程时序证明或运行复现**。审查者应先确认调用线程、已有消息/锁协议，不以把裸写包成 setter 作为完成。

### Work 2：UI3 呈现与性能诊断

1. `IdtMain.cpp:1591-1594,1832-1835,2055-2057` 建立共享 UI3 管线/Bar HWND 和 Bar 初始化；`RenderPipeline.cpp:752-792` 创建 D2D/DWrite、默认 WARP device epoch，随后启动 Scheduler。`RenderPipeline.cpp:53-62,440-448,493-575` 固定顺序串行回调，idle 使用 event 等待，活动帧按约 16.67 ms 截止时间运行；Settings 慢回调能延后 Bar，不能看到 Bar 间隔长就归因 Bar 自己。Draw3 不共享这套设备或线程，见 `draw3-integration.md:54-65`。
2. Bar `RenderFrame` 在 `Bar.RenderLoop.cpp:12983-13244` 做 wake/snapshot、失败退避、动画/layout、lighting、dirty/present，返回 Idle/Retry/DeviceLost/Continue。计时点位于 `:9865-9866` 的 Draw、`:12551` 的 present 锁、`:12607-12649` 的 GetDC/ULW/ReleaseDC/EndDraw；成功事务在 `:12678-12705` 才提交 dirty 和呈现快照。`GetDC` 可能包含此前 D2D flush 等待，阶段耗时不可机械当作纯 API 拷贝成本。
3. `FrameAnimationClock::Tick` 使用 steady clock 并把活动 dt clamp 到 50 ms，见 `Bar.FramePacing.cppm:89-124`。当前 `WakeAndSnapshot` 在 `Bar.RenderLoop.cpp:981-985` Tick，`CalculateDirtyAndDrawPresent` 仅在准备返回 Idle 前 Rebase（`:12925-12930`）；失败退避门在动画推进前返回 Retry（`:13026-13030`）。这与 `rendering-and-ui.md:74-99` 所述“真正 idle 唤醒后 rebase”合同存在可核对差异，也是 09-22 调查 F4 的历史风险；是否修改时间语义须用确定性注入时钟和真实轨迹验证，不能直接删除 clamp。
4. 生产 `[UI3Diag]` sink 由 `IdtMain.cpp:1152-1164` 安装，复用既有日志线程池、`discard_new`；聚合器默认健康静默，异常/长帧阈值 50 ms，见 `RenderPipeline.Diagnostics.h:10-12,92-125`。`FrameDiagnostics` 分开记录回调、动画推进、GetDC/ULW 尝试、成功提交，见 `RenderPipeline.cppm:88-135` 与 `RenderPipeline.Diagnostics.h:116-125`；合法 Retry 不能独立算失败。`render_scheduler_tests.cpp:619-1017` 覆盖实际聚合器及 Scheduler 的限频、恢复、sink/TLS/实例隔离。
5. 现有日志有累计/max/失败快照，但不是逐场景完整原始帧序列；不足以直接计算三轮 median/P95/P99、真实动画完成时间或 GPU 光学延迟。Work 0/2 要独立保存固定样本或从可比生产场景收集原始时间戳，记录 warm-up、环境、有效成功提交和 sample count。`ui3-render-diagnostics.md:25-64` 明确健康静默及“无窗口验证不等于真实 GPU/ULW”。
6. 光影已有数量有界的 gradient/parent mask/geometry mask 缓存与设备失效清理，见 `Bar.Rendering.cpp:167-216,966-1007,1469-1647,2066-2243`；exact/fallback 与实际 `FillOpacityMask` 次数需分开，见 `:1764-1978,2010-2017`。09-22 研究表明 parent 命中仍可带来大量分片提交；生产新诊断已保留 `maskDraws` 等字段。静态查缓存数量不能证明 GPU 成本，也不能把缓存命中当作不绘制。
7. SVG `Bar.UI.cpp:400-489` 在 cache miss 时替换颜色、重新 `lunasvg::Document::loadFromData`、`renderToBitmap`、`ID2D1DeviceContext::CreateBitmap`；`Bar.Rendering.cpp:2723-2786` 用颜色、基础尺寸、动画放大阈值决定是否重建。内容切换也在 `Bar.UI.cpp:332-368` 做解析/代次校验，设备 epoch 清理在 `Bar.Rendering.cpp:167-175`。应分别计 parse/raster/upload/draw 和每图缓存占用，比较第一次交互与稳态；`tarW/tarH` 转 int 前的有限数/范围需输入来源审计，当前尚不是已复现漏洞。

### Work 3：Draw3 输入至成功 Present、保存

1. `IdtMain.cpp:1725-1835,1895-1966` 由 Window Service 创建主/辅助 HWND，再以一个 `Draw3::StartProduct` 附着 Host；DComp 启动失败需首帧前停止并重建 legacy HWND 链，`IdtMain.cpp:1945-1966`。唯一 Host 和生产 facade 在 `Draw3.Product.cpp:19-55`、`IdtDrawpadFacade.cpp:57-60`。`Draw3.Host.cpp:83-100,1101-1135` 持有独立 input、renderer、presenter、保存 worker 和绘制线程，RTS 只在图形 ready 后启用。不得让 UI3/Draw3 共用 device/线程。
2. RTS `Packets` 在 `Draw3.RealtimeStylus.cpp:1318-1359` 对一次回调只解码最后一个 packet；`ContactInputCoordinator::PublishMove` 在 `Draw3.ContactInput.cpp:640-670` 覆盖当前 contact 的最新快照，写争用可能不发布；Down/Up/Cancelled 分开可靠发布，见 `Draw3.ContactInput.cppm:185-195`。这是**当前已有输入语义与质量风险**，不应在性能优化中进一步减少样本；必须用同轨迹的慢笔、快速折返、角点、压感、Up/Cancel、跨页接触与 H2 对比。是否已造成可见退化尚未验证。
3. `DrawingController::Run` 在 `Draw3.DrawingController.cpp:1576`，活跃时 drain 命令、取每 contact 快照、modeler/prediction、合并 dirty、更新 L0/L1/L2/GPU 再至多一次 backbuffer 合成/Present，见 `:6506-6512,6857-6863,7738-7815`。GPU 热点可在 `Draw3.RendererPrimitives.cpp:67-142` 的 Map/Draw、`Draw3.Renderer.cpp:52-118` 的 copy/合成和 `Draw3.TransparentPresentation.cpp:897-932` 的呈现处分段计时；不要从单个总帧耗时推断瓶颈。
4. `DrawingController::PresentFrame` 在 `Draw3.DrawingController.cpp:1505-1519` 记录 `presentation_.Present` 结果；`RuntimeMetricsSession::StageLanding/CommitStagedLandings` 只在成功 Present 后确认软件口径可见，见 `Draw3.RuntimeMetrics.cpp:202-234` 与 `DrawingController.cpp:7738-7847`。但生产 `Host.cpp:1129-1130` 创建 controller 时未传 metrics 参数，故该会话目前未为产品启用；demo 指标不能冒充集成产品指标。软件成功 Present 仍非光学端到端。
5. ULW 主 fallback 或 selection 辅助输出在 `Draw3.TransparentPresentation.cpp:246-329,897-932` 执行 GPU→staging copy、Map/逐行 CPU copy、**逐 dirty 像素 alpha/premultiplication 校验**，再 `UpdateLayeredWindowIndirect`。这段 O(dirty pixels) 是可测生产静态热点候选，需单独保留像素正确性回归；未实测前不宣称优化收益。GPU 路径 `Present1` 成功也只证明 API 提交，不等于光学显示。
6. `Host.cpp:938-986` 消费 bridge 命令/保存完成；正常退出在 `Host.cpp:1279-1318` 请求 `PrepareExitAutoSave` 屏障，等待 controller 捕获并 drain worker。`Draw3.DrawingController.cpp:1689-1764,6031-6035` 的退出/clear 快照由 Host worker 编码落盘。PPT index 写入使用 flush+`ReplaceFileW`/`MoveFileExW`，见 `Draw3.PresentationAutoSave.cpp:259,410-430`；仍需磁盘满/权限/中断故障测试，不能仅凭函数存在推断 durable 恢复通过。
7. `Draw3.Bridge.cpp:122-127` 对 Save/SuperRecovery/AutoStraighten/InputTest 等显式返回 Unsupported；`draw3-integration.md:76-78` 说明 Whiteboard 自动保存和跨进程 PPT 恢复等入口暂未开放。验收不要把 gate 当缺陷。

### Work 4：退出、崩溃、重启和恢复的实际链

1. 正常关闭/主动重启入口 `Helper.CrashHandler.cppm:56-71` 都先隐藏窗口、卸载 crash filter，再发布 `offSignal=1/2`。`IdtMain.cpp:273-280` 同步唤醒 UI3；主线程在 `:2082-2139` 等待，`:2141-2158` 按 Setting/UI3/Draw3/Window Service 顺序 stop/join，`:2174-2191` 清 COM/互斥体后只对 `offSignal==2` 用当前 exe 路径 `ShellExecuteW(..., "-Restart")`。启动参数识别和 single-instance gate 在 `IdtMain.cpp:397-451`，带 `-Restart/-CrashTry` 会跳过普通 release mutex 检查；需要真实隔离进程验收旧/新实例交接。
2. 未处理异常由 `CrashHandler::Initialize` 的 `SetUnhandledExceptionFilter` 安装，`Helper.CrashHandler.cpp:30-35,331-339` 有第二次崩溃/重入门禁，`:362-439` 试写 dump/report，`:441-484` 按安全模式状态显示提示或发起 `-CrashTry`。`GetExeDirectory()` **实际返回完整 exe 路径**，见 `:57-69`；函数名/注释失准，`ShellExecuteW` 在 `:474-476` 接收到的是 exe 而非目录，不能误报成启动目录错误。`IdtMain.cpp:399,451` 在新进程把 `-CrashTry` 映为 `IsSecond(true)`，防再次自动拉起。是否能覆盖工作线程故障、二次异常、初始化中途失败仍需受控真实注入。
3. Crash handler 用 filesystem、i18n try-read、MessageBox、ShellExecute 和 `MiniDumpWithPrivateReadWriteMemory`，见 `Helper.CrashHandler.cpp:345-476,518-525`。在损坏堆/锁/低内存状态下的可靠性和 dump 隐私是审查点；静态调用链不能证明 crash handler 可重入安全或自动重启成功。`-CrashTry` 只说明重启标记，不说明数据恢复。
4. 当前生产 `DesktopAutoSaveService::SubmitLoad` 调用点是 `Draw3.Host.cpp:516`，被 `DrawingController.cpp:6074-6080` 用于**当前进程内 Clear 撤销**；`IdtMain.cpp:1907-1910` 仅把 auto-save root 传 Host，未找到启动时从 Desktop index 加载旧画布的生产调用。`draw3-integration.md:95-103` 将 Desktop 自动保存描述为 Clear/正常退出恢复点，`draw3-integration.md:76-78` 明确跨进程 PPT 恢复尚未开放。因此要把“已提交 UInk/索引仍有效”“新进程自动/手动恢复可见”分开；后者当前至少是**未验证且可能不属已开放能力**，不能因为新进程启动就写 PASS。

### Win7 SP1 + KB2670838：设备与呈现补充调查

1. Draw3 `InitializeGraphicsDevice` 只请求 FL11.1/11.0，包含 11.1 的首轮创建返回 `E_INVALIDARG` 时改用仅 FL11.0 的列表重试；Hardware `D3D11CreateDevice` 最终失败才切 WARP，见 `Draw3.GraphicsInitialization.cpp:49-99`。之后必须取得 `IDXGIDevice1`、adapter 和同源 `IDXGIFactory2`，任何一步失败则整个初始化失败，**不会再因后续 DXGI 查询失败重新尝试 WARP**，见 `:107-133`。这不等于确认 Win7 会失败；需分别记录 Hardware FL、WARP FL、适配器、每步 HRESULT 与最终真实 backend。UI3 是另一路：默认 WARP，FL11.1/11.0 首试、`E_INVALIDARG` 后 FL11.0 重试，`ID3D11Device1` 是可选快照，见 `RenderPipeline.cpp:186-235,752-792`。两路设备不共用。
   UI3 的 Bar/PageControl 使用 D2D/GDI/ULW；Setting 借 UI3 epoch 但独占传统 `CreateSwapChain`/`DXGI_SWAP_EFFECT_DISCARD`，见 `Setting.Base.cppm:65-77`，`Setting.cpp:8311-8317`。因此保留 Draw3 FLIP 的决定不要求把 UI3 Setting 一起迁为 FLIP。
2. `ShouldPreconfigureNoRedirectionBitmap` 以从 System32 加载 `dcomp.dll` 并寻找 `DCompositionCreateDevice` 为条件，见 `Draw3.TransparentPresentation.cpp:43-57,111-118,470-472`；微软 [Win7 Platform Update](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7) 将 DComp 列为不可用，DXGI 1.2 列为部分可用。产品 `IdtMain.cpp:1725-1728,1780-1783,1907-1909` 据能力预置 HWND 样式，再以该布尔值控制 Host。若首轮 DComp Host 启动失败，`IdtMain.cpp:1945-1966` 在显示前停止 Host、销毁旧窗口链，创建 legacy-compatible HWND 后以 `allowDirectComposition=false` 重试。
3. **当前自动 presenter 仍不符合本次 Win7 产品决定。** `kTransparentPresentModes` 是 DComp→`DwmBlurBehind2`→`DwmBlurBehind`→ULW，见 `Draw3.TransparentPresentation.cpp:90-95`。`Initialize` 在 `allowDirectComposition=false` 时从索引 1 开始，`RecoverFromRuntimeFailure` 同样用索引 1，见 `:750-789,813-869`；因此 Win7 会先尝试两种现已明确禁用的 DWM 透明方案。`requireMode` 走独立路径，见 `:770-775,842-853`，不经过自动候选过滤。最小实现应**在生产选路及恢复重试中同时跳过 Win7 的两种 DWM 模式，并拒绝 Win7 强制 DWM 模式**；Win8+ 路径是否保留按现有行为，避免无关改动。检测版本优先复用 `IdtStart.cpp:80-105` 的现有 `GetWindowsVersion()` 结果，经窄的 Host/presenter policy 传递；不要在 Draw3 内复制另一套 OS 检测，也不要以 `dcomp.dll` 不可用单独判定系统就是 Win7。
4. 每次尝试前 `ReleaseAttempt()` 清理 presenter、renderer 与 swapchain，见 `Draw3.TransparentPresentation.cpp:499-530,670-704`；waitable swapchain 创建失败、`IDXGISwapChain2` QI 或 SetMaximumFrameLatency 失败时落到普通 swapchain，见 `:574-643`。当前所有模式，包括 ULW，使用 BGRA8、双缓冲、`DXGI_SCALING_STRETCH`、`DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`；仅 GPU 模式使用 premultiplied DXGI alpha，ULW 用 unspecified，见 `:616-628`。`CreateSwapChainForHwnd` 失败的 HRESULT 由 `:632-636` 记录；若 Win7 两 DWM 被跳过且 ULW 也失败，Host 应报告失败，不能假称已透明呈现。`Draw3.Renderer.cpp:261-266,376-394` 依赖 `GetBuffer(0)`/`ResizeBuffers`；ULW 仍需要这张 GPU backbuffer，然后读回并以 `UpdateLayeredWindowIndirect` 提交（`Draw3.TransparentPresentation.cpp:246-329,897-932`）。
5. **保留当前 FLIP，不新增 bitblt/swap effect 回退。** 用户已明确 Win7 SP1 + 仅 KB2670838 上 `FLIP_SEQUENTIAL` 实测可用；此任务应把它作为已观测事实，不因 [微软 DXGI_SWAP_EFFECT 文档](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect) 的“Windows 8 起支持”通用表述而改动生产路径。两者存在资料与现场冲突，建议记录实测 Windows build/补丁清单、DXGI runtime、CPU/GPU/驱动、Hardware 或 WARP、`DXGI_SWAP_CHAIN_DESC1`、waitable/ordinary、`CreateSwapChainForHwnd` 与首帧/resize HRESULT、真实 `ActiveMode` 和透明像素结果；仅用户当前设备的实测不能自动外推所有 Win7 驱动。
6. 无窗口 `InkeysHeadlessTests/render_scheduler_tests.cpp:607-609` 只验证 UI3 初始化所得 epoch 为 WARP 且 FL≥11.0，不能替代 Win7 真机。Draw3 `HiddenWindowTest.cpp:2126-2181` 有 DComp、automatic、强制 DWM2/DWM/ULW 及旧 HWND 重建和像素/dirty 验证；它创建 HWND，当前研究未运行，且 Win7 两 DWM 模式禁用后强制模式测试必须在该系统检查“被拒绝/跳过”而非仍期待启动。应增加生产候选策略的纯函数无窗口测试，分别覆盖 Win7 automatic/recovery/requireMode 跳过、Win8+ 保留、DComp 不可用、ULW 失败；真实 Win7 x86/x64 分别验证 Hardware FL11.0、Hardware 失败后的 WARP FL11.0、DComp 不尝试、两个 DWM 不尝试、ULW 首帧/脏区/resize/device-lost/退出，且单列用户已有 FLIP 实测记录与本次复验记录。
7. 规范需随实施复核：`.trellis/spec/native-desktop/draw3-integration.md:74,378-381` 仍把 legacy 回退写成 DWM2→DWM→ULW；`.trellis/spec/native/platform-and-resources.md:150-169` 的候选链只有一个 DWM、又称 OS 标签不改变候选顺序，已与当前代码及用户 Win7 决定不符。研究 agent 不修改 spec；主会话应在代码合同确定后用 Trellis spec 更新流程修订，保留 Win7 FLIP 实测与文档冲突说明。

### 最小验证方案与证据口径

| 验证层 | 可用入口 | 证明范围 / 不可外推项 |
| --- | --- | --- |
| 主工程构建 | `InkeysRepo.sln Debug|ARM64`，再按改动范围验证 Release/三架构；需原生 ARM64 MSBuild、同一 PowerShell invocation 先清理重复 `Env:PATH` 并禁 node reuse、完整构建至少 5 分钟超时。 | `InkeysRepo.sln:6-15,27-79` 含 Inkeys、PptCOM、PptCOM.Tests、InkeysHeadlessTests；`PptCOM.csproj:63-67` 生成/复制 DLL/TLB。退出码及 shader/resource 输出必须单独记录。 |
| UI/状态无窗口逻辑 | `InkeysHeadlessTests.exe --no-window`。入口在 `animation_tests.cpp:1533-1590`；`render_scheduler_tests.cpp:619-1017` 测真实聚合器/调度器；`dirty_region_tests.cpp`、`frame_pacing_tests.cpp`、`present_decision_tests.cpp` 等为专项。 | 不建立真实 Bar/Office/ULW 体验证据；`--no-window` 跳过 `RunWindowTests` 和 MessageBox 窗口分支。业务入口迁移要让测试触及被修改实现。 |
| Draw3 算法/持久化 | `inkStrokeModelerTestTests.exe`（`contact_input_tests.cpp:2997-3055` 有 `--uink-presentation-only`、`--laser-incremental-only`、`--drawing-perf` 等）；共享生产 `Draw3.AutoSave/PresentationAutoSave/Presentation` 代码被直接编译。 | 该工程仍大量编译 `inkStrokeModelerTest/draw3/*` demo 源，见其 vcxproj:127-148；不能把旧 demo `--benchmark/--drawing-perf` 结果标为集成 Host 成本。主产品 Draw3 还需独立 headless/隔离进程计量。 |
| PPT 托管合同 | `PptCOM.Tests` 的 fake COM/生命周期/descriptor，见 `Program.cs:1-328` 和 `PptCOM.Tests.csproj:24-30`。 | 不代表真实 PowerPoint/WPS、位数、Office busy 或 UI 页回执。 |
| 真实窗口/重启 | `IdtMain.cpp:295-307` 有 offscreen/hidden 参数，09-22 文档也有故障日志入口。 | 本次研究未运行；按用户 AGENTS 默认限制，未获 GUI/异常注入明确授权前仅静态及严格 no-window。隐藏 HWND 测试也不是纯 no-window；需隔离数据/进程，不碰用户实例。 |

建议的最小先后顺序：先在 H0 固定环境与相同输入轨迹下采 UI3/Draw3 **分别**的 baseline，明确成功 Present 与 callback/采样率差别；Work 1 每次入口迁移做等价/迟到回调测试并检查线程；Work 2 在相同 WARP/效果/Release 场景收集三轮活动帧与阶段 raw samples（median/P95/P99 仅样本足够才报告）；Work 3 增加生产 Host 的受控真实模块采样，另量输入队列、modeler、GPU map/draw/present、持久化；Work 4 分别验正常退出、主动重启、支持的未处理异常、loop suppression、提交文件完整和恢复可见。无 GUI 时保留 MANUAL/NOT VERIFIED，不用旧报告 PASS 充当新 HF PASS。

### 相关规范、历史与外部参考

- 当前更直接的生产合同：`.trellis/spec/native-desktop/index.md`、`rendering-and-ui.md`（UI3 时钟/调度/dirty/缓存）、`ui3-render-diagnostics.md`、`draw3-integration.md`、`input-and-ink.md`、`errors-logging-and-resources.md`、`build-and-compatibility.md`、`draw3-shaders.md`，以及 `.trellis/spec/ppt-interop/native-session-ui3.md`。
- CPU/GPU 镜像和 Win7 仅 SP1+KB2670838 约束见 `.trellis/spec/shaders/cpu-gpu-contracts.md`、`.trellis/spec/native/quality-and-validation.md`；后者的构建入口是**独立 demo** `inkStrokeModelerTest.sln`，不能覆盖主产品 `InkeysRepo.sln`。
- `.trellis/tasks/archive/2026-08/08-09-ui3-animation-performance-audit/prd.md` 的背景仍写 Draw2 过渡及 `IdtD2DPreparation/IdtDrawpad.cpp`，属于当时版本，不是现在的生产路径。`.trellis/tasks/09-22-ui3-animation-stutter-investigation/findings.md` 标题仍称等待批准，但同任务 `verification/first-batch-result.md` 记录 S1-S4 在 2026-09-23 已实施且当时构建/headless 通过；偶发卡顿现场未验证，必须重验当前 HF。
- Win7 专题核对了微软官方 [Platform Update for Windows 7](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7)、[DXGI_SWAP_EFFECT](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect) 和 [CreateSwapChainForHwnd](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd) 文档；关于 FLIP 的通用版本文字与用户实测相冲突，已按用户事实记录，未把文档推断升格为产品故障。未核验外部安全公告。代码工程声明 VS `v143`、Windows SDK `10.0.26100.0`（`Inkeys.vcxproj:30-76`），PptCOM 为 .NET Framework 4.0（`PptCOM.csproj:8-15`）；这些配置事实不等于当前可用工具链或所有机器兼容的实测证明。

## Caveats / Not Found

- 只做源代码/规范静态调查；未运行编译、无窗口测试、基准、GUI、Office、崩溃注入或 Git 操作。以上代码点是调查入口与假设，不构成性能通过或发布就绪结论。
- 未找到生产 Host 已启用 `RuntimeMetricsSession`、Desktop 在新进程自动装载旧 UInk 的入口、或现有 UI3 诊断提供原始多轮 median/P95/P99 的导出。搜索范围以列出的产品入口为主；若其他进程/外部启动器负责恢复，需由独立链路审查补证。
- 未判断目标 Canary/H2 版本、此次 Win7 实测的完整设备/驱动与 DXGI 元组、用户机器故障因果或三架构/Win7/Office 普遍兼容；不能把历史 demo 或 Debug 结果当作生产 Release 同机对照。Win7 FLIP 已有用户实测，但本轮未亲自复验。
- 状态跨线程竞态、SVG 数值边界、UI3 idle 时钟、Draw3 packet 最新值语义及 ULW 全像素校验均需按本任务 finding 流程进一步证明触发条件、影响和最小修复；本文件不将静态风险写成已复现缺陷。
