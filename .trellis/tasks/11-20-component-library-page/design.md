# C4 设计：组件页快捷块、重置与组件库

> 实现者：Claude（2026-11-20 用户调整分工后由主 Agent 直接实现）。需求见本目录 `prd.md` 与父任务 `prd.md` R3–R5。依赖 C1 的布局模型与应用入口；卡片拖入依赖 C3 的拖放会话。

## 1. 页面结构（`Setting.cpp` `settingTabEnum::tabComponent`）

```
PageHeader("组件")
[C3] 主栏预览块（Setting.ComponentEditor 提供，C3 实现）
NavigationRow  快捷方式：主栏尺寸与排列 → navigateTo({PageId::Window, Section::WindowLayout})
ButtonRow      恢复默认布局：两段式（“重置” → 3 秒内“确认重置”）
SectionHeader  添加组件
TabStrip       全部 | 桌面操控 | IslandCaller | SecRandom | NamePicker | ClassIsland（默认“全部”）
内容区         “全部”：分小节矩形卡片网格；其余：整行条目（ClassIsland 页顶部保留 URL 协议提示框）
```

- 原 16 个 `ToggleRow` 与旧小节标题删除；`ExplorerTitle` 等标题与依赖说明键保留复用。
- Tab 选中项、重置待确认状态保存在设置会话的局部状态中（与 `settingLinkageTab` 同级的会话变量），不持久化。离开组件页时重置待确认状态。

## 2. 组件目录（单一来源）

在 `Setting.cpp` 组件页附近定义只读目录数组，每项：`componentId`（`Inkeys::BarComponentId::*` 或 `BarButtonId::UserDivider`）、分类（Builtin / Desktop / IslandCaller / SecRandom / NamePicker / ClassIsland）、图标 glyph、标题键、精简说明键（“全部”卡片）、完整说明键（专属条目）、可选前置条件键、是否提供“+”。

| 分类 | 条目 |
| --- | --- |
| 内置（仅出现在“全部”首小节） | 分割线（`UserDivider`，无“+”） |
| 桌面操控 | 文件资源管理器、任务管理器、控制面板、显示桌面、锁屏、ESC、Alt+F4 |
| IslandCaller | IslandCaller 1 随机点名、IslandCaller 2 单人点名 |
| SecRandom | SecRandom 1 闪抽、SecRandom 2 闪抽、SecRandom 2 闪抽（兼容） |
| NamePicker | NamePicker 随机点名 |
| ClassIsland | 应用设置、档案编辑、快速换课 |

## 3. 操作键语义

- 布局快照：每帧 `barUISet.barButtonSet.ReadBarLayout()`（未就绪时操作键禁用）。
- 存在性：组件 → B1 或 More 中有该 ID（含隐藏）；分割线 → A1/B1/A2 中有 `UserDivider`。
- 组件无实例：显示“+”，点击 `AutoAdd`；有实例：显示垃圾桶，点击 `RemoveAllOf`。分割线：无“+”，有实例时显示垃圾桶，点击 `RemoveUserDividers`。
- 提交：`ApplyBarLayout(expected, next)`；`Applied` → `QueueConfigWrite()`；`Stale` → 重新读取快照，本次点击丢弃。
- 重置：第二次点击 `ApplyBarLayout(current, Reset(), true)`，`Applied` → `QueueConfigWrite()`。

## 4. 新控件（`Setting.Controls.h/.cpp`，`Design` 命名空间）

- `bool TabStrip(const char* id, const std::vector<std::string>& labels, int& selected)`：一行文字标签（Body 字号），未选中为次要文字色，悬停有淡底；选中项文字主色并在下方画 3 DIP 圆角强调色短下划线（与图一“组件库”一致）。超宽时横向滚动交给外层（标签很短，按换行处理：放不下时换到下一行）。返回是否切换。
- `ComponentCardResult ComponentCard(const char* id, const ComponentCardSpec& spec, const ImVec2& size)`：矩形卡片，`DrawCardSurface` 底板；左侧 32 DIP glyph，右侧标题（Body）+ 精简说明（Caption，最多两行，超出省略）；右上角 28 DIP 图标按钮（“+” 或垃圾桶，可无）。返回 `{ actionClicked }`。卡片网格由调用方计算：卡片最小宽 260 DIP、高 96 DIP、间距 8 DIP（2026-11-20 多语言测试发现 220×76 放不下长标题后调整；常量见 `Design::ComponentCard*`），列数 = max(1, floor((可用宽 + 间距) / (最小宽 + 间距)))，宽度均分。
- 专属 Tab 条目：复用 `SettingRow`，action 区为 `IconButton`（“+” / 垃圾桶），说明用完整说明，前置条件放 `note`。
- C3 接入后，卡片与条目的非操作键区域作为拖动源（按下后超过阈值即交给 C3 会话，浮起按钮图标）；该接线在 C3 完成后由本任务补上（见 implement.md 步骤 4）。

## 5. 文案（zh-CN 新增，其余语言 `pwsh Scripts/i18n.ps1 sync` 后补译）

`SettingsUI.Component` 下新增：`ShortcutTitle`、`ShortcutDescription`、`ResetTitle`、`ResetDescription`、`ResetButton`、`ResetConfirm`、`AddSection`、`TabAll`、`TabDesktop`、`BuiltinSection`、`DividerTitle`、`DividerShort`、`DividerDescription`、`AddTooltip`、`RemoveTooltip`、各组件 `*Short`（精简）与 `*Description`（完整）。IslandCaller / SecRandom / NamePicker / ClassIsland 为产品名，Tab 直接使用名称字面量。删除不再使用的 `SoftwareSection`、`SystemSection`、`KeyboardSection`、`RollCallSection`、`ClassIslandSection` 前先全仓库搜索确认无其他引用。

## 6. 测试

- `setting_design_tests.cpp` 中旧组件 `ToggleRow` 多语言排版断言（约 7921–7974）改为新控件：卡片网格列数随宽度变化、条目操作键可点击、Tab 切换。导航用例不受影响（侧栏结构不变）。
- 操作语义由 C1 已有的模型单测覆盖；页面层只验证调用接线（必要时用源码断言或无窗口点击）。
