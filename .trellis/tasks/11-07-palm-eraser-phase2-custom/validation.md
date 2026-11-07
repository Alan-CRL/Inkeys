# 验证记录：手掌橡皮大小自定义（R2–R4、R6）

日期：2026-11-07；分支 bugfix/hand-erase；对照快照 `refs/inkeys-baseline/custom-codex`（未提交，叠加在 shapes/blocking 未提交改动之上）。

## 分工

- Claude（Opus 5.5）
  - 规划工作：按实际代码重写 design.md 和 implement.md，修正 prd 中的错误备注。
  - 独立审查 Codex 的 diff，包括：定尺公式与等效换算、三种方式的分支、默认大小的换算路径、笔速复用、屏蔽范围的当前尺寸、续接不再按形状过滤。
  - 修正两处测试：
    - 原有手掌页测试把页面上第一个 i 当作边擦边写提示，新分组也带 i，改为遇到粗细方式说明即停止扫描。
    - 新增的倍率重置测试误用 `##reset` 作为 ID，改为与透明度用例相同的按位置点击。
  - 完成构建与全部测试。
- Codex `gpt-6.1-sol`（High，Fast，workspace-write）：实现 design §1–§6，exit 0，没有触发停止条件。
- Codex `gpt-6.1-terra`（Medium，Fast，workspace-write）：补译 19 种语言。第一次只完成 4 种，剩下 15 种分三路并行完成。

## 结果

| 项目 | 结果 |
| --- | --- |
| i18n（22 个新键 × 20 种语言） | 808/808 PASS |
| `InkeysRepo.sln` Debug\|ARM64（ARM64 原生 MSBuild） | exit 0 |
| `Inkeys.exe --draw3-palm-eraser-test` | PASS |
| `Inkeys.exe --config-boundary-test` | PASS |
| `Inkeys.exe --draw3-renderer-upload-test` | PASS failures=0 |
| `InkeysHeadlessTests.exe --palm-eraser-only` | cases=2053 failures=0 |
| `InkeysHeadlessTests.exe --setting-eraser-only` | settings=0 speed=0 attributes=0 palm=0 |
| `InkeysHeadlessTests.exe --no-window` 全量 | exit 0，stderr 为空，无 failed |
| 编码与换行 | 全部改动文件为 CRLF，`git diff --check` 无告警 |

## 已知行为变化

- 竖长矩形手掌的默认尺寸改为“矩形等面积”（D10），比原先的内切椭圆等面积大，面积比为 4/π。
- 手掌续接不再按普通橡皮形状过滤。原因是手掌选了独立形状后，旧的过滤会让续接失败。

## 未验证

- 设置页在真实 GUI 中的外观（未启动应用）。
- 实机大屏上的三种粗细方式、笔速手掌、矩形屏蔽范围。
- Win7 运行。
- 部分语言的术语（韩语“직사각형”、繁中“直長/豎長矩形”）需要母语使用者复核。

## 验收补充（2026-11-07）

- 文档：修正 blocking 的 prd R6 与 validation、shapes 的 validation、`draw3-cursors.md` 中“矩形屏蔽留给 custom”的过时描述；custom prd 控件名改为 `InlinePreviewSelector`；父任务 D12 注明已按用户复核改为独立分类；父任务与 custom 的状态改为 `in_progress`。
- 新增 AC6 显式断言，位于 `setting_design_tests.cpp` 的 `CheckEraserCursorCard`，覆盖 20 种语言 × 7 种缩放：
  - 三种形状下，粗细方式、大小计算方式、倍率三行纵向按序不重叠，都在页面宽度内。
  - 每个下拉选项（含推荐后缀）的文字宽度 + 40px ≤ 下拉框宽度；不满足时输出 `[PalmSizeFit]` 诊断。
- 断言首次运行发现截断：16 种语言的方法名和 6 种语言的固定方式名超出 320 DIP 的下拉上限，例如德语 496px。
  - 修复：Codex `gpt-6.1-terra`（Medium，Fast）分三路，把 19 种语言的选项改为简短名称，例如英文 “Inscribed-ellipse area”、“Fixed (no speed eraser)”。行描述已说明是“将接触矩形换算”，选项中不再重复。
  - zh-CN 保持用户原定名称，本身就放得下。
- 复测结果：
  | 项目 | 结果 |
  | --- | --- |
  | i18n check | PASS |
  | 完整构建 | exit 0 |
  | `--setting-eraser-only` | 0 失败，无 `[PalmSizeFit]` |
  | `--palm-eraser-only` | 2053/0 |
  | `--draw3-palm-eraser-test` / `--config-boundary-test` / `--draw3-renderer-upload-test` | 均 PASS |
  | `--no-window` 全量 | exit 0，stderr 为空 |
- 待母语复核：俄语、乌克兰语缩写（如 “Фикс.”、“впис.”）。
