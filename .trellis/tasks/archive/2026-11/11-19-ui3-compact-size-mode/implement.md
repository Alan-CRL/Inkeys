# 执行计划：UI3 小尺寸界面模式

> 依据 `prd.md` 与 `design.md`。本任务规模大（触及 RenderLoop/Interaction/PageControl/Setting/StartupPreview），建议拆成父任务 + 6 个子任务，每个子任务独立实现、检查、提交与归档。子任务的创建在用户批准规划后执行。

## 0. 子任务划分（建议）

| 子任务 | 内容（design 章节） | 依赖 | 对应需求 |
| --- | --- | --- | --- |
| C1 尺寸基础与主栏 | §2 尺寸模型、§3 配置/设置/i18n/发布、§4 投影与列表布局（Main）、§5 主栏/主按钮、§7 吸附/居中/底栏提示与步骤栏分离 | 无 | R1、R2、R3、R5、R6 |
| C2 属性窗与面板 | §6 橡皮/撤回/形状/More/绘制间隙、§4.2 Geometry/GroupedGrid 分支 | C1 | R4 |
| C3 状态指示器 | §8 | C1 | R7 |
| C4 翻页控件 | §9（含删除旧 compact 分支） | C1 | R8 |
| C5 悬停提示 | §11 | C1、C2（属性窗按钮悬停） | R9 |
| C6 启动预览 | §10 | C1 | R10 |

父任务保留源需求、跨子任务验收（AC2、AC4、AC12）与最终集成评审。

按用户偏好：由 Claude 写实现，子代理不并行修改同一工作区；如启用 Codex 仅做审查。

## 1. 执行清单

### C1 尺寸基础与主栏

1. `Bar.Metrics.cppm`：`BarSizeMode`、`BarSizeMetrics`、`ResolveBarSizeMetrics`、`ToListLayoutMetrics`、两套 `static_assert`；保留现有常量。
2. `Other.Config.cppm/.cpp`：`SizeMode` 字段、`NormalizeBarSizeMode`、读取范围校验。
3. `Bar.State.cppm`/`Bar.Main.cppm`/`Bar.Main.cpp`：`requestedSizeMode` 与 `SetSizeMode`。
4. `IdtMain.cpp`：启动时 Bar 初始化前发布。
5. 设置页：`Setting.Pages.h/.cpp` 草稿/事件/ComboRow，`Setting.cpp` 缓存/装载/回写；i18n zh-CN + sync + 19 语言翻译 + check。
6. RenderLoop 帧首落地：pending 机制（拖动延后）、关闭附属面板、整幅重画、通知分页。
7. `Bar.Button.cpp:1087` 投影映射；`ResolveBarButtonListLayout` 增 metrics 参数与 Main Compact 单行分支；`UI3.ListLayout` 增可选 `rowCenterY`。
8. RenderLoop 主按钮/主栏：`mainButtonBaseWidth/Height`、`mainBodyHeight`、`mainBar->h`、圆角、硬编码 80/40/10 替换；`Bar.Initialization.cpp`、`Bar.Zoom.cppm` 默认位置。
9. 吸附/果冻：dock 中心调用改读 `mainBodyHeight`；`Interaction.cpp:9335`；视觉上限按 metrics。
10. 底栏提示与步骤栏分离（`Bar.BottomDock.cppm:296-335`、`Bar.ConstructionPrompt.cpp:188-191`）及容量冻结覆盖。
11. headless：配置策略、设置页行、metrics、ListLayout Compact、bottom dock Compact；形状内嵌测试增步骤栏分离用例。

### C2 属性窗与面板

1. `attachedGap` 替换四处 10 DIP（Large 数值不变）。
2. 橡皮：`EraserAttributeLayoutInput.metrics`、按钮/自动按钮/分割线/视觉种类；验证预览裁剪与命中。
3. 撤回：35×35 面板、`StandardOneOne`。
4. 形状：GeometryGrid Compact（12 列、1×1、滚动步长、视觉种类、图标 20）。
5. More：GroupedGrid Compact 列数、定位相对主栏边、视觉种类。
6. 测试：橡皮离屏 Compact 场景、`eraser_attribute_tests`/`recall_attribute_tests` Compact 用例、形状测试 Compact 网格。

### C3 状态指示器

1. 帧首命令 drain 与 `ResolveCompactMainTextDemand`。
2. 主按钮变宽（远离主栏方向、主栏不动、居中保持）、Logo 外端锚定、文字区绘制。
3. 小尺寸停靠参数、飞行路径、Fan 半径按 35×35；点击路由（变宽主按钮 → Expire；单方块 → 重新变宽）。
4. `DrawRing` 光晕遮罩缓存键核实；模式切换归一。
5. `status_indicator_tests` Compact 用例。

### C4 翻页控件

1. 参数化 `ResolveBarPptStandardPaginationGeometry`/`ResolvePptWidgetContracts`；删除旧 compact 常量、开关与分支。
2. `Bar.Scene.cpp` Configure Compact 映射与视觉种类、横向测宽条件。
3. `PageControl` 尺寸函数化、模式快照、布局缓存身份、位置重新夹取；`AssignLoadState` 与 `PageNumber` Compact 规则。
4. 更新 `page_control_tests`、`whiteboard_ui_tests`、`PageControl.cpp:4072-4086` 离屏断言为按模式；新增 Compact 用例。

### C5 悬停提示

1. 输入线程目标发布（主栏、More、橡皮、撤回、形状），真实鼠标门禁与清除点。
2. 渲染线程 500 ms 判定、跟随定位与边界夹取、绘制登记与常驻容量预留。
3. headless：定位/夹取纯函数用例；门禁逻辑用例。

### C6 启动预览

1. Config 新字段 `CachedStartupBarWidthDipCompact`；Mini 读取加入 `SizeMode`。
2. StartupPreview 按模式高度/折叠外形/最小宽度；首帧宽度按模式写回。
3. `startup_preview_state_tests`、`config_metadata_tests` Compact 用例。

## 2. 验证命令

每个子任务完成后（最小范围）：

```powershell
& "<VS>\MSBuild\Current\Bin\arm64\MSBuild.exe" InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64 -m
```

- 超时 ≥ 5 分钟；检查退出码与首个错误。
- `InkeysHeadlessTests.exe --no-window`（只跑受影响的测试组，最终全量一次）。
- `Inkeys.exe --ui3-golden-dump <目录>`：35 场景与基线逐字节一致（C1、C2、C4 必跑）。
- 相关内嵌自测：`--bar-eraser-offscreen-test`（C1、C2）、`--ui3-geometry-attribute-test`（C1、C2）；用 `& exe | Out-File` 启动，避免自检参数尾空格误启动产品。
- i18n：`pwsh Scripts/i18n.ps1 check`（C1）。
- 修改的 `.cpp/.cppm/.h` 保持 UTF-8 BOM + CRLF；提交前检查 `git diff` 无意外改动。
- GUI 实机验证由用户执行，不擅自启动产品窗口。

## 3. 评审关口

- G1：C1 完成后，确认大尺寸 golden 不变、小尺寸主栏可热切换，再开始 C2-C6。
- G2：C4 删除旧分支前后各跑一次 page control 测试与 golden。
- G3：全部子任务完成后父任务集成检查（AC2、AC4、AC12）与 spec 更新（design §14）。

## 4. 风险文件与回滚点

- 高风险：`Bar.RenderLoop.cpp`（主按钮/吸附/容量）、`Bar.Interaction.cpp`（拖动、悬停）、`PageControl.cpp/.cppm`、`Bar.StatusIndicatorModel.cppm`。
- 回滚点：每个子任务一个提交；C4 的旧分支删除与新布局同一提交。
- `Other.Config.cppm:591` 一行三个 `X(...)`，新增字段另起一行，不重排。
- `GeneralDraft` 位置聚合初始化：新字段放默认值区域。
