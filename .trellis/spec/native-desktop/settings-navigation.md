# 设置导航树与分类

## 1. Scope / Trigger

新增、移动或重命名设置页面/子页，或调整侧栏顺序、旧入口重定向时使用。只约束“设置项属于哪个页面、页内顺序如何、导航如何路由”；配置键、默认值、业务回调与保存时机由各自功能规范负责，迁移页面不得改变它们。

## 2. Signatures

- `Setting.Navigation.h`（`Inkeys::UI::Setting::Design`）：`PageId`、`Section`、`NavigationDestination{page, section}`、`NavigationChild{section, labelKey, glyph}`、`NavigationEntry{page, labelKey, glyph, children}`。
- 表：`NavigationMiddleEntries`（侧栏中部顺序，唯一数据源）、`GeneralChildren/DrawChildren/WindowChildren/LogsConfigChildren`、`NavigationMiddleItemCount`、`NavigationGroupCount`、`NavigationMaxChildren`。
- 查询：`NavigationEntryIndex/GroupIndex/GroupPage/Children/ChildIndex`、`PageForSection`、`DefaultSection`、`NormalizeDestination`、`DestinationForPage`（均 constexpr）。
- `NavigationState::groups[NavigationGroupCount]`（`NavigationGroupState{expanded, lastSection, reveal, parentBounds, chevronBounds, childBounds, flyoutChildBounds, parentItemId}`）、`activeGroup`、`TryGroup/Group/LastSection`。
- `RenderGeneral(GeneralDraft&, NavigationDestination)` 与 `RenderGeneral(GeneralDraft&, Section)`：通用、窗口与显示器、按键与快捷键、日志页共用同一草稿/事件结构；`Setting.Pages.cpp` 内按子页拆成 `RenderGeneralBasic/Appearance`、`RenderWindowLayout/Behavior`、`RenderKeyShortcuts` 等独立函数。
- `InkeysHeadlessTests.exe --no-window`：`CheckNavigationTree`（纯数据）、`CheckGeneralSections`（各子页行归属）、`CheckHierarchicalNavigation`（真实 DrawData 的分组展开/浮层/键盘）。

## 3. Contracts

- 侧栏顺序（中部）：通用、绘制、窗口与显示器、按键与快捷键、日志与配置、性能、联动、组件、插件、实验室、版本。子页：通用=基本/外观与动画/状态指示器/桌面快捷方式；绘制=绘制效果/交互与手势；窗口与显示器=窗口/行为/显示器；日志与配置=日志/配置。
- 分类原则：每个设置项恰在一个页面出现；有逻辑关联的相邻（例：多指绘图后紧跟多指激光笔与断触接续）；最重要、最常用的靠前，低频、高级、诊断靠后（透明度在外观页置底，激光笔粒子在绘制效果页置底）；通用页只放最重要内容（选择与穿透、交互模式、启动与崩溃处理），窗口位置/缩放/收起等归窗口与显示器，快捷键归按键与快捷键。
- 空页（窗口与显示器·显示器、日志与配置·日志）保留导航入口，只渲染页标题；后续功能接入时只在对应 `RenderXxx` 补内容，不改导航表。
- `NormalizeDestination` 是旧入口的唯一重定向点：`PageId::Configuration`→日志与配置·配置；`PageId::HotKeys`→按键与快捷键；有子页的页面收到不属于自己的 section 回落到首个子页；无子页页面 section 恒为 `None`。旧页面编号保留，不复用；新页面 18/19/20。
- 每个带子页的分组各自保存展开状态、动画、命中框与记忆子页；允许多个分组同时展开；同一时刻只有 `activeGroup` 拥有浮层/子项焦点/回焦。紧凑侧栏的 flyout 内容取自活动分组的子页。禁止再为单个页面复制 `childKeys/childGlyphs` 之类的并行列表。
- 页面路由键 `(page<<32)|subpage`：普通页 `subpage=section`；绘制页 `subpage=(section<<4)|DrawingSubpage`（详情层：Overview/SpeedEraser/PalmEraser/Cursor），详情页仍属于“交互与手势”，侧栏高亮不变。
- 通用类页面的事件接线集中在 `Setting.cpp` 的同一分支，用 `destination.section`/`page` 守卫仅作用于当前页的“从草稿回写”语句；事件驱动语句不需要守卫。配置页（日志与配置·配置）仍走独立业务分支。
- 墨迹预测、抬笔笔锋修正、多指绘图共用一次 `SetGeneralDrawingOptions` 下发：每帧都先读取三项配置，只有当前页渲染的控件会改变值，因此绘制效果与交互与手势两页可各自提交而互不覆盖；拆页时不得改为只下发其中一项。
- 交互模式（大屏触控/桌面交互）在通用·基本页，`GeneralDraft::paintDevice` 每帧从 `Inkeys::config.Drawing.PaintDevice` 读取，写回时同步 Draw3 并刷新标尺。
- 新页面/子页名称为 i18n 键 `SettingsUI/Design/{NavWindowDisplay,WindowLayout,WindowBehavior,WindowDisplay,NavKeysShortcuts,NavLogsConfig,LogsLog,LogsConfiguration,DrawEffects,DrawInteraction}`；页标题与侧栏项使用同一键。

## 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 旧 Configuration/HotKeys 路由或记忆页 | 重定向到新页面，不出现空白页 |
| 子页 section 与页面不匹配 | `NormalizeDestination` 回落首个子页 |
| 空页进入/离开/DPI/主题切换 | 只显示页标题，不产生编辑事件、不报错 |
| 拆页后在两个绘制子页分别改多指/笔锋/预测 | 三项值都不被另一页的旧值覆盖 |
| 窄窗/紧凑侧栏点击任一分组 | 首次导航到记忆子页，再次点击开 flyout；flyout 子项数取自该分组 |
| 分组 A 展开时操作分组 B | A 的展开、记忆子页与命中框不变 |
| 新增语言键 | 20 语言 `i18n.ps1 sync/check` 通过，无待译标记 |

## 5. Good / Base / Bad Cases

- Good：新增“显示器”设置只在 `RenderXxx` 内补行与事件，导航表不变。
- Base：现有设置项原样搬运（含 `QueueConfigWrite`、`SyncDraw3State` 与 ConditionalBlock）。
- Bad：在 Shell 中为新页面再写一份子页列表；移动设置项同时改配置键；把页内回写守卫误绑到不会渲染该控件的子页导致状态被陈旧草稿覆盖。

## 6. Tests Required

- `CheckNavigationTree`：表唯一性/顺序/子页数、重定向、跨页 section 回落、各分组记忆子页。
- `CheckGeneralSections`：每个子页与按键页的行归属、空页无编辑、草稿跨页保持。
- `CheckHierarchicalNavigation`：通用分组的完整真实绘制行为，以及绘制/窗口与显示器/日志与配置三组的展开、选中、记忆与互不干扰。
- 完整 `InkeysRepo.sln` `Debug|ARM64` 构建、`InkeysHeadlessTests.exe --no-window`、`Scripts/i18n.ps1 check`；不能把这些称为实际桌面 GUI 验收。

## 7. Wrong vs Correct

Wrong：在 Shell 或页面里硬编码“通用有 8 个子页”。

Correct：新增/调整页面只改 `Setting.Navigation.h` 的表和对应 `RenderXxx`，其余（侧栏、flyout、重定向、测试）从表推导。
