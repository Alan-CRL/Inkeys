# Implement — 场景进入自动居中底栏与位置设置页

> 全解决方案 MSVC 构建，本机 ARM64：`Debug|ARM64` + arm64 MSBuild，整个 `InkeysRepo.sln`（见 PRD 之外的项目约定）。

## 阶段 A：纯逻辑与配置

- [ ] `UI/StartupPlacement.h`：扩展枚举 3–6、`NormalizeStartupPlacement`、类型/位置双向映射、顶栏/悬浮底栏居中落点；补 headless 用例。
- [ ] `Bar.BottomDock.h`：`ResolveBarWorkspaceEntryTakeover`、`ResolveBarWorkspaceEntrySide`；单测。
- [ ] `Other.Config.cppm`：新增 `Workspace.Ppt/Whiteboard.{AutoDockMask,Direction}`；`Other.Config.cpp` 的规范化/读取（对照 `UI.Bar.StartupPlacement` 处理，`:602`）。

## 阶段 B：渲染线程接入

- [ ] `Bar.RenderLoop.cpp` Enter 分支（~13585 之后）：读取目标场景配置快照、分类、命中接管（见 design §3）。
- [ ] 核对放置分支 `:2840-2940`：折叠命中保持展开、方向覆盖、跨屏中心；`:2926` fold 写入条件。
- [ ] 启动路径：`Bar.Initialization.cpp`、`Bar.RenderLoop.cpp:2856-2900`、`:13722`、`Bar.Zoom.cppm:180` 对新枚举适配；顶栏贴顶面板翻向与视口裁剪。
- [ ] `StartupPreview` 同步。

## 阶段 C：设置页

- [ ] `Setting.Design.h` 加 `GeneralSection::Position`；`Setting.Shell.cpp` 子项键/图标；`Setting.Pages.h/.cpp` 草稿、事件、`RenderGeneral` 位置页、`RenderWorkspaceDockRow`。
- [ ] `Setting.cpp` 草稿装载/写回（`:3570`、`:3667` 一带）、从基础页移除旧“启动后位置”。
- [ ] i18n：新增键 + 20 语言，经 `Scripts/i18n.ps1` 生成链（参考归档任务 `11-02-i18n-twenty-locales` 的 research 与 `.trellis/spec` i18n 章节）。

## 阶段 D：验证

- [ ] 离线/headless：`--ui3-workspace-centering-test`（补 4 状态×勾选/未勾选×方向、折叠命中展开、Leave 还原）、`--ui3-bottom-dock-transition-test`、`--eraser-attribute` 相关不回归、设置页相关测试。
- [ ] 设置页截图核对（多语言、DPI），顶栏落点与任务栏上置场景。
- [ ] 真实 PPT/白板 GUI 验收由用户进行（软件通过不代表真实 ULW/DWM 验收，沿用既有声明）。

## 阶段 E：spec 与收尾

- [ ] 更新 `rendering-and-ui.md:993`、`whiteboard.md:28`、`native-session-ui3.md:52`，新增位置页与配置键说明。
- [ ] Phase 3.3 spec 更新、3.4 提交（不加 Claude 署名）。

## 风险/回滚点

- Enter 分支与 `whiteboardDockAnimationActive` 所有权交互；快速反向（Enter→Leave→Enter）需沿用既有 ABA 保护。
- 顶栏贴顶的面板翻向、`ClipBarViewportForTaskbarEdge`；任务栏在上方时的 work 区取值。
- 默认值改变用户可感知行为（普通底栏进入 PPT 将居中），需在 spec/变更说明中明确。
