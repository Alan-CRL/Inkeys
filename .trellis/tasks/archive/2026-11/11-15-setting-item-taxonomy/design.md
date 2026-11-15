# design.md — 设置项分类树重构

> 范围：仅 UI 层的页面归属、页内顺序、导航结构。配置键、默认值、业务回调、保存时机一律不变（Minimal Diff）。

## 1. 现状约束（已核实）

| 位置 | 现状 | 对本任务的影响 |
| --- | --- | --- |
| `Setting.Pages.h` `PageId` | 数值保留旧路由：Home0 Language1 Build2 Configuration3 General4 Draw5 Plugins8 Components9 HotKeys10 Version11 Support12 Debug13 Experimental14 Feedback15 Performance16 Linkage17 | 新页面取 18+；`Configuration`/`HotKeys` 保留编号仅做重定向，不复用 |
| `Setting.Design.h` `GeneralSection` | 8 个枚举值，同时是 `NavigationState` 数组尺寸、`NavigationDestination.section`、页面路由键 | 只有「通用」是带子页的分组；需泛化 |
| `Setting.Shell.cpp` 约 146–420、557 | 「通用」分组的展开/收起、`groupReveal` 动画、`childBounds`、紧凑模式 flyout 全部硬编码；`childKeys/childGlyphs` 静态数组 | 新增三个分组（绘制/窗口与显示器/日志与配置）必须改成表驱动 |
| `Setting.Design.h` `NavigationMiddleItemCount = 8` + `middleBounds{8}` + 高度计算 | 中部侧栏项数 | 改为 11，并由导航表推导 |
| `Setting.cpp` ~2920–3010 | 路由键 `(settingTab<<32)\|subpage`；`subpage` 按页分别来自 `destination.section` / `drawingSubpage` / 插件/联动 tab | 子页统一后路由键口径不变，仅来源统一 |
| `DrawingSubpage {Overview, SpeedEraser, PalmEraser, Cursor}` | 绘制页内三个详情页，靠 `NavigateDrawingSubpage` 切换并保留滚动 | 变成「交互与手势」下的详情层，侧栏高亮仍停在「交互与手势」 |
| `Setting.cpp` tab3 (3770–4126) | 绘制页单函数顺序渲染所有区块 | 按区块拆到两个子页，业务代码块整体搬运，不改内部逻辑 |
| `SettingOpenTarget::{SpeedEraser,SoftwareVersion}`、`HomeAction::{Appearance,Shortcuts,General…}` | 外部/主页直达入口 | 目标需重定向到新位置 |

## 2. 导航模型

### 2.1 表驱动导航树

新增单一数据源（建议 `Setting.NavigationTree.h`，constexpr 表），Shell、`NormalizeDestination`、测试都从它读取，禁止再各写一份 `childKeys/childGlyphs`：

```
Group { PageId page; key(i18n); glyph; children[] }   // children 为空 = 普通单页
Child { Section id; key(i18n); glyph }
```

顶层顺序（中部 11 项）：General、Draw、Window、Keys、LogsConfig、Performance、Linkage、Components、Plugins、Experimental、Version。

| 分组页 | 子页（顺序） |
| --- | --- |
| General | Basic、Appearance、StatusIndicator、DesktopShortcuts |
| Draw | Effects（绘制效果）、Interaction（交互与手势） |
| Window | Window（窗口）、Behavior（行为）、Display（显示器，空页） |
| LogsConfig | Logs（日志，空页）、Configuration（配置） |
| Keys / Performance / Linkage / Components / Plugins / Experimental / Version | 无子页 |

### 2.2 目的地与路由

- `NavigationDestination { PageId page; int section; }`：`section` 按页解释，各页用自己的具名枚举（`GeneralSection` 收缩为 4 值；新增 `DrawSection`、`WindowSection`、`LogsSection`）。路由键口径 `(page<<32)|subpage` 不变，`subpage = section`（Draw 页再叠加详情层，见 2.4）。
- 新 `PageId`：`Window = 18`、`Keys = 19`、`LogsConfig = 20`。
- `NormalizeDestination` 重定向表（保持旧入口可用）：

| 旧目的地 | 新目的地 |
| --- | --- |
| `Configuration`(3) / `General.Storage` | `LogsConfig.Configuration` |
| `HotKeys`(10) / `General.Shortcuts` | `Keys` |
| `General.Position` | `Window.Window` |
| `General.Interaction` | `Window.Behavior` |
| 旧 `Draw`(5) 无子页 | `Draw.Interaction`（或记住上次子页） |

- 重定向仅在 `NormalizeDestination` 一处实现并由测试覆盖；`DestinationForPage(int)` 沿用。

### 2.3 分组展开状态

- `generalExpanded/lastGeneralSection/groupReveal/childBounds/flyoutChildBounds/scrollSelectedGeneral` 泛化为按分组索引的数组（分组数 4）：每组各自 `expanded`、`reveal` tween、`lastSection`、子项 bounds。
- 展开策略：**允许多组同时展开**（与现有「通用」一致，不引入手风琴切换动画）；选中某子页所属分组自动展开并 `scrollSelected`。
- 紧凑（折叠侧栏）模式：点击分组图标打开该组 flyout，沿用现有 `flyoutReveal`，flyout 内容取自导航表；`flyoutChildBounds` 按组存。
- 侧栏总高度/滚动计算（`Setting.Design.h` 328/340 行）改由导航表推导：中部固定项 + 各组展开时子项高度。
- 选中指示条（indicator）逻辑不变，仅选中项来源改为表。

### 2.4 绘制页：子页 + 详情层

- `Draw.Effects`：单层页。
- `Draw.Interaction`：列表层 + 详情层 `DrawingSubpage {Overview→List, SpeedEraser, PalmEraser, Cursor}`；`NavigateDrawingSubpage`、返回按钮、`SettingOpenTarget::SpeedEraser` 都落到 `Draw.Interaction` + 对应详情；侧栏高亮不变。
- 路由键：`subpage = section * 16 + detail`，保证两个子页及各详情页滚动状态互相独立。
- 离开 `Draw.Interaction` 时详情层复位为列表层（与现行「离开绘制页」一致）。

### 2.5 空页面

`Window.Display` 与 `LogsConfig.Logs` 只提交 `PageHeader(title)`，无 SectionHeader/行。需验证：页面动画、滚动 child、Home 开屏缓存、`BeginScrollChild` 在零内容时不异常（高度由 `AutoResizeY` 推导）。

## 3. 逐页最终内容与顺序

所有行/卡片的实现代码**原样搬运**（含 `QueueConfigWrite`、`SyncDraw3State`、`ConditionalBlock` 等），仅改变所在函数/页面和提交顺序。

### 通用
- **基础**：①选择与穿透（含蓝色说明 Notice）②交互模式 `##draw-device`（原绘制页「绘图环境」，业务块整体搬运；含 `SyncDraw3State/WriteSetting/GetDrawingScale` 刷新逻辑）③SectionHeader「启动」：开机自启、允许软件重复启动 ④崩溃处理方式（原「安全与恢复」）。
- **外观**：①主题（主栏主题、设置主题）②动画（启用动画、启动加载动画+效果、设置开屏动画）③边缘光影（+动态，条件块）④透明度（主栏背景/内容）。
- **状态指示器**、**桌面快捷方式**：内容不动。

### 绘制
- **绘制效果**：①墨迹预测 ②抬笔笔锋修正 ③激光笔粒子效果（禁用说明行，置底）。注意：原代码三项读写/下发是**一个共用事件**（`multiTouchChanged||penTailCorrectionChanged||predictionChanged` 后统一调用 `SetGeneralDrawingOptions(multi, penTail, mode)`）。拆页后两页都要保持同一调用的完整参数——实现方式：抽出局部 helper 读取三项当前值并统一提交，不得出现某页提交时用陈旧值覆盖另一页的字段。
- **交互与手势**：①多指绘图 → 多指激光笔（依赖多指，紧随其后，保持 `multiTouchEnabled` 条件）→ 断触接续 ②橡皮擦（笔速橡皮、手掌橡皮入口）③光标入口 ④白板拖动惯性（`RenderCanvasPanInertia`）⑤手写笔触觉反馈。
  - 多指绘图仍调用同一 `SetGeneralDrawingOptions`，`SetLaserOptions(...)` 的隐藏语义不变。

### 窗口与显示器
- **窗口**：①缩放（任务栏缩放基准、主栏 UI 缩放，原通用·外观；因最常用置顶）②启动位置 ③PPT 自动停靠 ④白板自动停靠（原 `Position` 子页 `workspaceRow`）⑤主栏布局（图标顺序、展开方向，原通用·基础）。
- **行为**：①主栏自动收起（公共 Notice + 4 行）②置顶间隔 ③右键关闭。
- **显示器**：空页。

### 按键与快捷键
- 单页：全局 → 编辑 → 辅助，内容与顺序不变，页标题改用「按键与快捷键」。

### 日志与配置
- **日志**：空页。
- **配置**：①配置清理 ②画布保存（原 `tabConfiguration` 分支整体搬运）。

### 不动
性能、联动、组件、插件、实验室、版本、调试页、光标/笔速橡皮/手掌橡皮详情页内部。组件在侧栏顺序上调到插件之前（仅调整 `middleEntries` 顺序，`PageId` 不变）。

## 4. i18n

- 新增（`Inkeys/src/i18n/zh-CN.jsonc` 为基准，其余 19 种语言用 `Scripts/i18n.ps1 sync` 补齐、`check` 校验；需更新 `Scripts/i18n.zh-CN.snapshot.jsonc` 与生成的 `IdtI18nKeys.g.h`）：窗口与显示器、窗口、行为、显示器、按键与快捷键、日志与配置、日志、配置、绘制效果、交互与手势、通用·基础中的「启动」分区名（若无现成键）。
- 旧键（`Design.GeneralInteraction`、`Position.N`、`GeneralStorage` 等）若无活动引用按规范删除；有引用的保持。具体规则以 `configuration-i18n-and-assets.md` 为准。
- 子页标题与侧栏项使用同一键，避免标题/侧栏不一致。

## 5. 风险与应对

| 风险 | 应对 |
| --- | --- |
| 导航泛化破坏现有「通用」展开/flyout/键盘导航/指示条动画 | 先只做框架且保持「通用」行为逐帧等价，再接入其他分组；沿用现有连续帧测试观测 `childBounds` |
| 绘制通用三项共用一个下发调用，拆页后互相覆盖 | 见 3「绘制效果」，加测试断言两页分别操作后三项值都正确 |
| 旧入口/记忆页失效（主页卡片、`SettingOpenTarget`、会话内 `lastGeneralSection`） | 重定向表 + 测试逐条断言 |
| 空页触发布局/滚动/开屏缓存边界问题 | 专项测试：空页进入、离开、DPI 与主题切换 |
| 236 处测试引用旧枚举 | 先改枚举/表，再批量修测试；`setting_design_tests` 的子页循环改为遍历导航表 |
| 20 语言文案缺失 | `i18n.ps1 check` 作为阻断项 |

## 6. 回滚点

- 阶段 1（框架）与阶段 2（内容迁移）分别可独立提交；若内容迁移出问题，可回退到「框架就绪、旧内容仍可见」的稳定点。
