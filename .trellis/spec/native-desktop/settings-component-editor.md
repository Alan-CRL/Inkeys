# 设置组件页：主栏编辑器合同

> 来源任务：`11-20-settings-bar-component-editor`（C1 布局模型见 [configuration-i18n-and-assets.md](configuration-i18n-and-assets.md)「UI3 Bar 分区布局配置合同」；本文覆盖 C2 预览实例、C3 拖放与设置宿主、C4 组件库页面）。

## 1. Scope / Trigger

修改设置 → 组件页、`Inkeys.UI.Bar:ContentInstance`（预览实例与共用绘制函数）、`Inkeys.UI.UI3.DragSession`、`Setting.ComponentEditor.*`、ImGui DX11 预乘回调，或真实主栏 `Bar.RenderLoop.cpp` 中被抽成共用函数的绘制段时适用。

## 2. Signatures

- `Inkeys::UI::Bar::BarContentInstance`：`SetLayout(model, revision)`、`Lift(Position, revision)`、`Place(accepted, originalGap, revision)`、`CancelLift()`、`SetMoreExpanded(bool)`、`Suspend()`、`Advance(PreviewInputs) -> PreviewFrame`、`Draw(FrameTarget&, frame)`、`ConfirmFrame(frameId, succeeded)`、`Geometry()`、`HitTest(point)`、`InsertionGaps(point)`、`DrawGhost(target, source, point, hint, danger)`、`DrawFollowingHint(...)`。
- 共用绘制（真实主栏与预览都调用）：`PositionContentMore`、`DrawContentBody`、`DrawContentMore`、`DrawContentHint`。
- `Inkeys::UI::UI3::DragSession`：`Press / BeginDragging / Move / Release / Cancel`，`Phase / Payload / Point / Origin / PointerId / Kind`；`ReleaseResult { None, Click, Drop }`。
- `Inkeys::UI::Setting::ComponentEditor`：`Draw(ImGuiWindow* scrollWindow, void(*queueWrite)())`、`BeginLibraryDrag(componentId, screenPoint, touch)`、`DragActive()`、纯函数 `ResolveDrop(model, source, catalog, trash, more, gaps) -> DropTarget`。
- ImGui DX11：`ImGui_ImplDX11_PremultipliedBegin / End` 特殊回调值（-18 / -19）。
- 设置控件：`Design::TabStrip`、`Design::ComponentCard(id, spec, size, bool* dragStarted)`、`Design::DragSourceOverlay(id, min, max, skip)`。

## 3. Contracts

### 3.1 真实主栏与预览的边界（C2，方案 B）
- 真实主栏只把本体/按钮、More 姿态与绘制、悬停提示四段内联绘制改为调用共用函数；其余帧推进、脏区、命中发布、宿主生命周期不变。**门禁：golden 逐字节、第二宿主、属性脏区与改动前一致。**
- 预览实例自带按钮克隆（`state = &localState`、`clickFunc` 为空）、布局、`UiValue` 动画时钟、`UIRendering` 与字号帧、`LightingStateMachine` 与指针输入、格式缓存（逐帧淘汰，排版资源/设备换代清理）；不读写 `barUISet` 按钮列表、`BarAtomic`、`floating_window`、shared lighting、真实 dirty tracker 与命中发布。
- 预览使用全局已应用的主栏主题、背景/内容透明度与全局动画开关/速率（与真实主栏一致）；不引入主题快照机制。
- 几何与命中扩展范围只在宿主确认帧成功（`ConfirmFrame(true)`）后发布；悬停与 `HitTest` 共用同一层级/遮挡判定（More 面板与本体重叠时本体优先）。
- `Lift` 只从装箱序列移除并保留原 `Position`；`InsertionGaps` 返回**移除前**的模型缝隙编号，与 C1 `Move`/`Insert` 合同一致；原位放下保留夹在中间的隐藏项顺序。More 开合时本体位置与适配比例随开合进度连续变化。
- 尺度只由宿主给定的目标高度与可用宽度决定（大/小尺寸同高，窄时等比缩小），不受主栏 `configZoom` 与任务栏基准影响；`IconOrder` 2/3 固定方向，其余从左到右。

### 3.2 拖放（C3）
- `DragSession` 不认识主栏与 ImGui；阈值鼠标 4 DIP、触摸/笔 10 DIP（按宿主像素比换算）；只接受当前接触 ID。
- 设置宿主每帧：读 `ReadBarLayout()`（与实例版本不同则取消拖动并 `SetLayout`）→ 组装输入 → `Advance` → 需要时绘制到预览纹理（静止时复用纹理）→ ImGui 预乘合成 → 处理拖放。
- 落点：`ResolveDrop` 依次判定垃圾桶、More 入口（追加到 More 末尾）、按距离排序的缝隙，均以 C1 `CanPlace` 过滤。外部来源丢进垃圾桶为取消添加，不改模型。
- 放下：`Move`/`Insert`/`Remove` → `ApplyBarLayout(expected, next)` → `Applied` 时调用写盘回调（`QueueConfigWrite`，**不在渲染回调中写盘**）→ `Place` → `Cancel` → `SetLayout(ReadBarLayout())`；`Stale` 或不允许 → `CancelLift()`。
- 取消条件：Esc、失焦、离开组件页（ImGui EndFramePost 钩子）、活动项被夺走、**拖动中输入来源变化（鼠标/触摸混用）**、**开始时持有原生捕获而后丢失**、预览绘制失败帧。
- D2D 本地目标 `EndDraw` 返回 `D2DERR_RECREATE_TARGET` 时丢弃本地 context 与两张纹理，下一帧在同一设备代次重建。
- 边缘自动滚动：可视区上下 48 DIP 带内按深入程度线性加速（最高约 1200 DIP/s），浮点累计目标位置；外部滚动偏离 ≥1 像素时以实际位置重新基准。
- 跟随提示：插入/收纳/删除/取消添加按落点；不可放置时默认 A1/A2 按钮显示“只能在原区域内调整位置”，其他来源显示“不能放在这里”；指针既不在预览也不在垃圾桶上时不显示提示。按住固定交界线短暂提示“固定分割线不能移动”。
- 预乘合成：`AddImage` 前后成对插入 `PremultipliedBegin/End`（`ONE, INV_SRC_ALPHA`），结束时恢复默认混合；只在默认混合状态下使用。

### 3.3 组件页（C4）
- 结构：`PageHeader` → 预览块 → 快捷方式行（`NavigationRow`，跳 `PageId::Window / Section::WindowLayout`）→ 恢复默认布局（两段式：首次点击变“确认重置”，3 秒内再点执行 `Reset()` 并清旧开关，离开页面或超时复位）→ “添加组件”分区 → `TabStrip`（全部 / 桌面操控 / IslandCaller / SecRandom / NamePicker / ClassIsland）。
- “全部”：按分类小节的矩形卡片网格（`ComponentCardMinimumWidth` 260 DIP、`ComponentCardHeight` 96 DIP、`ComponentCardGap` 8 DIP）；标题最多两行，精简说明按剩余高度显示一到两行，越界部分裁剪在卡片内（多语言测试直接断言“标题 ≤ 两行且至少留一行说明”，不依赖墨迹检查）；分割线卡片在“内置组件”首小节且没有“+”。专属 Tab：整行条目，完整说明 + 前置条件；ClassIsland Tab 顶部保留 URL 协议提示框。
- 操作键：主栏（含 More）无该组件 → “+”执行 `AutoAdd`（B1 已有 ≥2 个组件进 More）；有 → 垃圾桶执行 `RemoveAllOf`（分割线为 `RemoveUserDividers`，不影响固定交界线）。`Applied` 后 `QueueConfigWrite()`。
- 拖动源：卡片/条目非操作键区域由 `DragSourceOverlay` 覆盖；操作键悬停时不提交覆盖层；越过阈值即 `BeginLibraryDrag`，之后由宿主接管。覆盖层成为活动项，因此触摸在卡片上拖动只拖组件、不滚动页面（滚动可在间隙或靠边缘自动滚动）。

## 4. Validation & Error Matrix

| 情况 | 行为 |
| --- | --- |
| 布局未就绪（`ReadBarLayout()` 为空） | 预览不绘制，操作键禁用 |
| 拖动中外部改了布局 | 取消拖动，按新布局 `SetLayout` |
| `ApplyBarLayout` 返回 `Stale` | 取消抓起，重读布局 |
| 默认 A1/A2 按钮拖到其他区 / 垃圾桶 | 无合法候选，提示“只能在原区域内调整位置”，放下回原位 |
| Setting 拖到垃圾桶 | 垃圾桶显示禁用外观，放下回原位 |
| 外部组件丢进垃圾桶 | 取消添加，模型不变、不写盘 |
| D2D 目标失效 | 本地资源重建；当帧取消手势 |
| 鼠标与触摸混用、捕获丢失 | 取消拖动 |

## 5. Tests Required

- golden 逐字节、`--ui3-host-offscreen-test`（含预览成像、实例隔离、失败帧不发布几何、主题采样、500 ms 提示定点比较、原位保序、More 开合连续）、`--ui3-attribute-dirty-test`、`--bar-eraser-offscreen-test`、`--ui3-geometry-attribute-test`。
- headless：`drag_session_tests`、`ResolveDrop` 规则、预乘合成像素（`setting_home_warp_tests`）、组件库卡片/条目多语言排版（`setting_design_tests`）。
- 实机：鼠标/触摸拖动、跨 child 捕获、边缘滚动手感、Win7 SP1 预览显示。

## 6. Wrong vs Correct

~~~cpp
// Wrong：预览直接借真实主栏的按钮或在设置渲染回调中写盘。
preview.buttons = barUISet.barButtonSet.buttonList.Snapshot();
Inkeys::config.Write();

// Correct：预览用自己的克隆；编辑经 C1 入口发布，写盘交给设置业务队列。
if (buttons.ApplyBarLayout(expected, next) == ApplyResult::Applied) QueueConfigWrite();
~~~

## 7. 已知后续

- 静止组件页因预乘回调不能进入设置页稳定帧去重缓存，收到等价重绘请求时会重新提交一帧（审查 P1-4，记为后续）。
- 真实主栏中的拖拽触发方式（下一轮）；去掉 B1 上限后真实主栏超宽时的处理；A 区多支笔。
