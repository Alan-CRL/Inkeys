# C5 设计：共享主栏内容编排、沙盒交互与组件页体验修正

> 实现者：Claude（每阶段 gpt-6.2-sol High 只读审查）。行号以 HEAD `89617eee` 为准，开工时重新核对。
> 设计已吸收 Sol 方案审查（`%TEMP%\inkeys-comp\impl\c5-plan.report.md`）的 2×P0、4×P1 意见。

## 0. 总原则

- 不实例化第二个 `BarUISetClass`（全局单例，含窗口、停靠、属性面板，`Bar.Main.cppm:1631`）。共享的是“主栏内容”这一层：投影、批次、目标写入、推进、提示门禁。
- 真实主栏在**原调用位置、原顺序**调用抽出的函数，值与求值顺序不变；绘制属性、主按钮、停靠、换边、有限观察器、脏区登记表等仍留在 RenderLoop，读取抽出函数返回的批次结果。
- 新代码放在 Bar 模块内部（扩充 `:ContentInstance` 分区或新增同级分区 `:MainContent`），只依赖 UI3 原语；不新增 UI3 公共接口。

## 1. 共享层（L-A / L-B）

### 1.1 显示投影（纯函数）
- 从 `BarButtonSetClass::ApplyMainPresentationLocked`（`Bar.Button.cpp:967` 起）抽出每项的 `preset/baseSize + 输入 → {size, hide, state}`：
  `MainBarPresentationInputs{ stateModeSelect, whiteboard, presentation, hasContent, featureEnabled, separate, freezeAvailable, freezeActive, canvasDebugEnabled, canvasDebugVisual, settingVisible, fold, moreOpen, compactSize, recall(RecallChromeView) }`。
- 副作用留在实栏原处：`moreExpanded` 清零、Recall 可用性刷新与缓存、`recallAttribute` 写回、图标文案过渡时机。
- Recall/Redo 的 `commandEnabled` 只影响灰态（实栏仍允许点击延期），与预览编辑命中资格分开。
- 按钮内容样式：`UpdateDraw/Eraser/GeometryButtonStyle`（`Bar.Button.cpp:1200–1285`）及选择/白板样式，抽出“输入 → (图标资源, 文案, 角标)”的纯解析；实栏保留 styleKey 缓存与锁，预览按沙盒输入调用同一解析后 `TransitionContent`。

### 1.2 布局批次
- 抽出 `ResolveMainBarBatch(MainBarBatchState&, const MainBarBatchInput&) -> MainBarBatch`（`Bar.RenderLoop.cpp:3659–3747`）：重开/加入/后半程重开判定、批次曲线、`operationDur`、相位、`syncedValueCurve/syncedPctCurve`、`forceRestart`、换边关键帧标志。`MainBarBatchState` 持有 `mainBarTimeline`、`mainBarBatchCurve`、上次布局快照/宽度/图标顺序。
- `SyncValueDuration/SyncPctDuration` 改为显式接收批次参数的函数，不再捕获之后会被绘制属性改写的 `operationDur`/曲线（Sol P1-3）。
- 时间轴推进点不变：按钮推进（约 6033–6074）与帧末时间轴推进（7821）两个点分别保留；停靠自动居中与属性面板仍在其间/其后读取时间轴。

### 1.3 目标写入（三个独立函数，实栏在原位置分别调用）
- `ApplyMainBarButtonTargets(list, placements, batch, ctx)`（3752–3914）：位置（含镜像、折叠锚、换边关键帧）、分割线样式、Geometry 选中大图标、`__color` 分支、按压倍率（press/release 曲线）、各子值 `Sync*`、换边透明度关键帧。字号测量显式传宿主 renderer（不读 `barUISet.spec`）。
- 隐藏锚定：`AnchorHiddenBarButton`（`Bar.Main.cppm:306`）改为接收有序列表、隐藏实例、首选锚点与目标写入回调，沿用“首选 → 前驱 → 后继”规则。隐藏实例保留在列表与快照中，绘制时退场项先画，命中与插入缝只用可见项。
- `ApplyMainBarBodyTargets(body, BodyTargetInput)`（4024–4085）：输入含总宽、折叠、主按钮宽、主按钮间隙、方向、换边关键帧；预览使用“居中锚点”变体（`x=0`，无主按钮、无折叠、无换边）。
- More：`progress/opacity` 的 `SetTar`（5479–5517）与 `PositionContentMore`；预览改为与实栏相同的“先定位后推进”顺序，并推进 X 的 SVG（`AdvanceSvgVisual`，修复 X 不可见）。

### 1.4 推进与提示
- 按钮推进共用 `Bar::AdvanceButtonVisual` 及同一组上下文语义（`visible = 非折叠 && 可见 && (非 More 项 || More 展开)`，`hoverAllowed = 非禁用 && 非选中`）；角标在停靠拖动时锁存的规则作为参数传入。
- 小尺寸悬停提示：共享文案解析、500 ms 延时、新移动解禁、布局/面板动画期间禁止与定位策略（7903–7990）；门禁与实栏一致——只在小尺寸开启。

## 2. 预览宿主（L-C）

- `BarContentInstance` 改为：沙盒状态 + 从编辑模型构建的按钮列表（结构与实栏 `buttonList` 相同：A1、交界线、B1、More、交界线、A2；More 面板列表）+ 共享层调用 + 相机 + 编辑叠加。删除自写的批次、曲线、缩放推导。
- 沙盒默认值：`IdtPen` + 软笔、无内容、非白板/放映、`settingVisible=true`、`freezeAvailable` 取当前实机值或默认可用（实现时核对 `ResolveBarA2Projection` 输入）。
- 相机与预览区（R3）：
  - 缩放只由**目标**包络决定：`min(高度适配, 可用宽 / max(主栏目标宽, More 面板目标宽) + 阴影)`；目标变化时以 EaseOutCubic 过渡（不带 Back），绝不读取 `progress.val` 或动画中的 `body.w.val`。
  - 纵向：上沿紧贴标题；主栏上方预留 = 一行 More 面板高度 × Back 峰值（`UiGetCurveExtrema(EaseOutBack).maximum`）+ 锚点间隙 + 阴影；More 行数 ≥2 时每多一行，预览区向下扩展一行（主栏随之下移，过渡同上，仅在编辑结果改变行数时发生）。主栏下方间隙约一行，内放垃圾桶胶囊。
  - 宿主纹理裁框使用稳定参考（预览区矩形），裁框变化不得移动主栏与命中。
- 编辑叠加不变（抓起时从布局移除、插入缝、幽灵按钮），但插入缝与命中只取可见项。

## 3. 设置侧输入适配（L-D，Sol P1-5）

- 单一指针会话：`Setting.ComponentEditor` 的 `DragSession` 仍是唯一的捕获/阈值/取消所有者。
- 按下：记录命中的已呈现实例身份与布局 revision，立即 `SetButtonPressedVisual(true)`（与实栏一致），停止悬停。
- 越过拖动阈值：先清除按压视觉，再 `Lift`（可拖动项）；固定 A 区项照常可在区内拖动。
- 抬起：仅在未进入拖动、抬起仍命中同一实例且 revision 未变时派发沙盒动作；失焦、设备换代、布局替换、来源混用共用取消路径。
- 沙盒动作：工具模式切换；More 切换；X 用与实栏相同的 `ButtonPointerState` 按压语义；More 面板项点击后按 `closeMoreAfterAction` 收起；面板与 More 之外按下（含右键）收起 More。
- 右键：本任务只做“收起 More”，不做隐藏菜单（后续任务）。

## 4. 组件页编辑体验

### 4.1 垃圾桶胶囊（R4）
- 位置：主栏下方间隙水平居中；尺寸约 高 36 DIP、内边距 16 DIP、图标 + 文字（新文案 `SettingsUI.Component.TrashLabel`，如“拖到这里删除”）。
- 拖动可删除项或组件库项时淡入；悬停时底色变危险色、文字与图标反白；非拖动时隐藏。用 ImGui/Design 层绘制，透明度随页面动画。

### 4.2 放下语义（R5）
| 来源 | 位置 | 提示 | 松手 |
| --- | --- | --- | --- |
| 可删除项 | 合法插入缝/More | 插入/收纳 | 放下 |
| 可删除项 | 垃圾桶，或预览区之外 | 会被删除 | 删除 |
| 可删除项 | 预览区内非法位置 | 不能放在这里 | 回位 |
| 固定项（A1/A2 默认、设置） | 原区内合法缝 | 插入 | 放下 |
| 固定项 | 其他任何位置（含垃圾桶） | 只能在原区域内调整 / 不可删除 | 回位 |
| 组件库项 | 合法插入缝/More | 插入/收纳 | 添加 |
| 组件库项 | 其他任何位置（含垃圾桶） | 松手取消添加 | 放弃 |

- `ResolveDrop` 增加“预览区之外”输入；“预览区”= 整个预览块矩形（含上下预留），垃圾桶优先。

### 4.3 分割线拖入（R5）
- 先写离屏/无窗口测试复现“卡片拖入 A1/B1/A2 失败”，再定位（候选：目录 `entity` 标志、`Make(UserDivider)` 原型、幽灵/插入缝对 1 DIP 宽项的处理、卡片拖动源）。不得为此放宽模型规则（More 仍禁止）。

### 4.4 幽灵按钮配色（R6）
- 幽灵改为“按钮放在主栏材质上”：先画一块与主栏同材质的圆角底板（Surface 填充、边框、阴影，尺寸 = 按钮 + 一圈间隙），再以完整不透明度画按钮视觉；选中态青色底在主栏材质上呈现，与实栏观感一致。危险态整体降低透明度并叠危险色描边。

### 4.5 合成后透明度与页面动画（R7）
- 预览纹理以 `ImDrawList::AddImage` 合成时传 tint `IM_COL32(a,a,a,a)`（预乘纹理，四通道同乘），`a` 取页面动画给该块的透明度；位移随该块的上浮偏移。
- 幽灵与跟随提示只在拖动中存在，离开页面会取消拖动，不参与淡出。
- 不逐部件改透明度。

## 5. 测试

- 真实主栏：
  - golden 35/35 对 `comp-base` 逐字节。
  - **动画轨迹基线**（新增，抽取前在 HEAD 录制）：脚本化场景逐帧记录主栏 `x/w/pct`、按钮 `x/y/w/pct/pressScale`、More `progress/opacity`；场景含展开、收起、前半程加入、后半程重开、换边中重排、More 正反向、折叠/展开。抽取后逐帧逐值一致。
  - 几何、橡皮、第二宿主、属性脏区、headless。
- 预览（`Bar.Host.Test.cpp` 替换旧的“More 中途缩放下移”断言）：回弹峰值且相机不变；底板边缘与末端按钮同相位；X 像素存在；选择收拢与命中只含可见项；按压与拖动交接；小尺寸才有提示。
- 组件页：R5 放下表各行、分割线拖入三段、垃圾桶位置；页面动画中预览 tint 与块透明度一致。

## 6. 风险

| 风险 | 缓解 |
| --- | --- |
| 抽取改变求值顺序导致实栏细微差异 | 原位调用 + golden + 动画轨迹基线双门禁；分步抽取、每步回归 |
| 有限观察器/脏区登记按原对象地址与批次编号 | 实栏保留观察器递增点与登记身份，不把登记移入共享层 |
| `SyncValueDuration` 捕获被改写的变量 | 改为显式参数（1.2） |
| 预览缩放仍受 More 峰值影响 | 预留峰值空间，相机只读目标值 |
| 点击与拖放对同一抬起重复生效 | 单一会话 + 实例身份/revision 锁定（§3） |
