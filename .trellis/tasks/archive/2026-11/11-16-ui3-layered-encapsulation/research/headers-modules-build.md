# UI3 头文件、模块依赖、工程登记、日志与规范现状调查

## 最重要发现

- 【事实】当前 Bar 包含根模块、12 个分区、11 个独立子模块，共 24 个接口单元；分区与独立模块并存。证据见下文模块邻接表，入口为 `B/Bar.cppm:1`（`Inkeys.UI.Bar`）。
- 【事实】26 个被调查头均含函数声明、函数实现或带行为的类，没有仅引用/聚合的头。按 D4 均属于转换候选；两个 Startup 头不在 D4 明确列举的强制转换范围内。证据见头文件表及 `PRD:116`（D4）。
- 【事实】`Module::UI3` 已有使用者，背景中“尚无使用者”已过期：`B/Bar.Interaction.cpp:1609`（`InvokeSurfaceCallback`），`Inkeys/IdtMain.cpp:3147`（诊断 sink）。
- 【事实】范围内所有 `.h` 实际都是 **UTF-8 无 BOM、CRLF**，不能统一补 BOM；工程 XML 也无 BOM。证据位置及字节检查口径见编码表。
- 【事实】产品源码的目录外直接导入方为 13 个；另有 11 个 Headless 测试源和一个在册调研探针。`Window.Legacy.cpp` 没有直接导入 Bar，见 `W/Window.Legacy.cpp:1`（import 列表）。
- 【事实】Headless 工程直接编译 UI 和 Log 源码；当前未发现其 `.vcxproj.filters`。现有源登记见 `HP:97`（`ClCompile`）。
- 【事实】诊断聚合仍在 RenderPipeline，但产品输出已接统一日志；诊断规范第 45 行仍描述旧 async logger。证据：`RP/RenderPipeline.cpp:560`（`EmitDiagnostics`）、`Inkeys/IdtMain.cpp:3143`（`SetDiagnosticsSink`）、`SD/ui3-render-diagnostics.md:45`（Contracts）。

## 1. 范围、证据及缩写

本次仅直接读取文件和 Git 索引，未修改文件、未运行构建或测试。命令行进程首次被拒绝后，改用文件 API，没有重试子进程。分支依据工作树 Git 元数据中的 `HEAD`，为 `refactoring/drawpad-ui`。

报告中的路径缩写可机械展开：

| 缩写 | 路径 |
|---|---|
| `B/` | `Inkeys/Inkeys/UI/Bar/` |
| `U/` | `Inkeys/Inkeys/UI/` |
| `RP/` | `Inkeys/Inkeys/UI/RenderPipeline/` |
| `W/` | `Inkeys/Inkeys/Window/` |
| `T/` | `InkeysHeadlessTests/` |
| `R/` | `.trellis/tasks/09-22-ui3-animation-stutter-investigation/research/` |
| `P` / `PF` | `Inkeys/Inkeys.vcxproj` / `Inkeys/Inkeys.vcxproj.filters` |
| `HP` | `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj` |
| `SD/` / `SN/` | `.trellis/spec/native-desktop/` / `.trellis/spec/native/` |
| `PRD` | `.trellis/tasks/11-16-ui3-layered-encapsulation/prd.md` |

引用清单以当前在册源码为范围；本地 `Build/.../backup`、`.trellis/.runtime/...` 历史副本不计为当前消费者。在册调研探针单列，未混入产品或测试工程。

## 2. 头文件逐个分类

### 2.1 内容、自身依赖与归属

下表“内容与依赖”为【事实】；“转换/归属”为【推测】，是依据 D4 和当前职责的分类判断，不代表已经确定最终迁移边界。行数不计末尾换行产生的空元素。

标准库头以名称简写，均表示对应的 `<名称>`；没有列出的自定义宏即未发现。26 个头均未直接包含 `IdtMain.h`。

| 头文件、行数及符号证据 | 内容、自身 include、顺序风险【事实】 | 转换与归属分类【推测】 |
|---|---|---|
| `B/Bar.A2.h`，110；`:11` `BarWhiteboardButtonAction`、`:70` `BarA2CallbackDispatcher` | 枚举、结构、类、函数模板；functional/cstdint/mutex/string_view/utility（`:3` 起） | 转 cppm：有行为与模板；Bar：白板按钮动作和 A2 投影 |
| `B/Bar.AutoCollapse.h`，18；`:8` `ShouldPauseDrawingAutoCollapse` | 两个 constexpr 函数；BottomDock（`:3`） | 转：有策略函数；Bar：绘制自动收起政策 |
| `B/Bar.AutoCollapseInteraction.h`，45；`:8` `DragCollapseGate` | 两个有方法的类；cstdint、AutoCollapse（`:3`） | 转：有交互状态；Bar：自动收起候选管理 |
| `B/Bar.BottomDock.h`，1868；`:30` `BarBottomDockMode`、`:798` `BarBottomDockDragTracker` | 常量、枚举、结构、类、模板、内联函数；Windows/algorithm/atomic/cmath/cstdint/optional/limits（`:6`）；宏 `NOMINMAX`（`:4`） | 转：大量实现；Bar：底栏吸附、工作区、提示与拖动政策混合 |
| `B/Bar.ButtonFrameRing.h`，73；`:12` `ButtonFrameRing` | 带资源与 Draw 方法的结构；windows/d2d1_1/wrl/client/algorithm、PageLoadingIndicator（`:2`） | 转：有缓存与绘制；UI3：页码加载和长按进度共用环（`:10`） |
| `B/Bar.DirtyRegion.h`，366；`:27` `BarDirtyRegionTracker` | 常量、类、结构、内联函数；Windows/cmath/cstdint/unordered_map/vector（`:3`） | 转：有跟踪算法；UI3：脏区与调试包络，已有分页消费者 |
| `B/Bar.DisplayTransition.h`，79；`:21` `ResolveBarDisplayPlacement` | 结构、内联函数、函数模板；Windows/algorithm/cmath；宏 `NOMINMAX`（`:4`） | 转：有位置换算；Bar：现有显示器切换放置语义 |
| `B/Bar.FontSize.h`，14；`:7` `ResolveBarFontCorrectionTarget` | 内联函数；cmath（`:3`） | 转：有字号修正算法；UI3：不读取主栏状态 |
| `B/Bar.Haptics.h`，224；`:35` `ContactTokens`、`:192` `ButtonMotionGate` | 常量、结构、类、内联函数及 inline thread_local 状态（`:214`）；Draw3.HapticRequest/windows/utility（`:3`） | 转：有接触与按钮状态；UI3 候选，但耦合 Draw3 触觉类型 |
| `B/Bar.I18nFormat.h`，77；`:13` `FormatThicknessText` | 内联格式化函数；PenWidthPolicy.hpp、algorithm/cmath/format/string/string_view（`:3`） | 转：有格式化与异常回退；Bar：线宽业务单位格式 |
| `B/Bar.InteractionWorker.h`，236；`:19` `InteractionTask`、`:139` `ButtonPointerState` | 协程/邮箱模板、结构、类、函数声明；Windows/chrono/condition_variable/coroutine/cstdint/deque/exception/functional/mutex/memory/optional/utility（`:3`） | 转：有基础交互与工作器；UI3 候选，基础状态与 HWND 调度同时存在 |
| `B/Bar.KeyboardShortcuts.h`，657；`:9` `BarShortcutBackend`、`:33` `BarKeyboardShortcutOwner` | 无 include；匿名命名空间（`:4`）、类模板、静态成员；明确要求 import 后包含（`:3`），直接访问 `barUISet`（`:27`） | 转：实质实现；Bar：工具/画布快捷键业务，不能按普通自包含头处理 |
| `B/Bar.LogoAppearance.h`，78；`:10` `Snapshot`、`:32` `Resolve` | 常量、结构、函数及模板；ThemeMaterial/string（`:3`） | 转：有外观计算；Bar：注释明确“主按钮专属”（`:6`） |
| `B/Bar.MainLayoutPolicy.h`，96；`:7` `BarIconOrder`、`:81` `BarMainExpansionCache` | 枚举、结构、类、内联函数；cmath（`:3`） | 转：有布局政策；Bar：图标顺序与展开方向 |
| `B/Bar.PageLoadingIndicator.h`，116；`:43` `PageLoadingIndicator` | 常量、枚举、结构、内联函数；algorithm/atomic/chrono/cmath/cstdint（`:2`） | 转：有加载动效；UI3：Scene 与分页共用 |
| `B/Bar.Presentation.Source.h`，93；`:30` `Ui3FixtureInputV1`、`:65` `CompiledUi3FixtureInputs` | 常量、枚举、结构及封装函数声明；PresentationProbe、Ui3PresentationFixtureAuth、Windows/cstddef/cstdint/span（`:3`） | 转：不是纯聚合头；Bar：固定 MainFold/DrawAttribute fixture 授权与输入协议 |
| `B/Bar.PresentationAlpha.h`，108；`:15` `PresentationAlphaState` | 结构、带 constexpr 方法的类；cstdint（`:3`） | 转：有提交状态机；Bar：启动预览与主栏 alpha 交接 |
| `B/Bar.PresentationProbe.h`，582；`:83` `Ui3FinitePublication`、`:489` `Ui3FiniteObserver` | 常量、枚举、结构、类及方法声明；array/atomic/cstddef/cstdint/span/type_traits（`:3`） | 转：有观察与发布协议；Bar：有限场景证明与 fixture 耦合 |
| `B/Bar.PresentDecision.h`，410；`:204` `BarPresentDecision` | 枚举、结构、类、constexpr 函数；Windows/d2d1/dxgi/cstdint/limits（`:3`） | 转：有失败退避与提交决策；UI3 宿主能力候选，仍含 Bar 映射语义 |
| `B/Bar.ShapeGesture.h`，83；`:12` `ShapeContactGesture` | 类及内联方法；Draw3.ShapeInteraction、algorithm/array/cmath/cstdint（`:3`）；使用 `draw3::shapes::Vec2`（`:15`） | 转：有手势算法；Bar 现有形状编辑专有，非普通基础控件 |
| `B/Bar.StatusIndicator.h`，1286；`:14` `StatusIndicatorKind`、`:277` `StatusIndicatorModel` | 常量、枚举、结构、类、内联函数；algorithm/array/cmath/cstddef/cstdint/limits/optional/string/vector（`:2`） | 转：有完整模型/布局；Bar：PPT、SuperTop、更新等产品状态 |
| `B/Bar.ThemeMaterial.h`，351；`:17` `ColorRole`、`:189` `Resolve` | 常量、枚举、结构、内联/constexpr 函数；algorithm/cmath/cstdint（`:3`） | 转：有材质计算；UI3：无设备策略，Bar 与 StartupPreview 共用；仍含笔色政策 |
| `B/Bar.WindowGeometry.h`，567；`:13` `BarWindowPresentationTransaction`、`:503` `BarWindowViewportController` | 结构、类、内联/constexpr 函数；Windows/algorithm/cmath/limits/mutex（`:3`） | 转：有事务与几何算法；Bar：包含主栏粗细预览预约和固定拖动政策 |
| `U/Superellipse.Geometry.h`，81；`:13` `CreateSuperellipsePath` | 内联函数；d2d1_1/d2d1helper/wrl/client/algorithm/cmath/vector（`:3`） | 转：有路径构造；UI3：主栏与启动预览共用 |
| `U/StartupPlacement.h`，125；`:17` `StartupPlacement`、`:85` `ResolveStartupPlacementBounds` | 枚举、常量、constexpr/inline 函数；windows/algorithm/cmath、BottomDock（`:3`）；push/undef/pop `min`/`max`（`:5`） | 按内容应转；主栏启动政策，非通用 UI3。是否迁入 Bar 不由 D4 自动决定 |
| `U/StartupPreview.Effect.h`，19；`:5` `StartupPreviewEffect`、`:13` `NormalizeStartupPreviewEffect` | 枚举及 constexpr 函数；无 include | 按内容应转；启动效果配置专有，不能仅因位于 UI 根目录归入 UI3 |

### 2.2 全部直接 includer

以下均为【事实】，每个位置的证据符号为对应 `#include` 指令。

分类：`G`＝模块接口全局模块片段；`M`＝模块实现 `.cpp`；`C`＝传统非模块 `.cpp`；`H`＝其他头；`T`＝测试源。`T-M` 表示主工程中的模块测试单元。只有 KeyboardShortcuts 的包含位于 import 之后，其余列出的模块实现包含位于全局模块片段。

| 被包含头 | 全部在册直接 includer |
|---|---|
| `Bar.A2.h` | C `Inkeys/IdtState.cpp:14`；M `B/Bar.Button.cpp:13`、`B/Bar.Main.cpp:11`；T `T/bar_bottom_dock_tests.cpp:16`、`T/page_control_tests.cpp:14`、`T/ppt_session_tests.cpp:2` |
| `Bar.AutoCollapse.h` | H `B/Bar.AutoCollapseInteraction.h:4`；M `B/Bar.Interaction.cpp:20`；T `T/auto_collapse_tests.cpp:5` |
| `Bar.AutoCollapseInteraction.h` | M `B/Bar.Interaction.cpp:21`；T `T/auto_collapse_interaction_tests.cpp:1` |
| `Bar.BottomDock.h` | G `B/Bar.Main.cppm:16`；H `B/Bar.AutoCollapse.h:3`、`U/StartupPlacement.h:11`；M `B/Bar.Button.cpp:18`、`B/Bar.ConstructionPrompt.cpp:3`、`B/Bar.Interaction.cpp:19`、`B/Bar.Main.cpp:12`、`B/Bar.RenderLoop.cpp:20`、`B/Bar.State.cpp:8`、`B/Bar.StatusIndicator.cpp:7`、`U/PageControl/PageControl.cpp:25`；T-M `B/Bar.MultiTarget.Test.cpp:10`；T `T/animation_tests.cpp:22`、`T/bar_bottom_dock_tests.cpp:14`、`T/dirty_region_tests.cpp:1`、`T/page_control_tests.cpp:17`、`T/ppt_session_tests.cpp:3`、`T/window_geometry_tests.cpp:1`；调研 C `R/geometry_probe.cpp:2` |
| `Bar.ButtonFrameRing.h` | G `B/Bar.Main.cppm:19`；M `B/Bar.GeometryAttribute.cpp:6`、`B/Bar.Scene.cpp:16`；T-M `B/Bar.GeometryAttribute.Test.cpp:11` |
| `Bar.DirtyRegion.h` | M `B/Bar.RenderLoop.cpp:18`、`B/Bar.Scene.cpp:12`、`U/PageControl/PageControl.cpp:15`；T `T/bar_bottom_dock_tests.cpp:18`、`T/dirty_region_tests.cpp:2` |
| `Bar.DisplayTransition.h` | M `B/Bar.RenderLoop.cpp:23`；T `T/bar_display_transition_tests.cpp:10` |
| `Bar.FontSize.h` | G `B/Bar.Animation.cppm:9`；T `T/animation_tests.cpp:24`、`T/bar_bottom_dock_tests.cpp:15` |
| `Bar.Haptics.h` | M `B/Bar.Button.cpp:17`、`B/Bar.Interaction.cpp:38`；T `T/draw3_haptic_tests.cpp:1`、`T/ui_pointer_tests.cpp:11` |
| `Bar.I18nFormat.h` | M `B/Bar.Interaction.cpp:27`、`B/Bar.RenderLoop.cpp:24`、`B/Bar.State.cpp:10`；T `T/animation_tests.cpp:23` |
| `Bar.InteractionWorker.h` | G `B/Bar.Main.cppm:22`；C `Inkeys/IdtMain.cpp:50`；M `B/Bar.Button.cpp:16`、`B/Bar.GeometryAttribute.cpp:4`、`B/Bar.Initialization.cpp:15`、`B/Bar.Interaction.cpp:31`、`B/Bar.Scene.cpp:19`、`B/Bar.ShapeEditor.cpp:11`、`B/Bar.StatusIndicator.cpp:6`、`U/PageControl/PageControl.cpp:17`、`U/SelectionOverlay/SelectionOverlay.cpp:16`；T-M `B/Bar.MultiTarget.Test.cpp:12`；T `T/ui3_input_tests.cpp:2` |
| `Bar.KeyboardShortcuts.h` | M `B/Bar.Interaction.cpp:71`，import 后 |
| `Bar.LogoAppearance.h` | G `B/Bar.UI.cppm:9`；M `B/Bar.Initialization.cpp:4`、`B/Bar.Rendering.cpp:4`、`B/Bar.RenderLoop.cpp:4`、`B/Bar.UI.cpp:4`；T `T/bar_logo_tests.cpp:17` |
| `Bar.MainLayoutPolicy.h` | M `B/Bar.Main.cpp:13`、`B/Bar.RenderLoop.cpp:21`、`B/Bar.State.cpp:9`；T-M `B/Bar.EraserAttribute.Test.cpp:30`；T `T/bar_bottom_dock_tests.cpp:17` |
| `Bar.PageLoadingIndicator.h` | H `B/Bar.ButtonFrameRing.h:6`；M `B/Bar.Interaction.cpp:32`、`B/Bar.Scene.cpp:15`、`U/PageControl/PageControl.cpp:16`；T `T/page_control_tests.cpp:15` |
| `Bar.Presentation.Source.h` | C `Inkeys/IdtMain.cpp:49`、`B/Bar.Presentation.Source.cpp:1`；M `B/Bar.Button.cpp:15`、`B/Bar.Interaction.cpp:30`、`B/Bar.RenderLoop.cpp:28`；T-M `B/Bar.Presentation.Test.cpp:3`；T `T/render_scheduler_tests.cpp:18` |
| `Bar.PresentationAlpha.h` | M `B/Bar.RenderLoop.cpp:26`；T `T/bar_presentation_alpha_tests.cpp:1` |
| `Bar.PresentationProbe.h` | G `B/Bar.Main.cppm:26`、`B/Bar.UI.cppm:10`；H `B/Bar.Presentation.Source.h:3`；C `B/Bar.PresentationProbe.cpp:1`；M `B/Bar.Button.cpp:14`、`B/Bar.Interaction.cpp:29`、`B/Bar.Main.cpp:15`、`B/Bar.Rendering.cpp:24`、`B/Bar.RenderLoop.cpp:27`、`B/Bar.UI.cpp:9`；T-M `B/Bar.EraserAttribute.Test.cpp:29`、`B/Bar.MultiTarget.Test.cpp:13`；T `T/render_scheduler_tests.cpp:17` |
| `Bar.PresentDecision.h` | M `B/Bar.Main.cpp:14`、`B/Bar.RenderLoop.cpp:25`；T `T/bar_bottom_dock_tests.cpp:20`、`T/bar_presentation_alpha_tests.cpp:2`、`T/dirty_region_tests.cpp:4`、`T/present_decision_tests.cpp:1`、`T/render_scheduler_tests.cpp:16`；调研 C `R/scheduler-recovery-check.cpp:3` |
| `Bar.ShapeGesture.h` | M `B/Bar.ShapeEditor.cpp:12` |
| `Bar.StatusIndicator.h` | G `B/Bar.Main.cppm:18`；M `B/Bar.StatusIndicator.cpp:5`；T `T/status_indicator_tests.cpp:1` |
| `Bar.ThemeMaterial.h` | G `B/Bar.Rendering.cppm:11`、`B/Bar.RenderingAttribute.cppm:4`、`B/Bar.Theme.cppm:5`；H `B/Bar.LogoAppearance.h:3`、`U/StartupPreview/StartupPreview.Effects.Policy.h:4`；M `B/Bar.Button.cpp:21`、`B/Bar.ConstructionPrompt.cpp:4`、`B/Bar.GeometryAttribute.cpp:5`、`B/Bar.Initialization.cpp:17`、`B/Bar.Rendering.cpp:22`、`B/Bar.RenderLoop.cpp:19`、`B/Bar.Scene.cpp:14`、`B/Bar.ShapeEditor.cpp:14`、`B/Bar.StatusIndicator.cpp:9`；T-M `B/Bar.EraserAttribute.Test.cpp:11`、`B/Bar.GeometryAttribute.Test.cpp:10`；T `T/bar_theme_tests.cpp:17` |
| `Bar.WindowGeometry.h` | M `B/Bar.Button.cpp:19`、`B/Bar.Interaction.cpp:28`、`B/Bar.RenderLoop.cpp:31`；T-M `B/Bar.MultiTarget.Test.cpp:11`；T `T/bar_bottom_dock_tests.cpp:19`、`T/dirty_region_tests.cpp:3`、`T/window_geometry_tests.cpp:2`；调研 C `R/geometry_probe.cpp:3` |
| `Superellipse.Geometry.h` | M `B/Bar.Rendering.cpp:25`、`B/Bar.StatusIndicator.cpp:10`；H `U/StartupPreview/StartupPreview.Drawing.h:3` |
| `StartupPlacement.h` | G `B/Bar.Main.cppm:25`、`B/Bar.Zoom.cppm:4`、`U/StartupPreview/StartupPreview.cppm:7`；H `U/StartupPreview/StartupPreview.Drawing.h:4`；C `Inkeys/IdtMain.cpp:51`、`U/Setting/Setting.Pages.cpp:12`；M `B/Bar.Initialization.cpp:14`、`B/Bar.RenderLoop.cpp:22`、`U/Setting/Setting.cpp:27`、`U/StartupPreview/StartupPreview.cpp:25`；T-M `B/Bar.EraserAttribute.Test.cpp:31`、`B/Bar.GeometryAttribute.Test.cpp:5`、`B/Bar.MultiTarget.Test.cpp:9`；T `T/startup_preview_state_tests.cpp:8` |
| `StartupPreview.Effect.h` | G `Inkeys/Inkeys/Other/Other.Config.cppm:5`、`U/StartupPreview/StartupPreview.cppm:8`；H `U/StartupPreview/StartupPreview.Effects.Policy.h:3`；C `Inkeys/IdtMain.cpp:52`；M `Inkeys/Inkeys/Other/Other.Config.cpp:6`、`U/Setting/Setting.cpp:28` |

### 2.3 转换风险归纳

| 分类 | 证据与结论 |
|---|---|
| 【事实】传统源也已使用 import | `Inkeys/IdtMain.cpp:16`、`Inkeys/IdtState.cpp:30`（`import Inkeys.UI.Bar`）；`U/Setting/Setting.Pages.cpp:14`（`import Inkeys.Conv.Text`）。不能仅因“非模块 cpp”判定无法 import |
| 【待测】传统消费者转换可编译性 | 上表 C/T 消费者涉及模块声明身份、可见性和测试工程依赖；本次未编译，不能确认全部可直接转换 |
| 【事实】宏边界 | BottomDock/DisplayTransition 定义 `NOMINMAX`；StartupPlacement 保存并恢复 `min/max`。证据分别为 `B/Bar.BottomDock.h:4`、`B/Bar.DisplayTransition.h:4`、`U/StartupPlacement.h:5`（预处理指令）。这些预处理效果不能当作模块导出符号 |
| 【事实】匿名命名空间与包含顺序 | `B/Bar.KeyboardShortcuts.h:3` 明确 import 后包含，`:4` 匿名 namespace，`:27` 使用 `barUISet`，`:329` 使用 Log；它不是独立自包含接口 |
| 【事实】静态状态 | `B/Bar.Haptics.h:214`（`acceptedClick`）为 inline thread_local；`B/Bar.KeyboardShortcuts.h:347`（测试 Backend）有 inline static 数据。未发现这 26 个头自身的函数局部 static |
| 【事实】传递依赖存在局部 static | `B/Bar.ShapeGesture.h:3` 包含 Draw3.ShapeInteraction；后者 `Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeInteraction.h:21`（`ShapeToolEntryEpoch`）包含局部静态原子变量 |
| 【事实】产品类型耦合 | `B/Bar.Haptics.h:9` 使用 Draw3 `Click/Context`；`B/Bar.ShapeGesture.h:15` 使用 Draw3 几何类型；`B/Bar.KeyboardShortcuts.h:6` 使用输入与画布业务命名空间 |
| 【事实】传统实现单元不能漏项 | `B/Bar.PresentationProbe.cpp:1`、`B/Bar.Presentation.Source.cpp:1` 仍是 include 头的普通 cpp，并非 `module Inkeys.UI.Bar;` 实现 |
| 【事实】直接 includer 中未发现第三方或生成源 | 上表直接引用均来自第一方产品、测试或在册研究探针；例如生成国际化头是在调用方包含，见 `B/Bar.ShapeEditor.cpp:8`（`IdtI18nKeys.g.h`），不是它包含上述头 |

## 3. 模块组织与依赖图

### 3.1 接口邻接表

以下全部为【事实】。`E`＝`export import`，`I`＝普通 `import`；行号附在目标后。`:X` 表示 `Inkeys.UI.Bar:X`；`.X` 表示 `Inkeys.UI.Bar.X`。非 Bar 模块保留完整领域名，省略共同 `Inkeys.` 前缀。每行证据文件为 `B/Bar.<名称>.cppm`，根为 `B/Bar.cppm`；证据符号即 import 指令。

| 接口单元 | 完整直接依赖 |
|---|---|
| 根 | E `:Theme@3`、`:Main@4`、`:Layout@5`、`:Zoom@6`、`:Scene@7` |
| `:Atomic` | I `.WakeSignal@7` |
| `:State` | 无 import，模块声明 `:5` |
| `:UI` | I `:State@14`；E `.Animation@16` |
| `:Button` | I `:UI@13`、`:State@14`、`Other.Config@15` |
| `:Format` | I `<dwrite.h>@7`，是头单元 |
| `:RenderingAttribute` | I `:UI@8`、`:State@9` |
| `:Rendering` | I `:UI@17`、`:State@18`、`:Button@19`、`:Format@20`、`:RenderingAttribute@21`、`.Animation@23`、`UI.RenderPipeline@24` |
| `:Theme` | I `:State@13`、`.Metrics@14`、`UI.RenderPipeline@15` |
| `:Layout` | E `.Metrics@13`；I `:UI@15`、`:Theme@16`、`.Animation@18`、`Conv.Color@20` |
| `:Main` | I `:UI@31`、`:State@32`、`.EraserAttributeMotion@33`、`.RecallAttributeMotion@34`、`:Button@36`、`:Format@37`、`:RenderingAttribute@40`、`.Animation@42`、`Conv.Color@45`、`Helper.Thread@46`、`Message@47`、`UI.RenderPipeline@48`、`Display@49`；E `.RecallChrome@35`、`:Rendering@38`、`.Metrics@39`、`.ToggleClickCoalescer@43` |
| `:Scene` | E `.Metrics@19`、`.SurfaceLayout@20`；I `UI.RenderPipeline@22` |
| `:Zoom` | I `:Main@8`、`:Atomic@9`、`Other.Config@11`、`Display@12`、`UI.PageControl@13` |
| `.Animation` | 接口无 import，声明 `:19`；实现 `B/Bar.Animation.cpp:15` I `.Metrics` |
| `.Metrics` | 无 import，声明 `:1` |
| `.FramePacing` | 无 import，声明 `:16` |
| `.SurfaceLayout` | E `.Metrics@16` |
| `.WakeSignal` | 无 import，声明 `:8` |
| `.RecallChrome` | 无 import，声明 `:3` |
| `.ToggleClickCoalescer` | 无 import，声明 `:8` |
| `.EraserAttributeLayout` | E `.Metrics@8` |
| `.EraserAttributeMotion` | E `.EraserAttributeLayout@7`；I `.Animation@8` |
| `.RecallAttributeLayout` | E `.Metrics@7` |
| `.RecallAttributeMotion` | E `.RecallAttributeLayout@7`；I `.Animation@8`、`.EraserAttributeMotion@9` |

### 3.2 主模块实现单元导入的分区

以下全部为【事实】，文件均在 `B/`；单元声明为 `module Inkeys.UI.Bar;`。表中 `名称@行号` 的符号为 `import :名称`。

| `.cpp` | 显式分区导入 |
|---|---|
| `Bar.cpp` | 无；`:1` 只有模块声明 |
| `Bar.Button.cpp` | Button@33、Main@35、Layout@36、Theme@37 |
| `Bar.ConstructionPrompt.cpp` | Main@13、Scene@14、Layout@15、Theme@16、RenderingAttribute@17 |
| `Bar.EraserAttribute.cpp` | Main@15、Theme@16 |
| `Bar.EraserAttribute.Test.cpp` | Main@35、Scene@36、Theme@37 |
| `Bar.GeometryAttribute.cpp` | Main@15、Layout@16、Theme@17 |
| `Bar.GeometryAttribute.Test.cpp` | Main@27、Layout@28、Theme@29 |
| `Bar.Initialization.cpp` | Main@20、Layout@21、Atomic@22、Zoom@23、Theme@24 |
| `Bar.Interaction.cpp` | Main@46、Layout@47、Atomic@48、Theme@49 |
| `Bar.Main.cpp` | Main@22、Rendering@23、Layout@24、Atomic@25、Zoom@26、Theme@27 |
| `Bar.MultiTarget.Test.cpp` | Main@32、Atomic@33 |
| `Bar.Presentation.Test.cpp` | Main@31、Theme@32 |
| `Bar.RecallAttribute.cpp` | Main@13、Theme@14 |
| `Bar.Rendering.cpp` | Main@30、Rendering@31、UI@32、State@33、Button@34、Format@35、RenderingAttribute@36、Layout@37、Theme@38 |
| `Bar.RenderLoop.cpp` | Main@40、Scene@41、Rendering@42、Scene@43（重复）、Layout@44、Atomic@45、Zoom@46、Theme@47 |
| `Bar.Scene.cpp` | Scene@23、Main@24、Rendering@25、Button@26、State@27、Theme@28、UI@29 |
| `Bar.ShapeEditor.cpp` | Scene@31、Main@32、Layout@33、Theme@34 |
| `Bar.State.cpp` | State@13、Main@15 |
| `Bar.StatusIndicator.cpp` | Main@19、Theme@20 |
| `Bar.UI.cpp` | UI@22 |

### 3.3 实际相互引用与无环排布

- 【事实】Main 需要完整 renderer：`B/Bar.Main.cppm:38` 转导入 Rendering，`:895` 的 `BarUISetClass::spec` 是 `BarUIRendering` 值成员；Rendering 反向只前置声明 `BarUISetClass`，见 `B/Bar.Rendering.cppm:38`，构造函数接收其指针见 `:95`。完整的 Main 依赖只进入 `B/Bar.Rendering.cpp:30`。接口 import 图因此没有 Main↔Rendering 环。
- 【事实】Scene 只导出 `BarUIRendering` 前置声明，见 `B/Bar.Scene.cppm:21`；`BarPageSurface::Renderer` 返回引用见 `:152`，完整实现通过 `B/Bar.Scene.cpp:24`、`:25` 导入 Main/Rendering。
- 【事实】State 接口没有 Main import，但 `B/Bar.State.cpp:15` 可以导入 Main；UI/Button/Rendering 在接口侧单向依赖 State。证据：`B/Bar.State.cppm:5`（模块声明）、上述邻接表。
- 【事实】独立模块能被其他模块单独导入：Metrics 被 SurfaceLayout、两个 AttributeLayout 和 Config/StartupPreview 使用；FramePacing 被 RenderPipeline 实现直接导入。证据：`B/Bar.SurfaceLayout.cppm:16`、`B/Bar.EraserAttributeLayout.cppm:8`、`RP/RenderPipeline.cpp:32`。
- 【推测】这些独立模块形态有利于避免消费者引入整个 Bar 根接口；源码 import 关系支持这一解释，但不能据此断言作者最初选择它们的动机。

## 4. Bar 目录之外的导入方

下表为【事实】。导入列标明 import 行号；使用列给代表性符号及当前使用位置，不把同名配置字段误算成 Bar 导出符号。

| 文件 | Bar 直接导入 | 实际使用 |
|---|---|---|
| `Inkeys/IdtMain.cpp` | 根`:16` | `Initialization@:4092`、`WindowProc@:3821`、`SetThemeMode@:3322`、`RequestPresentationAlpha@:3081`、多个测试入口如 `RunUi3MultiTargetOffscreenBenchmark@:2038` |
| `Inkeys/IdtPlug-in.cpp` | 根`:27` | `CompleteEndShowRequest@:217`、`PublishPptStatusIndicator@:1294`、`barUISet.UpdateRendering@:1028`、业务回调注册`:1623` |
| `Inkeys/IdtState.cpp` | 根`:30` | `RefreshToolState@:1465`、`NoteRecallHistoryAvailability@:1154`、`SetWhiteboardActive@:4768` |
| `Inkeys/Inkeys/Other/Other.Config.cppm` | Metrics`:19` | 未定位到直接使用 Metrics 导出标识符；当前只确认 import 存在，不能把配置内 `BarButtonId` 当作 Metrics 类型 |
| `U/PageControl/PageControl.cpp` | 根`:56`、FramePacing`:59`、Animation`:60`、EraserAttributeMotion`:61` | `BarPageSurface@:128`、`BarUiValueClass@:224`、`DebugFrameSleepLatch@:296`、`SubmitSurfaceInput@:1378` |
| `U/PageControl/PageControl.cppm` | Metrics`:22` | `BarPptCompactLongSideDip@:280`、`BarButtonGapDip@:287`、`ResolveBarPptStandardPaginationGeometry@:385` |
| `U/Ppt/Ppt.cpp` | 根`:18` | `PublishPptSession@:470` |
| `RP/RenderPipeline.cpp` | FramePacing`:32` | `TryWaitForRemainingFrameTime@:754` |
| `U/SelectionOverlay/SelectionOverlay.cpp` | 根`:20` | `ShapeEditorSurface@:32`、`SurfaceInputEvent@:115`、`RegisterInteractionSurface@:294`、`RetryShapeUiIntents@:145` |
| `U/Setting/Setting.cpp` | 根`:63` | `barUISet@:1547`、`SetThemeMode@:3489`、`SetAnimationOptions@:3553`、`SetConfigZoom@:3583` |
| `U/StartupPreview/StartupPreview.cppm` | Metrics`:17` | `BarDarkSurfaceColorChannel@:24`、`BarMainBarFillOpacity@:27`、`BarMainBarFrameOpacity@:29` |
| `U/StartupPreview/StartupPreview.State.cppm` | Metrics`:8` | `BarDefaultMainBarLayoutWidthDip@:13`、`BarDefaultStartupPreviewTotalWidthDip@:15`、`BarMainBarHeightDip@:27` |
| `U/Whiteboard/Whiteboard.cpp` | 根`:39` | `BarPageSurface@:53`、`BarSurfaceBackgroundSpec@:200` |
| `W/Window.Legacy.cpp` | **无直接 Bar import**，`:1`—`:6` 为实际 import 列表 | 使用 StartupPreview 的 `GetBarStartupState@:105`、`IsBarStartupReady@:112`，不能据名字中的 Bar 判为直接依赖 |

全仓在册文本复查补充：

| 测试/研究源 | Bar 导入及代表符号证据【事实】 |
|---|---|
| `T/animation_tests.cpp` | Animation`:26`、Metrics`:27`；`BarUiValueClass@:122` |
| `T/bar_display_transition_tests.cpp` | Animation`:12`；`BarUiValueClass@:64` |
| `T/bar_logo_tests.cpp` | Animation`:19`；`BarUiValueClass@:151` |
| `T/bar_theme_tests.cpp` | Animation`:19`、Metrics`:20`；`BarDarkSurfaceColorChannel@:27` |
| `T/eraser_attribute_tests.cpp` | EraserAttributeMotion`:7`、Animation`:8`；`EraserAttributeLayoutInput@:35` |
| `T/frame_pacing_tests.cpp` | FramePacing`:12`；`FrameAnimationClock@:42` |
| `T/page_control_tests.cpp` | Metrics`:20`、SurfaceLayout`:21`、Animation`:23`；`BarUiApplyCurve@:31` |
| `T/recall_attribute_tests.cpp` | RecallAttributeMotion`:9`、RecallChrome`:10`、Animation`:11`、Metrics`:12`；`RecallAttributeLayoutInput@:26` |
| `T/toggle_click_coalescer_tests.cpp` | ToggleClickCoalescer`:8`；`BarToggleClickCoalescer@:28` |
| `T/wake_signal_tests.cpp` | WakeSignal`:10`；`WakeSignal@:28` |
| `T/whiteboard_ui_tests.cpp` | SurfaceLayout`:12`；`BarSurfaceHorizontalGroupSpec@:215` |
| `R/animation_target_probe.cpp` | Animation`:8`（import 指令），在册历史探针 |

【事实】Config、RenderPipeline、StartupPreview 没有形成当前接口 import 环：Config/StartupPreview 只指向无 import 的 Metrics；RenderPipeline 实现指向无 import 的 FramePacing。Bar 的反向依赖确实存在：`B/Bar.Button.cppm:15` 导入 Config，`B/Bar.Rendering.cppm:24` 导入 RenderPipeline，`B/Bar.Initialization.cpp:33`、`B/Bar.RenderLoop.cpp:52` 导入 StartupPreview。它们形成领域双向关系，但当前接口邻接图没有对应的循环。

【推测】`Other.Config.cppm` 的 Metrics import 可能是遗留依赖；本次没有找到使用证据，不能断言它必然可以删除。

## 5. 工程登记

### 5.1 项类型与原文样例

【事实】Bar 的 `.cppm` 与 `.cpp` 均登记为 `ItemGroup/ClCompile`，普通头为 `ItemGroup/ClInclude`。Bar 条目没有逐项 `CompileAs` 或配置条件；六套配置的 `ClCompile` 默认均启用模块依赖扫描，见 `P:195/235/275/312/354/393`（`ScanSourceForModuleDependencies`）。C++20、`/utf-8` 和 BMI 路径示例见 `P:187`—`:195`。

完整相关项目项原文：

```xml
<!-- P:415 -->
	<ClInclude Include="Inkeys\UI\Bar\Bar.ThemeMaterial.h" />

<!-- P:1079—1080 -->
    <ClCompile Include="Inkeys\UI\Bar\Bar.UI.cpp" />
    <ClCompile Include="Inkeys\UI\Bar\Bar.UI.cppm" />
```

```xml
<!-- PF:256—258 -->
    <Filter Include="Modules\Inkeys\UI\Bar">
      <UniqueIdentifier>{56be2dfa-0003-4904-8ed4-aac871d769c8}</UniqueIdentifier>
    </Filter>

<!-- PF:300—302 -->
    <ClInclude Include="Inkeys\UI\Bar\Bar.ThemeMaterial.h">
      <Filter>Modules\Inkeys\UI\Bar</Filter>
    </ClInclude>

<!-- PF:2282—2287 -->
    <ClCompile Include="Inkeys\UI\Bar\Bar.UI.cpp">
      <Filter>Modules\Inkeys\UI\Bar</Filter>
    </ClCompile>
    <ClCompile Include="Inkeys\UI\Bar\Bar.UI.cppm">
      <Filter>Modules\Inkeys\UI\Bar</Filter>
    </ClCompile>
```

【事实】工程其他模块存在显式 `<CompileAs>CompileAsCppModule</CompileAs>`，例如 `P:1098`；不能将此元数据误报为所有现有 Bar 项均有。

【推测】按现有 filters 格式与 D3，UI3 的登记事项是 filter 定义及其 UniqueIdentifier、各项目项的 Filter 映射；父级 `Modules\Inkeys\UI` 已存在于 `PF:253`。D3 对目标 filter 的要求见 `PRD:115`，此处不生成 GUID 或新增工程内容。

### 5.2 直接编译 UI/Log 的其他工程

【事实】当前在册 vcxproj 中，除主工程外发现 `HP` 直接编译这些源码；它在 `InkeysRepo.sln:12`（`Project`）中。

| `HP` 行号，符号均为 `ClCompile` | 源文件，均相对 `Inkeys/Inkeys/` |
|---|---|
| `97—104` | `UI/Bar/Bar.Animation.cpp/.cppm`、Metrics、SurfaceLayout、Scene、FramePacing、ToggleClickCoalescer、WakeSignal 的 `.cppm` |
| `105—106` | `UI/RenderPipeline/RenderPipeline.cppm/.cpp` |
| `118—121` | `UI/StartupPreview/StartupPreview.State.cppm/.cpp`、`StartupPreview.cppm/.cpp` |
| `122—126` | `UI/Ppt/Ppt.cppm`、`UI/Freeze/Freeze.cppm`、`UI/PageControl/PageControl.cppm`、`UI/Whiteboard/Whiteboard.cppm`、`UI/MessageBox/MessageBox.cppm` |
| `127—129` | `UI/MessageBox/MessageBox.cpp`、`.Layout.cpp`、`.Window.cpp` |
| `139` | `Log/Log.cpp` |
| `157—158` | `UI/Bar/Bar.PresentationProbe.cpp`、`Bar.Presentation.Source.cpp` |
| `163、171—175` | `UI/Setting/Setting.EraserDemoGpu.cpp`、`Setting.Controls.cpp`、`Setting.Shell.cpp`、`Setting.Pages.cpp` |
| `236—240` | `UI/Bar/Bar.EraserAttributeLayout.cppm`、EraserAttributeMotion、RecallAttributeLayout、RecallAttributeMotion、RecallChrome |

【事实】文件目录枚举和 Git 索引均未发现 `InkeysHeadlessTests.vcxproj.filters`；不存在的文件没有可引用行号。现存工程源登记证据是 `HP:97`（`ClCompile`），不能报告成“两份 filters 已齐备”。

登记检查清单仅列同步对象：

| 检查对象 | 现有落点与证据【事实】 |
|---|---|
| 主工程源码项目项 | `P:1035` 起 Bar `ClCompile`；`:414` 等 `ClInclude` |
| 主工程 filter 定义和文件映射 | `PF:256`、`:2282`（`Filter`、`ClCompile`） |
| 直接编译该文件的测试工程 | 上表 `HP` 各 `ClCompile`；测试扫描默认见 `HP:81` |
| 测试 filter 现状 | 当前缺文件；规范要求登记两者 filters，见 `SD/directory-structure.md:52`（主程序内部） |
| 头变模块后的消费者登记关系 | 本报告 2.2、4 中 include/import 清单，尤其普通 cpp 和测试源 |
| 现有主工程测试项条件 | 下述四项没有逐项条件，不能迁移时误附排除条件 |

【事实】四个 `Bar.*.Test.cpp` 均为无条件、自闭合 `ClCompile`：MultiTarget `P:1053`、GeometryAttribute `:1058`、EraserAttribute `:1061`、Presentation `:1070`，未带按配置的 `ExcludedFromBuild`。Harness 使用另一层条件：`P:84` 默认 Debug 开、`:85` 其余默认关，并以编译宏进入代码；例如 `B/Bar.Presentation.Source.cpp:3`（`INKEYS_ENABLE_UI3_PRESENTATION_HARNESS`）。

## 6. 编码与换行

【事实】对 26 个头、全部 Bar cpp/cppm、三份现有工程 XML及额外代表源进行了原始字节检查，并以严格 UTF-8 解码核验；抽查未发现孤立 LF/CR。编码属于字节属性，下列行号用于定位文件及符号，不表示 BOM 能从源码文字行判断。

| 范围 | 当前字节结果及定位 |
|---|---|
| 全部 26 个头 | **UTF-8 无 BOM + CRLF**；文件和符号见 2.1，不存在 BOM 头例外 |
| BOM 的 Bar cppm | Atomic、EraserAttributeLayout、EraserAttributeMotion、FramePacing、Layout、Main、Metrics、Rendering；均定位各文件`:1`（全局片段或模块声明） |
| BOM 的 Bar cpp | EraserAttribute、EraserAttribute.Test、GeometryAttribute、GeometryAttribute.Test、Initialization；均定位`:1`（全局模块片段） |
| 其余 Bar cpp/cppm | UTF-8 无 BOM + CRLF；例如 `B/Bar.RenderLoop.cpp:1`、`B/Bar.UI.cppm:1`（`module;`） |
| 特例 | `B/Bar.cpp:1`（`module Inkeys.UI.Bar;`）无 BOM，且文件末尾无换行 |
| 工程 XML | `P:1`、`PF:1`、`HP:1`（XML 声明）均无 BOM + CRLF |
| 额外源抽查 | `U/PageControl/PageControl.cpp:1`、`Inkeys/Inkeys/Other/Other.Config.cppm:1`（`module;`）均无 BOM + CRLF |

【事实】项目现有规范要求“保留原文件编码和换行”，见 `SD/cpp-conventions.md:48`；不能以 `SN/modules-and-code-style.md:77` 的代表性 BOM 描述覆盖本次实际文件格式。

## 7. 统一日志与 UI3 诊断

### 7.1 UI 下直接日志调用统计

以下为【事实】，统计的是源码调用表达式数，不是运行输出次数。`Enabled`、配置接口和 `EnterDiscardMode` 不算日志输出；条件级别调用计一处。

| 文件 | Module | 级别及数量 | 调用证据 |
|---|---|---|---|
| `B/Bar.Initialization.cpp` | Input | Warn×2 | `:239、243`，`Log::Warn` |
| `B/Bar.Interaction.cpp` | UI3 | Error×3 | `:1609、2104、8639`，`Log::Error` |
| 同上 | Bar | Error×1 | `:8927`，`Log::Error` |
| 同上 | Input | Info×1，TryFormat | `:9702` |
| `B/Bar.KeyboardShortcuts.h` | Input | Warn×1 | `:329`，`Log::Warn` |
| `B/Bar.Rendering.cpp` | Bar | Error×16、Warn×1 | Error `:554、1253、1328、1388、1435、1535、1565、1591、1630、1758、1780、1873、2363、2385、2472、2798`；Warn `:2159` |
| `B/Bar.RenderLoop.cpp` | Bar | Error×2 | TryFormat `:9361`、Error `:12625` |
| `U/PageControl/PageControl.cpp` | PageControl | Debug×2，TryWrite | `:205、2229` |
| `U/Ppt/Ppt.cpp` | Ppt | Info/Warn 条件选择×1，TryWrite | `:100—101`，`recovered` 决定级别 |
| `U/Setting/Setting.cpp` | Setting | Info×2、Error×2 | Info `:296、306`；TryFormat Error `:215`、Error `:5621` |

合计 **34 处**。另有：

- 【事实】`U/Whiteboard/Whiteboard.cpp:130` 设置 DiagnosticOutput 的 `Module::Whiteboard/Level::Debug` 目标，属于日志适配目标，不计直接 Log 输出调用。
- 【事实】`B/Bar.Presentation.Test.cpp:685` 调用 `EnterDiscardMode`，属于测试控制。
- 【事实】UI3 已存在三处 UI 内直接 Error，以及 UI 目录外诊断桥的 Warn：`Inkeys/IdtMain.cpp:3147`（`TryWrite`）。因此 `PRD:40` 的“尚无实际使用者”与现码不符。

### 7.2 include 写法与接口边界

【事实】本次 UI 源中没有直接 include `Log.h` 的样例，现有调用通过传统汇聚头获得声明：`Inkeys/IdtMain.h:83` 包含 `"Inkeys/Log/Log.h"`；例如 `B/Bar.Interaction.cpp:3` 包含 IdtMain.h。

仓库其他模块中的实际全局模块片段样例：

```cpp
// Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererSnapshot.cpp:1、12、14
module;
// 中间为其他系统/标准库 include
#include "../../Log/Log.h"

module Inkeys.Drawing.Draw3.renderer;
```

| 接口 | 当前边界【事实】 |
|---|---|
| `Write` 与 Debug/Info/Warn/Error 包装 | 阈值过滤，`tryOnly=false`；可能等待内存锁。证据 `Inkeys/Inkeys/Log/Log.h:74—105` |
| `TryFormat` | 阈值过滤，`tryOnly=true`，热路径非阻塞投递。证据同文件`:120—125` |
| `Trace` | 通道开关控制、绕过普通阈值、非阻塞投递。证据同文件`:128—133` |
| 规范热路径限制 | `SD/errors-logging-and-resources.md:60`，“统一日志服务合同 / 3. Contracts”：热路径“只能用 `TryWrite`/`TryFormat`/`Trace`/`DiagnosticOutput`”；反例见`:82` |
| UI3 采样限制 | `SD/ui3-render-diagnostics.md:38`，“3. Contracts”：正常热路径不做日志格式化、文件写入或逐 packet 日志 |

【事实】当前仍存在热路径附近的阻塞式日志写法：`B/Bar.Rendering.cpp:529`（`HandleFrameEndDrawResult`）在 `:554` 调用 Error；`B/Bar.RenderLoop.cpp:12625` 在提交失败分支调用 Error；`B/Bar.Interaction.cpp:1609` 在输入回调异常分支调用 Error。这是现有调用与上述合同的差异；【待测】实际锁竞争、丢帧及异常触发频率未经运行确认。

### 7.3 现有 UI3 诊断机制

| 项目 | 位置及结论【事实】 |
|---|---|
| 数值聚合 | `RP/RenderPipeline.Diagnostics.h:101`（`DiagnosticsAccumulator`）；长帧 50ms、间隔 1s、健康汇总关闭见`:10—12` |
| 限频与拒绝保留 | 同文件`:267` `Ready`、`:279` `BeginAttempt`、`:286` `CompleteAttempt`；拒绝不清聚合 |
| TLS 与采样 | `RP/RenderPipeline.cpp:40`（`currentDiagnostics`）、`:42`（`CallbackDiagnosticsScope`）；`:847—855` 创建样本和设置 detailedCaptureEnabled |
| 格式化/投递 | 同文件`:106` `FormatDiagnostics`、`:121` `[UI3Diag]`、`:560` `EmitDiagnostics`；sink 在锁外执行见`:572` |
| 对外接口 | `RP/RenderPipeline.cppm:155` `StampBarCommit`、`:159` `FrameStageTimer`、`:174` `CurrentFrameDiagnostics`、`:176` `DiagnosticsSink`、`:321/357` 两层 `SetDiagnosticsSink` |
| 产品日志桥 | `Inkeys/IdtMain.cpp:3143—3147`；Warn/UI3、TryWrite；阈值过滤返回 true |
| 最终文件出口 | `Inkeys/Inkeys/Log/Log.cpp:663`（`Inkeys\\Logs`）、`:683`（`basic_file_sink_st`）；不是旧 UI3 专用 async logger |

调用数量口径：

| 接口 | 调用点数量与证据【事实】 |
|---|---|
| `CurrentFrameDiagnostics()` | UI 下 20 处：Rendering 6（`:1729` 等）、RenderLoop 4（`:1182` 等）、Scene 5（`:670` 等）、PageControl 4（`:1475` 等）、EraserAttribute.Test 1（`:2723`）；不计声明和定义 |
| 同接口 Headless 测试 | `T/render_scheduler_tests.cpp` 19 处，始于`:988`，末于`:1895` |
| `FrameStageTimer` 实例 | UI 下 23 处：RenderLoop 15（`:7518` 等）、Scene 3（`:670、872、891`）、PageControl 5（`:1579` 等）；不计类型别名 |
| `SetDiagnosticsSink` 调用 | 产品 1（`IdtMain.cpp:3143`）；主工程测试 1（`Bar.EraserAttribute.Test.cpp:2708`）；Headless 6（`render_scheduler_tests.cpp:1009、1057、1128、1157、1457、1842`） |

## 8. 规范落点与过期表述

“适合补充的位置”为【推测】，仅指出落点，不提供实现或修改规范。

| 文件与小节 | 当前记录【事实】 | 适合补充的内容【推测】 |
|---|---|---|
| `SD/directory-structure.md:46`，“Inkeys 主程序内部” | `:52` 要求新增功能 cppm+cpp；`:53` 描述 Bar；`:20—22` 规定 filters | D3 的 UI3 四层职责、目录与 filter 归属 |
| 同文件`:64`，“C++20 module 的已观察模式” | `:70` 记录 Bar 及分区，但没有聚合示例 | 分区与根模块聚合的使用说明 |
| `SD/cpp-conventions.md:37`，“module 与传统头文件边界” | `:39` 仅描述接口 cppm、按需 cpp | D4 头判定标准及全局片段 include |
| 同文件`:18`，“模块导出与独立纯接口” | `:20` 记录导出前置声明身份 | 分区前置声明与聚合可见性边界 |
| `SD/build-and-compatibility.md:161`，“ARM64 主机构建命令” | `:165` 要求模块同步工程/filters | 工程登记检查的交叉引用 |
| `SD/rendering-and-ui.md:39`，“Inkeys.UI.Bar” | `:41`、`:47—49` 将公共按钮/renderer 能力记在 Bar 下 | 迁移后产品 Bar 与公共 UI3 的责任描述 |
| `SD/index.md:7`，“文档索引” | `:9、11、12、20、27` 已链接相关规范 | D6 新合同的可发现入口 |
| `SN/modules-and-code-style.md:3`，“C++20 Module Shape” | `:5—17` 明确接口+一个或多个实现；`:19—33` 有全局片段 include 样例 | 可交叉引用；其主适用范围仍是 draw3 |
| `SN/index.md:3、5、11` | 区分 demo/native 与主产品边界 | 不宜把 demo 规则未经说明直接当 UI3 全仓合同 |

需要校正或迁移后复核的现有文字：

| 位置 | 差异【事实】 |
|---|---|
| `SD/directory-structure.md:53`，“主程序内部” | 仍写 Bar“是否为发布默认待确认”；`SD/index.md:5` 已写 UI3 为唯一悬浮栏入口 |
| `SD/cpp-conventions.md:35`，“当前命名观察” | “是否要求所有未来代码迁入 module…待确认”与`:39`“需要分离实现时”不够表达本任务 D4/D6 已定规则；D4/D6 见 `PRD:116、122` |
| `SD/rendering-and-ui.md:41、47—49`，“Inkeys.UI.Bar” | 是当前实现描述；通用能力迁出后文件归属和能力归属将过期，当前不能提前当作已迁移 |
| `SD/ui3-render-diagnostics.md:45`，“Contracts” | 仍写专用 async logger、discard_new、IDTLogger；当前桥为 `IdtMain.cpp:3147` 的统一日志 TryWrite |
| `SN/modules-and-code-style.md:77`，“Encoding And Line Endings” | BOM 是该范围代表性观察，不能推广为本次所有 UI 文件的实际编码 |
| `PRD:37、40`，“已确认事实” | “约20个头”“UI3尚无使用者”分别与本次23个Bar头、现有UI3调用不符 |

## 9. 迁移/封装前必须先解决的事项

以下为已经存在、需要纳入迁移边界的【事实】清单，不展开实现方案。

| 事项 | 当前证据 |
|---|---|
| 头转换同时涉及普通 cpp、模块全局片段、其他头及测试源 | 2.2 全部 include 清单；典型 `Inkeys/IdtMain.cpp:50`（InteractionWorker） |
| KeyboardShortcuts 有匿名命名空间和 import 后可见性要求 | `B/Bar.KeyboardShortcuts.h:3—9`、`:27`（`BarShortcutBackend`） |
| PresentationProbe/Source 仍有普通 cpp 实现，同时被两个工程编译 | `B/Bar.PresentationProbe.cpp:1`、`B/Bar.Presentation.Source.cpp:1`；`P:1068—1069`、`HP:157—158` |
| Main/Rendering/Scene 的前置声明和导出身份是现有无环条件 | `B/Bar.Rendering.cppm:38`、`B/Bar.Scene.cppm:21`、`SD/cpp-conventions.md:20` |
| 公共候选中混有主栏政策和 Draw3 类型 | `B/Bar.BottomDock.h:52`（工作区场景）、`B/Bar.Haptics.h:9`（Click）、`B/Bar.ShapeGesture.h:15`（Vec2） |
| Headless 的直接源码登记必须一并核对，且其 filters 当前缺失 | `HP:97—106、236—240`（ClCompile）；规范要求见 `SD/directory-structure.md:52` |
| 格式须按文件保存，不能统一补 BOM | 编码表；`SD/cpp-conventions.md:48`（保留原编码） |
| 日志现状与热路径合同存在差异，诊断规范出口描述已过期 | `B/Bar.RenderLoop.cpp:12625`（Error）、`SD/errors-logging-and-resources.md:60`、`SD/ui3-render-diagnostics.md:45` |
| 两个 Startup 头的内容不是纯枚举，但 D4 强制范围未明确包含它们 | `U/StartupPlacement.h:85`、`U/StartupPreview.Effect.h:13`；`PRD:116`（D4） |

## 10. 未能确认的事项

- 【待测】头转换后的 MSVC 模块可见性、ODR、BMI 扫描和全部传统/测试消费者能否编译；本次按要求没有构建。
- 【待测】现有阻塞式 Error/Warn 在渲染、输入异常路径中的实际竞争及性能影响；源码调用证据见日志表，未运行不能判断影响程度。
- 【待测】统一日志实际文件创建、阈值过滤、掉样与退出排空结果；当前只确认出口实现，见 `Inkeys/Inkeys/Log/Log.cpp:663、683`。
- 【推测】Config 的 Metrics import 是遗留项；`Other.Config.cppm:19` 确有导入，但本次未找到直接使用，不能据此认定可删除。
- 【推测】部分宿主算法、触觉与交互工作器可整体归入通用 UI3；目前只确认职责和依赖，不能由文件名决定最终公共边界。
- 【待测】所有头的独立自包含编译能力；除 KeyboardShortcuts 的明确顺序合同外，本次没有进行逐头编译探针。
- 【待测】迁移后的视觉、输入、设备丢失、Win7 兼容性与性能保持；静态依赖图和现有测试登记均不能替代运行证据。