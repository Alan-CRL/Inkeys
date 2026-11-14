# Research: 显示物理尺度可信链与 DPI 回退

- Query: 当前 EDID、活动输出和输入映射如何共同决定物理标尺；哪些合同可用于白板惯性？
- Scope: mixed（当前生产源码、现有测试/规格、Microsoft 官方文档）
- Date: 2026-11-13
- 状态：仅静态研究；没有修改产品代码、编译、GUI、硬件查询或 commit。

## Findings

### 1. 结论

已有物理标尺链条可作为惯性的事实来源，但不能把 `EdidInfo::valid` 或 `DisplayScale::physicalAvailable` 单独解释为“这个接触已取得真实毫米”。实际还要确认当前绘图 HWND 的输出、来源是否直接作用于该屏，以及映射矩形是否完全一致。DPI 回退提供的是系统 DIP；大屏橡皮另有有界动作启发式，它不是物理密度。

当前“可信”是程序策略意义的可用状态，并不保证 EDID 厂商尺寸一定真实。现有解析不校验尺寸纵横比、不读取 `EDID_OVERRIDE`、不解析扩展尺寸。新惯性规划应明确这个能力边界，避免承诺没有证据的物理一致性。

### 2. 文件地图

| 文件 | 作用 |
| --- | --- |
| [Display.cppm](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Display/Display.cppm:31) | 拓扑、原始 EDID、业务物理尺寸、不可用原因及只读快照 |
| [Display.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Display/Display.cpp:233) | CCD 活动目标、SetupAPI 精确设备匹配、注册表 EDID、解析、刷新发布 |
| [Draw3.Host.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1620) | 将实际绘图输出的同代显示事实发布给 Draw3 |
| [Draw3.RealtimeStylus.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:424) | RTS 能力、Pointer cursor 身份和屏幕映射缓存/重建 |
| [Draw3.SpeedEraser.h](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h:22) | InputSource、DisplayScale、开发覆盖与尺度单位 |
| [Draw3.SpeedEraser.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp:225) | 来源映射、物理密度校验、手动标定和回退解析 |
| [Draw3.PalmEraser.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PalmEraser.cpp:30) | 可信毫米面积与 10000 系统 DIP² 回退 |
| [PenWidthPolicy.hpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Business/PenWidthPolicy.hpp:19) | 系统 DIP 笔宽和大屏分辨率策略；不使用 EDID |
| [Draw3.DrawingController.cpp](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2633) | 当前惯性只取 DIP/px；本任务需补充物理上下文边界 |

### 3. Display 的原始事实与业务可用状态

`EdidInfo.valid` 仅表示原始 EDID 成功解析；`PhysicalSizeInfo.available` 才是经过拓扑/映射/尺寸判定的业务状态。结构明确分开，原始数据含完整 rawBytes、原始方向的整数厘米、设备路径/实例 ID、EDID 版本。见 [Display.cppm:61](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Display/Display.cppm:61)。

获取链条：

1. `QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)` 取得活动路径；容量变化时最多重新查询四次，失败返回 unavailable。每个路径保留 source/target 的 adapter LUID 与 ID，分别取得 GDI source 名称、target monitorDevicePath。见 [Display.cpp:311](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Display/Display.cpp:311)。
2. `ReadMonitorEdid` 枚举当前 monitor interfaces，大小写不敏感完整匹配 target 路径，再在该设备 hardware key 查询 `EDID` REG_BINARY；并非按产品名、分辨率或主屏猜设备。见 [Display.cpp:233](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Display/Display.cpp:233)。
3. `ParseEdid` 要求至少 128 字节、标准 8 字节头、基础块 checksum 正确；记录 bytes[18]/[19] 版本，bytes[21]/[22] 为原始厘米。没有对版本是否受支持、扩展块 checksum、详细时序毫米或 DisplayID 尺寸进行进一步解析。见 [Display.cpp:1068](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Display/Display.cpp:1068)。
4. 同 adapter+sourceID 被多个目标使用即 `CloneOrMixed`，包含部分复制+部分扩展；不是比较分辨率来猜复制。见 [Display.cpp:1090](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Display/Display.cpp:1090)。
5. 逻辑屏通过 GDI deviceName 与 target sourceDeviceName 匹配。`mappingSetReliable` 要求 Single/Extended、屏数=target 数、全体 target 和逻辑屏都一一对应；任何映射集合歧义会令全局传入的 uniqueTarget=false。EDID 单屏失败则不污染其它屏的映射集合。见 [Display.cpp:705](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Display/Display.cpp:705)。

`ResolvePhysicalSize` 按下表优先级判定，失效时业务宽高均为零；成功后只对当前 90/270 度方向交换业务宽高，原始 EDID 不改。见 [Display.cpp:1104](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Display/Display.cpp:1104)。

| 条件（依次判定） | 业务结果 |
| --- | --- |
| 显式快照 fallback | SnapshotFallback |
| 活动拓扑 Unknown | TopologyUnknown |
| 复制/部分复制 | CloneOrMixed，全局禁用 |
| 全集合映射不是一一对应 | DisplayTargetAmbiguous |
| EDID 未找到 / 读取失败 / 解析失败 | EdidUnavailable / EdidReadFailed / EdidParseFailed |
| 任一厘米边为零 | MissingDimensions |
| 任一厘米边 <5 | DimensionsBelowMinimum；恰好 5 cm 可用 |
| 其余 Single / Extended | available=true；扩展中某屏 EDID 失败仅禁用该屏 |

这里没有非零但明显错误尺寸、逐轴密度、尺寸纵横比、目标物理尺寸与当前活动图像区域的额外可信校验。不可把 `available=true` 等同“厂商数据已测量验证”。

### 4. 系统 DPI 与显示输出绑定

`QueryMonitorDpi` 动态调用 `GetDpiForMonitor(monitor,0,...)`，0 为有效 DPI；失败则用指定显示 DC 的 LOGPIXELSX/Y，初值 96。见 [Display.cpp:371](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Display/Display.cpp:371)。因此 `dipPerPixel=96/effectiveDpi` 是 Windows 逻辑尺度；`mmPerPixel` 应来自可信物理宽高除实际像素宽高，两者不可互相冒充。

Host 的 `PumpDisplayScale` 使用 `MonitorFromWindow(实际 attached Drawpad HWND, NEAREST)` 后 `snapshot.Find(monitor)`；不是 `snapshot.Primary()`。输出宽高是全幅 monitor bounds，DIP/px 分轴计算。`logicalOutputKnown` 同时要求快照/monitor 不是 fallback、客户区矩形已知且完全包含在该 monitor 内；跨两个屏的 HWND 不获物理输入映射资格。见 [Draw3.Host.cpp:1638](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1638)。

Host 仍可发布 `physicalAvailable=true` 而 `logicalOutputKnown=false`，所以后续消费者必须继续做映射检查。没有显示快照匹配时可由 HWND DPI 回退，但不制造厘米。

### 5. 来源是屏幕直接动作，还是间接鼠标/数位板

`ReadPointerSources` 通过动态加载 `GetPointerDevices`、`GetPointerDeviceCursors`、`GetPointerDeviceRects` 取 pointer 类型、cursor ID 和目标 screen rect。屏幕映射保存 HMONITOR+left/top/width/height；不会从“只有一块屏”推断。API 不可用、枚举失败/过量、cursor 重复时保守处理。见 [Draw3.RealtimeStylus.cpp:424](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:424)。

RTS `BuildContextDecoder` 取得实际 TabletDeviceKind、THWC_Integrated、现代 API 可用位，缓存 pointerSources；`SourceForCursor` 将 cursor 唯一匹配与 RTS 能力交叉校验，再带 contextId/cursorId/generation 发布。见 [Draw3.RealtimeStylus.cpp:890](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:890)、[Draw3.RealtimeStylus.cpp:944](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:944)。

| 真实来源/证据 | 当前分类/尺度注意事项 |
| --- | --- |
| MouseLeft / MouseRight | Mouse，间接 DIP；不应用显示器毫米来解释鼠标动作 |
| 现代 API+唯一 cursor IntegratedPen，且 RTS 能力不冲突 | IntegratedPen；只有精确映射输出时能消费该屏毫米 |
| ExternalPen 或 Integrated 虚拟 HID 有 screen rect 但 monitor=0 | ExternalPen，保持间接 DIP；后者是已有保守纠偏 |
| Touch，RTS 集成能力/Pointer 类型一致 | Touch；仍要通过全幅输出映射检查 |
| TouchPad | 独立识别，不冒充 Touch；本任务继续不启用触控板 |
| Win7 / 能力不足 / cursor 歧义或冲突 | Unknown（Mouse 例外）；不自动宣称直接物理输入 |

分类规则证据：[Draw3.SpeedEraser.cpp:84](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp:84)。笔尾与笔尖是不同输入入口偏好，不是两套独立物理表面识别；inverted 不令外置板变成屏幕笔。

### 6. 当前橡皮三类尺度合同不同

**笔速橡皮：** `ResolveConfig` 先确定 IndirectDip / ScreenPenHybrid / DirectTouch；间接来源始终以系统 DIP 动作。直接响应要求 `logicalOutputKnown`、source.monitor 非零且等于输出 monitor、四个 rect 值全等。可信 EDID 路径再要求 `development.scale==Automatic && physicalAvailable`。分轴 `mmX=10*cmPerPixelX`、`mmY=10*cmPerPixelY`；宽高像素为正，mm 两轴有限且正，`rho=sqrt(mmX/dipX)*sqrt(mmY/dipY)` 处于 [0.01,10] 才启用 mm/s。见 [Draw3.SpeedEraser.cpp:248](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp:248)。

开发 ManualSurface 可在 monitor/尺寸/方向匹配时提供毫米；尺寸允许 5..1000 cm。只有开发 response 被强制时可以绕过真实 source 映射资格，但不会修改真实 source 身份。惯性不应自动继承这个橡皮专用覆盖。当前 physicalAvailable 不是唯一事实入口。

物理无效后：普通模式用 DIP；直接响应且 LargeScreen（或 ForceUnavailable 调测）且 logicalOutputKnown 时，用 `ResolutionDpiHeuristic`，并标 `HeuristicPerSecond`。其动作增益为：

```text
extent = min(max(W_DIP,H_DIP)/1920, min(W_DIP,H_DIP)/1080)
gain = 1 / clamp(extent, 0.5, 4.0)      // [0.25, 2.0]
motionPerPixel = systemDipPerPixel * gain
```

W/H 为当前全幅像素乘各自 DIP/px，不是假定英寸。见 [Draw3.SpeedEraser.cpp:186](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp:186)、[Draw3.SpeedEraser.cpp:303](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp:303)。这个 gain 是橡皮动作响应先验；将它直接乘到按住拖动位置会破坏 1:1，不能照搬到平移几何。

**手掌橡皮：** 复用 SpeedEraser 的映射/旋转/物理校验，但只接受实际 Touch（旧 Unknown 仅 DPI 回退）；显式 Mouse/Pen/TouchPad 无资格。可信时用 2500mm²（25cm²）面积；不可信时用 10000 系统 DIP²，再分轴除 DIP/px 转 px²。不沿用速度 gain 或速度面积辅助的尺寸上限。见 [Draw3.PalmEraser.cpp:30](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PalmEraser.cpp:30)、[Draw3.PalmEraser.cpp:61](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PalmEraser.cpp:61)。

**普通笔宽：** 显示 Context 只含 effectiveDpi 与全幅像素、paintDevice。普通笔预设是系统 DIP；大屏 preset 用旋转对称的 1920×1080 像素参考，再除 systemScale。Laser 单独 DIP，不使用 EDID。显式 DIP 选择不随屏幕改写。见 [PenWidthPolicy.hpp:49](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Business/PenWidthPolicy.hpp:49)。这证明“物理毫米”和“画面相对尺寸”在当前产品中是有意分开的合同。

### 7. 刷新、热插拔与切域

Display observer 对 WM_DISPLAYCHANGE、WM_DEVICECHANGE、WM_SETTINGCHANGE 触发刷新；枚举失败会保留上一快照几何/DPI/原始诊断数据，但把 topology 改 Unknown，撤销物理尺寸。等价快照不增 generation；任务栏更新也可能增 generation。见 [Display.cpp:1034](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Display/Display.cpp:1034)、[Display.cpp:1455](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Display/Display.cpp:1455)。

Host 的 Display 订阅只发布 pendingSnapshot 并唤醒，绘制 owner 低频 `PumpDisplayScale`；HWND 的位置/DPI/display 消息也设 dirty。`DisplayScale.revision` 在 scale 或 client rect 变化时递增，development 变化、别的显示器引发的 snapshot.generation 变化同样可能推进 revision；不能把 revision/generation 任意增长都当作当前输入单位变化。见 [Draw3.Host.cpp:1698](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1698)、[Draw3.Host.cpp:2544](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:2544)、[Draw3.Host.cpp:3072](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:3072)。

RTS `UpdateMapping` 会清光标、关闭旧 producer contacts、清 decoder cache、重建含 pointer 映射的整代 decoder；TabletRemoved 同样重建，TabletAdded 优先增量，失败全重建。见 [Draw3.RealtimeStylus.cpp:1479](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1479)、[Draw3.RealtimeStylus.cpp:1542](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1542)、[Draw3.RealtimeStylus.cpp:1700](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1700)。

静态可证边界：Display/Host 和 RTS 映射生命周期是分别发布的，没有共同的代际事务，也没有在 Host 每次收到 display 变化时主动重读全部 pointer 映射。几何不匹配时现有精确矩形检查可退化，但不能据此声称所有驱动都会即时发 RTS UpdateMapping 或两个通知顺序已硬件验证。新惯性应记录并区分 `display generation` 与 `source generation`，在不兼容时停止旧手势；不要仅将两个计数比较相等，它们不是同一个时钟。

笔速橡皮已有 `SessionConfigCompatible`：比较实际输出/密度/动态参数，忽略无害版本/识别补全。它的“有效签名”思想可复用；完整橡皮 Config 不宜直接作为惯性 Config。见 [Draw3.SpeedEraser.cpp:426](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp:426)。

当前平移只接 `CanvasPanScale{dipPerPixelX,Y}`，首个无接触且无惯性批次锁存；本身尚未解析来源映射/毫米。`Stop` 已能标记本批旧接触 suppressed、保留到终态，适合作为将来单位变化屏障。见 [Draw3.DrawingController.cpp:2633](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2633)、[Draw3.DrawingController.cpp:2839](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2839)、[Draw3.DrawingController.cpp:2880](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2880)。

### 8. 静态可证的可信边界与待定项

| 项 | 当前代码事实 | 对惯性规划的含义 |
| --- | --- | --- |
| EDID 厂商尺寸真实性 | 只校验格式/checksum和>=5cm；有限 checksum 不能发现语义错误 | 诊断中称“通过当前可信策略”，不承诺测量精度；异常数据保守回退策略需要独立定义 |
| 异常纵横比/各向差 | Display 无检查；SpeedEraser 只对几何均值 rho 给很宽范围 | 两轴保留，不能先平均密度；不可无依据用狭窄像素纵横比阈值误伤非原生模式 |
| EDID 精度 | 基础块尺寸整数 cm，未读详细时序毫米或 DisplayID | 小屏物理标定存在固有量化；不承诺与旧 DIP 逐位相同 |
| EDID_OVERRIDE | ReadMonitorEdid 只查 EDID；仓库未找到 override 合并逻辑 | 记录保守来源限制；若要求使用系统有效 EDID，需另行评审补齐读取/合并，不能静默称已支持 |
| 图像区域与面板 | 使用业务厘米 / 全幅当前像素，没有保留 target scaling/letterbox 区域 | 通常满屏映射成立；黑边/缩放等不能凭现有快照保证物理映射精确，不应为此顺手重构 Display |
| 外置板 | 正常是 IndirectDip；其设备动作毫米不是显示器毫米 | 不能用 EDID 解释板面手程；保留单独间接设备手感 |
| Win7 | Pointer APIs 动态缺失时不自动判直接物理来源 | DPI 回退可用；不新增硬导入或扩大 Win7 自动物理识别承诺 |
| 调测覆盖 | ManualSurface/ResponseOverride 可影响橡皮解析 | 惯性默认不继承；共享显式标定需用户产品决策 |
| 跨屏/热插拔 | 有有效输出检查和两条独立通知链 | 首 Down 锁存，实际语义变化先 Stop，再等新完整手势；无关显示/诊断更新不能误停 |

### 9. 可复用的最小事实边界（建议，未实现）

建议形成纯事实解析结果：实际 monitor/bounds/像素/方向、系统 DIP/px、输入 source identity/rect、可信 mm/px、来源与失败原因。不在热路径硬件查询；不把 SpeedEraser 的 threshold、penBeta、touch profile weight、sensitivity、面积或开发 response override 混入惯性。

可保持两层：Display 继续负责拓扑和业务物理尺寸；小型 shared helper 负责来源映射+毫米轴校验，橡皮和惯性分别使用各自响应。若不提取 helper，也可由惯性只消费相同事实字段，但必须通过测试锁住规则一致性。当前 EDID_OVERRIDE/异常尺寸策略尚未获用户范围批准，不应借本任务隐式改写整个显示服务。

惯性上下文签名应包括真正影响单位/参数的输出、系统DPI、有效物理密度、旋转、映射、输入类型/本批来源及最终模式；原始 snapshot generation、debug flag、无关显示器变化不是必然停止原因。单位或物理依据改变时，旧估速样本、boost residual、fling 不得带入新域；内容拖动需同一变换正反闭环，以维持按住时 1:1。

### 10. 现有测试证据与后续覆盖

本轮只读测试，未运行。已有：

- [display_tests.cpp:556](D:/Project/Inkeys/Repo/Inkeys/InkeysHeadlessTests/display_tests.cpp:556)：128 字节/头/checksum、原始字节保留、0/4/5cm、旋转、single/extended/clone/mixed、单屏失败与映射/fallback。
- [palm_eraser_tests.cpp:95](D:/Project/Inkeys/Repo/Inkeys/InkeysHeadlessTests/palm_eraser_tests.cpp:95)：各向面积积、来源排除、六种 mapping 不符、无物理时 10000 DIP²。

新增惯性阶段应覆盖相同物理轨迹在 DPI/分辨率/旋转变化下的门槛/速度/滑行等价，以及失效切 DIP、旧残余清理、同屏无关 revision 不误停、异向单位方向闭环、来源不匹配和跨屏/RTS 先后通知。需以实际调用生产 resolver 的测试覆盖，不能只重写数学公式。

## External references

截至本次研究阅读的官方 API 文档：

- [GetDpiForMonitor](https://learn.microsoft.com/en-us/windows/win32/api/shellscalingapi/nf-shellscalingapi-getdpiformonitor)：effective DPI 受用户系统缩放和调用方 DPI awareness 影响；API 不可直接当作真实 mm 密度，现代 per-monitor-aware HWND 应考虑 GetDpiForWindow。这里仅说明当前实现边界，不建议在本次规划内顺便替换显示服务。
- [GetPointerDeviceRects](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerdevicerects)：分别返回设备 HIMETRIC 范围和其映射显示器当前分辨率范围；最低 Windows 8。现有代码取后者确认显示输出映射，没有使用外置板 HIMETRIC 作为屏幕毫米。
- [GetDeviceCaps](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getdevicecaps)：缺失驱动数据时会固定公式推算；错误驱动值也会原样返回。因此 HORZSIZE/VERTSIZE 不应成为绕过 EDID 的额外“可信”尺寸捷径。
- [Using an INF file to override EDIDs](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/overriding-monitor-edids)：Windows 可逐 128 字节块使用 hardware-key EDID_OVERRIDE 替代 EEPROM 对应块，registry override 优先。当前源码没有显式读取/合并该子键，不能承诺拿到与 Windows 完全一致的有效 EDID。

## Related specs

- [cpp-conventions.md:84](D:/Project/Inkeys/Repo/Inkeys/.trellis/spec/native-desktop/cpp-conventions.md:84)：Display 物理标尺、原始 valid 与业务 available 分离。
- [palm-eraser.md](D:/Project/Inkeys/Repo/Inkeys/.trellis/spec/native-desktop/palm-eraser.md)：物理面积与 DIP² 回退。
- [pen-width.md](D:/Project/Inkeys/Repo/Inkeys/.trellis/spec/native-desktop/pen-width.md)：系统 DIP、全幅分辨率及同代 Context。
- [canvas-pan.md](D:/Project/Inkeys/Repo/Inkeys/.trellis/spec/native-desktop/canvas-pan.md)：当前导航/释放/停止合同。
- [09-12 显示物理尺寸任务 design](D:/Project/Inkeys/Repo/Inkeys/.trellis/tasks/09-12-display-physical-size-validity/design.md)：历史要求；任务 metadata 仍 in_progress 不表示源码尚未落地，本报告以当前代码确认实际能力。

## Caveats / Not Found

- 没有读取当前用户机器 EDID、显示拓扑、物理长边、rho、输入设备标识；没有硬件实测数据，不能生成参考标定常量。
- 未发现 EDID_OVERRIDE 合并、详细时序/DisplayID 尺寸解析、纵横比异常策略或 Display 与 RTS 共用的发布事务。以上是静态能力边界，不是已重现用户故障。
- 不把额外可信增强、手动标定 UI、显示服务重构或新后端作为本任务当然范围；仍需主会话与用户收敛。
