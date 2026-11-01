# 技术设计

状态：用户已批准实施。需求权威来源为 `prd.md`，最新帧率边界是共享界面与独立画布。

## 修改边界

- 最小行为缺口：设置缺少两套帧率、通用绘图及已有激光能力入口；Draw3 缺少产品可调的预测/抬笔修正偏好、全局 Touch 多指准入及可供设置读取的初始化事实。
- 行为所有者：界面节拍在 `RenderPipeline::Scheduler`；画布节拍/模型在 `StrokeModelConfiguration`；触点生命周期和激光效果在 `DrawingController`；设备、呈现和输入初始化由唯一 Draw3 Host 协调。
- 在实际所有者补接配置，不在设置页代替引擎筛触点、修改 GPU 对象或另开调度线程。
- 四部分共用设置、schema、启动接线和 Host 发布边界，采用一项集成任务、分步骤验证，不拆成独立并行修改同一文件的任务。
- 不进行无关格式化、调度架构重写、模型/粒子算法重写、配置系统迁移或依赖调整。

## 设置组织

1. 左侧中段顺序：常规 → 绘制 → 性能 → 插件 → 组件 → 实验室 → 版本。
2. 新 PageId 追加为未使用编号（当前最后编号 15，预计 Performance=16），同步 `settingTabEnum`；不占用历史退役空洞，不重排旧值。
3. 性能页沿用现有设计控件：
   - 帧率：界面帧率 ComboRow、画布帧率 ComboRow；画布注明重启生效。
   - 渲染与兼容：原硬件加速、DComp、兆芯兼容 ToggleRow；均注明重启生效。
4. 绘制主页在恢复警告之后先显示“绘图环境”（原绘图效果优化/设备选项），再显示“通用”SectionHeader，依次放置多指绘图、抬笔笔锋修正、墨迹预测；不显示统一生效提示。激光笔分类保留多指ToggleRow及粒子不可操作说明行，说明均含BETA。
5. 软件调测页在显示器与 EDID 块之前放“画布初始化信息”只读块，沿用现有卡片/文本排版，不添加控制台开关、逐帧统计或复制/导出功能。

## 配置合同

新字段归 `Inkeys::Config` 的 main.json；已有 Draw3/光标等相邻消费者使用该系统，不涉及传统 deploy.json 迁移。

| 建议路径 | 类型/默认值 | 运行行为 |
| --- | --- | --- |
| `UI.FrameRate` | int = 60；仅 30/60 | 整套共享界面后续调度批次生效 |
| `Drawing.FrameRate` | int = 120；仅 30/60/120/240 | 下次启动使用完整 profile |
| `Drawing.MultiTouchEnabled` | bool = true | 后续新 Touch Down 的准入 |
| `Drawing.PenTailCorrectionEnabled` | bool = true | 新独立笔画锁存，Up完成拟合时读取该笔保存值 |
| `Drawing.InkPredictionMode` | int = 2（精准）；仅 0/1/2 | 显式映射既有 enum：0=Disabled/关闭，1=StrokeEnd/快速，2=Kalman/精准；新笔使用 |
| `Drawing.Laser.MultiTouchEnabled` | bool = false | 与全局开关求有效值，作用后续 Laser Touch Down |
| `Drawing.Laser.ParticlesEnabled` | bool = false，兼容字段 | 产品始终忽略启用值；Controller构造和setter均强制关闭 |

- 三项迁移沿用 `Experimental.Inkeys3.Draw3.EnableHardwareAcceleration / EnableDirectComposition / EnableZhaoxinCompatibility`；位置变化不改磁盘键。
- 两项帧率的解码严格检查 JSON 整数类型、宽度和白名单；缺项、浮点、布尔、字符串、超宽整数和非法值分别回退 60 / 120。运行时公开入口也校验，不能依赖 UI 下拉框保证合法。
- 预测模式同样严格白名单解码，缺项/非法值回退精准；UI下拉顺序为精准/快速/关闭，不能把索引直接强转为 Disabled/StrokeEnd/Kalman。
- 设置先更新原子配置/发布运行偏好，再按现有 `QueueConfigWrite()` 冻结保存快照进入 FIFO。禁止在 render callback 同步落盘。
- 关闭全局多指只使激光多指条目隐藏/无效，不覆盖其保存值；再开启恢复用户偏好。粒子项始终可见。

## 共享界面帧率

- `RenderPipeline.cpp:52` 的常量节拍改为 Scheduler 拥有的受限偏好。提供实例级与进程级轻入口，便于启动接线和真实 Scheduler 无窗口测试。
- `IdtMain` 读取配置后、UI 客户端启动前传入一次；设置修改同步同一入口。不能让启动顺序恢复为默认 60。
- 下一批调度使用 30 FPS（约 33.33 ms）或 60 FPS（约 16.67 ms）的间隔；切档重算后续 deadline，避免沿用旧节拍或一次大 dt。
- 保留唯一串行 render owner、共享 frameTime、固定 dispatch 顺序和客户端续帧合同。所有 WARP 客户端统一生效，包括共享管线的启动预览/白板冻结客户端，不新增逐客户端例外。
- 空闲仍为事件等待；改变偏好不能制造永久 Continue。保留 request/reset 双检、Stop、DeviceLost 和单客户端 Retry 行为。
- 只改变渲染节拍，不修改 Animation.SpeedRate、业务时钟、指针采样或 RTS。

## 画布帧率

- 扩展现有模型配置创建入口，使其接受合法 `StrokeTimingProfileId`；原调用保持默认 Fps120。
- 将磁盘整数在产品边界映射到枚举，经 HostStartOptions 或等价启动值传入 Draw3；不得依赖 UI 管线的帧率。
- 启动时从 `GetStrokeTimingProfile` 获取整套参数，连同 min_output_rate、prediction_interval、Kalman 样本与 wobble 超时等构造模型。
- 不仅覆盖 `target_fps`，不修改四档既有数值。Resize/DPI/设备恢复若重建模型，沿用本会话已选择的同一档位。
- 当前会话只保存下次启动偏好，不中断 stroke 或热重建历史/模型。无需增加第二套实时切档协议。

## 通用绘图偏好与每笔锁存

### 已有能力映射

- 精准：`InkPredictionMode::Kalman`，复用现有调参和帧率参数组。
- 快速：`InkPredictionMode::StrokeEnd`，复用 Ink Stroke Modeler 的 StrokeEndPredictor；沿当前模型状态追至最近输入位置，不外推未来，不实现新的直线预测器。
- 关闭：`InkPredictionMode::Disabled` / DisabledPredictorParams；停止预测补绘，但保留模型 Update、平滑、真实 Up 处理和独立抬笔修正。
- 抬笔笔锋修正对应 `FinalizeStoredStroke` 普通 Pen 分支的 `FitCompletedPenGeometry -> ReconstructAdaptiveTerminalTail`。当前产品已经调用该能力；旧 spec 开头“尚未接产品”是历史阶段，不作为现状。
- 修正关闭只跳过完成态副本的异常几何拟合，保留 `ApplyLiveTipTaper`、`EnforceCapsuleTangency`、真实终点锁定及正常 Stored 提交。保留原工具适用范围与所有预算/拒绝/原子回退门，不改算法阈值。

### 接线与生命周期

- Host 发布多指、抬笔修正、预测模式的一份通用偏好值快照，沿既有控制唤醒交给唯一绘制 owner；可复用现有 InputWidthModeSettingsState 的有界编码/原子快照惯例，不新增线程。
- 真正创建新独立笔画时，在 reset/modeler.Reset 前保存该笔预测模式和尾修正值，按同一模式构造 model params。新 Touch 准入使用同次读取的多指偏好。
- 不能只改线程启动时的一份 `strokeModelParams`；活跃并发笔画可因落笔时刻不同持有不同模式，DPI/reset等在同笔内重置模型时仍用其锁存值。
- `ApplyPredictionMode` 改为显式接受模式；候选断触预测、形状、激光和普通笔四处 `kActivePredictionMode` 分支均改用对应笔画锁存值，避免 Disabled 时误调用返回错误的 Predict。
- 成功断触接续沿用旧 stroke、modeler 和已锁存偏好；物理新 contact 不能让同一逻辑笔划重新读取预测或修正设置。失败候选正常收尾，新独立笔画才读取新值。
- 完成态修正从 ActiveStroke 或等价随笔数据读取保存值，不能在 Up、等待续接后固化、Clear/退出完成时读取最新全局值。
- 不把这些运行时笔画偏好加入 UInk 文档格式；历史笔迹保持已经提交的最终几何，不在设置切换时重算。

### 面向用户的候选说明

| 条目 | 说明 |
| --- | --- |
| 多指绘图 | 允许多个手指同时书写或绘图。关闭后，仅响应单指绘图。 |
| 抬笔笔锋修正 | 减少抬笔时笔迹末端的突兀折线，使收笔更自然。 |
| 墨迹预测 | 减轻书写时笔迹的滞后感。精准模式预估后续走势，快速模式将笔迹补绘至最近的输入位置；关闭后不补绘预测笔迹。 |

按用户最新要求移除三项共用的生效提示，保持新笔锁存逻辑。说明不使用 Kalman、StrokeEnd、拟合窗口等内部术语，不承诺未经验证的精度/性能数值，不仅复述标题。

## 2026-11-01 显示细化

- Performance/Experimental图标分别改用对方原glyph，页面编号/顺序不动。
- 默认的60 FPS、120 FPS和精准选项增加本地化推荐标记，数值映射不动。
- 本轮仅调整设置/i18n、Controller粒子产品门禁、对应probe和任务/规范记录；保持原BOM/CRLF，不重构粒子系统。

## 多指与激光偏好

- Host 沿现有 `SetInterruptedStrokeReconnectEnabled` / PumpBridgeState 的发布、唤醒和绘制 owner 消费方式，向 controller 传递偏好；启动前发布的值也必须在 controller 创建后应用。
- 激光多指继续使用现有setter；粒子保留原算法，但Controller构造和 `SetLaserParticlesEnabled` 强制false，旧配置/调用方true均无效。UI保留无toggle的不可操作条目，不新增资源或渲染路径。
- 新全局 Touch 门禁放在 controller 的真实 Down 准入处，覆盖普通笔、荧光笔、形状、橡皮和激光。Pen/Mouse 不参与 Touch 计数。
- 全局关闭且已有物理活动 Touch 时，额外 Down 使用既有 ignored/DiscardUntilTerminal 路径；后续 Move/Up/Cancel 只消费终态，不产生笔迹，也不在首指抬起时接管。
- 准入应先于触摸手势登记，或成对撤销登记；不能仅在 Laser helper 旁提前 return 而遗留手势计数。RTS 仍采集全部接触，避免 producer 层漏终态。
- 不将 awaitingReconnect、已经 Up 的收尾 runtime、Laser Hold/Fade 当作物理仍按下的第一指。保持已接受接触及现有断触续接、倒笔橡皮、批次清理行为。
- 开关改变只影响未来新 Down；不强制取消现有 contact，不新增 Touch Pan 产品入口。

## 画布初始化信息快照

### 数据与展示

- 分离“用户配置偏好”和“本代实际结果”。UI 只展示初始化/恢复已发布的值，错误时用不可用/失败状态与 HRESULT，而不是猜测。
- 低频数据包括生命周期/代次、Hardware/WARP、Feature Level、adapter 描述和 VendorId/DeviceId/SubSysId/Revision、可取得的驱动版本和显存容量。
- 呈现信息取实际成功模式；ULW 也可能使用 FLIP 交换链，不能根据模式名拼造 swapchain。实际 `GetDesc1` 获取尺寸、格式、bufferCount、swapEffect、alpha、sampleCount/quality、flags，另记录 waitable 是否真正获得。
- 输入信息使用真实 `HostInputMode`（RTS Extended、RTS Basic XY、Win32Mouse、Disabled）及已有初始化结果/回退原因；基本 XY 仍是 RTS，不误写成 Mouse。必要时在 `RealtimeStylus` 增加有界只读尝试摘要，不启用产品当前关闭的 `DRAW3_RTS_DIAGNOSTICS` 宏。
- 保留既有控制台日志格式；设置块使用结构化事实，不解析控制台文本或依赖 ConsoleOutput 开关。

### 所有权和发布

- 设备、adapter、swapchain 查询只在其现有创建/恢复/resize owner 执行，设置/UI 线程不碰 live COM/GPU 对象，也不枚举系统设备。
- 在现有 Host 文件内增加有界只读值快照及读取入口；优先单独低频快照，避免给每次 RuntimeSnapshot 的热路复制大段初始化文本。
- 发布不可变快照或等价受保护值，代次与生命周期一并发布。每次尝试失败先标对应状态，只有最终成功才发布当前可用设备/呈现/交换链。
- 初始化、DComp→ULW、Hardware→WARP、恢复、resize 后更新，Stop/新 Start 清理或明确标记上代信息。设置开着时也不混合旧 device 与新 presenter。
- 不新增采样 worker、日志读取线程或设备轮询；调测块不作为性能页的“渲染状态”扩展。

## 预计文件范围

| 文件组 | 必要性 |
| --- | --- |
| `Setting.Pages.h`、`Setting.Shell.cpp`、`Setting.cpp` | 新导航/页面、迁移条目、绘制设置与诊断块 |
| `Other.Config.cppm/.cpp` | 七个新字段、离散帧率和预测模式的校验 |
| `RenderPipeline.cppm/.cpp` | 唯一共享界面帧率入口与节拍消费 |
| `IdtMain.cpp`、必要的 `IdtDrawpadFacade.cpp` | 启动配置、产品状态接线与已有配置测试入口 |
| `Draw3.Host.h/.cpp` | 启动档位、偏好发布、初始化快照 |
| `Draw3.InkPrediction.cppm/.cpp`、`Draw3.DrawingController.cppm/.cpp`、`Draw3.StrokeGeometry.cpp` | 按档位创建模型、每笔预测/尾修正快照、全局 Touch gate，复用现有预测/尾修正与激光能力 |
| 必要的 `Draw3.GraphicsInitialization.*`、`Draw3.TransparentPresentation.*`、`Draw3.Diagnostics.*`、`Draw3.RealtimeStylus.*` | 仅为捕获既有初始化事实和输入尝试摘要，按研究结果选择实际需要的最小子集 |
| `Inkeys/src/i18n/*.jsonc`、`IdtI18nKeys.g.h`、`Scripts/i18n.zh-CN.snapshot.jsonc` | 文案、BETA、生成链 |
| 既有 headless/产品自检文件及相关 spec | 验证行为和记录新配置/生命周期合同 |

工程文件仅在确有必要登记测试/源码时改动；优先扩展既有文件，不引入新的构建目标。

## 风险与回退

- Touch gate 是主要新增行为，重点检查手势计数、ignored 终态、Reconnect 与混合输入；不借此重构输入系统。
- 每笔锁存必须覆盖全部 Predict、model reset、接续与完成入口，不能让全局变化影响旧笔画，不能把“关闭抬笔修正”实现成关闭整个笔锋或 Finalize。
- 初始化快照是跨线程值发布，重点检查失败路径与代次隔离，不扩大到完整诊断框架。
- 缺项配置沿默认行为，新值只写新版系统；回滚源码后旧字段可被忽略，迁移的三项原键继续可读。无需改数据文件格式或清理未知键。
- 若需要扩大至热切换画布参数、额外 GPU 支持或手势重写，先停止扩大范围并重新审阅，不自行实施。

## R8：注意事项行（用户已要求继续实现）

- SettingRow及Toggle/Combo/Slider/Button/Navigation等封装追加可选 `const char* note=nullptr` 参数，不解析原说明文字，不让控件层依赖i18n聚合头；页面显式传递说明与提示。
- 注意事项在说明下方，Caption说明对应Caption提示；蓝色信息i图标，文字使用主题正常前景色，自动换行。布局高度包含图标/提示和间距，沿现有响应式说明宽度与动作区退让逻辑测量。
- 无note时严格保持旧几何和动画；有note时将其计入文字块，宽窗/窄窗/stacked布局均不能与操作区或下一行重叠。必要时暴露InfoText以复用同样样式展示设置分组下的明确提示。
- 页面显式迁移重启/启动、权限/依赖和数据风险文案；只处理活动设置行，普通功能解释、下拉模式和错误/确认对话框不机械迁移。
- 激光两句按用户要求直接删除，不作为note，BETA及粒子禁用均保留。
- 修改限于Setting.Controls.h/.cpp、必要Setting.Design.h、Setting.cpp/Pages.cpp、国际化生成链及现有headless测试；不改引擎、权限或持久化流程。
- 增加已有headless中的注意事项专项入口，仅运行相关纯布局/ImGui绘制测试，避开已知昂贵且不稳定的无关HomeWARP基准；不新增构建系统或可见窗口。

## R9：用户澄清的最终边界

- 小型条目附注改用已验证的Segoe Fluent Icons InfoSolid U+F167；不替换侧栏或旧Notice大图标。
- SettingRow入口按参数角色归一：description为空而note非空，则作为普通description，清空note；不是按内容关键词解析。只有独立解释之外的额外注意才绘制i。
- 快捷键/类/页级原信息框恢复；不再用InfoText替代原说明块。PPT详情去掉重复支持版本行，教程/权限卡片使用正常正文和原动作；通道原块及架构原说明还原。
- 状态框新增独立版本状态标题键，实际更新偏好栏目键不变。其他条目内的重启/风险附注不回滚。
- 本轮范围限共用提示glyph/入口归一、对应专项预期、上述页面/本地化与规范记录；不改引擎、配置或业务流程。

## R10：诊断复制与格式

- 两个信息块分别复用标准Button字体/自然宽度/32DIP高度，在块下右对齐。按钮仅点击时通过现有ImGui剪贴板接口复制该帧完整缓存字符串，返回后恢复左侧游标，后续标题不继承右对齐。
- 可以在Controls增加专用的右对齐复制按钮辅助以供两块复用、真实ImGui/ClipboardProbe测试，不引入新的剪贴板线程或系统服务。
- DXGI/D3D/HRESULT格式化复用Diagnostics模块的纯值辅助，低频owner采集处格式化；未知枚举/flag位保留数值，已知名称使用完整SDK前缀。RTS摘要/输入名称在原owner路径仅改文字。
- EDID文本只调整标签与单位/全称，保留同一DisplaySnapshot逐屏构造；无需更改Display枚举或缓存。
- Scope：Controls及对应headless、Setting.cpp、Diagnostics/TransparentPresentation/RealtimeStylus/Host的文本，必要既有纯生产probe；不改初始化、回退、设备查询线程或工程配置。

## R11：复制按钮末项边界

- CopyButton末尾SetCursorScreenPos恢复X时，Y仍在Button完成后原生ItemSpacing处，超出不含spacing的CursorMaxPos。EndChild的End检查IsSetPos便报错；旧测试后续Text/Dummy掩盖了该问题。
- 最小行为缺口在共用CopyButton布局所有者，优先用Indent/Unindent等不标记未登记空间的水平布局操作保持对齐，不在每个页面塞Dummy补丁，不关闭检查。
- 保留最后ItemData为按钮、原间距/标准大小与复制动作；offset=0不能调用会使用默认IndentSpacing的Indent(0)。
- 测试独立child末项直接结束与根window末项，收集真实ErrorCallback/ErrorCount，涵盖裁剪/滚动/外层缩进/宽度等边界。仅改Controls.cpp、既有设置专项及规范/验证记录。
