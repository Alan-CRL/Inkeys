# C4 实施计划

实现者：Claude。Astra 运行 C2/C3 期间不构建、不跑产品测试（共享中间目录与 exe）。

## 白名单

- `Inkeys/Inkeys/UI/Setting/Setting.cpp`（仅 `tabComponent` 分支与组件页会话状态）
- `Inkeys/Inkeys/UI/Setting/Setting.Controls.h`、`Setting.Controls.cpp`（`TabStrip`、`ComponentCard`）
- `Inkeys/src/i18n/*.jsonc`、`Inkeys/IdtI18nKeys.g.h`（脚本生成）、`Scripts/i18n.zh-CN.snapshot.jsonc`（若脚本更新）
- `InkeysHeadlessTests/setting_design_tests.cpp`（组件页相关断言）

## 步骤

1. 文案：zh-CN 增加 design §5 的键；`pwsh Scripts/i18n.ps1 sync` 生成键头并标记其他语言；补译 19 种语言（可交给 Claude 子代理，逐条保留产品名）。验证：`pwsh Scripts/i18n.ps1 check` 通过。
2. 控件：`TabStrip`、`ComponentCard`。验证：构建通过。
3. 页面：替换 `tabComponent` 内容为快捷块、重置块、添加组件区（Tab + 卡片网格 / 条目），接 C1 入口与 `QueueConfigWrite()`。验证：构建；headless `setting_design_tests` 相关断言更新并通过。
4. C3 完成后：插入预览块；卡片/条目拖动源接 C3 会话。验证：手工与 C3 的拖放测试。
5. 全量门禁：golden、几何、eraser、headless。

## 回滚点

每步独立；步骤 3 回滚即恢复 C1 过渡期的 16 个开关。
