# 实施计划：手掌橡皮大小自定义（R2–R4、R6）

前置：shapes（含 R1 手掌形状）与 blocking 已在工作区（未提交）；基线快照 `refs/inkeys-baseline/custom-start`。

分工：Codex（`gpt-6.1-sol`，High，`service_tier=fast`，`workspace-write`）实现第 1–6 步；Claude 审查、补全 19 语译文、完整构建与测试（第 7–8 步）。

1. 配置 5 项 + 严格校验 + 默认；`IdtState` 读写；`IdtMain.cpp` config-boundary 用例（design §1）。
2. `PalmEraser::Settings` 扩展 + `NormalizeSettings`；`State::Begin` 锁存生效形状（design §2）。
3. 纯函数 `ContactEraserDiameter` + `Observe` 接入；Headless 数值用例：12 种组合 × {100×100、160×90、极端长宽比 3.5:1}，倍率 25/100/300 线性，默认组合与 `√(WH)` 逐位一致（design §3）。
4. 三种粗细方式接入 `PromotePalmRuntime` 及运行时；`--draw3-palm-eraser-test` 覆盖三种方式、全局自动粗细开/关、续接与工具转换（design §4，含停止条件）。
5. 屏蔽范围矩形判定 + 用例（圆形原用例保持）（design §5）。
6. 设置 UI 分组 + 条件显示 + 保存；`setting_design_tests.cpp` 覆盖条目可见性、下拉选项随形状切换、倍率重置；zh-CN 文案（design §6）。
7. Claude：`pwsh -NoProfile -File Scripts/i18n.ps1 sync` → 补译 19 语 → check。
8. 验证：ARM64 原生 MSBuild 完整构建 `InkeysRepo.sln` Debug|ARM64；`--draw3-palm-eraser-test`、`--config-boundary-test`、`--setting-eraser-only`、`--palm-eraser-only`、`--no-window` 全量；更新 spec `palm-eraser.md` 与本任务 validation.md；实机/GUI/Win7 未测列出。

回到规划的条件：笔速控制器无法在不改 SpeedEraser 对外合同的前提下复用（先按固定默认大小实现并报告）；设置控件无法复用导致需要新布局系统。
