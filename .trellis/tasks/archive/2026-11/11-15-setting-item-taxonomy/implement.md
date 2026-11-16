# implement.md — 设置项分类树重构

> 执行前提：`prd.md`、`design.md` 经用户评审，`task.py start` 已执行。构建环境规则见全局 CLAUDE.md Part 2（Solution 构建、ARM64 原生 MSBuild、超时 ≥5 分钟、检查退出码）。不 commit、不 push，除非用户另行授权。

## 阶段 0 — 基线

- [ ] `git status` 干净；记录当前分支 `chore/setting-item`。
- [ ] 完整构建 `InkeysRepo.sln` `Debug|ARM64`，运行 `InkeysHeadlessTests.exe --no-window`，记录基线结果（便于区分既有失败）。
- [ ] 读 spec：`settings-attention-notes.md`、`configuration-i18n-and-assets.md`、`setting-motion.md`、`settings-performance-and-drawing.md`、`rendering-and-ui.md`。

## 阶段 1 — 导航框架（旧内容仍在原处可见）

- [ ] 新增导航表（`Setting.NavigationTree.h`）：分组、子页、i18n 键、图标；新增 `PageId` 18/19/20 与各页具名 section 枚举。
- [ ] `NormalizeDestination` 实现 design 2.2 的重定向表；`DestinationForPage` 保持。
- [ ] `NavigationState` 泛化为按分组数组（expanded/reveal/lastSection/childBounds/flyoutChildBounds）；`NavigationMiddleItemCount` 与高度计算由表推导。
- [ ] `Setting.Shell.cpp`：中部项、分组渲染、紧凑 flyout、键盘导航、指示条全部改为遍历导航表；「通用」行为保持等价。
- [ ] `Setting.cpp` 路由：`pageSubpage` 统一来自 `destination.section`；Draw 页叠加 `detail`（design 2.4）；`SettingOpenTarget`/`HomeAction` 目标重映射。
- [ ] 新增页面先挂空实现（只 `PageHeader`），旧页面内容暂不动。
- 验证：构建通过；`setting_design_tests` 导航/连续帧用例（先把枚举引用改到新表）通过；手工无 GUI 要求，使用 `--no-window` 专项。

## 阶段 2 — 内容迁移（整体搬运，不改业务逻辑）

按下列顺序，每步一小提交点（若用户授权提交）：

1. [ ] 日志与配置·配置：搬 `tabConfiguration` 分支；日志空页。
2. [ ] 按键与快捷键：搬 `Shortcuts` 分支，标题键更新。
3. [ ] 窗口与显示器：窗口（Position 全部 + 主栏布局 + 缩放）、行为（自动收起/置顶/右键关闭）、显示器空页。
4. [ ] 通用：基础（选择与穿透 → 交互模式迁入 → 启动 → 崩溃处理）、外观重排（主题→动画→光影→透明度，去掉缩放区）；删除已搬空的 Position/Interaction/Storage/Shortcuts 渲染分支与枚举值。
5. [ ] 绘制拆页：Effects（预测、笔锋、激光粒子）、Interaction（多指→多指激光笔→断触接续→橡皮入口→光标入口→白板惯性→触觉）；处理共用 `SetGeneralDrawingOptions` 调用（design 3，需 helper）；详情页返回目标改为 Interaction。
6. [ ] 侧栏顺序：组件移到插件前。

## 阶段 3 — i18n

- [ ] zh-CN 基准新增键；`Scripts/i18n.ps1 sync`，补齐其余 19 种语言占位/翻译（按规范）；更新 snapshot；重新生成 `IdtI18nKeys.g.h`。
- [ ] 清理无活动引用的旧键；`Scripts/i18n.ps1 check` 通过。

## 阶段 4 — 测试与收尾

- [ ] 更新 `InkeysHeadlessTests/setting_design_tests.cpp`、`setting_home_warp_tests.cpp`：子页循环遍历导航表；断言重定向表、空页、绘制两页共用选项不互相覆盖、展开/收起/flyout 位置。
- [ ] 逐项核对「设置项盘点」：现有每个设置项恰在一处出现（可写脚本/测试枚举 `SettingRow` ID 与迁移前清单比对，ID 不变即满足）。
- [ ] 完整构建 `Debug|ARM64` + `InkeysHeadlessTests.exe --no-window` + 受影响专项（`--setting-attention-only --no-window` 等）。
- [ ] `git diff` 检查：无无关改动、编码/换行保持、无意外 EOL 变化。
- [ ] 更新 spec：新增/修改设置导航规范（导航表为唯一数据源、重定向表、空页约定、分类原则），写入 `.trellis/spec/native-desktop/`。
- [ ] 仅在用户授权后提交。

## 回滚点

- 阶段 1 完成后可单独保留（行为等价）；阶段 2 每步可独立回退。

## 执行状态（2026-11-16）

- 阶段 0–4 已完成：基线（构建 + Headless 全绿）→ 导航表/框架 → 内容迁移 → i18n（20 语言 `check` 通过）→ 测试迁移并新增导航树/分组断言。
- 验证：完整 `InkeysRepo.sln` `Debug|ARM64` 构建 0 错误；`InkeysHeadlessTests.exe --no-window` 全量通过；`--setting-general-only`、`--setting-attention-only` 通过。
- 规范：新增 `.trellis/spec/native-desktop/settings-navigation.md`，并订正 3 处旧描述。
- 实现与计划的差异：导航状态按分组数组化而非保留 `generalExpanded` 等字段；绘制页的恢复提示（工作区/画布恢复）在两个绘制子页都显示；`RenderGeneral` 拆为按子页的独立函数。
- 未做：真实桌面 GUI 验收（按全局规则未启动窗口）；未提交。
