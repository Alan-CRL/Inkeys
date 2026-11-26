# 执行计划

分工：Claude 写实现与测试、构建、跑门禁、核对 diff；Codex（`gpt-6.2-sol`，high，fast，read-only）独立审阅一次。源文件保持 UTF-8 BOM + CRLF。

- [x] 0 基线：HEAD 构建；`--ui3-golden-dump` 导出 `Build/ui3-golden/shape-base`。
- [x] 1 布局：计算并保存 `fontTargetScales[4]`（design §2.1）。
- [x] 2 绘制：`Draw` 开头 `BeginFontFrame(now, {0,1,false,true}, scale)`；每层内容前 `SetFontTargetScale`。
- [x] 3 测试：shape-editor 测试加开合逐帧修正目标恒定用例；变异验证后还原。
- [x] 4 门禁：完整构建；shape-editor / eraser-offscreen / geometry-attribute / host-offscreen / attribute-dirty / headless；golden 与 shape-base 逐字节一致。
- [x] 5 审阅：自审 → Codex 只读审阅 → 修复。
- [x] 6 spec：`rendering-and-ui.md`“开合式弹出面板”条补充形状编辑浮条（独立 renderer 必须显式设置展开态目标）。
- [x] 7 实机：用户确认后提交。

回滚：单文件改动，一个 fix 提交可整体回退。

## 执行记录（2026-11-26）

- 基线：HEAD dc13ae10 的构建产物，golden `Build/ui3-golden/text-fix2`。
- v1：按 design §2 实现；首版用例误把修正类默认系数 1 当首样本，临时诊断确认修复后目标恒定（13×1.25 → 16/16.25），改为只采样已绘制文字后通过。
- Codex 审阅 v1：取色器展开倍率随收缩中的下层矩形变化（窄屏回退）为阻塞项，工具栏 fit 取 max(目标,当前) 为建议项；v2 改为展开态链（design“审阅后修订”）。
- Codex 复审 v2：layerPose 数值等价、展开态链与静止姿态一致；其“FLOAT 与 double 在整数边界差 1 px”项不采纳——spec 规定终点用显式 double 目标、禁止用 FLOAT 矩阵反推（见 rendering-and-ui.md）；测试补强建议已采纳。
- 测试：工具栏文字开合逐帧修正目标恒定、SidesEntry 精确等于展开态 floor(T)/T、开合各方向均采到运动帧、落定姿态等于展开态倍率；窄工作区取色器回退在收起中目标恒定且等于静止实际倍率、确有下层收缩帧。
- 变异：去掉 SetFontTargetScale + 取色器改回当前下层矩形 → 开 66 / 合 26 / 精确值 36 / 取色器 14 处失败；均已还原。
- 门禁（v2）：完整构建；eraser-offscreen / geometry-attribute / host-offscreen / attribute-dirty / shape-editor / headless 全 0；golden 与 text-fix2 逐字节一致，trace 一致。最终版仅补测试，shape-editor 复跑 failures=0。
- 实机：用户 2026-11-26 实机验收通过。
