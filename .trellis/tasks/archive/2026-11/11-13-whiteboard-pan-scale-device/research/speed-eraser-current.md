# Research: 当前笔速橡皮设备、场景、尺度与动态状态

- Query：Mouse / Touch / Pen / PenTail 当前各怎样计算笔速橡皮，桌面/大屏模式与可信物理尺度如何影响它，哪些机制可用于白板惯性规划。
- Scope：internal；只读生产代码、测试与现有规范，本轮不运行应用或测试。
- Date：2026-11-13
- 证据：当前生产源码；单元测试仅作为已编写的合同，不能冒充本轮执行结果或硬件实测。

## 文件索引与引用代号

以下 `SE:行号` 等引用均指本表对应文件；范围说明用于定位逻辑，不意味着每行都是独立规则。

| 代号 | 文件 | 职责 |
|---|---|---|
| SE | [Draw3.SpeedEraser.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp) | 唯一生产解析器、动作测速、目标直径与连续控制器 |
| SH | [Draw3.SpeedEraser.h](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h) | 输入身份、五入口、显示标尺、Config 与状态数据 |
| DC | [Draw3.DrawingController.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp) | 接触 Down 锁存、触摸批次、Hover 会话与几何接线 |
| HO | [Draw3.Host.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp) | Display 快照、主绘制窗口输出、模式/配置发布 |
| RS | [Draw3.RealtimeStylus.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp) | 实际来源识别和映射 |
| ST | [IdtState.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/IdtState.cpp) | 持久偏好读取/迁移、业务设置发布 |
| CF | [Other.Config.cppm](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Other/Other.Config.cppm) | Drawing.Eraser 字段声明 |
| UI | [Setting.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.cpp) | 产品设置与条件编译的开发调测 UI |
| UP | [Setting.Pages.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp) | 笔速橡皮页面的设备/计算方式选择 |
| TT | [speed_eraser_tests.cpp](D:/Project/Inkeys/Repo/Inkeys/InkeysHeadlessTests/speed_eraser_tests.cpp) | 场景/单位/状态机/回退/配置测试 |

## Findings

### 1. 四个正交维度：入口、真实设备、响应模型、尺度来源

不是简单的“鼠标/触摸/笔三套尺寸”。当前有四层：

1. **入口 InputEntry**：MouseLeft、MouseRight、Touch、PenTip、PenTail；每入口保存 Fixed/Speed，两个笔入口额外各存一个 PenResponseChoice。
2. **真实 SourceKind**：Unknown、Mouse、ExternalPen、IntegratedPen、Touch、TouchPad，以及识别来源、context/cursor/generation、mappedMonitor 和完整映射矩形。
3. **响应 ResponseModel**：IndirectDip、ScreenPenHybrid、DirectTouch；正式笔计算方式可覆盖自动模型，但不能篡改真实设备身份。
4. **动作 ScaleSource / MotionUnit**：DipOnly / DIP/s、TrustedPhysical 或 ManualCalibration / mm/s、ResolutionDpiHeuristic / reference DIP/s。直径属性另为 DIP，不与动作单位混合。

SH:9–58、99–115、199–237；SE:225–314。未发现生产中另一个独立 EraserDynamics 模块；动态状态由此 Controller 的 DynamicsState 持有（SH:361–426）。

| 实际来源及正式选择 | 自动响应 | 可信映射与物理尺寸可用 | 尺寸策略 |
|---|---|---|---|
| Mouse 左/右键 | IndirectDip | 仍用系统 DIP/s，不启用屏幕毫米测速 | 0.5B / B / 5B 的 DIP 动态尺寸 |
| ExternalPen（外置板），笔尖/笔尾选自动或 Tablet | IndirectDip | 仍用映射后 DIP/s | 同上，不受 EDID 或大屏选项切物理 |
| IntegratedPen（屏幕笔），笔尖/笔尾选自动或 ScreenPen | ScreenPenHybrid | 每轴 px→mm，速度 mm/s；标准尺寸以上有有界 beta 补偿 | 最终仍夹在 0.5B…5B DIP |
| Touch | DirectTouch | 每轴 px→mm，速度 mm/s；物理长边和显式模式共同选清扫阈值 | 动态目标直接 DIP；物理信息不把全部尺寸变成固定毫米 |
| Unknown / TouchPad（模型层） | IndirectDip | 不把未知/触控板当直接触屏 | 此模型存在不等于产品触控板导航已支持 |

正式 PenResponseChoice：Automatic=0、ScreenPen=1、Tablet=2；笔尖和笔尾独立，笔尾不是 SourceKind，依照 `inputType==Pen && inverted` 映射 PenTail（SE:196–214、248–258、408–423）。Win7 依 `GetPointerType` 是否可解析去掉正式 Auto，回退 Tablet；UI 可见索引转换回稳定枚举，不能把两项索引 0 当持久 Auto（ST:1210–1227、UP:1023–1037）。

生产 RTS 用 Pointer cursor 与 RTS capabilities 联合分类，冲突/重复匹配为 Unknown，Unknown 清空 mappedMonitor。驱动声称 integrated 但没有显示器映射的虚拟 HID 笔，在证据不冲突时按 ExternalPen；不是按当前屏大小猜笔类型（SE:84–114，RS:890–913）。

### 2. 配置、默认值和触发门

CF:463–474：`Drawing.Eraser.Automatic=true`、`BaseDiameterDip=32`、`Sensitivity=1`；五入口和两个响应最初为 -1（迁移哨兵）。ST:102–116、1215–1252 读取/迁移：正常缺省五入口 Speed，原来显式固定的用户偏好仅迁移普通左键/Touch/笔尖；旧右键/笔尾曾经硬编码 Fixed 不是用户偏好，不能据此强制迁移。

`ResolveInput` 优先级（SE:408–423）：

- 总 Automatic 关闭 → 全 Fixed，包括显式 Speed 工具；五入口已保存偏好不被覆盖。
- 总开关开启时：显式 Fixed 工具 → Fixed；显式 Speed 工具 → Speed；普通工具 → 各入口保存值。
- 解析有效 kind 后，各入口仍各自解析 source/penResponse；不会复制首个触点已经解析出来的响应模型。

普通产品橡皮通过 ConfiguredEraser/ByEntry。右键和笔尾的“可不可以临时切橡皮”另由工具和笔尾启用门决定，不能因该入口保存 Speed 就任意进入擦除：DC:14414–14420、14773–14824。笔尾与右键在适合的 Pen/HardPen/Highlighter/Shape 工具下临时进入 Eraser，PenTail 还需要 invertedPenEraserEnabled。

产品设置页读整表快照，在 `Drawing.Eraser` 修改五入口、两个笔响应、总开关/灵敏度；面积开关单独沿旧 Experimental 字段保存（UI:3745–3788）。总开关关闭仍允许编辑各入口偏好；笔响应和面积的显隐不会改保存值（UP:1001–1043）。

### 3. 桌面交互/大屏触摸模式究竟影响什么

产品全局 `paintDevice` 只有 0/1。UI 两项分别是 Touch 与 MousePen 对应的本地化模式，钳制到 0…1；修改后 SyncDraw3State + WriteSetting（UI:3829–3851）。Host 明确把 0→LargeScreen，其他→Laptop（HO:1836–1842）。首次启动用硬件缺省选择，后读用户配置可覆盖（[IdtMain.cpp:3357](D:/Project/Inkeys/Repo/Inkeys/Inkeys/IdtMain.cpp:3357)）。

内部 `DeviceMode::Automatic` 可按实测长边完全自动解析，测试使用它；当前产品模式 UI 没有第三个 Automatic 选项。这也与“笔计算方式 Automatic”是两件事。

**对物理 Touch 的影响（SE:11–14、336–365）：**

令 L 为两轴实际 mm/px 乘活动像素后的物理长边，S(x)=clamp(x,0,1)²·(3−2clamp(x,0,1))：

- 原始 w₀=S((L−320mm)/(1200−320)mm)。
- Laptop：w=min(w₀,0.25)；LargeScreen：w=max(w₀,0.25)；内部 Automatic：w=w₀。
- 入清扫 `90+260w` mm/s，出清扫 `60+190w` mm/s，大目标 `250+1050w` mm/s。
- fineToStandard 恒30mm/s，不把大屏精细操作也按比例变粗。
- 长边只选场景阈值，不改变实际 px→mm 换算。

| 例子 | 模式/权重 | 入/出/大目标 mm/s |
|---|---|---|
| 物理长边≤320mm小屏 | Laptop / 0 | 90 / 60 / 250 |
| 同一小屏手动选大屏 | LargeScreen / .25下限 | 155 / 107.5 / 512.5 |
| 长边≥1200mm教室屏 | LargeScreen / 1 | 350 / 250 / 1300 |
| 同一大屏手动选桌面 | Laptop / .25上限 | 155 / 107.5 / 512.5 |

解释：大屏一般手臂动作更快，普通拖擦不能一律落入最大清扫；显式场景能改变响应，但尺寸权重仍连续、有界。这是代码体现的设计意图，不是白板惯性已获批准的参数。

**对其他设备：**

- Mouse/ExternalPen/Unknown 的默认 IndirectDip：不因模式或 EDID 改动作标尺或阈值。
- 可信物理 ScreenPenHybrid：物理阈值固定，不用 Touch 长边权重；模式变化不产生 Touch 场景曲线。
- 直接模型无物理时：LargeScreen 可启用分辨率/DPI启发式；Laptop 正常严格 DIP 回退。所以即使是屏幕笔，丢失物理标尺后模式仍可能间接影响动作输入尺度。

### 4. 可信物理与 DPI 回退的实际入口

HO:1620–1704 由主绘制 HWND 的 monitor 和客户端矩形取 Display 快照。DIP/px=96/effectiveDpi；physicalAvailable 消费 `monitor.physicalSize.available`，不是原始 `edid.valid`。窗口客户端必须完整位于该 monitor，且 snapshot/monitor 非fallback，才 logicalOutputKnown；物理 cm/px=业务宽高cm/活动像素。

SE:265–295 还要求本来源的 mappedMonitor 与绘制 monitor 相等，left/top/width/height 完整相等。直接模型、映射正确、Automatic 标尺、physicalAvailable 才使用可信 EDID 业务标尺。两轴各自 mm/px；`rho=sqrt(mmX/dipX)*sqrt(mmY/dipY)` 是几何平均 mm/DIP，需有限且0.01…10；mmX/Y各自有限为正、像素宽高正。复制/拓扑/EDID 原始读取的可信政策由 Display 层提供，此文不把 physicalAvailable 扩写成“所有EDID绝对准确”。

手动标尺只在 development.scale=ManualSurface 有效：绑定当前 monitor、logicalOutputKnown、像素有效、宽高各5…1000cm，依校准与显示方向奇偶交换。正式路径仍要求来源映射正确；只有显式开发强制响应可绕过输入映射门（SE:273–290）。它不是通用持久屏幕校准功能。

| 未取得物理标尺时 | 动作标尺与阈值 |
|---|---|
| IndirectDip 任一模式 | 系统DIP；fine100，入800，出600，大目标1900 DIP/s |
| ScreenPenHybrid + Laptop（常规） | 系统DIP；fine80，入480，出320，大目标1400 DIP/s |
| ScreenPenHybrid + LargeScreen且logicalOutputKnown | reference DIP；上述80/480/320/1400 **数值不变但单位为reference DIP/s** |
| DirectTouch + Laptop（常规） | 系统DIP；fine100，入240，出160，大目标700 DIP/s |
| DirectTouch + LargeScreen且logicalOutputKnown | reference DIP；fine100，入120，出80，大目标400 reference DIP/s |
| 直接模型但logicalOutputKnown=false | 系统DIP，不用分辨率启发式；Touch240/160/700，ScreenPen480/320/1400 |
| ForceUnavailable 开发覆盖且logicalOutputKnown | 直接模型启用reference DIP；此条件也可覆盖Laptop的正常DIP回退 |

启发式（SE:186–194、305–313）：

`extent=min(max(pixelW*dipX,pixelH*dipY)/1920, min(...)/1080)`；`gain=1/clamp(extent,.5,4)`。动作每轴乘gain，范围.25…2，旋转不改变gain。它表达分辨率/系统DPI经验动作幅度，**不是EDID反推英寸，不得标mm/s**。

设置警告使用 `DirectInputUsesDpiFallback` 调用同一解析器检查绘制输出的直接输入能力；覆盖 DipOnly 和 ResolutionDpiHeuristic，monitor=0未发布不误报，已验证的手动校准不报；正常鼠标/板DIP不是退化（SE:386–406，UI:3760–3761）。

### 5. 尺寸、灵敏度与静态目标曲线

尺寸由 BaseSize B=24/32/40 DIP 派生：minimum=.5B、standard=B、maximum=5B、Touch新Down=.5B、Fixed=B；旧16→24、旧64→40。DIP直径除 `sqrt(dipPerPixelX*dipPerPixelY)` 成画布像素（SE:42–81）。这是显示视觉尺寸，与动作mm/s独立。

灵敏度 Low/Medium/High=0.85/1/1.15，仅在速度超过fineToStandard后：`s_sweep=fine+(s−fine)*gain`，中档原值；精细阈值、时间、Touch防点擦及面积测量不随之缩放（SE:51–54、464–469）。

目标（SE:471–499）：

- s≤fineHold：.5B。
- fineHold<s<fineToStandard：.5B→B，SmoothStep连续插值。
- s≥fineToStandard：以 `s_sweep` 算 t=S((s_sweep−sweepEnter)/(largeTarget−sweepEnter))，referenceTarget=`B*exp(t*ln5)`，即B→5B的对数目标。
- fineHold=.20·fine，fineRelease=.35·fine；不是同一阈值。
- 只有 ScreenPenHybrid 的标准以上增量作beta补偿：`B+clamp((.25/rho)^beta,.25,4)*(target−B)`，beta默认.5并限制0…1；最终夹.5B…5B。
- **DirectTouch 不用这个尺寸补偿**，其真实物理事实参与动作测速和场景选择。0.25mm/DIP是屏幕笔响应参考值，不是硬件测量事实，不能当白板惯性的通用毫米。

| 模型 | 可信物理 fine / 入 / 出 / 大目标 | 纯DIP回退对应值 |
|---|---|---|
| IndirectDip | 不进入物理 | 100 / 800 / 600 / 1900 |
| ScreenPenHybrid | 20 / 120 / 80 / 350 mm/s | 80 / 480 / 320 / 1400 DIP/s |
| DirectTouch | 30 / (90+260w) / (60+190w) / (250+1050w) mm/s | 100 / 240 / 160 / 700 DIP/s |

### 6. 实际动态不是“速度一变立即套目标”

状态是多组正交标志和连续证据，不是单个枚举状态机：

| 子过程 | 进入/推进 | 退出/抑制 |
|---|---|---|
| Touch起步锁 | 新Down尺寸.5B，记录最大离Down位移 | 物理1…3mm或回退2…6动作DIP平滑解锁；未解锁目标≤B，防点擦变大 |
| 精细稳定低区 | 140ms长窗路程速度；≤.2fine累积100ms进入fineHeld | ≥.35fine连续160ms释放；迟滞带只保留，不无限积弱证据；方向反转也有确认 |
| 清扫资格 | 有真实移动支持、速度越入清扫；出清扫阈值较低 | Hover预览不获清扫；无Move不续充；稀疏大间隔不充作连续快擦 |
| 清扫证据/增长cap | `strength=.4+.6*S(...)`，E按泄漏积分并夹full，E≥start才放行 | 每步实际动作和cap共同限制增长；目标很大不等于立即有资格增大 |
| 大尺寸保持/回缩 | 大尺寸/充分证据进入sweeping；较小目标需保持+确认后缩小 | DirectTouch/ScreenPen被cap拒绝的尖峰不清空已累计回缩；实际支持的增长可重置 |
| idle | 有效移动消失后到idleStart | 向精细下限/已接受面积floor收敛；晚到面积不能静止反向放大 |
| 非Touch离面 | 释放后只转交尺寸会话，不生成连接几何 | 未观察到新Hover时排除空档位移，真实时间仍衰减；新接触继承兼容会话 |
| 真断触接续 | Pause冻结OC候选 | Resume平移所有时间和位置基准，缺失位移不能算速度或解锁Touch |

测速不是白板惯性用的尾段线性拟合：记录最多64个原始 MotionSegment，路程在窗口内按重叠比例积分，`MotionSpeed=window内路程/window长度`，分母包含静止和无Move时间，往返路程不会互相抵消（SE:954–1028）。常规短窗：Mouse80ms、ScreenPen40ms、Touch50ms；参考运动窗160ms、fine窗140ms。移动空间门物理.2mm/回退.75动作DIP；超过80ms的单输入区间不当连续证据，输入缺失>1s不猜轨迹（SE:317–380、1320–1422）。

目标用 log直径 Follow：先按最大log变化率推进，再指数趋近，使用精确分段解，不因长帧跨过目标。AdvanceState以≤4ms子步积分证据与目标。sampleState为真实输入锚点、frameState为帧预览，帧Advance不反写已接受raw状态；迟到raw从sampleState重新积分（SE:27–38、1320–1429，SH:408–426）。

关键时间（单位秒，与屏幕尺寸/DPI不乘比例；SH:224–266及SE:315–375）：

| 动态参数 | IndirectDip | ScreenPenHybrid | DirectTouch |
|---|---:|---:|---:|
| evidence start/full/decay | .100/.240/.350 | .080/.200/.350 | .080/.180/.300 |
| growth tau 普通/较大 | .280/.160 | .200/.160 | .220/.180 |
| growth log rate 普通/较大 | 4/6 | 4/6 | 4/6 |
| hold 普通/sweep | .100/.650 | .080/.200 | .100/.650 |
| decrease确认 普通/sweep | .100/.680 | .080/.240 | .100/.680 |
| shrink tau 普通/sweep | .160/.300 | .120/.220 | .160/.300 |
| idleStart / idleTau | .280/.200 | .220/.160 | .280/.200 |

细区独立tau收缩.200/增长.260；面积主导Touch增长另保留.120/.100、log rate6/8，不受较重清扫增长阻尼影响（SE:1111–1120、1284–1291）。

### 7. 面积辅助不是设备尺寸适配

仅真实Touch、有正确来源映射、Touch接触且开关开启可用，需报告PROPERTY_METRICS明确可转换的CanvasPixels；不能按数字大小猜单位。实际转换用原始span分辨率/单位、axis分辨率/单位与既有位置线性变换，排除平移（SE:501–547、627–670）。

以稳定真实拖动至少50ms确认参考宽高；floor=`clamp(1.10*max(widthDip,heightDip)+6, B, min(5B,max(B,64)))`；参考锁存，不随摊开/重压无限增大。异常分轴判断，恢复需160ms稳定真实运动且不超过首次floor；缺包2s、显式无效200ms宽限、180ms衰减释放（SE:633–757，SH:151–158）。

它只提供受门控的拖擦下限，与速度目标取最大且仍要真实运动许可；不能给高速清扫证据充能。这里的Touch接触面积辅助与PalmEraser手掌识别是不同功能，都不是白板惯性应复用的运动阈值。

### 8. 生产会话与安全切换

- Touch每接触独立Controller，新Down从Touch精细尺寸起步；MouseLeft、MouseRight、PenTip、PenTail各自独立MouseLifecycle会话及ownerToken（DC:14381–14402、14422–14476）。虽类名MouseLifecycle，它实际用于全部非Touch入口。
- 非Touch Hover预览仅精细/标准范围，无清扫；接触Down可继承兼容预览，清速度段避免离面位置被当接触动作；Up转交真实已接受几何直径和时间，取消/固定/选区等清视觉会话（SE:885–951、1550–1623）。
- 同一物理接触批次按真实Down/Up QPC判断，后加入者沿首批显示/模式/偏好/面积快照，但每入口按自身source和penResponse重新解析。不能因本帧先消费旧Up就将早已重叠的新Down切到新标尺（SH:90–97，DC:14724–14809）。
- `SessionConfigCompatible`检查入口、context、实际来源冲突、monitor/边界/方向/DPI和双方已知映射，再比较有效动作标尺、尺寸与动态参数。generation/revision或识别补全不单独导致Reset；模式枚举和motionSource标签也不单独比较，但其造成的有效参数仍比较（SE:426–449）。
- 该函数给非Touch Configure/交还做安全判断；不能把“任何Display revision变化都停白板惯性”视为可复用策略。
- 选择模式不画橡皮Hover；Touch pan按住时抑制笔橡皮光标的采样/显示（DC:16168–16228），这不是笔靠近导致白板惯性减速的证据。

### 9. 开发覆盖与正式参数边界

`DRAW3_RTS_DIAGNOSTICS`编译条件下，实验页存在“笔速橡皮策略比较（临时，不保存）”：强制响应、手动当前表面cm或ForceUnavailable、beta .25/.50/.75、低成本诊断（UI:4755–4839）。手动默认28×18cm只是调测种子，绑定当前monitor/方向，换屏不得套用。唯一在该块仍持久化的是TouchContactAreaAssistance；正式绘制页也复用这个键（[IdtMain.cpp:3391](D:/Project/Inkeys/Repo/Inkeys/Inkeys/IdtMain.cpp:3391)）。

这些覆盖可改变有效模型但不伪造真实身份；白板惯性若复用DisplayScale必须隔离 `display.development.response / scale / penBeta`，不能被橡皮调测设置意外重调。

### 10. 已有测试合同与证据边界

TT:620–647 使用教室1920×1080、144DPI、139×78cm和小屏2880×1920、200%DPI、28×19cm夹具，验证Touch场景阈值；TT:636–638 的目标表如600mm/s→42.16DIP、1000mm/s→109.40DIP，是**参考目标公式**，不是时间积分后的实际尺寸。

TT:868–939 覆盖显式模式夹权重、自动连续性、旋转、手动、无物理/未知映射/强制回退；TT:1655–1694 覆盖真实来源分类、鼠标不被EDID/大屏改变、板/屏幕笔冲突、模型覆盖保留身份；TT:1471–1593 覆盖五入口、总门、Win7枚举及配置保存。TT:363起的FineBand回归、后段TouchArea/控制器基准证明存在对应测试，但本轮未执行，不声称新测试通过。

## 可为白板惯性复用的事实与不可照搬项

可以复用/借鉴：

- 来源身份与产品体验选择分开；屏幕笔/外置板分开，不把所有Pen都当显示器直接物理输入。
- Display业务物理可信状态 + 当前输入映射 + 实际输出一起判断，逐轴物理转换；DPI回退明确命名和诊断。
- 物理长边320…1200mm连续先验及显式场景限制可作为候选分类证据，需本轮用户决定，不是默认产品结论。
- 接触批次锁存，同单位内完整闭环；比较有效事实/参数，忽略无害revision和他屏变化。
- 时间常数不随像素/DPI缩放；精细操作与大动作响应分层；高频输入/帧观察分离。

不可直接照搬：

- 橡皮“路程/window”测速会把往返都计入，白板释放要方向和末段趋势；不能替换当前可靠尾段向量估速。
- 0.25mm/DIP、beta、B/5B和SweepActionSpeed是橡皮尺寸响应，不能当真实物理标尺或给惯性初速做无条件放大。
- Touch90…1300mm/s与Pen20…350mm/s阈值代表擦除意图，不能原样等同甩动画布动作阈值。
- 橡皮长边权重提高大屏“进入大橡皮”门槛，惯性大屏应滑更远/更慢还是保精确需要另行明确；同一权重不蕴含同一增益方向。
- 橡皮面积许可、断触冻结/离面尺寸继承和保持回缩，不是导航惯性状态。白板保留自己的Down中断、跨设备仲裁、多指拓扑与真实Up规则。
- ResolutionDpiHeuristic是有界启发式，可作为无EDID的候选体验策略，不能保证不同真实英寸屏物理手感相同。

## 相关规范

- [橡皮属性与配置](D:/Project/Inkeys/Repo/Inkeys/.trellis/spec/native-desktop/eraser-attributes.md)
- [手掌橡皮独立合同](D:/Project/Inkeys/Repo/Inkeys/.trellis/spec/native-desktop/palm-eraser.md)
- [输入与墨迹](D:/Project/Inkeys/Repo/Inkeys/.trellis/spec/native-desktop/input-and-ink.md)
- [当前白板导航](D:/Project/Inkeys/Repo/Inkeys/.trellis/spec/native-desktop/canvas-pan.md)
- [历史笔速橡皮设计](D:/Project/Inkeys/Repo/Inkeys/.trellis/tasks/archive/2026-09/09-12-speed-eraser-physical-scale/design.md)；本报告值以当前生产源码为准。

## External references

本主题由当前仓库事实可回答，未使用外部资料。Display/EDID平台API或跨系统体验的外部验证由其他主题覆盖，不能从此文推导硬件兼容保证。

## Caveats / Not Found

- 未读取当前运行进程配置/原始硬件快照；无法据本报告认定用户本机当前选择模式、真实mm/DIP或EDID可信程度。
- 本轮未运行编译、headless、隐藏窗口或真实Touch/Pen；只研究，不增加动态性能/最佳手感保证。
- 当前 SourceRecognition 与映射依已有后端/驱动证据，不能保证驱动提供的物理事实绝对真实。
- 归档早期设计包含已替代曲线/规则；特别是“Touch整体固定毫米尺寸”已不符合当前SE:493。历史数据必须标日期/实现阶段/是否真实输入。
