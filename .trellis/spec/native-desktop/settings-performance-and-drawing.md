# 性能设置与每笔绘图偏好

## 1. Scope / Trigger

修改性能页、通用绘图、激光设置、共享界面节拍、Draw3 启动帧率或画布初始化信息时应用。产品是当前 UI3 / Draw3，不能恢复传统 deploy.json 已退役的 Performance 容器。

## 2. Signatures

- `RenderPipeline::Scheduler::SetFrameRate(int)` 与进程级 `RenderPipeline::SetFrameRate(int) noexcept`：共享界面帧率。
- `Host::SetGeneralDrawingOptions(bool multiTouch, bool tailCorrection, int predictionMode) noexcept`：传统头不前置声明 module 所属枚举；Controller 内部接口使用 `InkPredictionMode`。
- `Host::SetLaserOptions(bool multiTouch, bool particles) noexcept`：多指偏好受 Host 的全局多指约束；粒子参数保留兼容，但由 Controller 强制关闭。
- `CreateStrokeModelConfiguration(int dpiX, StrokeTimingProfileId profile = Fps120)`：整套画布模型参数。
- `Host::CanvasInformation() const -> std::string`：复制 owner 已发布的 UTF-8 初始化文本，不在 UI 线程访问 COM。

## 3. Contracts

以下字段均位于 Inkeys/Config/main.json，新字段不上传：

| 路径 | 默认与合法值 | 生效边界 |
| --- | --- | --- |
| UI.FrameRate | 60；30/60 | 共享界面后续批次，设置/主栏/翻页同一节拍 |
| Drawing.FrameRate | 120；30/60/120/240 | 下次启动，完整 profile，不仅改等待时间 |
| Drawing.MultiTouchEnabled | true | 多指绘图总开关：新独立 Touch 绘图准入 |
| Drawing.MultiTouchTools.{Pen,Highlighter,Eraser,Shape} | 全 true | 多指绘图按工具子项（2026-11-20），总开关开启时按批次工具判定新独立 Touch；Pen 含 HardPen，Shape 含旧直线/矩形与一笔画通用图形；需要两步及以上的构造图形一律单指 |
| Drawing.PenTailCorrectionEnabled | true | 新笔锁存，完成态拟合读取该笔值 |
| Drawing.InkPredictionMode | 2；0/1/2 | 新笔锁存，0=Disabled、1=StrokeEnd、2=Kalman |
| Drawing.Laser.MultiTouchEnabled | false | 多指绘图的“激光笔”子项：后续新 Laser Touch，受全局多指约束 |
| Drawing.Laser.ParticlesEnabled | false，兼容字段 | 产品忽略启用值，构造与setter均强制关闭 |
| Experimental.Inkeys3.Draw3.PresentationMode | 0；自动/DComp/Win7Dwm/ULW = 0/1/2/3 | 下次启动；缺少新键时将旧 EnableDirectComposition 的 false 映射为 ULW，true 映射为自动 |
| Experimental.Inkeys3.Draw3.RenderAcceleration | 0；自动/硬件加速/软件加速 = 0/1/2 | 下次启动；自动 = 硬件优先、失败回退 WARP；硬件加速 = 只用硬件，不回退；软件加速 = WARP。设备丢失恢复沿用同一策略。缺少新键时旧 EnableHardwareAcceleration=false 迁移为软件加速，true 或缺键为自动 |
| Experimental.Inkeys3.Draw3.AdapterPreference | 0；自动/优先核显/优先独显 = 0/1/2，越界或非整数按 0 | 下次启动；只决定硬件尝试顺序（软件加速档不读取），设备丢失恢复沿用同一快照。自动 = 按 DXGI 枚举顺序（0 号为主显示器适配器）；优先核显/独显 = 先按枚举顺序尝试该类全部适配器，再尝试另一类；第一个能建出 11_0 设备的即采用，全部失败再按 RenderAcceleration 决定是否回退 WARP。软件适配器（`DXGI_ADAPTER_FLAG_SOFTWARE` 或 0x1414/0x8C）不参与硬件尝试。分类：Win8+ 读 D3DKMT 混合显卡标志（HybridIntegrated/HybridDiscrete），没有时按独占显存 ≤ 512 MiB 判为核显（2026-11-21） |
| Experimental.Inkeys3.Draw3.EnableWaitableWait | true | 下次启动（`configOnce` 快照）；关闭时 DComp 改建普通 FLIP 链（2026-11-20 阶段 A 起，见 `draw3-integration.md`） |
| Experimental.Inkeys3.Draw3.GpuSchedulingPriority | 1；标准/均衡/高性能 = 0/1/2，越界按 1 | 即时：设置页写配置后调用 `Inkeys::Helper::ApplyProcessScheduling`；启动时在完整配置读取后、Draw3 Host 前应用一次。映射 D3DKMT 调度类 NORMAL=2 / HIGH=4 / REALTIME=5；请求失败（NTSTATUS < 0）逐级回退：高性能 REALTIME→HIGH→NORMAL、均衡 HIGH→NORMAL、标准仅 NORMAL，成功即停，配置不改写（2026-11-21 阶段 A2） |
| Experimental.Inkeys3.Draw3.DisablePowerThrottling | true | 即时，同上；true = `SetProcessInformation(ProcessPowerThrottling, {1, EXECUTION_SPEED, 0})` 显式关闭执行速度节流；false = `ControlMask=0` 交还系统默认（不是强制节流）。Win10 1709 以下调用失败只记日志 |
| Experimental.Inkeys3.Draw3.EnableUlwAsyncReadback | true | 下次启动（`configOnce` 快照）；只影响主画布 ULW：关闭 = 同步读回、不节流、不对齐（独立纹理与 SIMD 复制仍保留） |
| Experimental.Inkeys3.Draw3.UlwCompositionAlign | true，性能页合成对齐下拉框 | 下次启动；异步读回开启时是否对齐 DWM 合成时机提交，与 Win7dwm、DComp 键共同组成五档：始终、关闭、仅 UpdateLayeredWindow、仅 Win7dwm、仅 DirectComposition（`Draw3CompositionAlignIndex` / `Draw3CompositionAlignFromIndex`），手改出的混合组合不选中任何档且原值保留 |
| Experimental.Inkeys3.Draw3.Win7DwmCompositionAlign | true，性能页合成对齐下拉框 | 下次启动；Win7dwm 主画布按 `DwmGetCompositionTimingInfo` 的合成时序微调下一帧起点（与 DComp 共用 `DcompPhaseAdjustMs`，日志 `[Draw3Diag][win7dwm-align]`）；2026-11-21 新增，旧配置缺键时取 `UlwCompositionAlign && DcompCompositionAlign`，使原“仅 X/关闭”保持原义 |
| Experimental.Inkeys3.Draw3.DcompCompositionAlign | true，性能页合成对齐下拉框 | 下次启动；DComp 主画布连续绘制时按预计合成时刻微调下一帧起点，独立于 waitable 开关，改动通过 i 提示下次启动生效 |
| Experimental.Inkeys3.Draw3.UnthrottledMouseMoveWhileDrawing | false | 即时：设置页写配置后必须调用 `SyncDraw3State()`，经 `CursorSettings` bit 44 下发到窗口线程 |
| Experimental.Inkeys3.Draw3.InputMode | 0；自动/RealTimeStylus/WM_POINTER/鼠标兼容输入 = 0/1/2/3，越界按自动 | 下次启动（Host 启动时解析一次，不热切换）；手选直接尝试所选后端；自动时大屏触控模式（`Drawing.PaintDevice==0`）选 RTS，桌面交互模式 Win8.1/10 选 Pointer、其余选 RTS；Win7 上 WM_POINTER 仍可选（不置灰），按 RTS 解析。任一首选失败按 RTS 扩展→基础 XY→WM_POINTER→鼠标兼容回退（跳过已试项）。详见 `input-and-ink.md`“输入后端选择（第4步）” |
| Drawing.LiveTaper.{Enabled,Mouse,Touch,Pen} | 全 true | 新笔锁存（普通笔 Down 时取 `Enabled && 设备项`），同笔实时、定稿与保存共用；与 PenTailCorrection 独立 |
| Drawing.DevicePerformanceRecording | true | 即时：设置页写配置后直接调用 `Host::SetDevicePerformanceRecording`（不经 SyncDraw3State），启动时在 Host 创建绘制线程前下发；关闭后帧内不统计、不写盘 |

- UI 下拉顺序是精准/快速/关闭，必须显式映射为 2/1/0。快速模式追至最近输入位置，不外推未来；普通说明描述效果，不暴露实现类名。
- 新笔锁存预测和尾修正，同笔 Move、Up、等待断触续接后固化均读旧值；成功接续保留旧 modeler 和锁存值，新独立笔才读新偏好。
- Disabled 不调用 Predict，但保留 Update、正常 ModelEndOfStroke、平滑和独立尾修正。普通笔、Shape、Laser、候选接续四处判断必须一致；橡皮保持本来禁用预测的规则。
- 关闭尾修正只跳过 FinalizeStoredStroke 的 FitCompletedPenGeometry，保留正常 taper、公切线、真实 Up、Stored 和历史提交。当前修正使用 ReconstructAdaptiveTerminalTail，旧阶段二“尚未接产品”是历史说明。
- 全局单指限制 Touch，不限制 Pen/Mouse，不截断已准入笔画。拒收触点走 DiscardUntilTerminal，保留 Up/Cancel；手势登记必须对称，不让被忽略触点中途接管。真正接入旧笔的接续不能被新增单指策略误拒。
- 激光多指有效值是全局多指与保存子偏好的合取；隐藏不清空子偏好。Host 在 render owner 应用，setter 幂等，不能每批重复 control wake 导致空转。
- 粒子条目不显示toggle，仅保留BETA禁用说明；Controller构造配置true、setter请求true也始终关闭，不因旧配置恢复，重复请求不产生额外wake。保留算法/Shader/配置字段，不扩大为资源清理任务。
- 绘制页先“绘图环境”（设备项）、后“通用”；移除统一新笔提示但保留每笔锁存。界面60FPS、画布120FPS、精准显示本地化推荐标记，数据值不变；侧栏性能与实验glyph对调，不改变路由。
- 原硬件加速、DComp、兆芯兼容三项迁移至性能页并重启生效：硬件加速改为 RenderAcceleration 下拉，DComp 开关改为 PresentationMode 下拉（旧键仅在缺少新键时迁移），兆芯兼容继续使用原键。
- 软件调试 → 画布初始化信息：标题下按需显示呈现方式回退警告（交换链类型由呈现方式固定，没有交换链回退，2026-11-20 删除 `DebugSoftware.SwapChainFallback` 与 `PresentationFallbackReport::chainWarning`）。自动后端回退仅记录日志；主动指定后端失败才提示。正文展示实际状态。警告沿用黄色信息条，持有本代首次尝试的失败原因，外层窗口重建时传递。正文末尾由设置页追加两行进程级状态（2026-11-21 阶段 A2）：`GPU 调度类：HIGH (4)`（取读回值；读回失败显示最近成功请求的类并注明“未读回”；从未成功为“不可用”）与 `电源节流：已关闭执行速度节流/系统默认/不支持/设置失败（错误码 N）`。只显示当前状态，不显示回退过程；文本由 `FormatProcessSchedulingInformation(LastProcessSchedulingResult())` 生成（读缓存，不调用系统 API），与画布文本拼成同一字符串供显示与复制。
- 性能页三块顺序：“渲染”= `##draw3-acceleration`（画布渲染加速，只有 i 重启提示）→ `##draw3-adapter-preference`（GPU 偏好：自动（推荐）/优先核心显卡/优先独立显卡，说明 + i 重启提示）→ `##gpu-scheduling-priority`（GPU 调度优先级，说明 + i 提示“未关闭电源节流时……所选档位可能不会生效”，选高性能时上方黄色 Notice 提示录屏慎用，即时生效）→ `##disable-power-throttling`（关闭电源节流，说明含“Windows 10 1709 及以上生效”，即时生效）→ 兆芯兼容；“高级设置”= `##draw3-unthrottled-mouse-move`（绘制时鼠标移动不受画布帧率限制，即时生效，无重启提示）；“底层设置”= `##draw3-presentation`（透明呈现方式）→ `##draw3-composition-align`（合成时机对齐，五档：始终（推荐）、关闭、仅 UpdateLayeredWindow、仅 Win7dwm、仅 DirectComposition）→ `##draw3-waitable-wait`（Waitable 模式下启用等待）→ `##draw3-ulw-async-readback`（UpdateLayeredWindow 模式下异步读回），均重启生效。所有选项在任何系统上都可选（便携版换系统后由启动链自动回退，全选自动为最佳），不置灰；行上方按条件显示 `Design::Notice`：不兼容组合/系统不支持用黄色警告，系统适用条件用蓝色信息。输入方式：手选 RTS → 信息 `InputMode.RtsTouchpadNotice`；Win7 手选 WM_POINTER → 警告 `InputMode.PointerUnsupported`。透明呈现：Win7（低于 6.2）选 DirectComposition → 警告 `PresentationMode.DcompUnsupported`；Win8+ 选 Win7dwm → 警告 `PresentationMode.Win7dwmUnsupportedFormat`（{0}=8/8.1/10/11，10.0 build≥22000 为 11）；Win7 选 Win7dwm → 信息 `PresentationMode.Win7dwmAeroRequired`。交换链设置项已删除（2026-11-20 用户决定）：DirectComposition 固定 FLIP，Win7dwm 固定 SEQUENTIAL（Win7 Aero 实机 FLIP 黑屏且无报错），ULW 不用交换链；旧配置键 `SwapChainMode` 读取时忽略、保存时退役。提示只看已保存的选项与本机系统版本，不看运行时实际回退。waitable 行为合同见 `draw3-integration.md`“DComp waitable 帧延迟等待与熔断”；ULW 读回合同见同文件“ULW 独立纹理、异步读回与合成对齐”；鼠标选项合同见 `draw3-cursors.md`“绘制时光标跟随画布帧率”。
- 性能页“底层设置”首行为 `##draw3-input-mode`（输入方式，重启生效）。说明行为固定用户文案 `InputMode.E`（“决定设备消息的输入方式，一些选项可能与触控板手势冲突。”），不再拼接实际后端、大屏指定或重启提示（`NextInputBackendDiffers` 已删除）。实际后端与回退原因只在“软件调试 → 画布初始化输出”展示：`InputFallback` 非空时与呈现/交换链回退同位置显示警告条 `##canvas-input-fallback`（标题 `DebugSoftware.InputFallback`，正文“已从 {首选} 回退到 {实际}。”+ 各失败原因；首选取手选偏好，自动取解析结果）。`ComboRow/ComboBox` 支持单个 `disabledItem`（置灰、点击不提交、方向键/Home/End 跳过）。
- 设置→绘制→白板：系统低于 Win11 且实际后端为 RTS 扩展/基础 XY 时，在“白板拖动惯性”上方用 `Design::Notice` 显示 RTS 不兼容触控板拖动；Win11 实际 RTS 且窗口注册不可用时改显示“当前系统或设备无法在 RealTimeStylus 模式下使用此手势”。“白板拖动惯性”上方常驻 ⓘ `CanvasPanInertia.TouchpadRequirement`（“触控板手势需要 Windows 8.1 或更高版本及精确式触控板。”）。
- 绘制页“触摸”分区的 `##draw-multi-touch` 是多指绘图总开关，下方内联 `##draw-multi-touch-tools`：绘制/荧光笔/激光笔/橡皮/图形（一笔画），总开关关闭时原位禁用并保留勾选（同实时笔锋）。激光笔项写 `Laser.MultiTouchEnabled` 并经 `SetLaserOptions` 发布，其余四项经 `Host::SetMultiTouchToolOptions` 整组发布；原独立行 `##draw-laser-multi-touch` 与 `Draw.Laser.MultiTouch` 文案已删除。
- 绘制效果页“实时笔锋”位于“抬笔笔锋修正”之后，内联鼠标/触摸/笔；总开关关闭时子项原位置灰并保留勾选。启动与改值经 `Host::SetLiveTaperOptions` 整组发布。
- `Drawing.FrameRate` 只在启动时选定 profile；设置页改档只写配置（带重启提示），当前会话帧循环仍按启动值运行。实机验证帧率相关行为时必须先重启再测。
- 共享 Scheduler 保留原子请求、固定顺序、唯一 render owner、Idle/Continue/Retry 和设备恢复；帧率变更不制造永久渲染，也不修改动画倍率或输入采样。
- 画布设备、adapter、交换链和 RTS 信息在实际初始化/恢复/Resize 边界发布。初始化新代次清旧成功详情，停止后明确标为最后记录；Hardware 偏好不等于实际 Hardware，DComp 偏好不等于最终呈现模式。
- 初始化文本不依赖 ConsoleOutput、不解析日志、不轮询设备；只在 owner 查询 GetDesc/GetDesc1，设置 getter 短锁复制缓存。

## 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 帧率/预测类型错误、超宽整数或非法档 | 回退该字段默认值；不窄化或把 bool 当整数 |
| 一笔中途修改预测/尾修正 | 旧笔及成功接续不变，下一独立笔采用新值 |
| 关闭全局多指且有旧笔等待续接 | 仍按既有身份、时间和运动判定真正续接；其他额外新指拒收 |
| 关闭预测后 Up | 不调用禁用 predictor；正常模型收尾与真实终点保留 |
| 尾拟合拒绝/关闭 | 保持合法完成基线，其他笔锋处理照常 |
| 关闭多指后再打开 | 激光子项恢复原保存值，不写成 false |
| 旧配置或调用方请求开启粒子 | 构造/setter/Get均保持关闭，UI无toggle，不增加wake |
| GPU/呈现/输入回退 | 信息显示实际 WARP/ULW/BasicXY/Mouse，与同代设备一致 |
| 查询失败或停止 | 显示不可用/错误或历史标记，不用模板冒充当前成功 |

## 5. Good / Base / Bad Cases

- Good：旧笔按精准起笔，中途改关闭，旧笔及其断触接续正常结束，下一笔无预测；关闭尾修正仍保留正常收笔。
- Base：缺项配置保持界面60、画布120、多指/修正开启、精准预测、激光两项关闭。
- Bad：仅在 Run 开头改 model params、Up 时读最新偏好、直接回收 producing touch、通过每帧 DXGI 查询填信息块。

## 6. Tests Required

- 配置真实磁盘 roundtrip、缺项、类型/整数宽度/白名单、激光子偏好保留。
- 真实Controller对象使用粒子配置true构造，再重复setter(true/false)，Get始终false且wake generation/pending不变；不Attach/Initialize/Run，不创建HWND。
- 真实 Scheduler 30/60、运行时切档、多客户端同批、idle/Stop。
- 真实模型/生产 probe 覆盖三预测模式、四帧率、尾修正开关、并发笔快照、候选接续、终态回收及池复用；不能只断言枚举或对末端 helper 手工赋值。
- 初始化文本发布/合并、新代清理、停止标记和真实 owner 回退路径；无窗口结果不能宣称实体 GPU、RTS 或桌面视觉验收。
- i18n sync/check、完整 InkeysRepo.sln Debug|ARM64 及适用无窗口测试；不自动启动 GUI。

## 7. Wrong vs Correct

Wrong：`ApplyPredictionMode(params, latestMode)` 仅调用一次，然后全局 constexpr 控制所有 Predict；Finalize 时读取配置开关。

Correct：新笔锁存 mode/correction，Reset、全部 Predict 判断及 Finalize 共用该笔快照；Host 仅发布期望值，不重置旧笔模型。

## 诊断文本与复制（R10）

### 1. Scope / Trigger

修改画布初始化/显示器EDID信息、SDK名称格式化或复制入口时应用；仅显示格式与复制，不改变设备、交换链、RTS/鼠标回退或查询线程。

### 2. Signatures

- `Design::CopyButton(const char* id,const char* label,const char* text) -> bool`：标准按钮尺寸、块下右对齐、点击后ImGui::SetClipboardText，恢复原左侧cursor。
- Diagnostics模块的 `FormatDiagnosticFeatureLevel/Format/SwapEffect/AlphaMode/Scaling/SwapChainFlags/SampleDesc/HResult/SwapChain/RtsAttempt`：纯值到字符串，无设备查询。
- `RunDiagnosticFormattingProductionTest()`：由既有 `--draw3-device-policy-test` 调用，无GPU/HWND。

### 3. Contracts

- 两块各一个独立ID的复制按钮，使用该帧已显示的完整canvasInformation/edidText，包含隐藏于滚动区域的多行/全部屏幕；不能重新查询再复制，不能只复制可见DrawData。
- 标准ButtonWidth（至少72 DIP自然宽度）、ControlHeight=32 DIP，在可用范围内右齐；复制后后续标题回到原左起点。
- 水平对齐使用成对的正值Indent/Unindent，offset=0不调用Indent（0会使用默认IndentSpacing）。不得在Button后用SetCursorPos/SetCursorScreenPos恢复到含尾部ItemSpacing的Y；该位置不在CursorMaxPos内，若作为最后item会在EndChild/End报错。保留Button的LastItemData、原间距，不改ImGui检查旗标。
- 已知枚举显示完整SDK常量：DXGI_FORMAT_*、DXGI_SWAP_EFFECT_*、DXGI_ALPHA_MODE_*、DXGI_SCALING_*及D3D_FEATURE_LEVEL_*，数值不丢。
- Flags逐位显示当前SDK的完整DXGI_SWAP_CHAIN_FLAG_*名称，组合用`|`，未知位和总32位hex保留；0为无标志，不能编造NONE常量。
- SampleDesc明确Count/Quality，1/0注明无MSAA；HRESULT显示已知符号名和8位hex，未知结果保留数值。
- Hardware说明硬件GPU，辅助ULW显示UpdateLayeredWindow全称，主DirectComposition保持。输入后端使用RealTimeStylus (RTS)，尝试摘要为中文就绪/失败并保留失败阶段；用户态图形驱动（UMD）版本名称完整。
- DPI标签为X/Y和乘号，EDID全称只在块首出现一次，原解析状态与真实显示器数据保持。
- 所有图形/输入文本仍在原owner的初始化/恢复/Resize边界缓存，UI只读取；控制台日志格式不因UI摘要改变。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 多行/多屏或首部离开viewport | 复制完整原字符串，不截断或丢Unicode |
| 同label两个按钮 | 独立ID，分别复制各自块，未点击不触发 |
| 本帧数据更新 | 复制与当前显示同一版本，不读下一次缓存 |
| 未知枚举/flag位/HRESULT | 明确未知并保留数值；组合flags不丢已知位 |
| 无数据/查询失败 | 保留原状态/错误，不用模板冒充成功 |

### 5. Good / Base / Bad Cases

- Good：flags=0x40显示完整FRAME_LATENCY_WAITABLE_OBJECT；点击画布复制只复制该帧全部画布摘要，EDID按钮复制全部屏幕。
- Base：普通S_OK、BGRA8和FLIP_SEQUENTIAL使用完整名称，图形状态和回退顺序不变。
- Bad：硬编码截图参数、丢unknown位、复制时跨线程查询DXGI、使用同一按钮ID或忘记恢复cursor。

### 6. Tests Required

- ClipboardProbe真实ImGui按钮点击覆盖尺寸/右齐/块下/ID/后续cursor、多DPI/主题/三语和中文长多行/四屏文本；不写真实系统剪贴板。
- 控件必须单独覆盖作为容器最后一项直接EndChild/End：非零ItemSpacing、root/固定child/AutoResizeY、滚动/裁剪、外层缩进、恰好/小于按钮自然宽度及分数DPI。收集真实ErrorCallback/ErrorCount且保持assert/tooltip配置不变，不能靠后续Text/Dummy清除IsSetPos而掩盖问题。
- 纯格式probe覆盖用户实际值、组合/未知flag、未知enum/HRESULT、SampleDesc与组合DXGI_SWAP_CHAIN_DESC1、RTS成功/失败；不创建GPU或窗口。
- 完整Debug ARM64 solution及适用attention/copy无窗口专项；不以纯格式测试称真实设备故障或桌面视觉验收。

### 7. Wrong vs Correct

Wrong：点击后再调用CanvasInformation取新快照，或仅复制已换行显示的可见片段。

Correct：绘制与复制复用同一局部完整字符串；只在user点击标准按钮时交给现有ImGui剪贴板接口。

## 白板拖动惯性（多设备漫游）

“绘制 → 通用”在预测设置之后、橡皮组之前新增“白板拖动惯性”，使用现有 ToggleRow + InlineCheckboxes 五类输入。总开关默认开、鼠标/笔尾默认关、触控板/触摸/笔默认开；父关闭收起但保留子偏好，子项可全关。设置Changed及启动经Host同代快照发布，owner应用，只有真实变化唤醒；热禁停对应coast而不取消held跟手，重新开启不复活旧速度。2026-11-12起PTP扩展暂时停用，触控板子项仅保留偏好，不代表当前设备能力；本轮不为停用另改UI或文案。2026-11-20第4步起按实际后端恢复触控板入口，并在该分区加入 RTS 不兼容/手势不可用提示，见上文第3节。完整字段/线程/输入/验证合同见 [canvas-pan.md](canvas-pan.md)，不要添加重启生效说明或新二级页。

## 性能页设备性能看板（2026-11-18）

- 位置：性能页 `PageHeader` 之后、第一个 `SectionHeader` 之前。`Setting.PerformanceDashboard.h/.cpp` 采用 Input/Events 模式，不依赖 Host；`Setting.cpp` 读取 `ProductHost().DevicePerformanceSnapshot(revision, report)`（版本未变不复制）、`Drawing.FrameRate` 与 `Drawing.DevicePerformanceRecording`，并传入 `navigationDelta`、动画开关和动画速率。
- 卡片前调用 `PageContentStart()`；卡片后不另加节间距。背景不用 `ChannelsSplit`：先画背景，内容画完后按真实高度平移背景下半部顶点。数字、条形、底色、箭头统一用 `NavigationTween`（颜色是固定起点到目标的 0→1 插值），不用 `AnimateColorU32`。
- 卡片内开关用 `ToggleAction` 手动布局，不用 `ToggleRow`（会画出卡中卡）。清除按钮为两段式，3 s 内再点生效。窄窗口下放不下的状态标签不画。
- `Palette::performanceFill/performanceAccent[5]`（无数据/充裕/适中/偏紧/超载）：绿、黄、红底色与 notice 一致，Acrylic 下与 heroFill 一样设为 alpha 179，“无数据”底色运行时取 `card`。
- 统计窗口为最近 7 个使用日，按实际生效的设置签名分开（帧率、设备、呈现模式与实际交换链、实际 waitable、ULW 异步读回与对齐、DComp 主画布下的合成对齐设置值、兆芯兼容实际生效值）。阶段 E 前写入、缺少 DComp 对齐字段的 DComp 记录按“未对齐”读入。当前签名没有数据时显示最近签名的数据，并标注“设置变更前”。
- 内容（2026-11-18 精简；2026-11-19 帧率口径轮）：收起态只有标题与档位/状态标签、大号负载百分比，其右侧三行为负载标签、“平均渲染帧率 约 N FPS”（主数值，`Body`）、“计算帧率 约 N FPS · 无限制渲染帧率 约 N FPS”（`Caption` 次要色）；下方一句档位结论，结论下方（收起态）为“当前显示器的帧率限制为 N FPS”（实测范围不同时写 `60–120`，未测到不显示）；各维度行右侧为该维度的平均渲染帧率（取不到为 `--`）；不显示帧率建议、统计范围、覆盖维度或其他说明。展开态是各维度行、记录开关（无说明）、清除按钮，与按钮同行靠右显示评估时间（只显示时间）。
- 软件调试页“画布”节的“设备性能看板预览”下拉框（默认/无数据/充裕/适中/偏紧/超载/设置变更前）只替换看板显示用的报告，不写配置、不影响记录与统计，重启恢复默认；预览期间看板内的记录开关与清除按钮不作用于真实数据。
- 文案数字模板（`RenderFpsFormat`、`DimensionFpsFormat`、`DisplayLimitFormat` 的实参为字符串，可为 `--` 或 `60–120`；`SecondaryFpsFormat` 为两个整数）用 `{0}`/`{1}` 占位符，格式化包 try/catch，模板登记在 `IdtTest::RunI18nLocaleTests`。文案中不用 “—”“…”（字形检查）。
- 计数与写盘合同见 `draw3-integration.md`“设备性能看板计数与移交”。

## GPU 调度优先级与电源节流（2026-11-20 阶段 A，R1–R4）

### 1. Scope / Trigger
其他程序（课堂 PPT 放映、录屏、视频）占用 GPU 时 Inkeys 书写滞后甚至停滞。Win11 实测：进程拥有可见窗口但不在前台时，约 3 秒后 QoS 降为 1，GPU 调度优先级随之为 1（与窗口样式无关）；前台程序为 16，DWM 为 23。

### 2. Signatures
- 模块 `Inkeys.Helper.ProcessScheduling`（`Inkeys/Inkeys/Helper/Helper.ProcessScheduling.cppm`）：`GpuSchedulingTier{Standard,Balanced,Performance}`、`NormalizeGpuSchedulingTier(int)`、`GpuSchedulingClassForTier(tier)`、`ApplyProcessScheduling(tier, disablePowerThrottling) -> ProcessSchedulingResult`（线程安全，参数不变时跳过系统调用并返回上次结果）。

### 3. Contracts
- `D3DKMTSet/GetProcessSchedulingPriorityClass`（gdi32）与 `SetProcessInformation`（kernel32，Win8+ 才导出）全部 `GetProcAddress` 动态获取；**禁止**静态调用（Win7 无法启动）。失败不影响启动与绘制。
- 调度类请求按档位逐级回退（2026-11-21 阶段 A2，`SchedulingFallbackRoute` / `RunSchedulingFallback`）：高性能 REALTIME→HIGH→NORMAL、均衡 HIGH→NORMAL、标准仅 NORMAL；第一个尝试的就是档位对应的类，失败判定只看 `D3DKMTSetProcessSchedulingPriorityClass` 返回的 NTSTATUS < 0（读回值只用于显示），当前一级成功即停；全部失败保持系统原状。`ProcessSchedulingResult` 记录 `requestedClass/appliedClass/attemptCount/valid`，`LastProcessSchedulingResult()` 返回缓存副本。
- 每次实际调用输出一行 `Log::Module::Draw3` Info：`进程调度 tier requested attempts=<类:status,...> applied readBack disableThrottling priorityApi status throttlingApi throttlingApplied throttlingError os`。
- Win11 实测映射（WPR `ChangePriority`）：节流时 NORMAL/HIGH 都是 1、REALTIME 23；关闭节流后 NORMAL/HIGH 都是 16、REALTIME 23。即 Win11 上“均衡”的收益来自关闭电源节流。2026-11-21 实机回收：Win10（HD 4600）上 HIGH 与 NORMAL 延迟无差别；Intel 核显即使被降到 1 也不会饿死，GPU 压力下延迟约为压力程序单包时长 × 1.6（抢占粒度限制），档位与节流基本无影响；饥饿型降级主要见于骁龙（Adreno）。
- 实测（Snapdragon X，GPU 压力工具占前台，waitable 链）：默认（均衡 + 关闭节流）p50 44 ms / max 90 ms；标准或均衡 + 节流默认：墨迹 1.5 s 内不上屏；高性能 60–78 ms。OBS 录屏 + 压力：默认掉帧 2.5%（改动前 4.4%），高性能 23.5%——高性能不作默认，说明提示录屏慎用。

### 6. Tests Required
- `config_metadata_tests.cpp`：默认值（1、true）、档位映射 2/4/5、越界回退均衡；注入假 setter 覆盖三档回退路线、成功即停、全部失败，以及画布信息两行文本的各分支。
