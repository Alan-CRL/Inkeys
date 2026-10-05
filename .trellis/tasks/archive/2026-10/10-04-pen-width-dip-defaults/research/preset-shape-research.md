# Research: 手动粗细匹配预设高亮与形状共用档位

- Query: 手动调到细/中/粗对应逻辑宽度，松手后按钮高亮；形状三个档位应与普通硬/软画笔策略相同并自动高亮。
- Scope: internal
- Date: 2026-10-04

## Findings

### 已加载协议与规范

Native research hook 已给出 active task；遵守研究隔离，没有加载 implement.jsonl/check.jsonl，没有任何 Git 操作。已读取 .trellis/workflow.md、native-desktop/index.md、cpp-conventions.md、rendering-and-ui.md、pen-width.md，以及任务 PRD/design/implement。旧设计“ExplicitDip 不选中预设”的显示规则需要由主会话按本轮用户要求更新，但来源仍保持 ExplicitDip。

### Files found

- Inkeys/Inkeys/UI/Bar/Bar.Layout.cppm：共用高亮判断与推荐预设解析。
- Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp：真实预设点击、Slider/触摸/FineDial 提交、CPU 生产测试。
- Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp：画笔与形状布局、文字、预览及悬停动画。
- Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp：形状三个读数空字符串初始化，不是旧常量。
- Inkeys/Inkeys/Business/PenWidthPolicy.hpp：唯一 Brush/Highlighter/Laser 预设与 DIP 解析。
- Inkeys/IdtState.cpp、IdtState.h：独立形状宽度槽、同锁版本入口与快照。
- Inkeys/IdtMain.cpp：无窗口生产测试 CLI。
- InkeysHeadlessTests/pen_width_tests.cpp：纯策略矩阵、非法输入及 px 量化碰撞测试。

### 根因与实际调用链

1. Layout.cppm:349–365 的 IsBarThicknessPresetSelected 目前只接受 Source::Preset + preset身份，所以真实手动提交即使恰好为3DIP仍不会亮。这个函数不读展示整数或 canvas px，是本轮最小修复入口。
2. Interaction.cpp:4104–4131 的触摸/Slider映射保持连续double候选；:5152 和 :5183 在真实有效抬手调用 SetPenWidthIfRevision。IdtState.cpp:944–947 该setter始终写 ExplicitDip 并保留原double真值；:938–942 普通SetPenWidth同理。不应为了高亮改成 SetPenWidthPresetIfRevision，否则换显示器/设备模式会错误重解析用户显式宽度。
3. Interaction.cpp:1869–1901 的 FineDial Commit 用相同真实条件setter，候选清理后可由下一帧统一判断高亮。已有修订号、dirty/noop、cancel行为本轮保留。
4. 画笔布局 RenderLoop.cpp:4100、悬停动画:5946、交互hover :2188 共同调用该判断；形状布局 :4777/:4785/:4793、悬停动画 :6023、交互hover Interaction.cpp:2140 也共用同一判断。无需各入口另写比较逻辑，无需每帧修复Selection来源。

### 形状档位已与普通画笔共用

- Policy.hpp:54–75 是唯一预设。普通Brush1/3/6DIP，大屏参考2/4/6px后按同Context解析；Highlighter与Laser各自独立。
- IdtState.cpp:894–898 同一ResolveWidthSlotsLocked用Family::Brush解析Brush1/StraightLine1/Rectangle1；:919–925 根据当前形状写其独立槽，直线/虚线共用StraightLine1，矩形另槽；:956–977 从当前槽取Selection，形状Family恒Brush。
- Geometry实际按钮定义 Interaction.cpp:3827–3847 依次索引0/1/2，抬手 :3889–3891 直接 SetPenWidthPresetIfRevision。普通画笔按钮 :5397–5404 同样0/1/2，抬手 :5470 同样条件预设setter。
- Initialization.cpp:1155–1171 形状三档word用L空字符串初始化，未发现硬编码旧1/3/5。
- Layout.cppm:302–309 的 GetBarThicknessPresetMetrics(SoftPen, index, snapshot)直接Resolve Brush预设。
- Geometry读数 RenderLoop.cpp:4844–4849 用该函数resolvedWidthDip格式化，Geometry小线绘制 :11627–11629 用同函数的appliedCanvasWidthPx。没有旧1/3/5数据路径。局部panelScale是快捷按钮视觉动画/布局比例，不写状态或Draw3；本輪不改相关视觉样式。
- 主会话建议正确：不为已经正确的Geometry点击、读数和预览添加多余变更，通过新增生产测试明确其共用策略。

### 最小实现建议

仅改 Bar.Layout.cppm 共用 IsBarThicknessPresetSelected + Bar.Interaction.cpp 既有CPU生产测试：

1. index>=3返回false；先取GetPenWidthSelection(snapshot)。
2. Preset保持身份优先，只按有效身份返回对应按钮，不能按数值推断，确保量化碰撞仍唯一。
3. ExplicitDip先校验IsValidDip；根据GetPenWidthFamily(snapshot)和snapshot.penWidthContext取得当前PresetDip(targetIndex,family,context)，不要使用传入旧penMode、float slot.width、圆点尺寸、显示整数、最终px或Bar自身DPI/zoom。
4. 数值相等容差使用 8 * numeric_limits<double>::epsilon() * max(abs(value),abs(target))。没有绝对1.0 floor；只吸收连续double映射/反算浮点误差，不把“显示一样”当成“粗细相等”。例如3.1和3.0、1.939和64/33都不能亮。不开启吸附、不重写状态、来源或配置。
5. hover/layout自然消费同一匹配函数，不改提交状态机、动画/取消/锁定/revision。

### Production test seams

- IdtMain.cpp:1065–1074 的 --pen-width-production-test 调用真实preferences/state，再RunThicknessFineDialProductionTest；同测试也由 --state-mode-production-test调用。这些入口在主程序窗口初始化前返回，符合无GUI限制。
- Bar.Interaction.cpp:1331–1429 已有真实Bar policy、Preset身份、量化碰撞、FineDial精度/提交/取消和Shape当前槽测试；:1432–1494 有真实PublishThicknessSliderCandidate到preview的CPU接缝。
- 可新增当前普通/大屏Context、Brush硬软共享/Highlighter/Laser各family的ExplicitDip精确值或nextafter(预设)匹配、非预设不匹配、无显示整数/px碰撞、非法Explicit和无效index测试。
- 最好使用真实FineDial从非预设7DIP调到6DIP再Commit(false)，断言真实Selection仍ExplicitDip且只Thick高亮；取消或过期revision仍不写入、不出现错误高亮。触摸/Slider用真实PublishThicknessSliderCandidate(3DIP)+SetPenWidthIfRevision(false)模拟已存在CPU发布/有效提交接缝，明确不是整套HWND消息循环实机测试。
- 对StraightLine/DashedLine/Rectangle循环切换真实工具，逐档SetPenWidthPresetIfRevision(false)，assert同Context的GetPenWidthMetrics等于画笔GetBarThicknessPresetMetrics；再同值ExplicitDip提交后assert匹配且其它形状/Brush独立槽不变。
- 跨显示/设备模式变化：Preset保持身份重新解析；Explicit保留DIP，只有它仍等于新场景推荐值才继续亮，不能为高亮转成Preset。

### External references / Related specs

本轮仅内部代码研究，不新增外部API或依赖；延续已有Windows DIP合同。相关规范为 native-desktop/pen-width.md、cpp-conventions.md的StateMode快照/revision段，以及rendering-and-ui.md的统一串行调度/动画合同。主会话应同步pen-width规范和任务design里ExplicitDip无选中态的旧说明。

## Caveats / Not Found

- 尚未构建、运行测试或执行GUI/触摸实机验证；这些由主会话和独立check负责。
- Geometry三个档位在当前代码已共用Brush策略，未发现需修复的独立数值表；只改其共用高亮规则，新增测试证明行为。
- 过大异常显示尺寸会触发PresetDip的安全中档fallback，可能使多个逻辑预设返回同DIP；正常输出模式的三档各不相等。Preset身份始终唯一；如Explicit匹配异常fallback仍要求唯一，可以在匹配时明确稳定索引优先，但不要引入宽容吸附或更改状态。
- 连续Slider真实手指/像素位置未必精确命中预设DIP。本轮明确的严格数值匹配不会把“显示四舍五入相同但真实值不同”当成相等；不擅自新增snap。
