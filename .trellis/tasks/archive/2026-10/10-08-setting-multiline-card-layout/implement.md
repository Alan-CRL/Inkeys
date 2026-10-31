# 实施计划（已获实现授权）

## 0. 实现门禁

- [x] 用户要求创建规划任务，已建立父子关系并将本会话指向本任务。
- [x] 保存此前试做补丁并撤回本轮产品源码、测试和规范差异。
- [x] 写入 PRD、设计、研究和后续上下文。
- [x] 用户审阅规划并明确授权实现，见 research/authorized-implementation.md。
- [x] 用户明确授权后执行 task.py start，状态已改为 in_progress。

实现门禁已满足，按本计划派发 implement/check 并完成验证。代理不 GUI/push；用户已授权实现 commit 及本子任务收尾，父任务保留原有进行中状态。

## 1. 确定预算和过渡参数

- [x] 阅读最新 native-desktop 规范，尤其实际文件的设置行合同（文件较大，不能只依赖被截断的上下文注入）。
- [x] 用真实字体/语言和代表性操作预算核对说明阈值、回程余量及极窄回退。
- [x] 定义可验证的中间帧避让、文字完整包络、卡片占位和反向行为。

## 2. 公共布局实现

- [x] Trellis implement 子代理按当前任务上下文实现最小共享行布局。
- [x] 接入常驻多行说明及动态下方通栏布局；极窄控件换行。
- [x] 接入稳定行态和已有动画偏好/恢复时钟；保证单一操作回调。
- [x] 置顶间隔用既有完整国际化说明，移除仅该设置的独立详情块，逐项核对原七选项与副作用。

## 3. 回归与独立检查

- [x] 添加真实 Controls/DrawData 连续帧用例：窗口宽度/操作预算变化、中途反向、临界抖动、动画关闭/速率、首次显示/隐藏恢复。
- [x] 核对标题/说明/图标/操作不相交，说明字形未截断，当前卡片和下一卡片占位完整。
- [x] 验证滑动条、按钮组、按钮加下拉框及每帧操作回调次数/业务值。
- [x] Trellis check 子代理独立检查并修复必要问题。

## 4. 构建和验证

- [x] 动态定位 Visual Studio ARM64 原生 MSBuild；完整 InkeysRepo.sln Debug|ARM64，同一 PowerShell invocation 规范化 PATH，超时至少 5 分钟。
- [x] 如遇本机分页文件/内存压力，可仅在命令中使用 /m:1 /p:CL_MPCount=1，不改项目配置或源码绕过环境错误。
- [x] 成功构建后执行实际 InkeysHeadlessTests.exe --no-window；不创建窗口。
- [x] 运行 Scripts/i18n.ps1 check、编码/换行和 git diff --check，区分源码与环境错误。

## 5. 交付

- [x] 更新与本次布局直接相关的规范/验证记录，说明无窗口检查与实际视觉验收的边界。
- [x] 默认保留未提交代码，明确授权后才 commit/push；不自动结束父任务。

本计划已按明确授权执行，当前实现重新构建与完整无窗口回归均通过，详见 research/validation.md 和 research/final-review.md。实现 commit 5aaf7690 已提交；2026-10-09 用户完成人工验证并授权结束本子任务及提交收尾记录，见 research/manual-acceptance.md。归档本子任务，父任务继续 in_progress，不 push。此前试做构建不计入本任务验收；历史边界见 research/suspension-record.md。
