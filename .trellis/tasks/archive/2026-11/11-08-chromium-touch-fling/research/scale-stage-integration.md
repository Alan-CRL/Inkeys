# Research: 屏幕尺度阶段的产品接线与锁存边界

- Query: 在当前已确认手感基线上如何接入可信物理标尺和桌面/大屏响应，避免耦合橡皮调试选项；产品Automatic实际是否存在。
- Scope: internal；只读当前源码/规范，未进行Git操作、编译、GUI或产品运行。
- Date: 2026-11-08
- Baseline: 主会话提供的当前已提交基线为4b4edb91。本文方案为候选，未获实施批准。

## Findings

### 1. 最重要的现状修正

`research/display-scale-audit-2026-11-08.md`的PPI96/逐轴Physics平方距离属于7b阶段1历史。当前产品已改成向量摩擦、逐指可靠尾段和残余投影；旧表不能继续当作当前设备适配公式。

产品设置目前只有两个场景选项：

- `Setting.cpp:3831–3849` 的devices只有大屏触控与桌面交互，paintDevice被clamp到0..1。
- `zh-CN.jsonc:713–716`：0对应大屏触控模式，适用于教室/会议室直接大屏；1对应桌面交互模式，适用于个人电脑、平板及鼠标操控的大屏。
- `Draw3.Host.cpp:1828–1829`：paintDevice==0传LargeScreen，其余传Laptop。
- `SpeedEraser::DeviceMode::Automatic`确实存在（SpeedEraser.h:9），但常规Host不会传它。`SpeedEraser.cpp:386–405`的DirectInputUsesDpiFallback仅以Automatic探测直接输入标尺能力，不是用户的自动场景设置。

因此MVP可复用“用户场景选择+可信物理长边连续权重”，不能宣称已有Automatic UI。新增第三个场景值会波及paintDevice其它消费者/持久化，超出最小接线；若真需要，属于另一个产品决定。

### 2. Files found / 当前关键边界

| 文件 | 作用/锚点 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | 1618 PumpDisplayScale；1646 DIP/px；1651输出映射已知；1654–1659可信cm/px；1696–1702 revision；1828场景发布；2536显示订阅 |
| `Draw3.WindowControl.cpp` | 752–783 display/mode由同一mutex保护，但分成两个getter/setter；更新仅请求cursor/control wake |
| `Draw3.SpeedEraser.h` | 22–30 InputSource带真实kind、generation、monitor/rect；99–115 DisplayScale带DIP/cm、尺寸、方向、revision及development整包 |
| `Draw3.SpeedEraser.cpp` | 84–113来源分类；225–312映射/物理尺度与fallback；342–355长边与显式mode权重 |
| `Draw3.PalmEraser.cpp` | 30–45已有借用标尺解析的样例；仅可信时采纳mm/px，其余明确估算DIP |
| `Draw3.DrawingController.cpp` | 2619–2632 CanvasPanScale当前只有两轴DIP/px；2750 Register；2825 Step；2953 Push；15329准入传当前显示标尺；15401更新导航 |
| `Draw3.RealtimeStylus.cpp` | 465–480 Pointer来源/映射；890–913 SourceForCursor；每份来源携带decoder generation；SourceKind::Unknown会清mappedMonitor |
| `Inkeys/Inkeys/Display/Display.cpp` | 1104–1140可信业务物理尺寸，拒绝fallback/未知拓扑/复制混合/非唯一目标/坏EDID或尺寸缺失 |

缩写文件均位于`Inkeys/Inkeys/Drawing/Draw3/`。

### 3. 不能把整份橡皮Config直接变成平移配置

当前SpeedEraser解析器不仅算标尺：

- `SpeedEraser.cpp:248–258`会受正式PenResponseChoice及development.response override改变响应模型。
- `263–264`读取development.penBeta；`272–284`读取橡皮的ManualSurface调试标定；`306–312`使用分辨率/DPI经验增益。
- Host:1629–1635把eraserDevelopment复制进DisplayScale，仅剥离两个面积/trace标志；response/scale/manual/beta仍在。

建议最小接线有两种可选实现，首选A：

A. **小范围提取纯事实解析器**：从已有函数提取来源映射匹配、当前方向的每轴物理密度和可信度判断；SpeedEraser和Pan分别消费结果，各自处理自己的策略/调试。需逐字段对照证明橡皮行为不变。不改Display枚举、来源识别或橡皮参数表。

B. **较小过渡包装**：复制DisplayScale后显式清空development，用真实source、Automatic场景、Automatic笔响应调用现成ResolveConfig，只取inputMapped/mmPerPixel/unit；Pan单独算场景权重。它能避免复制校验，但存在平移依赖橡皮Config整包的维护耦合，不应借此继承橡皮专有override。旧Palm实现没有清development，不能机械复制就声称两者独立。

原始可信EDID可直接供MVP；若希望Pan也能使用手动标定，需要明确它是共享正式表面校准还是仅橡皮调试项，不能暗中复用。目前没有找到独立正式Pan校准偏好。

### 4. 最小产品数据流

建议使用一个小型纯CPU解析结果，保留现有Host低频发布，不增加逐点EDID查询：

```text
Display已发布快照 + Host实际输出 + 用户paintDevice
  -> 一次锁内取得PanDisplayContext(display facts + mode)
  -> 真实Down InputSource + InputDeviceType
  -> ResolvePanMetric / ResolvePanScenario
  -> CanvasPanInput锁存该手势的变换与响应
  -> 当前CanvasPanDynamics内核
  -> 像素位移/系统DIP速度输出到原视口和tile/blur
```

候选结果字段：`systemDipPerPixelX/Y`、`motionPerPixelX/Y`、unit（SystemDip/ReferenceMotion）、可信mmPerPixel、physical/fallback source、mode/profile weight、monitor/rect/pixel size/orientation、显示generation/revision供日志、来源映射和校准身份供有效签名。

WindowController目前display与mode两次getter虽各自安全，但会读到不同时间。可以增加一个只读组合getter，使用现有speedEraserConfigMutex一次复制两者；无需重命名/重构整套橡皮发布路径。Host两项事实本身分别更新，组合getter只承诺某一瞬间一致的已发布组合；后续有效字段变化按下面锁存规则处理。

### 5. 推荐统一ReferenceMotion单位U的入口适配

主会话提出的候选：可信Touch令`motionPerPixel_i = mmPerPixel_i / rho_ref`；间接Mouse继续`motionPerPixel_i = systemDipPerPixel_i`。所有产品几何/估速在同一手势内使用选定motion单位，避免在每个tracker/曲线重复转换。

此方案可行且侵入较少：每轴线性变换与质心求平均可交换；按同一标量推进的向量摩擦经逆变换后仍沿原像素释放方向。无需恢复逐轴平方曲线。多指必须共用同一变换，不能一指用mm、一指用DIP。

必须完整审计下列入口/出口，不能只改Push：

| 当前位置 | 需要的适配 |
| --- | --- |
| Controller:2957–2960 Push | 原px乘motionPerPixel，sourceKey身份规则保持 |
| 2905–2906 viewportSizeDip | 同一motionPerPixel转换；字段名或合同不能再假称物理Touch输入一定是系统DIP |
| 2919–2921 frame.contentDeltaPixels | 内核motion delta除motionPerPixel |
| 2740 VelocityDip | 当前直接返回内核速度，采用U后必须改成motion→px→系统DIP |
| 2741–2745 VelocityPixels | 内核速度除motionPerPixel |
| 2926 frame.velocityDip | 同上，禁止直接接内核U速度 |
| 2927–2929 frame.velocityPixels | 内核速度除motionPerPixel |
| 18414 tile coverage预测 | 消费真实px/s，保持原策略 |
| 19928 snapshotMotion/blur | 消费真实系统DIP/s，保持300DIP/s清晰门限与12DIP模糊上限 |
| 15432–15445诊断 | release/raw/boost/focus/span/flingOffset是U或DIP必须标明；可另打印px/s、系统DIP/s、可信时mm/s |

当前15DIP slop、.1 scroll epsilon、1DIP反向证据等会一并成为15U/.1U/1U，故这是**用户能感知的起步/反向门限政策变更**，应在最终方案明确确认。它不是“只改惯性距离”。如果不希望本轮改变slop，必须把几何门限留在系统DIP并承担双度量转换，不能同时宣称最小统一单位方案与原slop完全不变。

ReferenceMotion只是参考单位定义；选择固定rho_ref还是当前已确认设备锚点、如何保存校准属于主方案待定项。每台新设备启动都把自己当前rho当ref会抵消跨设备归一化，不能这么做。不得把当前设备HMONITOR作为跨进程稳定校准身份。

### 6. 场景权重与手动模式优先级

可复用SpeedEraser.cpp:342–352的分类规则作为初始版本：

```text
w_measured = smoothstep((physicalLongEdgeMm - 320) / (1200 - 320))
Desktop/Laptop: w = min(w_measured, .25)
LargeScreen:    w = max(w_measured, .25)
```

这表示用户选择约束物理尺寸先验，不会改变真实mm/px。桌面并非一律w=0，大屏也并非一律w=1；应在方案中说清楚。最终摩擦/门限/残余增益属于Pan自己的参数表，不搬擦除90/350等数值。若用户要求“无论屏幕多大，桌面永远完全一套、大屏永远完全另一套”，就与上述连续规则不同，需要产品选择。

物理尺寸失效时没有真实w_measured。建议保留用户mode、采用各自显式fallback参数，日志标estimated；分辨率/DPI启发式可仅用于动作强度/响应选择，不能伪造毫米，也不要同时给输入与输出各乘一次增益。

### 7. 触点、来源与失效规则

- **MouseLeft / Win32兼容Mouse**：沿用当前DIP响应，不因显示器大就套直接Touch物理尺度。兼容Touch被降为Mouse的已知输入后端降级不能凭整机配置逆推成真实Touch。
- **明确Touch且映射匹配**：才采用直接物理标尺。只有InputDeviceType::Touch但source.kind Unknown/Conflict/TouchPad或mappedMonitor缺失时，保守DIP fallback。`ClassifySource:88–113`明确Win7/不可信识别可能Unknown。
- **Pen / ExternalPen / IntegratedPen**：当前不进入平移，hover/落笔停止规则保持。屏幕笔混合、数位板间接响应仅为后续明确笔平移需求时的边界，不应本轮创建未用控制器。
- **同一批多指**：相同有效输出/metric/profile的不同contact可共享几何，cursorId不同本身不应导致每指重设标尺。sourceKey现按class/context清跨来源boost；尺度阶段还应防止参数/校准不同的来源共用残余。
- **混合映射**：同批新指若解析为另一个物理输出或不兼容metric，不可改整个活动批次的变换。建议仅允许与锁存变换等价的新指加入，其余跟踪/抑制到终态并记录原因；另一可选策略是整批取消。此为稀有但应明确的产品边界。

### 8. 锁存与显示更新

当前Register:2761只在无触点且dynamics不活动时更新scale，Step不接收新display context。WindowControl:752–783的display/mode变更只wake；Controller读取新值仅服务橡皮，未发现针对同尺寸DPI/物理可用性变化的pan Stop钩子。Resize Stop不能代替此检查。

建议新增轻量观察点（导航Step前以及新Down解析前），基于**有效字段签名**处理：

1. 真正输出monitor/rect/pixel size/orientation/DPI变化，或已采用物理映射失效：走现有stopCanvasNavigation，旧epoch/残余清空，旧接触吞至终态；下一有效Down采用新变换。
2. 用户mode/调参变化：当前手势与fling锁存；下一首指Down采用新profile并清不兼容boost，不应持续连甩无限期锁住旧模式。
3. 初始EDID晚到由fallback变physical：不要在仍按住时重新解释坐标；可等下一手势切换并清旧残余。与“当前物理映射变不可信”分开处理。
4. 无关显示器刷新、同值generation增长、橡皮debug更新：解析Pan有效事实未变，不打断平移。DisplayScale.revision包含development变化（Host:1629、1698），所以不能比较raw revision就Stop。
5. 陈旧RTS映射与新输出尺寸不匹配：保持DIP fallback，不能沿用旧mm映射。现未充分核对每类display change是否都会触发RTS pointerSources重建；不承诺热插拔后无缝保持physical，需日志与设备测试。

### 9. 最小文件范围与测试建议

候选生产改动：

- `Draw3.DrawingController.cpp`：组合上下文读取、Register首指锁存、Step前有效变更观察、入出站单位闭环与诊断。
- `Draw3.WindowControl.cpp/.cppm`：必要的display+mode组合只读getter；不把D3D操作移到窗口线程。
- `Draw3.SpeedEraser.h/.cpp`：仅当选择提取公共可信标尺resolver时最小改动，必须保持橡皮数值与行为逐字段回归。
- `Draw3.CanvasNavigation.cppm/.cpp`：只引入明确的motion单位/参数合同及profile参数传递，不再改变已确认尾段算法和事件流。
- `Draw3.Host.cpp`：现有Display发布够用；只有需要暴露physical unavailable reason等事实时扩展小型发布对象，不重复枚举EDID。
- 现有Headless/生产pan probe：加入物理比例、系统DPI、旋转/非等比密度、来源映射/模式与变化时序矩阵；不新增调参设置页作为前提。

必须验证：同profile同物理轨迹在100/150/200/300%系统缩放下运动一致；几何px roundtrip与tile/blur单位正确；当前参考设备基线保持；Mouse完全绕过物理策略；新指不改变批次变换；mode切换在下一手势生效；输出/DPI变化旧批次不复活；EDID未知/复制/冲突日志清楚；橡皮debug开关不改变Pan；橡皮原回归不退化。

### 10. Related specs / External references

- `.trellis/spec/native-desktop/canvas-pan.md`：当前产品释放、向量摩擦、诊断与保留边界。
- `.trellis/spec/native-desktop/cpp-conventions.md:84–115`：业务物理尺寸可信度、单屏/扩展/复制拓扑、热路径禁止枚举。
- `.trellis/spec/native-desktop/palm-eraser.md:24–26`：直接Touch可信mm/明确DIP fallback与锁存示例。
- `.trellis/spec/native-desktop/pen-width.md`：系统DIP与Canvas像素边界，不能借UI zoom替代DPI。
- `.trellis/spec/native-desktop/eraser-attributes.md`：输入场景与擦除灵敏度各自独立。
- 本轮未新增外部资料；现有Chromium旧研究只作历史。实际当前接线结论全部来自上述本地源码。

## Caveats / Not Found

- 没有当前电脑实际可信mm/px、来源映射和mode日志，不能填实测rho_ref或声称当前设备一定进入physical分支。
- 方案未实施，未运行编译/C++/GUI，测试建议不等于PASS。
- 尚需主方案确认：ReferenceMotion是否包含slop等几何门限；参考锚点/持久化方式；连续场景权重与显式profile端点的用户期望；混合不兼容多指映射是抑制新指还是整批取消。
- 未完整审计RTS映射热刷新，不能把display generation与source generation当同一代。
