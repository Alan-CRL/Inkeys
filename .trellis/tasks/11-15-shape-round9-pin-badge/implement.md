# 执行计划

1. Research（已完成）：research/r3-outside-contact.md；用户随后决定不做拖动开画。
2. 通用角标组件 + 主栏几何按钮接入（R1），补离屏测试：显隐动画、倒转换边、bounds 覆盖旋转。
3. 固定状态模型 `shapePinnedPreset` 与 Draw3 同步（R2 状态部分），补状态测试。
4. 抽取共享 loading 环路径，新增 progress 模式；页码环行为回归不变。
5. 几何面板长按 + 进度环 + 面板角标（R2），补离屏测试：中止、完成、Up 不触发短按、直线/箭头不变、再次长按取消。
6. R3 收窄：`RestoreToolAfterShapeEditing` 固定时跳过恢复入口工具，补工具状态测试。
7. InkeysRepo.sln Debug|ARM64 构建；编辑器、几何面板、工具状态、核心、原生输入离屏回归；diff/编码检查。
8. 更新 shapes.md 规格；等待用户实机验收后再按授权提交。

回滚点：步骤 2、5、6 各自独立可撤销。

## 中间提交
2026-11-15：用户要求先提交当前改动，随后继续修复实机发现的 bug；任务保持 in_progress，不推送、不归档。
