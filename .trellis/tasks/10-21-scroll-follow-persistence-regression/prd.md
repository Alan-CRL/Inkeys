# 滚动批注保存与完整回归

## 目标
针对已接入功能补齐UInk往返与编辑历史、ARM64/x64完整构建及真实受控GUI矩阵，记录最终参数/实测/未测和Console方法。

## 需求来源
../10-21-scroll-follow-v1/requirements-source.md 与父prd。用户已明确批准本任务树及design。

## 验收
依赖产品接入。A->scroll->B->eraser/Undo/Redo->Clear或Exit保存->加载->再scroll；完整检查与回归按父implement。

## 边界
拥有验证工具与测试、任务报告/必要bugfix；任何生产修复保持最小diff，不替换已有保存流程或更改UInk版本。
遵守父任务批准的feature/move、GUI授权、保守互斥、不commit/push和编码/EOL约束。
