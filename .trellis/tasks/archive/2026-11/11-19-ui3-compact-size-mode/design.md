# 设计：UI3 小尺寸界面模式

> 依据 `prd.md`（R1-R10、D1-D8、P1-P13）与 `research/*.md`。行号基于 `refactoring/drawpad-ui` @ `2f2adf9b`。

## 1. 总体原则

1. **单一尺寸来源**：新增 `BarSizeMode { Large, Compact }` 与 `BarSizeMetrics`，所有布局、绘制、命中、吸附、启动预览只从这份 metrics 取尺寸。
2. **大尺寸零变化**：`ResolveBarSizeMetrics(Large)` 直接返回现有 `inline constexpr` 常量，现有常量与 `static_assert` 保留；大尺寸路径的计算顺序与浮点表达式不变，保证 golden 逐字节一致。
3. **预设不动、投影映射**：按钮 `baseSize`、配置、注册默认值不改；只在统一投影点把有效尺寸映射为 1×1。
4. **热切换走主题式发布**：设置线程只发布原子目标，渲染线程在帧首把模式落地为本帧 metrics 快照；同一帧内不混用两套尺寸。
5. **复用而非平行分支**：翻页小尺寸复用标准列表布局，删除旧硬编码 compact 分支。

## 2. 尺寸模型

位置：`Bar.Metrics.cppm`（已被 StartupPreview、Config、PageControl 引用，且无副作用）。

```cpp
enum class BarSizeMode : std::uint8_t { Large = 0, Compact = 1 };

struct BarSizeMetrics
{
    double gap;                 // 网格间距：Large 5 / Compact 1.25
    double one;                 // 1×1 边长：32.5 / 32.5
    double two;                 // 2 格跨度：70 / 66.25
    double barHeight;           // 主栏/翻页短边：80 / 35
    double barCornerRadius;     // 8 / 5.25（P2）
    double mainButtonSide;      // 80 / 35
    double mainButtonGap;       // 主按钮↔主栏：10 / 5（P1）
    double attachedGap;         // 属性窗/提示/步骤栏↔主栏：10 / 5（P1）
    double moreAnchorGap;       // More：12（相对 More 按钮）/ 5（相对主栏边）
    double dividerHeight;       // 主栏分割线：50 / 22（P3）
    double paginationDragLane;  // 10 / 5（P12）
    double dockVisualLimit;     // 竖向果冻上限：24 / 10（P8）
};
constexpr BarSizeMetrics ResolveBarSizeMetrics(BarSizeMode) noexcept;
constexpr ListLayoutMetrics ToListLayoutMetrics(const BarSizeMetrics&) noexcept; // {gap, one, two}
```

- 两套各自 `static_assert`（Large 与现有常量逐项相等；Compact：`one*2+gap==two`、`one+gap*2==barHeight`）。
- `NormalizeBarSizeMode(int)`：非 0/1 → Large。
- 预留"2×1 副本"：`ButtonSizeEnum` 将来可加 `twoOneShared`，投影时 Compact 保留为 twoOne；本任务不加枚举值（R2.3）。

## 3. 配置、设置与发布链

### 3.1 配置

- `Other.Config.cppm:591` 的 `Experimental.Inkeys3.UI3` 组新增 `X(..., IdtAtomic<int>, SizeMode, 0)`（新起一行，不重排原三项）。
- 新增 `Experimental.Inkeys3.UI3.StartupPreview.CachedStartupBarWidthDipCompact`（默认 = 小尺寸默认展开包络，见 §10）。
- 读取钩子仿 `UseTaskbarHeightAsScaleBase`（`Other.Config.cpp:669-680`）做整数范围校验。

### 3.2 设置页

- `GeneralDraft` 追加带默认值的 `int sizeMode = 0;`（放在已有默认字段区，避开位置聚合初始化的前置字段，`Setting.cpp:3333`、`setting_design_tests.cpp:4293`）；`GeneralEvents` 追加 `bool sizeModeChanged = false;`。
- `RenderGeneralAppearance`（`Setting.Pages.cpp:1429`）"主题"分区、主栏主题下方新增 `ComboRow("##界面尺寸", …)`。
- `Setting.cpp`：会话缓存（仿 `:2340`）、每帧装载（仿 `:3359`）、Appearance 回写（仿 `:3489-3499`）：写 config → `Bar::SetSizeMode()` → `QueueConfigWrite()`。
- i18n：`SettingsUI/Regular/Appearance/UiSize/{N,E,Large,Small}`，zh-CN 改基准后 `pwsh Scripts/i18n.ps1 sync`，补全 19 种翻译，`check` 通过。

### 3.3 发布与落地

- `Bar.State.cppm` `BarStyleClass` 增 `std::atomic<int> requestedSizeMode`；`Bar.Main.cppm` 导出 `SetSizeMode(int) noexcept`，实现仿 `SetThemeMode`（`Bar.Main.cpp:498`）：`exchange` + `UpdateRendering(false)`。
- 启动：`IdtMain.cpp:3344-3360` 归一后、Bar 初始化前调用 `SetSizeMode`；`Bar.Initialization.cpp` 读取 requested 值构造首帧几何（替换 `:336-337, 358, 370, 377-379` 的 80）。
- 渲染线程帧首（`Bar.RenderLoop.cpp:2549` 附近，主题应用旁）：
  - 若 `requested != applied` 且 **无活跃主栏拖动**（`bottomDockDragActive` / 直拖会话）→ 落地：`state.sizeMode`、`state.sizeMetrics` 更新；关闭附属面板（绘制/橡皮/撤回/形状/More、橡皮灵敏度菜单、状态指示器 Fan 归一为 Docked）；重置橡皮/撤回 `dragLayoutInput_/stableLayoutInput_` 锁存；`ForceFullDamage` + `RequireVisualRetry`；`RenderPipeline::Request(PptPageMask())`；`PageControl::NotifyLayoutChanged()`。
  - 拖动中则保持 pending，松手后的第一帧再落地（D3）。
- 本帧所有布局/绘制/命中快照只读 `state.sizeMetrics`；输入线程读呈现快照中的尺寸，不读原子。

## 4. 按钮映射与列表布局

### 4.1 投影映射

`Bar.Button.cpp:1087`：`if (compact && size != oneTwo) size = oneOne;` 写入 `button->size`。`MainLayoutSnapshot()`（`:996`）读 `size`，切换时自然触发主栏布局批次动画；RenderLoop `:3677` 与 More `:5512` 的视觉种类、`:3697/:5537` 的隐藏文字自动生效。

### 4.2 `ResolveBarButtonListLayout`

签名增加 `const BarSizeMetrics&`（旧调用委托 Large），实现 `Bar.Button.cpp:2027-2120`：

- `spec.height = metrics.barHeight`（`:2033`）。
- 尺寸→跨度映射使用 `metrics.one/two`（`:2043-2051`）。
- **Main**：Large 保持 `GroupedGrid + PairedRows`；Compact 改 `SingleRow`，分割线走 SingleRow 竖线分支，高 `metrics.dividerHeight`、占宽 `1 + gap`。`ListLayoutSpec` 增可选 `rowCenterY`（默认空 = 原行为），Compact 传 `barHeight/2`。起步与间距由 `ListLayoutMetrics{1.25, 32.5, 66.25}` 提供。
- **GeometryGrid**：Compact 为 1×1 格、`FixedGrid` 格边 `one`、列上限 12（P4）。
- **GroupedGrid（More）**：Compact 列数 `min(10, ceil(sqrt(n)*1.5))`、`endRowAlignment=1`（P5）。
- `UI3.ListLayout` 为公共组件：只通过 `ListLayoutMetrics` 与新可选字段注入，不改默认语义。

## 5. 主栏与主按钮

- `Bar.RenderLoop.cpp:933` `mainButtonBaseSize` → 运行时 `mainButtonBaseWidth/Height`：`metrics.mainButtonSide`（宽度另加状态指示器需求，§8）。Logo 基准（`:935-936`）按 `side/80` 同比。
- 新增 `state.mainBodyHeight`（UiValue，随 `mainBarTimeline` 同批次动画，不受点击脉冲影响），作为吸附中心、竖向果冻基准、容量的唯一主体高度来源。
- `mainBar->h.SetTar(metrics.barHeight, …)` 与 `w` 同批次（`:3897-3934`）；圆角取 `metrics.barCornerRadius`。
- 替换硬编码（清单见 research）：折叠锚点 `(40,40)` → `(mainButtonW/2, barH/2)`；折叠/换边宽 `80` → 主按钮宽；`10.0` → `metrics.mainButtonGap`；容量 `max(80,…)` → 主按钮宽；展开方向 `expansionInput`（`:2802-2811`）读运行时值；`Bar.Initialization.cpp:325-326`、`Bar.Zoom.cppm:98-99` 默认位置按 metrics。
- Large 下上述替换必须与原数值逐项等价（同一表达式顺序），由 golden 守门。

## 6. 附属属性窗与面板

- 间隙：`attachedGap` 取代绘制属性硬编码 10（`:4020/4022`）、橡皮/撤回借用的 `BarMainButtonToMainBarGapDip`（`EraserAttributeLayout.cppm:141`、`RecallAttributeLayout.cppm:82`）、形状局部 `gap=10`（`GeometryAttribute.cpp:452`）。Large 值仍为 10，数值不变。
- **橡皮**：`EraserAttributeLayoutInput` 增 `metrics`；`button = one`（Compact）/`two`（Large）；自动按钮 `button + 20`；主分割线高 `metrics.dividerHeight`；按钮内间距 `gap`；视觉 `large` 按钮在 Compact 用 `StandardOneOne`（`EraserAttribute.cpp:304, 323-329`）。左侧预览直径不变，复用 `previewClips` 与面板求交裁剪（`:165-173`）。灵敏度菜单不变。
- **撤回 / 重做**（P7 修订）：Compact 下 `RecallChromeView` 投影为撤回与重做均常驻主栏（1×1）、`popupOpen` 恒为 false，不可用时灰色视觉（保持可点击延期语义）；撤回属性面板在 Compact 不打开。
- **橡皮（修订）**：Compact 下不生成 Clear 项（`items[0]` 隐藏、不参与布局与命中），布局为“左侧尺寸预览组 | 分割线 | 自动粗细（带三角）+ 手掌橡皮”。
- **形状**：`GeometryAttribute.cpp:234` 不改预设；网格经 §4.2 Compact 分支；`:767` 视觉种类按 Compact 取 `StandardOneOne` 并隐藏文字；`:249` 图标 28→20；滚动步长 `:1196` 用 `one + gap`。
- **More**：Compact 定位改为相对主栏边缘 `attachedGap`（`RenderLoop.cpp:5392-5395`）；视觉种类 Compact 强制 `StandardOneOne`（`:5509-5512`）。

## 7. 底栏提示、步骤栏、吸附与居中

- `ResolveBarBottomDockIndicatorGeometry`（`Bar.BottomDock.cppm:296-335`）增 `separatedGapDip` 参数：Large 传空保持 `top = visibleTop - 15`；Compact `top = visibleTop - attachedGap - 30`。RenderLoop 两处调用（`:10474, :10495`）同传，dirty/峰值预留/命中/viewport 自动一致。
- `Bar.ConstructionPrompt.cpp:188-191`：Compact 下 `above = anchorTop - attachedGap - h`，不足则 `anchorBottom + attachedGap`；Large 保持 embed 15。
- 直拖/释放冻结容量（`RenderLoop.cpp:10304-10316`）需覆盖分离后的更高位置：Compact 下预约包络始终包含提示峰值位置。
- 吸附：所有 `ResolveBarBottomDockCenterScreenY(..., mainButtonBaseSize, ...)` 调用（`RenderLoop.cpp:2316, 2391, 2455, 2522, 3833`；`Interaction.cpp:9377, 9607, 9740`）改读 `mainBodyHeight.val`；`Interaction.cpp:9335` 的 `80.0` 改读呈现快照主体高度（`BottomDockPresentedSnapshot`）。
- 竖向果冻：基准（`:7279-7282, 7373, 7547`）读 `mainBodyHeight`；视觉上限取 `metrics.dockVisualLimit`；捕获带 20、水平居中 40 不变（P8）。
- 热切换连续性：吸附态因每帧 `SetDirect(dockLine - bodyH/2)` 读动画值，底边始终贴线；悬浮态主按钮中心不动（P9）；居中态根 X 由联合外框每帧反推。

## 8. 状态指示器

- 模型不变。`StatusIndicatorLayoutInput` 增 `compact` 与小尺寸样式参数（DockSize 12、DockIcon 8、DockInset、HitMin 18、FlightSink 等，P11）。
- **宽度需求的帧内顺序**：主按钮目标在 `RenderLoop.cpp:2679` 提交，早于 `statusIndicator.Advance`（`:7899`）。方案：命令 drain 提前到帧首；新增纯查询 `ResolveCompactMainTextDemand(model, now)` 返回队首 `{key, text, accent, textWidth, presence}`，在主按钮目标提交前读取；Advance 位置不变。大尺寸不调用该查询，路径不变。
- **变宽方向**：远离主栏展开方向（折叠时沿用最近展开方向）。主按钮几何改为"靠主栏侧边缘固定、外侧边缘外扩"：主按钮中心 x 偏移 `±extra/2`，主栏 `targetX` 以主按钮靠主栏侧边缘计算，不移动。居中态由联合外框反推根 X，整体保持居中（R7.2）。
- **内容**：Logo 改为锚定主按钮外侧端（Compact 专用），文字区位于 Logo 与主栏侧边缘之间，复用胶囊的 Word + 渐隐裁剪与滚动（`StatusIndicator.cpp:507-530`、`Model.cppm:247-252`），文字上限 123 DIP。进度环 `DrawRing` 按主按钮实际 w/h 自动跟随；核实 `GetGeometryDiffuseMask` 缓存键需含宽高。
- **收起**：主按钮宽度收回 + 文字淡出，缩小版方块从文字区飞向右上角停靠位（FlightSink 按 Compact 参数）。停靠方块位于主按钮右上角内侧，不进入 overflow（窗口合同不变）。
- **点击**：主按钮点击路径先询问指示器：若 Compact 且队首处于变宽展示 → `Expire`（R7.4）。单方块点击 → 重新变宽（flight/morph 目标回 0，复用"就地重新展开"）；多方块 → Fan，`StatusIndicatorFanLayerRadius` 用 35×35 主按钮实际参数（R7.5）。
- 模式切换时 Expanded/Fan/selected 归一为 Docked（类似 `SetOptions` 的 `SnapDocked`）。

## 9. 翻页控件

- `ResolveBarPptStandardPaginationGeometry`（`Bar.Metrics.cppm:95-128`）与 `ResolvePptWidgetContracts`（`PageControl.cppm:363-422`）参数化为 `(vertical, right, metrics)`，删除 `BarPptCompactPaginationEnabled`、`BarPptCompact*`、`PptCompact*Dip` 常量与 `PageControl.cppm:381-421`、`Bar.Scene.cpp:593-603` 旧分支。
- `Bar.Scene.cpp:562-631` `Configure`：Compact 下 `pageButtons` 尺寸映射为箭头/More/加页 `oneOne`、页码 `twoOne`，`ResolveBarButtonListLayout(SingleRow, metrics)`；竖向沿用转置得到 1×2；DragHandle lane `metrics.paginationDragLane`。白板与 PPT 同路径（D6）。
- 视觉种类：Compact 页码横 `PageHorizontal`（含白板底部）、竖 `PageVertical`；箭头 `StandardOneOne` 清空标签；`UpdateHorizontalPageWidths` 条件改为"Compact 且横向"。
- 状态文字：`AssignLoadState`（`PageControl.cpp:1090-1122`）Compact 下不替换文字，只保留加载环/边框（D7）；`PageNumber` 上限 Compact 横竖均 999（R8.4）。
- PageControl 侧：`PptControl*SideDip` 等 constexpr 改为按模式求值的函数；PageControl 读取发布的尺寸模式快照，并把模式纳入 `GroupLayoutBudgetCache`/帧身份，`NotifyLayoutChanged` 后重新布局并重新夹取已保存位置（R8.6）。Bottom HWND 在 PPT/白板间的形变两端都用同一模式几何。
- 白板 More 菜单、主题面板内容不变（R8.5），仅锚点随按钮位置变化。

## 10. 启动预览

- Mini 读取列表（`IdtMain.cpp:3016-3025`）加入 `SizeMode` 与 `CachedStartupBarWidthDipCompact`；`StartOptions` 增 `sizeMode`。
- `StartupPreview.State.cppm:29`、`StartupPreview.cpp:851-858, 889-893, 1105-1107` 的 80 改为按模式（35）。
- 缓存宽度：按模式各一字段；`CachedStartupBarWidthMinimumDip` 按模式取主按钮宽；`NotifyBarFirstCommittedFrame` 附带首帧模式，主线程写对应字段（`IdtMain.cpp:4199-4211`）。变宽中的主按钮宽度不写入缓存（取 base 宽）。
- Compact 默认展开包络 = `35 + 5 + 默认 compact 主栏宽`，由 constexpr 推导并 `static_assert`。

## 11. 悬停提示（Compact 专属）

- **目标发布（输入线程）**：在主栏悬停分支（`Bar.Interaction.cpp:5379-5383`）及 More、橡皮、撤回、形状面板的悬停命中处，发布 `{targetId, 文字来源, 光标 DIP 坐标, 进入时间}`；门禁：Compact、真实鼠标（`!IsBarTouchPointerMessage && source==Mouse`，且非笔提升消息）、非按下/拖动/折叠。清除点与现有 `StopMainBarButtonHover` 调用点一致（`:5046, :5130, :5214, :6018, :6214, :8265-8301`）。
- **显示（渲染线程）**：按时间戳判定 500 ms；按钮间切换时若已显示则直接换字。位置 = 光标 + (12, −12 − h)（右上方），先在显示器工作区内、再在 Bar 视口内夹取，右侧放不下翻到左侧、上方放不下翻到下方。文字取 `name.content.GetTar()`，`\n` 转空格；颜色块（`__color` 前缀）与分割线排除。
- **绘制**：Bar HWND 内，新视觉登记项（范式 `RenderLoop.cpp:1589-1610`），Rigid 映射、主栏材质/主题、12 DIP 文字；PanelStack 不登记输入。Compact 下在纵向容量中常驻预留提示带，避免首次显示触发容量扩张。
- 按钮布局批次动画进行中、面板开合动画中隐藏。

## 12. 兼容性与回滚

- `SizeMode` 默认 0：未修改设置的用户行为完全不变；旧配置无该键时取默认。
- 删除旧 compact 分支属于不可单独回滚的清理，与 §9 同一子任务提交，便于整体回退。
- 每个子任务独立提交；回滚某子任务后其余仍可编译（Compact 路径缺失时视觉退化但大尺寸不受影响）。

## 13. 测试策略

- 大尺寸回归：`--ui3-golden-dump` 35 场景逐字节一致；现有 headless 与内嵌自测全部通过（旧断言不改）。
- 新增 headless：尺寸模式 schema 默认/归一/往返；设置页 `##界面尺寸` 行归属与 `noEdits`；`ResolveBarSizeMetrics` 两套断言；`ResolveBarButtonListLayout` Compact（Main 单行、分割线、Geometry 12 列、More 列数）；底栏吸附中心与果冻上限 Compact；状态指示器 Compact 布局（变宽方向、停靠、Fan 半径）；翻页 Compact 几何（命中合同 = Scene）；启动预览 Compact 尺寸与缓存。
- 内嵌自测：橡皮离屏测试增 Compact 场景（35 高、预览裁剪、命中不外扩）；形状测试增步骤栏分离用例；`PageControl.cpp:4072-4086` 改为按模式断言。
- 运行验证：GUI 实机由用户执行（不擅自启动 GUI）。

## 14. 需同步的 spec

`rendering-and-ui.md`（尺寸、吸附、提示条、状态指示器、翻页、tooltip）、`eraser-attributes.md`、`shapes.md`（步骤栏分离）、`startup-preview.md`、`configuration-i18n-and-assets.md`（新字段）、`ppt-interop/native-session-ui3.md`（翻页尺寸）、`bar-theme-material.md`（如圆角/材质有变）。
