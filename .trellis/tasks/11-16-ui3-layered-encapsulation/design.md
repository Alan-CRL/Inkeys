# UI3 分层封装与通用 UI3 模块迁移 — 技术设计

> 需求与决策见 `prd.md`（D1–D9）。事实依据见 `research/`：
> `layer1-primitives-rendering.md`（下称 R1）、`layer2-3-controls-panels.md`（R2）、`layer4-host-presentation.md`（R4）、`headers-modules-build.md`（RH）。
> 代码基准：分支 `refactoring/drawpad-ui` @ `7047db94`。行号以调研报告为准，实施时按符号重新定位。
> 本文定义边界、契约和取舍；具体类成员可在实施中细化，但不得越出这里的边界。

## 1. 总体结构

四层是责任划分，不是继承层级：

| 层 | 责任 | 位置 |
| --- | --- | --- |
| L1 基础绘制与动效 | 图元、动画值、文字格式、材质、主题状态、内容绘制器、脏区包络 | `UI/UI3/` |
| L2 基础控件 | 按钮（视觉/状态/布局/命中）、指针与手势、进度反馈、扩展内容合同 | `UI/UI3/` |
| L3 序列与面板 | 按钮列表与排布、面板（外框/开合/姿态/候选与已呈现/留白阻挡）、叠层、共享光照、颜色选择器 | `UI/UI3/` |
| L4 宿主与呈现 | 帧目标、帧事务、提交端、输入接入、坐标映射 | `UI/UI3/` |
| 产品层 | 主栏、More、绘制属性、翻页场景、形状编辑、状态指示、底栏吸附等的内容组织与业务动作 | `UI/Bar/`、`UI/PageControl/`、`UI/SelectionOverlay/` |

依赖方向只允许：产品层 → L4 → L3 → L2 → L1 → 基础设施（`Inkeys.UI.RenderPipeline`、`Inkeys.Log` 等）。`UI/UI3/` 内任何文件不得 `import Inkeys.UI.Bar*`，不得 `#include "IdtMain.h"` / `"IdtState.h"`，不得读取 `barUISet`、`floating_window`、`currentBarThemeStyle`。

## 2. 模块与文件组织

### 2.1 两种模块形态及选用规则

调研确认 `InkeysHeadlessTests` 直接编译一部分 UI 源并 `#include` 多个 Bar 头（RH §2.2、§5.2），但不编译完整主栏。由此得到本任务、也是今后的规则（写入 spec，见 §10）：

1. **独立模块**（`Inkeys.UI.UI3.<Name>` / `Inkeys.UI.Bar.<Name>`）：不依赖 D2D 设备对象的运行时状态、不依赖 `IdtMain.h`、可被测试工程或其他领域单独导入的叶子能力。被 `InkeysHeadlessTests` 消费的东西**必须**是独立模块。
2. **分区**（`Inkeys.UI.UI3:<Name>`）：彼此需要对方完整类型、只作为整体使用的重型运行时（图元、绘制器、按钮、面板、宿主）。分区用于解决相互引用：接口侧单向依赖，反向依赖放进实现单元。
3. 根模块 `Inkeys.UI.UI3`（`UI3.cppm`）`export import` 全部分区和全部独立子模块，作为产品层的唯一常规入口。
4. 不得在一个模块里前置声明另一个模块拥有的类型。需要对方类型就 `import` 对方。

### 2.2 `UI/UI3/` 模块表

文件名 `UI3.<Name>.cppm` + `UI3.<Name>.cpp`（无实现体的纯 constexpr/模板单元可以只有 `.cppm`）。命名空间 `Inkeys::UI::UI3`。

独立模块：

| 模块 | 来源 | 内容 |
| --- | --- | --- |
| `Inkeys.UI.UI3.Animation` | `Bar.Animation.cppm/.cpp`（去掉 15 个产品 helper，R1 §4.2）、`Bar.FontSize.h` | 动画值/颜色/透明度/状态、曲线、时间线、事务、推进上下文、字号修正 |
| `Inkeys.UI.UI3.Metrics` | `Bar.Metrics.cppm` 的「通用控件视觉」部分（R1 §8） | 网格间距/边长、圆角、描边、图标文字槽、按压/hover/禁用/光效/时长常量、按钮子布局解析 |
| `Inkeys.UI.UI3.Material` | `Bar.ThemeMaterial.h` | 颜色与透明度数学、颜色角色、材质解析、对象边缘光、笔色显示校正 |
| `Inkeys.UI.UI3.DirtyRegion` | `Bar.DirtyRegion.h` | 脏区跟踪、调试包络 |
| `Inkeys.UI.UI3.Superellipse` | `UI/Superellipse.Geometry.h` | 超椭圆路径生成 |
| `Inkeys.UI.UI3.LoadingIndicator` | `Bar.PageLoadingIndicator.h` | 等待/失败动效状态 |
| `Inkeys.UI.UI3.FrameRing` | `Bar.ButtonFrameRing.h` | 沿圆角外框的进度环绘制与缓存 |
| `Inkeys.UI.UI3.Interaction` | `Bar.InteractionWorker.h` 的纯状态部分 | `InteractionTask`、`InteractionMailbox`、`ButtonPointerState`、手势识别（§5.3） |
| `Inkeys.UI.UI3.WakeSignal` | `Bar.WakeSignal.cppm` | 唤醒信号 |
| `Inkeys.UI.UI3.FramePacing` | `Bar.FramePacing.cppm` | 帧节拍与动画时钟（`RenderPipeline.cpp` 改为导入它，不再导入 Bar） |
| `Inkeys.UI.UI3.PresentationProbe` | `Bar.PresentationProbe.h/.cpp` | 有限场景观察协议（`Ui3Finite*`、`Ui3Svg*`）。图元与绘制器依赖它，所以必须在 UI3 |

分区（`Inkeys.UI.UI3:<Name>`）：

| 分区 | 层 | 来源 | 内容 |
| --- | --- | --- | --- |
| `:Primitive` | L1 | `Bar.UI.cppm/.cpp` | 继承坐标、Shape、Superellipse、SVG、PNG、Word |
| `:Format` | L1 | `Bar.Format.cppm` | DWrite 格式缓存 |
| `:Theme` | L1 | `Bar.Theme.cppm` 的通用部分 | 主题模式、颜色角色、主题状态快照与发布（§4.2） |
| `:Envelope` | L1 | `Bar.RenderingAttribute.cppm` | 图元可视包络 |
| `:Renderer` | L1 | `Bar.Rendering.cppm/.cpp` 的内容绘制与设备缓存部分 | §4.3 |
| `:Button` | L2 | `Bar.Button.cppm/.cpp`、`Bar.Main.cppm` 中的公共按钮入口 | §5 |
| `:Progress` | L2 | 各处的圆环/长按进度状态 | §5.4 |
| `:List` | L3 | `BarButtonListClass`、`ResolveBarButtonListLayout` 的通用部分 | §6.1 |
| `:Panel` | L3 | 各面板的共同部分 | §6.2 |
| `:Lighting` | L3 | `PrepareFrameLighting` 的状态机部分、`PublishSharedLighting` 与订阅 | §6.3 |
| `:ColorPicker` | L3 | 主栏与样式栏两套连续取色器 | §6.4 |
| `:Host` | L4 | 帧目标、帧事务、提交端、坐标映射 | §7.1–7.3 |
| `:HostInput` | L4 | `Bar.InteractionWorker.h` 的表面注册/事件部分与 `Bar.Interaction.cpp` 中的共享输入 worker | §7.4 |

`Inkeys.UI.Bar.Metrics` 保留主栏、翻页、白板操作行、启动预览专用常量，并 `export import Inkeys.UI.UI3.Metrics`，使 `PageControl.cppm`、`StartupPreview*.cppm`、`Other.Config.cppm` 现有导入继续可用。

> **实施调整（2026-11-17）**：
> - `:Progress` 落实为独立模块 `Inkeys.UI.UI3.Progress`（纯状态的长按进度单元，按 §2.1 规则 1 需要能被测试工程单独导入）。圆环与沿框环的绘制仍在 `:Renderer` 与 `UI3.FrameRing`。
> - 手势识别单元与 `ButtonPointerState` 同在独立模块 `Inkeys.UI.UI3.Interaction`，配 Headless 单元测试。
> - 光照状态机在 S3 只做了「产品输入解析 vs 状态机」的拆分，仍位于 `:Renderer` 的绘制器类内；S5 与发布订阅一起拆为 `:Lighting`。

### 2.3 主栏专有头的模块化（D4）

调研确认 26 个头全部含函数或类，没有纯引用头（RH §2.1）。以下留在 `UI/Bar/`，各自成为独立模块（因为多数被 `InkeysHeadlessTests` 单独包含）：

| 现头文件 | 目标模块 | 备注 |
| --- | --- | --- |
| `Bar.A2.h` | `Inkeys.UI.Bar.A2` | |
| `Bar.AutoCollapse.h`、`Bar.AutoCollapseInteraction.h` | `Inkeys.UI.Bar.AutoCollapse` | 合为一个模块，两个测试源都改为导入它 |
| `Bar.BottomDock.h` | `Inkeys.UI.Bar.BottomDock` | 1868 行，24 处引用；`NOMINMAX` 留在全局模块片段 |
| `Bar.DisplayTransition.h` | `Inkeys.UI.Bar.DisplayTransition` | |
| `Bar.Haptics.h` | `Inkeys.UI.Bar.Haptics` | 耦合 Draw3 触觉类型，不进 UI3；`inline thread_local` 状态改为实现单元内变量加访问函数 |
| `Bar.I18nFormat.h` | `Inkeys.UI.Bar.I18nFormat` | |
| `Bar.KeyboardShortcuts.h` | 分区 `Inkeys.UI.Bar:KeyboardShortcuts` | 它直接访问 `barUISet` 且只被 `Bar.Interaction.cpp` 使用；匿名命名空间内容改为分区内非导出符号 |
| `Bar.LogoAppearance.h` | `Inkeys.UI.Bar.LogoAppearance` | 导入 `Inkeys.UI.UI3.Material` |
| `Bar.MainLayoutPolicy.h` | `Inkeys.UI.Bar.MainLayoutPolicy` | |
| `Bar.Presentation.Source.h/.cpp` | `Inkeys.UI.Bar.PresentationSource` | 现 `.cpp` 改为该模块的实现单元 |
| `Bar.PresentationAlpha.h` | `Inkeys.UI.Bar.PresentationAlpha` | |
| `Bar.PresentDecision.h` | `Inkeys.UI.Bar.PresentDecision` | 含主栏映射语义，本任务不进 UI3 |
| `Bar.ShapeGesture.h` | `Inkeys.UI.Bar.ShapeGesture` | 全局片段包含 `Draw3.ShapeInteraction.h` |
| `Bar.StatusIndicator.h` | `Inkeys.UI.Bar.StatusIndicatorModel` | |
| `Bar.WindowGeometry.h` | `Inkeys.UI.Bar.WindowGeometry` | |
| `UI/StartupPlacement.h` | `Inkeys.UI.StartupPlacement`（`UI/StartupPlacement.cppm`） | 它包含 `Bar.BottomDock.h` 并被三个 `.cppm` 的全局片段包含，BottomDock 模块化后必须随之转换 |

不转换：`UI/StartupPreview.Effect.h`（19 行，无依赖，不在 D4 范围）。`UI/StartupPreview/*.h` 中包含了被转换头的（`StartupPreview.Drawing.h`、`StartupPreview.Effects.Policy.h`）改为「由包含它的实现单元先 import 再 include」，这两个头本身不转换。

转换通则：

- 头里的预处理效果（`NOMINMAX`、`min`/`max` 保存恢复）不能导出，各消费者自行处理；UI3 与新模块内部统一写 `(std::min)` / `(std::max)`。
- 原 `#include` 这些头的全部位置（RH §2.2 的完整清单，含 `IdtMain.cpp`、`IdtState.cpp`、`Setting.Pages.cpp`、约 20 个测试源）改为 `import`。
- `.trellis/tasks/09-22-*/research/` 下的历史探针源不在工程内，不修改。

### 2.4 工程登记

每新增、移动、删除一个源文件，同步修改（RH §5）：

1. `Inkeys/Inkeys.vcxproj`：`ClCompile`（`.cppm` 与 `.cpp`）/ `ClInclude`，不加逐项条件。
2. `Inkeys/Inkeys.vcxproj.filters`：新增 filter `Modules\Inkeys\UI\UI3`（带新的 `UniqueIdentifier`），每项映射到与磁盘目录一致的 filter。
3. `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj`：凡测试源导入的模块，其 `.cppm`（以及有实现体时的 `.cpp`）都要登记；现有 `..\Inkeys\Inkeys\UI\Bar\...` 条目随移动改路径。该工程当前没有 `.filters` 文件，本任务不新建。

移动文件使用 `git mv` 保留历史。`InkeysRepo.sln` 头部版本行不得被改写。

### 2.5 命名与迁移垫片

- 进入 UI3 的符号去掉 `Bar` 前缀，其余不变：`BarUiShapeClass` → `UI3::UiShapeClass`，`BarUIRendering` → `UI3::UIRendering`，`BarButtonVisualParts` → `UI3::ButtonVisualParts`，`BarDirtyRegionTracker` → `UI3::DirtyRegionTracker`，`BarButtonGapDip` → `UI3::ButtonGapDip`。去前缀后与标准库或 Windows 宏同名、或在 UI3 内重名的，在改名步骤开始前列出并逐个定名。
- 为了让每一步都能编译，移动时先**不改名**：符号进入 `Inkeys::UI::UI3`，同时在 `Inkeys::UI::Bar` 中以 `using` 声明保留旧名（垫片）。全部迁移完成后单独做一次纯机械改名并删除垫片（实施计划 S7）。垫片不得留到任务结束。
- 留在 Bar 的产品类型不改名。

### 2.6 文件格式（D9）

本任务新建或修改的 `.cpp` / `.cppm` / `.h` 一律 UTF-8 **带 BOM**、CRLF；原本无 BOM 的文件在被修改时补上 BOM。未被修改的文件不为统一格式而改动。`.vcxproj`、`.filters`、`.sln` 与 `.trellis/` 下的 Markdown 保持各自现有格式。

## 3. 需要先解开的耦合（事实来源 R1 §10、R2 §8、R4 §9）

| 耦合 | 处理 |
| --- | --- |
| 绘制器的 owner 构造器认识 `BarUISetClass`、preset、主栏对象表 | 删除 owner 构造器；主栏在自己的初始化里组装服务对象后用服务注入构造 |
| 绘制器把 `BarUISetClass`、`BarRenderLoopCoordinator`、`BarStatusIndicatorPanel` 声明为友元，RenderLoop 调用其受保护的粗细预览/FineDial 方法 | 这些方法是产品绘制，移到 Bar 侧（§4.4）；友元声明全部删除 |
| 默认 `GetThemeColor` 读 `currentBarThemeStyle` 并直接唤醒 `Client::SelectionOverlay`（290 处调用） | UI3 自有主题状态与变更订阅（§4.2） |
| Shape/Superellipse 绘制时查询全局常驻背景透明度 | 常驻透明度成为主题状态的一部分，绘制器从主题快照读取 |
| SVG 图元带 `mainLogoAppearance` 与主 Logo 节点加工 | 改为通用的「SVG 文档预处理钩子 + 外观键」（§4.1） |
| `PrepareFrameLighting` 按工具模式选锚点、读主栏光效全局 | 拆为产品侧的光照输入解析与 UI3 的光照状态机（§6.3） |
| `Bar.Animation` 含 15 个笔型/粗细/Laser helper | 移到 `Inkeys.UI.Bar.PenPreviewMotion`（独立模块，`animation_tests.cpp` 改导入） |
| `Bar.Layout.cppm` 的颜色数学与工具业务快照混在一起 | HSV/色板映射进入 `:ColorPicker`；笔宽与模式换算留在 Bar |
| 固定按钮样式读 `barUISet.barStyle.darkStyle`；Geometry 标签测量读 `barUISet.spec` | 前者读主题快照；后者由调用方传入绘制器引用 |
| `BarButtonClass` 混有产品字段；`BarButtonSetClass` 是主栏注册表 | §5.1 |
| 指针进入/离开通知 `PostMessage` 到 `floating_window`；光照发布端直接唤醒 SelectionOverlay | §6.3 |
| 输入表面注册按 HWND 替换 | §7.4 |

## 4. L1 设计

### 4.1 图元（`:Primitive`）

- 类型与行为原样迁入。`themeSurface`、`residentSurface`、`PenWhenDrawing` 等标志保留，它们表达的是「该表面使用哪类材质/边缘光」，取值由调用方设置；标志对应的数值策略来自主题快照与 `Material`，图元和绘制器不再访问主栏。
- SVG：`mainLogoAppearance` 替换为两个通用成员——文档预处理回调（在栅格化前修改 lunasvg 文档，返回是否成功）与外观键（参与缓存失效比较）。主 Logo 的节点规则留在 `Inkeys.UI.Bar.LogoAppearance`，由主栏初始化时挂到主 Logo 图元上。行为（缺节点时拒绝缓存成功）不变。
- SVG/PNG 位图缓存仍在图元对象上，但增加「所属设备代次」记录：上传时写入，绘制时与绘制器当前代次不一致即重新上传。保留现有外部重置链（它仍负责及时释放），新增的代次校验是防止跨目标误用的兜底。

### 4.2 主题（`:Theme`）

- UI3 持有一份进程内的主题状态快照（深浅、背景透明度、内容透明度、常驻填充相关值、禁用透明度等，即今天 `BarStyleClass` 中被主题函数读取的那几项），带单调递增的修订号。
- 产品层（主栏帧首应用样式处）调用发布接口写入；UI3 的 `GetThemeColor(role, alpha)` 等查询读取该快照。显式模式重载保持不变。
- 变更订阅：提供注册回调的接口，状态修订变化时在发布方线程调用。主栏在初始化时注册一个回调去请求 `Client::SelectionOverlay` 重绘，替代今天写死在 `GetThemeColor` 里的副作用。查询函数本身不再有副作用。
- 290 处默认重载调用点不改调用形式，只是解析到 UI3 的函数。
- 11 色画笔预设（`BarThemePresetColorEnum`、`GetPresetColor`）是产品数据，留在 `Inkeys.UI.Bar:Theme`。

> **实施调整（S3a，2026-11-17）**：上面第一、二条「产品发布快照」的做法在实施时改为「产品绑定、UI3 实时读取」。原因：多个离屏测试直接改样式对象的已应用字段后只调用 `SetThemeStyleSource`，没有发布动作；改成快照会改变这些测试与 SelectionOverlay 的唤醒时机，违反 R7。落实方式：UI3 定义主题状态结构（深浅、背景透明度、内容透明度三个原子字段），`BarStyleClass` 继承它并绑定；变更检测保留在默认 `GetThemeColor` 的查询路径（原边沿闩锁不变），命中时调用订阅回调。主栏以静态注册器注册「请求 SelectionOverlay 重绘」的回调。边界不变：UI3 自有主题状态类型与查询，不认识主栏，不点名渲染客户端。

### 4.3 内容绘制器（`:Renderer`）与目标分离

今天的 `BarUIRendering` 同时拥有目标资源/帧生命周期、设备相关缓存、内容绘制（R1 §5.2）。拆成两个对象：

| 对象 | 归属 | 拥有 |
| --- | --- | --- |
| 内容绘制器（`:Renderer`） | L1 | 画刷/遮罩/几何/渐变缓存、文字格式服务、标签拟合缓存、内容绘制函数（Shape、Superellipse、Svg、Png、Word、Ring、ProgressRing、点光、阴影、高光、色板渐变、SliderThumb）、当前帧光照快照、当前缩放与字体目标 |
| 帧目标（`:Host`，§7.1） | L4 | 设备上下文、目标位图、GDI interop、Ensure/Recreate/Discard、Begin/Finish/End、脏区裁剪、透明清空 |

契约：

- 绘制函数继续以 `ID2D1DeviceContext*` 为参数，不关心它来自谁。
- 绘制器记录它的缓存所属的设备代次。帧目标在设备代次变化或目标重建时调用绘制器的「设备已变化」入口，绘制器清空设备相关缓存并触发 `resetDeviceCaches` 服务。
- 服务对象保留三个回调的形态（typography、resetDeviceCaches、光照输入改由 §6.3 提供）。
- `HandleFrameEndDrawResult` 中与精确遮罩相关的失败处理留在绘制器，由帧目标在 `EndDraw` 后回调。
- 呈现观察协议（clear、clip、bounds cull 的观察钩子）随各自所在对象迁移，行为不变。

### 4.4 留在主栏的绘制

粗细预览路径/渐变、FineDial selector/数字布局及其缓存（R1 §5.2「同类中的跨层内容」）移到 Bar 侧新分区 `Inkeys.UI.Bar:PenPreviewRendering`，自持缓存，通过绘制器的公开接口取画刷和几何工厂，并挂接到 `resetDeviceCaches`。

## 5. L2 设计

### 5.1 按钮模型拆分

- `UI3::ButtonClass`：`size`、`lastDrawX/Y`、背景、`hoverStage`、`pressScale`、名称文字、SVG/PNG 图标及种类、可选角标、本地/共享状态、`clickFunc`（R2 §1.2「通用」两行）。
- `Bar::BarButtonClass`：公开继承 `UI3::ButtonClass`，增加 `baseSize`、`baseUserVisible`、`preset`、`id`、`userVisible`、`hide`、`only`、`compactMultilineLabel`、`closeMoreAfterAction`，以及 `IsVisible()`、`LabelFontSize()` 这两个产品判断。
- UI3 的入口函数（Retarget / Configure / Advance / Draw / HitTest）参数类型为 `UI3::ButtonClass&`。需要可见性、字号这类产品判断的地方改为由调用方以参数传入（Retarget 已经接收 visible/enabled/selected；命中快照生成增加显式 visible/enabled 的重载）。不引入虚函数。
- `BarButtonSetClass`、`BarButtonRegistrationClass` 留在 Bar。
- `ButtonVisualParts` 适配器保留，供仍持有独立图元字段的调用方使用。

### 5.2 尺寸与外观是参数，行为是统一的

按钮的边长、图标尺寸、圆角、命中尺寸、文字槽通过配置传入（`UI3.Metrics` 提供标准档位，调用方可覆盖）。按压倍率与曲线、hover 阶段、选中光照、禁用表现由 UI3 统一实现，调用方不得自行计算按压矩阵。这是 D7 的落实方式：统一的是行为，尺寸差异作为参数保留，因此绝大多数现有控件外观尺寸不变。

### 5.3 指针与手势（`Inkeys.UI.UI3.Interaction`）

- `ButtonPointerState` 原样迁入。
- 新增按钮激活方式配置：抬起激活（默认）、按下激活并支持拖过相邻项连续选择（预设色块今天的语义）。
- 新增手势识别单元，把今天分散实现的判定收敛为一份：点击、次键点击、双击、长按（起始延时、完成时长、移动阈值由调用方配置）。它只产出手势事件，不执行业务动作。Main/More 的协程、图形长按锁定、取色器长按锁色改为消费它；业务分支（清空双击、主按钮右键关闭、PPT 焦点回交等）留在产品层。
- 原生捕获、接触来源、窗口代次属于 `:HostInput`。

### 5.4 进度反馈（`:Progress`、`UI3.FrameRing`、`UI3.LoadingIndicator`）

- 圆环与沿框进度环的绘制已是公共的，保持。
- 新增长按进度状态单元：持有起始时间、延时、完成时长、淡入淡出时长、是否已完成；提供「下一次需要唤醒的时刻」。图形长按锁定（200 ms 出环、1000 ms 提交）与取色器长按（500 ms 起、再 1500 ms 锁定）使用同一单元、各自的参数。资源失败后本次不重试、设备代次变化后重置的现有语义保留。
- 现有时长数值全部保持。`Bar.Main.cppm` 中 `ConsumeHoldWake` 声明处的注释（写 0.5 s / 2.0 s）与实现不符，随代码迁移改正注释。

### 5.5 扩展内容合同

`ButtonDrawOptions` 的 `underlay` / `content` 回调与 `BarSurfaceBackgroundSpec::drawContent` 保留。新增可选的「内容范围」声明：扩展内容的可视包络（相对按钮）与是否处于动画活跃状态。提供了声明时，公共绘制恢复包络提前拒绘，并把包络并入脏区；未提供时维持今天「有回调就不提前拒绘」的保守行为。命中仍以按钮标准几何为准，扩展内容不改变命中。

## 6. L3 设计

### 6.1 列表与排布（`:List`）

- `ButtonList`（强引用快照容器）原样迁入。
- 排布引擎输入为「项尺寸、是否可见、是否分隔项」的序列加排布规格（单行、定列数网格、分组网格），输出位置/尺寸/行列/分隔信息。今天按 preset 判断分隔项、GeometryGrid 的六列等产品规则，由调用方在构造输入和规格时给出。`Main` 的特殊分隔与 More 的分组/溢出规则留在 Bar，调用通用引擎。

### 6.2 面板（`:Panel`）与叠层

面板描述一块 UI 内容的共同部分，不是递归控件树：

- 外框图元与材质角色；
- 开合动效（几何进度与透明度，时长与曲线可配）；
- 局部姿态（平移、缩放、字体目标、光照局部位置的同步设置，即今天翻页场景 `SetPose` 做的事）；
- 裁剪范围；
- 可视包络（含边框、柔光、阴影外扩）与可交互范围；
- 候选帧与已呈现帧的分离：`CommitPresented`，以及带序号的「捕获候选 / 按序号确认」（今天 `BarPageSurface` 的异步路径），失败帧不推进；
- 留白阻挡：面板可交互范围内未命中任何成员的点，由面板消费。

叠层（`PanelStack`）：按 z 序持有若干面板引用，提供两件事——按序绘制的遍历，和「给定点，最前面的哪个面板拥有它」的查询（命中成员或留白阻挡）。

采用范围：

| 现有内容 | 处理 |
| --- | --- |
| Geometry / Eraser / Recall 属性面板、ConstructionPrompt | 共同部分（外框、开合、候选/已呈现、留白）改用面板；各自的成员、排布、业务状态留在原类 |
| More 面板、主栏取色器面板、两个属性 tooltip、底栏吸附/居中提示 | 从 `Bar.RenderLoop.cpp` 的手工组织改为面板实例；底栏提示的四阶段文字交换状态机留在 `Inkeys.UI.Bar.BottomDock` |
| 翻页场景的 More / Theme 固定面板、分页根 | 改用面板与叠层，替换 `FrontPanel` 的手工优先级 |
| 形状样式栏的工具栏、色板、边数、取色器层 | 改用面板与叠层 |
| Main 外框 | 改用面板；换边关键帧、fold 宽度等产品动效参数由主栏设置 |
| 主栏输入的留白阻挡（`Bar.Interaction.cpp` 各 PointerStage） | 「点属于哪个面板」改查叠层；之后的业务分派不变 |

### 6.3 共享光照（`:Lighting`）

三件事分开：

| 责任 | 归属 |
| --- | --- |
| 怎么画（点光边框、柔光遮罩、阴影、高光） | `:Renderer`，不变 |
| 光源状态如何演进 | `:Lighting` 的光照状态机：接收每帧输入（主光锚点矩形、光标位置与可见性、强调色、是否处于绘制模式、开关），推进主光/光标光位置与笔色过渡，产出帧光照快照 |
| 输入从哪来 | 产品层：主栏根据工具模式选择锚点按钮、取当前笔色（今天 `PrepareFrameLighting` 里 `Pen→Draw、Eraser→Eraser…` 那段和 owner 的 `mainLightingInput`），留在 Bar |
| 谁消费、如何定位 | `:Lighting` 的发布/订阅：发布屏幕坐标快照，订阅方登记回调与自身屏幕范围；发布时比较快照，按影响范围筛选后在锁外回调。提供屏幕坐标到局部坐标的映射函数 |

- 同一进程只有一份共享光照状态，由主栏推进并发布（与今天一致）。翻页与形状编辑通过订阅消费，不再由发布端点名唤醒 SelectionOverlay。
- 其他表面的指针进入/离开通知改为调用 `:Lighting` 的接口（线程安全地记录来源并唤醒发布方），不再 `PostMessage` 到 `floating_window`。主栏帧循环从该接口读取。
- 主栏没有独立窗口时由谁推进光照，属于第3步；本设计只保证推进方不必是「拥有主栏 HWND 的对象」。

### 6.4 连续颜色选择器（`:ColorPicker`，D5）

一个组件，包含状态、布局、绘制、手势：

- 面板 300×209、圆角 8；色板、顶部预览、位置标记、RGB 区、长按提示与进度环——两版静态数值本来一致（R2 §6），直接采用。
- 亮暗切换与关闭为标准按钮（30×30、圆角 4）。
- 输入参数（由场景决定）：放置结果（面板矩形与朝向——主栏跟随主栏上下方向，样式栏沿用它自己的避让算法）、默认色系、移动阈值所用的缩放、初始颜色。
- 输出回调（由场景决定）：预览（拖动中每次变化）、提交（抬起且仍持有捕获）、取消。主栏：预览即 `SetInteractionPenColor(…, false)`，提交即 `SetInteractionPenColor(…, true)`。样式栏：预览只更新本地显示，提交调用一次 `SetStyle`，取消不提交。**两种提交时机保持不变。**
- 捕获与取消的原生协议仍由各宿主负责，组件只接收「按下/移动/抬起/取消」。
- HSV 换算、色板投影函数从 `Bar.Layout.cppm` 迁入本分区；色相/明暗渐变画刷留在绘制器。

## 7. L4 设计

### 7.1 帧目标

帧目标拥有设备上下文和绘制目标，提供三种形态：

| 形态 | 用途 |
| --- | --- |
| GDI 兼容位图目标 | 现有 ULW 宿主（主栏、四个翻页窗、SelectionOverlay、白板 Freeze 背景）。创建参数与今天完全相同（`TARGET | GDI_COMPATIBLE`、BGRA8 预乘） |
| 普通位图目标 | 不带 GDI 兼容标志的离屏位图。用于本任务的第二宿主验证 |
| 外部上下文 | 调用方提供已经处于 `BeginDraw` 状态的设备上下文，帧目标不负责 Begin/End/Clear。为第3步白板直接绘制预留；本任务只需通过编译并有一条测试覆盖 |

重建条件（设备代次、资源缺失、像素尺寸变化）与今天一致。丢弃与重建时通知绑定的内容绘制器。每个宿主仍有自己的帧目标与绘制器实例，共享 RenderPipeline 的设备 epoch 与工厂。

### 7.2 帧事务与提交端

把今天三处重复的「Begin → 绘制 → Finish → GetDC → 提交 → ReleaseDC → End」固化为一个帧事务对象：

- 开始时设置脏区裁剪并透明清空（裁剪先于清空）；
- 绘制阶段把设备上下文交给内容；
- 结束绘制命令（退出裁剪/图层）后，把像素交给**提交端**；
- 提交端是宿主提供的回调：ULW（`UpdateLayeredWindowIndirect`）、Freeze 表面提交、测试读回、或「无」（外部上下文形态下宿主自行呈现）；
- 事务返回逐步结果：GetDC 的 HRESULT、提交端的成功与错误码、ReleaseDC 的 HRESULT、EndDraw 的 HRESULT。四步全部成功才算像素提交成功。

**成功确认仍由宿主决定**，事务不替宿主确认：

- 主栏：四步成功即推进脏区、视口、映射、窗口边界、命中与各面板的已呈现状态（`BarPresentDecision::CompleteAttempt` 的输入来自事务结果）。
- 翻页：四步成功后发布原生候选，等到精确回执才按序号确认候选。
- SelectionOverlay：四步成功后再 `SetBounds`、`Show`，都成功才确认。
- 白板背景：四步成功形成提交记录，等原生候选回执。

`.trellis/spec/native-desktop/rendering-and-ui.md` 中的相关条款（R4 §8.2 列出）是硬约束：裁剪先于清空、GetDC 前退出裁剪、ReleaseDC 传空修改矩形、单次 Begin/End、成功后才发布命中、失败保留旧成功状态并重试、局部目标丢失与共享设备丢失分开处理。

### 7.3 坐标映射

定义一个值类型描述「内容布局坐标 ↔ 目标像素 ↔ 屏幕」的映射（屏幕原点、布局缩放、目标内原点、光效外扩）。内容的绘制、光照局部映射、命中使用同一份映射；宿主每帧提供。主栏今天散落在 RenderLoop 的 `capacityOrigin`、`monitorOrigin`、outset 归入这份映射。

### 7.4 输入接入（`:HostInput`）

- `Bar.Interaction.cpp` 中的共享输入邮箱、表面注册表、worker 泵、离屏 worker（R4 §5）迁入 `:HostInput` 的实现单元；`SurfaceInputEvent`、`RegisterInteractionSurface`、`SubmitSurfaceInput`、`DrainInteractionSurfaceCallbacks`、`CreateOffscreenInteractionWorker` 的声明随之迁入。
- 主栏自己的消息路径（`QueueWindowMessageInLayoutSpace` → 邮箱的 `forMain` 分支 → 主栏协程）改为：主栏向 `:HostInput` 注册一个「主内容处理器」，worker 泵通过它派发，主栏协程与业务留在 `Bar.Interaction.cpp`。
- 表面注册的键增加一个内容实例标识（今天等价于恒为 0），替换/注销按「HWND + 内容实例」进行。现有调用方行为不变；同窗多内容的路由留给第3步。
- 原生 Down/Up、捕获票据、来源与注册代次、取消屏障、邮箱溢出处理（`ui-pointer-session.md` 的合同）原样保留。

### 7.5 三类宿主的接入

| 宿主 | 改动 |
| --- | --- |
| 主栏（`Bar.RenderLoop.cpp`） | `CalculateDirtyAndDrawPresent` 中 Begin 到 Finish 之间的内容绘制抽成独立的「主栏内容绘制」入口，参数为绘制器、设备上下文、坐标映射；外层改用帧目标与帧事务。提交端为 ULW，测试时可替换（现有 `CreateBarOffscreenRenderCallback` 的替换点保留并改接到提交端） |
| 翻页（`PageControl.cpp` + `BarPageSurface`） | `BarPageSurface` 不再对外暴露绘制器与资源入口，只保留内容接口（配置、推进、`Render(设备上下文)`、候选/确认、命中）。帧目标由 `PageControl` 的每个 `SurfaceState` 持有。`Bar.Scene.cppm` 的接口因此不再出现绘制器类型 |
| SelectionOverlay + `ShapeEditorSurface` | `ShapeEditorSurface::Present` 拆开：内容侧只有 `Draw(设备上下文)`；帧目标与帧事务由 `SelectionOverlay` 持有并驱动 |
| 白板背景（`Whiteboard.cpp`） | 随 `BarPageSurface` 接口变化改用帧目标与帧事务，提交端为 Freeze 表面提交。行为不变 |

`BarPageSurface` 与 `ShapeEditorSurface` 仍是产品内容场景，留在 `UI/Bar/`，不迁入 UI3。

### 7.6 第二宿主验证（D2）

仓库已有无窗口主栏绘制入口 `CreateBarOffscreenRenderCallback`，但它使用主栏自己的 GDI 兼容目标，且默认不校验像素（R4 §7.3）。本任务新增一个命令行测试入口 `--ui3-host-offscreen-test`（实现放在 `UI/Bar/Bar.Host.Test.cpp`，与既有 `Bar.*.Test.cpp` 同样编入主工程），验证：

1. 测试自己创建帧目标（普通位图形态）和内容绘制器，主栏内容经「主栏内容绘制」入口画入；全程 `floating_window` 为空，不创建任何 HWND。
2. 同一帧状态再经现有 GDI 兼容路径画一次；两份 BGRA 像素逐像素一致。
3. 画入结果在主栏预期矩形内非全透明，矩形外全透明。
4. 外部上下文形态：测试自行 `BeginDraw`，把上下文交给内容，结果与第 1 项一致。
5. 帧目标丢弃重建后再画一次，结果与重建前一致（验证设备缓存归属）。

输出沿用既有约定：结果日志写到 `Build/ui3-host/results.log`，stderr 汇总 checks/failures，退出码 0 成功、1 断言失败、2 COM 失败、3 管线失败。不新增 RenderPipeline 的 `Client` 或 `WindowRole`。

## 8. 日志设计（D6 之外的 R6）

- 位于 `UI/UI3/` 的代码使用 `Inkeys::Log::Module::UI3`；留在产品目录的代码沿用 `Bar`、`PageControl`、`SelectionOverlay`、`Whiteboard`。代码迁入 UI3 时其现有日志的 Module 随之改为 `UI3`。
- 模块文件在全局模块片段以相对路径包含 `Log/Log.h`，不通过 `IdtMain.h`。
- **线程规则**：渲染线程、交互 worker 线程、窗口过程中只允许 `TryFormat` / `TryWrite` / `Trace`。`Info/Warn/Error` 这类可能等锁的接口只用于初始化与销毁路径。被迁移或修改的代码里现有违反此规则的调用（R1 §9 的 17 处、`Bar.RenderLoop.cpp` 呈现失败分支、`Bar.Interaction.cpp` 的三处回调异常）一并改为非阻塞接口。
- **不做逐帧日志**。只在边沿或经闩锁后记录。

| 事件 | 级别 | 频率控制 |
| --- | --- | --- |
| 帧目标创建/重建/丢弃（形态、像素尺寸、设备代次） | Info | 每次发生 |
| 设备相关资源创建失败（带 API 名与 HRESULT） | Error | 每设备代次每类资源一次 |
| 精确遮罩失败回退 | Warn | 每设备代次一次 |
| 帧事务失败（四步各自的结果码） | Error | 首次失败记录，持续失败不重复 |
| 帧事务从失败恢复 | Info | 每次恢复一次，带连续失败次数 |
| 宿主绑定/解绑、输入表面注册/注销（角色、代次、内容实例） | Info | 每次发生 |
| 输入邮箱溢出并建立取消屏障 | Warn | 每次发生 |
| 输入/宿主回调抛出异常 | Error | 每次发生 |
| 主题状态发布（深浅、透明度发生变化） | Debug | 值变化时 |
| 面板开合（面板标识、目标状态） | Debug | 状态切换时 |
| 颜色选择器打开/提交/取消 | Debug | 每次发生；不记录具体颜色以外的用户内容 |
| 长按进度资源失败后停止尝试 | Warn | 每次手势一次 |
| SVG 预处理钩子失败、图片解码失败（资源标识） | Error | 每资源每设备代次一次 |

日志不包含用户文档内容。现有 `RenderPipeline` 的 `[UI3Diag]` 聚合机制不改。

## 9. 有意变更清单（D5、D7）

实施不得产生本清单之外的可见变化。若基线比对发现清单外的差异：属于迁移错误的修掉；确属统一行为带来的，停下来补进清单并请用户确认。清单中「获得」类条目在调研里标为推测（R2 §2.2），以基线比对的实测为准更新。

| # | 位置 | 变化 |
| --- | --- | --- |
| C1 | 形状样式栏取色器 | 关闭时长 0.24 s → 0.40 s |
| C2 | 形状样式栏取色器 | 紧凑态比例由 60/300 改为主栏的 60/370 |
| C3 | 形状样式栏取色器 | 透明度过渡由独立 Sine 曲线改为跟随几何进度（主栏方式） |
| C4 | 形状样式栏取色器 | 亮暗按钮图标由过渡换图改为按当前色系直接切换（主栏方式） |
| C5 | 主栏取色器的亮暗/关闭按钮 | 改用标准按钮后，释放回弹不再被夹到 ≤1，会出现标准按钮的轻微外扩 |
| C6 | 11 个预设色块 | 获得标准按钮的按压缩放与 hover 反馈；按下即改色、拖过相邻色块连续选择的语义不变 |
| C7 | 自定义色盘入口、More 关闭 X、笔型扩展入口、「自由线」行、三档粗细与精调入口、属性 tooltip 的关闭 X 与问号入口 | 改用标准按钮；尺寸、图标大小、圆角作为参数保持原值。若现状缺少 hover 或选中光照，则获得标准反馈 |

**不变并保留为场景参数**：取色器默认色系（主栏暗、样式栏亮）；取色器放置与避让规则；长按移动阈值所乘的缩放；全部长按时长；取色器面板外框的鼠标边缘光强度倍率（主栏 0.30、样式栏 1.0——S5c 实施时发现的调研未列差异，统一它属于清单外的持续可见变化，未经用户确认不统一）。实施中再发现的两版静止态差异同样做成场景参数并在进度记录里列出。

**保留专有绘制及理由**（通过扩展内容合同接入）：

| 内容 | 理由 |
| --- | --- |
| 线宽斜向胶囊预览、粗细连续预览、滑块与精调拨盘 | 绘制内容由笔宽参数决定，没有标准按钮对应物 |
| 翻页主题色与网格/点阵预览 | 内容来自白板主题绘制 |
| 橡皮示例与自动粗细分隔线/箭头 | 自持位图缓存的示例绘制 |
| 状态指示器及其超椭圆进度环、主 Logo | 专有外观与状态表达 |
| 多步形状提示的步骤圆环与文字 | 业务步骤显示；其操作按钮已是标准按钮 |
| Geometry 滚动条 | 滚动控件，不是按钮；本任务不新增通用滚动条 |
| `BackgroundWarning`、Geometry 辅助说明 | 未启用的预留内容，不动 |

## 10. 规范更新（D6）

任务收尾时更新 `.trellis/spec/`（RH §8 给出落点与过期表述）：

| 文件 | 内容 |
| --- | --- |
| 新增 `native-desktop/ui3-layers.md`，并在 `native-desktop/index.md` 加链接 | 四层责任、`UI/UI3/` 模块表、依赖方向、宿主边界契约、有意保留的专有绘制、UI3 日志规则 |
| `native-desktop/cpp-conventions.md`「module 与传统头文件边界」 | 头文件判定标准（D4）：含类或函数的写成 `.cppm`（+`.cpp`），只起引用作用的可留 `.h`；`.cppm` 全局模块片段内可 `#include`；独立模块与分区的选用规则（§2.1）；不得跨模块前置声明；预处理效果不能导出 |
| `native-desktop/directory-structure.md` | 新增 `UI/UI3/` 行；更新 `UI/Bar/` 的描述；`Modules\Inkeys\UI\UI3` filter；分区聚合的例子 |
| `native-desktop/rendering-and-ui.md`「Inkeys.UI.Bar」小节 | 公共按钮/绘制器的归属改为 UI3 |
| `native-desktop/errors-logging-and-resources.md` | UI3 的线程与日志级别规则 |
| `native-desktop/ui3-render-diagnostics.md` 第 45 行附近 | 改正仍在描述旧 async logger 的过期文字 |
| 源文件编码 | 在 `cpp-conventions.md` 写明「改动的源文件为 UTF-8 带 BOM + CRLF」 |

## 11. 行为保持的护栏

「一个任务做完四层」的主要风险是回归难以定位，所以在动产品代码之前先建立基线：

- **像素基线**：新增一个命令行入口 `--ui3-golden-dump <目录>`，经现有无窗口回调把一组固定场景画出并导出 BGRA。动画以固定帧时间推进到静止后再导出。场景至少覆盖：主栏展开（浅/深，100%/150%）、主栏收起、More 展开、三种绘制属性面板、主栏取色器（亮/暗）、两个 tooltip、底栏提示、多步形状提示、翻页（PPT 紧凑、白板展开、More、Theme）、形状样式栏（工具栏、色板、取色器）。共享管线默认 WARP，输出应可逐字节复现；实施第一步先验证「同一版本连跑两次一致」，不一致的场景从基线中剔除并记录。
- 每个实施步骤结束后重新导出并与基线比对：纯迁移步骤要求逐字节一致；涉及有意变更的步骤只允许清单内场景出现差异，并在该步骤后刷新基线。
- 基线文件放在 `Build/ui3-golden/`，不进仓库。
- **既有测试**：`InkeysHeadlessTests.exe --no-window`、`--ui3-geometry-attribute-test`、`--bar-eraser-offscreen-test`、`--ui3-shape-editor-test`、`--ui3-multitarget-benchmark`。动代码前先各跑一次记录现状（已知既有失败不归本任务）。
- **人工**：像素基线不覆盖动画过程、真实输入、DWM 合成与 Win7，这些由用户按 `implement.md` 的实机清单确认。

## 12. 风险与取舍

| 风险 | 应对 |
| --- | --- |
| 模块环：UI3 → Bar → UI3 | §1 的单向规则；§3 的解耦在移动 `:Renderer`、`:Button` 之前完成；每步完整构建 |
| `InkeysHeadlessTests` 被迫编译重型分区 | 被测试消费的都是独立模块（§2.1）；`Bar.Scene.cppm` 接口去掉绘制器类型（§7.5）。若仍有测试需要某个分区的类型，优先把那部分拆成独立模块，其次才在测试工程登记 UI3 的接口单元 |
| 头转模块后的可见性、ODR、匿名命名空间与 `thread_local` | 按 §2.3 逐个处理；一次只转一组并构建 |
| 分区增多导致 BMI 重建变慢 | 接口单元只放声明与必须内联的内容，实现进 `.cpp` |
| `Bar.RenderLoop.cpp`、`Bar.Interaction.cpp` 体量大，抽取易漏 | 像素基线 + 既有离屏测试；抽取时不改逻辑顺序 |
| 改名步骤 diff 巨大 | 放在最后单独一步，纯机械，改名前后像素基线逐字节一致 |
| 光照通知由 `PostMessage` 改为直接接口后的时序差异 | 保持「记录来源 + 唤醒发布方」的语义；实机清单包含跨表面光标光 |
| 增量链接文件达到 256 MiB 报 LNK1000 | 删除 `Inkeys.ilk` 后重链，不是源码问题 |

**不做的事**：不引入递归控件树或通用布局引擎；不新增 RenderPipeline 客户端或窗口角色；不改 Draw3；不改 `BarPresentDecision` 的决策逻辑；不实现同窗多内容路由；不把翻页场景与形状编辑场景迁出 `UI/Bar/`；不升级任何依赖。

## 13. 回滚形态

每个实施步骤是一个可独立构建、通过基线比对的检查点。出现无法在本步骤内解决的问题时，回退到上一个检查点的工作区状态（步骤之间由用户决定是否提交；未获授权不提交）。垫片机制保证中间状态下产品层代码无需同步改名。
