# 独立 Magnification 连续捕获与 PoC

## 目标
独立实现 MagCaptureService、共享 Mag runtime lease、图像有效性、超时与重复启停、[ScrollFollow]基础日志，并在现有 Headless 工程中提供 no-window 单测及显式真实 PoC。

## 需求来源
../10-21-scroll-follow-v1/requirements-source.md 与父prd。用户已明确批准本任务树及design。

## 验收
必须真实验证源图案变化、Host内容不可见、排除叠加窗口、静止帧与重复启动/停止，记录耗时/有效性/清理。失败在Mag方案内调查，不开始tracker/产品接入。

## 边界
拥有 Graphics/MagCaptureService.*、必要的 MagnificationRuntime.*、IdtMagnification.cpp/.h 的运行库寿命窄修改、新 capture tests 与现有工程登记。不要修改任何 Draw3/Bar/Tracker 生产逻辑。
遵守父任务批准的feature/move、GUI授权、保守互斥、不commit/push和编码/EOL约束。
