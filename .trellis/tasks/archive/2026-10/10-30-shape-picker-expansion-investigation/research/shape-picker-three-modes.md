# Research: 形状选择窗口无法展开及 PPT / 桌面 / 白板三态核查

- Query: 用户报告白板中形状选择窗口无法展开；定位直接原因，核查按钮命中/点击、展开状态、布局尺寸、窗口可见性与 workspace，并检查类似展开面板。
- Scope: internal
- Date: 2026-10-30
- 证据边界: 本轮仅做源码、测试源码与 Spec 静态审查；未构建、未运行测试、未启动 GUI、未操作真实 PPT。下文“受影响/不受影响”均为代码推断，不是运行结论。

## Findings

### 结论摘要

| 工作区 | 静态结论 | 直接依据 |
|---|---|---|
| PPT (`WorkspaceScene::Presentation`) | **代码推断不受此问题影响** | `WhiteboardActive()` 为 false；在已选形状、主栏未折叠、Geometry 可见时，`PresetHoming` 不会清除展开状态。 |
| 桌面 (`WorkspaceScene::Desktop`) | **代码推断不受此问题影响** | 与 PPT 共用同一条非白板分支；没有发现桌面专属的永久收起条件。 |
| 白板 (`WorkspaceScene::Whiteboard`) | **代码推断受影响；直接原因已定位** | `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp:1533-1534` 在每次面板准备时无条件执行 `if (whiteboard) geometryAttribute = false`，先于布局目标提交。 |

**直接原因不是按钮命中失败、300 ms 点击合并、布局尺寸不足或独立 popup HWND 未显示。** 点击回调能写入 `geometryAttribute=true`，但同一次渲染的 `PrepareMainPanels` 又在 `SubmitTargetsAndLayout` 前将其清零，导致渲染与命中始终消费关闭态。

### Files Found

| File Path | Description |
|---|---|
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | Geometry 点击回调、三态按钮可见性、直接原因 `PresetHoming`。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | 主按钮 Down/Up 命中与回调执行、面板子按钮命中、通用面板收起。 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 渲染前调用顺序、Geometry 展开/紧凑尺寸与透明度、主栏 HWND 包络。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` | Geometry 尺寸常量、`WhiteboardActive()` 的 committed workspace 真值。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp` | Geometry 面板及子控件初始化，初始为紧凑透明态。 |
| `Inkeys/Inkeys/UI/Bar/Bar.State.cppm` | `geometryAttribute` 等展开状态定义。 |
| `Inkeys/Inkeys/UI/Bar/Bar.ToggleClickCoalescer.cppm` | Geometry 独立 toggle 通道与 300 ms 合并算法。 |
| `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp` | 三工作区 Geometry 按钮可见性测试；不含展开保持断言。 |
| `InkeysHeadlessTests/toggle_click_coalescer_tests.cpp` | 300 ms 合并和通道独立性测试；不覆盖 workspace 或渲染前状态规范化。 |
| `.trellis/spec/native-desktop/rendering-and-ui.md` | Geometry toggle 合同、workspace 转换时一次性收起合同。 |
| `.trellis/spec/native-desktop/whiteboard.md` | 三工作区、共享 UI3 与白板窗口合同。 |

## 完整状态流

### 1. 形状主按钮命中与点击链是通的

1. 主按钮列表逐项跳过空按钮、不可见按钮、分隔线和禁用按钮；命中采用 `HitPresentedButton`，见 `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:6155-6171`。
2. 只有 Down 后在同一按钮上收到明确 `WM_LBUTTONUP` 才执行回调，见 `Bar.Interaction.cpp:6173-6204`。
3. Geometry 走普通 `temp->clickFunc()`，见 `Bar.Interaction.cpp:6235-6240`；成功后调用 `UpdateRendering()`，见 `Bar.Interaction.cpp:6255-6257`。
4. Geometry 点击回调位于 `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp:431-455`：
   - 当前不是 `IdtShape`：首击只调用 `ChangeStateModeToShape()`，并令绘制/几何属性关闭（`:433-440`）。
   - 当前已是 `IdtShape`：经 `BarToggleChannel::GeometryAttribute` 门控后计算 `open=!geometryAttribute`，打开时关闭 More/绘制属性，再写 `geometryAttribute=open`（`:443-453`）。
5. 因此正常复现路径是“第一次选择形状工具，第二次点击展开”，不是首击必须展开。300 ms 合并只发生在已经处于形状工具后的真正 toggle 上，见 `Bar.ToggleClickCoalescer.cppm:92-108` 和 Spec `rendering-and-ui.md:1537-1545`。即使间隔超过 300 ms，白板仍会被下一条强制清零，故它不是根因。

### 2. 白板在渲染前无条件撤销展开

调用顺序可直接串联：

```text
Geometry Up / clickFunc
  -> geometryAttribute = true                         Bar.Button.cpp:443-453
  -> UpdateRendering()                               Bar.Interaction.cpp:6255-6257
  -> 渲染帧 PrepareMainPanels(stateModeSnapshot)      Bar.RenderLoop.cpp:13485-13490
  -> PresetHoming(stateMode)                         Bar.Button.cpp:936-940
  -> WhiteboardActive()==true
  -> geometryAttribute = false                       Bar.Button.cpp:1531-1534
  -> SubmitTargetsAndLayout(...)                     Bar.RenderLoop.cpp:13490
```

关键代码：

- `Bar.Button.cpp:1531-1534`：取 `whiteboard=WhiteboardActive()`，并无条件关闭 Geometry。
- `Bar.Button.cpp:1542-1546`：只有 **非白板** 才进入常规关闭条件：非 Shape、主栏折叠、Geometry 不可见。
- `WhiteboardActive()` 不读取缓存猜测，而是判断 `GetWorkspaceSceneSnapshot().committed == WorkspaceScene::Whiteboard`，见 `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:521-524`；三态枚举及 committed 字段见 `Inkeys/IdtState.h:139-149`。

所以稳定白板中，每一次合法打开都会在同一渲染目标提交前被撤销。 workspace 转换时收起面板本身是合同要求，但这里额外造成了“稳定白板内之后也永远不能重开”。Spec 只要求每次 workspace 转换先 `CollapseAuxiliaryPanels(true)`、进入稳定白板时面板保持关闭（`rendering-and-ui.md:1736`），并未找到“稳定白板永久禁用 Geometry 展开”的合同；通用 toggle 合同也没有白板例外（`rendering-and-ui.md:1537-1545`）。

### 3. 布局与可见性只是消费被清零后的状态

- 展开常量：`335×100 DIP`；紧凑宽度 `60 DIP`，紧凑比例 `60/335`，见 `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:112-118`。
- `geometryAttribute=true` 时，面板目标为 `335×100`，相对 Geometry 主按钮向上/向下偏移并保留 10 DIP 间隔；表面和边框透明度转为可见，见 `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:4442-4464`。
- `geometryAttribute=false` 时，面板回到主按钮中心的紧凑尺寸，`y=0`，表面与边框透明度目标均为 0，见 `Bar.RenderLoop.cpp:4442-4464`。
- 子形状图标、标签、分割线和关闭按钮的透明度也直接由 `geometryAttribute` 控制，见 `Bar.RenderLoop.cpp:4548-4563`、`:4581-4582`、`:4595-4613`、`:4626-4628`。
- 面板初始化确实是紧凑透明态，见 `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp:1147-1165`；这与最终故障外观一致，但初始化不是根因，真正阻止离开该状态的是 `PresetHoming`。
- 面板子按钮的 hover/click 也要求 `geometryAttribute=true`，见 `Bar.Interaction.cpp:5407-5433`、`:6280-6287`。所以被清零后不仅看不到，也无法通过子按钮命中链补救。

### 4. 窗口归属与尺寸不是直接阻断点

- Geometry 是主栏 `shapeMap` 内的 `GeometryAttributeBar`，不是单独创建的 popup HWND；它以 Geometry 主按钮为父锚点，见 `Bar.RenderLoop.cpp:8064-8071`。
- 主栏窗口横向容量固定把 `BarGeometryAttributeExpandedWidth` 纳入预留，见 `Bar.RenderLoop.cpp:7754-7762`。
- Geometry 当前/动画包络参与可见内容边界与窗口 viewport 计算，见 `Bar.RenderLoop.cpp:8277-8286`、`:9114-9171`。
- 因而不存在“白板另有形状窗口但未 Show/未加入 workspace”的代码路径。直接失败发生在这些包络计算之前：状态已被清零，窗口只能收到透明紧凑面板。
- UI3 是唯一生产悬浮栏入口，旧 `IdtFloating`/旧 `IdtWindow` 不参与编译，见 `.trellis/spec/native-desktop/index.md:5`；无需到旧 UI 查找另一套形状窗口。

## 三态逐项判断与触发条件

### PPT：代码推断不受影响

在 `WorkspaceScene::Presentation` 下 `WhiteboardActive()==false`。满足以下常规条件时，`PresetHoming` 不清除 Geometry：

- `stateMode.StateModeSelect == IdtShape`；
- `barState.fold == false`；
- Geometry 主按钮 `IsVisible()==true`；
- 第二次真实 toggle 未被 300 ms 同通道合并。

对应关闭条件见 `Bar.Button.cpp:1542-1546`。Geometry 在空白 Selection 的紧凑栏会隐藏，但这不是 Shape 展开状态；按钮选中样式仅由 `IdtShape` 决定，见 `Bar.Button.cpp:966-999`。未发现 PPT 专属永久清除 Geometry 的分支。真实 Office/WPS、原生输入与视觉仍需运行确认。

### 桌面：代码推断不受影响

与 PPT 使用同一非白板分支和相同触发条件。未发现 Desktop 专属永久清除 Geometry 的分支。浮动/贴底、折叠展开、DPI 与边缘换向的实际视觉仍需运行确认。

### 白板：代码推断受影响，直接原因确定

- 白板并不通过 `compactSelection` 隐藏 Geometry：`compactSelection = !whiteboard && selection && !hasContent`，见 `Bar.Button.cpp:961-968`；Geometry 的 `hide=compactSelection`，见 `:996-999`。这与“按钮存在且可选择形状，但面板不展开”的报告一致。
- 只要 committed workspace 是 Whiteboard，`PresetHoming` 每次都清零，见 `:1531-1534`；与工具、fold、按钮可见性无关。
- 首击选工具、第二击尝试打开、超过 300 ms 再试，均不能绕过该清零。
- 从 Desktop/PPT 进入 Whiteboard 时，通用 `CollapseAuxiliaryPanels` 还会关闭所有辅助面板，见 `Bar.Interaction.cpp:3346-3357`；这是一次性转换行为，不是永久故障根因。最小修复应保留该转换合同。

## 类似展开面板核查

| 面板 | 是否受同一永久清零影响 | 静态依据与边界 |
|---|---|---|
| 绘制属性 `drawAttribute` | **代码推断不受此特定问题影响** | `PresetHoming` 只在非 Pen、fold 或 Draw 不可见时关闭（`Bar.Button.cpp:1535-1540`），没有 Whiteboard 无条件分支；点击打开见 `:315-350`。 |
| 橡皮属性及灵敏度子面板 | **代码推断不受此特定问题影响** | 第二击经独立通道打开（`Bar.Button.cpp:382-399`）；`PresetHoming` 不含 Whiteboard 永久关闭橡皮状态。 |
| 撤回/重做 `recallAttribute` | **代码推断不受此特定问题影响** | 打开入口见 `Bar.Button.cpp:487-491`、`:513-517`；`PresetHoming` 仅 fold 时额外 Close（`:1548-1550`），另有既有 idle/workspace 规则。 |
| 主栏 More | **代码推断不受此特定问题影响** | 点击开关见 `Bar.Interaction.cpp:6210-6227`；只受 fold/无 More 内容等条件约束（`Bar.Button.cpp:971-977`、`:1548`）。 |
| 笔型菜单、颜色选择器、粗细 popup | **代码推断不受此特定问题影响** | 它们属于绘制属性下游，未被 Whiteboard Geometry 条件直接写入；仍受父面板和通用关闭规则约束。 |
| 白板左下 More / Theme | **代码推断不受此特定问题影响** | 属于 PageControl/分页 surface，而非 `geometryAttribute`；其菜单与主题在同一左下 HWND 内展开，合同见 `whiteboard.md:7-9`。 |

通用 `CollapseAuxiliaryPanels` 会一次性关闭 Eraser/Recall/Draw/Geometry/More/笔型菜单/粗细/颜色面板（`Bar.Interaction.cpp:3346-3357`），在落笔和 workspace 转换等路径会被调用。这能解释“某次操作后面板被收起”，不能解释“稳定白板每次点击都无法重新展开”。除 Geometry 外，本轮未在 Bar 中找到同形的 Whiteboard 每帧永久清零条件；未运行验证这些面板的全部交互。

## 现有测试能静态证明什么

1. `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp:795-825` 的三工作区矩阵遍历 Desktop/Presentation/Whiteboard、五种工具和内容状态；`:811-814` 明确期望白板中 Geometry 按钮保持可见。这支持“按钮应存在”，但 **没有点击 Geometry，也没有断言 `geometryAttribute` 经 `PrepareMainPanels` 后保持 true**，所以不能证伪本缺陷。
2. `InkeysHeadlessTests/toggle_click_coalescer_tests.cpp:24-54` 静态覆盖首击、299 ms 合并、300 ms 边界和 Geometry 独立通道；`:156-168` 注册执行这些测试。它证明合并器的设计意图，不覆盖 workspace、点击回调后的渲染准备或面板像素。
3. Geometry 文本尺寸测试位于 `Bar.EraserAttribute.Test.cpp:1205-1211`，只覆盖本地化标签，不覆盖展开状态。
4. 未找到现有“三态第二次点击 Geometry -> UpdateRendering/PrepareMainPanels -> 仍展开”的测试，也未找到白板 Geometry 展开像素/命中测试。**本轮未执行上述测试，不能引用为运行通过证据。**

## 最小修复方向（仅建议，未修改）

1. 在 `Bar.Button.cpp:1531-1547` 删除 Whiteboard 对 Geometry 的永久禁用：去掉 `:1534` 的无条件清零，并去掉常规关闭条件 `:1542` 的 `!whiteboard &&` 限制。
2. 保留其余常规条件不变：非 Shape、fold、Geometry 不可见时仍关闭；保留 workspace 转换的统一 `CollapseAuxiliaryPanels(true)`，不要为实现重开而取消转换时收起。
3. 不建议修改点击合并器、335×100 布局、主栏 HWND 尺寸或 PageControl；这些均不在直接原因链上，改动会扩大风险。
4. 最小回归断言应直接覆盖 `PresetHoming/PrepareMainPanels`：三种 committed workspace 中，Shape + unfolded + Geometry visible + `geometryAttribute=true` 经准备后仍为 true；非 Shape/fold/invisible 三种负例仍应收起。

## 可执行验证建议

### A. 无窗口生产状态回归（修复后优先）

在现有 Bar 离屏测试中加入并执行三态用例，使用真实 `WorkspaceSceneScope`、`SetWhiteboardActive` 和 Geometry 点击回调：

1. 每个场景从非 Shape 开始，首击只进入 Shape 且不展开；第二击打开。
2. 第二击后调用真实 `UpdateRendering` 或至少 `PrepareMainPanels`，再次读取状态；白板必须不再被清零。
3. 为避免 300 ms 合并干扰，每个场景使用新 owner/新 coalescer，或注入间隔达到 300 ms 的时钟；不要以 sleep 掩盖状态问题。
4. 断言根面板目标为 `335×100`、表面透明度目标非零、位置离开紧凑中心；推进动画后验证直线/矩形/粗细/Close 的实际呈现命中。
5. 补 Desktop/PPT 防回归，以及切工具、fold、Geometry 隐藏、workspace 转换后仍需关闭的负例。

现有入口可参考：

```text
Inkeys.exe --bar-eraser-offscreen-test
InkeysHeadlessTests.exe --no-window
```

`--bar-eraser-offscreen-test` 的产品入口见 `Inkeys/IdtMain.cpp:1536-1538`；该测试文件已包含三工作区 fixture，适合承载上述生产状态回归，但当前没有 Geometry 展开断言。`--no-window` 会执行 toggle coalescer 测试，见 `InkeysHeadlessTests/animation_tests.cpp:1730-1731`、`:1795-1799`。这些是当前可执行入口建议，不代表本轮已运行。

### B. 构建与真实三态验收

按仓库合同使用完整 Solution、Debug 和当前设备原生架构：

```text
MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=<NativePlatform>
```

入口依据为 `.trellis/spec/native-desktop/build-and-compatibility.md:8-21`，构建超时应至少 5 分钟。构建后分别人工验证：

- Desktop、真实 PPT、Whiteboard 中“首击选 Shape、第二击展开”，再点击切换直线/矩形和三档粗细。
- 展开/收起动画开关、浮动/贴底、折叠后重开、屏幕上下换边、100%/150%/200% 或相应 DPI。
- Desktop→PPT→Whiteboard 及反向转换：转换当次应收起，稳定后第二次点击必须能重开。
- 对照 Draw/Eraser/Recall/More 与白板 More/Theme，确认没有引入互斥或常驻面板回归。

真实 PPT、任务栏、ULW/DWM、触摸和视觉结果不能由 headless 测试替代，验收记录需单列实际环境；本轮这些项目全部未执行。

## Caveats / Not Found

- 未找到要求“稳定白板永久禁用 Geometry 面板”的 Spec；现有合同只要求 workspace 转换时收起。
- 未找到其他主栏面板存在同样的 Whiteboard 永久清零条件。
- 未找到现有三态 Geometry 展开保持/像素/命中回归测试。
- 没有本轮构建、测试或 GUI 运行证据；PPT/桌面的“不受影响”和其他面板的判断均仅限代码推断。

## 修复实施与验证记录（2026-10-30）

- `Bar.Button.cpp::PresetHoming` 已删除 Whiteboard 对 Geometry 的永久清零，并让三种工作区统一执行非 Shape、fold、Geometry 不可见时收起的规则；Draw 与 workspace 转换时的 `CollapseAuxiliaryPanels` 未改。
- `Bar.EraserAttribute.Test.cpp` 已复用真实 `UpdateRendering → PrepareMainPanels` 路径，增加 Desktop/Presentation/Whiteboard 展开保持，以及非 Shape、fold、Geometry invisible 收起负例。
- 实施代理实际执行完整 `MSBuild.exe InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64 -m`：成功，0 errors；仅有 6 条既有 hashlib++ C4267 警告。
- 实施代理实际执行新产物 `./Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test`：退出码 0，通过。
- `git diff --check` 通过。Trellis 质量审查未发现问题，也未追加代码修改。
- 完整 `./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 首次在 300 秒上限终止；主会话随后以 900 秒上限重跑，约 362 秒完成，退出码 0，通过。
- 用户已于 2026-10-30 确认真实三态人工验证通过；本任务可以结束并提交。
