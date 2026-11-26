# 执行计划

分工：Claude 写实现与测试、构建、跑门禁、逐行核对 diff；Codex（`gpt-6.2-sol`，high，fast，read-only）做一次独立审阅。源文件保持 UTF-8 BOM + CRLF（用 Edit 或 `perl -pi`，不用 `sed -i`）。

## 0. 基线

- [x] 在 HEAD 上构建 `InkeysRepo.sln` Debug|ARM64（ARM64 原生 MSBuild，超时 ≥ 5 分钟）。
- [x] `--ui3-golden-dump` 导出基线到 `Build/ui3-golden/text-base`；记录 `--ui3-trace-dump` 中 More / 绘制属性开合段的字号字段作对照。
- 自检启动用 `& exe --flag | Out-File`，不用带尾空格的 `Start-Process` 参数。

## 1. 实现（按 design.md §3）

- [x] S1 `Bar.EraserAttributeMotion.cppm`：`BarAttributePanelFrame::targetPose` → `expandedPose`，按几何进度 1 求值，父级组合 `expandedPose`；改名同步到 `Bar.EraserAttribute.cpp`、`Bar.RecallAttribute.cpp`、`Bar.GeometryAttribute.cpp`、`Bar.Scene.cpp`（含 trace 字段）。
- [x] S2 `Bar.ContentInstance.cpp`：More `fontTargetScale` 取展开态倍率；注释写明后续用户展开倍率的落点。
- [x] S3 `Bar.RenderLoop.cpp`：`fontPanelTargetScale` 取展开态；笔型 / 粗细显示 / 档位数字三处设置 `fontTargetSize`。
- [x] S4 `UI3.ColorPicker.cpp`：`fontPickerTargetScale` 取展开态。
- [x] 每处加简短中文注释：字号取整只看展开态，收起端不参与取整。

## 2. 测试

- [x] `Bar.EraserAttribute.Test.cpp` More 用例：`fontTargetScale` 改传展开态；新增断言——收起与反向重开全程 `实际字号 == 期望字号 × 当前 scale`（1e-5），收起段修正系数不变。
- [x] 生产路径：至少一个基于 `BarAttributePanelFrame` 的属性窗（橡皮）开→关→开，断言 `fontCorrection.Target()` 全程不变、关后重开无字号尾段（`FontAnimationsActive()` 随面板落定而结束）。
- [x] 绘制属性窗：请求收起后，笔型 / 粗细显示 / 档位数字的 `fontTargetSize` 等于展开态字号。
- [x] 新增引用的符号逐个 grep 确认存在；检查新增断言不破坏同文件既有用例的定位假设。

## 3. 门禁

- [x] 完整 solution 构建通过。
- [x] `--bar-eraser-offscreen-test`、`--ui3-geometry-attribute-test`、`--ui3-host-offscreen-test`（含组件页预览复刻检查）、`--ui3-attribute-dirty-test`、`--ui3-shape-editor-test` 通过。
- [x] `--ui3-golden-dump` 与 `text-base` 逐字节一致（静止画面不应变化）；如有差异逐项解释。
- [x] InkeysHeadlessTests `--no-window` 通过。
- [x] `git diff` 检查 BOM / CRLF / 无整文件差异。

## 4. 审阅与收尾

- [x] Claude 自审 diff → Codex 只读审阅 → 修复。
- [x] 更新 spec：`native-desktop/rendering-and-ui.md` 字号节（“父/子 panel 用目标 pose 组合”改为“展开态 pose”；新增开合式面板与后续用户展开倍率的约定）；`Bar.Scene.cpp` trace 字段语义变化写入。
- [x] 实机观感（真实主栏 + 组件页预览，100%/125%/150%）交给用户验收；Win7 / 兆芯在父任务集成时一并验。

## 回滚点

- 每个 S 步骤独立，可单独回退；整体一个 feat 提交。

## 执行记录（2026-11-26）

- 基线：HEAD 000b830c 构建通过；golden `Build/ui3-golden/text-base` 35/35，trace 16/16。
- 实现：S1–S4 按 design.md §3；产品 6 文件约 +20/-10 行。
- 测试：More 辅助用例改传展开态并加逐帧等比断言；headless `ResolveBarAttributePanelFrame` 开合全程 expandedPose 恒等展开态；属性脏区测试新增绘制属性窗收起后字号目标、More 收起前后字号目标不变两条生产路径检查（后者为采纳 Codex 审阅意见补充）。
- 变异验证：S1+S3 回退 → headless `[EraserAttribute] failures=31`、属性脏区 `FONT-TARGET-COMPACT`（目标 1.6–2.1）；More 生产回退 → `FONT-TARGET-MORE collapsed target=2.08 expected=13`；均已还原。
- 门禁：完整构建通过；eraser-offscreen / geometry-attribute / host-offscreen / attribute-dirty / shape-editor / headless 全部 0；golden 与基线逐字节一致；trace 也一致（现有 trace 不记录字号字段，开合中间帧字号变化未被 trace 覆盖）。
- Codex 审阅（`research/codex-sol-review.md`）：主栏修复成立；“共用展开倍率入口”留给用户倍率任务（落点已写入 spec）；形状编辑浮条按当前 DC 缩放取整为基线既有缺口，不在本任务范围，另提后续任务。
- 实机：用户 2026-11-26 实机验收通过（真实主栏 + 组件页预览）。Win7/兆芯在父任务集成时验。
