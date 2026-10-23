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

## 本轮继续实施（2026-10-21）

用户已批准完成正式取帧及全部后续阶段，上一轮仅诊断的范围限制结束。本轮验证仅无窗口或完全不在屏幕显示的HWND，禁止Computer Use/前台/可见测试；必要可见验收时停止。当前授权见[implementation-authorization.md](../10-21-scroll-follow-v1/research/implementation-authorization.md)，其优先于本文旧轮次边界。原定格Mag的无callback基础验证见research/freeze-printwindow.md（捕获子任务）；后续正式路径须分别验证。本轮不自动commit/push。


## 2026-10-22 实际画面测试继续

用户现已允许创建窗口、操作电脑和截图，但不使用Computer Use。此前本轮不可见限制属于上一阶段历史，当前以父research/live-test-authorization.md为准；不自动commit/push、不结束任务。先正式worker取帧，成功后真实产品/系统窗口交接，逐项记录而非沿用seam结果。
