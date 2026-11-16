# Inkeys dev：Bar / UI3 分层封装现状调查

> 用途：为后续 UI3 公共能力整理、统一控件体验，以及白板独立窗口直接绘制主栏提供背景参考。
>
> 本文记录当前代码事实、现有复用程度、尚待明确的责任边界和应保留的行为。本文不确定最终类名、接口签名、目录结构、线程方案或呈现后端，不作为已经批准的具体实现设计。

## 1. 调查基线与范围

- 仓库：Alan-CRL/Inkeys。
- 分支：dev。
- 固定提交：**c06697b1ab2ba01800edf677b4fb46d4d0e2de61**（[查看该提交](https://github.com/Alan-CRL/Inkeys/commit/c06697b1ab2ba01800edf677b4fb46d4d0e2de61)）。
- 提交时间：2026-11-16 11:39:34，Asia/Shanghai。
- 提交说明：Merge pull request #274 — Refactor settings navigation and migrate legacy configurations。
- 调查日期：2026-11-16。
- 方法：读取固定提交中的定义、实现及实际调用链，交叉检查主栏、翻页、形状编辑、白板背景、共享渲染管线和窗口服务。历史研究文档没有被当作当前实现的替代证据。
- 验证范围：静态代码调查；未在 Windows 上编译、运行或实测外观、输入与性能。文中“已经复用”表示生产代码存在调用，不表示所有场景已通过本次运行验证。

本次主要覆盖用户提出的四个逻辑层次：

| 逻辑层次 | 希望承担的责任 | 服务对象 |
| --- | --- | --- |
| 第一层：基础绘制与动效 | 圆角矩形、超椭圆、图片、SVG、文字、动画值及基础绘制能力 | 所有 UI3 内容 |
| 第二层：基础控件 | 按钮及其文字、图标、角标、进度反馈、状态与交互；公共光影的绘制 | 普通按钮及有特殊内容的控件 |
| 第三层：序列与面板 | 按钮序列、布局、外框、组动画、光照接入、面板间遮挡与交互 | 主栏、二级菜单、提示、翻页、样式栏等 |
| 第四层：宿主与呈现 | 放到哪个窗口或绘制目标；坐标、输入接入、设备资源、绘制帧与提交 | 独立覆盖窗、未来白板内部等 |

这四层是**责任划分**，不表示必须采用四级继承、四个大类，或一套递归控件树。设置界面整体迁移、插件 SDK 和白板渲染架构的完整设计不属于本次调查范围。

## 2. 核心判断

**现有代码已经建立了大量第一层和第二层能力，也有第三层的布局与特定场景封装。接下来需要把已有能力整理为清晰的公共边界，继续补齐面板组合和宿主解耦。**

### 2.1 已有能力不能忽略

当前并非只存在大量彼此无关的自绘函数：

1. 图元、动画、文字、SVG/PNG、材质和光影已有集中实现。
2. 按钮已有共享的状态外观、内部布局、动画推进、绘制、角标和命中入口。
3. 主栏、More、部分绘制属性、翻页、形状样式栏、多步形状提示已经使用这些公共入口。
4. 翻页与形状编辑各有内容场景，窗口侧也已经通过 Window Service 和 RenderPipeline 管理。
5. 光影已有“主栏计算并发布、其他表面按自身位置消费”的快照链路。

依据：[图元定义][S01]、[共享按钮入口][S08]、[按钮实现][S10]、[场景接口][S18]、[共享光照发布][S22]。

### 2.2 目前最主要的缺口

**内容仍然在多个地方依赖主栏状态、主栏输入调度、特定 HWND、专用目标位图和 ULW 提交流程。** 有公共绘制函数，和能够把同一内容完整放进白板窗口，是两个不同的完成程度。

此外，通用列表布局已经存在，但“按钮序列 + 外框 + 组动画 + 光照 + 遮挡命中 + 可视范围 + 失效通知”的完整面板责任仍由多个场景分别组织。颜色选择器则是一个很明确的复用缺口：数学和绘制零件已共享，完整面板与手势仍有两套。

依据：[按钮的全局依赖][S11]、[主栏面板编排][S14]、[renderer 目标资源][S05]、[主栏窗口提交][S25]、[颜色选择现状][S31]。

### 2.3 四层现状速览

| 层次 | 已有基础 | 当前还未完全统一的部分 |
| --- | --- | --- |
| 第一层 | 图元、动效、DWrite、图像缓存、光影、主题、尺寸、脏区 | 图元/动画模块含产品特例；renderer 同时拥有目标资源、帧生命周期和主栏光源策略 |
| 第二层 | 组合按钮、状态样式、角标、主副文字、按压、统一绘制、命中与基础指针状态 | 主栏业务字段和全局读取；完整手势分散；进度环只部分组件化；部分小按钮仍手工编排 |
| 第三层 | 列表容器、四种列表布局、公共外框绘制、多个独立面板/场景 | 面板生命周期、层叠、开合、光照接入、damage 与扩展内容的统一约定 |
| 第四层 | Window Service、共享 RenderPipeline、独立场景和提交回执 | 固定窗口角色、HWND 输入路由、GDI/ULW 资源与场景交织；白板内嵌目标尚未形成统一接入边界 |

## 3. 当前模块如何对应这些责任

### 3.1 UI3 的公共能力主要仍位于 Bar 模块

本次代码中的基础 UI3 能力主要在 Inkeys/Inkeys/UI/Bar 下，通过 Inkeys.UI.Bar 的模块分区组织。公共图元和控件能力没有形成一个与主栏产品完全分离的独立 UI3 包。

| 模块 / 文件 | 当前主要责任 | 跨层之处 |
| --- | --- | --- |
| Bar.UI.cppm / .cpp | 图元状态、继承坐标、SVG/图片/文字资源与缓存 | 含主 Logo、常驻表面等产品语义 |
| Bar.Animation.cppm / .cpp | 动画值、颜色、透明度、曲线、时间线 | 同时放有笔型、粗细预览等具体控件 helper |
| Bar.Rendering.cppm / .cpp | 图元绘制、字体、光影、缓存、裁剪 | 同时拥有 DC、目标位图、GDI interop、Begin/EndDraw 和主栏光源策略 |
| Bar.Button.cppm / .cpp、Bar.Main.cppm 中的共享入口 | 组合按钮、视觉、交互状态、列表与布局 | 控件数据和部分函数仍包含主栏业务和全局状态 |
| Bar.Metrics、Theme、ThemeMaterial、Layout | 尺寸、语义色、材质、几何和颜色换算 | 主题源及部分布局规则仍以主栏产品为中心 |
| Bar.RenderLoop.cpp | 主栏整帧更新、布局、组合绘制、光照发布、脏区、提交 | 内容组织和独立窗口呈现仍在同一协调器中 |
| Bar.Scene.cpp | 翻页场景及共享光照接线 | 包含分页、More、Theme 固定面板和目标资源 |
| Bar.ShapeEditor.cpp | 当前形状编辑 UI、样式栏、参数面板、取色器和编辑手势 | 既是内容场景，也包含 GDI/ULW Present |
| PageControl.cpp | 四个分页实例、PPT/白板状态投影及交互、窗口提交 | 四位置和 HWND 拓扑仍进入运行时 |
| SelectionOverlay.cpp | 形状编辑场景的窗口与原生输入接入 | 读取主栏窗口矩形用于避让 |
| RenderPipeline、Window | 渲染调度/设备与原生窗口管理 | 使用具体产品 Client/WindowRole 枚举 |

模块入口与定义依据：[Bar 导出入口][S00]、[场景接口][S18]、[管线定义][S24]、[窗口服务与角色][S42]、[SelectionOverlay 接入][S27]。

### 3.2 “Surface”目前有不同含义

阅读后续实现时，需要区分：

| 名称 | 实际含义 |
| --- | --- |
| BarPageSurface | 分页/背景内容场景，附带布局、输入快照、动画和 renderer 资源 |
| ShapeEditorSurface | 形状编辑内容场景，范围超过单独样式栏，也带 Present |
| WindowSurfaceState / SurfaceSnapshot | HWND、窗口代次、边界、可见性等原生窗口事实 |
| renderer 的 target bitmap | 真正接收 D2D 绘制的像素目标 |

这些名字相近，但并不代表已经拥有同一个通用“表面层”。后续整理时应先对齐责任，再判断是否需要调整类型和模块。

## 4. 第一层：基础绘制与动效现状

### 4.1 已有能力清单

| 能力 | 当前实现与复用程度 | 证据 |
| --- | --- | --- |
| 位置、尺寸、透明度和父坐标 | BarUiInnheritBaseClass / BarUiInheritClass；含相对锚点、外侧停靠和实际绘制位置 | [S01] |
| 圆角矩形 | BarUiShapeClass；支持填充、边框、独立透明度、主题材质、点光边框和圆角命中 | [S01]、[S04] |
| 超椭圆 | BarUiSuperellipseClass；绘制与命中已存在，公共路径生成也供 StartupPreview 使用 | [S01]、[S02] |
| SVG | BarUiSVGClass；资源/字符串输入、双强调色、旋转、内容切换动画，lunasvg 栅格化后上传 D2D | [S01]、[S03] |
| 图片 | BarUiPNGClass；内存/资源输入、解码、预乘 BGRA、旋转及当前位图缓存 | [S01]、[S03] |
| 文字 | BarUiWordClass；内容、字号、颜色、切换动画；DWrite 绘制、测量和按钮标签适配 | [S01]、[S06] |
| 字体策略 | 字体集/字族/字号/字重/locale 等格式缓存；本地化更新；静止目标的设备像素字号修正 | [S06]、[S40]、[S41] |
| 动画 | 数值、颜色、透明度、显隐状态、目标值、曲线、线性/关键帧时间线、事务保护 | [S07] |
| 光影 | 主光/光标光、点光边框、柔光遮罩、阴影、顶部高光 | [S04]、[S21] |
| 绘制辅助 | 颜色底图、色相/明暗渐变、普通圆环、滑块 thumb | [S32]、[S47]、[S48] |
| 脏区与裁剪 | 包络计入边框、柔光、阴影、抗锯齿和旋转；有 frame clip 与 bounds cull | [S33]、[S55] |
| 统一样式常量 | Metrics、Theme、ThemeMaterial 已集中一批尺寸、字号、颜色、材质、hover 和按压参数 | [S09]、[S23]、[S49] |

这里的“封装”已经包括状态、缓存、动画和失效处理，不只是包一层 D2D 绘图调用。

### 4.2 Renderer 已经有服务注入

BarUIRenderingServices 已提供 typography、resetDeviceCaches、mainLightingInput 三个服务。BarUIRendering 支持默认构造和服务注入；旧的主栏 owner 构造器把 owner 转成这些回调。没有外部 typography 时可以创建本地格式缓存。

因此，现状应表述为：**已有减少主栏 owner 依赖的工作，但仍存在全局策略和目标生命周期耦合。** 不能把它描述成完全必须绑定一个完整主栏对象。[S05]、[S54]

### 4.3 第一层中仍混有更高层语义

几个能够从代码直接指出的例子：

- Shape / Superellipse 含 themeSurface、residentSurface、PenWhenDrawing 等标志；绘制时会读取主栏常驻背景透明度策略。
- SVG 带 mainLogoAppearance，资源处理会修改主 Logo 的特定节点。
- Animation 模块同时包含笔型扩展、粗细预览、预设圆/数字转换等具体控件逻辑。
- 默认 GetThemeColor 读取 currentBarThemeStyle，并在检测变化时请求 SelectionOverlay 重绘。
- PrepareFrameLighting 根据工具模式选择主按钮、选择、画笔、橡皮、图形或穿透按钮的光源锚点。

这些是已有产品能力，应被保留并放到合适的责任边界；它们并不都属于任意 UI3 图元的基础属性。[S01]、[S03]、[S07]、[S21]、[S23]

### 4.4 白板直接绘制时，第一层必须澄清什么

当前 EnsureDeviceResources 创建自己的 D2D device context、带 GDI_COMPATIBLE 的目标位图和 GDI interop；BeginFrameDraw 还负责 BeginDraw、透明清空和裁剪，EndFrameDraw 负责结束绘制和故障处理。[S05]

因此，未来需要明确：

- 内容使用宿主的 DC/目标，还是拥有独立目标；两种用法如何保持同一内容语义。
- BeginDraw、EndDraw、Clear、clip、transform、整体透明度分别由哪一方负责。
- 图片、SVG、画刷、光影遮罩等缓存属于哪个设备和哪个生命周期。
- DPI、目标缩放、字体目标尺寸、内容尺寸动画之间如何传递。
- 更换宿主、改变目标尺寸或设备代次时，哪些状态保留、哪些资源失效。

这些都是必要边界，本文不确定最终使用何种对象组合。

设备问题不能只靠函数接收一个 DC 参数解决。现有 SVG/PNG 位图和 renderer 画刷缓存具有设备归属。Microsoft 的 Direct2D 文档明确指出，混用不同资源域的设备上下文、画刷和位图会导致 Wrong Resource Domain；换到另一个 HARDWARE 设备时需要匹配资源所属设备。[D01]、[D02]

## 5. 第二层：按钮、交互与附加视觉现状

### 5.1 已有共享按钮运行时

现有 BarButtonClass 已包含背景、SVG/PNG 图标、主文字、按压倍率、hover 阶段、可选角标、本地或共享状态以及 clickFunc。[S12]

共享入口已经覆盖：

| 环节 | 现有入口 | 已解决的问题 |
| --- | --- | --- |
| 状态外观 | RetargetBarButtonInteractionVisual / RetargetBarFixedButtonVisual | visible、enabled、selected、pressed、颜色、边框光影与按压目标 |
| 内部布局 | ConfigureBarButtonVisualTargets | 标准尺寸、图标槽、文字槽及部分字号规则 |
| 动画 | AdvanceBarButtonVisual | 背景、图标、主副文字、按压和角标统一推进 |
| 绘制 | DrawBarButtonVisual | 背景、SVG/PNG、文字、角标及统一按压变换 |
| 旧字段接入 | BarButtonVisualParts / DrawBarFixedButtonVisual | 既有 Shape/SVG/Word 字段可以复用公共行为 |
| 命中 | BarButtonHitSnapshot / HitTestBarButton | 圆角/超椭圆几何、可见和启用状态 |
| 基础指针状态 | ButtonPointerState | Move、Down、Up、Leave、Cancel 与抬起后的 hover 抑制 |

依据：[S08]、[S10]、[S13]。

实际消费方包括 Main、More、笔型按钮、图形/橡皮/撤回属性、多步形状提示、翻页场景、形状样式栏。**保留 shapeMap/svgMap/wordMap 的某项控件，也可能已经通过适配器使用公共按钮能力；不能仅凭字段形式认定它仍是完全独立自绘。**[S14]、[S15]、[S19]、[S28]、[S52]、[S53]

### 5.2 尺寸、角标、文字与进度环的成熟度并不相同

| 变化点 | 当前事实 | 后续需整理之处 |
| --- | --- | --- |
| 尺寸不同 | 已有主列表 2×2、2×1、1×1、分隔项及多种视觉布局；Metrics 集中标准槽位 | 将通用尺寸规则与 Main/More/分页等场景规则区分 |
| 角标 | 已是 optional 按钮部件；支持显隐、旋转、上下换边、跟随颜色/透明度，并计入 bounds | 确认其他消费者通过同一能力接入 |
| 字号/颜色变化 | 已有颜色动画、文字切换、字重/对齐、副文字和标签拟合 | 梳理仍读主栏状态的特殊规则，保留不同文字更新语义 |
| 普通圆环 | renderer 已有 DrawRing / DrawProgressRing | 进度来源与业务状态仍由消费方提供 |
| 沿圆角外框的进度环 | ButtonFrameRing 已共用绘制算法，支持等待弧线及长按进度 | 计时、状态、淡入淡出、失败与资源重置仍由面板持有 |
| 自绘内容 | underlay/content 回调已有实际使用 | 需要同时约定可视包络、脏区、clip 和命中，而非只有 Draw 回调 |

角标是已经融入按钮模型的部件；进度环目前更多是共享绘制与各场景状态组合。二者不能按同一完成程度描述。[S08]、[S09]、[S12]、[S16]、[S32]、[S48]、[S58]

### 5.3 现有公共按钮仍有主栏依赖

明确例子：

- RetargetBarFixedButtonVisual 直接读取 barUISet.barStyle.darkStyle。
- ConfigureBarButtonVisualTargets 对 Geometry 文字的特殊适配直接使用 barUISet.spec 测量，并依其设备代次缓存。
- BarButtonClass 包含主栏 preset、用户可见性、上下文隐藏和 More 收起策略。
- BarButtonSetClass 管理注册、A1/Extension/A2、用户配置和 More 分流，属于主栏产品组织，不能默认要求所有面板都依赖它。

这些事实说明需要区分“通用控件的视觉和状态”与“主栏按钮的产品定义、注册、配置和动作”。具体是否拆类型、通过服务传入或采用其他方式，留待实现设计。[S11]、[S12]

### 5.4 交互已经部分共用，但完整手势仍分散

ButtonPointerState 统一了基础点击状态；共享 InteractionMailbox 已按接触合并相邻 Move，保留 Down/Up 顺序，并对溢出进行取消保护。[S13]

不过：

- 通用按钮直接提供的是 clickFunc，并没有同等程度的统一右键、长按、双击回调组。
- Main/More 在 Bar.Interaction.cpp 中仍各自组织协程等待、命中、动作、收起和触觉反馈。
- 清空双击、主按钮右键关闭、PPT 焦点回交等按业务 preset 分支处理。
- 图形长按锁定等特定面板自行管理计时、进度和完成条件。
- 原生捕获、接触来源、窗口代次和成功呈现版本属于更外侧的输入适配，不能只靠按钮视觉状态覆盖。

后续要明确哪些交互语义所有按钮都应一致，哪些动作仍由场景决定；点击、拖动、长按、取消、业务拒绝和实际接受动作应有清晰区别。[S13]、[S15]、[S17]

### 5.5 仍可继续归一的具体自绘

例如 More 的关闭 X、笔型扩展入口仍手工计算按压矩阵、组织 Shape/SVG 绘制。它们可作为继续统一普通控件的候选，而线宽斜向胶囊、背景网格预览等特殊内容则适合保留扩展绘制。[S14]、[S15]、[S52]

当前 DrawBarButtonVisual 在存在 content/underlay 时会跳过标准 bounds 的提前拒绘，因为扩展内容没有额外包络约定。这是“保留扩展能力”还涉及失效范围与裁剪的直接证据，并非已确认的显示缺陷。[S10]

## 6. 第三层：按钮序列、面板外框与光影接入

### 6.1 列表和布局已经存在

BarButtonListClass 提供列表与强引用快照；ResolveBarButtonListLayout 支持 Main、GroupedGrid、SingleRow、GeometryGrid，返回按钮位置、尺寸、行列与分隔信息。[S08]、[S12]、[S34]

但这套布局仍包含具体产品规则，例如 Main 的特殊分隔、More 的分组/溢出及 GeometryGrid 的列数。保留这些规则是合理的，关键是让它们和通用排布能力有清楚关系。

### 6.2 外框绘制函数不等于完整面板封装

DrawBarBackgroundVisual 当前主要是转发 renderer.Shape。Main/More 的外框、开合、局部坐标、遮盖顺序、clip 和常驻透明度仍在 RenderLoop 组织。[S14]、[S34]

现有 Geometry、Eraser、Recall、ConstructionPrompt 已有各自面板类；BarPageSurface 也形成了特定场景封装。这说明已经有可整理的基础，但尚未形成一致的通用面板边界。

后续需要能够描述一块 UI 内容的：

- 成员序列、排布结果、背景/边框和内容区域。
- 进入/退出、展开/收起、组透明度和局部变换。
- 可视范围、可交互范围、光影外扩范围与脏区。
- 叠层顺序和遮挡规则，包括上层面板的留白也阻止下层按钮点击。
- 锚点、屏幕边缘避让、宿主边界、弹出方向。
- 光照输入、变化通知和需要继续播放动画的状态。
- 特殊内容与动作出口。

这是一组需要覆盖的责任，不要求必须构造递归 UI 节点图。

### 6.3 光影要分清三件事

| 责任 | 当前代码 |
| --- | --- |
| 如何画光影 | BarUIRendering 中的点光、渐变、柔光遮罩、阴影和高光 |
| 当前光源如何产生 | 主栏根据工具状态、按钮锚点、指针和笔色推进光源状态 |
| 哪些面板消费及如何定位 | 发布屏幕坐标快照，面板按 bounds 映射局部坐标，订阅变化并失效/唤醒 |

当前主栏只推进一份共享主光/光标光状态，BarPageSurface::PublishSharedLighting 发布；翻页和 ShapeEditorSurface 消费该快照。其他表面的指针进入/离开通知仍发到 floating_window，发布逻辑还包含对形状编辑场景的特定唤醒。[S21]、[S22]、[S35]、[S56]、[S57]

因此，用户提出的第二层“公共光影绘制”和第三层“光影发布”已有实现基础，后续需要确定其边界与归属，尤其是主栏没有独立 HWND 时的来源、坐标和调度。统一体验要求同一场景中的光源状态一致，不宜因多个面板各自封装而无意产生不同步的光源状态。

## 7. 各服务对象的现状与接入重点

### 7.1 覆盖矩阵

| 服务对象 | 当前内容组织 | 当前宿主 | 主要待整理内容 |
| --- | --- | --- | --- |
| 主栏本身 | BarUISetClass、按钮集合、RenderLoop | WindowRole::Bar / floating_window | 内容组合、工具状态、拖动吸附和窗口提交的边界 |
| 主栏二级菜单/More | 部分列表化，部分固定属性字段及独立 Panel | 随主栏绘制 | 面板外框、开合、关闭按钮、层叠和通用成员 |
| 底栏文字提示 | bottomDockIndicatorWord / BottomDockIndicator | 主栏渲染循环内 | 外框、文字过渡、尺寸和显示生命周期 |
| 多步形状操作栏 | 独立 BarConstructionPrompt | 主栏绘制并提交 | 公共面板体验与业务步骤、对象身份分离 |
| PPT/白板翻页 | PageControl + BarPageSurface | 四个 Ppt* 窗口角色 | 内容场景与四窗口布局/原生输入/提交的边界 |
| 形状完成后样式栏 | ShapeEditorSurface 的一部分 | SelectionOverlay | 样式内容与对象控制点、手势和 Present 的边界 |
| 主栏连续颜色选择 | 主栏 maps + RenderLoop + Interaction | 主栏同一个 HWND | 面板、状态、布局与手势形成可复用单元 |
| 形状连续颜色选择 | ShapeEditorSurface::Impl::DrawPicker 等 | 随 SelectionOverlay | 与主栏统一视觉/交互，保留样式提交语义 |
| 绘制属性中的特殊内容 | 固定字段、自绘 helper、部分 Panel | 主要随主栏 | 保留专有能力，同时共用控件、材质和生命周期 |

依据：[S14]、[S18]、[S19]、[S20]、[S25]、[S27]、[S28]、[S29]、[S30]、[S31]。

### 7.2 翻页控件：四个空间实例，PPT/白板共用体系

当前四个 Surface 是 BottomLeft、BottomRight、MiddleLeft、MiddleRight；模式为 Hidden、PptCompact、WhiteboardExpanded。

**PPT 和白板并非各有四套。** PPT 可按配置显示底部和中部两组；白板展开模式使用底部左右两组，中部两组隐藏。每个实例有自己的 scene、动画、输入和呈现状态。[S20]

已有复用包括：

- BarButtonClass、公共按钮视觉和动画。
- 页码主副文字、等待/失败投影和加载框线环。
- 统一背景与光照。
- More/Theme 面板的基础控件与主题预览扩展。

仍然分别处理的内容包括：

- 四个窗口角色及位置布局。
- PPT 与白板的动作和交互能力；PPT 支持拖动、长按、滚轮、位置持久化等，白板此处只开放点击。
- More/Theme 的固定成员、组变换与层叠命中。
- 窗口捕获、工作区交接和提交回执。[S43]

另外，纯数字页码更新与进入/退出状态文字采用不同更新方式。统一文字控件应支持这些差异，不能把所有变化强制成同一种换字动画。[S19]、[S20]

### 7.3 形状样式栏：内容场景已独立，但范围大于工具栏

ShapeEditorSurface 同时负责：

- 样式按钮、颜色、线形、线宽及参数面板。
- 根据形状能力决定按钮可见性，处理折行、锚定与避让。
- 对象外框、控制点、移动/旋转等编辑手势。
- 形状快照与身份、候选及已呈现状态。
- 自有 renderer、Draw、Present 及输入提交。

它已经复用标准按钮与扩展内容，但不能直接视为“已经完全宿主无关的样式栏组件”。SelectionOverlay 是其原生窗口适配层；ShapeEditorSurface 自己的 Present 仍包含 GDI/ULW 操作。[S18]、[S27]、[S28]

未来可复用的内容需要与对象编辑事务保持边界：旧对象的迟到输入不能作用于新对象，按钮按下期间的目标身份不能因一次视觉更新而被错误替换。

### 7.4 底栏文字提示与多步形状栏应分开辨认

本次把用户所说的“底栏文字提示”对应到当前可定位的**吸附/居中反馈提示** bottomDockIndicatorWord / BottomDockIndicator。其文字、显示阶段、宽度、临时外框和绘制还在 RenderLoop 中。[S29]、[S50]

多步形状栏已有独立 BarConstructionPrompt：

- 包含提示文字、进度圆环、退回/前进/完成/取消按钮。
- 根据可用宽度做横排或折行。
- 使用公共按钮、文字和外框绘制。
- 已接受外部 renderer/DC。
- 主栏在整帧成功后调用 CommitPresented。
- 业务层保留构造步骤和形状身份；多步提示出现时会抑制原吸附提示。[S30]

因此，可统一的是基础面板体验，业务触发、互斥、步骤状态与动作仍应各自明确。

属性说明 tooltip 和 BackgroundWarning 是其他内容。BackgroundWarning 属于透明背景/Aero 警告预留，不能据此认定已有完整底栏提示控件或完整异常提示流程；属性 tooltip 则服务标注线/粗细说明。[S36]、[S51]

### 7.5 颜色选择浮窗是明确的公共复用候选

当前没有单独名为 ColorPanel 的模块或窗口。两套连续选择器是：

| 项目 | 主栏选择器 | 形状样式选择器 |
| --- | --- | --- |
| 所在内容 | BarUISet 的 shapeMap/wordMap + RenderLoop | ShapeEditorSurface::Impl 的 pickerPanel / DrawPicker |
| 宿主 | 主栏同一个 HWND | 随 SelectionOverlay |
| 已共享 | RGB/HSV 换算、颜色投影、渐变画刷、基础图元、字体与圆环绘制 | 同左 |
| 分别维护 | 面板尺寸、footer、marker、tone 切换、hold 进度、捕获与手势状态 | 同类内容另有实现 |
| 结果语义 | 拖动时实时更新当前画笔颜色，松手记忆 | 拖动期间保留本地颜色反馈，松手一次提交 SetStyle |

形状侧代码明确避免逐 Move 写入历史；目前 Draw3 没有相应的样式预览事务。这是封装时必须保留的业务差别。[S31]、[S32]、[S44]、[S45]、[S46]、[S47]、[S48]

一个公共颜色选择单元需要覆盖显示颜色、连续选择、亮暗切换、数值/透明度信息、标记、锁色反馈、展开方向和输入生命周期。调用场景再决定预览、提交、取消、记忆，以及写入当前笔色还是对象样式。

白板主题菜单目前是预置背景色与底纹预览，不是同一个连续取色器。它可以是未来消费者，但不应被描述成已经接入完整 picker。[S19]、[S20]

## 8. 第四层：宿主、目标资源和呈现现状

### 8.1 现在已经有哪些基础设施

Window Service 已统一管理 WindowRole、创建/显示/隐藏/边界、窗口代次、owner 与呈现租约；RenderPipeline 提供共享设备 epoch、资源、单帧回调、调度、失效唤醒、设备恢复和诊断。[S24]、[S42]

UI RenderPipeline 初始化选择 WARP，代码也定义 Hardware backend 和准备/切换入口。主栏初始化注册单帧回调，渲染线程由共享管线持有；交互另有 worker。调度器帧率入口当前归一到 30 或 60，而非直接继承 Draw3 的墨迹帧率。[S26]、[S37]

这说明“共享 UI3 能力”和“共享一个帧循环”已有基础；未来白板的独立线程或更高帧率目标仍需明确动画时钟与内容更新的归属。

### 8.2 当前实际呈现路径

| 对象 | 当前路径 | 证据 |
| --- | --- | --- |
| 主栏 | 独立 layered HWND；自有 GDI-compatible 目标 → GetDC → UpdateLayeredWindowIndirect → ReleaseDC → EndDraw | [S05]、[S25] |
| 四个分页实例 | 各自 Ppt* HWND 和 RenderPipeline client，scene 提供内容与资源，PageControl 处理窗口及提交 | [S18]、[S20] |
| 形状编辑/样式栏 | SelectionOverlay layered HWND；ShapeEditorSurface::Present 执行绘制/回读并调用窗口提交 sink | [S27]、[S28] |
| 白板背景 | 仍通过 Freeze 表面，使用 BarPageSurface 的背景能力及 GDI/ULW 提交 | [S38] |

启动代码也将 Bar、四个 Ppt*、SelectionOverlay 与 Drawpad 分开配置。Drawpad 的 DComp 相关路径不表示主栏已经以 DComp 或白板内嵌内容呈现。[S39]

### 8.3 能传入 DC，还没有覆盖整个宿主问题

当前存在几个不同程度的接入点：

- 基础图元 Draw 能接收 DC。
- BarConstructionPrompt::Draw 接收 renderer/DC，已经较接近内容级绘制。
- BarPageSurface::Render 接收 DC，但该对象同时提供资源创建、GDI interop 和呈现快照。
- ShapeEditorSurface 有 Draw，也有包含 GDI/ULW 的 Present。
- 主栏协调器直接围绕 floating_window、窗口位置和提交事务运行。

所以白板接管主栏显示时，待明确的内容包括：

| 边界 | 当前依赖 | 后续需满足的要求 |
| --- | --- | --- |
| 内容身份 | 全局 barUISet 和具体场景状态 | 明确哪些是共享产品状态、哪些是某个可见实例的状态 |
| 绘制目标 | renderer 自有 DC/bitmap/GDI interop | 内容可以由选定宿主正确绘制，资源域匹配 |
| 帧生命周期 | Begin/Clear/Clip/End 与 Present 分布在场景/协调器 | 一帧内谁清空、谁画、谁结束和谁提交有明确责任 |
| 坐标 | 屏幕、窗口客户区、主栏布局、DIP、present outset | 绘制、光源、命中和 dirty 使用一致映射 |
| 原生输入 | HWND 注册、捕获、窗口消息、touch route | 同一窗口内能按内容区域正确路由并处理取消 |
| 遮挡/穿透 | 独立窗、透明区域命中、上层面板背景 | 内嵌时 UI 留白/弹层也能正确挡住画布操作 |
| 调度 | 固定 Client + 共享管线/交互 worker | 动画只推进一次，失效通知能到达实际宿主 |
| 呈现确认 | 成功帧与候选帧分离 | 命中使用对应已提交内容，失败帧不提前发布 |
| 生命周期 | HWND/设备/布局/对象等各自有代次 | 重建、切换、隐藏、最小化时不复用过期输入或资源 |

依据：[S05]、[S13]、[S18]、[S24]、[S25]、[S27]、[S37]。

原生表面注册目前以 HWND 和 generation 为核心。未来同一白板窗口中承载多块 UI 时，需要检查其逻辑目标标识与路由，不能把原来的“一窗一个表面”接线直接当作任意多内容实例的保证。[S13]

### 8.4 必须保留成功呈现与命中的关系

主栏只有在 GetDC、ULW、ReleaseDC、EndDraw 等完整事务成功后，才推进已呈现边界、输入命中和相关快照。BarPageSurface、ShapeEditorSurface 和特定面板同样区分候选与已呈现状态。[S18]、[S25]、[S27]

这里的“成功呈现”是代码确认的提交边界，并不等于测量到屏幕已经发光。换宿主后可以重新定义适配方式，但应保留视觉内容与输入命中使用同一已确认版本这一要求。

## 9. 我们需要做什么：按责任列出工作范围

以下是从现状得出的整理范围，不规定最终实现形式或强制执行顺序。

### 9.1 第一层：明确公共基础能力和资源边界

- 盘点并复用现有图元、动画、文字、图片、SVG、光影、主题和 Metrics。
- 区分公共视觉能力与主 Logo、笔型、常驻主栏等产品策略。
- 明确资源设备归属、缓存失效、DPI/缩放、外部目标与帧生命周期。
- 让公共能力的使用不隐含“必须存在主栏独立窗口”。

### 9.2 第二层：补齐完整控件一致性

- 围绕现有 Retarget / Configure / Advance / Draw / Hit 能力继续统一。
- 梳理标准按钮与旧字段适配器的使用，识别真正重复的普通控件。
- 统一可见、启用、可交互、选中、按下、hover、文字和透明度的含义。
- 明确角标、圆环、框线进度、文字、副文字、自绘内容的组合方式。
- 明确公共点击/取消等语义与右键、双击、长按、业务动作的边界。
- 将主栏注册/配置/preset 等职责与通用控件能力区分。

### 9.3 第三层：形成一致的面板内容边界

- 整理序列、外框、组动画、透明度、层叠、遮挡命中、可视包络和 damage。
- 保留 Main、More、Geometry、分页等各自的产品布局规则。
- 将光源生产、发布订阅和局部坐标消费的责任说清楚。
- 覆盖主栏二级菜单、底栏提示、多步操作、翻页、形状样式栏，而非只抽主栏主按钮列表。
- 将连续颜色选择器作为完整复用单元，保留各场景不同的颜色提交语义。

### 9.4 第四层：补齐独立窗与白板内嵌的接入能力

- 让宿主决定窗口、目标表面、坐标转换、原生输入和呈现方式。
- 保留既有设备代次、捕获取消、失效调度、呈现确认与命中快照。
- 检查 fixed WindowRole/Client、floating_window 和全局 barUISet 的隐含前提。
- 让同一 UI 内容能够被独立窗或白板宿主消费；实际后端、线程与资源组织留待设计。
- 按项目既定 Windows 7 SP1 + KB2670838 兼容约束审查后续选择；本次没有验证任何新后端兼容性。

### 9.5 让统一样式调整真正有效

当前 Theme / ThemeMaterial / Metrics 已有集中来源，但仍有场景内布局常量、手工配色/字号与特殊状态分支。后续需要明确：

- 公共按钮、外框、间距、字号、材质和状态反馈的统一来源。
- 哪些是允许的场景差异，哪些属于无意重复。
- 主题或字号改变时，哪些内容需要重新测量、重建缓存和失效。
- 特殊自绘内容如何消费同一套基础规则。

“所有文件都调用一个主题函数”还不足以保证统一体验；布局、状态、交互与资源更新也需要一致约定。

## 10. 扩展能力应保留到什么程度

现有代码已经用 underlay/content、background.drawContent 等方式保留自绘扩展。未来还需要服务线宽预览、橡皮示例、状态指示、白板底纹、形状参数和控制点等内容。[S08]、[S15]、[S19]、[S28]、[S38]

需要保留的扩展能力至少包括：

| 扩展内容 | 可共用的基础 | 场景仍应保留的能力 |
| --- | --- | --- |
| 线宽/笔型预览 | 按钮外框、hover、按压、文字、材质 | 专有绘制和参数映射 |
| 颜色选择 | 面板、色板、marker、文字、交互反馈 | 预览/提交/取消/记忆与目标写入 |
| 页码和状态 | 主副文字、加载环、按钮状态 | 恢复/失败/翻页语义 |
| 图形参数 | 按钮、滑块、说明、组布局 | 图形能力约束与修改事务 |
| 特殊面板背景 | 外框、透明度、clip、光影 | 网格/点阵/内容预览 |
| 主 Logo/状态指示 | 基础图元、动画、资源缓存 | 专有外观与状态表达 |

扩展代码还应能够说明自己的可视范围、命中行为、动画活跃状态、失效范围与设备资源生命周期，才能在统一系统中持续维护。

## 11. 后续实现时值得保留的行为与验证重点

本节列出已有行为和迁移风险，不代表本次已执行这些验证。

1. **视觉与命中一致。** 候选布局未成功提交时，输入继续对应已确认布局；按压缩放不改变标准布局命中尺寸。
2. **状态含义一致。** 区分 visible、enabled、interactive、selected、hover 和 pressed，不能用一个 opacity 替代所有状态。
3. **动画时钟一致。** 使用同一帧时间推进一次；换宿主或多次绘制时，不重复推进同一动画。
4. **父坐标一致。** 绘制、dirty、角标包络和光照映射遵守同一坐标链。
5. **输入可以可靠结束。** 移出、取消、捕获丢失、接触来源切换、窗口隐藏/销毁及业务拒绝不残留按下状态。
6. **保持设备资源正确。** 设备重建、更换 target、DPI 或尺寸改变时，按资源实际依赖正确复用、更新或失效，避免沿用不匹配的设备资源，也避免无必要的缓存重建。
7. **保留文字行为。** 本地化、长标签适配、字体目标修正，以及数字页码和状态文字不同的更新方式都要继续有效。
8. **保持叠层阻挡。** 浮层背景及留白不能漏点到底层按钮或白板画布。
9. **颜色历史语义正确。** 当前笔色的实时改变与形状的单次样式提交分别验证。
10. **特殊内容仍参与公共机制。** 自绘扩展的光影外扩、clip、damage 和资源释放不脱离整体约定。
11. **跨宿主迁移具有实际证据。** 应验证代表性内容在独立窗和白板内的绘制、输入与主题效果；仅编译通过不足以证明解耦完成。
12. **性能保留按需特性。** 静止面板、局部状态变化和光照变化仍通过既有失效/唤醒机制工作。性能提升及 120 FPS 不能由本次静态封装调查直接保证。

现有仓库已包含多目标、呈现、属性面板及形状场景相关测试/诊断代码。后续应在相关边界变动时评估这些现有资产的覆盖，而不是把本次报告当作它们已通过的证明。

## 12. 给后续实现者的阅读顺序

| 阅读目标 | 入口 |
| --- | --- |
| 看清第一层已有能力 | Bar.UI.cppm、Bar.Animation.cppm、Bar.Rendering.cppm |
| 看清共享按钮范围 | Bar.Main.cppm 的公共按钮入口、Bar.Button.cppm、Bar.Button.cpp |
| 看清统一视觉来源 | Bar.Metrics.cppm、Bar.Theme.cppm、Bar.ThemeMaterial.h |
| 看清列表与面板差别 | ResolveBarButtonListLayout、Bar.RenderLoop 的 Main/More 绘制、Bar.Scene |
| 看清输入与呈现关系 | Bar.InteractionWorker.h、Bar.Interaction.cpp、Bar.RenderLoop 提交段 |
| 看可迁移内容的已有实例 | Bar.ConstructionPrompt.cpp、BarPageSurface、ShapeEditorSurface |
| 看颜色复用缺口 | 主栏颜色相关 maps/State/RenderLoop/Interaction 与 ShapeEditor::DrawPicker |
| 看宿主事实 | PageControl.cpp、SelectionOverlay.cpp、RenderPipeline、Window、IdtMain 的窗口注册 |
| 看白板当前接入 | Whiteboard.cpp 的 backgroundScene 和 Freeze 提交 |

**后续设计的出发点：保留并整理现有公共能力，补齐控件、面板和宿主之间的边界；让同一内容拥有一致体验，并能由不同窗口或目标承载。**

---

## 附录 A：源码定位索引

所有 S 编号链接固定到本次调查提交，避免 dev 后续变化造成证据漂移。表中行号是调查时的定位线索；后续提交应按符号重新检索。

| 编号 | 主要内容 |
| --- | --- |
| [S00] | Bar 模块导出入口 |
| [S01] | 图元、继承坐标、SVG/PNG/文字定义 |
| [S02] | 公共超椭圆路径生成 |
| [S03] | SVG/PNG 资源、缓存与主 Logo 特例 |
| [S04] | 图元绘制、材质与光影组合 |
| [S05] | renderer 服务注入、资源创建和帧生命周期 |
| [S06] | DWrite、格式缓存、本地化与字号策略 |
| [S07] | 动画值/时间线及笔型预览 helper |
| [S08] | 公共按钮视觉、动画、布局和命中接口 |
| [S09] | 公共控件尺寸与视觉布局 |
| [S10] | 按钮统一绘制及扩展包络处理 |
| [S11] | 公共按钮中残留的全局主题/测量依赖 |
| [S12] | 按钮模型、角标、列表和主栏注册模型 |
| [S13] | 输入邮箱、基础按钮状态和 HWND 表面注册 |
| [S14] | 主栏/More 组合绘制与手工关闭按钮 |
| [S15] | 图形属性公共按钮及专有预览/长按 |
| [S16] | 圆角框线进度环公共实现 |
| [S17] | Main/More 手势、业务分支和颜色交互 |
| [S18] | 分页和形状场景接口 |
| [S19] | 分页场景、固定 More/Theme 面板及内容绘制 |
| [S20] | 四个分页位置、工作区模式与动作声明 |
| [S21] | 光照快照与主栏光源策略 |
| [S22] | 主栏每帧发布共享光照 |
| [S23] | 语义色、全局主题状态和重绘副作用 |
| [S24] | RenderPipeline 设备与客户端定义 |
| [S25] | 主栏 ULW 提交与成功快照 |
| [S26] | 主栏初始化和共享渲染注册 |
| [S27] | SelectionOverlay 的窗口、输入和呈现接入 |
| [S28] | 形状样式栏、picker 和 Present |
| [S29] | 底栏吸附/居中指示绘制 |
| [S30] | 多步形状提示/操作面板 |
| [S31] | 主栏颜色选择布局、绘制和交互 |
| [S32] | 色彩数学与颜色位置投影 |
| [S33] | 基础图元脏区包络 |
| [S34] | 背景绘制包装和列表布局实现 |
| [S35] | 其他表面的光标通知仍发往主栏窗口 |
| [S36] | 背景警告和属性 tooltip 初始化 |
| [S37] | 管线初始化、设备后端与调度帧率 |
| [S38] | 白板背景 scene 和 Freeze 呈现 |
| [S39] | 启动时的独立窗口配置 |
| [S40]、[S41] | 格式缓存、本地化与字号策略 |
| [S42] | 窗口服务、窗口角色及代次接口 |
| [S43] | 四个分页窗口的运行时注册 |
| [S44]、[S45] | 主栏取色交互和选色状态 |
| [S46] | 形状取色器松手单次提交样式 |
| [S47]、[S48] | 渐变色板、滑块 thumb 及普通圆环绘制 |
| [S49] | 公共主题材质参数 |
| [S50] | 底栏吸附/居中文案与显示目标 |
| [S51] | 背景警告绘制及 TODO |
| [S52]、[S53] | 笔型按钮的公共适配和专用扩展入口 |
| [S54] | renderer 服务注入和构造接口 |
| [S55] | 基础绘制裁剪与 bounds cull |
| [S56]、[S57] | 光照订阅、变化唤醒和形状编辑器消费 |
| [S58]、[S59] | 公共光影/圆环与 slider thumb 接口 |

## 附录 B：外部平台依据

- [D01] Microsoft Learn — Resources Overview：设备相关/无关资源及共享范围。
- [D02] Microsoft Learn — D1121 Wrong Resource Domain：设备上下文与画刷、位图等资源的设备域约束。

外部文档仅用于解释设备资源边界；关于 Inkeys 当前实现的判断以本次固定提交源码为依据。

[S00]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.cppm#L1-L7
[S01]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.UI.cppm#L130-L459
[S02]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Superellipse.Geometry.h#L1-L80
[S03]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.UI.cpp#L470-L803
[S04]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp#L2816-L3096
[S05]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp#L169-L492
[S06]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp#L3276-L3464
[S07]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Animation.cppm#L139-L1100
[S08]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Main.cppm#L268-L424
[S09]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Metrics.cppm#L5-L278
[S10]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Button.cpp#L2258-L2413
[S11]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Button.cpp#L2077-L2474
[S12]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Button.cppm#L17-L348
[S13]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.InteractionWorker.h#L78-L234
[S14]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp#L10953-L11055
[S15]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.GeometryAttribute.cpp#L651-L944
[S16]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.ButtonFrameRing.h#L1-L73
[S17]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp#L5033-L6485
[S18]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Scene.cppm#L24-L230
[S19]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp#L121-L849
[S20]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/PageControl/PageControl.cppm#L27-L1114
[S21]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp#L559-L820
[S22]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp#L7385-L7437
[S23]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Theme.cppm#L17-L187
[S24]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm#L35-L80
[S25]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp#L12114-L12539
[S26]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp#L158-L295
[S27]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/SelectionOverlay/SelectionOverlay.cpp#L79-L305
[S28]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.ShapeEditor.cpp#L583-L2438
[S29]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp#L11918-L12002
[S30]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.ConstructionPrompt.cpp#L61-L306
[S31]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp#L11579-L11666
[S32]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Layout.cppm#L454-L536
[S33]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.RenderingAttribute.cppm#L14-L120
[S34]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Button.cpp#L2408-L2803
[S35]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp#L10142-L10170
[S36]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp#L338-L1005
[S37]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp#L609-L1269
[S38]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp#L192-L635
[S39]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/IdtMain.cpp#L3789-L3931
[D01]: https://learn.microsoft.com/en-us/windows/win32/direct2d/resources-and-resource-domains
[D02]: https://learn.microsoft.com/en-us/windows/win32/direct2d/d1121


[S40]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Format.cppm#L9-L120
[S41]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp#L230-L309
[S42]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/Window/Window.cppm#L24-L381
[S43]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/PageControl/PageControl.cpp#L6184-L6235
[S44]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp#L5033-L5227
[S45]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.State.cppm#L83-L96
[S46]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.ShapeEditor.cpp#L2423-L2438
[S47]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp#L3467-L3537
[S48]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp#L1448-L1492
[S49]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.ThemeMaterial.h#L1-L351
[S50]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp#L6878-L6930
[S51]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp#L9633-L9650
[S52]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp#L9818-L9887
[S53]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp#L3594-L3635

[S54]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Rendering.cppm#L56-L108
[S55]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp#L426-L527
[S56]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp#L915-L940
[S57]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.ShapeEditor.cpp#L1750-L1770
[S58]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Rendering.cppm#L349-L423
[S59]: https://github.com/Alan-CRL/Inkeys/blob/c06697b1ab2ba01800edf677b4fb46d4d0e2de61/Inkeys/Inkeys/UI/Bar/Bar.Rendering.cppm#L556-L567
