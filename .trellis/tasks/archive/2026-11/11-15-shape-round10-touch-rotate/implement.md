# 实施计划：形状第十轮

执行方：Codex `gpt-6.1-sol` High，`service_tier=default`，`--sandbox workspace-write`。两路并行，按文件划分所有权。主 Agent 负责构建、测试和审查。

## 路 X（Bar.ShapeEditor.cpp + Draw3/核心 + 资源）
1. R1：`DrawEditorInputCoverage` 构造期不填包围盒，并加位图 alpha 回归。
2. R4：核心 `RotateShape`、动作枚举与分派、运行时白名单、编辑器按钮/图标/可见性/顺序、`.rc`/`.vcxproj`/`.filters` 登记（SVG 已由主 Agent 提供），zh-CN/en-US 文案；扩展翻转相关测试（UI 动作、核心几何、undo）。
3. R5：样式栏 3s 冻结及到期唤醒，加注入时钟的回归测试。
- 禁止触碰：`Bar.Button.cpp`、`Bar.ConstructionPrompt.cpp`、`Bar.Interaction.cpp`、`Bar.Main.cppm`、`Bar.GeometryAttribute*.cpp`。

## 路 Y（构造提示触摸）
1. R3：触摸白名单加入提示面板，补上 accepted 反馈，并在提示 fixture 里补触摸 probe 回归。
- 允许修改：`Bar.Button.cpp`、`Bar.ConstructionPrompt.cpp`、`Bar.Interaction.cpp`、`Bar.Main.cppm`、`Bar.GeometryAttribute.Test.cpp`（只改提示 fixture 段）、`Bar.RenderLoop.cpp`（如必须）。
- 禁止触碰：`Bar.ShapeEditor.cpp`、Draw3、`SelectionOverlay`。

## 主 Agent
1. 用 pwsh 7 跑 `Scripts/i18n.ps1` 同步其余语言，并生成头文件。
2. 构建 `InkeysRepo.sln` Debug|ARM64。
3. 跑测试：`--ui3-shape-editor-test`、`--ui3-geometry-attribute-test`、`--shape-tool-state-test`、`--shape-editor-native-input-test`、`--ui3-input-worker-test`、`--draw3-shape-core-test`，以及 inkStrokeModelerTestTests 的形状测试（如有对应目标）。
4. 审查 diff（检查编码与 CRLF），同步 spec `shapes.md` / `rendering-and-ui.md`。
