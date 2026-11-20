# 第4步 · Inkeys3 的 WM_POINTER 绘制输入与白板触控板拖动

2026-11-19。调查基准为本地 `refactoring/draw` 的 `0f1584e1362a4b74dd06f9e78c8b451fdeaff8ff`。11 月 12 日的路线图用于对照；本文按大屏触控模式和操作系统记录启动策略。结论来自生产源码、微软接口文档和下列实现对照，尚无同机双后端延迟或触控板共存实测。

## 1. 产品决策与系统边界

**可行。** Windows 8 起可以为笔和触摸屏新增 WM_POINTER 绘制后端，沿用 Draw3 的触点协调、固定帧消费和 Ink Stroke Modeler。选择绘制后端与取得触控板双指导航是两次独立的能力判定。只增加 Pointer 消息处理，白板不会自动获得拖动能力。

设置 → 性能 → 输入方式保留 **自动（推荐）／RealTimeStylus／WM_POINTER／鼠标兼容输入**。按以下优先级解析启动后端：选中“大屏触控模式”时，各系统均使用 RTS；其余场景尊重手选后端，选择“自动”或旧配置缺少该项时按下表。大屏模式生效期间，设置页显示“由大屏触控模式指定为 RealTimeStylus”，保留原偏好，退出大屏模式后恢复。这里的 Win8 专指 Windows 8.0。

| 系统 | 大屏触控模式 | 其他模式选“自动” | 白板触控板双指拖动 |
|---|---|---|---|
| Windows 7 SP1 | RTS | RTS | 低于手势最低要求。 |
| Windows 8.0 | RTS | RTS | 低于手势最低要求。 |
| Windows 8.1 | RTS | WM_POINTER | 以 PTP + DM 为目标；现有显式命中路径需在 8.1 真机验收。实际使用 RTS 时不提供该手势。 |
| Windows 10 | RTS | WM_POINTER | 通过 PTP + DM 接入；实际使用 RTS 时不提供该手势。 |
| Windows 11 | RTS | RTS | 在 RTS 下验证窗口级触控板注册；注册或同窗共存不可用时保留 RTS，单独报告手势不可用。手选 Pointer 时可走 DM。 |

在 Win8.1 和 Win10 引入 WM_POINTER，主要目的是让白板能通过 Direct Manipulation（DM）接收精确式触控板（PTP）的双指平移。目前没有同机证据表明 WM_POINTER 在书写延迟或轨迹质量上有显著的普遍优势。Win11 的自动选择不依赖是否检测到 PTP。绘制工具不启用双指平移，触控板按住主键产生的普通鼠标拖动按现有鼠标输入处理。

**白板“拖动”选项上方的 ⓘ 警告条目。** 输入初始化完成后，当系统低于 Windows 11，且实际运行后端为 RTS 扩展或 RTS 基础 XY 时，显示以下文案：

> ⓘ 当前使用的是 RealTimeStylus 消息输入，不兼容触控板白板拖动。

自动选择、手动选择、大屏模式指定、Pointer 初始化失败后回退到 RTS，均按同一条件判断。实际后端切到 Pointer 或鼠标时移除该条；Win11 不显示这条针对旧系统的警告。白板“拖动”选项的常规说明保留“双指拖动白板需要 Windows 8.1 或更高版本及精确式触控板”。Win11 的 RTS 接口探测失败时，可单独提示“当前系统或设备无法在 RealTimeStylus 模式下使用此手势”。

区分设置偏好、启动选择与实际后端。RTS 启动沿用扩展 → 基础 XY → 鼠标的既有回退；自动选择 Pointer 的系统在其初始化失败时回退 RTS，再沿用现有回退链。记录成功启用的后端及回退原因，单包缺少压力或倾角不触发切换。切换时先结束旧触点与惯性，再开放新入口。Win7 启动所需模块应动态解析较新系统接口。[PTP 平台概览](https://learn.microsoft.com/en-us/windows/win32/input-precisiontouchpad/precision-touchpad-portal) · [DM 命中消息版本](https://learn.microsoft.com/en-us/windows/win32/inputmsg/dm-pointerhittest)

## 2. 九项 RTS 属性如何接到同一份输入

当前 `Draw3.RealtimeStylus.cpp:55` 请求 X、Y、法向压力、X/Y 倾角、方位角、仰角、宽和高。设备实际返回的 GUID、顺序和 metrics 决定可读字段。Pointer 的标准结构覆盖现有书写所需的语义，但不能保证逐设备、逐字段、逐精度等价。尤其是仅报告方位角／仰角的笔，两个 tilt mask 缺失时，标准笔结构不能直接替代那一对字段。[RTS 属性描述](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-getpacketdescriptiondata) · [笔结构](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_pen_info) · [触摸结构](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_touch_info)

| 数据 | RTS 当前生产路径 | Pointer 转换及消费方 |
|---|---|---|
| X、Y | 包坐标乘共享位置倍率，生成浮点画布位置 | 使用原始位置。以带符号的 `ptPixelLocationRaw` 屏幕像素映射至绘制 HWND 客户区／画布。它是整数，慢速细笔可能损失亚像素。高精度笔可用 `ptHimetricLocationRaw` 与该 sourceDevice 的设备／显示映射矩形做浮点仿射映射，再减窗口原点；以 raw 像素校验偏移、负坐标、转屏与映射边界，失败退像素。不要用固定 96 DPI 换整块虚拟桌面，也不要将系统预测的触摸位置再次送入笔迹预测。 |
| 法向压力 | 笔按逻辑上下限线性归一化并限幅，无效范围给未知；笔尾有单独的压力抑制策略 | `PEN_MASK_PRESSURE` 有效时 pressure / 1024.0，合法零保留，缺失给 −1。例：512 → 0.5，但无 mask 的默认 512 不能当实测。标准字段只有 0…1024，高分档 RTS 压感会量化。触摸可有独立 pressure mask；现有产品只解码笔压力，第一版沿用。 |
| X/Y 倾角 | 读取各自单位及 resolution，再由双轴投影算总倾角和方位 | 两个 tilt mask 都有效时将度转为弧度，再按下文公式转换。只缺一轴时另一轴不能唯一确定三维姿态；缺失字段保留未知。 |
| 方位角、仰角 | 两者均有效时优先于双轴倾角 | 标准笔结构无这两个直接字段，可由双轴推导等价姿态。`rotation` 是笔绕自身轴的扭转。若要覆盖仅有方位／仰角的特殊设备，可枚举 `GetPointerDeviceProperties`，对实际可读的 HID usages 试用 `GetRawPointerDeviceData`，核对单位、范围和轴义；驱动未暴露的字段保持未知。 |
| WIDTH、HEIGHT | 生产路径只对 Touch 将设备宽高按轴／宽高各自的长度单位、resolution 和 context 倍率换成画布像素 | `PT_TOUCH` 且 `TOUCH_MASK_CONTACTAREA` 有效，取 `rcContactRaw` 的 right−left、bottom−top；这是原始屏幕接触包围框的两个像素长度。用与位置一致的线性部分换画布长度，平移不作用于宽高。矩形退化或缺失则走面积不可用分支。无需 RTS metrics、无需再次乘 DPI。标准笔结构没有笔尖接触面积。 |

Pointer 原始 HID 读取受报告布局限制。HID、RTS 和 Pointer 对同名角度的方向也可能不同，不能用一张符号表通吃。[坐标、时间与设备来源](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_info) · [设备与显示矩形](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerdevicerects) · [HID usages](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/supporting-usages-in-digitizer-report-descriptors)

**Modeler 的边界。** 本仓库 `Inkeys/additional/ink_stroke_modeler/types.h:141` 接受事件、位置、时间、压力、倾角和方位角。总倾角 0 为垂直、π/2 为贴屏，方位从 +X 逆时针，未知姿态或压力用负值。生产的 Down 在 `Draw3.DrawingController.cpp:17751` 附近以笔划时间 0 构造 Input，后续根据 QPC 起点换秒并做单调／显示时间处理，部分路径保留上次有效姿态。触摸宽高另供手掌／速度橡皮使用。触摸结构的 orientation 表示接触方向，不能填入笔方位角。下游 `Draw3.SpeedEraser.cpp:718` 还检查 raw 宽高是否正有效，所以 Pointer 需要同时提供来源为屏幕像素的原始宽高、画布宽高及有效单位状态；只填换算结果会使有效样本遭拒。

**补齐生命周期。** 按 PT_PEN／PT_TOUCH 分别取扩展结构，hover 供光标，接触阶段才发布 Down/Move/Up。区分 PEN_FLAG_INVERTED、ERASER 和 BARREL，按工具策略解释，保留笔尾擦除及触觉／光标通知。每个 pointerId 只在本次生命周期内使用，sourceDevice 保留完整句柄，映射到已有来源和稳定触点键。取消、捕获转移、设备移除和后端退出都须关闭相应触点。真实鼠标仍须能画，已消费的笔／触摸所提升的鼠标事件须去重，不能让一次触摸产生两条线。

### 何时向 Ink Stroke Modeler 传入 `-1`

`Input` 中允许用负值表示未知的是 pressure、tilt 和 orientation。应先在 ContactSnapshot 中保留**该包实际提供了什么**，然后按笔划状态生成 Modeler 输入；这两个时刻的值可能不同。

| 情况 | 为什么缺值，以及如何处理 |
|---|---|
| 压力 mask 缺失 | 应用无法区分设备未提供压力和结构中的占位数值。字段记为 −1。mask 有效时 0 是有效的零压力，512 是半压，两者都应保留。倒置笔抑制压力是 Draw3 已有的工具策略，可让一个有效的硬件读数在模型入口变为 −1。 |
| 仅报告一个倾角 | 例如 tiltX=0°、tiltY 未报告，笔既可能垂直，也可能向上或向下倾斜 45°。同一观测对应多个总倾角和方位。没有另一轴或可靠的替代姿态数据时，两项记为 −1；不能将缺轴填零。 |
| 两轴都有效且均为 0° | 明确知道笔垂直，tilt=0。笔轴在屏幕上的投影长度为零，方位没有几何定义。快照可注明方位未知。模型入口若已有本笔有效方位，可沿用；首点传 −1 时须计入下述整笔影响。这是字段策略需要专门验收的边界。 |
| 设备只给方位／仰角，或只给 rotation | 标准笔结构没有直接的方位／仰角字段。能通过设备属性和原始报告取得并核准的，可转换后使用；无法取得时姿态未知。rotation 是沿笔轴自转，例如保持笔身向右倾斜并转动笔杆，rotation 会变，笔方位仍向右。触摸接触面的朝向同样不能代替笔方位。 |
| 输入不携带笔属性 | 普通鼠标没有笔压或姿态；触摸可能有独立的压力报告，但当前 Draw3 只对笔启用硬件笔压。缺少接触面积走面积状态和橡皮回退。Modeler 没有宽高、接触面积、笔杆旋转或倒置标志槽位，无需为这些字段构造 −1。 |

**缺一次与缺整笔的区别。** Draw3 普通新笔划的 Down 直接传入姿态。后续适用路径用 `KeepLastValidStylusValue`／`KeepLastValidOrientation` 按字段保留本笔最近有效值。因此，压力依次为 0.6、缺失、0.8 时，快照如实记录缺失，模型可依次收到 0.6、0.6、0.8。新笔没有前值时保留 −1，不借用上一笔或另一支笔。当前绑定的 `StylusStateModeler::Update` 一旦真正收到某字段为负，就设置该字段的 unknown 标记；`Project` 此后对该字段输出 −1，直到该模型状态 Reset。即使下一包有效，也不会自动恢复该字段的投影输出。压力、倾角、方位三者各自标记，位置轨迹仍可建模。硬件压感直径函数遇到未知压力使用基准直径。

**应列入验收的例子。** 分别测试“首包缺压力、第二包有半压”“中途偶缺后恢复”“垂直起笔后再倾斜”。同时记录原始 mask、快照、实际传入的 Input 和投影 Result。若产品要求首包未知后本笔仍能恢复该字段，应另行设计逐字段恢复或有效性策略；仅在 Pointer 解码器补读后续样本无法改变当前 Modeler 行为。位置、时间和事件类型是构建轨迹的必需输入，位置无效时不发该 Move 并确保终态可清理，时间戳缺失沿用既有到达时钟策略。

代码依据为 `Inkeys/additional/ink_stroke_modeler/internal/stylus_state_modeler.cc:29、76、222`、`Draw3.DrawingController.cpp:345、17751、18163`、`Draw3.InkPrediction.cpp:316`。[微软笔结构及字段有效性](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_pen_info)

## 3. 第四步必须修正的 RTS 姿态

1. **双轴倾角路径存在可定位的 Y 方位符号问题。** 微软 Tablet PacketProperty 定义 RTS 正 Y 为远离用户，Pointer 正 Y 为朝向用户。Modeler 方位从屏幕向右逆时针计。以自然横向、屏幕上方远离用户为例，令 x = tan(tiltX)、y = tan(tiltY)，总倾角均为 atan(hypot(x,y))；Pointer 方位用 wrap(atan2(−y,x))，按 RTS 规范则应为 wrap(atan2(+y,x))。生产 `Draw3.RealtimeStylus.cpp:172` 的 RTS 双轴分支用了负号。RTS 输入 (0°, +45°) 按规范应得 tilt=45°、orientation=90°，现代码给 270°；对 Pointer 同样的数字，270° 合理。现有模型器测试项目 `inkStrokeModelerTestTests/contact_input_tests.cpp:480` 恰好断言了 270°，不能用来证明真实物理方向。建议记录笔分别向屏幕四边倾斜时两后端的原值，覆盖旋转屏；仅校正有问题的解码分支。±90° 需极限处理，垂直笔的投影无方向，可保留上个有效方位或给未知。[RTS 轴定义](https://learn.microsoft.com/en-us/windows/win32/tablet/packetproperty-guids) · [Pointer 轴定义](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_pen_info)
2. **倒置笔的负仰角会被压成贴屏。** RTS 规范允许倒置时 altitude 为负。生产代码用 π/2 − clamp(altitude,0,π/2)，因此 −60° 会得到 90°。若要表达倒置笔轴的倾斜幅度，应得 30°，候选为 π/2 − |altitude|，并单独保留倒置标志。笔尾若无需姿态，也可明确给未知。方位还取决于驱动给的是哪一端的投影，不能仅凭倒置标志统一加 π。正向笔的仰角取余角、顺时针方位取反，在零轴一致时符合模型约定。[RTS 仰角与方位](https://learn.microsoft.com/en-us/windows/win32/tablet/packetproperty-guids)
3. **压力与触摸宽高没有发现同类的直接错传。** 正式 `DecodeSnapshot` 对宽高调用 `ConvertContactArea`，并非旁边的简化测试 helper。每轴换算为 rawSpan × (axisResolution/spanResolution) × 长度单位比 × |inkToCanvas/inkToDigitizer|，带单位、范围和有限值检查。尺寸不可信时沿用面积回退。位置另有“取首 context 倍率供多个 context 共用”的兼容行为；微软的倍率定义为特定 context 的 ink→digitizer。副屏或外置数位板需与真实落点对照，不能据此直接判定所有 RTS 设备位置错误。另一个可见边界是设备种类查询失败时 decoder 初值为 Pen，此时触摸面积不会进入 Touch 分支。证据在 `Draw3.RealtimeStylus.cpp:943、1011、1856` 和 `Draw3.SpeedEraser.cpp:559`。

## 4. 延迟比较与验证方法

RTS 同步插件运行在 RTS 的执行线程，生产入口无需等绘制窗口取出下一条消息。当前 `Packets` 取整批最后一点，在回调内读 QPC；即使设备早已采样，现有计时也只从回调附近开始。Pointer 由目标 HWND 的消息线程取件，窗口线程繁忙时 Move 可以合并；优点是同时拿到系统时间戳和历史样本。两者进入协调器后都受最新快照、模型、帧调度、Present 和显示扫描影响。架构上最值得关注的是 **窗口线程忙时 Pointer 的尾延迟**，以及 **RTS 批到达时间掩盖的采样等待**。没有证据支持给某一后端标注固定的毫秒优势。[RTS 线程模型](https://learn.microsoft.com/en-us/windows/win32/tablet/realtimestylus-reference) · [Pointer 合并与历史](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerpeninfohistory)

- **同口径比较。** 两后端分别记录入口 QPC、发布 QPC、绘制消费 QPC、提交／Present；按触点、批次和实际显示帧关联。Pointer 的 PerformanceCount 是系统接收消息时的高分辨率计数，有硬件扫描时标时可校准。它和回调 QPC 分列，不能把 RTS 的设备到回调等待记为 0。为 0、回退、倒序或时钟域不可确认时标记不可用，用到达时钟维持兼容。RTS 的 timer tick／serial 当前仅观察，未正式请求或校准。首轮 A/B 保持相同模型时钟策略，避免把改时间基准的收益归给 API。
- **先比最新点，再比历史策略。** 两边首轮都以最新 Move、完整 Down/Up/Cancel 和同一预测设置运行。Pointer 要在处理当前消息时读取历史，返回顺序最新在前且包含当前点；若另测保留转折点，按旧到新、有界去重，记录 historyCount、实际建模点数、每帧成本和轨迹误差。把每个历史点逐个写进只保留最新 Move 的槽，不能恢复整条轨迹。
- **真机实验。** 同笔同屏交替 RTS/Pointer，控制分辨率、刷新率、画布帧率、预测和笔刷。在空闲、绘制 HWND 线程人为繁忙、4K 渲染繁忙、多指并发下，分别统计落笔首墨、连续跟笔、抬笔末段的 p50/p95/p99、错过显示帧比例和队列等待。同步拍摄笔尖与屏幕首个发光像素，或使用光学测量，报告相机帧周期误差。Present 返回并不等于像素已发光。对预测端点同时测急转弯偏离和回拉。60/120 Hz 的一帧分别约 16.67/8.33 ms，这只是错过一次帧机会的量级。Win11 优先考察 RTS 的依据是独立输入线程、已有生产路径，以及部分笔能提供更细的压力分档或直接姿态。窗口线程繁忙时它有机会获得更低的尾延迟；若瓶颈在帧调度或呈现，两后端可能相近。在两后端姿态均正确的条件下实测，用于评估第一节启动策略的实际表现。[Pointer 时间戳定义](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_info)

## 5. 白板“拖动”的两条接入路径

### Pointer 绘制 + Direct Manipulation

仓库已经有一条平移管线。`Draw3.TouchpadInput.h` 配置 XY 平移、惯性、显式命中、输入透传及手动更新。`Draw3.WindowControl.cpp:378` 管理 viewport 和事件回调，约 2346 行处理 DM_POINTERHITTEST，按设备类别筛选 PTP 后调用 SetContact。`Draw3.DrawingController.cpp:3728` 在拖动获准且没有活动接触时接收原生导航，将连续累计平移之差送给白板，原生惯性不会再叠加应用 fling。历史修复 `241f1f44` 之后，`kTouchpadDirectManipulationQuarantined=true` 使初始化在创建 manager 前返回，日志原因是 RTS 与 DM 在同一 HWND 冲突。现有无窗口测试覆盖路由逻辑，不能证明真实 PTP 已工作。

建议只在实际生效后端为 Pointer、白板处于拖动工具时允许这条路径。由 HWND owner 建立／激活 manager、配置 viewport，跟随客户区尺寸；命中且通过类型校验才 SetContact。关联成功只是候选，等 RUNNING 和 content transform 再应用位移；INERTIA 按设置消费，READY 消费最后净位移。捕获交给 DM 时结束相应普通触点所有权，不把它误判成整个 DM 会话取消。离开拖动、白板关闭／加载、新笔接管、窗口销毁或切后端时停止惯性，废弃旧代回调；RTS 模式保留现有 DM 隔离。触摸屏和笔继续走各自既有绘制／拖动路径，绘制时无需为了触控板注册 DM。

**8.1 有一项具体的验证障碍。** 微软 DM 流程文档描述了命中、捕获和 SetContact，但独立消息页将 DM_POINTERHITTEST 标为最低 Win10，且 wParam 标为未使用。Inkeys 与 Chromium 的实际代码均从 wParam 提取 pointerId，再查询 PT_TOUCHPAD。它是有工程先例的做法；要在 8.1 承诺支持，应在真实 8.1 + PTP 记录命中消息、ID/类型、SetContact、RUNNING 和 transform，以及笔／触摸不被抢占。任一步缺失都不能凭系统版本报成功。在未验证平台，继续提供鼠标按键拖动；传统驱动生成的滚轮不能证明取得原生双指输入。[微软 DM 输入流程](https://learn.microsoft.com/en-us/windows/win32/directmanipulation/directmanipulation-processing-input-with-directmanipulation) · [命中消息参考](https://learn.microsoft.com/en-us/windows/win32/inputmsg/dm-pointerhittest) · [Chromium 的同类实现](https://chromium.googlesource.com/chromium/src/+/HEAD/content/browser/renderer_host/direct_manipulation_helper_win.cc)

### Windows 11 + RTS tablet 窗口

Draw3 已在外部画布 HWND 上安装 Tablet Pen Service 属性，并处理笔光标、触觉与 Pointer 观察消息。这些标志控制 tablet 行为；它们本身不注册精确式触控板。微软为桌面 HWND 提供单窗口 opt-in，官方示例是普通独立窗口。文档没有保证该注册与 Inkeys 同一 HWND 上的 RTS 同步插件在所有系统和驱动上共存。

1. 只在目标绘制 HWND 上试用 `RegisterTouchpadCapableWindow(hwnd, TRUE)`，以配套入口动态解析和调用结果判断能力；微软列为 Windows 11 且带预发布说明，注册函数还列有 User32 导出 ordinal。不要以 Win11 大版本、系统中有触控板或只解析到一个函数作为成功依据。窗口级注册由 owner 管理，后一次调用决定状态。窗口注册也不能靠线程级设备枚举行为作唯一探测。
2. 在拖动模式仅消费 `PT_TOUCHPAD` 的双指手势消息。按帧读 `GetPointerFrameTouchpadInfoHistory`，将完整的 `POINTER_TYPE_INFO` 帧送 `ProcessPointerFramesInteractionContext2`，同帧只处理一次。Interaction Context 配置 XY 平移与可选惯性，遵从系统平移方向及禁用设置，忽略缩放。系统会补发较早的 Down，直接把触点绝对位置当白板位移会导致起步跳动；使用 manipulation 输出，按选定单位只换算一次，再送现有累计位移桥。仅消费自己处理的触控板序列，同一序列避免再经 DefWindowProc 提升为滚轮重复移动。
3. 惯性只选一个来源；若采用系统 Interaction Context，按官方接口推进惯性、用 `ReportWindowContentInertia` 通知系统开始／结束，并响应 WM_STOPINERTIA／WM_ENDINERTIA。退出拖动时终止手势并解除该窗口注册或切回默认路由；笔／触摸始终由 RTS 发笔迹。不要为了这个模式解除遗留 DM 隔离。
4. 在同一台设备同一窗口依次比较“RTS 单独”“仅注册新接口”“RTS + 注册”。记录注册返回值、PT_TOUCHPAD 的 Down/Update/Up、RTS 笔与触摸回调、普通鼠标、横纵净位移、首帧跳动、退回绘制后第一笔，以及笔靠近或按下时惯性是否停止。只有同时保住 RTS 书写和双指平移才将组合列为支持，否则保留 RTS、报告当前不可用。这是当前最大的实机未知项。[窗口级注册](https://learn.microsoft.com/en-us/windows/win32/input-precisiontouchpad/registertouchpadcapable) · [触控板帧与坐标](https://learn.microsoft.com/en-us/windows/win32/input-precisiontouchpad/getpointertouchpadinfo) · [微软完整处理示例](https://learn.microsoft.com/en-us/windows/win32/input-precisiontouchpad/precision-touchpad-guide)

## 6. 普通笔的硬件压感与实时笔锋

**第四步需求。** 普通笔工具使用有效硬件压感时，每个轨迹点先取得自身的压感笔宽，再乘实时笔锋倍率。重按段仍有重按的厚度，轻按段仍能收细。移动时显示笔锋，停住时笔身像鼠标／触摸一样追到笔尖并恢复局部宽度，抬起时按当时的收锋状态定稿。RTS 和 WM_POINTER 共用这条几何路径。真实压感首版采用原收尖强度的一半，笔锋充分展开时尖端保留当地压感笔宽的 65%。后续按实际书写效果调整。

### 现有模拟压感怎样形成笔锋

1. **先求笔身宽度。** 普通笔落笔时按设备设置选定本笔宽度模式。鼠标／触摸默认用模拟压感，笔也可显式选择模拟压感；设置为硬件压感的笔若 Down 压力无效，本笔回退模拟压感。模拟模式不向 Modeler 伪造压力。Draw3 从真实输入位置和时间估算并平滑速度，再以配置的预期速度归一化。首点用基准直径 D，后续目标直径从慢速的 1.4D 平滑过渡到快速的 0.8D，按时间渐近跟随，并限制相邻点在给定时间、距离内的半径变化。
2. **再给尾段收尖。** 普通笔当前 Normal 时间窗为 55 ms。每帧重建实时层 L0 时，取未提交的真实点及预测点副本，计算窗口内的弧长和最大原始半径 R。设有效尾长为 L，端点的目标倍率为 `1 − 0.7 × smoothstep(L / (4R))`，其中 smoothstep 将输入限制在 0…1。沿尾段从起点的 1 平滑过渡到此倍率，每点乘自己的原半径，最后做公切线约束。有效尾长等于一个参考直径时，端点目标为 0.65；达到两个参考直径时为 0.30。短划、慢移、长划的尖度因此不同。真实点未被改写，逐帧重绘不会反复乘细。
3. **停笔追上同时涉及位置和宽度。** 暂停后，位置模型按已有判定用最后的真实端点继续收敛；合成 Move 沿用最近有效压力，不算作新的真实位移。宽度方面，显示时间继续推进，有效尾长按 smoothstep 缩短，最终倍率回到 1。此时恢复的是该处自身的笔身宽度。55 ms 是笔锋的参考窗口，最终显示还受预测参考时间、模型收敛和帧调度影响。物理 Up 锁定终态显示时间，快速提笔能保留尚未追完的锋，停稳再抬笔则能保留饱满的笔尾。

生产入口可对照 [宽度模式选择](<D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cpp:274>)、[速度与硬件压力的笔宽估算](<D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:507>)、[尾部倍率及有效弧长](<D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:393>)、[停笔时推进模型](<D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:22662>)。

### 硬件压感怎样叠加，强度怎样选

当前普通笔的硬件压力目标直径为 `D × (0.2 + 1.2p)`，p 为 0…1 的有效模型压力，后续点还会经过时间和距离限幅。硬件笔已参与普通笔的端点收敛和 L1 尾段保护；`ResolveLiveTipTaperDurationSeconds` 专门对 HardwarePressure 返回 0，因此其压感几何没有实时收尖。建议恢复同一时间窗，在尾段使用 **已经按压力算好并限幅的局部半径** 乘倍率，不改模型压力、不再套一次速度模拟，也不把倍率写回稳定点。未取得硬件压力而进入模拟模式的笔继续用模拟强度。

首版将真实压感的收尖强度 k 定为 0.50。令 M 为鼠标／触摸现有算法在该点算出的倍率，应用 `M笔 = 1 − k × (1 − M)`。k=0 是当前硬件笔外观，k=1 是完整的现有笔锋。保留已有尾长、预测和追赶逻辑；当 M 回到 1，任意 k 都会准确恢复该点的压感宽度。下表是充分展开、M=0.30 时的**几何目标**，其中 65% 为首版值，其他行供后续调参对照。数字假定均匀压力、限幅已稳定，不含最终光栅覆盖和几何约束。

| 强度 k | 端点保留当地笔宽 | 轻压 p=0.10，笔身 0.32D | 重压 p=0.80，笔身 1.16D |
|---|---|---|---|
| 0（当前硬件笔） | 100% | 0.320D | 1.160D |
| 0.25 | 82.5% | 0.264D | 0.957D |
| **0.50（首版）** | **65%** | 0.208D | 0.754D |
| 1（完整笔锋） | 30% | 0.096D | 0.348D |

**判断。** 同一 M 对局部笔宽的削减百分比相同，硬件笔不会天然得到更轻的笔锋。轻压的绝对宽差较小，有时看不清变化；细线也可能因继续变细而接近一个像素以下，出现断续感。重压时绝对宽差反而更大，完整倍率可能使重写的笔尾看起来像提前卸力。实际 M 还依赖窗口内最大半径，所以同速下轻→重与重→轻都值得单独测。首版先用 k=0.50；实际使用出现轻压断续或重压收锋不足时，再用表中其他强度复测。

### “绘图效果”中的实时笔锋开关

在“设置 → 绘图效果”加入 **实时笔锋** 总开关，在其正下方内联显示 **鼠标、触摸、笔** 三个可独立勾选的设备项。建议总开关及三项初始均开启，旧配置缺项时使用相同默认值。关闭总开关时，设备项留在原位、置灰并保留勾选；重新开启后恢复原组合。

普通笔工具每次落笔时读取总开关和当前设备项，两者均开启才应用实时收尖。设备按实际识别的输入来源归类，RTS 与 WM_POINTER 共用同一组设置。“笔”同时控制硬件压感笔和走模拟压感的笔；强度取决于本笔实际使用的宽度模式。真实压感使用 k=0.50，模拟压感或固定笔宽使用已有的完整笔锋。关闭效果相当于尾部倍率为 1，压力或速度产生的笔身宽度仍照常计算，端点继续按已有逻辑追到实际落点。

建议设置变更从下一笔生效，同一笔的实时显示、抬笔定稿和保存使用落笔时的选择。验收时关闭“笔”、保留“鼠标／触摸”，分别绘制并确认只有笔取消收尖且仍有压感粗细；再关闭和重新开启总开关、重启应用，确认各设备选择能恢复。

### 调参与验收

- **先用同一轨迹分离变量。** 重放相同的坐标、压力、时间戳。固定速度改变压力，再固定压力改变速度，覆盖轻压细线、重压粗线、轻→重→轻、短点、长划、转角、快速提笔与停稳提笔。并排比较“仅压力”“压力 × 首版笔锋”和现有模拟模式；后两者各自以自己的局部笔身宽度作为参照。
- **看追上与定稿。** 记录真实端点与显示端点的距离、有效尾长、收尖前后局部半径、停笔至恢复局部宽度的时间，以及抬笔前后最后一帧宽差。检查静止轻重压变化不会重启笔锋，停笔不回弹，续写不继承旧尖锋；保存重开与抬笔定稿一致。正常提交和异常保存都应取相同的强度与终态时间，预测点仅用于实时显示。
- **再用真笔复核首版倍率。** 在常用细笔和粗笔上，以相同笔刷分别在 RTS 与具备有效压力的 Pointer 后端书写，对照正常显示尺寸下轻笔是否断、重笔是否保留层次、快收笔是否清楚。检查停笔回宽是否自然，记录是否需要调整首版强度。设备开关独立保存，收尖强度在两个后端共用。
- **实现时的回归位置。** 现有无窗口笔锋探针显式断言硬件压力路径不收尖，要更新这一条旧预期，并复用原有的长度／直径比例、变宽笔身、重复重建、延迟 Up 和停笔追赶用例。关闭笔锋时应还原原硬件压感笔迹；鼠标／触摸原倍率以及硬笔、荧光笔、激光笔和橡皮各自的策略保持独立。[现有笔锋探针](<D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:1470>) · [宽度模式对笔锋的门控](<D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cpp:266>)

## 7. 验收顺序

首先修正生产 RTS 双轴 Y 方向和倒置负仰角的处理，同步核对模型器测试项目的平行实现及预期值。用右、上、左、下四向、垂直笔及倒置 −60° 的固定样本核对 Modeler Input；在设备同时给两套角度时，分别强制经过直接角和双轴路径，对照它们与实际笔向。保留旋转屏、倒置方位及首包未知后的真机验收记录，再按第一节矩阵验收启动选择与实际后端提示。随后以一台带笔及 PTP 的 Win11 机器证明 Pointer 绘制与 DM 拖动能在工具间来回切换，同时测 RTS 基线。用同一支笔采集四向倾斜、垂直和倒置笔，记录真实 mask；再在教师大屏验证掌擦和压力缺失设备，核对双屏／旋转及亚像素轨迹。随后分别验证 Win10 + DM、8.1 + PTP 的实际命中链、8.0 的自动 RTS 与手选 Pointer 绘制和 Win7 的启动／回退。Win11 + RTS 新接口另记共存结果。最终支持矩阵应同时注明系统构建号、设备／驱动、实际生效后端及双指平移结果。

本次仅调查，未修改项目、未编译、未运行 GUI 或硬件实验。文中的时延量级为帧周期换算，各版本的拟接入逻辑均为方案。
