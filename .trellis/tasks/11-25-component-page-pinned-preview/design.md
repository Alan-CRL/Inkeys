# C6 设计：固定预览区与卡片长按起拖

需求见 `prd.md`。现行合同见 `.trellis/spec/native-desktop/settings-component-editor.md` §3.2–§3.3。

## 1. 现状（2026-11-25 读码）

- 设置正文结构：`##setting-content`（`Setting.cpp:2864` `BeginScrollChild`，页面滚动宿主、`BeginPageMotion` 的 owner）→ `##setting-page-content`（AutoResizeY，居中限宽）→ 各页 switch。组件页在 `Setting.cpp:4280` 起：`PageHeader` → `ComponentEditor::Draw` → 快捷方式行 → 重置行 → “添加组件” → `TabStrip` → 卡片/条目。
- 拖动滚动：`BeginDragScrolling`（帧首）/`ApplyDragScrolling`（帧末）。空白按下由 `ApplyDragScrolling` 取得 `##scrolldraggingoverlay`；内容型按钮调用 `AllowContentDrag()` 登记后，越过 ImGui 原生拖动阈值时由 `BeginDragScrolling` 夺取为滚动。宿主按 `DragEligible`（`ScrollMax.y > 0`）从悬停窗口向上找。
- 卡片起拖：`Design::DragSourceOverlay`（`Setting.Controls.cpp:2528`）`InvisibleButton` 激活后越过 4/10 DIP 即返回 true → `BeginLibraryDrag` 夺走 ActiveID。没有调用 `AllowContentDrag`，所以触摸在卡片上拖只拖组件。
- 边缘自动滚动：`ComponentEditor::Draw` 拖动分支（`Setting.ComponentEditor.cpp` “正文内层 child 自动增高…”一段）与 `scrollPosition` 字段；`scroll` 参数另用于 `in.height = max(80, scroll->InnerClipRect.height)`。

## 2. 固定区：组件页内层滚动 child

不改设置外壳，只在组件页内部分层：

```
##setting-content（外层，组件页时内容恰好等于视口 → 不滚动）
└ ##setting-page-content
   ├ PageHeader("组件")                  ┐ 固定区
   ├ ComponentEditor::Draw（预览块+垃圾桶）┘
   └ BeginScrollChild("##component-library", {pageWidth, 剩余高度})   ← 新滚动视口
        快捷方式行 / 重置行 / 添加组件 / TabStrip / 卡片
```

- 剩余高度 = 外层 `InnerClipRect.Max.y` − 当前光标 y − 外层底部 padding。每帧按当帧预览 extent 计算，因此 More 多行展开时固定区随相机过渡增高、内层视口同步变矮（R1.4）。
- 预览高度上限：`in.height = max(80 DIP, 0.5 × 外层 InnerClipRect 高度)`（取代原“整个视口高”）。
- 外层不滚动：组件页提交时让外层内容高度不超过视口（内层高度按上式取整并留 1 px 余量）；若外层 `stableBodyLayout` 的一帧滞后测高仍让 `ScrollMax.y > 0`，组件页对外层强制 `Scroll.y = 0`（只在组件页生效）。实现时先验证是否需要这条兜底。
- 拖动滚动：内层 child `ScrollMax.y > 0`，`DragEligible` 自然选中内层；固定区内悬停窗口向上找不到可滚宿主，不滚动。滚轮在固定区上不滚动（接受）。
- 页面动效：`PageBlockScope` 用当前窗口做块、用 owner 的 `ClipRect` 判可见，块写在嵌套 child 内是现有页面的常态（`##setting-page-content` 本身就是嵌套 child）。内层滚动 child 的块沿用同机制；需实测入场位移与退场淡出、离屏组合成是否覆盖内层 child（`CommitOpacityGroup` 处理 `firstChild_` 之后的 child）。
- 内层滚动条透明度与外层相同；内层滚动位置与其他页面的外层一样按窗口保留，不在进入页面时额外重置。

## 3. 组件库 Tab 与“全部”

- `tabs` 改为：全部、内置组件（复用 `component.BuiltinSection` 文案，若用户可见名需不同再新增 `TabBuiltin` 键）、桌面操控、IslandCaller、SecRandom、NamePicker、ClassIsland。
- `entries` 的 `group` 与 Tab 下标对齐：分割线 `group = 1`，其余 +1（桌面操控 2 … ClassIsland 6）。ClassIsland 提示框条件改为 `componentLibraryTab == 6`。
- “全部”：去掉分组循环与小标题，按 `entries` 顺序一次排成一个网格；网格尺寸规则不变。
- 专属 Tab 分支原样复用，分割线行因 `actionGlyph` 为空不显示“+”。

## 4. 长按起拖（触摸/笔）

扩展 `Design::DragSourceOverlay`，新增参数 `bool holdForTouch`（默认 false，保持现有调用语义）：

- 鼠标：不变，越过 4 DIP 返回 true。
- 触摸/笔且 `holdForTouch`：
  1. `InvisibleButton` 激活后立即 `AllowContentDrag()` 登记为内容型按钮：手指位移越过 ImGui 原生拖动阈值时，帧首 `BeginDragScrolling` 把活动项夺给内层滚动宿主——即“没抓起就滚动”，并包含已发生的位移（现有逻辑从捕获时 `Scroll.y` 开始累计后续 delta；首段位移是否补偿需实测，若明显“吃掉”一段再决定是否补偿）。
  2. 仍为本按钮活动、`io.MouseDownDuration[0] >= HoldSeconds` 且 `io.MouseDragMaxDistanceSqr[0] < 阈值²` → 返回 true，起拖（`BeginLibraryDrag` 改设 ActiveID 为预览捕获，滚动不再夺取）。
  3. 等待期间请求下一帧渲染：设置页按需渲染，手指静止时没有输入事件，需要在按住期间持续或按截止时间唤醒（复用 `ComponentEditor` 的 `Pipeline::Request(Settings)`，经新导出的轻量函数或在调用点请求）。
- 常量：`DragSourceTouchHoldSeconds = 0.45`；容差直接使用 ImGui 原生拖动阈值 `io.MouseDragThreshold`（默认 6 物理像素，与滚动夺取同一阈值，避免“既没起拖也没滚动”的死区）。实机若觉得过严，再评估统一调整滚动夺取阈值。
- 实现中发现：同一按钮先被触摸按住会留下 `##content-drag-button` 登记，之后鼠标拖动在 ≥1.5 倍缩放下会被页面滚动抢先（Pixels(4) ≥ 6 px）。鼠标/非长按路径激活时清除该登记。
- 两处调用（“全部”卡片 `ComponentCard` 内部、专属 Tab 行）都传 `holdForTouch = true`；`ComponentCard` 增加同名参数透传。
- 抓起反馈：沿用现有幽灵按钮（`BeginLibraryDrag` 后当帧绘制在指针处），卡片本身不新增视觉。

## 5. 删除边缘自动滚动

- 删除 `ComponentEditor::Draw` 拖动分支中的边缘滚动块与 `Editor::scrollPosition` 字段及其复位。
- `Draw` 的 `scroll` 参数保留，用于预览高度上限（§2）。

## 6. 影响面与兼容

- 只改：`Setting.cpp` 组件页分支、`Setting.ComponentEditor.cpp/.h`、`Setting.Controls.cpp/.h`（`DragSourceOverlay`/`ComponentCard` 参数）、i18n（如新增键）、`setting_design_tests.cpp`、spec。
- 预览实例、`MainContent`、实栏不动 → golden / trace 门禁不受影响（仍跑一次确认）。
- 风险：外层一帧测高滞后导致轻微可滚；页面动效覆盖内层 child；按住期间唤醒频率（只在按住卡片时请求，松手即停）。

## 7. 回滚

纯 UI 布局与手势改动，回滚即还原上述文件；无配置格式变化。
