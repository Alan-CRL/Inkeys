# Design — 场景进入自动居中底栏与位置设置页

## 1. 边界

- **设置层**（`UI/Setting`）：新增 `GeneralSection::Position`（Basic 之后）、`RenderGeneral` 的位置页渲染、草稿/事件、配置写回；`Setting.Shell.cpp:162` 子项键/图标数组与 `static_assert` 同步。
- **配置层**（`Other.Config.cppm` / `.cpp`）：新增持久化键，读取为原子 int，渲染线程在场景边沿读取一次快照。
- **策略层**（`Bar.BottomDock.h` 纯函数）：把“进入前状态 + 勾选掩码 + 方向 → 是否接管、目标方向”做成可单测纯函数；不依赖窗口。
- **渲染层**（`Bar.RenderLoop.cpp` Enter 分支 + `:2840-2940` 放置分支）：命中时把 `bottomDockMode/CenterMode/fold/mainBar` 置为居中底栏并走既有 `pptDockPlacementPending` 放置动画。
- **启动层**（`UI/StartupPlacement.h`、`Bar.Initialization.cpp`、`Bar.RenderLoop.cpp:2856-2900/13722`、`StartupPreview`）：扩展枚举与落点解析，新增居中顶栏、悬浮窗底栏居中。

## 2. 配置与数据契约

沿用 `UI.Bar` 组，不上传：

| 键 | 类型/默认 | 含义 |
|---|---|---|
| `UI.Bar.StartupPlacement` | int，默认 0 | 0 居中底栏右（旧值保持）、1 悬浮左下、2 悬浮右下（旧值保持）；新增 3 居中底栏左、4 居中顶栏右、5 居中顶栏左、6 悬浮底栏居中 |
| `UI.Bar.Workspace.Ppt.AutoDockMask` | int，默认 1 | bit0 底栏、bit1 底栏折叠、bit2 悬浮、bit3 悬浮折叠 |
| `UI.Bar.Workspace.Ppt.Direction` | int，默认 0 | 0 不改变、1 向右展开、2 向左展开 |
| `UI.Bar.Workspace.Whiteboard.*` | 同上 | 白板场景 |

类型/位置两个下拉是同一个 int 的展示拆分：`type = {0,3→底栏; 4,5→顶栏; 1,2,6→悬浮}`，`position` 由类型决定。UI 层集中放一个双向映射函数（有单测），避免散落魔数。未知/越界值经 `NormalizeStartupPlacement` 回落 0；掩码/方向同样 Normalize。

## 3. 进入策略（渲染线程）

1. 仅 `transition == Enter`（Desktop→PPT/白板）评估；`Switch`（PPT↔白板）、`Leave` 不变。
2. 在已有的“捕获桌面基线”之后分类进入前状态：`BottomDocked && !fold`→bit0，`BottomDocked && fold`→bit1，`Floating && !fold`→bit2，`Floating && fold`→bit3（取 `owner_.bottomDockMode` 与 `state.barState.fold`；居中底栏属于 bit0/bit1）。
3. 掩码命中（目标场景的配置）→ 接管：`BeginBottomDockTransition`；`bottomDockMode=BottomDocked`、`bottomDockCenterMode=Centered`、相位 Stable、弹性偏移清零；`barState.fold=false`；方向非“不改变”时改 `widgetPosition.mainBar`/`mainBarLayoutSide`；`UpdateSceneMonitor` 指向目标屏；`pptDockPlacementPending=true`。弹簧状态清零沿用现有代码。
4. 未命中 → 保持现有分支（`ShouldPlaceBarWorkspaceDock` 仍只处理展开底栏贴底，保留 Free/Centered）。
5. 放置分支（`:2902`）：命中接管时 `centerMode==Centered`，已有逻辑用目标屏中心计算 X，无需新增；现有 `if(!pptDockPlacementPending||whiteboardDockPlacementPending) fold=floatingStartup` 保证不被覆写，需核对折叠命中时 fold 保持 false。
6. 白板最小化的 Centered 切换逻辑 (`:13631-13650`) 保持，其“保存/还原 centerMode”作用于接管后的值。
7. Leave 还原不改，但需验证：接管前是悬浮/折叠 → 回到悬浮/折叠基线（`desktopDockMode/CenterMode/Fold/Side` 已在 Enter 前快照，快照必须在接管之前，现有顺序满足）。

策略判定放入 `Bar.BottomDock.h`：`ResolveBarWorkspaceEntryTakeover(mask, mode, fold) -> bool`，`ResolveBarWorkspaceEntrySide(direction, currentSide) -> bool`，便于 headless 测试。

## 4. 启动落点

- `StartupPlacement.h` 扩展枚举；“居中顶栏”落点：`top = work.top`（工作区含任务栏占位，天然贴任务栏），`left` 水平居中；Floating 模式，不置 `fold`。“悬浮窗底栏居中”：水平居中，`bottom = work.bottom - margin`，Floating。
- `opensRight`：枚举 0、4、6 默认向右；3、5、2 向左；1 向右（现状）；6 向右（待实现时确认默认，写在 implement 验证点）。
- 顶栏需保证窗口 viewport 裁剪逻辑（`ClipBarViewportForTaskbarEdge` 只处理底边）与面板朝向（面板默认向上展开，顶部不足时需翻向下，复用现有 `below` 翻转策略）在贴顶时正常——列为风险与验证点。
- `StartupPreview` 同步新选项预览（使用同一解析函数，避免两套）。

## 5. 设置页 UI

- 位置页三个分类（SectionHeader）：桌面场景（两个 `ComboRow`，位置选项随类型联动，类型变化时位置重置为该类型第一项并保存）、PPT 场景、白板场景。
- PPT/白板卡片复用 `RenderPptIdleFadeRow` 的 `InlineRowContent`+`Checkbox` 模式，新增通用函数 `RenderWorkspaceDockRow(...)`，内联区含四个复选框与“主按钮方向”`InlineCombo`；无条件开关，故内联区常显（不做折叠动画）。
- 事件只在真实用户变更时回写配置并 `QueueConfigWrite()`；运行中的渲染线程在下次 Enter 读取新值，不要求即时作用于已在场景内的主栏。

## 6. 兼容与回滚

- 旧配置：缺失新键用默认；`StartupPlacement` 的 0/1/2 不变，升级无迁移。
- 回滚：仅需移除新增 UI 与 Enter 分支的命中接管，未命中分支即现状；配置键遗留无害。
- 契约变更：`rendering-and-ui.md:993`、`whiteboard.md:28`、`native-session-ui3.md:52`、`ppt-idle-fade.md` 无关，改前者三处。

## 7. 取舍

- 选择“同一 int 展示拆分”而非两个独立配置，保证旧值兼容并避免非法组合。
- 命中后复用既有 `pptDockPlacementPending` 放置与 0.4s 动画，不新建动画通道；代价是必须核对与 `whiteboardDockAnimationActive` 的所有权交互。
